#pragma once
#include <juce_events/juce_events.h>
#include <atomic>
#include <vector>
#include "../core/CoreReservation.h"
#include "../core/CrashReport.h"

namespace td
{
/** What the message thread is busy with just now, in a few words (a string literal), for the stall watch's log lines. Set by BusyLabel. */
inline std::atomic<const char*>& messageThreadLabel() noexcept { static std::atomic<const char*> l { nullptr }; return l; }

/** Labels a stretch of message-thread work for the stall watch: "starting playback", "drawing the edit window" ... (nested: the inner one shows). */
struct BusyLabel
{
    explicit BusyLabel (const char* what) noexcept : previous (messageThreadLabel().exchange (what)) {}
    ~BusyLabel() { messageThreadLabel().store (previous); }
    const char* previous;
    JUCE_DECLARE_NON_COPYABLE (BusyLabel)
};

/** 3.45.1: the plug-in the message thread is starting just now, for the stall watch. Some plug-ins take many seconds to start (a big reverb checking its
    own installation and starting its threads: Seventh Heaven Professional took 19 s on 9 November 2026), and Fermata can only wait for them - a VST3
    plug-in is made on the main thread. The stall watch names the plug-in in its line instead of writing a freeze report for it (see StallWatch). */
struct PluginStarting
{
    static PluginStarting& get() { static PluginStarting p; return p; }
    std::atomic<int> depth { 0 };
    juce::SpinLock lock; juce::String name;                                 // (the one being started now; the last one, after)
    juce::String current() { const juce::SpinLock::ScopedLockType sl (lock); return name; }
};

/** Labels the start of one plug-in (the message thread). */
struct PluginStartLabel
{
    explicit PluginStartLabel (const juce::String& pluginName) : label ("starting a plug-in")
    {
        auto& p = PluginStarting::get();
        { const juce::SpinLock::ScopedLockType sl (p.lock); p.name = pluginName; }
        p.depth.fetch_add (1);
    }
    ~PluginStartLabel() { PluginStarting::get().depth.fetch_sub (1); }
    BusyLabel label;
    JUCE_DECLARE_NON_COPYABLE (PluginStartLabel)
};

/** Watches the message thread (2.102): a timer on it beats 20 times a second, and a thread of its own notices when the beats stop. Whenever the
    message thread is held up for 150 ms or more - nothing on the screen moves, Space and Stop wait - a line goes into the log once it is free
    again: how long, at what time, and what it was doing (the labels seen meanwhile, see BusyLabel). So a stall the person notices can be found
    in the log afterwards, with its cause. At most 30 lines a minute.
    Since 3.13.0 each line also says how hard the main thread itself was working through the stall, how much of the processor Fermata and the whole
    PC were using, and whether a laptop was on battery - so a stall with no label can be told apart: Fermata's own drawing (main thread working),
    a PC that is flat out (something else, or too few cores left), or the main thread waiting on Windows or a driver.
    Since 3.14.0, when the main thread has been stuck for 15 seconds a freeze report is written (a minidump: where every thread is at that
    moment, see CrashReport.h) - at most two a session - so a freeze that needs Fermata ending from the Task Manager can still be traced.
    3.17.0: after 5 seconds stuck, where every thread is goes into the log as text (at most five times a session), and the 15-second report is
    written by a helper process (crashreport::writeViaHelper) - the one written from inside the stuck program was left at 0 bytes.
    3.45.1: while a plug-in is being started (PluginStartLabel), the main thread is waiting for the plug-in, not stuck in Fermata. The time spent so is
    not counted towards the 5-second stacks and the 15-second freeze report (only Fermata's own time is), and the line names the plug-in that took the
    longest and how long; a stall that goes on for 60 s / 120 s in all still gets them, so a plug-in that never finishes starting is still reported.
    (Three of the four freeze reports of 9 November 2026 were a reverb starting up, and Fermata then said it had got stuck.) */
class StallWatch : private juce::Timer, private juce::Thread
{
public:
    StallWatch() : juce::Thread ("Fermata stall watch")
    {
        beat.store (juce::Time::getMillisecondCounterHiRes());
        SystemLoad::rememberMessageThread();                                // (constructed on the message thread)
        mainThreadId = (unsigned long) (juce::pointer_sized_int) juce::Thread::getCurrentThreadId();     // (Windows' own id of the message thread)
        startTimerHz (20);
        startThread (juce::Thread::Priority::low);
    }
    ~StallWatch() override
    {
        stopTimer();
        signalThreadShouldExit(); notify(); stopThread (2000);
    }

    /** 3.46.1: the message thread is in a wait it means to be in (closing: the last take's files being finished on the disk) and says so every few
        tens of milliseconds, as its timer would - so that wait is not written up as a freeze. Message thread. */
    void touch() noexcept { beat.store (juce::Time::getMillisecondCounterHiRes()); }

    static constexpr double kPeriodMs = 50.0, kReportMs = 150.0, kStacksMs = 5000.0, kFreezeReportMs = 15000.0;
    static constexpr double kPluginStacksMs = 60000.0, kPluginFreezeReportMs = 120000.0;      // (3.45.1: while a plug-in is starting)

private:
    void timerCallback() override { beat.store (juce::Time::getMillisecondCounterHiRes()); }

    void run() override
    {
        td::crashreport::nameThisThread (getThreadName());                // (3.17.0: named for the log)
        bool inStall = false;
        double lastBeatBefore = 0.0;
        const char* seen[4] = {};
        int nSeen = 0, linesThisMinute = 0, suppressed = 0;
        double minuteStart = juce::Time::getMillisecondCounterHiRes();
        SystemLoad::Snapshot atStart;
        double lastLoop = juce::Time::getMillisecondCounterHiRes(), watchedSince = 0.0;
        int freezeReports = 0, stackReports = 0;
        bool reportedThisStall = false, stacksThisStall = false;
        std::vector<std::pair<juce::String, double>> pluginsSeen;          // (3.45.1: the plug-ins started during this stall, and for how long)
        double pluginMs = 0.0, lastT = 0.0;
        while (! threadShouldExit())
        {
            wait (20);
            const double t = juce::Time::getMillisecondCounterHiRes();
            const double b = beat.load();
            if (t - lastLoop > 2000.0) watchedSince = 0.0;                   // this thread did not run either (the PC was asleep): not a freeze
            lastLoop = t;
            if (t - b > kPeriodMs + kReportMs * 0.5)                 // the beats have stopped: the message thread is busy
            {
                if (! inStall) { inStall = true; lastBeatBefore = b; nSeen = 0; atStart = SystemLoad::take(); watchedSince = t; reportedThisStall = false; stacksThisStall = false; pluginsSeen.clear(); pluginMs = 0.0; lastT = t; }
                if (watchedSince <= 0.0) watchedSince = t;
                if (PluginStarting::get().depth.load() > 0)                      // 3.45.1: waiting for a plug-in to start, not stuck in Fermata
                {
                    const double dt = juce::jmax (0.0, t - lastT);
                    pluginMs += dt;
                    const auto nm = PluginStarting::get().current();
                    bool have = false;
                    for (auto& ps : pluginsSeen) if (ps.first == nm) { ps.second += dt; have = true; }
                    if (! have && pluginsSeen.size() < 16) pluginsSeen.push_back ({ nm, dt });
                }
                lastT = t;
                const double total = t - watchedSince, own = total - pluginMs;    // (own: the main thread's time in Fermata itself)
                // 3.17.0: after 5 s, where every part of Fermata is, as text in the log (read on the user's PC with Fermata.pdb: function names and lines)
                if (! stacksThisStall && stackReports < 5 && (own >= kStacksMs || total >= kPluginStacksMs))
                {
                    stacksThisStall = true;
                    const auto where = crashreport::describeThreads (mainThreadId);
                    if (where.isNotEmpty())
                    {
                        ++stackReports;
                        juce::Logger::writeToLog ("Stall watch: the main thread has been stuck for " + juce::String (juce::roundToInt ((t - watchedSince) / 1000.0))
                                                  + " s. Where every part of Fermata is now (innermost first):\n" + where.trimEnd());
                    }
                }
                // after 15 s, a freeze report: written by a separate helper process (one written from inside a stuck program can get stuck itself)
                if (! reportedThisStall && freezeReports < 2 && (own >= kFreezeReportMs || total >= kPluginFreezeReportMs))
                {
                    reportedThisStall = true;
                    const int secs = juce::roundToInt ((t - watchedSince) / 1000.0);
                    juce::String why;
                    auto f = crashreport::writeViaHelper ("freeze", why);
                    if (f == juce::File())
                    {
                        juce::Logger::writeToLog ("Stall watch: the freeze report could not be written by the helper (" + why + "): writing it from here");
                        f = crashreport::write (nullptr, "freeze");
                    }
                    if (f != juce::File())
                    {
                        ++freezeReports;
                        juce::Logger::writeToLog ("Stall watch: the main thread has been stuck for " + juce::String (secs) + " s - a freeze report was written: " + f.getFullPathName());
                    }
                }
                if (const char* l = messageThreadLabel().load())
                {
                    bool have = false;
                    for (int i = 0; i < nSeen; ++i) have = have || seen[i] == l;
                    if (! have && nSeen < 4) seen[nSeen++] = l;
                }
                continue;
            }
            if (! inStall) continue;
            inStall = false;
            const double busy = (b - lastBeatBefore) - kPeriodMs;
            if (busy < kReportMs) continue;
            count.fetch_add (1);
            if (busy > longest.load()) longest.store (busy);
            if (t - minuteStart > 60000.0) { minuteStart = t; linesThisMinute = 0; }
            if (++linesThisMinute > 30) { ++suppressed; continue; }
            juce::String what;
            for (int i = 0; i < nSeen; ++i) what << (i > 0 ? ", " : "") << seen[i];
            juce::String line;
            if (busy > 30000.0 && pluginMs < busy * 0.5)                    // (3.45.1: not when it was waiting for a plug-in)
                line << "Stall watch: the program did not respond for " << juce::String (busy / 1000.0, 0) << " s (the computer may have been asleep)";
            else
                line << "Stall watch: main thread busy for " << juce::roundToInt (busy) << " ms at " << juce::Time::getCurrentTime().formatted ("%H:%M:%S")
                     << " (" << (what.isNotEmpty() ? what : juce::String ("nothing labelled: drawing or Windows")) << ")";
            if (! pluginsSeen.empty() && busy <= 30000.0 + kPluginFreezeReportMs)              // 3.45.1: which plug-in it waited for
            {
                const auto* top = &pluginsSeen.front();
                for (auto& ps : pluginsSeen) if (ps.second > top->second) top = &ps;
                line << "; " << juce::String (pluginMs / 1000.0, 1) << " s of it waiting for " << (pluginsSeen.size() == 1 ? "a plug-in" : juce::String ((int) pluginsSeen.size()) + " plug-ins")
                     << " to start (the longest: " << top->first << ", " << juce::String (top->second / 1000.0, 1) << " s) - a plug-in's own start-up, which Fermata has to wait for";
            }
            if (busy <= 30000.0)
                if (const auto load = SystemLoad::describe (atStart, SystemLoad::take(), true); load.isNotEmpty()) line << "; " << load;
            if (suppressed > 0) { line << "; " << suppressed << " more in the minute before were not logged"; suppressed = 0; }
            juce::Logger::writeToLog (line);
        }
    }

    unsigned long mainThreadId = 0;
    std::atomic<double> beat { 0.0 };
    std::atomic<int> count { 0 };
    std::atomic<double> longest { 0.0 };
};
} // namespace td
