#pragma once
/** fermata:Live 0.5.0: the coloured waveforms (fermata's WaveColour) of the takes of a session, while they are recorded. fermata colours a file once
    it is finished; a take still recording has no file yet, so this works its colours out from the session's chunks on the primary drive, a block
    (4096 samples) at a time, as far as the drive has written - in the station's window process, on a thread of its own at low priority, never in the
    recorder. The audio it reads was written a moment before, so it is normally still in the computer's memory and does not load the drive.
    The analysis is fermata's own (WaveColorCache::BlockAnalyser), so a take is coloured the same before and after it is merged.

    Threads: want / stop / colours are the message thread's; the analysis runs on its own thread. They share the results under a lock held only to
    copy them (the paint) or to add a finished batch. */
#include "ChunkTimeline.h"
#include "../ui/WaveColor.h"
#include <juce_core/juce_core.h>
#include <mutex>

namespace td::live
{
class LiveColours final : private juce::Thread
{
public:
    LiveColours() : juce::Thread ("fermata:Live waveform colours") {}
    ~LiveColours() override { stop(); }

    /** What to colour: the session's folder (empty: nothing - the colours are let go), how far the primary drive has written, and the session's
        takes as [first sample, end) (end < 0: still recording). Cheap: call it as often as the window likes. */
    void want (const juce::File& sessionFolder, juce::int64 writtenTo, std::vector<std::pair<juce::int64, juce::int64>> takeSpans)
    {
        {
            const std::lock_guard<std::mutex> g (lock);
            if (sessionFolder != folder) { folder = sessionFolder; tracks.clear(); scanned.clear(); }
            written = writtenTo; spans = std::move (takeSpans);
        }
        if (sessionFolder == juce::File()) return;
        if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
        notify();
    }
    void stop() { signalThreadShouldExit(); notify(); stopThread (5000); }

    /** The colours of a session track for 'n' pixel columns from 'first' (session samples), 'perPixel' samples each: the loudest block's colour in
        each column, 'none' where nothing has been worked out yet. False when nothing at all is known for the track (draw it plain). */
    bool colours (int trackIndex, juce::int64 first, double perPixel, int n, std::vector<juce::Colour>& out, juce::Colour none) const
    {
        out.assign ((size_t) juce::jmax (0, n), none);
        const std::lock_guard<std::mutex> g (lock);
        if (! juce::isPositiveAndBelow (trackIndex, (int) tracks.size())) return false;
        const auto& t = tracks[(size_t) trackIndex];
        if (t.argb.empty()) return false;
        bool any = false;
        const auto nb = (juce::int64) t.argb.size();
        for (int i = 0; i < n; ++i)
        {
            const juce::int64 a = first + (juce::int64) std::floor ((double) i * perPixel), b = first + (juce::int64) std::ceil ((double) (i + 1) * perPixel);
            juce::int64 b0 = juce::jmax ((juce::int64) 0, a / kBlock), b1 = juce::jmin (nb, juce::jmax (b0 + 1, (b + kBlock - 1) / kBlock));
            float best = -1.0f; juce::uint32 c = 0;
            for (juce::int64 k = b0; k < b1; ++k)                                // (0.10.11: as the knock weighting draws it)
                if (t.argb[(size_t) k] != 0 && t.level[(size_t) k] > best)
                { best = t.level[(size_t) k]; c = knockweight::shownAt (t.argb, t.level, t.low, (size_t) k); }
            if (c != 0) { out[(size_t) i] = juce::Colour (c); any = true; }
        }
        return any;
    }

private:
    static constexpr int kBlock = WaveColorCache::kBlock;
    static constexpr int kBatch = 16;                        // blocks read and worked out at a time (per track)

    void run() override
    {
        WaveColorCache::BlockAnalyser an;
        ChunkTimeline timeline;
        std::vector<std::vector<float>> chans;
        std::vector<double> mono ((size_t) kBlock), before ((size_t) kBlock / 2);
        while (! threadShouldExit())
        {
            juce::File f; juce::int64 w = 0; std::vector<std::pair<juce::int64, juce::int64>> sp;
            { const std::lock_guard<std::mutex> g (lock); f = folder; w = written; sp = spans; }
            if (f == juce::File()) { wait (500); continue; }
            timeline.setSession (f);
            timeline.setWrittenTo (w);
            timeline.refresh();
            if (! timeline.valid()) { wait (500); continue; }
            const auto& idx = timeline.sessionIndex();
            const double sr = idx.sampleRate > 0.0 ? idx.sampleRate : 48000.0;
            const int numTracks = (int) idx.tracks.size();
            bool didSome = false;
            for (auto& s : sp)
            {
                if (threadShouldExit()) return;
                const juce::int64 end = s.second < 0 ? w : juce::jmin (s.second, w);
                juce::int64 from;
                { const std::lock_guard<std::mutex> g (lock); auto it = scanned.find (s.first); from = it != scanned.end() ? it->second : s.first / kBlock; }
                const juce::int64 lastFull = end / kBlock;                       // (blocks wholly on the disk)
                if (from >= lastFull) continue;
                const juce::int64 to = juce::jmin (lastFull, from + kBatch);
                const int n = (int) ((to - from) * kBlock);
                const int lead = from > 0 ? kBlock / 2 : 0;                    // 3.61.0 (0.10.11): the half block before, for the knock weighting's second window
                std::vector<std::vector<juce::uint32>> argb ((size_t) numTracks);
                std::vector<std::vector<float>> level ((size_t) numTracks);
                std::vector<std::vector<juce::uint8>> low ((size_t) numTracks);
                for (int ti = 0; ti < numTracks; ++ti)
                {
                    if (threadShouldExit()) return;
                    const int ch = juce::jmax (1, idx.tracks[(size_t) ti].channels);
                    chans.resize ((size_t) ch);
                    std::vector<float*> dest;
                    for (auto& c : chans) { c.assign ((size_t) (lead + n), 0.0f); dest.push_back (c.data()); }
                    timeline.read (ti, from * kBlock - lead, lead + n, dest.data());
                    const int use = juce::jmin (2, ch);
                    for (juce::int64 b = 0; b < to - from; ++b)
                    {
                        for (int i = -kBlock / 2; i < kBlock; ++i)
                        {
                            const juce::int64 at = lead + b * kBlock + i;
                            if (at < 0) continue;
                            double v = 0.0;
                            for (int c = 0; c < use; ++c) v += chans[(size_t) c][(size_t) at];
                            (i < 0 ? before[(size_t) (i + kBlock / 2)] : mono[(size_t) i]) = v / (double) use;
                        }
                        float rms = 0.0f; juce::uint8 lowShare = 0;
                        const bool haveBefore = from + b > 0;
                        const auto colour = an.colour (mono.data(), sr, rms, &lowShare, haveBefore ? before.data() : nullptr);
                        argb[(size_t) ti].push_back (colour.getARGB()); level[(size_t) ti].push_back (rms); low[(size_t) ti].push_back (lowShare);
                    }
                }
                {
                    const std::lock_guard<std::mutex> g (lock);
                    if (f != folder) break;                                        // (another session meanwhile: these are not its colours)
                    if ((int) tracks.size() < numTracks) tracks.resize ((size_t) numTracks);
                    for (int ti = 0; ti < numTracks; ++ti)
                    {
                        auto& t = tracks[(size_t) ti];
                        if ((juce::int64) t.argb.size() < to) { t.argb.resize ((size_t) to, 0); t.level.resize ((size_t) to, 0.0f); t.low.resize ((size_t) to, 0); }
                        for (juce::int64 b = 0; b < to - from; ++b)
                        {
                            t.argb[(size_t) (from + b)] = argb[(size_t) ti][(size_t) b]; t.level[(size_t) (from + b)] = level[(size_t) ti][(size_t) b];
                            t.low[(size_t) (from + b)] = low[(size_t) ti][(size_t) b];
                        }
                    }
                    scanned[s.first] = to;
                }
                didSome = true;
            }
            if (! didSome) wait (300);
        }
    }

    struct Track { std::vector<juce::uint32> argb; std::vector<float> level; std::vector<juce::uint8> low; };      // by block of the session's timeline (argb 0: not worked out)
    mutable std::mutex lock;
    juce::File folder;
    juce::int64 written = 0;
    std::vector<std::pair<juce::int64, juce::int64>> spans;
    std::vector<Track> tracks;
    std::map<juce::int64, juce::int64> scanned;                  // a take's first sample -> the next block of it to work out
};
} // namespace td::live
