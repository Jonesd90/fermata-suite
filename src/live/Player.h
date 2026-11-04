#pragma once
/** fermata:Live 0.3.0: a station's player - plays a take window's timeline (TakeLayout) through the station's own mixer gains, rendering on the
    playback stream's thread (StreamClient) as far ahead as the recorder's window allows, so the recorder plays it on the station's output pair.
    A take of the session is read from the session's chunks (or its merged files), any other from the project's files. The Fixer limits it to
    what the drive has written (setLimit): it never plays past the end of the audio that is on the disk.

    An audition (setAudition: the ReTouch result before it is saved) plays in place of the original on the tracks it was made for.

    Threads: everything is the message thread's except render (the stream's thread); they share a little state under a lock held for copying. */
#include "TakeLayout.h"
#include "ChunkTimeline.h"
#include "StreamClient.h"
#include <map>
#include <mutex>

namespace td::live
{
class Player
{
public:
    /** Per track: each channel's gain to the left and to the right output (linear). A track with none is not read at all. */
    using Gains = std::map<juce::Uuid, std::vector<std::pair<float, float>>>;
    struct Audition { int takeNumber = 0; juce::int64 from = 0; std::map<juce::Uuid, std::vector<std::vector<float>>> tracks; };   // take samples [from, from + length): per track, its channels

    Player (int streamPort, const juce::String& stationName, int station)
        : client (streamPort, stationName, station)
    {
        client.render = [this] (float* buf, int frames) { return render (buf, frames); };
        client.start();
    }
    ~Player() { client.stop(); }

    void setLayout (std::shared_ptr<const TakeLayout> l)        { const std::lock_guard<std::mutex> g (lock); layout = std::move (l); }
    void setGains (Gains g)                                     { const std::lock_guard<std::mutex> l (lock); gains = std::make_shared<Gains> (std::move (g)); }
    void setOutputs (int left, int right)                       { client.setOutputs (left, right); }
    void setWrittenTo (juce::int64 s)                           { writtenTo = s; }
    /** The Fixer: nothing past this place on the window's timeline is played (seconds; < 0 = no limit). */
    void setLimit (double seconds)                              { limit = seconds; }
    void setLoop (bool on, double a, double b)                  { const std::lock_guard<std::mutex> l (lock); loopOn = on && b > a; loopA = a; loopB = b; }
    void setAudition (std::shared_ptr<const Audition> a)        { const std::lock_guard<std::mutex> l (lock); audition = std::move (a); }
    bool isConnected() const                                    { return client.isConnected(); }
    /** A track's level (linear peak, before its fader) in what was played since the last time it was asked: the station's mixer meters. */
    float takePeak (const juce::Uuid& id)
    {
        const std::lock_guard<std::mutex> l (meterLock);
        auto it = peaks.find (id);
        if (it == peaks.end()) return 0.0f;
        const float p = it->second; it->second = 0.0f;
        return p;
    }
    juce::uint32 underruns() const                              { return client.underruns(); }

    void play (double fromSeconds)
    {
        {
            const std::lock_guard<std::mutex> l (lock);
            startSeconds = juce::jmax (0.0, fromSeconds);
            renderPos = startSeconds; loops = 0;
            ++seeks;                                                    // (a render already under way must not put its old place back: see render)
        }
        playing = true;
        gen = client.newGeneration (true);
    }
    void stop()
    {
        if (! playing.load()) return;
        stopAt = playheadSeconds();
        playing = false;
        gen = client.newGeneration (false);
    }
    bool isPlaying() const noexcept                              { return playing.load(); }
    /** Where the sound is now (seconds on the window's timeline): while playing, what has reached the outputs; else where it stopped. */
    double playheadSeconds() const
    {
        if (! playing.load()) return stopAt;
        const double rate = rateNow.load();
        const double played = rate > 0.0 ? (double) client.playedFrames (gen.load()) / rate : 0.0;
        const std::lock_guard<std::mutex> l (lock);
        if (! loopOn) return startSeconds + played;
        // with a loop: the played time folded into the loop once it has reached its end
        const double first = loopB - startSeconds;
        if (played < first || loopB <= loopA) return startSeconds + played;
        return loopA + std::fmod (played - first, loopB - loopA);
    }
    /** The render position reached the end of what there is (the last take, or the limit): the message thread stops it. */
    bool reachedEnd() const noexcept                             { return ended.load(); }

private:
    int render (float* out, int frames)
    {
        std::shared_ptr<const TakeLayout> L; std::shared_ptr<Gains> G; std::shared_ptr<const Audition> A;
        double pos = 0.0, la = 0.0, lb = 0.0; bool lp = false;
        juce::uint32 seeksAtStart = 0;
        {
            const std::lock_guard<std::mutex> l (lock);
            L = layout; G = gains; A = audition; pos = renderPos; lp = loopOn; la = loopA; lb = loopB; seeksAtStart = seeks;
        }
        juce::FloatVectorOperations::clear (out, frames * 2);
        if (L == nullptr || L->rate <= 0.0) return 0;
        const double rate = L->rate;
        rateNow = rate;
        if (L->sessionFolder != timeline.folder()) timeline.setSession (L->sessionFolder);
        timeline.setWrittenTo (writtenTo.load());
        timeline.refresh();
        double end = L->endSeconds();
        const double lim = limit.load();
        if (lim >= 0.0) end = juce::jmin (end, lim);
        if (lp) end = lb;
        int n = frames;
        if (pos >= end)
        {
            if (lp && lb > la) pos = la;
            else { if (lim < 0.0 || ! anyRecording (*L)) ended = true; return 0; }   // (the Fixer at the written end of a take still recording: waits)
        }
        ended = false;
        n = (int) juce::jmin<juce::int64> (n, (juce::int64) std::floor ((end - pos) * rate));
        if (n <= 0) return 0;
        const juce::int64 w0 = (juce::int64) std::llround (pos * rate);
        mixInto (*L, G.get(), A.get(), w0, n, out);
        pos += (double) n / rate;
        if (lp && pos >= lb - 0.5 / rate) pos = la;
        { const std::lock_guard<std::mutex> l (lock); if (seeks == seeksAtStart) renderPos = pos; }     // (play() moved it meanwhile: its place stands)
        return n;
    }
    static bool anyRecording (const TakeLayout& L) { for (auto& t : L.takes) if (t.recording) return true; return false; }

    /** The window's samples [w0, w0 + n) through the gains, added into the interleaved stereo 'out'. */
    void mixInto (const TakeLayout& L, const Gains* G, const Audition* A, juce::int64 w0, int n, float* out)
    {
        if (G == nullptr) return;
        const double rate = L.rate;
        for (auto& t : L.takes)
        {
            const juce::int64 ts = (juce::int64) std::llround (t.start * rate);
            const juce::int64 te = ts + t.lengthSamples + (t.recording ? (juce::int64) (rate * 600) : 0);
            const juce::int64 a = juce::jmax (w0, ts), b = juce::jmin (w0 + n, te);
            if (b <= a) continue;
            const int len = (int) (b - a), at = (int) (a - w0);
            const juce::int64 local = a - ts;                                   // (the take's own samples)
            for (auto& tr : L.tracks)
            {
                auto git = G->find (tr.id);
                if (git == G->end()) continue;
                const auto& g = git->second;
                const int chans = juce::jmin (tr.channels, (int) g.size(), kMaxTrackChannels);
                if (chans <= 0) continue;
                bool audible = false;
                for (int c = 0; c < chans; ++c) audible = audible || g[(size_t) c].first != 0.0f || g[(size_t) c].second != 0.0f;
                if (! audible) continue;
                scratch.resize ((size_t) chans);
                for (auto& sc : scratch) sc.assign ((size_t) len, 0.0f);
                std::vector<float*> dest; for (auto& sc : scratch) dest.push_back (sc.data());
                bool got = false;
                if (t.session && tr.sessionIndex >= 0 && timeline.valid())
                    got = timeline.read (tr.sessionIndex, t.sessionStart + local, len, dest.data());
                if (! got)
                    for (auto& f : t.files)
                        if (f.trackId == tr.id)
                        {
                            if (auto* r = fileReader (f.file)) got = r->read (local, len, dest.data(), chans) > 0;
                            break;
                        }
                if (A != nullptr && A->takeNumber == t.number)                 // the ReTouch audition: its audio in place of the original
                    if (auto it = A->tracks.find (tr.id); it != A->tracks.end())
                        for (int c = 0; c < chans && c < (int) it->second.size(); ++c)
                        {
                            const auto& src = it->second[(size_t) c];
                            const juce::int64 s0 = juce::jmax (local, A->from), s1 = juce::jmin (local + len, A->from + (juce::int64) src.size());
                            for (juce::int64 s = s0; s < s1; ++s) dest[(size_t) c][(size_t) (s - local)] = src[(size_t) (s - A->from)];
                        }
                float pk = 0.0f;
                for (int c = 0; c < chans; ++c) pk = juce::jmax (pk, juce::FloatVectorOperations::findMaximum (dest[(size_t) c], len), -juce::FloatVectorOperations::findMinimum (dest[(size_t) c], len));
                { const std::lock_guard<std::mutex> l (meterLock); auto& m = peaks[tr.id]; m = juce::jmax (m, pk); }
                for (int c = 0; c < chans; ++c)
                {
                    const float gl = g[(size_t) c].first, gr = g[(size_t) c].second;
                    const float* src = dest[(size_t) c];
                    float* o = out + 2 * at;
                    for (int i = 0; i < len; ++i) { o[2 * i] += src[i] * gl; o[2 * i + 1] += src[i] * gr; }
                }
            }
        }
    }
    RawWavReader* fileReader (const juce::File& f)
    {
        auto it = files.find (f.getFullPathName());
        if (it != files.end()) return it->second.get();
        if (files.size() > 64) files.clear();
        auto r = std::make_unique<RawWavReader>();
        if (! r->open (f)) return nullptr;
        auto* p = r.get(); files[f.getFullPathName()] = std::move (r);
        return p;
    }

    StreamClient client;
    mutable std::mutex lock;
    std::shared_ptr<const TakeLayout> layout;
    std::shared_ptr<Gains> gains;
    std::shared_ptr<const Audition> audition;
    double startSeconds = 0.0, renderPos = 0.0, loopA = 0.0, loopB = 0.0; bool loopOn = false; int loops = 0;
    juce::uint32 seeks = 0;                                             // (lock: goes up with every play())
    std::atomic<bool> playing { false }, ended { false };
    std::atomic<juce::uint32> gen { 0 };
    std::atomic<double> stopAt { 0.0 }, limit { -1.0 }, rateNow { 48000.0 };
    std::atomic<juce::int64> writtenTo { 0 };
    // the stream thread's own
    ChunkTimeline timeline;
    std::map<juce::String, std::unique_ptr<RawWavReader>> files;
    std::vector<std::vector<float>> scratch;
    std::mutex meterLock;                                               // (held only to copy a number: the meters)
    std::map<juce::Uuid, float> peaks;
};
} // namespace td::live
