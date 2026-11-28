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

    The spectrogram itself, the markers on the highest true peaks, the gain boxes and the mastering plug-in come with the next build. */
class SpecEditComponent : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    SpecEditComponent (AppContext&, const juce::Uuid& editId);
    ~SpecEditComponent() override;
    static juce::String windowKey() { return "specedit"; }
    /** Another piece (the Mastering window's button, while this window is open). */
    void setPiece (const juce::Uuid& editId);

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
