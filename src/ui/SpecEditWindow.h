#pragma once
#include "AppContext.h"
#include "MasterBusy.h"
#include "../core/WaveStats.h"
#include "../core/MasterRender.h"

namespace td
{
/** 3.74.0 (asked for on 27 November 2026; written with Claude Fable): the SPECTROGRAM EDITING window, opened from Mastering for one piece.
    This first part of it is the measuring: the piece's render (the stereo file the Mastering window plays and exports - MasterRender.h) is read
    once into a wavestats::Profile (src/core/WaveStats.h), and the window shows

      - the piece's waveform, with its loudness along it (momentary and short-term, and the integrated loudness as a line). A click puts the
        playhead there; a drag selects a part;
      - the figures of iZotope RX's Waveform Statistics for the whole piece, and beside them for the selected part (exact to the sample for the
        peaks, the clipped samples, the DC and the total RMS; they follow the mouse while it drags);
      - meters that follow the playhead while the piece plays (and show what is under it when it stands still): true peak and sample peak,
        peak with average on one scale, momentary / short-term / integrated loudness and the loudness range, the correlation of left and right
        with a vectorscope, and a third-octave spectrum.

    The render is the piece through its mixer, before the export's own level and dither. When the piece changes (an edit, a fader, a plug-in)
    its render is made again and measured again by itself ("Keep up to date"), or when Measure again is pressed. Only one render is made at a
    time in the whole program (MasterBusy.h).

    3.75.0, the second part - the mending:

      - the render's SPECTROGRAM (the picture and the tools of Noise mode: SpectrogramView, its Rectangle / Time / Brush tools and feathering),
        with the highest true peaks marked along it and listed ("Peaks and gain": how many is chosen; peaks nearer than half a second to a
        higher one belong to it - wavestats::topPeaks). For each, the band that carries most of its height is worked out (bandAtPeak: the
        sample at the peak is the sum of what every third of an octave adds to it there); "Box it" draws a box round that band;
      - the GAIN BOX: ReTouch's Gain (FixSpec Patch + RepairMode::Gain, through prepareRepairJobs / applyRegionJobs, exactly as Noise mode's
        Gain) on the tracks that are ticked, so the cut is made in the multitracks and the piece stays editable up to the export. It is put
        into the edit as new piece files - the recordings are never touched - and is one Undo step there and here. The render's first sample
        is the edit's first sound (EditDef::audibleStart), which is how the box's times become the edit's. A list shows each track's own level
        in the box (or at the picked peak), loudest first;
      - the PLUG-IN on chosen tracks: one instance on each ticked track, in the track's post-fader slot (Common.h: kPostSlot - after the fader,
        just before the mix; the project's post-fader slots are switched on for it). "Link the settings" moves a control moved on one instance
        on the others. The engine's delay compensation counts the slot, so the tracks stay lined up, in the render as in playing.

    What plays here is always the render: a gain box or a plug-in is heard, seen and measured once the piece has been rendered again (by
    itself with "Keep up to date"). */
class SpecEditComponent : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    SpecEditComponent (AppContext&, const juce::Uuid& editId);
    ~SpecEditComponent() override;
    static juce::String windowKey() { return "specedit"; }
    /** Another piece (the Mastering window's button, while this window is open). */
    void setPiece (const juce::Uuid& editId);
    /** For tools/fit-check only (never used by the program): "pick" picks the highest peak. */
    void checkStep (const juce::String& what);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Impl;
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    AppContext& app;
    std::unique_ptr<Impl> impl;
};
} // namespace td
