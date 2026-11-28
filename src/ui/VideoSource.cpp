#include "VideoSource.h"
#include "VideoReader.h"
#include "../core/AudioOps.h"
#include "../core/CrashReport.h"
// 3.73.0: the report CMakeLists.txt writes about the video reader (its build folder's generated/, on the include path), when there is one
#if defined (__has_include)
 #if __has_include ("VideoBuildReport.h")
  #include "VideoBuildReport.h"
  #define FERMATA_HAS_VIDEO_REPORT 1
 #endif
#endif
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// 3.68.0: a video file for fermata's video lane and picture window, over the computer's own decoder (VideoReader.h). See VideoSource.h for
// what fermata relies on. Written with Claude Fable (26 November 2026).
//
// THE PICTURE (frameAt). One thread per Source decodes; the message thread never waits for it. Between them, under one lock held only for
// moments (never while decoding): what is wanted - a time, playing or not, a size - and 'shots', the decoded frames that are ready, in the
// order of their times, ending where the decoder now stands. The decoder's thread looks at what is wanted and does one of three things:
//   - Rest: the frame for the time wanted is there (and, while playing, a third of a second after it; standing, the two frames after it);
//   - Step: decode the next frame - the one wanted is that one or comes within a second, or more are wanted ahead;
//   - Seek: the time wanted is behind the decoder or far ahead: back to the key frame before it, then on from there. The shots are thrown
//     away at once (they no longer end where the decoder stands); the frame last given stays on the screen meanwhile.
// A frame decoded on the way to the one wanted is not copied out of the decoder at all, except the few just before a playhead that stands
// (so a step back by a frame is at once), and - while playing - those that come slowly, more than 60 ms apart: a decoder catching up after
// a jump passes them in a few milliseconds each and none is copied, but on a computer too slow for the file every frame it manages is shown,
// behind the sound by up to a second, then it jumps on to the key frame before the playhead and carries on from there.
namespace td::video
{
namespace
{
std::atomic<bool> hardwareAllowed { true };

constexpr double kEps = 0.0005;                 // times within half a millisecond are the same time
constexpr double kStepOn = 1.0;                 // a frame less than this ahead of the decoder is reached by decoding on, not by a seek
constexpr double kAheadPlaying = 0.35;          // while playing, frames are kept ready this far ahead of the playhead
constexpr double kStaleOk = 1.5;                // playing: a frame this far behind the time wanted is still shown (a slow decoder); older ones are not
constexpr double kStaleStanding = 0.06;         // standing: only the frame wanted, or one a frame or so before it (a scrub shows where it is)
constexpr double kIdleMs = 10000.0;             // nothing asked for this long: the decoder and the frames are let go of
constexpr double kRetryMs = 3000.0;             // a file that could not be opened is tried again this often while it is asked for
constexpr juce::int64 kShotBytes = 96 << 20;    // the frames kept ready take at most this much memory
constexpr int kBack = 10;                       // frames kept before a standing playhead

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
double frameLength (double fps) { return fps > 1.0 ? 1.0 / fps : 0.04; }

/** The held frame of a reader as a picture (empty if it cannot be copied). A plain picture in memory: made on any thread. */
juce::Image imageOf (raw::PictureReader& r, const raw::Frame& fr)
{
    if (fr.width < 2 || fr.height < 2 || fr.width > 16384 || fr.height > 16384) return {};
    juce::Image im (juce::Image::ARGB, fr.width, fr.height, false, juce::SoftwareImageType());
    juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
    if (bd.pixelStride != 4 || ! r.copyFrame (bd.data, bd.lineStride, fr.width, fr.height)) return {};
    return im;
}

Info infoOf (const raw::Format& f)
{
    Info i;
    i.seconds = f.seconds; i.fps = f.fps; i.width = f.width; i.height = f.height; i.codec = juce::String::fromUTF8 (f.codec.c_str());
    i.soundChannels = f.soundChannels; i.soundRate = f.soundRate; i.hardware = f.hardware;
    return i;
}

/** How many lines the frames need for a picture drawn w x h pixels (shape: its width / height): the next of a few steps up, never above 1080. */
int linesFor (int w, int h, double shape)
{
    if (w <= 0 || h <= 0) return 540;
    const double drawn = juce::jmin ((double) h, (double) w / juce::jlimit (0.2, 5.0, shape > 0.0 ? shape : 16.0 / 9.0));
    for (int lines : { 180, 270, 360, 540, 720 }) if (drawn <= lines * 1.06) return lines;
    return 1080;
}

/** What the two threads of a Source share, and the decoder's thread itself. Kept alive by both (the thread may outlive the Source by the
    moment its last decode takes: see retire). */
struct Core
{
    explicit Core (const juce::File& f) : file (f), path (f.getFullPathName().toStdString()), lastAskMs (nowMs()) {}

    const juce::File file;
    const std::string path;

    std::mutex m;
    std::condition_variable cv;
    // ---- under m
    bool quit = false;
    int openState = 0; juce::String openWhy;        // see Source::state
    Info inf;
    double t0 = 0.0; bool haveT0 = false;           // the file's clock at its first frame
    bool asked = false;                             // frameAt has been called
    double wantT = 0.0; bool wantPlaying = false;
    double lastAskMs;                               // when frameAt last asked (at first: when the Source was made)
    int wantW = 0, wantH = 0; double sizeAskedMs = 0.0;
    struct Shot { double t = 0.0, len = 0.0; juce::Image im; };
    std::deque<Shot> shots;                         // decoded frames, by time, ending where the decoder stands
    bool atEnd = false;                             // the decoder has reached the end of the file: shots.back() is its last frame
    Shot shown;                                     // the frame last given
    // ----
    std::atomic<bool> done { false };               // the thread has finished

    void noteFormat (const raw::Format& f, double firstFrameTime)       // (under m)
    {
        inf = infoOf (f);
        if (! haveT0) { t0 = firstFrameTime; haveT0 = true; }
    }

    juce::Image frameAt (double t, bool playing, double& frameTime)
    {
        const std::lock_guard<std::mutex> g (m);
        wantT = t; wantPlaying = playing; asked = true; lastAskMs = nowMs();
        t = juce::jmax (0.0, t);                                                    // (before the first frame: the first frame)
        const Shot* best = nullptr;
        for (auto& s : shots) { if (s.t <= t + kEps) best = &s; else break; }
        // (a frame from long before the time wanted - the decoder is on its way to another place - is not put on the screen for a moment)
        if (best != nullptr && best->im.isValid()
            && (t < best->t + best->len + (playing ? kStaleOk : kStaleStanding) || (atEnd && best == &shots.back()))) shown = *best;
        cv.notify_one();
        frameTime = shown.t;
        return shown.im;
    }

    void run()
    {
        td::crashreport::nameThisThreadW (L"Video decoder");
        {
            const raw::ThreadUse use;
            decode();
        }
        done.store (true, std::memory_order_release);
    }

    void decode()
    {
        std::unique_ptr<raw::PictureReader> rd;
        int rdLines = 0; bool rdAskedHardware = false, hardwareFailed = false, logged = false;
        double pos = -1.0;                    // the time of the frame rd gives next (from the first frame), -1: not known (after a seek)
        bool keepAll = false, keepAllNext = false;
        double deadT = -1.0e300;              // a time for which the file has no frame at all (asked twice: nothing)
        double lastKeptMs = 0.0, failedAtMs = 0.0;

        std::unique_lock<std::mutex> lk (m);
        for (;;)
        {
            if (quit) break;
            const double now = nowMs();
            const bool wanted = openState == 0 || now - lastAskMs <= kIdleMs;
            if (! wanted)
            {
                if (rd != nullptr)                                               // nothing has asked for a while: the decoder and the frames go
                {
                    std::deque<Shot> old; old.swap (shots);
                    atEnd = false; pos = -1.0;
                    lk.unlock(); rd.reset(); old.clear(); lk.lock();
                    continue;
                }
                cv.wait (lk);
                continue;
            }
            // ---- the decoder: opened; opened again for another size, or when the graphics card is switched off / on
            const int lines = linesFor (wantW, wantH, inf.height > 0 ? (double) inf.width / (double) inf.height : 0.0);
            const bool hardware = hardwareAllowed.load (std::memory_order_relaxed) && ! hardwareFailed;
            if (rd == nullptr || rdAskedHardware != hardware || (lines != rdLines && now - sizeAskedMs > 300.0))
            {
                if (openState < 0 && now - failedAtMs < kRetryMs) { cv.wait_for (lk, std::chrono::milliseconds (500)); continue; }
                const bool wantFirst = ! asked || wantT < 1.0;
                const int stateBefore = openState;
                lk.unlock();
                rd.reset();
                std::string why;
                raw::Options o; o.maxHeight = lines; o.hardware = hardware;
                std::unique_ptr<raw::PictureReader> r;
                if (! file.existsAsFile()) why = "the file is not there: " + path;
                else r = raw::openPicture (path, o, why);
                raw::Format fmt; raw::Frame first; juce::Image firstIm; bool got = false;
                if (r != nullptr)
                {
                    fmt = r->format();
                    bool ended = false;
                    got = r->next (first, ended, why);
                    if (! got && why.empty()) why = "it has no picture";
                    if (got && wantFirst) firstIm = imageOf (*r, first);
                    if (! got) r.reset();
                }
                if (got && ! logged)
                {
                    logged = true;
                    juce::Logger::writeToLog ("Video: " + file.getFileName() + " - " + juce::String::fromUTF8 (fmt.how.c_str()) + " (" + juce::String::fromUTF8 (fmt.codec.c_str())
                                              + ", " + juce::String (fmt.width) + " x " + juce::String (fmt.height) + ", " + juce::String (fmt.fps, 3) + " fps); frames "
                                              + juce::String (first.width) + " x " + juce::String (first.height));
                }
                if (! got && stateBefore >= 0) juce::Logger::writeToLog ("Video: cannot show " + file.getFullPathName() + ": " + juce::String::fromUTF8 (why.c_str()));
                lk.lock();
                if (! got)
                {
                    openState = -1; openWhy = juce::String::fromUTF8 (why.c_str()); failedAtMs = nowMs();
                    shots.clear(); atEnd = false;
                    continue;
                }
                noteFormat (fmt, first.time);
                openState = 1; openWhy.clear();
                rd = std::move (r); rdLines = lines; rdAskedHardware = hardware;
                shots.clear(); atEnd = false; keepAll = keepAllNext = false; deadT = -1.0e300;
                const double ft = first.time - t0, fl = first.length > 0.0 ? first.length : frameLength (inf.fps);
                if (firstIm.isValid()) shots.push_back ({ ft, fl, firstIm });
                pos = ft + fl;
                continue;
            }
            if (! asked) { cv.wait_for (lk, std::chrono::milliseconds ((int) kIdleMs / 4)); continue; }      // (opened for the picture window; nothing wanted yet)

            // ---- what to do for the time wanted
            const double t = juce::jmax (0.0, wantT);
            const bool playing = wantPlaying;
            const double fl = frameLength (inf.fps);
            const juce::int64 bytes = shots.empty() ? (juce::int64) 1 : (juce::int64) shots.back().im.getWidth() * shots.back().im.getHeight() * 4;
            const int maxShots = (int) juce::jlimit ((juce::int64) 6, (juce::int64) 40, kShotBytes / juce::jmax ((juce::int64) 1, bytes));
            bool have = false; double newest = -1.0e300;
            if (! shots.empty())
            {
                newest = shots.back().t + shots.back().len;
                have = t >= shots.front().t - kEps && (t < newest - kEps || atEnd);
            }
            enum class Act { rest, step, seek } act = Act::rest;
            if (! have)
            {
                if (std::abs (t - deadT) < kEps) act = Act::rest;                                   // (the file has nothing there: asked twice)
                else if (! atEnd && pos >= 0.0 && t >= pos - kEps && t < pos + kStepOn) act = Act::step;
                else act = Act::seek;
            }
            else if (! atEnd && (int) shots.size() < maxShots
                     && newest < t + (playing ? juce::jmin (kAheadPlaying, (maxShots - 3) * fl) : 2.0 * fl)) act = Act::step;
            if (act == Act::rest)
            {
                cv.wait_for (lk, std::chrono::milliseconds ((int) kIdleMs / 4));
                continue;
            }
            std::deque<Shot> old;                                                   // (frames thrown away are freed below, without the lock)
            if (act == Act::seek) { old.swap (shots); atEnd = false; pos = -1.0; keepAll = keepAllNext; keepAllNext = false; }
            const double t0Now = t0;
            lk.unlock();
            old.clear();

            // ---- decode (no lock held)
            bool ok = true, ended = false; std::string why;
            if (act == Act::seek) ok = rd->seek (t0Now + t);
            raw::Frame fr;
            if (ok) ok = rd->next (fr, ended, why);
            double ft = 0.0, flen = fl; juce::Image im; bool keep = false;
            if (ok)
            {
                ft = fr.time - t0Now; if (fr.length > 0.0) flen = fr.length;
                double tNow; bool playingNow;
                { const std::lock_guard<std::mutex> g (m); tNow = juce::jmax (0.0, wantT); playingNow = wantPlaying; }
                const bool before = ft + flen <= tNow + kEps;                                      // an earlier frame than the one wanted now
                if (! before || keepAll) keep = true;
                else if (playingNow) keep = nowMs() - lastKeptMs > 60.0;
                else keep = ft + flen > tNow - kBack * flen;
                if (keep) { im = imageOf (*rd, fr); keep = im.isValid(); }
            }

            lk.lock();
            if (quit) break;
            if (! ok)
            {
                if (ended)
                {
                    atEnd = true; pos = -1.0;
                    if (shots.empty())                                                             // the end, and nothing kept on the way to it
                    {
                        if (! keepAll) keepAllNext = true;                                         // (once more, keeping every frame: the file's last ones)
                        else deadT = t;
                        atEnd = false;
                    }
                    continue;
                }
                const bool wasHardware = rdAskedHardware && rd->format().hardware;
                shots.clear(); atEnd = false; pos = -1.0;
                lk.unlock();
                rd.reset();
                juce::Logger::writeToLog ("Video: " + file.getFileName() + (wasHardware ? " - the graphics card's decoder stopped (" + juce::String::fromUTF8 (why.c_str()) + "): carrying on without it"
                                                                                      : " cannot be read on: " + juce::String::fromUTF8 (why.c_str())));
                lk.lock();
                if (wasHardware) { hardwareFailed = true; logged = false; }                        // (opened again without the graphics card)
                else { openState = -1; openWhy = juce::String::fromUTF8 (why.empty() ? "it cannot be read" : why.c_str()); failedAtMs = nowMs(); }
                continue;
            }
            pos = ft + flen;
            if (keep)
            {
                if (! shots.empty() && ft <= shots.back().t + kEps) shots.clear();                 // (never out of order)
                shots.push_back ({ ft, flen, im });
                lastKeptMs = nowMs();
            }
            // frames no longer needed: while playing, those before the one showing; standing, all but the few before the playhead
            {
                const double tn = juce::jmax (0.0, wantT);
                const double keepFrom = wantPlaying ? tn - 1.5 * fl : tn - kBack * fl;
                while (shots.size() > 1 && shots[1].t <= keepFrom + kEps) shots.pop_front();
                while ((int) shots.size() > maxShots && shots[1].t <= tn + kEps) shots.pop_front();
            }
        }
        // the end (the Source has gone): the frames are freed here, not by whichever thread lets go of this last
        std::deque<Shot> old; old.swap (shots);
        Shot last; std::swap (last, shown);
        lk.unlock();
        old.clear(); last = {};
        rd.reset();
    }
};

/** The decoders' threads of Sources that have gone: each ends by itself once its last decode returns (a moment; longer on a drive that has
    gone to sleep), so nothing on the screen waits for it. They are joined here - the finished ones whenever another is added, all of them
    when fermata closes (finish), so none is still inside the decoder when the program's own parts are taken down. */
struct Retired
{
    std::mutex m;
    std::vector<std::pair<std::thread, std::shared_ptr<Core>>> list;
    bool closing = false;
};
Retired& retired() { static auto* r = new Retired(); return *r; }                     // (never destroyed: no thread is left unjoined by a destructor)

void retire (std::thread t, std::shared_ptr<Core> core)
{
    auto& r = retired();
    std::vector<std::thread> finished;
    {
        const std::lock_guard<std::mutex> g (r.m);
        if (r.closing) finished.push_back (std::move (t));
        else
        {
            for (auto it = r.list.begin(); it != r.list.end();)
                if (it->second->done.load (std::memory_order_acquire)) { finished.push_back (std::move (it->first)); it = r.list.erase (it); }
                else ++it;
            r.list.emplace_back (std::move (t), std::move (core));
        }
    }
    for (auto& f : finished) if (f.joinable()) f.join();
}

class StreamSource final : public Source
{
public:
    explicit StreamSource (const juce::File& f) : core (std::make_shared<Core> (f)) {}
    ~StreamSource() override
    {
        { const std::lock_guard<std::mutex> g (core->m); core->quit = true; }
        core->cv.notify_all();
        if (thread.joinable()) retire (std::move (thread), core);
        const raw::ThreadUse use;
        stillRd.reset();
    }
    void startThread()
    {
        if (thread.joinable()) return;
        auto c = core;
        thread = std::thread ([c] { c->run(); });
    }
    /** open()'s: the file is opened here and now. */
    bool openNow (juce::String& why)
    {
        const raw::ThreadUse use;
        std::string w;
        if (! ensureStill (320, w)) { why = juce::String::fromUTF8 (w.c_str()); return false; }
        const std::lock_guard<std::mutex> g (core->m);
        core->openState = 1;
        return true;
    }

    Info info() const override { const std::lock_guard<std::mutex> g (core->m); return core->inf; }
    int state (juce::String& why) const override { const std::lock_guard<std::mutex> g (core->m); why = core->openWhy; return core->openState; }
    void wantSize (int w, int h) override
    {
        const std::lock_guard<std::mutex> g (core->m);
        if (w == core->wantW && h == core->wantH) return;
        core->wantW = w; core->wantH = h; core->sizeAskedMs = nowMs();
        core->cv.notify_one();
    }
    juce::Image frameAt (double t, bool playing, double& frameTime) override
    {
        startThread();
        return core->frameAt (t, playing, frameTime);
    }
    juce::Image still (double t, int maxWidth) override
    {
        const raw::ThreadUse use;
        std::string why;
        maxWidth = juce::jmax (16, maxWidth);
        if (! ensureStill (maxWidth, why)) return {};
        double t0; Info inf;
        { const std::lock_guard<std::mutex> g (core->m); t0 = core->t0; inf = core->inf; }
        raw::Frame fr; bool ended = false;
        if (! stillRd->seek (t0 + juce::jmax (0.0, t)) || ! stillRd->next (fr, ended, why)) return {};
        auto im = imageOf (*stillRd, fr);
        if (! im.isValid()) return {};
        // its shape is the picture's (the frame's pixels need not be square), and it is no wider than asked
        const double shape = inf.width > 0 && inf.height > 0 ? (double) inf.width / (double) inf.height : (double) im.getWidth() / (double) im.getHeight();
        int w = juce::jmin (maxWidth, im.getWidth());
        int h = juce::jmax (2, juce::roundToInt ((double) w / shape));
        if (h > im.getHeight()) { h = im.getHeight(); w = juce::jmax (2, juce::roundToInt ((double) h * shape)); }
        if (w != im.getWidth() || h != im.getHeight()) im = im.rescaled (w, h, juce::Graphics::mediumResamplingQuality);
        return im;
    }
    bool readSound (const juce::File& wav, const std::function<bool (float)>& progress, juce::String& why) override
    {
        const raw::ThreadUse use;
        std::string w;
        if (! ensureStill (320, w)) { why = juce::String::fromUTF8 (w.c_str()); return false; }        // (the first frame's time: where the sound's sample 0 belongs)
        double t0;
        { const std::lock_guard<std::mutex> g (core->m); t0 = core->t0; }
        auto sr = raw::openSound (core->path, w);
        if (sr == nullptr) { why = juce::String::fromUTF8 (w.c_str()); return false; }
        const auto fmt = sr->format();
        const int nch = fmt.channels; const double rate = fmt.rate;
        if (nch < 1 || nch > 64 || ! (rate >= 8000.0 && rate <= 384000.0)) { why = "its sound has a shape fermata cannot use (" + juce::String (nch) + " channels at " + juce::String (rate, 0) + " Hz)"; return false; }
        auto writer = audioops::makeWavWriter (wav, rate, nch);
        if (writer == nullptr) { why = "the file for its sound could not be made: " + wav.getFullPathName(); return false; }
        juce::AudioBuffer<float> work (nch, 8192);
        juce::int64 written = 0;                                                    // frames in the WAV: where, from the video's first frame, the next sample goes
        bool ok = true;
        auto silence = [&] (juce::int64 n)
        {
            work.clear();
            while (n > 0 && ok)
            {
                const int k = (int) juce::jmin (n, (juce::int64) work.getNumSamples());
                ok = writer->writeFromAudioSampleBuffer (work, 0, k);
                written += k; n -= k;
            }
        };
        const juce::int64 mostSilence = (juce::int64) (rate * 3600.0);
        std::vector<float> block;
        for (;;)
        {
            double time = 0.0; bool ended = false;
            block.clear();
            if (! sr->next (time, block, ended, w))
            {
                if (! ended) { ok = false; why = juce::String::fromUTF8 (w.empty() ? "its sound cannot be read" : w.c_str()); }
                break;
            }
            const int frames = (int) (block.size() / (size_t) nch);
            if (frames <= 0) continue;
            const juce::int64 at = (juce::int64) std::llround ((time - t0) * rate);     // where this block belongs
            int skip = 0;
            if (written == 0)                                                            // the start: lined up on the first frame
            {
                if (at < 0) skip = (int) juce::jmin ((juce::int64) frames, -at);         // (sound from before the first frame: left out)
                else if (at > 0 && at < mostSilence) silence (at);                       // (the sound starts later than the picture)
            }
            else if (at - written > (juce::int64) (0.02 * rate) && at - written < mostSilence) silence (at - written);     // a hole in the sound: kept as silence, so what follows stays in step
            for (int from = skip; from < frames && ok;)
            {
                const int k = juce::jmin (frames - from, work.getNumSamples());
                for (int c = 0; c < nch; ++c)
                {
                    float* d = work.getWritePointer (c);
                    const float* s = block.data() + (size_t) from * (size_t) nch + (size_t) c;
                    for (int i = 0; i < k; ++i) d[i] = s[(size_t) i * (size_t) nch];
                }
                ok = writer->writeFromAudioSampleBuffer (work, 0, k);
                written += k; from += k;
            }
            if (! ok) { why = "the drive did not take its sound (is it full?)"; break; }
            if (progress && ! progress (fmt.seconds > 0.0 ? (float) juce::jlimit (0.0, 1.0, (time - t0) / fmt.seconds) : 0.5f)) { ok = false; why = "stopped"; break; }
        }
        writer.reset();
        if (ok && written <= 0) { ok = false; why = "its sound track is empty"; }
        if (ok) if (const auto bad = audioops::checkWrittenLength (nullptr, wav, written); bad.isNotEmpty()) { ok = false; why = bad; }
        if (! ok) wav.deleteFile();
        return ok;
    }

private:
    /** The reader of the small pictures (and of open()): opened when first needed, small. Only ever used by the thread that uses the Source. */
    bool ensureStill (int maxWidth, std::string& why)
    {
        if (stillRd != nullptr) return true;
        if (! core->file.existsAsFile()) { why = "the file is not there: " + core->path; return false; }
        raw::Options o; o.maxWidth = juce::jmax (320, 2 * maxWidth); o.hardware = hardwareAllowed.load (std::memory_order_relaxed);
        auto r = raw::openPicture (core->path, o, why);
        if (r == nullptr) return false;
        raw::Frame first; bool ended = false;
        if (! r->next (first, ended, why)) { if (why.empty()) why = "it has no picture"; return false; }
        const auto fmt = r->format();
        { const std::lock_guard<std::mutex> g (core->m); core->noteFormat (fmt, first.time); }
        stillRd = std::move (r);
        return true;
    }
    std::shared_ptr<Core> core;
    std::thread thread;
    std::unique_ptr<raw::PictureReader> stillRd;
};
} // namespace

std::shared_ptr<Source> open (const juce::File& file, juce::String& why)
{
    if (! file.existsAsFile()) { why = "the file is not there: " + file.getFullPathName(); return nullptr; }
    auto s = std::make_shared<StreamSource> (file);
    if (! s->openNow (why)) return nullptr;
    return s;
}

std::shared_ptr<Source> openForPicture (const juce::File& file)
{
    auto s = std::make_shared<StreamSource> (file);
    s->startThread();
    return s;
}

juce::String systemName() { return raw::systemName(); }
juce::String buildReport()
{
   #if FERMATA_HAS_VIDEO_REPORT
    return juce::String::fromUTF8 (reinterpret_cast<const char*> (kVideoBuildReport));
   #else
    return "(no report: this copy was built without CMakeLists.txt's video trial - the Linux checks' build)";
   #endif
}
void setHardwareDecoding (bool allowed) { hardwareAllowed.store (allowed, std::memory_order_relaxed); }
bool hardwareDecoding() { return hardwareAllowed.load (std::memory_order_relaxed); }

void finishDecoders()
{
    auto& r = retired();
    std::vector<std::pair<std::thread, std::shared_ptr<Core>>> all;
    { const std::lock_guard<std::mutex> g (r.m); r.closing = true; all.swap (r.list); }
    for (auto& p : all) if (p.first.joinable()) p.first.join();
}
} // namespace td::video
