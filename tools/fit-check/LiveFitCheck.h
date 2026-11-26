#pragma once
/** The small-screen check's scenes for fermata:Live (0.10.18; FIT_PROGRAM=live tools/fit-check.sh): the Primary's main window, then each window its menu bar opens
    (with the bar's own keys), on the project it last had open. Built into fermata:Live only for the check (FERMATA_FIT_CHECK=1), never into the
    program the user runs. Checker.h does the looking. */
#include "Checker.h"

namespace td::fitcheck
{
class LiveDriver : public Checker
{
public:
    using Checker::Checker;
    void start()
    {
        add ("main", [] {}, {}, true);
        for (auto* key : { "Audio settings", "Preamps", "Project Designer", "Take Window", "Mixer", "Bounce out" })
        {
            const juce::String text (key);
            add (text.toLowerCase().replaceCharacter (' ', '-'), [this, text] { if (auto* b = findButton (main, text)) b->triggerClick(); });
        }
        begin (6000);
    }
};
} // namespace td::fitcheck
