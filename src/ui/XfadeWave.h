#pragma once
#include "WaveDraw.h"
#include "../core/Edit.h"
#include "../core/AudioFormats.h"
#include "../core/CrashReport.h"
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace td
{
/** 3.45.0: what a crossfade will sound like, for the edit window. Through the crossfade between two pieces, on one track, the sound is the first piece's
    audio times its fade-out plus the second's times its fade-in (EditRegion::gainAt - the curves the engine plays). The edit window used to draw the first
    piece's file up to the join and then the second's, a hard switch that is never heard; it now draws this mix through the whole crossfade.

    The mix needs the samples of both files over the crossfade, so it is worked out on a thread of its own (a crossfade is short: a few thousand samples
    a track), kept as a min / max outline (one point per sample for a crossfade up to about a third of a second at 96 kHz, coarser for longer ones), and
    the window repaints when it is ready; until then the old picture shows. A file rewritten in place (a relinked RX file) is noticed within five seconds,
    and so is a missing file that has come back (3.46.0: a mix that cannot be made is remembered as such until then).
    The fades only - not the pieces' volume changes - are applied, as the rest of the waveform is drawn as the files are. */
class XfadeWaveCache
{
public:
    /** A piece's fades on the timeline (samples), and the gain they give at t: EditRegion::gainAt. */
    struct Fades
    {
        juce::int64 ib = 0, ie = 0, ob = 0, oe = 0; FadeCurve in = FadeCurve::EqualPower, out = FadeCurve::EqualPower;
        static Fades of (const EditRegion& r) { return { r.fadeInBegin(), r.fadeInFinish(), r.fadeOutBegin(), r.fadeOutFinish(), r.curve, r.outCurveValue() }; }
        float gainAt (juce::int64 t) const noexcept
        {
            float g = 1.0f;
            if (ie > ib) g *= t < ib ? 0.0f : t >= ie ? 1.0f : EditRegion::fadeGainFor (in, (float) (t - ib) / (float) (ie - ib), true);
            else         g *= t < ib ? 0.0f : 1.0f;
            if (oe > ob) g *= t < ob ? 1.0f : t >= oe ? 0.0f : EditRegion::fadeGainFor (out, (float) (t - ob) / (float) (oe - ob), false);
            else         g *= t < ob ? 1.0f : 0.0f;
            return g;
        }
        juce::String key() const { return juce::String (ib) + "," + juce::String (ie) + "," + juce::String (ob) + "," + juce::String (oe) + "," + juce::String ((int) in) + "," + juce::String ((int) out); }
    };
    /** One side of a crossfade on one track: its file (none: silence on that side), where its audio comes from (file sample = timeline sample + offset),
        and its fades. */
    struct Side { juce::File file; juce::int64 offset = 0; Fades fades; };
    /** The mix over [z0, z1) of the timeline: per channel, the lowest and highest sample of every 'spp' samples (16-bit, as the thumbnails). */
    struct Mix
    {
        juce::int64 z0 = 0, z1 = 0; int spp = 1, channels = 0;
        std::vector<std::vector<juce::int16>> mn, mx;
        juce::int64 bytes() const { juce::int64 b = 0; for (auto& v : mn) b += (juce::int64) v.size() * 4; return b; }
        /** The lowest and highest sample of channel c over timeline samples [t0, t1). False when [t0, t1) is outside the mix. */
        bool minMax (int c, double t0, double t1, float& lo, float& hi) const
        {
            if (c < 0 || c >= channels || t1 <= (double) z0 || t0 >= (double) z1) return false;
            const auto& a = mn[(size_t) c]; const auto& b = mx[(size_t) c];
            const juce::int64 n = (juce::int64) a.size();
            juce::int64 p0 = (juce::int64) std::floor ((t0 - (double) z0) / spp), p1 = (juce::int64) std::ceil ((t1 - (double) z0) / spp);
            p0 = juce::jlimit ((juce::int64) 0, n - 1, p0); p1 = juce::jlimit (p0 + 1, n, p1);
            int l = 32767, h = -32768;
            for (juce::int64 p = p0; p < p1; ++p) { l = juce::jmin (l, (int) a[(size_t) p]); h = juce::jmax (h, (int) b[(size_t) p]); }
            lo = (float) l / 32767.0f; hi = (float) h / 32767.0f;
            return true;
        }
    };

    std::function<void()> onReady;                                               // message thread: a mix asked for is ready (repaint)
    XfadeWaveCache() : shared (std::make_shared<Shared>()), worker (shared) { shared->ready = [this] { if (onReady) onReady(); }; }     // (called only while 'alive': see Worker)
    ~XfadeWaveCache() { shared->alive = false; worker.stop(); }

    /** The mix of a and b over [z0, z1), or null while it is being worked out (it is asked for here; onReady is called when it is ready). */
    std::shared_ptr<const Mix> get (const Side& a, const Side& b, juce::int64 z0, juce::int64 z1)
    {
        if (z1 <= z0 || z1 - z0 > kMaxSamples) return nullptr;
        const auto key = a.file.getFullPathName() + "|" + juce::String (a.offset) + "|" + a.fades.key() + "#" + b.file.getFullPathName() + "|" + juce::String (b.offset) + "|" + b.fades.key()
                         + "#" + juce::String (z0) + "-" + juce::String (z1);
        const auto now = juce::Time::getMillisecondCounter();
        {
            std::lock_guard<std::mutex> l (shared->lock);
            auto it = shared->done.find (key);
            if (it != shared->done.end())
            {
                auto& e = it->second;
                e.used = ++tick;
                if (now - e.checkedAt > 5000)                                     // a file rewritten in place: worked out again
                {
                    e.checkedAt = now;
                    if (modOf (a.file) != e.modA || modOf (b.file) != e.modB) { shared->done.erase (it); it = shared->done.end(); }
                }
                if (it != shared->done.end()) return it->second.mix;        // (null for one that could not be made: drawn as before, not asked for again)
            }
            if (shared->pending.count (key) > 0) return nullptr;
            shared->pending.insert (key);
            shared->jobs.push_back ({ key, a, b, z0, z1 });
            trimLocked();
        }
        worker.wakeUp();
        return nullptr;
    }
    /** Forget everything (the waveforms are being drawn again). */
    void clear() { std::lock_guard<std::mutex> l (shared->lock); shared->done.clear(); }

private:
    static constexpr juce::int64 kMaxSamples = 30 * 96000;          // a crossfade longer than 30 s at 96 kHz is drawn as before
    static constexpr juce::int64 kMaxPoints = 32768;                 // the outline's points per channel at most
    static constexpr juce::int64 kBudgetBytes = 64ll * 1024 * 1024;
    struct Job { juce::String key; Side a, b; juce::int64 z0 = 0, z1 = 0; };
    struct Entry { std::shared_ptr<const Mix> mix; juce::Time modA, modB; juce::int64 used = 0; juce::uint32 checkedAt = 0; };
    struct Shared
    {
        std::mutex lock;
        std::map<juce::String, Entry> done; std::set<juce::String> pending; std::deque<Job> jobs;
        std::atomic<bool> alive { true };
        std::function<void()> ready;                                   // message thread (set once, by the cache)
    };
    static juce::Time modOf (const juce::File& f) { return f == juce::File() ? juce::Time() : f.getLastModificationTime(); }
    void trimLocked()
    {
        for (;;)
        {
            juce::int64 total = 0; auto oldest = shared->done.end();
            for (auto it = shared->done.begin(); it != shared->done.end(); ++it)
            {
                total += it->second.mix != nullptr ? it->second.mix->bytes() : 0;
                if (oldest == shared->done.end() || it->second.used < oldest->second.used) oldest = it;
            }
            if (total <= kBudgetBytes || oldest == shared->done.end()) return;
            shared->done.erase (oldest);
        }
    }

    /** Reads [a, b) of one file, every channel (zeros where the file has nothing). */
    static bool readSpan (juce::AudioFormatManager& fm, const juce::File& f, juce::int64 a, juce::int64 b, juce::AudioBuffer<float>& out)
    {
        const int n = (int) (b - a);
        out.setSize (1, juce::jmax (1, n)); out.clear();
        if (f == juce::File()) return true;                            // (no file on this side: silence)
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr || r->numChannels < 1) return false;
        out.setSize ((int) r->numChannels, juce::jmax (1, n)); out.clear();
        const juce::int64 ra = juce::jmax ((juce::int64) 0, a), rb = juce::jmin (r->lengthInSamples, b);
        if (rb <= ra) return true;
        juce::AudioBuffer<float> tmp ((int) r->numChannels, (int) (rb - ra));
        if (! r->read (&tmp, 0, (int) (rb - ra), ra, true, true)) return false;
        for (int c = 0; c < tmp.getNumChannels(); ++c) out.copyFrom (c, (int) (ra - a), tmp, c, 0, tmp.getNumSamples());
        return true;
    }
    static std::shared_ptr<Mix> work (juce::AudioFormatManager& fm, const Job& j)
    {
        juce::AudioBuffer<float> A, B;
        if (! readSpan (fm, j.a.file, j.z0 + j.a.offset, j.z1 + j.a.offset, A) || ! readSpan (fm, j.b.file, j.z0 + j.b.offset, j.z1 + j.b.offset, B)) return nullptr;
        const juce::int64 n = j.z1 - j.z0;
        auto m = std::make_shared<Mix>();
        m->z0 = j.z0; m->z1 = j.z1;
        m->channels = juce::jmax (j.a.file == juce::File() ? 1 : A.getNumChannels(), j.b.file == juce::File() ? 1 : B.getNumChannels());
        m->spp = (int) juce::jmax ((juce::int64) 1, (n + kMaxPoints - 1) / kMaxPoints);
        const juce::int64 points = (n + m->spp - 1) / m->spp;
        std::vector<float> ga ((size_t) n), gb ((size_t) n);
        for (juce::int64 i = 0; i < n; ++i) { ga[(size_t) i] = j.a.fades.gainAt (j.z0 + i); gb[(size_t) i] = j.b.fades.gainAt (j.z0 + i); }
        const auto q = [] (float v) { return (juce::int16) juce::jlimit (-32767, 32767, juce::roundToInt (v * 32767.0f)); };
        for (int c = 0; c < m->channels; ++c)
        {
            const float* pa = A.getReadPointer (juce::jmin (c, A.getNumChannels() - 1));
            const float* pb = B.getReadPointer (juce::jmin (c, B.getNumChannels() - 1));
            std::vector<juce::int16> lo ((size_t) points), hi ((size_t) points);
            for (juce::int64 p = 0; p < points; ++p)
            {
                float l = 1.0e9f, h = -1.0e9f;
                for (juce::int64 i = p * m->spp; i < juce::jmin (n, (p + 1) * m->spp); ++i)
                {
                    const float v = pa[i] * ga[(size_t) i] + pb[i] * gb[(size_t) i];
                    l = juce::jmin (l, v); h = juce::jmax (h, v);
                }
                lo[(size_t) p] = q (l); hi[(size_t) p] = q (h);
            }
            m->mn.push_back (std::move (lo)); m->mx.push_back (std::move (hi));
        }
        return m;
    }

    /** The thread that works the mixes out, one at a time, at low priority. */
    struct Worker : public juce::Thread
    {
        explicit Worker (std::shared_ptr<Shared> s) : juce::Thread ("Crossfade waveforms"), shared (std::move (s)) {}
        void wakeUp() { if (! isThreadRunning()) startThread (juce::Thread::Priority::low); notify(); }
        void stop() { signalThreadShouldExit(); notify(); stopThread (20000); }
        void run() override
        {
            td::crashreport::nameThisThread (getThreadName());
            juce::AudioFormatManager fm; td::registerAudioFormats (fm);
            while (! threadShouldExit())
            {
                Job j; bool have = false;
                { std::lock_guard<std::mutex> l (shared->lock); if (! shared->jobs.empty()) { j = std::move (shared->jobs.back()); shared->jobs.pop_back(); have = true; } }      // (the newest first: what was asked for last is on the screen now)
                if (! have) { wait (-1); continue; }
                auto mix = work (fm, j);
                Entry e; e.mix = mix; e.modA = modOf (j.a.file); e.modB = modOf (j.b.file); e.checkedAt = juce::Time::getMillisecondCounter();
                {
                    std::lock_guard<std::mutex> l (shared->lock);
                    shared->pending.erase (j.key);
                    // 3.46.0: a mix that could not be made (a file that is missing or will not open) is kept too, as "none": it used to be forgotten,
                    // so the repaint below asked for it again, which failed again and repainted again - the edit window drew itself without end, and
                    // this thread never rested, for as long as a crossfade with a missing file was in view. It is tried again when the file changes
                    // (get() looks every five seconds), not before.
                    shared->done[j.key] = std::move (e);
                    if (mix == nullptr) continue;                           // (nothing new to draw: the old picture stays)
                    if (! shared->jobs.empty()) continue;                   // (more to do: one repaint when the batch is done)
                }
                std::weak_ptr<Shared> weak (shared);
                juce::MessageManager::callAsync ([weak]
                {
                    if (auto s = weak.lock()) if (s->alive) { std::function<void()> r; { std::lock_guard<std::mutex> l (s->lock); r = s->ready; } if (r) r(); }
                });
            }
        }
        std::shared_ptr<Shared> shared;
    };

    std::shared_ptr<Shared> shared;
    juce::int64 tick = 0;
    Worker worker;                                                    // (last: stopped first)
};
} // namespace td
