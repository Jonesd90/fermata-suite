#pragma once
#include "Uikit.h"

namespace td
{
/** 3.52.0: the picture a timeline holds while a process window (noise mode, ReTouch ...) is being worked in - see ProcessWindows (Uikit.h).

    From a log of 10 November 2026: while the noise window played, the Edit window and a take window behind it were redrawn again and again,
    150 to 470 ms each time, on the one thread that draws everything - about 290 times in the evening, and in one play of 58 seconds the
    screen was refreshed 1,681 times instead of some 3,500. The sound was never touched (it has its own cores); the noise window's moving
    picture stood still for each of those redraws.

    So, while a process window is in use:
      - AT REST the timeline draws as always, and then draws the same again into 'image' (the visible part, at the screen's own pixels), so
        that it has a whole picture ready the moment a play starts;
      - WHILE IT PLAYS, and for a second and a half after it stops (Space is pressed again within that time more often than not), every
        redraw that is asked for - whatever asks: the playhead, the meters, a waveform that has been read further, the project saying that
        something changed - only copies that picture to the screen, which costs next to nothing. 'stale' remembers that something wanted
        drawing; the timeline's own timer redraws it once when the hold is over.
    The held picture has no moving playhead in it: the process window has its own. When the view no longer fits the picture (the window was
    resized or scrolled meanwhile) it is drawn afresh, once.

    On a screen scaled by a whole number the copy is exactly what was there. At 125 / 150 / 175 % it can be very slightly soft, for as long
    as the play lasts, in a window that is not the one being worked in. */
struct HeldPicture
{
    juce::Image image; juce::Rectangle<int> area; float scale = 0.0f;
    bool stale = false;
    int skipped = 0;                                                       // redraws not made since the count was last taken (for the log)
    juce::uint32 lastPlay = 0; bool played = false;

    /** A development aid: FERMATA_TEST_HOLD=1 holds the picture whenever a process window is in use, playing or not, and the Edit window asks
        for a redraw at every tick while it is held - to look at the held picture, and time it (FERMATA_PAINT_PROFILE=1), on a machine with no
        audio device, where nothing can play. */
    static bool testing() { static const bool on = juce::SystemStats::getEnvironmentVariable ("FERMATA_TEST_HOLD", "0") == "1"; return on; }
    void notePlaying (bool playing) { if (playing) { lastPlay = juce::Time::getMillisecondCounter(); played = true; } }
    /** The picture is being held now: nothing is to be drawn afresh. */
    bool frozen (bool playing)
    {
        if (! ProcessWindows::get().inUse()) return false;
        return playing || testing() || (played && juce::Time::getMillisecondCounter() - lastPlay < 1500);
    }
    /** A process window is in use and there is no whole picture yet: the timeline should draw itself once (at rest) to make it. */
    bool wanted (juce::Rectangle<int> visible, bool playing)
    {
        return ! frozen (playing) && ProcessWindows::get().inUse() && ! visible.isEmpty() && ! (image.isValid() && area == visible);
    }
    /** First thing in paint(): true when the held picture was copied to the screen and nothing else is to be drawn. */
    bool show (juce::Graphics& g, juce::Rectangle<int> visible, bool playing)
    {
        if (! frozen (playing) || ! image.isValid() || area != visible) return false;
        if (std::abs (g.getInternalContext().getPhysicalPixelScaleFactor() - scale) > 0.001f) return false;
        stale = true; ++skipped;
        g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale).translated ((float) area.getX(), (float) area.getY()));
        return true;
    }
    /** Last thing in paint(), after the timeline has drawn itself to the screen: the same into the held picture ('paintNow' draws the timeline
        into the Graphics it is given, in the timeline's own coordinates). Nothing is done - and no picture is kept - while no process window is
        in use. */
    template <typename PaintFn>
    void keep (juce::Graphics& g, juce::Rectangle<int> visible, PaintFn&& paintNow)
    {
        if (! ProcessWindows::get().inUse() || visible.isEmpty()) { image = juce::Image(); stale = false; return; }
        const float sc = juce::jlimit (0.5f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const bool fits = image.isValid() && area == visible && std::abs (sc - scale) < 0.001f;
        auto clip = fits ? g.getClipBounds().getIntersection (visible) : visible;       // (a new picture is drawn whole, whatever part the screen asked for)
        if (! fits)
        {
            image = juce::Image (juce::Image::RGB, juce::jmax (1, (int) std::ceil ((float) visible.getWidth() * sc)), juce::jmax (1, (int) std::ceil ((float) visible.getHeight() * sc)), true);
            area = visible; scale = sc;
        }
        if (! clip.isEmpty())
        {
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale (sc));
            ig.setOrigin (-visible.getX(), -visible.getY());
            ig.reduceClipRegion (clip);
            paintNow (ig);
        }
        stale = false;
    }
};
} // namespace td
