#pragma once
#include <atomic>
#include <juce_core/juce_core.h>

namespace td
{
/** What the message thread is busy with just now, in a few words (a string literal), for the stall watch's log lines. Set by BusyLabel.
    (3.52.0: in a file of its own - it was in StallWatch.h - so that the shared windows can label their drawing too: the noise and ReTouch
    picture had no label, and a stutter there showed in the log as "nothing labelled". In fermata:Live nothing reads the label.) */
inline std::atomic<const char*>& messageThreadLabel() noexcept { static std::atomic<const char*> l { nullptr }; return l; }

/** Labels a stretch of message-thread work for the stall watch: "starting playback", "drawing the edit window" ... (nested: the inner one shows). */
struct BusyLabel
{
    explicit BusyLabel (const char* what) noexcept : previous (messageThreadLabel().exchange (what)) {}
    ~BusyLabel() { messageThreadLabel().store (previous); }
    const char* previous;
    JUCE_DECLARE_NON_COPYABLE (BusyLabel)
};
} // namespace td
