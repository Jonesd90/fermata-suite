#pragma once
/** fermata:Live 0.3.0: the timeline of a station's take window - fermata's take window, reduced: the flags (with their notes and the Fixer's replies),
    the ruler, the takes' headers and a lane per track with its waveform, the playhead, the record line of a take that is recording (Primary) and
    the edge of what is on the disk (Fixer). Each track's header (left) has M and S (the station's own mixer) and, on the Primary, ARM.

    0.5.0: the NOTES row at the top (the station's own notes, and the project's ordinary markers - the gaps - read only), the time of day (the clock
    along the ruler, with the date at each midnight), the NOW line, the FLAG POINT (a click on the flag band where there is no flag: where Enter / N
    pin when the moment has passed), fermata's zoom (round a point that is then put in the middle), the scroll bars, a take chosen by its header
    (P), the Fixer's rule for a drag (every recorded track of the take; Alt + drag only the tracks dragged over) and the coloured waveforms.

    The waveform of a session's take comes from the station's PeakStore (live); a take recorded before is drawn from its files (fermata's
    WaveThumb, read in the background). The window that owns the view sets its public fields and repaints it; the view calls back for what the
    user does. Message thread only. */
#include "TakeLayout.h"
#include "LiveColours.h"
#include "../ui/Uikit.h"
#include "../ui/WaveDraw.h"

namespace td::live
{
class LiveTakeView final : public juce::Component, public juce::SettableTooltipClient, private juce::ScrollBar::Listener
{
public:
    LiveTakeView();
    ~LiveTakeView() override;

    /** A note (or an ordinary marker) as the view draws it: on the timeline shown. */
    struct NoteShown { juce::String id; double seconds = 0.0; juce::String text, by; juce::Time at; bool plain = false; bool editable = true; };

    // ---- what to show (the window sets these, then repaints)
    std::shared_ptr<const TakeLayout> layout;
    const StationModel* model = nullptr;
    const Project* project = nullptr;
    std::vector<Marker> projectFlags;                    // the flags the project already has (markers with flag set), on fermata's timeline
    std::vector<NoteShown> notes;                        // 0.5.0: the station's notes and the project's ordinary markers, on the timeline shown
    double playhead = 0.0;                               // seconds on the timeline shown (< 0: not drawn - the Primary while a take records)
    double liveEnd = -1.0;                               // the end of the take recording now (< 0: none)
    double writtenEnd = -1.0;                            // the Fixer: the audio on the disk ends here (< 0: everything is)
    double nowLine = -1.0;                               // 0.5.0, the time of day: now (drawn while no take records; < 0: none)
    double flagPoint = -1.0;                             // 0.5.0: the flag point chosen in the flag band (< 0: none)
    double mark1 = -1.0, mark2 = -1.0;                   // the Primary's marks 1 and 2 (Bounce out), on the timeline shown (< 0: not set)
    bool showArm = true;
    bool allTracksByDefault = false;                     // 0.5.0, the Fixer: a drag chooses every track recorded in the take; Alt + drag only those dragged over
    int selectedTake = 0;                                // 0.5.0: the take chosen by a click on its header (0: none) - lit
    juce::String selectedNote;                           // 0.5.0: the note open in the window's note panel - lit
    bool colourWaves = false;                            // 0.5.0: fermata's WaveColour (colour by what is in the sound)
    WaveColorCache* fileColours = nullptr;               // the colours of the takes' files (when colourWaves)
    const LiveColours* sessionColours = nullptr;         // the colours of the session's takes, from its chunks (when colourWaves)
    std::function<bool (const juce::Uuid&)> isArmed;
    std::function<void (const juce::Uuid&)> toggleArm;
    std::function<bool (const juce::Uuid&, bool solo)> isMuteSolo;
    std::function<void (const juce::Uuid&, bool solo)> toggleMuteSolo;
    std::function<juce::Colour (const juce::Uuid&)> colourOf;
    juce::String selectedFlag;                           // (a FlagRec id: drawn lit)

    // ---- what the user did
    std::function<bool()> canSeek;                       // false: a click does not move the playhead (the Fixer following; the Primary while a take records)
    std::function<void (double seconds)> onSeek;
    std::function<void (double seconds)> onLaneClick;    // 0.5.0: any click on the lanes (before onSeek): the flag point goes back to NOW
    std::function<void (double a, double b, const std::vector<juce::Uuid>& tracks)> onSelect;   // a drag over the lanes (a == b: cleared)
    std::function<void (const juce::String& flagId, bool doubleClick)> onFlagClick;
    std::function<void (double seconds)> onFlagBandClick;  // 0.5.0: a click on the flag band where there is no flag: the flag point
    std::function<void (int takeNumber)> onTakeClick;      // 0.5.0: a click on a take's header (0: on none)
    std::function<void (const juce::String& noteId)> onNoteClick;   // 0.5.0: a click on a note in the notes row
    std::function<void()> onUserScrolled;                  // 0.5.0: the user moved the view (a scroll bar, the wheel)

    // ---- the view
    double viewStart = 0.0, pxPerSec = 12.0;
    int laneH = 44;
    float vzoom = 1.0f;
    double selA = -1.0, selB = -1.0;
    std::vector<juce::Uuid> selTracks;
    /** Keeps 'seconds' in view: paged when it reaches the right edge (followMode), or centred when it is off the screen. */
    void keepInView (double seconds, bool page);
    /** Puts 'seconds' in the middle of the view (the zoom stays: fermata's C). */
    void centreOn (double seconds);
    /** What the windows' timers call: repaints only if something drawn has changed since the last time (a playhead that moved a pixel, new
        waveform, a flag, the view, a track's M / S / ARM...), and twice a second whatever happens (for the waveforms read in the background).
        A window that has just changed something itself calls repaint(). */
    void repaintIfChanged();
    /** Zooms round a point, as the mouse wheel does: the point stays where it is on the screen. */
    void zoomBy (double factor, double aroundSeconds);
    /** 0.5.0: fermata's zoom (the arrow keys, + and -): round 'anchor', which is then put in the middle of the view (as SADiE does); an anchor off
        the screen zooms round the middle of what is shown. Near the start it cannot go further left than the start. */
    void zoomCentred (double factor, double anchor);
    void zoomToFit();
    /** All the tracks smaller (< 0) or bigger (> 0) (fermata's Up / Down). */
    void changeLaneHeight (int direction);
    double secondsAt (int x) const       { return viewStart + (double) (x - kHeaderW) / pxPerSec; }
    int xOf (double seconds) const       { return kHeaderW + (int) std::lround ((seconds - viewStart) * pxPerSec); }
    int lanesTop() const                 { return kNoteH + kFlagH + kRulerH + kTakeHeadH; }
    /** The tracks of 'take' that have audio in it (a session take: the tracks the session records; a project take: those with a file). */
    std::vector<juce::Uuid> tracksIn (const ShownTake& take) const;
    /** 0.5.0: the span the scroll bar covers: the takes, and now / the live end / the playhead, with a little room either side. */
    juce::Range<double> contentRange() const;

    static constexpr int kHeaderW = 170, kNoteH = 22, kFlagH = 36, kRulerH = 20, kTakeHeadH = 18, kBarH = 14;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    struct FlagShown { juce::String id; double seconds = 0.0; juce::String note, by; std::vector<ReplyRec> replies; bool removed = false; juce::Time pinnedAt; };
    std::vector<FlagShown> flagsShown() const;
    void paintRuler (juce::Graphics&, juce::Rectangle<int>);
    void paintFlags (juce::Graphics&, juce::Rectangle<int>, const std::vector<FlagShown>&);
    void paintNotes (juce::Graphics&, juce::Rectangle<int>);
    void paintLanes (juce::Graphics&, juce::Rectangle<int>);
    void paintTrackHeaders (juce::Graphics&, juce::Rectangle<int>);
    void paintWave (juce::Graphics&, const ShownTake&, const LayoutTrack&, juce::Rectangle<int> lane, int x0, int x1, juce::Colour);
    int trackRowAt (int y) const;
    int lanesBottom() const             { return getHeight() - kBarH; }
    juce::Rectangle<int> headerButton (int row, int which) const;     // which: 0 M, 1 S, 2 ARM
    const NoteShown* noteAt (int x) const;
    WaveThumb* thumbFor (const juce::File&);
    void updateBars();
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    double visibleSeconds() const        { return (double) juce::jmax (100, getWidth() - kHeaderW - kBarH) / pxPerSec; }

    juce::AudioFormatManager formats;
    WaveThumb::Pool pool;
    std::map<juce::String, std::unique_ptr<WaveThumb>> thumbs;
    juce::ScrollBar hbar { false }, vbar { true };
    bool updatingBars = false;
    int scrollY = 0;
    bool selecting = false, dragAlt = false; double dragFrom = 0.0; int dragRow0 = 0;
    juce::String hoverText;
    juce::uint64 lastSignature = 0;
    juce::uint32 lastPaintMs = 0;
};
} // namespace td::live
