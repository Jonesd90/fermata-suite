#pragma once
/** 3.61.0 (asked for on 23 November 2026): the end of every export that writes files - Bounce Out, the Virtual Master (and its stems), the DDP,
    the listening copies, the Atmos files, Export AAF and Export for video edit. Some of them said "complete" in a box, some only on their status
    line. Now each one, once its bar has reached 100 %, shows the same box: what was exported, what each file is called, and where it is (Show the
    files / OK). When the box is closed the bar empties and reads "Export complete" until the next export starts.

    The renders the Mastering window makes by itself (for its player and the DDP) are not exports: they say nothing. fermata only. */
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "WindowPlace.h"

namespace td::exportdone
{
/** The box's text: the line saying what was exported, each file's name (the first 15, then how many more), the folder (or each folder, when the
    files are in more than one - stems in their own Stems folder), then any notes. */
inline juce::String describe (const juce::String& what, const juce::StringArray& files, const juce::String& notes = {})
{
    juce::String text = what;
    if (! files.isEmpty())
    {
        text << "\n\nCalled:";
        const int shown = juce::jmin (15, files.size());
        for (int i = 0; i < shown; ++i) text << "\n   " << juce::File (files[i]).getFileName();
        if (files.size() > shown) text << "\n   ... and " << (files.size() - shown) << " more";
        juce::StringArray folders;
        for (auto& f : files) folders.addIfNotAlreadyThere (juce::File (f).getParentDirectory().getFullPathName());
        if (folders.size() == 1) text << "\n\nWhere:\n   " << folders[0];
        else
        {
            text << "\n\nWhere (" << folders.size() << " folders):";
            for (int i = 0; i < juce::jmin (6, folders.size()); ++i) text << "\n   " << folders[i];
            if (folders.size() > 6) text << "\n   ... and " << (folders.size() - 6) << " more";
        }
    }
    if (notes.trim().isNotEmpty()) text << "\n\n" << notes.trim();
    return text;
}

/** The box (title such as "Bounce complete"); Show the files opens the folder of the first file. 'closed' runs when it is closed, whichever way
    (that is when the bar is emptied - barDone). */
inline void show (juce::Component* parent, const juce::String& title, const juce::String& what, const juce::StringArray& files, const juce::String& notes,
                  bool warn, std::function<void()> closed)
{
    auto* aw = new juce::AlertWindow (title, describe (what, files, notes), warn ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon, place::anchor (parent));
    const juce::File first = files.isEmpty() ? juce::File() : juce::File (files[0]);
    if (first.exists()) aw->addButton ("Show the files", 1);
    aw->addButton ("OK", 0, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, first, closed] (int res)
    {
        if (res == 1 && first.exists()) first.revealToUser();
        delete aw;
        if (closed) closed();
    }));
}

/** After the box: the bar empties and says "Export complete" (until barStart). */
inline void barDone (juce::ProgressBar& bar, double& value)
{
    value = 0.0;
    bar.setTextToDisplay ("Export complete");
    bar.setVisible (true);
    bar.repaint();
}
/** A new export starts: the bar shows its percentage again. */
inline void barStart (juce::ProgressBar& bar) { bar.setTextToDisplay ({}); }
} // namespace td::exportdone
