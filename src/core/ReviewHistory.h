#pragma once
#include "EditVersions.h"
#include "ReviewPage.h"
#include <map>

namespace td
{
namespace reviewpage
{
/** 3.51.0: the conversation so far, for a listening copy of the live version of 'live' (see PastLane). One lane for each version that has
    anything in it - the frozen ones and the live one - oldest first.

    Where: a comment made on Edit 1 is put where its music is in the live version (editversions::mapSeconds: the same take, the same place in
    it), then into the copy's own time ('offsetSeconds': where the copy's 0:00 is on the edit's timeline). One whose music is gone keeps its
    old place and is marked adrift. A thread - notes at one moment - stays at one moment: every note of it is given the place of its first.

    Which lane: every note is in the lane of the version whose list it is kept in - a listener's comment in the lane of the version it was made
    on, and the editor's reply to it (kept under that comment, whichever version was being made when it was written) with it. The page draws a
    lane as two lines, the listeners' comments and under them the editor's replies, and opens the newest round. (3.51.0 put an editor's note
    in the lane of the version being made when it was written, FeedbackNote::madeIn: the replies were then away from the comments they
    answer. Changed in 3.52.0, the user's workflow of 10 November 2026.)

    3.52.0, 'version': the copy is of that version (0, or the live version's number: the live edit). The conversation is then as it stood for
    that version - the lanes up to it, placed on ITS timeline - and what the editor wrote while making a later version is left out. */
inline std::vector<PastLane> historyFor (const EditDef& live, int version, double offsetSeconds)
{
    const int now = version > 0 && version < live.versionNumber() && live.versionEdit (version) != nullptr ? version : live.versionNumber();
    const EditDef& target = *live.versionEdit (now);
    std::map<int, PastLane> byVersion;
    for (int n = 1; n <= now; ++n)
    {
        const auto* ve = live.versionEdit (n);
        if (ve == nullptr || ve->feedback.empty()) continue;
        auto notes = ve->feedback;
        feedback::sortNotes (notes);
        for (auto& th : feedback::threads (notes))
        {
            bool adrift = false;
            const double at = editversions::mapSeconds (*ve, target, notes[(size_t) th.front()].seconds, &adrift) - offsetSeconds;
            for (int i : th)
            {
                const auto& f = notes[(size_t) i];
                // (written while a later version was being made: not in a copy of this one. A number above the live version's cannot be - see
                // editversions::settleMadeIn - and counts as the live version's)
                if (f.editor && juce::jmin (f.madeIn, live.versionNumber()) > now) continue;
                PastNote p;
                p.id = f.key; p.who = f.who.trim(); p.bar = f.bar; p.text = f.text; p.replyTo = f.replyTo;
                p.seconds = at; p.argb = feedback::personArgb (f); p.editor = f.editor; p.done = f.done; p.adrift = adrift;
                byVersion[n].notes.push_back (std::move (p));
            }
        }
    }
    std::vector<PastLane> lanes;
    for (auto& kv : byVersion)
    {
        auto& lane = kv.second;
        lane.version = kv.first; lane.current = kv.first == now;
        juce::String sub = kv.first == live.versionNumber() ? live.subheading.trim() : juce::String();
        if (const auto* v = live.version (kv.first)) sub = v->subheading.trim();
        lane.label = "Edit " + juce::String (kv.first) + (sub.isNotEmpty() ? " - " + sub : juce::String());
        std::stable_sort (lane.notes.begin(), lane.notes.end(), [] (const PastNote& a, const PastNote& b) { return a.seconds < b.seconds; });
        lanes.push_back (std::move (lane));
    }
    return lanes;
}
inline std::vector<PastLane> historyFor (const EditDef& live, double offsetSeconds) { return historyFor (live, 0, offsetSeconds); }
} // namespace reviewpage
} // namespace td
