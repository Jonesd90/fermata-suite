#pragma once
/** fermata:Live 0.3.0: the timeline of a station's take window - fermata's take window, reduced: the marker strip, the flags (with their notes and
    the Fixer's replies), the ruler, the takes' headers and a lane per track with its waveform, the playhead, the record line of a take that is
    recording (Primary) and the edge of what is on the disk (Fixer). Each track's header (left) has M and S (the station's own mixer) and, on the
    Primary, ARM.

    The waveform of a session's take comes from the station's PeakStore (live); a take recorded before is drawn from its files (fermata's
    WaveThumb, read in the background). The window that owns the view sets its public fields and repaints it; the view calls back for what the
    user does. Message thread only. */
#include "TakeLayout.h"
#include "../ui/Uikit.h"
#include "../ui/WaveDraw.h"
#include "../ui/MarkerStrip.h"

namespace td::live
{
class LiveTakeView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LiveTakeView();
    ~LiveTakeView() override;

    // ---- what to show (the window sets these, then repaints)
    std::shared_ptr<const TakeLayout> layout;
    const StationModel* model = nullptr;
    const Project* project = nullptr;
    /** The window's markers (moved / renamed here on the Primary), asked for each time they are used - never kept: opening or making a project
        replaces the take windows, and a kept pointer would then point at freed memory. Null / returning null: none. */
    std::function<std::vector<Marker>*()> markerList;
    std::vector<Marker>* markersNow() const              { return markerList ? markerList() : nullptr; }
    std::vector<Marker> projectFlags;                    // the flags the project already has (markers with flag set): drawn with the session's
    double playhead = 0.0;                               // seconds on the window's timeline
    double liveEnd = -1.0;                               // the end of the take recording now (< 0: none)
    double writtenEnd = -1.0;                            // the Fixer: the audio on the disk ends here (< 0: everything is)
    double mark1 = -1.0, mark2 = -1.0;                   // the Primary's marks 1 and 2 (Bounce out), on the window's timeline (< 0: not set)
    bool showArm = true, editableMarkers = true;
    std::function<bool (const juce::Uuid&)> isArmed;
    std::function<void (const juce::Uuid&)> toggleArm;
    std::function<bool (const juce::Uuid&, bool solo)> isMuteSolo;
    std::function<void (const juce::Uuid&, bool solo)> toggleMuteSolo;
    std::function<juce::Colour (const juce::Uuid&)> colourOf;
    juce::String selectedFlag;                           // (a FlagRec id: drawn lit)

    // ---- what the user did
    std::function<bool()> canSeek;                       // false: a click does not move the playhead (the Fixer following the Primary)
    std::function<void (double seconds)> onSeek;
    std::function<void (double a, double b, const std::vector<juce::Uuid>& tracks)> onSelect;   // a drag over the lanes (a == b: cleared)
    std::function<void (const juce::String& flagId, bool doubleClick)> onFlagClick;
    std::function<void()> onMarkersChanged;

    // ---- the view
    double viewStart = 0.0, pxPerSec = 12.0;
    int laneH = 44;
    float vzoom = 1.0f;
    double selA = -1.0, selB = -1.0;
    std::vector<juce::Uuid> selTracks;
    /** Keeps 'seconds' in view: paged when it reaches the right edge (followMode), or centred when it is off the screen. */
    void keepInView (double seconds, bool page);
    /** What the windows' timers call: repaints only if something drawn has changed since the last time (a playhead that moved a pixel, new
        waveform, a flag, the view, a track's M / S / ARM...), and twice a second whatever happens (for the waveforms read in the background).
        A window that has just changed something itself calls repaint(). */
    void repaintIfChanged();
    void zoomBy (double factor, double aroundSeconds);
    void zoomToFit();
    double secondsAt (int x) const       { return viewStart + (double) (x - kHeaderW) / pxPerSec; }
    int xOf (double seconds) const       { return kHeaderW + (int) std::lround ((seconds - viewStart) * pxPerSec); }
    int lanesTop() const                 { return kMarkerH + kFlagH + kRulerH + kTakeHeadH; }

    static constexpr int kHeaderW = 170, kMarkerH = markerui::kStripH, kFlagH = 36, kRulerH = 20, kTakeHeadH = 18;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    struct FlagShown { juce::String id; double seconds = 0.0; juce::String note; std::vector<ReplyRec> replies; bool removed = false; juce::Time pinnedAt; };
    std::vector<FlagShown> flagsShown() const;
    void paintRuler (juce::Graphics&, juce::Rectangle<int>);
    void paintFlags (juce::Graphics&, juce::Rectangle<int>, const std::vector<FlagShown>&);
    void paintLanes (juce::Graphics&, juce::Rectangle<int>);
    void paintTrackHeaders (juce::Graphics&, juce::Rectangle<int>);
    void paintWave (juce::Graphics&, const ShownTake&, const LayoutTrack&, juce::Rectangle<int> lane, int x0, int x1, juce::Colour);
    int trackRowAt (int y) const;
    juce::Rectangle<int> headerButton (int row, int which) const;     // which: 0 M, 1 S, 2 ARM
    WaveThumb* thumbFor (const juce::File&);

    juce::AudioFormatManager formats;
    WaveThumb::Pool pool;
    std::map<juce::String, std::unique_ptr<WaveThumb>> thumbs;
    std::unique_ptr<markerui::Controller> markerMouse;
    int scrollY = 0;
    bool selecting = false; double dragFrom = 0.0; int dragRow0 = 0;
    juce::String hoverText;
    juce::uint64 lastSignature = 0;
    juce::uint32 lastPaintMs = 0;
};
} // namespace td::live
