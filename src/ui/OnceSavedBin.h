#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace td
{
/** 3.46.1 (sweep 4, area C): files that may only be deleted once the project has been SAVED WITHOUT THEM.

    Noise mode puts every fix into the edit the moment it is made, so an automatic save may write the project file with the fix's new files in it. Its Undo
    (and "Throw them away" when it closes) then deleted those files at once: until the next save - 30 seconds to 5 minutes later, or never after "Quit
    without saving" - the project file on the disk named audio that was gone, and a crash or a quit in that time left the edit with holes in it.

    The files are put here instead. Every save takes a number before its snapshot is made (snapshot()); when that save is on the disk (saved()), the files
    put here BEFORE its snapshot was taken may go: that save is without them. One put here after the snapshot waits for the next save. A save that fails
    hands nothing back. Another project, or the program closing without a last save: what is left here stays on the disk, where the project file that
    names it can still find it. (Message thread only.) */
class OnceSavedBin
{
public:
    /** A save's snapshot is about to be taken: its number. */
    int snapshot() noexcept { return ++taken; }
    /** These files are out of the project from now on. */
    void add (const std::vector<juce::File>& files) { for (auto& f : files) items.push_back ({ f, taken }); }
    /** The save made from snapshot 'number' is on the disk: the files that may be deleted now (they are forgotten here). */
    std::vector<juce::File> saved (int number)
    {
        std::vector<juce::File> due; std::vector<Item> keep;
        for (auto& i : items) { if (i.after < number) due.push_back (i.file); else keep.push_back (i); }
        items = std::move (keep);
        return due;
    }
    /** Another project is open: what is left is not this one's to delete. */
    void forget() { items.clear(); }
    int size() const noexcept { return (int) items.size(); }
private:
    struct Item { juce::File file; int after = 0; };      // after: how many snapshots had been taken when it was put here
    std::vector<Item> items;
    int taken = 0;
};
} // namespace td
