#pragma once
/** 3.59.0: WaveColour while a take is being recorded (fermata). fermata colours a file once it is finished (WaveColorCache); a take still
    recording is drawn from the engine's live waveform instead, so it had no colours until Stop - and then only once its files had been
    worked out again from the disk. Now the engine feeds the sound of every track it records (AudioEngine::LiveFeed, while WaveColour is
    on); this works out its colours a block (4096 samples) at a time as it comes in, on a thread of its own at low priority, with the same
    analysis as the files (WaveColorCache::BlockAnalyser) - so a take looks the same while it records and after. When a take's file is
    closed, the colours worked out for it are handed to WaveColorCache (onFileColoured) and the finished take is coloured at once; a feed
    the reader fell behind on (a part of it not known) is not handed over: that file is worked out from the disk as before.

    Threads: watch / handOver / stop and the drawing are the message thread's; the analysis runs on its own thread. A feed's colours are
    under its own lock (held to add a block or to read the colours for a paint); the list of feeds under 'lock'. */
#include "../core/AudioEngine.h"
#include "WaveColor.h"
#include <juce_events/juce_events.h>
#include <mutex>

namespace td
{
class LiveWaveColours final : private juce::Thread
{
public:
    static_assert (AudioEngine::LiveFeed::kBlock == WaveColorCache::kBlock, "the live colours and the files' colours must use the same block");
    using Feed = AudioEngine::LiveFeed;

    LiveWaveColours() : juce::Thread ("Live waveform colours") {}
    ~LiveWaveColours() override { stop(); }

    /** Called on the message thread for a take's file whose colours were all worked out while it recorded, once the file is closed: they are
        the file's colours (give them to WaveColorCache::put). */
    std::function<void (const juce::File&, std::shared_ptr<WaveColorData>)> onFileColoured;

    /** Message thread, as often as it likes while takes record or are being finished: the engine's feeds (AudioEngine::liveFeeds). New ones are
        taken on; the others are kept until they are done with. */
    void watch (const std::vector<std::shared_ptr<Feed>>& feeds)
    {
        if (! alive->load()) return;
        bool added = false;
        {
            const std::lock_guard<std::mutex> g (lock);
            for (auto& f : feeds)
            {
                if (f == nullptr) continue;
                bool have = false;
                for (auto& j : jobs) have = have || j.feed == f;
                if (! have) { jobs.push_back ({ f }); added = true; }
            }
        }
        if (! added) return;
        if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
        notify();
    }

    /** Message thread: these take files have just been finished and taken in (AppContext::takeInFinishedTakes - after the windows forgot their
        old pictures): the colours worked out for them go to the files now, or as soon as they are complete. */
    void handOver (const std::vector<juce::File>& files)
    {
        std::vector<std::shared_ptr<Feed>> now;
        {
            const std::lock_guard<std::mutex> g (lock);
            for (auto& j : jobs)
                if (std::find (files.begin(), files.end(), j.feed->file) != files.end()) j.wanted = true;
            now = takeReady();
        }
        for (auto& f : now) give (*f);
    }

    /** Closing: the analysis stops (waits for it, at most 4 s). Safe to call again. */
    void stop()
    {
        alive->store (false);
        signalThreadShouldExit(); notify(); stopThread (4000);
        const std::lock_guard<std::mutex> g (lock);
        jobs.clear();
    }

    /** The colour of each of n pixel columns of a take that records (or is being finished), the first from 'firstSeconds' into the take,
        'secondsPerPixel' each: the loudest block's colour in the column, 'none' where nothing is known yet. False when nothing is known at all.
        Message thread (the paint). */
    static bool colours (const Feed& f, double firstSeconds, double secondsPerPixel, int n, std::vector<juce::Colour>& out, juce::Colour none)
    {
        out.assign ((size_t) juce::jmax (0, n), none);
        const double blockSeconds = (double) Feed::kBlock / f.sampleRate;
        const std::lock_guard<std::mutex> g (f.lock);
        const auto nb = (juce::int64) f.argb.size();
        if (nb == 0 || blockSeconds <= 0.0) return false;
        bool any = false;
        for (int i = 0; i < n; ++i)
        {
            const double t0 = firstSeconds + (double) i * secondsPerPixel, t1 = t0 + secondsPerPixel;
            const auto b0 = juce::jmax ((juce::int64) 0, (juce::int64) std::floor (t0 / blockSeconds));
            const auto b1 = juce::jmin (nb, juce::jmax (b0 + 1, (juce::int64) std::ceil (t1 / blockSeconds)));
            float best = -1.0f; juce::uint32 c = 0;
            for (auto k = b0; k < b1; ++k)                                       // (3.61.0: as the knock weighting draws it)
                if (f.argb[(size_t) k] != 0 && f.level[(size_t) k] > best)
                { best = f.level[(size_t) k]; c = knockweight::shownAt (f.argb, f.level, f.low, (size_t) k); }
            if (c != 0) { out[(size_t) i] = juce::Colour (c); any = true; }
        }
        return any;
    }

private:
    static constexpr int kBlock = Feed::kBlock;
    struct Job
    {
        std::shared_ptr<Feed> feed;
        juce::int64 taken = 0;             // the next sample to work out (the analysis thread's)
        bool wanted = false;               // its file is finished and taken in: hand the colours over once complete
    };

    void run() override
    {
        WaveColorCache::BlockAnalyser an;
        std::vector<float> buf ((size_t) kBlock), half ((size_t) kBlock / 2);
        std::vector<double> mono ((size_t) kBlock), before ((size_t) kBlock / 2);
        // 3.61.0: the half block before the one worked out, for the knock weighting's second window (nullptr when the ring no longer holds it)
        auto beforeOf = [&] (const Feed& f, juce::int64 at) -> const double*
        {
            if (at < kBlock / 2 || ! f.read (at - kBlock / 2, half.data(), kBlock / 2)) return nullptr;
            for (int i = 0; i < kBlock / 2; ++i) before[(size_t) i] = half[(size_t) i];
            return before.data();
        };
        while (! threadShouldExit())
        {
            std::vector<std::pair<std::shared_ptr<Feed>, juce::int64>> todo;     // (copies: the list may change meanwhile)
            {
                const std::lock_guard<std::mutex> g (lock);
                for (auto& j : jobs) todo.push_back ({ j.feed, j.taken });
            }
            bool didSome = false, finishedOne = false;
            for (auto& [feed, start] : todo)
            {
                auto& f = *feed;
                juce::int64 taken = start;
                bool finished = false;
                for (int k = 0; k < 32 && ! threadShouldExit(); ++k)          // (a batch at most, so every track keeps up)
                {
                    {
                        const std::lock_guard<std::mutex> g (f.lock);
                        if (f.complete) break;
                    }
                    const auto w = f.written();
                    const bool ended = f.ended.load (std::memory_order_acquire) && w == f.written();
                    juce::uint32 argb = 0; float level = 0.0f; juce::uint8 low = 0; int blocks = 1;
                    if (w - taken >= kBlock)
                    {
                        if (! f.read (taken, buf.data(), kBlock))
                        {   // fell behind: what was not read is not known (left plain); on from the oldest block the ring still holds
                            const auto next = juce::jmax (taken + kBlock, ((w - f.capacity() + kBlock - 1) / kBlock + 1) * kBlock);
                            blocks = (int) ((next - taken) / kBlock);
                            const std::lock_guard<std::mutex> g (f.lock);
                            f.argb.insert (f.argb.end(), (size_t) blocks, 0u); f.level.insert (f.level.end(), (size_t) blocks, 0.0f); f.low.insert (f.low.end(), (size_t) blocks, (juce::uint8) 0);
                            f.gap = true;
                            taken = next; didSome = true;
                            continue;
                        }
                        for (int i = 0; i < kBlock; ++i) mono[(size_t) i] = buf[(size_t) i];
                        argb = an.colour (mono.data(), f.sampleRate, level, &low, beforeOf (f, taken)).getARGB();
                    }
                    else if (ended)
                    {   // the end of the take: its last part-block, padded with silence (as the file's own colours are), then complete
                        const int rest = (int) (w - taken);
                        if (rest > 0)
                        {
                            if (! f.read (taken, buf.data(), rest)) { const std::lock_guard<std::mutex> g (f.lock); f.gap = true; f.complete = true; finished = true; break; }
                            for (int i = 0; i < kBlock; ++i) mono[(size_t) i] = i < rest ? buf[(size_t) i] : 0.0;
                            argb = an.colour (mono.data(), f.sampleRate, level, &low, beforeOf (f, taken)).getARGB();
                        }
                        const std::lock_guard<std::mutex> g (f.lock);
                        if (rest > 0) { f.argb.push_back (argb); f.level.push_back (level); f.low.push_back (low); }
                        f.complete = true;
                        taken = w; finished = true; didSome = true;
                        break;
                    }
                    else break;                                                  // (the next block has not come yet)
                    {
                        const std::lock_guard<std::mutex> g (f.lock);
                        f.argb.push_back (argb); f.level.push_back (level); f.low.push_back (low);
                    }
                    taken += kBlock; didSome = true;
                }
                finishedOne = finishedOne || finished;
                const std::lock_guard<std::mutex> g (lock);
                for (auto& j : jobs) if (j.feed == feed) j.taken = taken;
            }
            if (finishedOne)
            {   // a take's colours are complete: if its file has been taken in already, they go to it now (on the message thread)
                bool any = false;
                { const std::lock_guard<std::mutex> g (lock); for (auto& j : jobs) any = any || j.wanted; }
                if (any)
                    juce::MessageManager::callAsync ([this, a = alive]
                    {
                        if (! a->load()) return;
                        std::vector<std::shared_ptr<Feed>> now;
                        { const std::lock_guard<std::mutex> g (lock); now = takeReady(); }
                        for (auto& f : now) give (*f);
                    });
            }
            todo.clear();
            {   // (a job leaves the list when its colours are handed over - takeReady; only one whose take never came to be finished - the audio
                // thread stalled at Stop - could stay: the list is kept from growing without end all the same)
                const std::lock_guard<std::mutex> g (lock);
                while (jobs.size() > 1024) jobs.erase (jobs.begin());
            }
            if (! didSome) wait (40);
        }
    }

    /** (under 'lock') The feeds whose colours can go to their files now - complete, nothing missing, the file closed and taken in - taken off the list. */
    std::vector<std::shared_ptr<Feed>> takeReady()
    {
        std::vector<std::shared_ptr<Feed>> now;
        for (size_t i = jobs.size(); i-- > 0;)
        {
            auto& j = jobs[i];
            if (! j.wanted || ! j.feed->fileDone.load (std::memory_order_acquire)) continue;     // (first: what the finisher marked before it is seen below)
            bool complete = false, gap = false;
            { const std::lock_guard<std::mutex> fg (j.feed->lock); complete = j.feed->complete; gap = j.feed->gap; }
            if (! complete) continue;
            if (! gap) now.push_back (j.feed);
            jobs.erase (jobs.begin() + (std::ptrdiff_t) i);                   // (with a gap: the file is worked out from the disk instead)
        }
        return now;
    }
    /** Message thread: the colours of a complete feed, as the file's. */
    void give (const Feed& f)
    {
        auto d = std::make_shared<WaveColorData>();
        d->blockSeconds = (double) kBlock / f.sampleRate;
        { const std::lock_guard<std::mutex> g (f.lock); d->argb = f.argb; d->level = f.level; d->low = f.low; }
        if (onFileColoured != nullptr && ! d->argb.empty()) onFileColoured (f.file, d);
    }

    std::mutex lock;
    std::vector<Job> jobs;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace td
