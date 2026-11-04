#pragma once
/** fermata:Live 0.3.0: a station's copy of the session's waveform (the recorder's PeakTap): the points it has been sent live and the history it
    asked for, in blocks, so a station that started late fills in from both ends. Message thread only. */
#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace td::live
{
class PeakStore
{
public:
    static constexpr int kBlock = 4096;                                    // points per block

    void reset (int channels, int samplesPerPoint, int coarseFactor)
    {
        numCh = juce::jmax (0, channels); spp = juce::jmax (1, samplesPerPoint); coarseK = juce::jmax (1, coarseFactor);
        fine.clear(); coarse.clear(); ++generation;
    }
    int channels() const noexcept          { return numCh; }
    int samplesPerPoint() const noexcept   { return spp; }
    int coarseFactor() const noexcept      { return coarseK; }
    /** Changes whenever the store is cleared (a new session): a drawing cache keyed on it knows to start again. */
    int changeCount() const noexcept       { return generation; }
    /** Goes up with every add (a view repaints when it changes). */
    juce::uint32 addCount() const noexcept { return adds; }

    /** Points [first, first + n) of every channel: data[ch] = 2n bytes (min, max of each point), as PeakTap encodes them. */
    void add (bool coarseLevel, juce::int64 first, int n, const std::vector<juce::MemoryBlock>& data)
    {
        if (numCh == 0 || n <= 0 || (int) data.size() < numCh) return;
        ++adds;
        auto& level = coarseLevel ? coarse : fine;
        for (int i = 0; i < n; ++i)
        {
            const juce::int64 k = first + i;
            if (k < 0) continue;
            auto& b = blockFor (level, k / kBlock);
            const int at = (int) (k % kBlock);
            for (int c = 0; c < numCh; ++c)
            {
                const auto* src = static_cast<const juce::int8*> (data[(size_t) c].getData());
                if ((size_t) (2 * i + 1) >= data[(size_t) c].getSize()) continue;
                b.data[(size_t) c][(size_t) at * 2] = src[2 * i]; b.data[(size_t) c][(size_t) at * 2 + 1] = src[2 * i + 1];
            }
            if (! b.has[(size_t) at]) { b.has[(size_t) at] = 1; ++b.count; }
        }
        if (! coarseLevel) highest = juce::jmax (highest, first + n);
    }
    /** Does the store have point k (fine or coarse)? */
    bool hasFine (juce::int64 k) const     { return has (fine, k); }
    bool hasCoarse (juce::int64 k) const   { return has (coarse, k); }
    juce::int64 fineEnd() const noexcept   { return highest; }

    /** The lowest and highest sample of channel ch over the timeline's samples [s0, s1) (-1 .. 1). The fine points where they are here, the
        coarse ones for a stretch that has none and is long enough for them. False: nothing known there. */
    bool range (int ch, juce::int64 s0, juce::int64 s1, float& lo, float& hi) const
    {
        if (! juce::isPositiveAndBelow (ch, numCh) || s1 <= s0) return false;
        const juce::int64 k0 = s0 / spp, k1 = juce::jmax (k0 + 1, (s1 + spp - 1) / spp);
        juce::int8 l = 127, h = -127; int got = 0;
        if (k1 - k0 >= coarseK * 2)                                     // long: the coarse points cover the middle, the fine ones the two ends
        {
            const juce::int64 c0 = (k0 + coarseK - 1) / coarseK, c1 = k1 / coarseK;
            const int wantCoarse = (int) (c1 - c0);
            if (wantCoarse > 0 && foldRange (coarse, ch, c0, c1, l, h) == wantCoarse)
            {
                foldRange (fine, ch, k0, c0 * coarseK, l, h);
                foldRange (fine, ch, c1 * coarseK, k1, l, h);
                got = 1;
            }
            else { l = 127; h = -127; got = foldRange (fine, ch, k0, k1, l, h); }
        }
        else
        {
            got = foldRange (fine, ch, k0, k1, l, h);
            if (got == 0) got = foldRange (coarse, ch, k0 / coarseK, k0 / coarseK + 1, l, h);   // (zoomed in on a part known only coarsely)
        }
        if (got == 0) return false;
        lo = decode (l); hi = decode (h);
        return true;
    }
    static float decode (juce::int8 q) noexcept { const float s = (float) q / 127.0f; return q < 0 ? -s * s : s * s; }

    /** The newest point in [from, to) of the fine level that is not here (-1: none missing): the history is asked for newest first, so the
        part nearest the live end fills first. A block at a time. */
    juce::int64 lastMissingFine (juce::int64 from, juce::int64 to) const
    {
        from = juce::jmax ((juce::int64) 0, from);
        for (juce::int64 k = to - 1; k >= from;)
        {
            const juce::int64 bi = k / kBlock, bStart = juce::jmax (from, bi * kBlock);
            auto it = fine.find (bi);
            if (it == fine.end()) return k;
            if (it->second.count == kBlock) { k = bStart - 1; continue; }
            for (; k >= bStart; --k) if (! it->second.has[(size_t) (k - bi * kBlock)]) return k;
        }
        return -1;
    }
    /** The oldest point in [from, to) of the coarse level that is not here (to: none missing). */
    juce::int64 firstMissingCoarse (juce::int64 from, juce::int64 to) const
    {
        from = juce::jmax ((juce::int64) 0, from);
        for (juce::int64 k = from; k < to;)
        {
            const juce::int64 bi = k / kBlock, bEnd = juce::jmin (to, (bi + 1) * kBlock);
            auto it = coarse.find (bi);
            if (it == coarse.end()) return k;
            if (it->second.count == kBlock) { k = bEnd; continue; }
            for (; k < bEnd; ++k) if (! it->second.has[(size_t) (k - bi * kBlock)]) return k;
        }
        return to;
    }

private:
    struct Block { std::vector<std::vector<juce::int8>> data; std::vector<char> has; int count = 0; };
    using Level = std::map<juce::int64, Block>;
    Block& blockFor (Level& level, juce::int64 bi)
    {
        auto it = level.find (bi);
        if (it != level.end()) return it->second;
        Block b; b.data.assign ((size_t) numCh, std::vector<juce::int8> ((size_t) kBlock * 2, 0)); b.has.assign ((size_t) kBlock, 0);
        return level.emplace (bi, std::move (b)).first->second;
    }
    bool has (const Level& level, juce::int64 k) const
    {
        if (k < 0) return false;
        auto it = level.find (k / kBlock);
        return it != level.end() && it->second.has[(size_t) (k % kBlock)] != 0;
    }
    /** Folds the points [k0, k1) of one level into l / h, a block at a time; returns how many were there. */
    int foldRange (const Level& level, int ch, juce::int64 k0, juce::int64 k1, juce::int8& l, juce::int8& h) const
    {
        if (k0 < 0) k0 = 0;
        int got = 0;
        for (juce::int64 k = k0; k < k1;)
        {
            const juce::int64 bi = k / kBlock, bEnd = juce::jmin (k1, (bi + 1) * kBlock);
            auto it = level.find (bi);
            if (it == level.end()) { k = bEnd; continue; }
            const auto& b = it->second;
            const auto& d = b.data[(size_t) ch];
            for (; k < bEnd; ++k)
            {
                const auto at = (size_t) (k - bi * kBlock);
                if (! b.has[at]) continue;
                l = juce::jmin (l, d[at * 2]); h = juce::jmax (h, d[at * 2 + 1]); ++got;
            }
        }
        return got;
    }

    int numCh = 0, spp = 512, coarseK = 16, generation = 0;
    juce::uint32 adds = 0;
    juce::int64 highest = 0;
    Level fine, coarse;
};
} // namespace td::live
