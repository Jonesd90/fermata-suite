#pragma once
#include "Uikit.h"

namespace td::headbuttons
{
/** 3.73.0 (asked for on 27 November 2026, with a picture of the edit window on the smaller screen, its buttons on three lines: "I want them to
    be on two please" - and, of the three ways offered, "Do all three"): the take and edit windows' buttons, beside the time readout, go on
    two lines whenever squeezing can do it. Tried in turn, the first that needs no more than two lines is used:
      0  as they are;
      1  narrower: the picture buttons 28 wide (34), the other buttons 6 narrower (12 for the widest); the gaps inside a group 4 (6, 8) and
         10 between groups (14) - and the name box narrower (the window's own resized() does that part);
      2  as 1, and a group may be split between the two lines (and the name box a little narrower again).
    A window wide enough for 0 looks exactly as before. */
inline std::vector<grid::FlowItem> compact (std::vector<grid::FlowItem> items)
{
    for (auto& it : items)
    {
        it.w = it.w == 34 ? 28 : it.w <= 52 ? it.w - 6 : it.w - 12;
        it.gapAfter = it.gapAfter >= 10 ? 10 : it.gapAfter >= 6 ? 4 : it.gapAfter;      // (10 or more still ends a group: grid::flow's keepGroups)
    }
    return items;
}

struct Choice { int level = 0; std::vector<grid::FlowItem> items; bool keepGroups = true; };

/** The first way (0, 1, 2 above) that puts 'items' on at most two lines - or, when none does, the one with the fewest lines (the first of
    those: a window whose buttons need three lines whatever is done keeps them as they are). 'widthAt' gives the buttons' room for a way
    (ways 1 and 2 have a narrower name box beside them, so more). */
inline Choice choose (const std::vector<grid::FlowItem>& items, const std::function<int (int level)>& widthAt)
{
    Choice best; int bestLines = 1 << 20;
    for (int level = 0; level <= 2; ++level)
    {
        Choice c;
        c.level = level;
        c.items = level == 0 ? items : compact (items);
        c.keepGroups = level < 2;
        int lines = 0;
        grid::flow (juce::Rectangle<int> (0, 0, juce::jmax (1, widthAt (level)), 0), c.items, false, &lines, c.keepGroups);
        if (lines <= 2) return c;
        if (lines < bestLines) { best = std::move (c); bestLines = lines; }
    }
    return best;
}
} // namespace td::headbuttons
