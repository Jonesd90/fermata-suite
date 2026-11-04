#pragma once
/** fermata:Live 0.2.0: the recorder's memory buffer, between the audio thread and the drives.

    One ring of float samples per channel, written by the audio thread and read by every drive's writer thread at its own pace. The audio thread
    NEVER waits and is never refused: it writes over the oldest samples whether or not every drive has taken them. A drive that falls more than
    the ring's length behind (its disk stalled for that long) finds that out when it reads - the samples it wanted are gone - and moves itself
    forward, with a gap of that length to record (see Reader::take). So one drive that stalls holds nobody up: not the audio, not the other drives.

    How a reader knows its samples were still there: the writer publishes 'claimed' (the position it is ABOUT to write up to) before it touches the
    ring, and 'head' (what is now complete) after. A reader copies up to head and then checks that claimed has not passed the start of what it
    copied plus the ring's length: if it has, the writer may have been writing over those slots during the copy, and the copy is thrown away.
    The fences make that hold in the C++ memory model too (the seqlock pattern): the writer's data stores may not move before its 'claimed' store,
    the reader's data loads may not move after its 'claimed' check. They cost nothing on x86. */
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace td::live
{
class Ring
{
public:
    /** channels x capacity floats. capacity = how many samples per channel the ring keeps (seconds of audio x the sample rate). */
    void prepare (int channels, juce::int64 capacity)
    {
        numCh = juce::jmax (0, channels);
        cap = juce::jmax<juce::int64> (1024, capacity);
        data.assign ((size_t) numCh, std::vector<float> ((size_t) cap, 0.0f));
        claimed.store (0); head.store (0);
    }
    /** Before the first push: the timeline begins at 'pos' instead of 0 (a session carried on after a stop: the samples before are gone anyway). */
    void startAt (juce::int64 pos) noexcept  { claimed.store (pos); head.store (pos); }
    int channels() const noexcept           { return numCh; }
    juce::int64 capacity() const noexcept   { return cap; }
    /** Samples written so far (the position the next block goes at). Any thread. */
    juce::int64 position() const noexcept   { return head.load (std::memory_order_acquire); }

    /** Audio thread: writes n samples of every channel (chans[c] may be nullptr: silence). Never waits, never fails. */
    void push (const float* const* chans, int n) noexcept
    {
        if (n <= 0 || numCh == 0) return;
        const juce::int64 h = head.load (std::memory_order_relaxed);
        claimed.store (h + n, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);                 // (claimed is seen before any slot is written over)
        const juce::int64 at = h % cap;
        const int first = (int) juce::jmin<juce::int64> (n, cap - at), second = n - first;
        for (int c = 0; c < numCh; ++c)
        {
            float* ring = data[(size_t) c].data();
            const float* src = chans[c];
            if (src != nullptr)
            {
                juce::FloatVectorOperations::copy (ring + at, src, first);
                if (second > 0) juce::FloatVectorOperations::copy (ring, src + first, second);
            }
            else
            {
                juce::FloatVectorOperations::clear (ring + at, first);
                if (second > 0) juce::FloatVectorOperations::clear (ring, second);
            }
        }
        std::atomic_thread_fence (std::memory_order_release);
        head.store (h + n, std::memory_order_release);
    }

    /** One drive's view of the ring: where it has read up to, and what happened when it read. */
    struct Reader
    {
        juce::int64 pos = 0;            // the next sample this reader wants
        juce::int64 gapAt = -1;         // the last take() found the samples gone: a gap of gapSamples starting at gapAt (pos was moved past it)
        juce::int64 gapSamples = 0;
    };

    /** How many samples 'r' could take now (0 when it is up to date). */
    juce::int64 available (const Reader& r) const noexcept { return juce::jmax<juce::int64> (0, position() - r.pos); }

    /** Drive thread: copies up to 'n' samples of every channel from r.pos into dest[c] (each at least n floats). Returns how many were copied
        (0 = nothing new yet) and moves r.pos on. If the samples were already written over (the reader had fallen more than the ring's length
        behind), nothing is copied: the reader is moved forward to the oldest sample still safely in the ring (an eighth of the ring behind the
        newest) - the gap it owes (from the old pos to the new) is put in r.gapAt / r.gapSamples for the caller to record - and 0 is returned; the
        next call reads on from there. */
    int take (Reader& r, float* const* dest, int n) const noexcept
    {
        r.gapAt = -1; r.gapSamples = 0;
        const juce::int64 h = head.load (std::memory_order_acquire);
        const int want = (int) juce::jmin<juce::int64> (n, h - r.pos);
        if (want <= 0) return 0;
        if (claimed.load (std::memory_order_acquire) > r.pos + cap) return fallBehind (r);     // (gone before we even started)
        const juce::int64 at = r.pos % cap;
        const int first = (int) juce::jmin<juce::int64> (want, cap - at), second = want - first;
        for (int c = 0; c < numCh; ++c)
        {
            const float* ring = data[(size_t) c].data();
            juce::FloatVectorOperations::copy (dest[c], ring + at, first);
            if (second > 0) juce::FloatVectorOperations::copy (dest[c] + first, ring, second);
        }
        std::atomic_thread_fence (std::memory_order_acquire);                 // (the copy's loads happen before the check below)
        if (claimed.load (std::memory_order_acquire) > r.pos + cap) return fallBehind (r);     // (written over while we copied)
        r.pos += want;
        return want;
    }

private:
    int fallBehind (Reader& r) const noexcept
    {
        const juce::int64 now = claimed.load (std::memory_order_acquire);
        const juce::int64 to = juce::jmax (r.pos, now - cap + cap / 8);
        r.gapAt = r.pos; r.gapSamples = to - r.pos; r.pos = to;
        return 0;
    }

    int numCh = 0;
    juce::int64 cap = 1024;
    std::vector<std::vector<float>> data;
    std::atomic<juce::int64> claimed { 0 }, head { 0 };
};
} // namespace td::live
