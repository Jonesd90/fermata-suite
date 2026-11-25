#pragma once
/** 3.62.0 (asked for on 23 November 2026): "put, in very small writing, the actual file names of each audio region onto the region, perhaps
    starting from the top left corner. Just so I can identify which physical file each audio clip refers to".

    The name of the file a clip plays, in small writing on a dark strip at the clip's top-left corner (as the DUD / Bar words sit along a take's
    bottom). When the clip starts left of the window, the name stays at the window's left edge, so it can still be read; it is cut short (with
    "...") on a narrow clip and left out where the clip is too small to hold it. Used by the edit window (pieces and overdubs) and the take
    window (each take's files). fermata only. */
#include <juce_gui_basics/juce_gui_basics.h>

namespace td::clipname
{
inline juce::Font font() { return juce::Font (juce::FontOptions (9.5f)); }      // (made each time: a static Font would outlive JUCE's typefaces at exit)

/** 'clip' is the clip on one track's row; 'visibleLeft' the left edge of the part of the timeline on screen (in the same coordinates). */
inline void draw (juce::Graphics& g, const juce::Rectangle<int>& clip, const juce::File& file, int visibleLeft)
{
    if (file == juce::File() || clip.getHeight() < 22) return;
    const int x = juce::jmax (clip.getX() + 2, visibleLeft + 2);
    const int room = clip.getRight() - 2 - x;
    if (room < 26 || ! g.getClipBounds().intersects (juce::Rectangle<int> (x, clip.getY() + 2, room, 12))) return;     // (a narrow repaint - the playhead's - measures no names it does not touch)
    const auto name = file.getFileName();
    const auto f = font();
    const int w = juce::jmin (room, juce::GlyphArrangement::getStringWidthInt (f, name) + 7);
    const juce::Rectangle<int> box (x, clip.getY() + 2, w, 12);
    g.setColour (juce::Colour (0xb0000000)); g.fillRect (box);
    g.setColour (juce::Colour (0xffe9eef3)); g.setFont (f);
    g.drawText (name, box.withTrimmedLeft (3).withTrimmedRight (2), juce::Justification::centredLeft, true);
}
} // namespace td::clipname
