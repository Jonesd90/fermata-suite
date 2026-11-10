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

    Which lane: a listener's comment is in the lane of the version it was made on (the one whose list it is kept in). A note of the editor's
    is in the lane of the version that was being made when it was written (FeedbackNote::madeIn) - a reply to a comment on Edit 1, written
    while making Edit 2, is kept under that comment in the project but shown in Edit 2's lane: it is what the editor says about Edit 2. So
    the live version's lane is "the editor's replies on this version". */
inline std::vector<PastLane> historyFor (const EditDef& live, double offsetSeconds)
{
    const int now = live.versionNumber();
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
            const double at = editversions::mapSeconds (*ve, live, notes[(size_t) th.front()].seconds, &adrift) - offsetSeconds;
            for (int i : th)
            {
                const auto& f = notes[(size_t) i];
                PastNote p;
                p.id = f.key; p.who = f.who.trim(); p.bar = f.bar; p.text = f.text; p.replyTo = f.replyTo;
                p.seconds = at; p.argb = feedback::personArgb (f); p.editor = f.editor; p.done = f.done; p.adrift = adrift;
                const int lane = f.editor && f.madeIn > 0 ? juce::jlimit (n, now, f.madeIn) : n;
                byVersion[lane].notes.push_back (std::move (p));
            }
        }
    }
    std::vector<PastLane> lanes;
    for (auto& kv : byVersion)
    {
        auto& lane = kv.second;
        lane.version = kv.first; lane.current = kv.first == now;
        juce::String sub = kv.first == now ? live.subheading.trim() : juce::String();
        if (const auto* v = live.version (kv.first)) sub = v->subheading.trim();
        lane.label = "Edit " + juce::String (kv.first) + (sub.isNotEmpty() ? " - " + sub : juce::String());
        std::stable_sort (lane.notes.begin(), lane.notes.end(), [] (const PastNote& a, const PastNote& b) { return a.seconds < b.seconds; });
        lanes.push_back (std::move (lane));
    }
    return lanes;
}
} // namespace reviewpage
} // namespace td
