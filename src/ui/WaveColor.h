#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_events/juce_events.h>
#include "../core/Dsp.h"
#include "WaveStore.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>
#include "../core/AudioFormats.h"
#include "../core/CrashReport.h"

namespace td
{
namespace chan
{
/** Waveform look: false (the default) = the dark clip with the WaveColour (colour by frequency) waveform on it, as always; true = a bright clip with a pale waveform and a thin dark outline (the colours are washed out, so it is off). */
inline bool& solidLook() { static bool b = false; return b; }
}

/** 3.61.0 (asked for on 23 November 2026): how readily a knock is drawn BLACK. Normal is how it has always been: a block (4096 samples, about a
    tenth of a second) is black when more than half its sound is below 100 Hz. A knock on a stand under the music is less than that, so it kept
    the music's colour. With a weighting a block is also black when (1) its share below 100 Hz is above the weighting's (Light 30 %, Medium 18 %,
    Strong 8 %), (2) its sound below 100 Hz is at least 2.5 times the block before's - a knock comes suddenly; a sustained bass note
    (organ pedals, double basses) does not, so it keeps its colour, though a bass note that starts suddenly can turn its first block black (a
    knock that falls across two blocks counts too: see shownAt) - and (3) it is not near silence (-60 dBFS). The share is measured so that a
    knock reads much the same wherever it falls in a block (BlockAnalyser). Each block keeps its share (WaveColorData::low) and its level, so a change of weighting is drawn at once,
    without working anything out again. Each program sets the weighting from its own settings (fermata: right-click the WaveColour button;
    fermata:Live: right-click Colours); it is read when drawing. */
namespace knockweight
{
    inline std::atomic<int>& level() { static std::atomic<int> v { 0 }; return v; }     // 0 Normal, 1 Light, 2 Medium, 3 Strong
    inline float share (int lvl) noexcept { static const float s[] = { 0.5f, 0.30f, 0.18f, 0.08f }; return s[juce::jlimit (0, 3, lvl)]; }
    inline juce::String name (int lvl) { static const char* n[] = { "Normal", "Light", "Medium", "Strong" }; return n[juce::jlimit (0, 3, lvl)]; }
    /** The colour block k is drawn: its own, or black by the weighting (see above). 'argb', 'levels' and 'lows' are the blocks' colours, levels
        (rms) and shares below 100 Hz (0..255; none kept: its own colour). (argb 0: not known - stays 0.) */
    inline juce::uint32 shownAt (const std::vector<juce::uint32>& argb, const std::vector<float>& levels, const std::vector<juce::uint8>& lows, size_t k) noexcept
    {
        const auto own = argb[k];
        const int lvl = level().load (std::memory_order_relaxed);
        if (own == 0 || own == 0xff000000u || lvl <= 0 || k >= lows.size() || k >= levels.size()) return own;
        const float lv = levels[k];
        if (lv < 1.0e-3f || (float) lows[k] <= share (lvl) * 255.0f) return own;
        auto lowAt = [&] (size_t i) { return i < levels.size() && i < lows.size() ? levels[i] * levels[i] * (float) lows[i] : 0.0f; };
        const float now = lowAt (k);
        if (now > 2.5f * (k > 0 ? lowAt (k - 1) : 0.0f)) return 0xff000000u;
        // a knock that fell across two blocks: the block before measured its start (under the share) and this one the rest. It came suddenly if
        // the block before THAT was quiet below 100 Hz, and it is a knock - not a bass note fading in, which also rises over two blocks - if it is
        // already dying away in the block after (while recording, the last block waits for the next one)
        if (k > 1 && k + 1 < lows.size() && (float) lows[k - 1] <= share (lvl) * 255.0f && now > 2.5f * lowAt (k - 2) && lowAt (k + 1) < 0.5f * now)
            return 0xff000000u;
        return own;
    }
    /** The heading and the four lines of the weighting's menu (each program makes the menu: ids 1 - 4, the one in use ticked). */
    inline juce::String menuHeading() { return "Knocks drawn black (WaveColour)"; }
    inline juce::String menuItem (int lvl)
    {
        static const char* what[] = { "Normal - as before: only a thump that is mostly below 100 Hz",
                                      "Light - also a knock under the music",
                                      "Medium - also a quieter knock",
                                      "Strong - the quietest knocks (a deep bass note that starts suddenly may go black too)" };
        return what[juce::jlimit (0, 3, lvl)];
    }
}

/** WaveColour: what colour the waveform of a file is at every moment. The hue says how high the sound is (red low, green middle, blue high),
    the saturation says how tonal it is (vivid = a note, pale = noise), and a thump below 100 Hz (a kicked mic stand, a door) is BLACK.
    One value per block of 4096 samples, worked out once in the background. */
struct WaveColorData
{
    double blockSeconds = 0.1;
    std::vector<juce::uint32> argb;                 // the colour of every block
    std::vector<float> level;                       // how loud every block is (linear, 0..1)
    std::vector<juce::uint8> low;                   // 3.61.0: the share of every block's sound below 100 Hz (0..255; see knockweight)

    /** The colour for the time span t0..t1 (seconds): the colour of the loudest block in it (3.61.0: as the knock weighting draws it). */
    juce::Colour colourFor (double t0, double t1) const
    {
        if (argb.empty()) return juce::Colours::grey;
        const long n = (long) argb.size();
        long b0 = juce::jlimit (0L, n - 1, (long) std::floor (t0 / blockSeconds));
        long b1 = juce::jlimit (b0 + 1, n, (long) std::ceil (t1 / blockSeconds));
        long best = b0; float bl = -1.0f;
        for (long b = b0; b < b1; ++b) if (level[(size_t) b] > bl) { bl = level[(size_t) b]; best = b; }
        return juce::Colour (knockweight::shownAt (argb, level, low, (size_t) best));
    }
};

/** Works out the WaveColorData of files in a background thread (one at a time). get() returns nothing until a file is done; onReady then fires.
    3.18.2: the Waveforms folder is asked for on the message thread, in get(), and handed to the job - the job used to ask the project for it from its own
    thread (a race with a project being opened, and a job still queued when Fermata closed asked a project that had already gone). stop() ends the pool
    early in closing; clear() lets the colours of a project go when another is opened (they were kept for the whole run). */
class WaveColorCache
{
public:
    WaveColorCache() = default;
    ~WaveColorCache() { stop(); }

    std::function<void()> onReady;
    std::function<juce::File()> folder;              // the Waveforms folder: worked-out colours are kept there between runs (called on the message thread only)
    /** 3.58.0: while this says true (fermata: a recording is under way), the colouring waits. Set once, before the first get(); called from the workers. */
    std::function<bool()> holdOff;
    /** 3.58.0: how many files are worked out at once (1 - 4; fermata:Live keeps 1). A take window of an orchestral session asks for hundreds of files
        at once, and one at a time it took many minutes before the colours of what was on the screen came. Message thread, before the first get(). */
    void setWorkers (int n) { if (pool == nullptr) maxWorkers = juce::jlimit (1, kMaxWorkers, n); }
    /** How many files are still waiting to be coloured (for a word on the screen). */
    int waiting() { std::lock_guard<std::mutex> lock (m); return (int) pending.size(); }

    /** Closing (3.18.2): nothing more is started, and a job under way is given up (it looks at 'alive' between chunks); waits for it, at most 20 s. Safe to call again. */
    void stop()
    {
        alive->store (false);
        if (pool != nullptr) pool->removeAllJobs (true, 20000);
    }
    /** Another project (3.18.2): the finished colours are let go (a file drawn again is worked out again, or read back from its Waveforms folder). */
    void clear()
    {
        std::lock_guard<std::mutex> lock (m);
        done.clear();
        queue.clear(); pending.clear();                                       // (3.58.0: what was still waiting is not worked out for a project that has gone)
    }

    std::shared_ptr<const WaveColorData> get (const juce::File& f)
    {
        if (! alive->load()) return nullptr;
        const auto key = f.getFullPathName() + "|" + juce::String (f.getSize());
        const auto dir = folder ? folder() : juce::File();                    // (here, on the message thread: never from the job)
        std::lock_guard<std::mutex> lock (m);
        auto it = done.find (key);
        if (it != done.end()) return it->second;
        if (pending.insert (key).second) queue.push_back ({ f, key, dir });
        else for (size_t i = 0; i < queue.size(); ++i)                       // (asked for again: drawn again now, so it is on the screen - to the back, taken next)
            if (queue[i].key == key) { auto rq = queue[i]; queue.erase (queue.begin() + (long) i); queue.push_back (rq); break; }
        if (pool == nullptr)                                                  // (made when first needed, as big as asked for; low priority: playing and recording come first)
            pool = std::make_unique<juce::ThreadPool> (maxWorkers, juce::Thread::osDefaultStackSize, juce::Thread::Priority::low);
        while (running < maxWorkers && running < (int) queue.size())
        {
            ++running;
            pool->addJob ([this, alive = alive] { work (alive); });
        }
        return nullptr;
    }

    /** 3.59.0: colours worked out elsewhere for a finished file (fermata: while it was recorded, LiveWaveColours) - kept as if worked out here,
        and saved in the Waveforms folder; a request for it still waiting is dropped. Message thread. */
    void put (const juce::File& f, std::shared_ptr<WaveColorData> d)
    {
        if (d == nullptr || d->argb.empty() || ! alive->load()) return;
        const auto key = f.getFullPathName() + "|" + juce::String (f.getSize());
        const auto dir = folder ? folder() : juce::File();
        {
            std::lock_guard<std::mutex> lock (m);
            done[key] = d;
            pending.erase (key);
            queue.erase (std::remove_if (queue.begin(), queue.end(), [&key] (const Request& r) { return r.key == key; }), queue.end());
        }
        if (dir != juce::File()) save (dir, f, *d);                          // (a few kilobytes: 8 bytes for every 4096 samples)
        if (onReady) onReady();
    }

    /** Forgets what was worked out for this file (the next draw works it out again from the audio). */
    void forget (const juce::File& f)
    {
        const auto key = f.getFullPathName() + "|" + juce::String (f.getSize());
        std::lock_guard<std::mutex> lock (m);
        done.erase (key);
    }

    static juce::Colour colourOf (double centroidHz, double tonality)
    {
        // hue: 100 Hz red, 1 kHz green, 8 kHz and above blue (even steps of pitch between)
        const double l = std::log2 (juce::jmax (100.0, centroidHz) / 100.0);                 // 0 .. ~6.3
        const double hue = l <= 3.32 ? 120.0 * l / 3.32 : 120.0 + 120.0 * juce::jmin (1.0, (l - 3.32) / 3.0);
        const float sat = (float) juce::jlimit (0.0, 1.0, 0.15 + 0.85 * tonality);
        return juce::Colour::fromHSV ((float) (hue / 360.0), sat, 0.95f, 1.0f);
    }

    /** The block the colours are worked out in (4096 samples), and the analysis of one block of mono sound: its colour (see colourOf; grey for silence,
        black for a thump below 100 Hz) and its level (rms). fermata:Live 0.5.0 colours its live waveforms with it, so both programs colour alike.
        3.61.0: and, when asked ('lowShare'), the share of its sound below 100 Hz, 0..255 (0 for silence) - for the knock weighting.

        The analysis window (Hann) hears the middle of the block fully and its edges hardly at all, so a knock that lands near the edge between two
        blocks was measured at a tenth of its share or less (in the trial a knock of 0.18 under a chord: 2 % and 5 %; the same knock in the middle
        of a block: over 20 %) and no weighting caught it. So the share is also measured over a second window centred on the block's START edge
        (the half block before it and its own first half): every moment is heard at half strength or more by one of the two, and the block keeps
        the larger share. 'before' is the kBlock / 2 samples just before the block (nullptr: not known - the first block, or a gap - its own window
        only). The colour, and Normal's black (more than half below 100 Hz), are worked out from the block's own window as always. */
    static constexpr int kBlock = 4096;
    class BlockAnalyser
    {
    public:
        BlockAnalyser() : fft (kBlock), win (dsp::hann (kBlock)), buf ((size_t) kBlock) {}
        juce::Colour colour (const double* mono, double sr, float& rms, juce::uint8* lowShare = nullptr, const double* before = nullptr)
        {
            if (lowShare != nullptr) *lowShare = 0;
            const int N = kBlock;
            const double binHz = sr / (double) N;
            double sumSq = 0.0;
            for (int i = 0; i < N; ++i) { const double v = mono[i]; sumSq += v * v; buf[(size_t) i] = v * win[(size_t) i]; }
            rms = (float) std::sqrt (sumSq / (double) N);
            fft.forward (buf);
            double total = 0.0, low = 0.0, wsum = 0.0, fsum = 0.0, logSum = 0.0; int nb = 0;
            for (int k = 1; k <= N / 2; ++k)
            {
                const double p = std::norm (buf[(size_t) k]), hz = (double) k * binHz;
                total += p; if (hz < 100.0) low += p;
                if (hz >= 100.0 && hz <= juce::jmin (12000.0, 0.45 * sr)) { wsum += p; fsum += p * hz; logSum += std::log (p + 1.0e-18); ++nb; }
            }
            if (rms < 1.0e-4f || total <= 0.0) return juce::Colour (0xff808080);                                // silence: grey
            if (lowShare != nullptr)
            {
                double share = low / total;
                if (before != nullptr) share = juce::jmax (share, edgeShare (before, mono, sr));
                *lowShare = (juce::uint8) juce::jlimit (0, 255, (int) std::lround (255.0 * share));
            }
            if (low > 0.5 * total) return juce::Colours::black;                                                   // a thump below 100 Hz
            const double centroid = wsum > 0.0 ? fsum / wsum : 1000.0;
            const double flat = nb > 0 && wsum > 0.0 ? std::exp (logSum / (double) nb) / (wsum / (double) nb) : 0.56;
            return colourOf (centroid, 1.0 - juce::jlimit (0.0, 1.0, flat / 0.56));                              // noise has a flatness of about 0.56, a pure note about 0
        }
    private:
        /** The share below 100 Hz over the window centred on the block's start edge (before[0 .. N/2) then mono[0 .. N/2)), measured as the block's
            own is (the bins from 1 up to below 100 Hz, over the bins from 1 to N/2), but only those few bins are worked out (Goertzel), not a whole FFT;
            the total comes from the windowed samples themselves (Parseval). */
        double edgeShare (const double* before, const double* mono, double sr) const
        {
            const int N = kBlock, H = kBlock / 2;
            const double binHz = sr / (double) N;
            double sumSq = 0.0, sum = 0.0;
            for (int i = 0; i < N; ++i) { const double v = (i < H ? before[i] : mono[i - H]) * win[(size_t) i]; sumSq += v * v; sum += v; }
            const double total = 0.5 * ((double) N * sumSq - sum * sum);                                      // the bins 1 .. N/2 (the Nyquist bin aside)
            if (total <= 0.0) return 0.0;
            double low = 0.0;
            for (int k = 1; (double) k * binHz < 100.0 && k <= N / 2; ++k)
            {
                const double c = 2.0 * std::cos (2.0 * dsp::kPi * (double) k / (double) N);
                double s1 = 0.0, s2 = 0.0;
                for (int i = 0; i < N; ++i)
                {
                    const double s0 = (i < H ? before[i] : mono[i - H]) * win[(size_t) i] + c * s1 - s2;
                    s2 = s1; s1 = s0;
                }
                low += juce::jmax (0.0, s1 * s1 + s2 * s2 - c * s1 * s2);
            }
            return juce::jlimit (0.0, 1.0, low / total);
        }
        const dsp::FFT fft; const std::vector<double> win;
        std::vector<std::complex<double>> buf;
    };

private:
    static std::shared_ptr<WaveColorData> loadSaved (const juce::File& dir, const juce::File& audio)
    {
        const auto f = wavestore::fileFor (dir, audio, ".fwcol");
        if (! f.existsAsFile()) return nullptr;
        juce::FileInputStream in (f);
        if (! in.openedOk() || ! wavestore::readHeader (in, wavestore::kMagicColour, audio)) return nullptr;
        auto d = std::make_shared<WaveColorData>();
        d->blockSeconds = in.readDouble();
        const auto n = in.readInt64();
        if (n < 0 || n > 50000000 || d->blockSeconds <= 0.0) return nullptr;
        if (in.getTotalLength() >= 0 && in.getTotalLength() - in.getPosition() < n * 8) return nullptr;      // (3.18.2: a garbled count never asks for more than the file holds)
        d->argb.resize ((size_t) n); d->level.resize ((size_t) n);
        if (in.read (d->argb.data(), (int) (n * 4)) != (int) (n * 4) || in.read (d->level.data(), (int) (n * 4)) != (int) (n * 4)) return nullptr;
        // 3.61.0: the share below 100 Hz follows (one byte a block). A file saved before has none: it is worked out again (once), so the knock
        // weighting works on it too. (An older program reads the colours and levels and stops there.)
        d->low.resize ((size_t) n);
        if (in.read (d->low.data(), (int) n) != (int) n) return nullptr;
        return d;
    }
    static void save (const juce::File& dir, const juce::File& audio, const WaveColorData& d)
    {
        if (d.argb.empty() || ! audio.existsAsFile() || ! dir.createDirectory().wasOk()) return;
        const auto f = wavestore::fileFor (dir, audio, ".fwcol");
        juce::TemporaryFile tmp (f);
        {
            juce::FileOutputStream out (tmp.getFile());
            if (! out.openedOk()) return;
            wavestore::writeHeader (out, wavestore::kMagicColour, audio);
            out.writeDouble (d.blockSeconds); out.writeInt64 ((juce::int64) d.argb.size());
            out.write (d.argb.data(), d.argb.size() * 4); out.write (d.level.data(), d.level.size() * 4);
            if (d.low.size() == d.argb.size()) out.write (d.low.data(), d.low.size());       // (3.61.0)
        }
        tmp.overwriteTargetFileWithTemporary();
    }
    /** 3.58.0: a worker - takes the newest request first (what was drawn last is what is on the screen now), until none is left. */
    struct Request { juce::File f; juce::String key; juce::File dir; };
    void work (std::shared_ptr<std::atomic<bool>> alive)
    {
        td::crashreport::nameThisThreadW (L"Waveform colours");
        juce::AudioFormatManager fm; td::registerAudioFormats (fm);         // (its own: the workers run side by side)
        for (;;)
        {
            if (! alive->load()) return;                                     // (closing: 'running' no longer matters)
            if (holdOff && holdOff()) { juce::Thread::sleep (250); continue; }
            Request rq;
            {
                std::lock_guard<std::mutex> l (m);
                if (queue.empty()) { --running; return; }
                rq = queue.back(); queue.pop_back();
            }
            auto d = rq.dir != juce::File() ? loadSaved (rq.dir, rq.f) : nullptr;
            if (d == nullptr)
            {
                d = analyse (fm, rq.f);
                if (alive->load() && rq.dir != juce::File()) save (rq.dir, rq.f, *d);
            }
            if (! alive->load()) return;
            { std::lock_guard<std::mutex> l (m); done[rq.key] = d; pending.erase (rq.key); }
            juce::MessageManager::callAsync ([this, alive] { if (alive->load() && onReady) onReady(); });
        }
    }
    std::shared_ptr<WaveColorData> analyse (juce::AudioFormatManager& formats, const juce::File& f)
    {
        auto d = std::make_shared<WaveColorData>();
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples < 1 || r->sampleRate <= 0.0) return d;
        const int N = kBlock; const double sr = r->sampleRate;
        d->blockSeconds = (double) N / sr;
        BlockAnalyser an;
        std::vector<double> mono ((size_t) N), prev ((size_t) N);
        bool havePrev = false;
        const int nCh = (int) juce::jmin ((juce::uint32) 2, r->numChannels);
        juce::AudioBuffer<float> chunk (nCh, N * 32);
        for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += (juce::int64) N * 32)
        {
            if (! alive->load()) return d;
            const int got = (int) juce::jmin ((juce::int64) N * 32, r->lengthInSamples - pos);
            chunk.clear();
            r->read (&chunk, 0, got, pos, true, nCh > 1);
            for (int b = 0; b * N < got; ++b)
            {
                for (int i = 0; i < N; ++i)
                {
                    const int s = b * N + i; double v = 0.0;
                    if (s < got) { for (int c = 0; c < nCh; ++c) v += chunk.getSample (c, s); v /= (double) nCh; }
                    mono[(size_t) i] = v;
                }
                float rms = 0.0f; juce::uint8 lowShare = 0;
                const auto c = an.colour (mono.data(), sr, rms, &lowShare, havePrev ? prev.data() + N / 2 : nullptr);
                d->argb.push_back (c.getARGB()); d->level.push_back (rms); d->low.push_back (lowShare);
                std::swap (prev, mono); havePrev = true;
            }
        }
        return d;
    }

    static constexpr int kMaxWorkers = 4;
    std::unique_ptr<juce::ThreadPool> pool;
    std::mutex m;
    std::map<juce::String, std::shared_ptr<const WaveColorData>> done;
    std::set<juce::String> pending;
    std::vector<Request> queue;                     // (3.58.0) waiting, the newest at the back
    int running = 0;                                // workers under way (both guarded by m)
    int maxWorkers = 1;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);     // (atomic: read by the job, written at closing)
};
}
