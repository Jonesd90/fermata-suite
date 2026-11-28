#pragma once
// 3.35.0: fermata's ReTouch window - the spectral picture, its selection tools (Rectangle, Time, Brush, feathering), harmonics, and the window round it
// (ReTouch, Gain, De-click, Denoise, Consonants) - shared with fermata:Live's Fixer (COMPATIBILITY.md). Moved here unchanged from FixToolsDialogs.h and
// FixToolsCommon.h. The window knows nothing of either program: it is given a RepairContext (how to read the audio, make the fixes, play, keep them),
// which each program fills in its own way.
#include "Uikit.h"
#include "TimeReadout.h"
#include "BusyLabel.h"
#include "../core/AudioOps.h"
#include "../core/SpectralRepair.h"
#include "../core/SpectralPicture.h"
#include "../core/BoxEdges.h"
#include "../core/Workers.h"
#include "../core/Consonants.h"
#include "../core/ConsonantData.h"
#include "../core/Tuning.h"
#include "../core/Denoise.h"
#include "../core/AudioFormats.h"
#include <array>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <limits>
#include <functional>
#include "KeyMap.h"

namespace td { namespace fixtools
{
/** The program's own little settings file for the things the offline windows remember (their size and place, the way they look). (Moved here with the
    ReTouch window in 3.35.0: "Fermata/ui-windows.settings", the same file for both programs.) */
inline juce::PropertiesFile& uiPrefs()
{
    static juce::PropertiesFile* f = [] ()
    {
        juce::PropertiesFile::Options o; o.applicationName = "ui-windows"; o.filenameSuffix = ".settings"; o.folderName = "Fermata"; o.osxLibrarySubFolder = "Application Support";
        o.storageFormat = juce::PropertiesFile::storeAsXML; o.millisecondsBeforeSaving = 0; o.ignoreCaseOfKeyNames = true;
        return new juce::PropertiesFile (o);                     // (kept for the whole run: it saves itself at every change)
    }();
    return *f;
}
/** 3.35.0: how the ReTouch window opens a small window of its own (its spectrogram settings): the program's way when it gives one (fermata: launchDialog),
    else a plain dialog window. */
inline std::function<void (std::unique_ptr<juce::Component>, const juce::String&, juce::Component*)>& openDialogHook()
{
    static std::function<void (std::unique_ptr<juce::Component>, const juce::String&, juce::Component*)> h; return h;
}
inline void openSmallDialog (std::unique_ptr<juce::Component> content, const juce::String& title, juce::Component* around)
{
    if (openDialogHook()) { openDialogHook() (std::move (content), title, around); return; }
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (content.release()); o.dialogTitle = title; o.componentToCentreAround = around; o.dialogBackgroundColour = theme::window;
    o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = false;
    td::fit::launchAsync (o);
}

/** 3.35.0: how a window that closes itself asks its DialogWindow to go. A program with windows of its own kind sets the hook (fermata: an OUT / IN STREAM
    window closes its pair); true = it was dealt with. */
inline std::function<bool (juce::DialogWindow*)>& closeDialogHook() { static std::function<bool (juce::DialogWindow*)> h; return h; }
inline void closeDialogOf (juce::Component* c)
{
    if (auto* dw = c->findParentComponentOfClass<juce::DialogWindow>())
    {
        if (closeDialogHook() && closeDialogHook() (dw)) return;
        dw->exitModalState (0);
    }
}


inline juce::AudioFormatManager& formats()
{
    // (3.30.1: registered inside the static's own initialisation, which the language runs once under a lock: it is now called from several worker threads at once)
    static juce::AudioFormatManager& fm = [] () -> juce::AudioFormatManager& { static juce::AudioFormatManager m; td::registerAudioFormats (m); return m; }();
    return fm;
}

/** The common look of a dialog: a title line, a progress bar and the Apply / Cancel buttons are laid out by each dialog itself. */
class JobDialogBase : public juce::Component, protected juce::Timer
{
public:
    JobDialogBase() : bar (progressValue)
    {
        getProperties().set (ownsRKeyProperty(), true);     // R here runs the window's own job (audition / do the fix): it does not start a recording
        addChildComponent (bar); addAndMakeVisible (status);
        fit::markMayShrink (status);                     // (3.71.0: a status line - cut off on a narrow window, never a reason to scroll it: FitLayout.h)
        status.setColour (juce::Label::textColourId, theme::warn); status.setMinimumHorizontalScale (1.0f);
        ProcessWindows::get().add (this);                   // 3.52.0: while this window is worked in and plays, the windows behind it hold their picture (Uikit.h)
    }
    ~JobDialogBase() override { ProcessWindows::get().remove (this); stopTimer(); job.release(); }          // (asks the work to stop and never waits for it: see AudioJob::release)
protected:
    /** 3.58.0 (asked for on 22 November 2026: a denoise "LOOKS like the program hangs or freezes"): the bar always moves while a job runs. Real progress is
        shown as it comes; while it has not moved for a second - one long step: a long box being denoised, the picture being made again - the bar shows
        the moving "working" stripes and how long the job has been at it, so a long step is never taken for a hang. */
    void timerCallback() override
    {
        const double p = (double) job.progress();
        const auto now = juce::Time::getMillisecondCounter();
        if (p > lastReal + 1.0e-4) { lastReal = p; lastMove = now; }
        const bool waiting = now - lastMove > 1000 && p < 0.999;
        progressValue = waiting ? -1.0 : p;
        bar.setTextToDisplay (waiting ? "Working... " + juce::String ((int) ((now - jobStart) / 1000)) + " s" : juce::String());
    }
    /** 'ifGone' (optional) runs instead of 'done' when the window has been closed before the job ended: it tidies away what the job made (its files). */
    void runJob (AudioJob::Work w, std::function<void (const juce::String&)> done, std::function<void (const juce::String&)> ifGone = {})
    {
        jobStart = lastMove = juce::Time::getMillisecondCounter(); lastReal = 0.0; bar.setTextToDisplay ({});
        bar.setVisible (true); progressValue = 0.0; startTimerHz (10);
        juce::Component::SafePointer<JobDialogBase> self (this);
        job.start (std::move (w), [self, done, ifGone] (const juce::String& err)
        {
            if (self == nullptr) { if (ifGone) ifGone (err); return; }
            self->stopTimer(); self->bar.setVisible (false);
            self->running = false; self->jobEnded (err);
            if (done) done (err);
        });
        running = true;
    }
    virtual void jobEnded (const juce::String&) {}
    AudioJob& job = *new AudioJob();                                   // (handed back with release() in the destructor: it may outlive this window)
    bool running = false;
    double progressValue = 0.0; juce::ProgressBar bar; juce::Label status;
    juce::uint32 jobStart = 0, lastMove = 0; double lastReal = 0.0;   // (3.58.0: when the job began, and when its progress last moved)
};


// ======================================================================================================== the repair dialog
/** One change made in the repair window: what to do and where (samples from the start of the loaded area). */
struct RepairOp { FixSpec spec; juce::int64 t0 = 0, t1 = 0; juce::int64 placed = -1; };      // placed: a pasted consonant - where it was first put (its nudges are counted from there)

struct RepairContext
{
    juce::String title, what;
    double rate = 48000.0;
    double refA = 440.0;                                                                // 3.28.0: the project's reference A (the pitch pipe), for the picture's note read-out
    juce::int64 length = 0;                                                             // samples in the scanned area
    std::function<std::vector<std::vector<float>> (AudioJob&)> load;                     // job thread: one mono vector per track, 'length' samples
    std::function<juce::String (const std::vector<RepairOp>&, AudioJob&)> prepare;      // job thread: makes the repaired files with ALL these changes, covering the whole loaded area; nothing is changed yet
    std::function<bool()> apply;                                                        // message thread: puts the prepared files in place (so playing / the edit window use them)
    std::function<void()> unapply, discard, accept, stop;                               // message thread: back to the original / delete the prepared files / keep what is in place / stop playing
    std::function<void (double fromSec, double toSec, bool loop)> play;                 // message thread: plays from..to (seconds from the start of the area), optionally looped
    std::function<void (bool)> setLooping, hold;                                        // message thread
    std::function<double()> position;                                                   // message thread: seconds from the start of the area, or < 0 when this is not playing
    std::function<void (std::function<bool()>)> pairWritten;                            // one stream window of an OUT / IN pair: 'Write back' only makes this stream ready (the pair puts both into the edit together)
    std::vector<denoise::Profile> roomMono;                                             // the project's room tone for each picture track (the mono mix; an invalid profile = none for that track)
    juce::String roomSource;                                                            // where the project's room tone came from ("" = none learned)
    // ---- consonants (3.6.0): copied per track and channel, kept with the project, pasted on top ----
    std::vector<juce::Uuid> trackIds; std::vector<juce::String> trackNames;            // the picture's tracks, in its order (one row of 'load' each)
    std::function<std::vector<std::vector<std::vector<float>>> (juce::int64 a, juce::int64 b, AudioJob&)> readChannels;   // job thread: [a, b) of the area, per track ALL its channels, as the files are (before this window's fixes)
    juce::String where; double areaStart = 0.0;                                         // the take or edit, and the time of the area's start in it (for a kept consonant's "from")
    std::function<std::vector<ConsonantDef>()> consonantList;                           // message thread: the project's kept consonants
    std::function<juce::String (const ConsonantAudio&, const juce::String& name, const juce::String& from, juce::Uuid& id)> keepConsonant;   // message thread: keeps one (its files and the list); "" or why not
    std::function<void (const juce::Uuid&, const juce::String&)> renameConsonant;
    std::function<void (const juce::Uuid&)> forgetConsonant;
    std::function<std::shared_ptr<const ConsonantAudio> (const juce::Uuid&, juce::String& error)> loadConsonant;
    // ---- 3.35.0: a program that keeps the fixes its own way (fermata:Live's Fixer: a fix version - the recording and the project are never changed) ----
    juce::String acceptText, acceptTip;                                                 // the keep button's name and tooltip ("" = fermata's "Write back to file")
    std::function<juce::String()> mayAccept;                                            // message thread, before keeping: "" = go ahead, else why not (shown; the window stays open)
};


// ======================================================================================================== the repair dialog (RepairOp and RepairContext are in FixToolsCommon.h)

/** How the spectrogram is drawn (the same ideas as the Spectrogram Settings of iZotope RX). Remembered between sessions. */
struct SpectroSettings
{
    bool   sharp = true;            // the picture is made for what is in view, with reassignment (sharp lines); off = the quick, softer picture
    bool   reassign = true;
    int    palette = 0;             // 0 cyan to orange, 1 standard (heat), 2 grey, 3 ice, 4 rainbow
    double lo = -134.3, hi = -5.3;  // the levels (dB) at the two ends of the colours
    double detail = 0.5;            // 0 = finer pitch detail, 1 = finer time detail
    double reduceAbove = 240.0;     // seconds in view above which fewer analysis points are used
    void load (juce::PropertiesFile* f)
    {
        if (f == nullptr) return;
        sharp = f->getBoolValue ("specSharp", true); reassign = f->getBoolValue ("specReassign", true); palette = juce::jlimit (0, 4, f->getIntValue ("specPalette", 0));
        lo = f->getDoubleValue ("specLo", -134.3); hi = f->getDoubleValue ("specHi", -5.3); detail = juce::jlimit (0.0, 1.0, f->getDoubleValue ("specDetail", 0.5)); reduceAbove = f->getDoubleValue ("specReduce", 240.0);
        if (hi - lo < 6.0) { lo = -134.3; hi = -5.3; }
    }
    void save (juce::PropertiesFile* f) const
    {
        if (f == nullptr) return;
        f->setValue ("specSharp", sharp); f->setValue ("specReassign", reassign); f->setValue ("specPalette", palette); f->setValue ("specLo", lo); f->setValue ("specHi", hi);
        f->setValue ("specDetail", detail); f->setValue ("specReduce", reduceAbove);
    }
};
inline juce::PropertiesFile* gSpecPrefs = nullptr;                      // the program's settings file (set when a repair window is opened)

/** 3.33.0: a time in the spectral windows (Noise mode, ReTouch): seconds under a minute ("47.3 s"), then minutes and seconds ("3:47.3"), and hours from an hour
    on ("1:03:47.3"); 'decimals' digits after the seconds, rounded. */
inline juce::String specTime (double t, int decimals)
{
    decimals = juce::jlimit (0, 3, decimals);
    juce::int64 q = 1; for (int i = 0; i < decimals; ++i) q *= 10;
    const auto u = (juce::int64) std::llround (juce::jmax (0.0, t) * (double) q);
    if (u < 60 * q) return juce::String ((double) u / (double) q, decimals) + " s";
    const auto whole = u / q, frac = u % q;
    const int hh = (int) (whole / 3600), mm = (int) ((whole / 60) % 60), ss = (int) (whole % 60);
    juce::String s = (hh > 0 ? juce::String (hh) + ":" + juce::String (mm).paddedLeft ('0', 2) : juce::String (mm)) + ":" + juce::String (ss).paddedLeft ('0', 2);
    if (decimals > 0) s << "." << juce::String (frac).paddedLeft ('0', decimals);
    return s;
}

struct PictureRequest { juce::int64 id = 0; double t0 = 0.0, t1 = 1.0; int w = 0, h = 0; PictureAxis axis; PictureOptions opt;
                        int c0 = 0, c1 = -1; };      // 3.38.0: only these columns are wanted (the rest of the picture is up to date): picture::RenderPart

class SpectrogramView : public juce::Component
{
public:
    std::function<void()> onSelection;
    std::function<void (const PictureRequest&)> onNeedDetail;     // asks for the sharp picture of what is in view (made on another thread, then given back with setDetail)
    std::function<void (const juce::String&)> onNote;         // a line for the status line (colour roll, scale, view)

    void setData (MergedSpectrogram s, double seconds, bool keepSelection = false) { setData (std::move (s), seconds, keepSelection, 0.0, -1.0); }
    /** 3.38.0: the sound changed only between changedFrom and changedTo (seconds; a fix, or Undo): the sharp picture is kept, and made again only where the
        change reaches it. (changedTo < changedFrom: anywhere.) */
    void setData (MergedSpectrogram s, double seconds, bool keepSelection, double changedFrom, double changedTo)
    {
        const bool same = spec.frames > 0 && std::abs (seconds - duration) < 1.0e-9;
        spec = std::move (s); duration = juce::jmax (0.001, seconds); dT0 = 0.0; dT1 = duration;
        if (! keepSelection) { const bool hadH = harmBase >= 0; clearMarks(); rects.clear(); softParts.clear(); hasSel = false; if (hadH && onHarmonics) onHarmonics(); }
        if (! same) resetView();
        if (same && keepSelection && changedTo >= changedFrom) invalidateDetailRange (changedFrom, changedTo);
        else { detailStale = true; ++detailGen; scheduleDetail(); }
        dirty = true; repaint();
    }
    /** A quick picture that covers only part of the time (Noise mode: a slice of the whole edit's pyramid for what is in view): its first frame
        stands for 'dataT0' seconds and its last for 'dataT1'; 'seconds' is the whole length. The view, the selection and the sharp picture stay. */
    void setDataRange (MergedSpectrogram s, double dataT0, double dataT1, double seconds)
    {
        const bool fresh = spec.frames == 0;
        spec = std::move (s); duration = juce::jmax (0.001, seconds); dT0 = dataT0; dT1 = juce::jmax (dataT0 + 1.0e-6, dataT1);
        if (fresh) { resetView(); scheduleDetail(); }
        dirty = true; repaint();
    }
    std::function<void()> onView;                                  // the part of the time in view changed (zoom, pan, reset)
    /** The sound changed under the picture: the sharp picture is made again. */
    void invalidateDetail() { detailStale = true; ++detailGen; dirty = true; repaint(); scheduleDetail(); }
    /** 3.38.0: the sound changed only between a and b (seconds): the sharp picture is made again only as far as the change reaches into it (its analysis
        window either side - about a third of a second at most); the rest stays sharp, and the quick picture shows there until it is done. */
    void invalidateDetailRange (double a, double b)
    {
        if (! ss.sharp) return;
        if (! detailCoversView()) { invalidateDetail(); return; }
        const double r = picture::reach (spec.sampleRate, detail.t0, detail.t1, detail.w, detailOpt);
        const double pxPerSec = (double) detail.w / (detail.t1 - detail.t0);
        const juce::int64 A0 = picture::firstColumn (detail.t0, detail.t1, detail.w);
        const juce::int64 ca = (juce::int64) std::floor ((a - r) * pxPerSec) - A0 - 1, cb = (juce::int64) std::ceil ((b + r) * pxPerSec) - A0 + 2;
        const int c0 = (int) juce::jlimit ((juce::int64) 0, (juce::int64) detail.w, ca), c1 = (int) juce::jlimit ((juce::int64) 0, (juce::int64) detail.w, cb);
        if (c1 <= c0) return;                                                      // (the change is out of sight: nothing in view changes)
        ++detailGen;                                                               // (a picture being made now hears the sound as it was)
        markStale (c0, c1);
        dirty = true; repaint(); scheduleDetail();
    }
    /** The sharp picture asked for with onNeedDetail (ignored if the view has moved on, or the sound changed, since it was asked for). 3.38.0: a part
        (DetailPicture::whole() false) goes into the picture there is, in its columns. */
    void setDetail (DetailPicture p)
    {
        if (p.id != detailSeq || reqGen != detailGen || ! p.valid()) return;
        if (! p.whole())
        {
            if (! detail.valid() || detailStale || detail.w != p.w || detail.h != p.h || std::abs (detail.t0 - p.t0) > 1.0e-9 || std::abs (detail.t1 - p.t1) > 1.0e-9
                || ! detail.axis.same (p.axis)) return;
            const int c0 = juce::jlimit (0, p.w, p.c0), c1 = juce::jlimit (c0, p.w, p.c1);
            for (int r = 0; r < p.h; ++r)
                std::copy (p.db.begin() + (std::ptrdiff_t) ((size_t) r * (size_t) p.w + (size_t) c0), p.db.begin() + (std::ptrdiff_t) ((size_t) r * (size_t) p.w + (size_t) c1),
                           detail.db.begin() + (std::ptrdiff_t) ((size_t) r * (size_t) p.w + (size_t) c0));
            if (staleC0 >= c0 && staleC1 <= c1) staleC0 = staleC1 = 0;
            dirty = true; repaint();
            return;
        }
        detail = std::move (p); detailStale = false; staleC0 = staleC1 = 0; detailOpt = lastOpt; dirty = true; repaint();
    }
    /** 3.36.0: the sharp-picture request for the page that would show from t0 - this page's width, and the picture's size, pitch scale and settings as they are
        now (Noise mode makes the next page's ahead of time while it plays, so the page turns straight to its sharp picture). w = 0 when there is none to make. */
    PictureRequest requestForPage (double t0) const
    {
        PictureRequest rq;
        if (! ss.sharp || spec.frames <= 0) return rq;
        const auto pl = plotArea();
        if (pl.getWidth() < 50 || pl.getHeight() < 50) return rq;
        const double span = vt1 - vt0;
        t0 = juce::jlimit (0.0, juce::jmax (0.0, duration - span), t0);            // (as setWindow puts it)
        rq.t0 = t0; rq.t1 = t0 + span; rq.w = pl.getWidth(); rq.h = pl.getHeight(); rq.axis = axisNow();
        rq.opt.reassign = ss.reassign; rq.opt.detail = ss.detail; rq.opt.reduceAboveSec = ss.reduceAbove;
        return rq;
    }
    /** 3.36.0: a sharp picture made ahead (with options 'opt') for exactly the page now in view: shown at once, and none is asked for. False, and nothing
        changes, when it is not for this page any more (the view, its size, the pitch scale or the settings have changed since it was asked for). */
    bool takeReadyDetail (DetailPicture p, const PictureOptions& opt)
    {
        const auto rq = requestForPage (vt0);
        if (rq.w == 0 || ! p.valid() || p.w != rq.w || p.h != rq.h || std::abs (p.t0 - rq.t0) > 1.0e-9 || std::abs (p.t1 - rq.t1) > 1.0e-9 || ! p.axis.same (rq.axis)) return false;
        if (opt.reassign != rq.opt.reassign || std::abs (opt.detail - rq.opt.detail) > 1.0e-9 || std::abs (opt.reduceAboveSec - rq.opt.reduceAboveSec) > 1.0e-9) return false;
        debounce.stopTimer();
        p.id = ++detailSeq; reqGen = detailGen;                                    // (a sharp picture still being made for this page is not wanted now)
        detail = std::move (p); detailStale = false; staleC0 = staleC1 = 0; detailOpt = opt; lastOpt = opt; dirty = true; repaint();
        return true;
    }
    /** 3.36.0: whether the sharp picture of the view as it is now is showing (not still being made, nor the quick picture standing in for it). */
    bool detailShowing() const { return detailCoversView() && staleC1 <= staleC0; }
    /** 3.38.0: whether the sharp picture is the view's as it is now (some of its columns may be out of date: staleC0 .. staleC1). */
    bool detailCoversView() const
    {
        return ss.sharp && detail.valid() && ! detailStale && std::abs (detail.t0 - vt0) < 1.0e-6 && std::abs (detail.t1 - vt1) < 1.0e-6 && detail.axis.same (axisNow());
    }
    const SpectroSettings& settings() const { return ss; }
    void setSettings (const SpectroSettings& n) { ss = n; buildLut(); detailStale = ss.sharp ? true : detailStale; ++detailGen; dirty = true; repaint(); scheduleDetail(); }
    void resized() override { scheduleDetail(); }
    bool hasSelection() const { return hasSel; }
    // ---- 3.33.0: the selection tools - Rectangle (S: any box, as before), Time (T: everything, every pitch, over the time dragged) and Brush (B: a circle painted
    //      over the picture; Shift adds to the selection, Alt takes out, as with the boxes). A click without a drag moves the playhead there (3.35.0: and takes the selection away).
    enum class Tool { rect, time, brush };
    Tool currentTool() const { return tool; }
    void setTool (Tool t) { tool = t; drawing = false; mode = Mode::none; repaint(); if (onTool) onTool(); }
    int brushSize() const { return brushPx; }                     // the brush's radius, in pixels of the picture
    void setBrushSize (int px) { brushPx = juce::jlimit (3, 150, px); repaint(); if (onTool) onTool(); }
    /** 3.34.0: feathering, for every tool: the fixes fade in and out over this much beyond the selection - in time (ms) and in pitch (semitones) - instead of
        stopping at its edge. The picture shows it: the selection's border solid, the fading part shaded less and less, its outer edge dotted. */
    double featherMs() const { return featherT * 1000.0; }
    double featherSemitones() const { return featherP * 12.0; }
    void setFeather (double ms, double semitones)
    {
        featherT = juce::jlimit (0.0, 2.0, ms / 1000.0); featherP = juce::jlimit (0.0, 4.0, semitones / 12.0);
        selectionChanged (true);
        if (onTool) onTool();
    }
    bool feathered() const { return featherT > 0.0 || featherP > 0.0; }
    bool brushed() const { return hasStrokes(); }
    std::function<void()> onTool;                                  // the tool, the brush size or the feathering changed: for the controls
    /** The selection is soft - painted with the brush, or feathered: the fixes then keep only what its mask lets through (SoftMask). */
    bool softSelection() const { return hasSel && (hasStrokes() || feathered()); }
    /** One part of the selection, fixed on its own: a rectangle (no mask: hard-edged, exactly as before) or a soft shape (the box round it, its feathering
        and its mask; 'lead': seconds of the mask before t0, where the box was cut at the start of the picture). */
    struct Part { double t0 = 0.0, t1 = 0.0, f0 = 0.0, f1 = 0.0; std::shared_ptr<const SoftMask> mask; double lead = 0.0; };
    std::vector<Part> selectionParts() const
    {
        if (! softSelection()) return hardParts();
        return softParts;
    }
    /** The rectangles as they are, hard-edged (a box round a click is mended exactly so, feathering or not). */
    std::vector<Part> hardParts() const { std::vector<Part> v; for (auto& r : rects) v.push_back ({ r.t0, r.t1, r.f0, r.f1, nullptr, 0.0 }); return v; }
    /** 3.29.0: the selection as non-overlapping rectangles (seconds, Hz): every box added (Shift) less every part taken out (Alt). What the tools process, one by one. */
    const std::vector<denoise::SelRect>& selectionRects() const { return rects; }
    /** The stretches of time the selection covers (seconds; merged): what the room tone is learned from. */
    std::vector<std::pair<double, double>> selectionSpans() const
    {
        if (! hasStrokes()) return denoise::selectedSpans (rects);
        std::vector<denoise::SelRect> all = rects;                    // (3.33.0: and the time each brush stroke covers)
        for (auto& st : strokes) if (st.add && ! st.dabs.empty()) { double a = 1.0e30, b = -1.0e30; for (auto& d : st.dabs) { a = juce::jmin (a, d.t - d.rt); b = juce::jmax (b, d.t + d.rt); } all.push_back ({ juce::jmax (0.0, a), juce::jmin (duration, b), 100.0, 200.0, true }); }
        return denoise::selectedSpans (all);
    }
    int boxCount() const { int n = 0; for (auto& b : boxes) if (b.add) ++n; for (auto& st : strokes) if (st.add) ++n; return n; }     // (3.30.1: the boxes drawn, not the rectangles they come to; 3.33.0: and brush strokes)
    /** Shows the sound around the box that the repair learns from (dotted): 'pct' is a % of the box, 'direction' 0 = left and right, 1 = up and down, 2 = both. */
    void setSurround (bool show, double pct, int direction, double afterWeight = 0.5) { showSurround = show; surroundPct = pct; surroundDir = direction; surroundAfter = juce::jlimit (0.0, 1.0, afterWeight); repaint(); }
    // the whole selection's extent (3.29.0: of every rectangle together; one box = that box; 3.33.0: and the brush strokes)
    double selT0() const { return extent().t0; }   // seconds
    double selT1() const { return extent().t1; }
    double selF0() const { return extent().f0; }   // Hz
    double selF1() const { return extent().f1; }
    void clearSelection() { clearMarks(); selectionChanged (true); }
    /** 3.75.0: one box put there by the program, in place of whatever was selected (seconds, Hz; kept inside the sound and the picture's pitches). It is an
        ordinary box from then on - moved, resized, added to, feathered. */
    void selectBox (double t0, double t1, double f0, double f1)
    {
        t0 = juce::jlimit (0.0, duration, t0); t1 = juce::jlimit (0.0, duration, t1);
        f0 = juce::jlimit (0.0, nyq(), f0); f1 = juce::jlimit (0.0, nyq(), f1);
        clearMarks();
        if (t1 - t0 >= 0.002 && f1 > f0) { boxes.push_back (denoise::SelRect { t0, t1, f0, f1, true }); boxSeq.push_back (nextSeq++); }
        drawing = false; editing = -1;
        selectionChanged (true);
    }
    /** 3.75.0: moments marked along the picture - a line down it with a small flag at the top (seconds from the start; 'strong': the one picked). They are
        only drawn: clicks go to the picture as always. */
    struct TimeMark { double t = 0.0; juce::String label; juce::Colour colour { 0xffffd24a }; bool strong = false; };
    void setTimeMarks (std::vector<TimeMark> m) { timeMarks = std::move (m); repaint(); }
    /** 3.75.0: the page moved and zoomed to show [t0, t0 + span] (kept inside the sound). */
    void showSpan (double t0, double span) { setWindow (t0, span); }

    // ---- 3.33.0: harmonics. One box of the selection (the last one drawn when they were asked for) is given boxes on the other harmonics of its sound: harmonics
    //      1 to 'count' of its fundamental - the fundamental IS the 1st harmonic - the box itself being harmonic 'boxIs'. They are ordinary parts of the selection (Alt
    //      takes a part out of them), they move and resize with that box, and they stop at the top of the picture. 'spreadCents': 0 = harmonics stacked exactly (2x,
    //      3x ...); more = each one a little sharper the higher it is, as a piano's are (this many cents at the 10th harmonic); less = flatter. 'thinning': 1 (the
    //      standard) = every box as wide in Hz as yours, so the high ones are as narrow as the one you drew and keep clear of their neighbours; 0 = as wide
    //      musically as yours (wider in Hz the higher it is); between, in between. Whatever the setting, a box never reaches more than 45 % of the way to the
    //      harmonic next to it (it would take sound that belongs to that one - or to another instrument).
    struct Harmonics { int count = 10; int boxIs = 1; double spreadCents = 0.0; double thinning = 1.0; };
    bool harmonicsOn() const { return harmBase >= 0; }
    /** Gives the last box drawn its harmonics, or new settings to the box that has them. False: no box to give them to. */
    bool setHarmonics (const Harmonics& h)
    {
        harm = h;
        if (harmBase < 0) for (int i = (int) boxes.size(); i-- > 0;) if (boxes[(size_t) i].add && ! isTimeBox (boxes[(size_t) i])) { harmBase = i; break; }     // (not a Time box: it has every pitch)
        if (harmBase < 0) return false;
        selectionChanged (true);
        return harmBase >= 0;
    }
    void clearHarmonics() { if (harmBase < 0) return; dropHarmonics(); selectionChanged (true); }
    /** The fundamental (Hz) worked out from the pitch found for the harmonic in the box itself (the strongest steady line in it; the middle of the box when there is none). */
    double harmonicFundamental() const { return harmFund; }
    /** The highest harmonic that is still below the top of the picture, with these settings (at least the box's own). */
    int maxHarmonic (const Harmonics& h) const
    {
        if (harmBase < 0 || harmPitch <= 0.0) return juce::jmax (1, h.boxIs);
        const double B = spreadB (h.spreadCents);
        const int k = juce::jmax (1, h.boxIs);
        const double sk = stretchOf (B, k); if (sk <= 0.0) return k;
        const double F = harmPitch / ((double) k * sk);
        int n = k;
        while (n < 4000) { const double s = stretchOf (B, n + 1); if (s <= 0.0 || F * (n + 1) * s >= maxHz()) break; ++n; }
        return n;
    }
    std::function<void()> onHarmonics;                             // the harmonics came or went (a new box drawn on its own takes them away): for the controls
    /** Seconds from the start of the area; < 0 hides the playhead.
        3.52.0: only the thin strip the line leaves and the one it moves to are redrawn. It was the whole picture, 30 times a second, for a
        line two pixels wide - on a big window that alone kept the drawing thread busy. (When a play begins or ends everything is redrawn
        once: the playhead you placed goes or comes back.) */
    void setPlayhead (double t)
    {
        if (std::abs (t - playT) <= 1.0e-6) return;
        const double was = playT; playT = t;
        if ((was < 0.0) != (t < 0.0)) { repaint(); return; }
        const auto pl = plotArea();
        const int x0 = tToX (was, pl), x1 = tToX (t, pl);
        if (x0 == x1) return;
        auto strip = [&] (double at, int x) { if (at >= vt0 && at <= vt1) repaint (x - 4, pl.getY(), 8, pl.getHeight()); };
        strip (was, x0); strip (t, x1);
    }
    /** 3.52.0: where the music is now (seconds from the start of the area; < 0: nothing plays), asked at every refresh of the screen. A window
        that sets it gets a playhead that moves at the screen's own rate; without it the window moves the line itself (setPlayhead). */
    std::function<double()> livePlayhead;
    /** The playhead you place by clicking the time ruler: where Play starts, and the point the zoom keys keep still. */
    double playFrom() const { return cueT >= vt0 && cueT < vt1 ? cueT : vt0; }
    /** The transport: put the placed playhead at t (seconds; < 0 takes it away), and the width of the page shown. */
    void placeCue (double t) { cueT = t < 0.0 ? -1.0 : juce::jlimit (0.0, duration, t); repaint(); }
    /** The playhead placed by clicking the ruler (seconds from the start of the area), or -1 when none is placed. */
    double cueTime() const { return cueT; }
    double pageSpan() const { return vt1 - vt0; }
    /** Where Play starts: the placed playhead if it is on this page (the very end counts), else the left edge of the page. */
    double startPoint() const { return cueT >= vt0 && cueT <= vt1 ? cueT : vt0; }
    /** Makes sure time t is on the page: if it is not, the page is turned so that t is a tenth of the way in (the zoom stays). */
    void showTime (double t) { if (t >= vt0 && t < vt1) return; setWindow (t - 0.1 * (vt1 - vt0), vt1 - vt0); }
    /** Turns to the page that starts at t0 (clamped so the last page ends with the audio; the zoom stays). */
    void showFrom (double t0) { setWindow (t0, vt1 - vt0); }

    // ---- the view: which part of the time is shown (this is what Play plays), which pitches, and how the pitches are spread up the picture
    double visT0() const { return vt0; }
    double visT1() const { return vt1; }
    void resetView() { vt0 = 0.0; vt1 = duration; fLo = kMinHz; fHi = maxHz(); dirty = true; repaint(); scheduleDetail(); if (onView) onView(); }
    /** factor < 1 zooms in, > 1 zooms out, around the middle of what is shown. */
    void zoomTime (double factor)
    {
        const double span = juce::jlimit (juce::jmin (0.05, duration), duration, (vt1 - vt0) * factor);
        // the zoom keeps the playhead where it is on the screen: the one running across the picture, or else the one you placed by clicking the ruler;
        // with neither in view it zooms around the middle
        const double at = (playT >= vt0 && playT <= vt1) ? playT : (cueT >= vt0 && cueT <= vt1) ? cueT : 0.5 * (vt0 + vt1);
        const double frac = (vt1 - vt0) > 0.0 ? (at - vt0) / (vt1 - vt0) : 0.5;
        setWindow (at - frac * span, span);
    }
    void zoomFreq (double factor)
    {
        const double lo = std::log (kMinHz), hi = std::log (maxHz());
        const double span = juce::jlimit (std::log (1.6), hi - lo, std::log (fHi / fLo) * factor);
        setFreqWindow (0.5 * (std::log (fLo) + std::log (fHi)) - 0.5 * span, span);
    }
    /** Dragging the pitch scale at the left moves what is shown up and down (the picture follows the mouse) and never changes how much is shown:
        the shift is made in the units the scale is drawn in (Mel, straight Hz, or octaves), from the window as it was when the drag began. 'down' is the drag
        distance as a fraction of the picture's height (down = positive = higher pitches come into view). */
    void dragPitchWindow (double down)
    {
        auto toU = [this] (double f) { return mel ? PictureAxis::melOf (f) : warp < 0.5 ? f : std::log (f); };
        auto fromU = [this] (double u) { return mel ? PictureAxis::hzOfMel (u) : warp < 0.5 ? u : std::exp (u); };
        const double u0 = toU (panF), u1 = toU (panF1), span = u1 - u0;
        const double minU = toU (kMinHz), maxU = toU (maxHz());
        double lo = u0 + down * span;
        lo = juce::jlimit (minU, juce::jmax (minU, maxU - span), lo);
        fLo = juce::jmax ((double) kMinHz, fromU (lo)); fHi = juce::jmin ((double) maxHz(), fromU (lo + span));
        if (fHi <= fLo * 1.01) fHi = fLo * 1.01;
        dirty = true; repaint(); scheduleDetail();
    }
    /** 0 = pitch spread evenly (linear), 1 = every octave the same height (musical); in between is a blend. */
    void setScale (double w) { warp = juce::jlimit (0.0, 1.0, w); mel = false; dirty = true; repaint(); scheduleDetail(); }
    /** The Mel scale (the pitch scale of iZotope RX). */
    void setMel() { mel = true; dirty = true; repaint(); scheduleDetail(); }
    /** Left / right = zoom out / in in time (right in), up / down = zoom out / in vertically (down in). */
    bool zoomKey (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::rightKey) { zoomTime (0.7); note(); return true; }
        if (k == juce::KeyPress::leftKey)  { zoomTime (1.0 / 0.7); note(); return true; }
        if (k == juce::KeyPress::downKey)  { zoomFreq (0.7); return true; }
        if (k == juce::KeyPress::upKey)    { zoomFreq (1.0 / 0.7); return true; }
        return false;
    }
    /** The round colour control (bottom left): each step is another colour map and another range of levels, so quiet noises show up. */
    void rollColour (double amount) { setRoll (rollPos + amount); }
    double rollValue() const { return rollPos; }
    void setRoll (double p)
    {
        rollPos = std::fmod (std::fmod (p, (double) kPresets) + (double) kPresets, (double) kPresets);
        preset = (int) std::floor (rollPos + 0.5) % kPresets;
        buildLut(); dirty = true; repaint();
        if (onNote) onNote ("Colour: " + juce::String (presetAt (preset).name) + ".  Roll the round control smoothly (drag up / down, or the mouse wheel) to blend between the looks and show quieter sounds; click to jump to the next one; double-click for the standard colours.");
    }

    SpectrogramView() { ss.load (gSpecPrefs); debounce.owner = this; buildLut(); setMouseCursor (juce::MouseCursor::CrosshairCursor); }

    void paint (juce::Graphics& g) override
    {
        BusyLabel busyLabel ("drawing the noise / ReTouch picture");             // 3.52.0: named in the log's stall lines (it was "nothing labelled")
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        paintPicture (g);
        playLog.painted (juce::Time::getMillisecondCounterHiRes() - t0);
    }
    void paintPicture (juce::Graphics& g)
    {
        g.fillAll (theme::wellDark);
        auto plot = plotArea();
        if (dirty || image.getWidth() != plot.getWidth() || image.getHeight() != plot.getHeight()) rebuildImage (plot);
        if (image.isValid()) g.drawImageAt (image, plot.getX(), plot.getY());
        else { g.setColour (juce::Colours::white); g.drawText ("Scanning...", plot, juce::Justification::centred); }
        g.setFont (juce::FontOptions (11.0f));
        // the pitch scale
        {
            static const double ticks[] = { 50, 100, 200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 12000, 15000, 20000 };
            int lastLabelY = plot.getBottom() + 20;
            for (double f : ticks)
            {
                if (f < fLo || f > fHi) continue;
                const int y = hzToY (f, plot);
                g.setColour (juce::Colours::white.withAlpha (0.14f)); g.drawHorizontalLine (y, (float) plot.getX(), (float) plot.getRight());
                if (lastLabelY - y < 14) continue;
                lastLabelY = y;
                g.setColour (juce::Colours::white.withAlpha (0.8f)); g.drawText (f >= 1000 ? juce::String (f / 1000.0, f >= 10000 || std::fmod (f, 1000.0) == 0.0 ? 0 : 1) + "k" : juce::String ((int) f), 2, y - 7, plot.getX() - 6, 14, juce::Justification::right);
            }
        }
        // the time ruler (above the picture): drag it to move the view left and right, double-click to see everything
        {
            auto ruler = rulerArea();
            g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRect (ruler);
            g.setColour (theme::border); g.drawHorizontalLine (ruler.getBottom() - 1, (float) ruler.getX(), (float) ruler.getRight());
            const double span = vt1 - vt0;
            static const double steps[] = { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
            double step = 600.0; for (double s : steps) if (s / span * plot.getWidth() >= 70.0) { step = s; break; }
            const double sub = step / 5.0;
            for (double t = std::ceil (vt0 / sub - 1.0e-9) * sub; t <= vt1 + 1.0e-9; t += sub)
            {
                const int x = tToX (t, plot);
                const bool major = std::abs (t / step - std::round (t / step)) < 1.0e-6;
                g.setColour (juce::Colours::white.withAlpha (major ? 0.7f : 0.3f)); g.drawVerticalLine (x, (float) (ruler.getBottom() - (major ? 10 : 5)), (float) (ruler.getBottom() - 1));
                if (! major) continue;
                g.setColour (juce::Colours::white.withAlpha (0.15f)); g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.drawText (specTime (t, step < 0.1 ? 2 : step < 1 ? 1 : 0), x + 2, ruler.getY(), 70, ruler.getHeight() - 8, juce::Justification::centredLeft);     // (3.33.0: minutes and seconds from 60 s)
            }
        }
        {
            juce::Graphics::ScopedSaveState saved (g);
            g.reduceClipRegion (plot);
            if (hasSel && showSurround && ! (softSelection() && ! softStale))
                for (auto& sr : rects)                                                  // (3.29.0: the surround of every rectangle of the selection: each is repaired on its own)
                {
                    const double t0 = sr.t0, t1 = sr.t1, f0 = sr.f0, f1 = sr.f1;
                    const double dt = (t1 - t0) * surroundPct / 100.0, df = (f1 - f0) * surroundPct / 100.0;
                    auto box = [&] (double ta, double tb, double fa, double fb)
                    {
                        ta = juce::jmax (0.0, ta); tb = juce::jmin (duration, tb); fa = juce::jmax (kMinHz, fa); fb = juce::jmin (maxHz(), fb);
                        if (tb <= ta || fb <= fa) return;
                        const int x0 = tToX (ta, plot), x1 = tToX (tb, plot), y0 = hzToY (fb, plot), y1 = hzToY (fa, plot);
                        juce::Rectangle<int> r (x0, y0, juce::jmax (2, x1 - x0), juce::jmax (2, y1 - y0));
                        g.setColour (juce::Colour (0x2229d3ff)); g.fillRect (r);
                        juce::Path p; p.addRectangle (r.toFloat());
                        juce::Path dashed; const float pattern[] = { 4.0f, 4.0f };
                        juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, pattern, 2);
                        g.setColour (juce::Colour (0xff29d3ff)); g.fillPath (dashed);
                    };
                    if (surroundDir != 1) { box (t0 - 2.0 * dt * (1.0 - surroundAfter), t0, f0, f1); box (t1, t1 + 2.0 * dt * surroundAfter, f0, f1); }      // before and after the box: shared out by the before / after weighting (50/50 = equal, 0 = all before)
                    if (surroundDir != 0) { box (t0, t1, f1, f1 + df); box (t0, t1, f0 - df, f0); }          // above and below it
                }
            if (softSelection() && ! softStale) paintSoft (g, plot);                   // 3.33.0: a shape painted with the brush (3.34.0: or feathered)
            else if (hasSel)
            {   // 3.29.0: the selection as one shape: its rectangles filled, and a border round the outside of the shape (a part taken out shows as a hole)
                juce::RectangleList<int> inner, outer;
                for (auto& sr : rects) { const auto r = rectOf (sr); inner.add (r); outer.add (r.expanded (2)); }
                g.setColour (juce::Colour (0x44ffd24a)); g.fillRectList (inner);
                outer.subtract (inner);
                g.setColour (juce::Colour (0xffffd24a)); g.fillRectList (outer);
            }
            if (hasSel && harmBase >= 0 && harmBase < (int) boxes.size())
            {   // 3.33.0: the box that carries the harmonics, outlined dashed (drag it to move them all), and each harmonic's number at its left
                const auto br = rectOf (boxes[(size_t) harmBase]).expanded (3);
                juce::Path p; p.addRectangle (br.toFloat());
                juce::Path dashed; const float pattern[] = { 5.0f, 3.0f };
                juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, pattern, 2);
                g.setColour (juce::Colours::white.withAlpha (0.9f)); g.fillPath (dashed);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                auto number = [&] (const denoise::SelRect& r, int n)
                {
                    const auto rr = rectOf (r);
                    if (rr.getBottom() < plot.getY() || rr.getY() > plot.getBottom()) return;
                    const int tx = rr.getX() - 22 >= plot.getX() ? rr.getX() - 22 : rr.getX() + 3;       // (left of the box; inside it when the box starts at the edge)
                    const auto tag = juce::Rectangle<int> (tx, rr.getCentreY() - 7, 19, 14);
                    g.setColour (juce::Colour (0xd0101418)); g.fillRoundedRectangle (tag.toFloat(), 3.0f);
                    g.setColour (juce::Colour (0xffffd24a)); g.drawText (juce::String (n), tag, juce::Justification::centred, false);
                };
                int lastY = 1 << 30;                                    // (numbers that would sit on each other are left out: the boxes are close together at the top)
                std::vector<std::pair<const denoise::SelRect*, int>> all;
                for (size_t i = 0; i < harmBoxes.size(); ++i) all.push_back ({ &harmBoxes[i], harmNums[i] });
                all.push_back ({ &boxes[(size_t) harmBase], juce::jmax (1, harm.boxIs) });
                std::sort (all.begin(), all.end(), [] (const auto& x, const auto& y) { return x.second < y.second; });
                for (auto& [r, n] : all) { const int y = rectOf (*r).getCentreY(); if (lastY - y >= 15) { number (*r, n); lastY = y; } }
            }
            if (mode == Mode::paint && ! paintPts.empty())
            {   // 3.33.0: the stroke being painted (the shape is made when the button comes up)
                g.setColour (drawAdd ? juce::Colour (0x55ffd24a) : juce::Colour (0x55ff5a4a));
                const float rr = (float) brushPx;
                for (auto& q : paintPts) g.fillEllipse ((float) q.x - rr, (float) q.y - rr, 2.0f * rr, 2.0f * rr);
            }
            if (tool == Tool::brush && hover.x >= 0 && plot.contains (hover))
            {   // 3.33.0: the brush, where it would paint
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.drawEllipse ((float) (hover.x - brushPx), (float) (hover.y - brushPx), 2.0f * (float) brushPx, 2.0f * (float) brushPx, 1.4f);
            }
            if (mode == Mode::select && drawing)
            {   // the box being drawn: added (yellow) or taken out (red), outline only until the mouse is let go (3.33.0: Time - all of the picture's height)
                auto r = rectOf (denoise::SelRect { juce::jmin (a.x, b.x), juce::jmax (a.x, b.x), juce::jmin (a.y, b.y), juce::jmax (a.y, b.y), drawAdd });
                if (tool == Tool::time) r = juce::Rectangle<int> (r.getX(), plot.getY(), r.getWidth(), plot.getHeight());
                g.setColour (drawAdd ? juce::Colour (0xffffd24a) : juce::Colour (0xffff5a4a)); g.drawRect (r, 2);
                if (! drawAdd) { g.setColour (juce::Colour (0x55ff5a4a)); g.fillRect (r); }
            }
            for (auto& mk : timeMarks)                                                // 3.75.0: the moments marked along the picture (the Spectrogram Editing window's highest peaks): their lines ...
            {
                if (mk.t < vt0 || mk.t > vt1 || duration <= 0.0) continue;
                const int x = tToX (mk.t, plot);
                g.setColour (mk.colour.withAlpha (mk.strong ? 0.95f : 0.6f));
                if (mk.strong) g.fillRect (x, plot.getY(), 1, plot.getHeight());
                else for (int y = plot.getY(); y < plot.getBottom(); y += 7) g.fillRect (x, y, 1, 4);
            }
            if (playT < 0.0 && cueT >= vt0 && cueT <= vt1 && duration > 0.0)          // the playhead you placed on the ruler
            {
                const int x = tToX (cueT, plot);
                g.setColour (theme::playhead); g.fillRect (x - 1, plot.getY(), 2, plot.getHeight());
            }
            if (playT >= 0.0 && duration > 0.0 && playT >= vt0 && playT <= vt1)
            {
                const int x = tToX (playT, plot);
                g.setColour (theme::playhead); g.fillRect (x - 1, plot.getY(), 2, plot.getHeight());
            }
            for (auto& mk : timeMarks)                                                // ... and their flags, over the playhead (a flag under it could not be read)
            {
                if (mk.t < vt0 || mk.t > vt1 || duration <= 0.0 || mk.label.isEmpty()) continue;
                const int x = tToX (mk.t, plot);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                const int w = juce::jmax (16, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), mk.label) + 8);
                const juce::Rectangle<int> flag (juce::jlimit (plot.getX(), juce::jmax (plot.getX(), plot.getRight() - w), x - w / 2), plot.getY() + 2, w, 15);
                g.setColour (mk.colour.withAlpha (mk.strong ? 1.0f : 0.85f)); g.fillRoundedRectangle (flag.toFloat(), 3.0f);
                g.setColour (juce::Colours::black); g.drawText (mk.label, flag, juce::Justification::centred, false);
            }
        }
        // the round colour control
        {
            const auto k = knobArea().toFloat(); const auto c = k.getCentre(); const float r = k.getWidth() * 0.5f;
            g.setColour (juce::Colour (0xff22262c)); g.fillEllipse (k);
            g.setColour (theme::border); g.drawEllipse (k.reduced (0.5f), 1.2f);
            for (int i = 0; i < kPresets; ++i)                                        // one dot per colour map, the lit one is the one in use
            {
                const float ang = juce::MathConstants<float>::twoPi * (float) i / (float) kPresets - juce::MathConstants<float>::halfPi;
                g.setColour (i == preset ? juce::Colours::white.withAlpha (0.8f) : juce::Colours::white.withAlpha (0.28f));
                g.fillEllipse (c.x + std::cos (ang) * (r - 3.0f) - 1.4f, c.y + std::sin (ang) * (r - 3.0f) - 1.4f, 2.8f, 2.8f);
            }
            g.setColour (lut[170]); g.fillEllipse (c.x - r * 0.45f, c.y - r * 0.45f, r * 0.9f, r * 0.9f);
            const float ang = juce::MathConstants<float>::twoPi * (float) rollPos / (float) kPresets - juce::MathConstants<float>::halfPi;
            g.setColour (juce::Colours::white); g.drawLine (c.x, c.y, c.x + std::cos (ang) * (r - 4.5f), c.y + std::sin (ang) * (r - 4.5f), 1.8f);
            g.setColour (juce::Colours::white.withAlpha (0.75f)); g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (presetAt (preset).name) + "   (colour roll)", (int) k.getRight() + 8, (int) k.getY(), 300, (int) k.getHeight(), juce::Justification::centredLeft);
        }
        paintHover (g);                                                           // 3.28.0: the frequency under the pointer
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        mode = Mode::none;
        if (knobArea().expanded (3).contains (e.getPosition())) { mode = Mode::knob; rollStart = rollPos; return; }
        if (rulerArea().contains (e.getPosition())) { mode = Mode::pan; panT0 = vt0; return; }
        auto pl = plotArea();
        if (e.x < pl.getX() && e.y >= pl.getY() && e.y < pl.getBottom()) { mode = Mode::pitchPan; panF = fLo; panF1 = fHi; return; }
        const bool shift = e.mods.isShiftDown(), alt = e.mods.isAltDown();       // 3.29.0: Shift = another box added to the selection, Alt = a box taken out of it
        downAt = e.getPosition(); moved = false;                                 // 3.33.0: a click without a drag places the playhead
        if (! shift && ! alt && tool != Tool::brush)
        {
            int hit = -1;
            if (const int ed = edgesAt (e.getPosition(), &hit))               // on an edge or corner of a box: drag it to resize that box
            {
                editing = hit;
                a = { boxes[(size_t) hit].t0, boxes[(size_t) hit].f0 }; b = { boxes[(size_t) hit].t1, boxes[(size_t) hit].f1 };
                boxedit::order (a.x, a.y, b.x, b.y);                       // a = lower-left corner, b = upper-right (3.28.0: all four read before either is set - see BoxEdges.h)
                const auto r = rectOf (boxes[(size_t) hit]);               // where the pointer is on the edge, so the edge does not jump to the pointer
                grabDx = (ed & 3) ? e.x - ((ed & 1) ? r.getX() : r.getRight()) : 0;
                grabDy = (ed & 12) ? e.y - ((ed & 4) ? r.getY() : r.getBottom()) : 0;
                mode = Mode::resize; edgeMask = ed; return;
            }
            if (! pl.contains (e.getPosition())) return;
            if ((hit = boxAt (e.getPosition())) >= 0)
            {   // 3.28.0: inside a box (away from its edges): drag it to move that box, the same size, in time and in pitch. (3.35.0: a click without a drag places the playhead and takes the selection away.)
                editing = hit;
                a = { boxes[(size_t) hit].t0, boxes[(size_t) hit].f0 }; b = { boxes[(size_t) hit].t1, boxes[(size_t) hit].f1 };
                boxedit::order (a.x, a.y, b.x, b.y);
                moveA = a; moveB = b; mode = Mode::move; return;
            }
        }
        if (! pl.contains (e.getPosition())) return;
        if (alt && boxes.empty() && ! hasStrokes()) { mode = Mode::cue; return; }      // nothing is selected: nothing to take out (a click still places the playhead)
        // a plain drag starts afresh (3.30.1: the old shape goes at once, as before 3.29.0) - 3.33.0: once it IS a drag (3.35.0: a plain click takes it away, see clickPlaces)
        freshOnDrag = ! shift && ! alt;
        drawAdd = ! alt; drawing = false;
        a = toTF (e.getPosition()); b = a;
        mode = tool == Tool::brush ? Mode::paint : Mode::select;
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        setHover (e.getPosition());
        using C = juce::MouseCursor;
        if (e.mods.isShiftDown() || e.mods.isAltDown()) { setMouseCursor (C (C::CrosshairCursor)); return; }       // (3.29.0: a box is about to be added / taken out)
        const int ed = edgesAt (e.getPosition());
        if (ed == 0 && boxAt (e.getPosition()) >= 0) { setMouseCursor (C (C::UpDownLeftRightResizeCursor)); return; }     // (3.28.0: inside a box - drag to move it)
        setMouseCursor (ed == 0 ? C (C::CrosshairCursor)
                        : ed == 1 || ed == 2 ? C (C::LeftRightResizeCursor)
                        : ed == 4 || ed == 8 ? C (C::UpDownResizeCursor)
                        : ed == (1 | 4) ? C (C::TopLeftCornerResizeCursor) : ed == (2 | 4) ? C (C::TopRightCornerResizeCursor)
                        : ed == (1 | 8) ? C (C::BottomLeftCornerResizeCursor) : C (C::BottomRightCornerResizeCursor));
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto pl = plotArea();
        if ((mode == Mode::select || mode == Mode::paint || mode == Mode::cue) && ! moved)
        {   // 3.33.0: it becomes a drag only once it has gone a few pixels (less is a click: the playhead)
            if (e.getDistanceFromDragStart() < 3) return;
            moved = true;
            if (mode == Mode::cue) return;
            if (freshOnDrag) { clearMarks(); rects.clear(); softParts.clear(); hasSel = false; }
            if (mode == Mode::select) drawing = true;
            if (mode == Mode::paint) { strokes.push_back (Stroke { {}, drawAdd, nextSeq++ }); paintPts.clear(); addDab (downAt); }
        }
        if (mode == Mode::paint) { setHover (e.getPosition()); addDab (e.getPosition()); return; }
        if (mode == Mode::cue) return;
        if (mode == Mode::move || mode == Mode::resize) moved = moved || e.getDistanceFromDragStart() >= 3;
        if (mode == Mode::select || mode == Mode::resize || mode == Mode::move) setHover (e.getPosition());     // (the read-out follows while a box is drawn or moved)
        if (mode == Mode::select) { b = toTF (e.getPosition()); repaint(); }
        else if (mode == Mode::move)
        {
            // 3.28.0: the box moves with the pointer from where it was taken hold of. In time by seconds; in pitch in the units the scale is drawn in (as the pitch
            // scale's drag), so the box keeps its height on the screen. It stops at the start and end of the audio and at the bottom and top of the pitch range.
            const double dt = (double) e.getDistanceFromDragStartX() / (double) juce::jmax (1, pl.getWidth()) * (vt1 - vt0);
            double t0 = moveA.x, t1 = moveB.x;
            boxedit::moveSpan (t0, t1, dt, 0.0, duration);
            auto toU = [this] (double f) { return mel ? PictureAxis::melOf (f) : warp < 0.5 ? f : std::log (f); };
            auto fromU = [this] (double u) { return mel ? PictureAxis::hzOfMel (u) : warp < 0.5 ? u : std::exp (u); };
            if (editing >= 0 && editing < (int) boxes.size() && isTimeBox (boxes[(size_t) editing])) { a = { t0, 0.0 }; b = { t1, nyq() }; }     // 3.33.0: a Time box: in time only
            else
            {
                const double du = -(double) e.getDistanceFromDragStartY() / (double) juce::jmax (1, pl.getHeight() - 1) * (toU (fHi) - toU (fLo));
                double u0 = toU (moveA.y), u1 = toU (moveB.y);
                boxedit::moveSpan (u0, u1, du, toU (kMinHz), toU (maxHz()));
                a = { t0, juce::jmax ((double) kMinHz, fromU (u0)) }; b = { t1, juce::jmin (maxHz(), fromU (u1)) };
            }
            if (editing >= 0 && editing < (int) boxes.size()) { writeBack (editing); selectionChanged (false); }     // (3.29.0: the shape follows while it is moved)
        }
        else if (mode == Mode::resize)
        {
            // Each side belongs to one axis: a left / right edge changes the width and nothing else, a top / bottom edge changes the height and nothing else;
            // only a corner changes both. The edge being dragged follows the pointer (where it was grabbed), the opposite edge stays where it is, and the
            // box is kept at least a few pixels across so it cannot collapse to a line while it is being resized.
            const auto pl2 = plotArea();
            const auto q = toTF ({ e.x - grabDx, e.y - grabDy });
            if (edgeMask & 3)                                              // width: a = left, b = right (they may swap places if the edge is dragged across)
            {
                const double minT = juce::jmax (0.002, 3.0 * (vt1 - vt0) / (double) juce::jmax (1, pl2.getWidth()));
                double& mv = (edgeMask & 1) ? a.x : b.x;
                const double fixedT = (edgeMask & 1) ? b.x : a.x;
                double v = q.x;
                if (std::abs (v - fixedT) < minT)
                {
                    const double side = v >= fixedT ? 1.0 : -1.0;
                    v = fixedT + side * minT;
                    if (v < 0.0 || v > duration) v = fixedT - side * minT;
                }
                if (v >= 0.0 && v <= duration) mv = v;
            }
            if (edgeMask & 12)                                             // height: b.y = top (highest pitch), a.y = bottom
            {
                double& mv = (edgeMask & 4) ? b.y : a.y;
                const double fixedF = (edgeMask & 4) ? a.y : b.y;
                const int yFixed = hzToY (fixedF, pl2);
                const int yTop = pl2.getY(), yBottom = pl2.getBottom() - 1;
                int yy = juce::jlimit (yTop, yBottom, e.y - grabDy);
                if (std::abs (yy - yFixed) < 3)
                {
                    const int side = yy >= yFixed ? 1 : -1;
                    yy = yFixed + side * 3;
                    if (yy < yTop || yy > yBottom) yy = yFixed - side * 3;
                }
                if (yy >= yTop && yy <= yBottom) mv = toTF ({ pl2.getX(), yy }).y;
            }
            if (editing >= 0 && editing < (int) boxes.size()) { writeBack (editing); selectionChanged (false); }     // (3.29.0: the shape follows while it is resized)
        }
        else if (mode == Mode::pan) setWindow (panT0 - (double) e.getDistanceFromDragStartX() / (double) juce::jmax (1, pl.getWidth()) * (vt1 - vt0), vt1 - vt0);
        else if (mode == Mode::pitchPan) dragPitchWindow ((double) e.getDistanceFromDragStartY() / (double) juce::jmax (1, pl.getHeight()));
        else if (mode == Mode::knob)
        {
            setRoll (rollStart - (double) e.getDistanceFromDragStartY() / 45.0);       // smooth: about 45 px per colour look
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto m = mode; mode = Mode::none;
        if (m == Mode::knob && e.getDistanceFromDragStart() < 4) { setRoll (std::floor (rollPos + 0.5) + (e.mods.isRightButtonDown() || e.mods.isShiftDown() ? -1 : 1)); return; }
        if (m == Mode::pan && e.getDistanceFromDragStart() < 4)                  // a click (not a drag) on the time ruler puts the playhead there
        {
            const auto pl = plotArea();
            cueT = juce::jlimit (0.0, duration, vt0 + (double) (e.x - pl.getX()) / (double) juce::jmax (1, pl.getWidth()) * (vt1 - vt0));
            repaint(); return;
        }
        if (m == Mode::pan || m == Mode::pitchPan) { note(); return; }
        if ((m == Mode::select || m == Mode::paint || m == Mode::cue) && ! moved) { clickPlaces (m != Mode::cue && freshOnDrag); return; }    // 3.33.0: a click: the playhead goes there
        if (m == Mode::paint) { paintPts.clear(); selectionChanged (true); return; }
        if (m == Mode::move)                                                     // (a click in the box: nothing changes - 3.33.0: but the playhead goes there)
        {
            if (a != moveA || b != moveB)                                        // (3.30.1: told even when the box slid wholly under a take-out and the selection emptied under the drag)
            {
                if (editing >= 0 && editing < (int) boxes.size()) writeBack (editing);
                editing = -1; selectionChanged (true);
            }
            else if (! moved) { clickPlaces (true); return; }                    // (3.35.0: and the selection goes)
            editing = -1; return;
        }
        if (m == Mode::resize)
        {
            if (! moved) { clickPlaces (true); return; }                          // (a click on an edge: the playhead - and the selection goes)
            if (editing >= 0 && editing < (int) boxes.size()) writeBack (editing);
            editing = -1; selectionChanged (true); return;
        }
        if (m != Mode::select) return;
        drawing = false;
        const double t0 = juce::jmin (a.x, b.x), t1 = juce::jmax (a.x, b.x);
        const double f0 = tool == Tool::time ? 0.0 : juce::jmin (a.y, b.y), f1 = tool == Tool::time ? nyq() : juce::jmax (a.y, b.y);     // (3.33.0: Time: every pitch)
        if (t1 - t0 >= 0.002 && f1 > f0) { boxes.push_back (denoise::SelRect { t0, t1, f0, f1, drawAdd }); boxSeq.push_back (nextSeq++); }     // (3.30.1: a flat box selects nothing and is not kept)
        selectionChanged (true);
    }
    /** 3.33.0: the brush's shape on the screen: the selected part filled (as a box is) and its border drawn round it, made again when the shape or the view changes. */
    void paintSoft (juce::Graphics& g, juce::Rectangle<int> plot)
    {
        const std::array<double, 6> viewNow { vt0, vt1, fLo, fHi, warp, mel ? 1.0 : 0.0 };
        if (softImageVersion != softVersion || softImageArea != plot || softImageView != viewNow)
        {
            softImageVersion = softVersion; softImageArea = plot; softImageView = viewNow;
            const int W = plot.getWidth(), H = plot.getHeight();
            softImage = juce::Image();
            if (W < 2 || H < 2 || sg.nt <= 0) return;
            std::vector<int> col ((size_t) W), row ((size_t) H);
            for (int x = 0; x < W; ++x) { const double t = vt0 + ((double) x + 0.5) / (double) W * (vt1 - vt0); const int i = (int) std::floor ((t - sg.t0) / sg.dt); col[(size_t) x] = i >= 0 && i < sg.nt ? i : -1; }
            for (int y = 0; y < H; ++y) { const double hz = hzOf (1.0 - (double) y / (double) (H - 1)); const int j = (int) std::floor ((std::log2 (juce::jmax (1.0, hz)) - sg.lf0) / sg.dl); row[(size_t) y] = j >= 0 && j < sg.nf ? j : -1; }
            std::vector<uint8_t> on ((size_t) W * (size_t) H, 0);                         // 1: selected; 2: in the feathering
            std::vector<float> amt ((size_t) W * (size_t) H, 0.0f);
            const bool haveSoft = sg.soft.size() == sg.hard.size();
            for (int y = 0; y < H; ++y)
            {
                const int j = row[(size_t) y]; if (j < 0) continue;
                for (int x = 0; x < W; ++x)
                {
                    const int i = col[(size_t) x]; if (i < 0) continue;
                    const size_t c = (size_t) i * (size_t) sg.nf + (size_t) j, px = (size_t) y * (size_t) W + (size_t) x;
                    if (sg.hard[c]) { on[px] = 1; amt[px] = 1.0f; }
                    else if (haveSoft && sg.soft[c] > 1.0e-3f) { on[px] = 2; amt[px] = sg.soft[c]; }
                }
            }
            softImage = juce::Image (juce::Image::ARGB, W, H, true);
            juce::Image::BitmapData bd (softImage, juce::Image::BitmapData::writeOnly);
            const auto fill = juce::Colour (0x44ffd24a), edge = juce::Colour (0xffffd24a), outer = juce::Colour (0xc8ffd24a);
            auto at = [&] (int xx, int yy) { return xx >= 0 && xx < W && yy >= 0 && yy < H ? on[(size_t) yy * (size_t) W + (size_t) xx] : (uint8_t) 0; };
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const auto here = on[(size_t) y * (size_t) W + (size_t) x];
                    const bool selHere = here == 1, anyHere = here != 0;
                    const bool selEdge = selHere != (at (x + 1, y) == 1) || selHere != (at (x, y + 1) == 1) || selHere != (at (x - 1, y) == 1) || selHere != (at (x, y - 1) == 1);
                    const bool outEdge = anyHere != (at (x + 1, y) != 0) || anyHere != (at (x, y + 1) != 0) || anyHere != (at (x - 1, y) != 0) || anyHere != (at (x, y - 1) != 0);
                    if (selEdge) { bd.setPixelColour (x, y, edge); continue; }                               // the selection's own edge: solid
                    if (outEdge && anyHere && ((x + y) / 3) % 2 == 0) { bd.setPixelColour (x, y, outer); continue; }   // how far the feathering reaches: dotted
                    if (anyHere) bd.setPixelColour (x, y, fill.withMultipliedAlpha (amt[(size_t) y * (size_t) W + (size_t) x]));   // shaded as much as the fix there
                }
        }
        if (softImage.isValid()) { g.setOpacity (1.0f); g.drawImageAt (softImage, plot.getX(), plot.getY()); }      // (an image is drawn with the opacity of the colour last set: full here)
    }
    /** 3.33.0: the playhead placed where the picture was clicked (as a click on the ruler does). */
    void placeCueAt (juce::Point<int> p) { cueT = juce::jlimit (0.0, duration, toTF (p).x); repaint(); if (onCue) onCue(); }
    /** A click without a drag on the picture: the playhead goes there, and (3.35.0) a plain click - no Shift or Alt - takes the selection away too (the boxes,
        the brush strokes, the harmonics). A Shift or Alt click keeps it (they build a selection), and so does a click on the time ruler. */
    void clickPlaces (bool plain)
    {
        drawing = false; editing = -1;
        if (plain && (hasSel || ! boxes.empty() || hasStrokes())) clearSelection();
        placeCueAt (downAt);
    }
    std::function<void()> onCue;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (knobArea().expanded (3).contains (e.getPosition())) { rollColour ((double) w.deltaY * 2.0); return; }
        Component::mouseWheelMove (e, w);
    }
    void mouseExit (const juce::MouseEvent&) override { setHover ({ -1, -1 }); }
    /** 3.28.0: the reference A the read-out names notes from (the project's pitch pipe: A = 415 at a Baroque session). */
    void setReferenceA (double hz) { if (hz > 0.0 && std::isfinite (hz)) refA = hz; }
    double referenceA() const { return refA; }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (knobArea().expanded (3).contains (e.getPosition())) { setRoll (0.0); return; }                 // back to the standard colours
        if (rulerArea().contains (e.getPosition())) { resetView(); note(); return; }                               // everything again
        if (plotArea().contains (e.getPosition())) clearSelection();
    }
private:
    static constexpr double kMinHz = 40.0;
    static constexpr int kPresets = 8;
    enum class Mode { none, select, pan, pitchPan, knob, resize, move, paint, cue };
    int edgeMask = 0;                                           // while resizing the box: 1 left, 2 right, 4 top, 8 bottom
    juce::Point<double> moveA, moveB;                           // while moving the box: its corners (lower-left, upper-right) when it was taken hold of
    // 3.29.0: the selection is a list of boxes in the order they were drawn, each added (Shift, or the first) or taken out (Alt); 'rects' is what that comes to
    std::vector<denoise::SelRect> boxes, rects;
    int editing = -1;                                           // the box being resized or moved (an index into boxes)
    bool drawing = false, drawAdd = true;                       // a box is being drawn: added or taken out
    /** The box, in pixels, of a rectangle in seconds and Hz. */
    juce::Rectangle<int> rectOf (const denoise::SelRect& r) const
    {
        auto pl = plotArea();
        const int x0 = tToX (juce::jmin (r.t0, r.t1), pl), x1 = tToX (juce::jmax (r.t0, r.t1), pl);
        const int y0 = hzToY (juce::jmax (r.f0, r.f1), pl), y1 = hzToY (juce::jmin (r.f0, r.f1), pl);
        return { x0, y0, juce::jmax (2, x1 - x0), juce::jmax (2, y1 - y0) };
    }
    /** The box (added, so that there is something to see) under the point, away from its edges: the last one drawn first. -1: none. */
    int boxAt (juce::Point<int> p) const
    {
        if (! plotArea().contains (p) || tool == Tool::brush) return -1;
        for (int i = (int) boxes.size(); i-- > 0;) if (boxes[(size_t) i].add && rectOf (boxes[(size_t) i]).contains (p)) return i;
        return -1;
    }
    /** The edited corners a / b written back into box i. */
    void writeBack (int i)
    {
        auto& r = boxes[(size_t) i]; const bool timeBox = isTimeBox (r);
        r.t0 = juce::jmin (a.x, b.x); r.t1 = juce::jmax (a.x, b.x);
        if (timeBox) { r.f0 = 0.0; r.f1 = nyq(); return; }                     // (3.33.0: a Time box keeps every pitch)
        r.f0 = juce::jmin (a.y, b.y); r.f1 = juce::jmax (a.y, b.y);
    }
    /** The boxes changed: the rectangles they come to are worked out again; a selection that has nothing left in it is no selection. */
    void selectionChanged (bool tell)
    {
        makeHarmonics();                                            // (they follow their box: it may have been moved or resized)
        rects = denoise::selectionRects (allBoxes());
        if (rects.empty() && ! hasStrokes()) { boxes.clear(); boxSeq.clear(); dropHarmonics(); }
        else if (tell && ! hasStrokes())                            // (3.33.0: with brush strokes every box stays: a take-out may be taking from a stroke)
        {   // 3.30.1: a box that shows nothing any more (wholly under a later take-out, or a take-out that took nothing) is dropped - it could still be grabbed and
            // moved as an invisible ghost. Only at the end of a drag ('tell'): in the middle of one 'editing' points into the list.
            // 3.33.0: a box taken out (Alt) is kept when it takes something out of the boxes BEFORE it. It was looked for in what is left - where it never is,
            // that being what it took out - so every take-out was dropped the moment the mouse was let go, and the hole closed again. The box that carries
            // harmonics is always kept (they hang on it), even when all of it has been taken out.
            auto overlaps = [] (const denoise::SelRect& b, const std::vector<denoise::SelRect>& rs)
            {
                for (auto& r : rs) if (juce::jmin (b.t1, r.t1) > juce::jmax (b.t0, r.t0) && juce::jmin (b.f1, r.f1) > juce::jmax (b.f0, r.f0)) return true;
                return false;
            };
            std::vector<denoise::SelRect> kept, sofar; std::vector<int> keptSeq; int base = -1;
            for (size_t i = 0; i < boxes.size(); ++i)
            {
                const auto& bx = boxes[i];
                const bool carries = (int) i == harmBase;
                if (! carries && ! (bx.add ? overlaps (bx, rects) : overlaps (bx, denoise::selectionRects (sofar)))) continue;
                if (carries) base = (int) kept.size();
                kept.push_back (bx); keptSeq.push_back (i < boxSeq.size() ? boxSeq[i] : 0); sofar.push_back (bx);
                if (carries) sofar.insert (sofar.end(), harmBoxes.begin(), harmBoxes.end());
            }
            if (kept.size() != boxes.size()) { boxes = std::move (kept); boxSeq = std::move (keptSeq); harmBase = base; rects = denoise::selectionRects (allBoxes()); }
            editing = -1;
        }
        if (tell) editing = -1;
        hasSel = ! rects.empty() || hasAddStrokes();
        ++softVersion;
        softStale = ! tell && ! hasStrokes();                       // (3.34.0: while a box is dragged with feathering on it shows as a box; the soft shape is made when it is let go)
        if ((hasStrokes() || feathered()) && hasSel && ! softStale) { softParts.clear(); buildSoft(); if (softParts.empty()) { clearMarks(); rects.clear(); hasSel = false; } }     // (3.33.0: the brush's shape and its parts; 3.34.0: feathered)
        if (! (hasStrokes() || feathered())) softParts.clear();
        repaint();
        if (tell && onSelection) onSelection();
        if (tell && onHarmonics) onHarmonics();
    }
    // ---- 3.33.0: the harmonics (see setHarmonics)
    int harmBase = -1;                                          // the box (an index into boxes) that carries them; -1: none
    Harmonics harm;
    std::vector<denoise::SelRect> harmBoxes;                    // the boxes made on its harmonics (not the box itself), lowest first
    std::vector<int> harmNums;                                  // the harmonic number of each
    double harmPitch = 0.0, harmFund = 0.0;                     // the pitch found in the box, and the fundamental worked out from it (Hz)
    void dropHarmonics() { harmBase = -1; harmBoxes.clear(); harmNums.clear(); harmPitch = harmFund = 0.0; }
    /** The boxes in the order they were drawn, the harmonics straight after the box they belong to (so a later take-out takes from them too). */
    std::vector<denoise::SelRect> allBoxes() const
    {
        std::vector<denoise::SelRect> v; v.reserve (boxes.size() + harmBoxes.size());
        for (size_t i = 0; i < boxes.size(); ++i) { v.push_back (boxes[i]); if ((int) i == harmBase) v.insert (v.end(), harmBoxes.begin(), harmBoxes.end()); }
        return v;
    }
    /** 'Spread' as the stiffness of a string (the inharmonicity B in f(n) = n f sqrt (1 + B (n^2 - 1))): the 10th harmonic is 'cents' sharp (or flat, below 0). */
    static double spreadB (double cents) { return (std::pow (2.0, cents / 600.0) - 1.0) / 99.0; }
    static double stretchOf (double B, int n) { const double q = 1.0 + B * ((double) n * (double) n - 1.0); return q > 1.0e-4 ? std::sqrt (q) : 0.0; }
    void makeHarmonics()
    {
        harmBoxes.clear(); harmNums.clear(); harmPitch = harmFund = 0.0;
        if (harmBase < 0 || harmBase >= (int) boxes.size() || ! boxes[(size_t) harmBase].add) { harmBase = -1; return; }
        const auto& b = boxes[(size_t) harmBase];
        if (b.f1 <= b.f0 || b.f0 <= 0.0) return;
        const int k = juce::jmax (1, harm.boxIs);
        const double fm = measurePitch (b);
        const double B = spreadB (harm.spreadCents);
        const double sk = stretchOf (B, k);
        if (sk <= 0.0) return;
        harmPitch = fm; harmFund = fm / ((double) k * sk);
        const double lowHz = juce::jmax (0.0, fm - b.f0), highHz = juce::jmax (0.0, b.f1 - fm);       // how far the box reaches below and above the pitch in it
        const double thin = juce::jlimit (0.0, 1.0, harm.thinning);
        auto centre = [this, B] (int n) { const double s = stretchOf (B, n); return s > 0.0 ? harmFund * (double) n * s : -1.0; };
        for (int n = 1; n <= juce::jmax (k, harm.count); ++n)
        {
            const double c = centre (n);
            if (c <= 0.0) break;
            if (c >= maxHz()) break;                                // the top of the picture: no more
            if (n == k) continue;                                   // (the box itself)
            const double scale = std::pow ((double) n / (double) k, 1.0 - thin);       // thinning 1: as wide in Hz as yours; 0: wider in step with the pitch
            double lo = lowHz * scale, hi = highHz * scale;
            if (const double cp = n > 1 ? centre (n - 1) : -1.0; cp > 0.0) lo = juce::jmin (lo, 0.45 * (c - cp));     // never into the room of the harmonic below ...
            if (const double cn = centre (n + 1); cn > 0.0) hi = juce::jmin (hi, 0.45 * (cn - c));                   // ... or above
            const double f0 = juce::jmax ((double) kMinHz, c - lo), f1 = juce::jmin (maxHz(), c + hi);
            if (f1 <= f0) continue;                                 // (below the bottom of the picture)
            harmBoxes.push_back (denoise::SelRect { b.t0, b.t1, f0, f1, true }); harmNums.push_back (n);
        }
    }
    /** The pitch of the strongest steady line in a box: the level of each analysis bin across it, averaged over the box's time, its peak placed between bins by
        the parabola through the three around it. The middle of the box (in pitch) when no bin stands 6 dB above the rest, or the box is narrower than three bins. */
    double measurePitch (const denoise::SelRect& b) const
    {
        const double middle = std::sqrt (b.f0 * b.f1);
        if (spec.frames <= 1 || dT1 <= dT0 || spec.bins < 4) return middle;
        const double ta = juce::jmax (b.t0, dT0), tb = juce::jmin (b.t1, dT1);
        if (tb <= ta) return middle;
        auto frameOf = [this] (double t) { return juce::jlimit (0, spec.frames - 1, (int) std::lround ((t - dT0) / (dT1 - dT0) * (double) (spec.frames - 1))); };
        const int fa = frameOf (ta), fb = juce::jmax (fa, frameOf (tb)), stepF = juce::jmax (1, (fb - fa + 1) / 160);
        const bool low = spec.lowBins >= 2 && spec.lowBinHz > 0.0 && middle < std::sqrt (spec.crossLo * spec.crossHi);     // the long (bass) analysis below the cross-over
        const double binHz = low ? spec.lowBinHz : spec.sampleRate / spec.fftSize;
        const int nb = low ? spec.lowBins : spec.bins;
        const int b0 = juce::jmax (1, (int) std::ceil (b.f0 / binHz)), b1 = juce::jmin (nb - 2, (int) std::floor (b.f1 / binHz));
        if (b1 - b0 < 2) return middle;
        std::vector<double> db ((size_t) (b1 - b0 + 3), 0.0);                         // bins b0 - 1 .. b1 + 1
        for (int bin = b0 - 1; bin <= b1 + 1; ++bin)
        {
            double p = 0.0; int n = 0;
            for (int fr = fa; fr <= fb; fr += stepF)
            {
                const float v = low ? spec.lowDb[(size_t) fr * (size_t) spec.lowBins + (size_t) bin] : spec.at (fr, bin);
                p += std::pow (10.0, (double) v / 10.0); ++n;
            }
            db[(size_t) (bin - b0 + 1)] = 10.0 * std::log10 (juce::jmax (1.0e-30, p / (double) juce::jmax (1, n)));
        }
        int pk = b0;
        for (int bin = b0; bin <= b1; ++bin) if (db[(size_t) (bin - b0 + 1)] > db[(size_t) (pk - b0 + 1)]) pk = bin;
        std::vector<double> inside (db.begin() + 1, db.end() - 1);
        std::nth_element (inside.begin(), inside.begin() + (long) inside.size() / 2, inside.end());
        if (db[(size_t) (pk - b0 + 1)] - inside[inside.size() / 2] < 6.0) return middle;      // nothing stands out: no steady line to measure
        const double l = db[(size_t) (pk - b0)], c = db[(size_t) (pk - b0 + 1)], r = db[(size_t) (pk - b0 + 2)];
        const double den = l - 2.0 * c + r;
        const double d = den < -1.0e-9 ? juce::jlimit (-0.5, 0.5, 0.5 * (l - r) / den) : 0.0;
        return juce::jlimit (b.f0, b.f1, ((double) pk + d) * binHz);
    }
    // ---- 3.33.0: the selection tools, the brush strokes and the soft shape they make
    Tool tool = Tool::rect;
    int brushPx = 18;
    juce::Point<int> downAt; bool moved = false, freshOnDrag = false;  // where the button went down; whether it has become a drag; a plain drag starts afresh
    /** One circle of a brush stroke: its middle (seconds, log2 Hz) and how far it reaches (seconds; octaves up and down - the picture's pitch scale is not even). */
    struct Dab { double t = 0.0, lf = 0.0, rt = 0.0, up = 0.0, down = 0.0; };
    struct Stroke { std::vector<Dab> dabs; bool add = true; int seq = 0; };
    std::vector<Stroke> strokes;
    std::vector<TimeMark> timeMarks;                            // 3.75.0 (setTimeMarks)
    std::vector<int> boxSeq;                                    // the order each box was drawn in, beside the strokes' (a later take-out takes from what came before)
    int nextSeq = 1;
    std::vector<juce::Point<int>> paintPts;                     // the stroke being painted, on the screen (drawn as it goes; the shape is made when the button comes up)
    std::vector<Part> softParts;                                // the soft shape's parts (when there are brush strokes)
    int softVersion = 0; bool softStale = false;
    /** The soft shape on a grid over time and pitch (log2 Hz): which cells are selected, and which part each belongs to (0: none). */
    struct SoftGrid { double t0 = 0.0, dt = 1.0, lf0 = 0.0, dl = 1.0; int nt = 0, nf = 0; std::vector<uint8_t> hard; std::vector<float> soft; } sg;
    double featherT = 0.0, featherP = 0.0;                      // 3.34.0: the feathering, in seconds and in octaves
    /** The lower envelope of parabolas: d[q] = min over p of w (q - p)^2 + f[p] (Felzenszwalb and Huttenlocher), over n values a 'stride' apart; f >= kFar is "none". */
    static constexpr double kFar = 1.0e20;
    static void distance1d (double* f, int n, int stride, double w, std::vector<int>& v, std::vector<double>& z, std::vector<double>& out)
    {
        int k = -1;
        for (int q = 0; q < n; ++q)
        {
            const double fq = f[(size_t) q * (size_t) stride];
            if (fq >= kFar) continue;
            if (k < 0) { k = 0; v[0] = q; z[0] = -1.0e300; z[1] = 1.0e300; continue; }
            double sq = 0.0;
            while (true)
            {
                const int p = v[(size_t) k];
                const double fp = f[(size_t) p * (size_t) stride];
                sq = ((fq + w * (double) q * (double) q) - (fp + w * (double) p * (double) p)) / (2.0 * w * (double) (q - p));
                if (sq <= z[(size_t) k] && k > 0) --k; else break;
            }
            if (sq <= z[(size_t) k]) { v[(size_t) k] = q; z[(size_t) k + 1] = 1.0e300; continue; }     // (k = 0: this one is lower everywhere)
            ++k; v[(size_t) k] = q; z[(size_t) k] = sq; z[(size_t) k + 1] = 1.0e300;
        }
        if (k < 0) return;                                          // (nothing in this line: it stays "none")
        int j = 0;
        for (int q = 0; q < n; ++q)
        {
            while (z[(size_t) j + 1] < (double) q) ++j;
            const int p = v[(size_t) j];
            out[(size_t) q] = w * (double) (q - p) * (double) (q - p) + f[(size_t) p * (size_t) stride];
        }
        for (int q = 0; q < n; ++q) f[(size_t) q * (size_t) stride] = out[(size_t) q];
    }
    juce::Image softImage; int softImageVersion = -1; juce::Rectangle<int> softImageArea; std::array<double, 6> softImageView {};
    bool hasStrokes() const { for (auto& st : strokes) if (! st.dabs.empty()) return true; return false; }
    bool hasAddStrokes() const { for (auto& st : strokes) if (st.add && ! st.dabs.empty()) return true; return false; }
    double nyq() const { return spec.sampleRate * 0.5; }
    bool isTimeBox (const denoise::SelRect& r) const { return r.f0 <= 0.0 && r.f1 >= nyq() * 0.999; }
    void clearMarks() { boxes.clear(); boxSeq.clear(); strokes.clear(); paintPts.clear(); dropHarmonics(); softParts.clear(); sg = SoftGrid(); ++softVersion; }
    struct Ext { double t0 = 0.0, t1 = 0.0, f0 = 0.0, f1 = 0.0; };
    Ext extent() const
    {
        Ext e { 1.0e30, -1.0e30, 1.0e30, -1.0e30 }; bool any = false;
        for (auto& r : rects) { any = true; e.t0 = juce::jmin (e.t0, r.t0); e.t1 = juce::jmax (e.t1, r.t1); e.f0 = juce::jmin (e.f0, r.f0); e.f1 = juce::jmax (e.f1, r.f1); }
        for (auto& st : strokes)
            if (st.add)
                for (auto& d : st.dabs)
                {
                    any = true;
                    e.t0 = juce::jmin (e.t0, d.t - d.rt); e.t1 = juce::jmax (e.t1, d.t + d.rt);
                    e.f0 = juce::jmin (e.f0, std::exp2 (d.lf - d.down)); e.f1 = juce::jmax (e.f1, std::exp2 (d.lf + d.up));
                }
        if (! any) return {};
        e.t0 = juce::jmax (0.0, e.t0); e.t1 = juce::jmin (duration, e.t1);
        return e;
    }
    /** A circle of the brush at p (the stroke being painted): its size is the brush's on the screen, so it reaches further in time when zoomed out, and as far up
        and down as the pitch scale shows there. The gap from the last one is filled, so a quick stroke has no holes. */
    void addDab (juce::Point<int> p)
    {
        if (strokes.empty()) return;
        auto& st = strokes.back();
        const auto pl = plotArea();
        p = { juce::jlimit (pl.getX(), pl.getRight() - 1, p.x), juce::jlimit (pl.getY(), pl.getBottom() - 1, p.y) };
        const auto c = toTF (p);
        const double hz = juce::jmax (1.0, c.y);
        Dab d;
        d.t = c.x; d.lf = std::log2 (hz);
        d.rt = (double) brushPx / (double) juce::jmax (1, pl.getWidth()) * (vt1 - vt0);
        d.up = juce::jmax (1.0e-3, std::log2 (juce::jmax (1.0, toTF ({ p.x, p.y - brushPx }).y) / hz));
        d.down = juce::jmax (1.0e-3, std::log2 (hz / juce::jmax (1.0, toTF ({ p.x, p.y + brushPx }).y)));
        if (p.y - brushPx < pl.getY()) d.up = juce::jmax (d.up, d.down);          // (at the top or bottom of the picture: as far as on the other side)
        if (p.y + brushPx >= pl.getBottom()) d.down = juce::jmax (d.down, d.up);
        if (! st.dabs.empty())
        {
            const auto q = st.dabs.back();
            const double gap = juce::jmax (std::abs (d.t - q.t) / juce::jmax (1.0e-9, d.rt), std::abs (d.lf - q.lf) / juce::jmax (1.0e-6, juce::jmin (d.up, d.down)));
            const int n = (int) std::ceil (gap / 0.3);
            for (int i = 1; i < n && n < 2000; ++i)
            {
                const double f = (double) i / (double) n;
                st.dabs.push_back ({ q.t + f * (d.t - q.t), q.lf + f * (d.lf - q.lf), q.rt + f * (d.rt - q.rt), q.up + f * (d.up - q.up), q.down + f * (d.down - q.down) });
            }
        }
        st.dabs.push_back (d);
        paintPts.push_back (p);
        repaint (juce::Rectangle<int> (p.x - brushPx - 2, p.y - brushPx - 2, 2 * brushPx + 4, 2 * brushPx + 4).getUnion (paintPts.size() > 1
                     ? juce::Rectangle<int> (paintPts[paintPts.size() - 2].x - brushPx - 2, paintPts[paintPts.size() - 2].y - brushPx - 2, 2 * brushPx + 4, 2 * brushPx + 4) : juce::Rectangle<int>()));
    }
    /** The brush's shape: the boxes (the harmonics with theirs) and the strokes, each added or taken out in the order it was drawn, on a grid over time and
        pitch; then each piece of it that stands on its own is one part - its box and its mask (the cells of that piece 1, everything else 0). */
    void buildSoft()
    {
        softParts.clear(); sg = SoftGrid();
        const auto ex = extent();
        if (ex.t1 <= ex.t0) return;
        const double lfBottom = std::log2 (16.0), lfTop = std::log2 (juce::jmax (32.0, nyq()));
        const double ta = juce::jmax (-featherT, ex.t0 - featherT), tb = juce::jmin (duration + featherT, ex.t1 + featherT);       // (3.34.0: room for the feathering)
        const double la = juce::jmax (lfBottom, (ex.f0 > 16.0 ? std::log2 (ex.f0) : lfBottom) - featherP), lb = juce::jmin (lfTop, std::log2 (juce::jmax (17.0, ex.f1)) + featherP);
        if (tb <= ta || lb <= la) return;
        const bool atBottom = la <= lfBottom + 1.0e-9, atTop = lb >= lfTop - 1.0e-9;
        const int ct = juce::jlimit (8, 1600, (int) std::ceil ((tb - ta) / 0.004)), cf = juce::jlimit (8, 900, (int) std::ceil ((lb - la) * 64.0));
        sg.dt = (tb - ta) / ct; sg.dl = (lb - la) / cf;
        sg.nt = ct + 4; sg.nf = cf + 4;                                         // (two empty cells round it, so a part's mask is 0 at its edges)
        sg.t0 = ta - 2.0 * sg.dt; sg.lf0 = la - 2.0 * sg.dl;
        sg.hard.assign ((size_t) sg.nt * (size_t) sg.nf, 0);
        const int nt = sg.nt, nf = sg.nf;
        auto cellT = [this] (int i) { return sg.t0 + ((double) i + 0.5) * sg.dt; };
        auto cellL = [this] (int j) { return sg.lf0 + ((double) j + 0.5) * sg.dl; };
        auto span = [] (double lo, double hi, double base, double step, int n, int& i0, int& i1)
        {   // the cells whose middles lie in [lo, hi] - at least the one under the middle, for a box thinner than a cell
            i0 = (int) std::ceil ((lo - base) / step - 0.5); i1 = (int) std::floor ((hi - base) / step - 0.5);
            if (i1 < i0) i0 = i1 = (int) std::floor ((0.5 * (lo + hi) - base) / step);
            i0 = juce::jmax (0, i0); i1 = juce::jmin (n - 1, i1);
        };
        auto paintBox = [&] (const denoise::SelRect& r)
        {
            int i0, i1, j0, j1;
            span (r.t0, r.t1, sg.t0, sg.dt, nt, i0, i1);
            if (isTimeBox (r)) { j0 = 0; j1 = nf - 1; }
            else span (std::log2 (juce::jmax (1.0, r.f0)), std::log2 (juce::jmax (1.0, r.f1)), sg.lf0, sg.dl, nf, j0, j1);
            for (int i = i0; i <= i1; ++i) for (int j = j0; j <= j1; ++j) sg.hard[(size_t) i * (size_t) nf + (size_t) j] = r.add ? 1 : 0;
        };
        auto paintStroke = [&] (const Stroke& st)
        {
            for (auto& d : st.dabs)
            {
                int i0, i1, j0, j1;
                span (d.t - d.rt, d.t + d.rt, sg.t0, sg.dt, nt, i0, i1);
                span (d.lf - d.down, d.lf + d.up, sg.lf0, sg.dl, nf, j0, j1);
                for (int i = i0; i <= i1; ++i)
                {
                    const double x = (cellT (i) - d.t) / juce::jmax (1.0e-9, d.rt);
                    for (int j = j0; j <= j1; ++j)
                    {
                        const double dl = cellL (j) - d.lf, y = dl / (dl >= 0.0 ? d.up : d.down);
                        if (x * x + y * y <= 1.0) sg.hard[(size_t) i * (size_t) nf + (size_t) j] = st.add ? 1 : 0;
                    }
                }
            }
        };
        // in the order they were drawn (the harmonics straight after their box)
        std::vector<std::pair<int, int>> order;                                 // (seq, index: boxes from 0, strokes from 1000000)
        for (size_t i = 0; i < boxes.size(); ++i) order.push_back ({ i < boxSeq.size() ? boxSeq[i] : 0, (int) i });
        for (size_t k = 0; k < strokes.size(); ++k) order.push_back ({ strokes[k].seq, 1000000 + (int) k });
        std::stable_sort (order.begin(), order.end(), [] (const auto& x, const auto& y) { return x.first < y.first; });
        for (auto& o : order)
        {
            if (o.second >= 1000000) { paintStroke (strokes[(size_t) (o.second - 1000000)]); continue; }
            paintBox (boxes[(size_t) o.second]);
            if (o.second == harmBase) for (auto& hb : harmBoxes) paintBox (hb);
        }
        // 3.34.0: the feathering - how far each cell is from the selection, in feathers (time and pitch each counted in their own feather), and from that how
        // much of the fix it gets: all of it inside, falling away as a raised cosine to nothing one feather out
        sg.soft.assign ((size_t) nt * (size_t) nf, 0.0f);
        if (! feathered()) { for (size_t c = 0; c < sg.hard.size(); ++c) sg.soft[c] = sg.hard[c] ? 1.0f : 0.0f; }
        else
        {
            std::vector<double> d2 ((size_t) nt * (size_t) nf);
            for (size_t c = 0; c < d2.size(); ++c) d2[c] = sg.hard[c] ? 0.0 : kFar;
            std::vector<int> v ((size_t) juce::jmax (nt, nf) + 1); std::vector<double> z ((size_t) juce::jmax (nt, nf) + 2), out ((size_t) juce::jmax (nt, nf));
            if (featherT > 0.0) { const double w = (sg.dt / featherT) * (sg.dt / featherT); for (int j = 0; j < nf; ++j) distance1d (d2.data() + j, nt, nf, w, v, z, out); }
            if (featherP > 0.0) { const double w = (sg.dl / featherP) * (sg.dl / featherP); for (int i = 0; i < nt; ++i) distance1d (d2.data() + (size_t) i * (size_t) nf, nf, 1, w, v, z, out); }
            for (size_t c = 0; c < d2.size(); ++c)
            {
                if (sg.hard[c]) { sg.soft[c] = 1.0f; continue; }
                if (d2[c] >= 1.0) continue;
                sg.soft[c] = (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * std::sqrt (d2[c])));
            }
        }
        // the pieces that stand on their own: each one a part
        std::vector<int> label ((size_t) nt * (size_t) nf, 0);
        std::vector<int> stack;
        int parts = 0;
        for (int i0 = 0; i0 < nt; ++i0)
            for (int j0 = 0; j0 < nf; ++j0)
            {
                const size_t c0 = (size_t) i0 * (size_t) nf + (size_t) j0;
                if (sg.soft[c0] <= 1.0e-3f || label[c0] != 0) continue;
                ++parts;
                int lo_i = i0, hi_i = i0, lo_j = j0, hi_j = j0;
                stack.assign (1, (int) c0); label[c0] = parts;
                while (! stack.empty())
                {
                    const int c = stack.back(); stack.pop_back();
                    const int i = c / nf, j = c % nf;
                    lo_i = juce::jmin (lo_i, i); hi_i = juce::jmax (hi_i, i); lo_j = juce::jmin (lo_j, j); hi_j = juce::jmax (hi_j, j);
                    const int nb[4][2] = { { i - 1, j }, { i + 1, j }, { i, j - 1 }, { i, j + 1 } };
                    for (auto& q : nb)
                    {
                        if (q[0] < 0 || q[0] >= nt || q[1] < 0 || q[1] >= nf) continue;
                        const size_t cq = (size_t) q[0] * (size_t) nf + (size_t) q[1];
                        if (sg.soft[cq] > 1.0e-3f && label[cq] == 0) { label[cq] = parts; stack.push_back ((int) cq); }
                    }
                }
                // its mask: one empty cell round it (0 at its edges), the cells of this piece 1
                const int a0 = juce::jmax (0, lo_i - 1), a1 = juce::jmin (nt - 1, hi_i + 1), b0 = juce::jmax (0, lo_j - 1), b1 = juce::jmin (nf - 1, hi_j + 1);
                auto m = std::make_shared<SoftMask>();
                m->nt = a1 - a0 + 1; m->nf = b1 - b0 + 1;
                if (m->nt < 2 || m->nf < 2) continue;
                m->seconds = (double) (m->nt - 1) * sg.dt; m->lf0 = cellL (b0); m->lf1 = cellL (b1);
                m->v.assign ((size_t) m->nt * (size_t) m->nf, 0.0f);
                for (int i = a0; i <= a1; ++i) for (int j = b0; j <= b1; ++j) if (label[(size_t) i * (size_t) nf + (size_t) j] == parts) m->v[(size_t) (i - a0) * (size_t) m->nf + (size_t) (j - b0)] = sg.soft[(size_t) i * (size_t) nf + (size_t) j];
                Part pt;
                const double T0 = cellT (a0);
                pt.t0 = juce::jmax (0.0, T0); pt.lead = pt.t0 - T0; pt.t1 = juce::jmin (duration, T0 + m->seconds);
                pt.f0 = lo_j <= 2 && atBottom ? 0.0 : std::exp2 (m->lf0);                    // (down to the bottom of the picture: every pitch below it too)
                pt.f1 = hi_j >= nf - 3 && atTop ? nyq() : juce::jmin (nyq(), std::exp2 (m->lf1));
                if (pt.t1 - pt.t0 < 1.0e-4 || pt.f1 <= pt.f0) continue;
                pt.mask = m;
                softParts.push_back (pt);
            }
        ++softVersion;
    }
    juce::Point<int> hover { -1, -1 };                          // 3.28.0: where the pointer is over the picture, for the read-out (-1: not over it)
    double refA = 440.0;

    /** The pointer moved to p (or left: -1, -1): the read-out is drawn again where it was and where it is now. */
    void setHover (juce::Point<int> p)
    {
        if (! plotArea().contains (p)) p = { -1, -1 };
        if (p == hover) return;
        const auto was = hover;
        hover = p;
        const auto pl = plotArea();
        if (tool == Tool::brush)                                                  // (3.33.0: the brush's circle, where it was and where it is)
            for (auto q : { was, p }) if (q.x >= 0) repaint (q.x - brushPx - 3, q.y - brushPx - 3, 2 * brushPx + 6, 2 * brushPx + 6);
        repaint (0, 0, pl.getX(), getHeight());                                   // the tag on the pitch scale
        repaint (0, pl.getBottom(), getWidth(), getHeight() - pl.getBottom());    // the line under the picture
    }
    /** 3.28.0: the frequency (and the time) under the pointer: a tag on the pitch scale at the pointer's height, and a line under the picture with the
        exact frequency, the nearest note at the project's reference A and the time. Drawn only while the pointer is over the picture. */
    void paintHover (juce::Graphics& g)
    {
        const auto pl = plotArea();
        if (hover.x < 0 || ! pl.contains (hover) || spec.frames <= 0) return;
        const auto tf = toTF (hover);
        const double hz = tf.y;
        const juce::String hzText = juce::String ((int) std::lround (hz));
        auto tag = juce::Rectangle<int> (1, hover.y - 8, pl.getX() - 2, 16).constrainedWithin ({ 0, pl.getY(), pl.getX(), pl.getHeight() });
        g.setColour (juce::Colour (0xffffd24a)); g.fillRoundedRectangle (tag.toFloat(), 3.0f);
        g.setColour (juce::Colours::black); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (hzText, tag.reduced (2, 0), juce::Justification::centredRight, false);
        const auto noteText = tuning::nearestNote (hz, refA);
        const juce::String line = juce::String (hz, hz < 100.0 ? 1 : 0) + " Hz" + (noteText.isNotEmpty() ? "   " + noteText : juce::String()) + "   " + specTime (tf.x, 3);
        g.setColour (juce::Colours::white.withAlpha (0.9f)); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (line, juce::Rectangle<int> (pl.getRight() - 360, pl.getBottom() + 4, 360, getHeight() - pl.getBottom() - 6), juce::Justification::centredRight, false);
    }
    int grabDx = 0, grabDy = 0;                                 // while resizing: how far the pointer was from the edge it took hold of (pixels)
    struct Stop { float t, r, g, b; };
    struct Preset { const char* name; int palette; float lo, hi, gamma; };
    static const Preset& presetAt (int i)
    {
        static const Preset p[kPresets] = {
            { "Standard",                 0, -100.0f, -15.0f, 1.0f },
            { "Strong sounds only",       0,  -80.0f, -20.0f, 1.0f },
            { "Show quiet sounds",        0, -115.0f, -45.0f, 1.0f },
            { "Show very quiet noise",    2, -125.0f, -65.0f, 0.8f },
            { "Grey",                     1, -100.0f, -15.0f, 1.0f },
            { "Grey, quiet sounds",       1, -120.0f, -50.0f, 0.7f },
            { "Rainbow",                  3, -100.0f, -20.0f, 1.0f },
            { "Rainbow, quiet sounds",    3, -125.0f, -60.0f, 0.85f } };
        return p[juce::jlimit (0, kPresets - 1, i)];
    }
    static juce::Colour ramp (const Stop* s, int n, float t)
    {
        for (int i = 1; i < n; ++i)
            if (t <= s[i].t) { const float f = (t - s[i - 1].t) / (s[i].t - s[i - 1].t); return juce::Colour::fromFloatRGBA (s[i - 1].r + f * (s[i].r - s[i - 1].r), s[i - 1].g + f * (s[i].g - s[i - 1].g), s[i - 1].b + f * (s[i].b - s[i - 1].b), 1.0f); }
        return juce::Colour::fromFloatRGBA (s[n - 1].r, s[n - 1].g, s[n - 1].b, 1.0f);
    }
    /** The look in use: the two neighbouring presets blended by how far the knob is between them, so it changes continuously. */
    Preset blended (const Preset*& pa, const Preset*& pb, float& f) const
    {
        const int i0 = (int) std::floor (rollPos) % kPresets, i1 = (i0 + 1) % kPresets;
        f = (float) (rollPos - std::floor (rollPos));
        pa = &presetAt (i0); pb = &presetAt (i1);
        return Preset { pa->name, pa->palette, pa->lo + f * (pb->lo - pa->lo), pa->hi + f * (pb->hi - pa->hi), pa->gamma + f * (pb->gamma - pa->gamma) };
    }
    static juce::Colour paletteAt (int pal, float t)
    {
        static const Stop heat[] = { { 0.0f, 0, 0, 0.05f }, { 0.25f, 0.2f, 0.0f, 0.4f }, { 0.5f, 0.75f, 0.1f, 0.35f }, { 0.75f, 1.0f, 0.55f, 0.1f }, { 1.0f, 1.0f, 1.0f, 0.8f } };
        static const Stop grey[] = { { 0.0f, 0, 0, 0 }, { 1.0f, 1, 1, 1 } };
        static const Stop ice[]  = { { 0.0f, 0, 0, 0.05f }, { 0.35f, 0, 0.15f, 0.6f }, { 0.7f, 0, 0.75f, 0.95f }, { 1.0f, 1, 1, 1 } };
        static const Stop cyanOrange[] = { { 0.0f, 0.0f, 0.0f, 0.04f }, { 0.18f, 0.02f, 0.10f, 0.32f }, { 0.36f, 0.0f, 0.38f, 0.62f }, { 0.52f, 0.0f, 0.80f, 0.85f }, { 0.64f, 0.55f, 0.92f, 0.70f }, { 0.78f, 1.0f, 0.82f, 0.25f }, { 0.90f, 1.0f, 0.52f, 0.05f }, { 1.0f, 1.0f, 0.95f, 0.85f } };
        static const Stop rain[] = { { 0.0f, 0, 0, 0.15f }, { 0.2f, 0.15f, 0, 0.7f }, { 0.4f, 0, 0.6f, 1.0f }, { 0.6f, 0.1f, 0.9f, 0.3f }, { 0.8f, 1.0f, 0.9f, 0.0f }, { 1.0f, 1.0f, 0.15f, 0.1f } };
        return pal == 0 ? ramp (heat, 5, t) : pal == 1 ? ramp (grey, 2, t) : pal == 2 ? ramp (ice, 4, t) : pal == 4 ? ramp (cyanOrange, 8, t) : ramp (rain, 6, t);
    }
    void buildLut()
    {
        const Preset *pa, *pb; float f; blended (pa, pb, f);
        for (int i = 0; i < 256; ++i)
        {
            const float u = (float) i / 255.0f;
            const auto ca = paletteAt (pa->palette, std::pow (u, pa->gamma)), cb = paletteAt (pb->palette, std::pow (u, pb->gamma));
            lut[(size_t) i] = ca.interpolatedWith (cb, f);
            static const int paletteIds[] = { 4, 0, 1, 2, 3 };                          // the sharp picture uses the palette chosen in the spectrogram settings, with the roll's gamma
            lutD[(size_t) i] = paletteAt (paletteIds[juce::jlimit (0, 4, ss.palette)], std::pow (u, pa->gamma + f * (pb->gamma - pa->gamma)));
        }
    }
    /** The two ends of the colours (dB) for the sharp picture: the settings, shifted by the colour roll's look. */
    void detailRange (float& lo, float& hi) const
    {
        const Preset *pa, *pb; float f; const Preset P = blended (pa, pb, f);
        lo = (float) ss.lo + (P.lo + 100.0f); hi = (float) ss.hi + (P.hi + 15.0f);
        if (hi - lo < 6.0f) hi = lo + 6.0f;
    }
    PictureAxis axisNow() const { PictureAxis ax; ax.fLo = fLo; ax.fHi = fHi; ax.warp = warp; ax.mel = mel; return ax; }
    void scheduleDetail() { if (ss.sharp && spec.frames > 0) debounce.startTimer (260); }
    void requestDetailNow()
    {
        if (! ss.sharp || spec.frames <= 0 || ! onNeedDetail) return;
        const auto pl = plotArea();
        if (pl.getWidth() < 50 || pl.getHeight() < 50) return;
        PictureRequest rq; rq.t0 = vt0; rq.t1 = vt1; rq.w = pl.getWidth(); rq.h = pl.getHeight(); rq.axis = axisNow();
        rq.opt.reassign = ss.reassign; rq.opt.detail = ss.detail; rq.opt.reduceAboveSec = ss.reduceAbove;
        const bool optSame = detailOpt.reassign == rq.opt.reassign && std::abs (detailOpt.detail - rq.opt.detail) < 1.0e-9 && std::abs (detailOpt.reduceAboveSec - rq.opt.reduceAboveSec) < 1.0e-9;
        if (detail.valid() && ! detailStale && optSame && detail.w == rq.w && detail.h == rq.h && std::abs (detail.t0 - rq.t0) < 1.0e-9 && std::abs (detail.t1 - rq.t1) < 1.0e-9 && detail.axis.same (rq.axis))
        {
            if (staleC1 <= staleC0) return;                                        // (all there)
            rq.c0 = staleC0; rq.c1 = staleC1;                                      // 3.38.0: only the columns out of date
        }
        rq.id = ++detailSeq; reqGen = detailGen; lastOpt = rq.opt;
        onNeedDetail (rq);
    }
    struct Debounce : juce::Timer { SpectrogramView* owner = nullptr; void timerCallback() override { stopTimer(); if (owner != nullptr) owner->requestDetailNow(); } } debounce;
    double maxHz() const { return juce::jmin (22000.0, spec.sampleRate * 0.5); }
    juce::Rectangle<int> plotArea() const { return getLocalBounds().withTrimmedLeft (46).withTrimmedTop (22).withTrimmedBottom (30); }
    juce::Rectangle<int> rulerArea() const { auto pl = plotArea(); return { pl.getX(), 0, pl.getWidth(), pl.getY() }; }
    juce::Rectangle<int> knobArea() const { return { 8, getHeight() - 28, 24, 24 }; }
    void note() { if (onNote) onNote ("Showing " + specTime (vt0, 2) + " - " + specTime (vt1, 2) + ".  Right / left arrows zoom in / out in time, down / up in pitch; click the ruler to place the playhead, drag it to move; double-click the ruler to see it all."); }
    void setWindow (double t0, double span)
    {
        span = juce::jlimit (juce::jmin (0.05, duration), duration, span);
        t0 = juce::jlimit (0.0, juce::jmax (0.0, duration - span), t0);
        const bool covered = detailCoversView(); const double o0 = vt0, o1 = vt1;
        vt0 = t0; vt1 = t0 + span;
        ++detailGen;                                                               // (a picture being made is for the view before)
        if (covered) carryDetail (o0, o1);
        dirty = true; repaint(); scheduleDetail(); if (onView) onView();
    }
    /** 3.38.0: the view moved along without changing its span (a drag, a scroll, the playhead's page): the sharp picture's columns the two views share move
        with it - exactly as they would be made again (picture::firstColumn) - and only the strip it uncovers is made. Zoomed: it is made again. */
    void carryDetail (double o0, double o1)
    {
        const double span = vt1 - vt0, oldSpan = o1 - o0;
        if (std::abs (span - oldSpan) > 1.0e-9 * oldSpan) return;
        const int W = detail.w, H = detail.h;
        const juce::int64 k = picture::firstColumn (vt0, vt1, W) - picture::firstColumn (o0, o1, W);
        if (k <= -W || k >= W) return;                                             // (nothing shared: made again)
        if (k != 0)
        {
            const int n = W - (int) std::abs (k);
            for (int r = 0; r < H; ++r)
            {
                float* row = detail.db.data() + (size_t) r * (size_t) W;
                if (k > 0) std::copy (row + k, row + W, row);
                else       std::copy_backward (row, row + n, row + W);
            }
            int s0 = staleC0, s1 = staleC1;
            if (s1 > s0) { s0 = juce::jlimit (0, W, s0 - (int) k); s1 = juce::jlimit (0, W, s1 - (int) k); }
            staleC0 = s0; staleC1 = s1;
            if (k > 0) markStale (n, W); else markStale (0, (int) -k);
        }
        detail.t0 = vt0; detail.t1 = vt1;
    }
    void markStale (int a, int b)
    {
        if (b <= a) return;
        if (staleC1 <= staleC0) { staleC0 = a; staleC1 = b; } else { staleC0 = juce::jmin (staleC0, a); staleC1 = juce::jmax (staleC1, b); }
    }
    void setFreqWindow (double logLo, double logSpan)
    {
        const double lo = std::log (kMinHz), hi = std::log (maxHz());
        logSpan = juce::jlimit (std::log (1.6), hi - lo, logSpan);
        logLo = juce::jlimit (lo, hi - logSpan, logLo);
        fLo = std::exp (logLo); fHi = std::exp (logLo + logSpan); dirty = true; repaint(); scheduleDetail();
    }
    // pitch <-> height: the Mel scale, or a blend of the straight line (even Hz) and the logarithm (even octaves)
    double posOf (double hz) const { return axisNow().posOf (hz); }
    double hzOf (double pos) const { return axisNow().hzOf (pos); }
    int tToX (double t, juce::Rectangle<int> pl) const { return pl.getX() + (int) std::lround ((t - vt0) / juce::jmax (1.0e-9, vt1 - vt0) * pl.getWidth()); }
    int hzToY (double hz, juce::Rectangle<int> pl) const { return pl.getY() + (int) std::lround ((1.0 - posOf (hz)) * (pl.getHeight() - 1)); }
    juce::Point<double> toTF (juce::Point<int> p) const
    {
        auto pl = plotArea();
        const double t = juce::jlimit (0.0, duration, vt0 + (double) (p.x - pl.getX()) / juce::jmax (1, pl.getWidth()) * (vt1 - vt0));
        const int yy = juce::jlimit (0, pl.getHeight() - 1, p.y - pl.getY());
        return { t, hzOf (1.0 - (double) yy / (double) juce::jmax (1, pl.getHeight() - 1)) };
    }
    /** Which edges of a box the point is on (1 left, 2 right, 4 top, 8 bottom, or two of them at a corner); 0 = not on any box's border. 'which' (may be null)
        gets the box (the last one drawn first; the boxes added, not those taken out). */
    int edgesAt (juce::Point<int> p, int* which = nullptr) const
    {
        if (! hasSel || tool == Tool::brush) return 0;
        for (int i = (int) boxes.size(); i-- > 0;)
        {
            if (! boxes[(size_t) i].add) continue;
            const auto r = rectOf (boxes[(size_t) i]).getIntersection (plotArea().expanded (1));      // (the rules - sides belong to one axis, only a real corner takes both - are in core/BoxEdges.h, with a test)
            int ed = boxedit::edgesAt ({ r.getX(), r.getY(), r.getWidth(), r.getHeight() }, p.x, p.y);
            if (isTimeBox (boxes[(size_t) i])) ed &= 3;                        // (3.33.0: a Time box has every pitch: only its two sides in time)
            if (ed != 0) { if (which != nullptr) *which = i; return ed; }
        }
        return 0;
    }
    /** The picture of what is in view, made at the size it is shown (so zooming in shows more detail, within what was scanned). */
    void rebuildImage (juce::Rectangle<int> pl)
    {
        dirty = false;
        const int W = pl.getWidth(), H = pl.getHeight();
        if (spec.frames <= 0 || W < 2 || H < 2) { image = juce::Image(); return; }
        image = juce::Image (juce::Image::RGB, W, H, false);
        juce::Image::BitmapData bd (image, juce::Image::BitmapData::writeOnly);
        std::vector<int> rb ((size_t) H); std::vector<float> rf ((size_t) H);
        for (int y = 0; y < H; ++y)
        {
            const double hz = hzOf (1.0 - (double) y / (double) (H - 1));
            rb[(size_t) y] = 0; rf[(size_t) y] = (float) hz;                       // (rf holds the pitch of this row; spec.level() blends the two analysis layers)
        }
        const Preset *qa, *qb; float qf; const Preset P = blended (qa, qb, qf);
        const float lo = P.lo, inv = 255.0f / (P.hi - P.lo);
        for (int x = 0; x < W; ++x)
        {
            const double t = vt0 + ((double) x + 0.5) / (double) W * (vt1 - vt0);
            const double fp = juce::jlimit (0.0, (double) spec.frames - 1.0001, (t - dT0) / (dT1 - dT0) * (double) (spec.frames - 1));
            const int f0 = (int) fp; const float ff = (float) (fp - f0); const int f1 = juce::jmin (spec.frames - 1, f0 + 1);
            for (int y = 0; y < H; ++y)
            {
                const float d0 = spec.level (f0, (double) rf[(size_t) y]), d1 = spec.level (f1, (double) rf[(size_t) y]);
                const float d = d0 * (1.0f - ff) + d1 * ff;
                const int idx = juce::jlimit (0, 255, (int) ((d - lo) * inv));
                bd.setPixelColour (x, y, lut[(size_t) idx]);
            }
        }
        // the sharp picture, where it covers the view (it replaces the quick one; the quick one shows while the sharp one is being made)
        if (ss.sharp && detail.valid() && ! detailStale && std::abs (detail.t0 - vt0) < 1.0e-6 && std::abs (detail.t1 - vt1) < 1.0e-6 && detail.axis.same (axisNow()))
        {
            float dlo, dhi; detailRange (dlo, dhi); const float dinv = 255.0f / (dhi - dlo);
            const double dSpan = juce::jmax (1.0e-9, detail.t1 - detail.t0);
            std::vector<int> drow ((size_t) H, -1);
            for (int y = 0; y < H; ++y)
            {
                const double hz = rf[(size_t) y];
                if (hz >= detail.axis.fLo && hz <= detail.axis.fHi) drow[(size_t) y] = juce::jlimit (0, detail.h - 1, (int) std::lround ((1.0 - detail.axis.posOf (hz)) * (double) (detail.h - 1)));
            }
            for (int x = 0; x < W; ++x)
            {
                const double t = vt0 + ((double) x + 0.5) / (double) W * (vt1 - vt0);
                const int col = (int) std::floor ((t - detail.t0) / dSpan * (double) detail.w);
                if (col < 0 || col >= detail.w || (col >= staleC0 && col < staleC1)) continue;        // (3.38.0: out of date there - the quick picture shows)
                for (int y = 0; y < H; ++y)
                {
                    const int row = drow[(size_t) y];
                    if (row < 0) continue;
                    const float db = detail.db[(size_t) row * (size_t) detail.w + (size_t) col];
                    bd.setPixelColour (x, y, lutD[(size_t) juce::jlimit (0, 255, (int) ((db - dlo) * dinv))]);
                }
            }
        }
    }
    MergedSpectrogram spec; double duration = 1.0, dT0 = 0.0, dT1 = 1.0; juce::Image image; bool dirty = true;
    double vt0 = 0.0, vt1 = 1.0, fLo = kMinHz, fHi = 20000.0, warp = 1.0;
    int preset = 0; double rollPos = 0.0, rollStart = 0.0; Mode mode = Mode::none; double panT0 = 0.0, panF = kMinHz, panF1 = 20000.0;
    std::array<juce::Colour, 256> lut, lutD;
    SpectroSettings ss; bool mel = true; DetailPicture detail; bool detailStale = true; juce::int64 detailSeq = 0; PictureOptions detailOpt, lastOpt;
    int staleC0 = 0, staleC1 = 0;                                       // 3.38.0: the sharp picture's columns out of date (made again soon; the quick picture shows there)
    juce::int64 detailGen = 0, reqGen = -1;                             // 3.38.0: bumped at every change of the sound or the view; a picture asked for before is not taken
    bool hasSel = false; juce::Point<double> a, b; double playT = -1.0, cueT = -1.0;
    bool showSurround = false; double surroundPct = 100.0; int surroundDir = 0; double surroundAfter = 0.5;
    /** 3.52.0: how steadily this picture's playhead moved during a play, as ONE line in the log when the play ends (a play of two seconds or
        more, in a window that sets livePlayhead): the screen refreshes that came late, and the redraws that took long. With the Edit window's
        own line ("Edit window playhead: ...") it says which window held the drawing up. */
    struct PlayLog
    {
        bool active = false; double t0 = 0.0, lastT = 0.0; int frames = 0, late = 0, slowPaints = 0; double worstGap = 0.0, worstPaint = 0.0;
        void painted (double ms) { if (active && ms >= 12.0) { ++slowPaints; worstPaint = juce::jmax (worstPaint, ms); } }
    } playLog;
    void notePlay (double t)
    {
        auto& l = playLog;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (t < 0.0)
        {
            if (l.active && l.frames >= 120)
                juce::Logger::writeToLog ("Noise / ReTouch picture playhead: " + juce::String ((now - l.t0) / 1000.0, 1) + " s played, " + juce::String (l.frames) + " screen refreshes; refreshes late (over 25 ms): "
                                          + juce::String (l.late) + (l.late > 0 ? ", worst " + juce::String (l.worstGap, 0) + " ms" : juce::String()) + "; redraws over 12 ms: "
                                          + juce::String (l.slowPaints) + (l.slowPaints > 0 ? ", worst " + juce::String (l.worstPaint, 0) + " ms" : juce::String()));
            l = PlayLog();
            return;
        }
        if (! l.active) { l = PlayLog(); l.active = true; l.t0 = l.lastT = now; return; }
        const double dt = now - l.lastT; l.lastT = now; ++l.frames;
        if (dt > 25.0) { ++l.late; l.worstGap = juce::jmax (l.worstGap, dt); }
    }
    void screenRefresh()
    {
        if (! livePlayhead) return;
        const double t = livePlayhead();
        notePlay (t);
        setPlayhead (t);
    }
    juce::VBlankAttachment vblank { this, [this] { screenRefresh(); } };      // (last: everything it uses exists first)
};

/** 3.33.0: the harmonics of ReTouch, in the ReTouch window and in Noise mode: they give the box you drew boxes on the other harmonics of its sound (see
    SpectrogramView::setHarmonics). Harmonics (H) adds them or takes them away; pressed and dragged up it adds more as the mouse goes up (down: fewer); - and +
    take one away or add one; "My box is the" says which harmonic the box is on (the fundamental is the 1st); Spread and Thinning shape them. The count, the spread
    and the thinning are remembered in the program's settings. */
class HarmonicsBar : public juce::Component
{
public:
    explicit HarmonicsBar (SpectrogramView& v) : view (v)
    {
        if (gSpecPrefs != nullptr)
        {
            h.count = juce::jlimit (1, 4000, gSpecPrefs->getIntValue ("harmCount", 10));
            h.spreadCents = juce::jlimit (-100.0, 100.0, gSpecPrefs->getDoubleValue ("harmSpread", 0.0));
            h.thinning = juce::jlimit (0.0, 1.0, gSpecPrefs->getDoubleValue ("harmThin", 1.0));
        }
        harmBtn.setButtonText ("Harmonics");
        harmBtn.setColour (juce::TextButton::buttonOnColourId, theme::accent);
        harmBtn.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        harmBtn.setTooltip ("Harmonics (key H): a box on each harmonic of the note in the box you drew - the fundamental is the 1st harmonic. Click to add them, click again "
                            "to take them away. Press and drag up to add more as you go (down: fewer); they go as high as the picture. Alt + drag in the picture takes any of them out again.");
        harmBtn.onClick = [this] { if (view.harmonicsOn()) view.clearHarmonics(); else apply(); refresh(); };
        harmBtn.onPress = [this] { if (! view.harmonicsOn()) apply(); dragFrom = h.count; };
        harmBtn.onSteps = [this] (int s) { setCount (dragFrom + s); };
        for (auto* b : { &minusBtn, &plusBtn }) { b->setConnectedEdges (b == &minusBtn ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft); addAndMakeVisible (*b); }
        minusBtn.setTooltip ("One harmonic fewer."); plusBtn.setTooltip ("One harmonic more (as many as you like, up to the top of the picture).");
        minusBtn.onClick = [this] { if (! view.harmonicsOn()) apply(); setCount (h.count - 1); };
        plusBtn.onClick  = [this] { if (! view.harmonicsOn()) apply(); setCount (h.count + 1); };
        countLbl.setJustificationType (juce::Justification::centred); countLbl.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (countLbl); addAndMakeVisible (harmBtn);
        for (int i = 1; i <= 4; ++i) boxIsBox.addItem (i == 1 ? "1st (fundamental)" : i == 2 ? "2nd harmonic" : i == 3 ? "3rd harmonic" : "4th harmonic", i);
        boxIsBox.setSelectedId (1, juce::dontSendNotification);
        boxIsBox.setTooltip ("Which harmonic your box is on. If the noise can still be heard after ReTouch, try 2nd, 3rd or 4th: the fundamental and the harmonics below your box are then taken too.");
        boxIsBox.onChange = [this] { h.boxIs = juce::jmax (1, boxIsBox.getSelectedId()); h.count = juce::jmax (h.count, h.boxIs); if (view.harmonicsOn()) apply(); refresh(); };
        addAndMakeVisible (boxIsBox);
        auto cap = [this] (juce::Label& l, const juce::String& t) { l.setText (t, juce::dontSendNotification); l.setColour (juce::Label::textColourId, theme::text); addAndMakeVisible (l); };
        cap (boxIsCap, "My box is the"); cap (spreadCap, "Spread"); cap (thinCap, "Thinning");
        auto slider = [this] (juce::Slider& sl, double lo, double hi, double step, double v, const juce::String& suffix, double home)
        {
            sl.setSliderStyle (juce::Slider::LinearHorizontal); sl.setRange (lo, hi, step); sl.setValue (v, juce::dontSendNotification); sl.setScrollWheelEnabled (false);
            sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 22); sl.setTextValueSuffix (suffix); sl.setDoubleClickReturnValue (true, home);
            sl.onValueChange = [this] { h.spreadCents = spread.getValue(); h.thinning = thin.getValue() / 100.0; if (view.harmonicsOn()) apply(); refresh(); };
            addAndMakeVisible (sl);
        };
        slider (spread, -100.0, 100.0, 1.0, h.spreadCents, " c", 0.0); slider (thin, 0.0, 100.0, 1.0, h.thinning * 100.0, " %", 100.0);
        spread.textFromValueFunction = [] (double x) { return (x > 0.0 ? "+" : "") + juce::String ((int) std::lround (x)); };     // (the slider adds " c")
        spread.updateText();
        spread.setTooltip ("0: the harmonics exactly 2, 3, 4 ... times the fundamental. Up: each one a little sharper the higher it is, as a piano's upper harmonics are; "
                           "down: a little flatter. The number is how many cents the 10th harmonic moves. Double-click for 0.");
        thin.setTooltip ("How the boxes narrow as they go up. 100 % (the standard): every box as wide in Hz as yours - draw it snugly round the line, and the high "
                         "harmonics get the same few Hz either side, so they keep clear of the harmonics next to them. 0 %: as wide musically as yours (wider in Hz the higher "
                         "it is). Whatever this is, no box reaches more than 45 % of the way to the next harmonic. Double-click for 100 %.");
        readout.setColour (juce::Label::textColourId, theme::dimText); readout.setMinimumHorizontalScale (0.75f);
        addAndMakeVisible (readout);
        fit::markMayShrink (readout);                    // (3.71.0: a hint - see FitLayout.h)
        view.onHarmonics = [this] { refresh(); };
        refresh();
    }
    /** H: harmonics on or off. */
    void toggle() { harmBtn.triggerClick(); }
    /** The width that takes everything in one row (without the read-out); narrower, the windows give the bar two rows: the count and the box on the first,
        Spread and Thinning on the second. */
    static constexpr int kFirstW = 96 + 4 + 24 + 34 + 24 + 12 + 88 + 2 + 140 + 12, kSecondW = 50 + 2 + 170 + 12 + 62 + 2 + 170 + 12, kOneRowW = kFirstW + kSecondW;
    static constexpr int kReadoutMinW = 220;
    void resized() override
    {
        const bool twoRows = getHeight() >= 40;
        auto r = twoRows ? getLocalBounds().removeFromTop (getHeight() / 2 - 2) : getLocalBounds();
        auto put = [&r] (juce::Component& c, int w, int gap) { c.setBounds (r.removeFromLeft (juce::jmin (w, juce::jmax (0, r.getWidth())))); r.removeFromLeft (juce::jmin (gap, r.getWidth())); };
        put (harmBtn, 96, 4); put (minusBtn, 24, 0); put (countLbl, 34, 0); put (plusBtn, 24, 12);
        put (boxIsCap, 88, 2); put (boxIsBox, 140, 12);
        auto rest = r;
        if (twoRows) r = getLocalBounds().removeFromBottom (getHeight() / 2 - 2);
        put (spreadCap, 50, 2); put (spread, 170, 12);
        put (thinCap, 62, 2); put (thin, 170, 12);
        if (! twoRows) rest = r;
        readoutFits = rest.getWidth() >= kReadoutMinW;
        readout.setBounds (readoutFits ? rest : juce::Rectangle<int>());
        refresh();
    }
private:
    struct DragKey : juce::TextButton
    {
        std::function<void()> onPress;                            // the drag has begun (it went more than a few pixels)
        std::function<void (int)> onSteps;                        // how far it is up from where it began, in harmonics (down: below 0)
        void mouseDown (const juce::MouseEvent& e) override { dragged = false; last = 0; juce::TextButton::mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseDrag (e);
            if (! dragged && std::abs (e.getDistanceFromDragStartY()) < 5) return;
            if (! dragged) { dragged = true; if (onPress) onPress(); }
            const int s = -e.getDistanceFromDragStartY() / 8;          // one harmonic every 8 pixels
            if (s != last) { last = s; if (onSteps) onSteps (s); }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (dragged) { dragged = false; setState (juce::Button::buttonNormal); return; }      // (a drag is not a click)
            juce::TextButton::mouseUp (e);
        }
        bool dragged = false; int last = 0;
    };
    void apply()
    {
        if (! view.setHarmonics (h)) readout.setText ("Draw a box round a note first (its fundamental, or any harmonic of it), then press Harmonics.", juce::dontSendNotification);
        save();
    }
    void setCount (int n)
    {
        const int top = view.harmonicsOn() ? view.maxHarmonic (h) : n;
        h.count = juce::jlimit (juce::jmax (1, h.boxIs), juce::jmax (h.boxIs, top), n);
        if (view.harmonicsOn()) view.setHarmonics (h);
        save(); refresh();
    }
    void save()
    {
        if (gSpecPrefs == nullptr) return;
        gSpecPrefs->setValue ("harmCount", h.count); gSpecPrefs->setValue ("harmSpread", h.spreadCents); gSpecPrefs->setValue ("harmThin", h.thinning);
    }
    void refresh()
    {
        const bool on = view.harmonicsOn();
        harmBtn.setToggleState (on, juce::dontSendNotification);
        countLbl.setText (juce::String (h.count), juce::dontSendNotification);
        if (! on) { readout.setText ("Draw a box round a note, then press Harmonics (H) - or press it and drag up.", juce::dontSendNotification); countLbl.setTooltip (countTip); return; }
        const int top = juce::jmin (h.count, view.maxHarmonic (h));
        const double f = view.harmonicFundamental();
        const auto nt = tuning::nearestNote (f, view.referenceA());
        readout.setText ("Fundamental " + juce::String (f, f < 100.0 ? 1 : 0) + " Hz" + (nt.isNotEmpty() ? " (" + nt + ")" : juce::String())
                         + "   harmonics 1 - " + juce::String (top) + (top < h.count ? " (the top of the picture)" : juce::String()), juce::dontSendNotification);
        countLbl.setTooltip (readoutFits ? countTip : readout.getText());          // (no room for the read-out: it is the count's tooltip)
    }
    bool readoutFits = true;
    const juce::String countTip { "How many harmonics: 1 to this number (the fundamental is the 1st)." };
    SpectrogramView& view;
    SpectrogramView::Harmonics h;
    int dragFrom = 10;
    DragKey harmBtn;
    juce::TextButton minusBtn { "-" }, plusBtn { "+" };
    juce::Label countLbl, boxIsCap, spreadCap, thinCap, readout;
    juce::ComboBox boxIsBox;
    juce::Slider spread, thin;
};

/** 3.34.0: the feathering, in a small panel that opens from the Feather button: how far beyond the selection the fixes fade in and out - in time and in pitch. */
class FeatherPanel : public juce::Component
{
public:
    explicit FeatherPanel (SpectrogramView& v) : view (&v)
    {
        title.setText ("Feathering - every tool", juce::dontSendNotification); title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        note.setText ("The fix fades in and out over this much beyond your selection, instead of stopping at its edge. 0 = a hard edge. The picture shows how far.",
                      juce::dontSendNotification);
        note.setFont (juce::FontOptions (12.0f)); note.setJustificationType (juce::Justification::topLeft);
        for (auto* l : { &title, &note, &timeCap, &pitchCap }) { l->setColour (juce::Label::textColourId, theme::text); addAndMakeVisible (*l); }
        timeCap.setText ("In time", juce::dontSendNotification); pitchCap.setText ("In pitch", juce::dontSendNotification);
        auto setup = [this] (juce::Slider& sl, double hi, double step, double mid, const juce::String& suffix, double v)
        {
            sl.setSliderStyle (juce::Slider::LinearHorizontal); sl.setRange (0.0, hi, step); sl.setSkewFactorFromMidPoint (mid); sl.setValue (v, juce::dontSendNotification);
            sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 86, 22); sl.setTextValueSuffix (suffix); sl.setDoubleClickReturnValue (true, 0.0);
            sl.onValueChange = [this] { if (view != nullptr) view->setFeather (time.getValue(), pitch.getValue()); };
            addAndMakeVisible (sl);
        };
        setup (time, 1000.0, 1.0, 100.0, " ms", v.featherMs()); setup (pitch, 24.0, 0.5, 4.0, " semitones", v.featherSemitones());
        time.setTooltip ("How long the fix takes to fade in before the selection and out after it. Double-click for 0.");
        pitch.setTooltip ("How far below and above the selection the fix fades out. Double-click for 0.");
        offBtn.onClick = [this] { time.setValue (0.0, juce::dontSendNotification); pitch.setValue (0.0, juce::dontSendNotification); if (view != nullptr) view->setFeather (0.0, 0.0); };
        addAndMakeVisible (offBtn);
        setSize (420, 150);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto top = r.removeFromTop (20); offBtn.setBounds (top.removeFromRight (60)); title.setBounds (top);
        note.setBounds (r.removeFromTop (38)); r.removeFromTop (4);
        auto a = r.removeFromTop (24); timeCap.setBounds (a.removeFromLeft (64)); time.setBounds (a);
        r.removeFromTop (6);
        auto b = r.removeFromTop (24); pitchCap.setBounds (b.removeFromLeft (64)); pitch.setBounds (b);
    }
private:
    juce::Component::SafePointer<SpectrogramView> view;
    juce::Label title, note, timeCap, pitchCap;
    juce::Slider time, pitch;
    juce::TextButton offBtn { "Off" };
};

/** 3.33.0: the selection tools, under Spectrogram settings in the ReTouch window and in Noise mode: Rectangle (S: a box of any shape, as always), Time (T: a
    stretch of time, every pitch from the bottom to the top), Brush (B: paint the selection; [ and ] make it smaller and larger). The tool and the brush's size
    are remembered in the program's settings. */
class SelectToolsBar : public juce::Component
{
public:
    explicit SelectToolsBar (SpectrogramView& v) : view (v)
    {
        cap.setText ("Select:", juce::dontSendNotification); cap.setColour (juce::Label::textColourId, theme::text); cap.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (cap);
        const char* tips[] = { "Rectangle (key S): a box of any shape round the sound, as always. Shift + drag adds another box, Alt + drag takes a part out.",
                               "Time (key T): a stretch of time, with every pitch in it - from the bottom to the top (half the sample rate). Drag across the time you want; "
                               "Shift adds another stretch, Alt takes one out.",
                               "Brush (key B): paint the selection - whatever the circle passes over is selected. Shift paints more onto it, Alt paints parts out again. "
                               "[ and ] make the brush smaller and larger (or use Size)." };
        juce::TextButton* bs[] = { &rectBtn, &timeBtn, &brushBtn };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = bs[i];
            b->setClickingTogglesState (false); b->setTooltip (tips[i]);
            b->setColour (juce::TextButton::buttonOnColourId, theme::accent);
            b->setConnectedEdges (i == 0 ? juce::Button::ConnectedOnRight : i == 2 ? juce::Button::ConnectedOnLeft : juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
            b->onClick = [this, i] { view.setTool ((SpectrogramView::Tool) i); };
            addAndMakeVisible (*b);
        }
        sizeCap.setText ("Size", juce::dontSendNotification); sizeCap.setColour (juce::Label::textColourId, theme::text); sizeCap.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (sizeCap);
        size.setSliderStyle (juce::Slider::LinearHorizontal); size.setRange (3.0, 150.0, 1.0); size.setScrollWheelEnabled (false);
        size.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 22); size.setTextValueSuffix (" px"); size.setSkewFactorFromMidPoint (30.0);
        size.setTooltip ("The brush's size (its radius on the screen). Keys: [ smaller, ] larger.");
        size.onValueChange = [this] { view.setBrushSize ((int) size.getValue()); };
        addAndMakeVisible (size);
        // 3.34.0: feathering, for every tool (a small panel with its two amounts)
        featherBtn.setClickingTogglesState (false); featherBtn.setColour (juce::TextButton::buttonOnColourId, theme::accent);
        featherBtn.setTooltip ("Feathering: the fix fades in and out beyond your selection - in time and in pitch - instead of stopping at its edge. Click to set it.");
        featherBtn.onClick = [this]
        {
            auto panel = std::make_unique<FeatherPanel> (view);
            juce::CallOutBox::launchAsynchronously (std::move (panel), featherBtn.getScreenBounds(), nullptr);
        };
        addAndMakeVisible (featherBtn);
        if (gSpecPrefs != nullptr)
        {
            view.setBrushSize (gSpecPrefs->getIntValue ("brushPx", 18));
            view.setTool ((SpectrogramView::Tool) juce::jlimit (0, 2, gSpecPrefs->getIntValue ("selTool", 0)));
            view.setFeather (gSpecPrefs->getDoubleValue ("featherMs", 0.0), gSpecPrefs->getDoubleValue ("featherSt", 0.0));
        }
        view.onTool = [this] { refresh(); };
        refresh();
    }
    static constexpr int kWidth = 52 + 108 + 80 + 86 + 10 + 36 + 160 + 10 + 176;
    void resized() override
    {
        auto r = getLocalBounds();
        cap.setBounds (r.removeFromLeft (52)); r.removeFromLeft (4);
        rectBtn.setBounds (r.removeFromLeft (104)); timeBtn.setBounds (r.removeFromLeft (80)); brushBtn.setBounds (r.removeFromLeft (86));
        r.removeFromLeft (10);
        featherBtn.setBounds (r.removeFromRight (juce::jmin (176, r.getWidth()))); r.removeFromRight (10);       // (3.34.0: at the right, under Spectrogram settings)
        sizeCap.setBounds (r.removeFromLeft (36)); r.removeFromLeft (2);
        size.setBounds (r.removeFromLeft (juce::jmin (160, r.getWidth())));
    }
    /** S, T, B, [ and ] (no other key held). */
    bool key (const juce::KeyPress& k)
    {
        if (k.getModifiers().isAnyModifierKeyDown()) return false;
        const auto ch = k.getTextCharacter();
        if (ch == 's' || ch == 'S') { view.setTool (SpectrogramView::Tool::rect); return true; }
        if (ch == 't' || ch == 'T') { view.setTool (SpectrogramView::Tool::time); return true; }
        if (ch == 'b' || ch == 'B') { view.setTool (SpectrogramView::Tool::brush); return true; }
        if (ch == '[' || ch == ']') { view.setBrushSize (view.brushSize() + (ch == '[' ? -1 : 1) * juce::jmax (1, view.brushSize() / 6)); return true; }
        return false;
    }
private:
    void refresh()
    {
        const auto t = view.currentTool();
        rectBtn.setToggleState (t == SpectrogramView::Tool::rect, juce::dontSendNotification);
        timeBtn.setToggleState (t == SpectrogramView::Tool::time, juce::dontSendNotification);
        brushBtn.setToggleState (t == SpectrogramView::Tool::brush, juce::dontSendNotification);
        const bool brush = t == SpectrogramView::Tool::brush;
        size.setVisible (brush); sizeCap.setVisible (brush);
        size.setValue (view.brushSize(), juce::dontSendNotification);
        const bool fe = view.feathered();
        featherBtn.setToggleState (fe, juce::dontSendNotification);
        auto num = [] (double x) { return juce::String (x, std::abs (x - std::round (x)) < 1.0e-6 ? 0 : 1); };
        featherBtn.setButtonText (fe ? "Feather: " + num (view.featherMs()) + " ms, " + num (view.featherSemitones()) + " st" : juce::String ("Feather: off"));
        if (gSpecPrefs != nullptr)
        {
            gSpecPrefs->setValue ("selTool", (int) t); gSpecPrefs->setValue ("brushPx", view.brushSize());
            gSpecPrefs->setValue ("featherMs", view.featherMs()); gSpecPrefs->setValue ("featherSt", view.featherSemitones());
        }
    }
    SpectrogramView& view;
    juce::Label cap, sizeCap;
    juce::TextButton rectBtn { "Rectangle [S]" }, timeBtn { "Time [T]" }, brushBtn { "Brush [B]" };
    juce::Slider size;
    juce::TextButton featherBtn { "Feather: off" };
};

/** 3.44.0: the De-hum tab's controls, in the ReTouch window and in Noise mode (the window owns the De-hum button, in the action corner, and does the
    work: finding the hums in the box - DeHum.h - and the fix). Find hums [H] lists what the finder found, loudest first, each with a tick (the faint
    ones under 15 dB start unticked), its words and a box with its pitch to change by hand (a buzz: its fundamental, and the harmonics follow); Add
    types a pitch the finder missed; Soft <-> Hard sets how hard the hums are taken out. */
class HumPanel : public juce::Component
{
public:
    static constexpr int kRows = 5;                                   // the panel rows it needs (one more than the others: the list needs room)
    std::function<void()> onFind;                                     // Find hums [H]
    std::function<void (double)> onAdd;                               // Add: a pitch typed (the window measures it and calls add())
    std::function<void (int, double)> onRetune;                       // a listed hum's pitch typed (the window measures it and calls set())
    std::function<void()> onChange;                                   // a tick, or the slider
    HumPanel()
    {
        findBtn.setTooltip ("Looks through the box (its time, its pitch range; all the tracks) for tones that never change - hums, buzzes, whistles - and lists them, loudest first. "
                            "Two seconds or more of box gives a sharp answer; under half a second the tones blur together. A buzz with a fundamental is one line: its harmonics are taken out together. "
                            "Press it again after De-hum to see what is left. Key: H");
        findBtn.onClick = [this] { if (onFind) onFind(); };
        summary.setColour (juce::Label::textColourId, theme::text); summary.setFont (juce::FontOptions (12.5f)); summary.setMinimumHorizontalScale (0.7f);
        summary.setText ("Draw a box round the hum (a stretch of room only is best) and press Find hums.", juce::dontSendNotification);
        addAndMakeVisible (findBtn); addAndMakeVisible (summary);
        viewport.setViewedComponent (&rows, false); viewport.setScrollBarsShown (true, false); viewport.setScrollBarThickness (10);
        addAndMakeVisible (viewport);
        addCap.setText ("Add", juce::dontSendNotification); addCap.setColour (juce::Label::textColourId, theme::text);
        addBox.setInputRestrictions (9, "0123456789.");
        addBox.setTextToShowWhenEmpty ("Hz", theme::dimText); addBox.setSelectAllWhenFocused (true);
        addBox.setTooltip ("A hum the finder missed: type roughly where it is (in Hz) and press Add (or Enter). The exact pitch is measured within 2 % of what you typed; if nothing steady is there, what you typed is taken out exactly.");
        addBox.onReturnKey = [this] { addPressed(); };
        addBox.onEscapeKey = [this] { addBox.setText ({}); if (auto* p = getParentComponent()) p->grabKeyboardFocus(); };
        addBtn.onClick = [this] { addPressed(); };
        addBtn.setTooltip (addBox.getTooltip());
        addAndMakeVisible (addCap); addAndMakeVisible (addBox); addAndMakeVisible (addBtn);
        hardCap.setText ("Soft <-> Hard", juce::dontSendNotification); hardCap.setColour (juce::Label::textColourId, theme::text);
        hard.setSliderStyle (juce::Slider::LinearHorizontal); hard.setRange (0.0, 100.0, 1.0); hard.setValue (70.0, juce::dontSendNotification); hard.setScrollWheelEnabled (false);
        hard.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22); hard.setDoubleClickReturnValue (true, 70.0);
        hard.textFromValueFunction = [] (double v) { dehum::Params p; p.hardness = v / 100.0; return juce::String ((int) std::lround (p.reductionDb())) + " dB"; };
        hard.valueFromTextFunction = [] (const juce::String& t) { return juce::jlimit (0.0, 100.0, (t.retainCharacters ("0123456789.").getDoubleValue() - 6.0) / 34.0 * 100.0); };
        hard.setNumDecimalPlacesToDisplay (0); hard.updateText();                      // (the text function came after the box: write it again)
        hard.setTooltip ("How hard the hums are taken out. Soft (left): 6 dB off each tone through a notch 1.5 Hz wide - gentle, for a hum that is only a little too loud. "
                         "Hard (right): 40 dB off (all of it) through a notch 4 Hz wide, so a hum that drifts a little still goes. Every notch is widened by how far that tone was measured to wander. "
                         "The number is how many dB come off. Double-click: 30 dB.");
        hard.onValueChange = [this] { if (onChange) onChange(); };
        addAndMakeVisible (hardCap); addAndMakeVisible (hard);
        for (auto* c : std::initializer_list<juce::Component*> { &findBtn, &addBtn }) c->setWantsKeyboardFocus (false);
    }
    /** The hums found (or none, with the finder's note). */
    void setHums (std::vector<dehum::Hum> found, const juce::String& note, double seconds, double f0, double f1)
    {
        list = std::move (found); scanned = true;
        if (list.empty()) summary.setText (note.isNotEmpty() ? note : juce::String ("No steady tone stands out in the box."), juce::dontSendNotification);
        else summary.setText (juce::String ((int) list.size()) + (list.size() == 1 ? " hum found in " : " hums found in ") + juce::String (seconds, 1) + " s, " + juce::String ((int) f0) + " - " + juce::String ((int) f1)
                              + " Hz. Untick what is music, change a pitch if it looks wrong, then press De-hum.", juce::dontSendNotification);
        rebuild();
        if (onChange) onChange();
    }
    /** A hum typed in (Add), measured by the window. */
    void add (dehum::Hum h)
    {
        h.on = true;
        for (auto& x : list) if (std::abs (x.editable() - h.hz) < 0.5 && x.kind == dehum::Hum::Kind::Tone) { x = h; rebuild(); if (onChange) onChange(); return; }
        list.insert (list.begin(), h);
        addBox.setText ({});
        rebuild();
        if (onChange) onChange();
    }
    /** A listed hum's pitch, as measured by the window after it was typed. */
    void set (int index, const dehum::Hum& measured)
    {
        if (index < 0 || index >= (int) list.size()) return;
        auto& h = list[(size_t) index];
        h.setEditable (measured.hz);
        if (h.kind == dehum::Hum::Kind::Tone) { h.aboveDb = measured.aboveDb; h.levelDb = measured.levelDb; h.wanderHz = measured.wanderHz; }
        rebuild();
        if (onChange) onChange();
    }
    const std::vector<dehum::Hum>& hums() const noexcept { return list; }
    bool anyOn() const { for (auto& h : list) if (h.on) return true; return false; }
    bool hasScanned() const noexcept { return scanned; }
    double hardness() const { return hard.getValue() / 100.0; }
    void setHardness (double h) { hard.setValue (juce::jlimit (0.0, 100.0, h * 100.0), juce::dontSendNotification); }
    dehum::Params params() const { dehum::Params p; p.hums = list; p.hardness = hardness(); return p; }
    /** Nothing selected any more, or the sound changed under the list: the list stays (its pitches still hold), the summary says what to do. */
    void setStatus (const juce::String& s) { summary.setText (s, juce::dontSendNotification); }
    void setReady (bool ready, bool haveSelection)
    {
        findBtn.setEnabled (ready); addBtn.setEnabled (ready); addBox.setEnabled (ready); hard.setEnabled (ready);
        for (auto& r : rowsUi) { r.tick->setEnabled (ready); r.pitch->setEnabled (ready); }
        juce::ignoreUnused (haveSelection);
    }
    void resized() override
    {
        const int rowH = 24, gap = 4;
        auto r = getLocalBounds();
        auto top = r.removeFromTop (rowH);
        findBtn.setBounds (top.removeFromLeft (116)); top.removeFromLeft (8); summary.setBounds (top);
        r.removeFromTop (gap);
        auto bottom = r.removeFromBottom (rowH);
        r.removeFromBottom (gap);
        viewport.setBounds (r);
        addCap.setBounds (bottom.removeFromLeft (34)); addBox.setBounds (bottom.removeFromLeft (72)); bottom.removeFromLeft (4); addBtn.setBounds (bottom.removeFromLeft (50)); bottom.removeFromLeft (16);
        hardCap.setBounds (bottom.removeFromLeft (92)); hard.setBounds (bottom.removeFromLeft (juce::jmin (bottom.getWidth(), 210)));
        layoutRows();
    }
private:
    struct Row { std::unique_ptr<juce::ToggleButton> tick; std::unique_ptr<juce::Label> text; std::unique_ptr<juce::TextEditor> pitch; };
    void addPressed()
    {
        const double hz = addBox.getText().getDoubleValue();
        if (hz < 20.0 || hz > 20000.0) { summary.setText ("Type a pitch between 20 and 20000 Hz, then Add.", juce::dontSendNotification); return; }
        if (onAdd) onAdd (hz);
        if (auto* p = getParentComponent()) p->grabKeyboardFocus();
    }
    void rebuild()
    {
        rebuilding = true;                                                           // (a pitch box that has the focus loses it as it goes: no retune from that)
        rowsUi.clear();
        rebuilding = false;
        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto& h = list[i];
            Row r;
            r.tick = std::make_unique<juce::ToggleButton>(); r.tick->setToggleState (h.on, juce::dontSendNotification); r.tick->setWantsKeyboardFocus (false);
            r.tick->setTooltip (h.on ? "Ticked: this one is taken out by De-hum. Untick it if it is part of the music." : "Unticked: left alone. Tick it to take it out too.");
            const int k = (int) i;
            r.tick->onClick = [this, k] { if (k < (int) list.size()) { list[(size_t) k].on = rowsUi[(size_t) k].tick->getToggleState(); if (onChange) onChange(); } };
            r.text = std::make_unique<juce::Label>(); r.text->setColour (juce::Label::textColourId, h.on ? theme::text : theme::dimText); r.text->setFont (juce::FontOptions (12.5f)); r.text->setMinimumHorizontalScale (0.7f);
            r.text->setText (h.text() + "  -  " + h.loudness() + (h.user ? " (yours)" : ""), juce::dontSendNotification);
            r.text->setTooltip (h.kind == dehum::Hum::Kind::Series ? "A buzz: tones at whole-number multiples of one fundamental (the fundamental is the 1st harmonic). They go together; the box holds the fundamental - change it and every harmonic follows."
                                : h.kind == dehum::Hum::Kind::Row ? "A row of tones evenly spaced, with no fundamental among them (a switching power supply, say). They go together; the box holds the lowest - change it and the others follow."
                                : "One steady tone. The box holds its pitch: type another if it looks wrong, and the exact pitch is measured near what you typed.");
            r.pitch = std::make_unique<juce::TextEditor>(); r.pitch->setInputRestrictions (9, "0123456789."); r.pitch->setJustification (juce::Justification::centredRight);
            r.pitch->setSelectAllWhenFocused (true);                                    // (a click in the box and typing replaces the pitch)
            r.pitch->setText (juce::String (h.editable(), h.kind == dehum::Hum::Kind::Series ? 2 : 1), juce::dontSendNotification);
            r.pitch->setTooltip (r.text->getTooltip());
            r.pitch->onReturnKey = [this, k] { retune (k); };
            r.pitch->onFocusLost = [this, k] { retune (k); };
            r.pitch->onEscapeKey = [this, k] { if (k < (int) list.size()) rowsUi[(size_t) k].pitch->setText (juce::String (list[(size_t) k].editable(), list[(size_t) k].kind == dehum::Hum::Kind::Series ? 2 : 1), juce::dontSendNotification); if (auto* p = getParentComponent()) p->grabKeyboardFocus(); };
            rows.addAndMakeVisible (*r.tick); rows.addAndMakeVisible (*r.text); rows.addAndMakeVisible (*r.pitch);
            rowsUi.push_back (std::move (r));
        }
        layoutRows();
    }
    void retune (int k)
    {
        if (rebuilding || k < 0 || k >= (int) list.size() || k >= (int) rowsUi.size()) return;
        const double hz = rowsUi[(size_t) k].pitch->getText().getDoubleValue();
        const double was = list[(size_t) k].editable();
        const bool series = list[(size_t) k].kind == dehum::Hum::Kind::Series;
        if (std::abs (hz - was) < 0.005) return;
        if (hz < (series ? 10.0 : 20.0) || hz > 20000.0)                                 // (a fundamental may be as low as 10 Hz; a tone 20 Hz - 20 kHz)
        {
            rowsUi[(size_t) k].pitch->setText (juce::String (was, series ? 2 : 1), juce::dontSendNotification);
            summary.setText (series ? "A fundamental must be between 10 and 20000 Hz." : "A pitch must be between 20 and 20000 Hz.", juce::dontSendNotification);
            return;
        }
        if (series) { list[(size_t) k].setEditable (hz); rebuild(); if (onChange) onChange(); }     // (a fundamental is taken as typed: it is seldom there to measure)
        else if (onRetune) onRetune (k, hz);
        if (auto* p = getParentComponent()) p->grabKeyboardFocus();
    }
    void layoutRows()
    {
        const int w = juce::jmax (100, viewport.getWidth() - (rowsUi.size() * 24 > (size_t) viewport.getHeight() ? 12 : 0)), h = 24;
        rows.setSize (w, juce::jmax (viewport.getHeight(), (int) rowsUi.size() * h));
        int y = 0;
        for (auto& r : rowsUi)
        {
            r.tick->setBounds (0, y, 24, h); r.pitch->setBounds (w - 78, y + 1, 76, h - 2); r.text->setBounds (26, y, w - 26 - 84, h);
            y += h;
        }
    }
    std::vector<dehum::Hum> list; bool scanned = false, rebuilding = false;
    juce::TextButton findBtn { "Find hums [H]" }, addBtn { "Add" };
    juce::Label summary, addCap, hardCap;
    juce::TextEditor addBox; juce::Slider hard;
    juce::Viewport viewport; juce::Component rows; std::vector<Row> rowsUi;
};

class RepairDialog : public JobDialogBase
{
public:
    explicit RepairDialog (RepairContext c) : ctx (std::move (c))
    {
        keymap::KeyMap::setSection (*this, "ReTouch, DeClick & Denoise (Spectral Repair)");      // 3.69.0: its keys are the "ReTouch, DeClick & Denoise (Spectral Repair)" list's (HotKeysLines.h, KeyMap.h)
        setSize (1100, 760);
        intro.setText (ctx.what, juce::dontSendNotification); intro.setColour (juce::Label::textColourId, theme::text); intro.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (intro); addAndMakeVisible (view); addAndMakeVisible (info);
        addAndMakeVisible (harmBar); addAndMakeVisible (toolsBar);
        addAndMakeVisible (humPanel); addAndMakeVisible (dehumBtn);                           // 3.44.0: the De-hum tab
        humPanel.onFind = [this] { findHums(); };
        humPanel.onAdd = [this] (double hz) { measureHum (-1, hz); };
        humPanel.onRetune = [this] (int k, double hz) { measureHum (k, hz); };
        humPanel.onChange = [this] { refreshControls(); };
        dehumBtn.onClick = [this] { preview (3); };
        dehumBtn.setColour (juce::TextButton::buttonColourId, theme::accent);
        dehumBtn.setTooltip ("Takes the ticked hums out of the box's stretch of time on every track - each tone through its own narrow notch that follows the hum's level, fading in and out at the box's edges - "
                             "and shows the result. With no box, the whole of what is loaded. Same rules as ReTouch: nothing is written yet, Play to listen, Undo if it did not work. Press Find hums again to see what is left.");
        info.setColour (juce::Label::textColourId, theme::text);
        dirBox.addItem ("Direction: Horizontal", 1); dirBox.addItem ("Direction: Vertical", 2); dirBox.addItem ("Direction: 2D (both)", 3);
        dirBox.setSelectedId (1, juce::dontSendNotification); dirBox.onChange = [this] { refreshControls(); invalidate(); };
        dirBox.setTooltip ("Where the repair finds its material. Left and right suits steady sounds (hum, a held note). Up and down suits short bursts (a click, a cough) with music above and below. Both blends the two.");
        auto cap = [this] (juce::Label& l, const juce::String& t) { l.setText (t, juce::dontSendNotification); l.setColour (juce::Label::textColourId, theme::text); addAndMakeVisible (l); };
        cap (sensCap, "Click sensitivity"); cap (strengthCap, "Strength"); cap (surroundCap, "Surrounding length"); cap (weightCap, "Before <-> After");
        cap (amountCap, "Amount"); cap (residualCap, "Residual"); cap (roomCap, "");
        auto slider = [this] (juce::Slider& sl, double lo, double hi, double step, double v, const juce::String& suffix)
        {
            sl.setSliderStyle (juce::Slider::LinearHorizontal); sl.setRange (lo, hi, step); sl.setValue (v, juce::dontSendNotification); sl.setScrollWheelEnabled (false);
            sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 22); sl.setTextValueSuffix (suffix);
            sl.onValueChange = [this] { updateSurround(); invalidate(); };
            addAndMakeVisible (sl);
        };
        slider (sens, 1, 10, 1, 6, ""); slider (strength, 0, 100, 1, 80, " %"); slider (surround, 10, 400, 5, 100, " %"); slider (weight, 0, 100, 1, 50, " %");
        slider (amount, -6.0, 12.0, 0.5, 0.0, " dB"); slider (residual, -30.0, -3.0, 1.0, -12.0, " dB");
        amount.setDoubleClickReturnValue (true, 0.0); residual.setDoubleClickReturnValue (true, -12.0);
        amount.setTooltip ("Denoise: how much louder than measured the room tone is taken to be. 0 = as learned; +3 dB takes a little more out, -3 dB a little less. Double-click for 0.");
        residual.setTooltip ("Denoise: how much of the room tone is left in the box (the gain never goes below this). -12 dB leaves a quiet, natural floor; -24 dB is a lot. Double-click for -12.");
        roomBtn.onClick = [this] { roomFromBox(); };
        denoiseBtn.onClick = [this] { preview (2); };
        roomBtn.setTooltip ("Draw a box over a part of THIS picture that holds nothing but the room (its pitch range does not matter: the whole spectrum is learned) and press this: that box is the room tone for Denoise here, instead of the project's. Several boxes (Shift): every stretch they cover. Key: L");
        denoiseBtn.setTooltip ("Denoise: takes the background noise out of the box you drew - its time span and its pitch range, on every track - with the room tone (the project's, from the edit window's Room tone button, or a box of this picture marked with L). "
                               "Above and below the box's pitch range nothing is touched; the denoising fades in and out at the box's edges. Same rules as ReTouch: nothing is written yet, Play to listen, Undo if it did not work. Key: N");
        denoiseBtn.setColour (juce::TextButton::buttonColourId, theme::accent);
        addAndMakeVisible (roomBtn); addAndMakeVisible (denoiseBtn);
        // 3.29.0: a noise only ever heard under the music (a car outside during the quiet passages): no clean stretch to learn from, so the quietest moments of a box WITH music are taken
        quietBox.setButtonText ("The box has music in it: learn the room tone from its quietest moments");
        quietBox.setTooltip ("For a noise that is only there under the music, with no clean stretch of it to learn from. At every pitch the box's quietest moments (between and under the notes) "
                             "are taken as the noise, so the notes drop out of the room tone. Draw the box over a few seconds of the noisy passage (longer is better; Shift adds more), tick this, press L, "
                             "then draw the box to denoise and press N. Keep the Amount modest.");
        quietBox.setColour (juce::ToggleButton::textColourId, theme::text);
        addAndMakeVisible (quietBox);
        strength.setTooltip ("How much of the repair replaces the original sound inside the box. 100 % is a full repair; less keeps some of the original.");
        surround.setTooltip ("How much of the sound around the box is used, as a percentage of the box: its length in time (left and right) and its height in pitch (up and down).");
        setupSides();                                                            // 3.33.0: L, C and R beside the slider
        weight.setTooltip ("0 uses only the sound before the box, 100 only the sound after it, 50 uses both equally (a cross-fade). Applies to left and right.\n"
                           "Attenuate a stray consonant: leave it at 50 and Fermata finds, for every track, the side away from the main consonant; move it to choose a side yourself.");
        methodBox.addItem ("Method: Attenuate", 1); methodBox.addItem ("Method: Replace", 2);
        methodBox.addItem ("Method: Stray consonant", 3);
        methodBox.setSelectedId (1, juce::dontSendNotification);
        methodBox.setTooltip ("Attenuate keeps the music that is under the noise and only turns down what sticks out above the surrounding sound: the best for a click, a pencil drop or a cough over a held note. Replace throws the box away and builds new sound from the surroundings: for gaps and loud, wide damage.\n\n"
                               "Attenuate a stray consonant: for tidying a doubled or messy consonant to one. Draw the box round the EARLY or LATE copy only (from where the stray one starts up to where the main one starts, "
                               "or from where the main one ends to where the stray one stops), from about 2 kHz (1.5 kHz for sh) to the top. Each track is matched to the side of the box AWAY from the main consonant "
                               "(the vowel before an early copy, the sound after a late one), so the main consonant next to the box does not count as 'normal' sound and the stray copy comes right down; "
                               "vowels that carry on through the box keep their level. The sound just before the box dies away naturally into it and the box rises to meet the sound after it, so there are no hard edges: "
                               "cover all of the stray copy and a little past it on the clean side. Use 100 % strength.");
        for (int b : { 128, 256, 512, 1024, 2048, 4096 }) bandsBox.addItem ("Bands: " + juce::String (b), b);
        bandsBox.setSelectedId (1024, juce::dontSendNotification);
        bandsBox.setTooltip ("How finely the sound is split into pitches for Attenuate. More bands follow steady notes more exactly; fewer bands are sharper in time (clicks). 1024 suits most things.");
        linkBtn.setToggleState (true, juce::dontSendNotification);
        linkBtn.setTooltip ("A stereo file is turned down by the SAME amount on both sides (the amount found for the louder side), with each side's own phase kept. The time difference between the microphones and the level difference between them, which make the stereo image, are then not changed. Untick to treat every channel on its own.");
        linkBtn.onClick = [this] { invalidate(); };
        multiBtn.setToggleState (true, juce::dontSendNotification);
        multiBtn.setTooltip ("Attenuate works with a long window for the bass and a short one for the top and joins them smoothly, so both low notes and clicks are matched well.");
        specBtn.setTooltip ("How the picture is drawn: sharpness, colours, and the levels shown.");
        specBtn.onClick = [this] { openSpectroSettings(); };
        methodBox.onChange = [this] { refreshControls(); invalidate(); };
        bandsBox.onChange = [this] { invalidate(); };
        multiBtn.onClick = [this] { invalidate(); };
        view.onNeedDetail = [this] (const PictureRequest& rq) { startDetail (rq); };
        view.setReferenceA (ctx.refA);                                       // (3.28.0: the read-out names notes at the project's A)
        for (auto* x : std::initializer_list<juce::Component*> { &methodBox, &bandsBox, &multiBtn, &linkBtn, &specBtn, &dirBox, &previewBtn, &declickBtn, &compareBtn, &revertBtn, &finishBtn, &close, &playBtn, &stopBtn, &loopBtn, &hearCap, &hearBox, &scaleBox, &detailBox }) addAndMakeVisible (x);
        scaleBox.addItem ("Pitch scale: Mel (as iZotope RX)", 6); scaleBox.addItem ("Pitch scale: linear (even Hz)", 1); scaleBox.addItem ("Pitch scale: mostly linear", 2); scaleBox.addItem ("Pitch scale: halfway", 3);
        scaleBox.addItem ("Pitch scale: mostly musical", 4); scaleBox.addItem ("Pitch scale: musical (every octave the same height)", 5);
        scaleBox.setSelectedId (6, juce::dontSendNotification); view.setMel();
        scaleBox.setTooltip ("How the pitches are spread up the picture. Linear: every 1000 Hz takes the same height (the high end gets room, the low end is squashed). Musical: every octave takes the same height (like a piano). The steps in between blend the two.");
        detailBox.addItem ("Bass detail: blended (sharp top, fine bass)", 1); detailBox.addItem ("Bass detail: off (sharpest in time)", 2); detailBox.addItem ("Bass detail: extra fine", 3);
        detailBox.setSelectedId (1, juce::dontSendNotification);
        detailBox.setTooltip ("The picture is worked out twice: with a short window (sharp in time, good for clicks) and with a long one (sharp in pitch, good for bass notes). Blended: the long one is used at the bottom, the short one at the top, and they fade into each other in between (250 Hz to 1.5 kHz). Off: the short one only. Extra fine: an even longer window for the bass (below about 800 Hz).");
        detailBox.onChange = [this] { rescan(); };
        scaleBox.onChange = [this] { static const double w[] = { 0.0, 0.25, 0.5, 0.75, 1.0 }; const int id = scaleBox.getSelectedId(); if (id == 6) view.setMel(); else view.setScale (w[juce::jlimit (0, 4, id - 1)]); };
        view.onNote = [this] (const juce::String& t) { status.setText (t, juce::dontSendNotification); };
        hearCap.setText ("Listen to", juce::dontSendNotification); hearCap.setColour (juce::Label::textColourId, theme::text);
        hearBox.addItem ("Original", 1); hearBox.addItem ("Fixed (after Fix)", 2); hearBox.setSelectedId (1, juce::dontSendNotification);
        hearBox.setTooltip ("Which sound Play plays: the audio as it is, or the audio with the repair applied (available after you press ReTouch or De-click). Press Play while a loop runs to hear the other one.");
        playBtn.setColour (juce::TextButton::buttonColourId, theme::accent);
        playBtn.setTooltip ("Plays the part of the picture you can see, from the red playhead if you have clicked the time ruler to place one (zoom in with the left / right arrows to play a shorter piece, zoom out to hear more), through the mixers, with a playhead on the picture. No repair is needed: use it to listen for what has to be mended. Key: Space");
        loopBtn.setTooltip ("Repeat what is playing until you press Stop");
        stopBtn.setTooltip ("Stops playing");
        playBtn.onClick = [this] { playPressed(); };
        stopBtn.onClick = [this] { if (ctx.stop) ctx.stop(); };
        loopBtn.onClick = [this] { if (! isPlayingNow()) return; if (loopBtn.getToggleState()) playPressed(); else if (ctx.setLooping) ctx.setLooping (false); };
        hearBox.onChange = [this] { if (isPlayingNow()) playPressed(); };
        ticker.owner = this; ticker.startTimerHz (30);
        if (ctx.hold) ctx.hold (true);
        previewBtn.onClick = [this] { preview (0); };
        gainBtn.onClick = [this] { applyGain(); };
        // 3.29.0: the amount is set in the panel (remembered), and G or the button applies it at once - no pop-up to click through each time
        gainCap.setText ("Gain", juce::dontSendNotification); gainCap.setColour (juce::Label::textColourId, theme::text); addAndMakeVisible (gainCap);
        gainSlider.setSliderStyle (juce::Slider::LinearHorizontal); gainSlider.setRange (-60.0, 18.0, 0.1);
        gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 74, 22); gainSlider.setDoubleClickReturnValue (true, 0.0);
        gainSlider.textFromValueFunction = [] (double v) { return v <= -59.95 ? juce::String ("-inf dB") : (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB"; };
        gainSlider.valueFromTextFunction = [] (const juce::String& t) { return t.containsIgnoreCase ("inf") ? -60.0 : t.retainCharacters ("-+0123456789.").getDoubleValue(); };
        gainSlider.setSkewFactorFromMidPoint (-6.0);
        gainSlider.setValue (uiPrefs().getDoubleValue (prefKey ("gainDb"), -6.0), juce::dontSendNotification);
        gainSlider.onValueChange = [this] { uiPrefs().setValue (prefKey ("gainDb"), gainSlider.getValue()); };
        gainSlider.setTooltip ("How much louder or quieter the box is made: set it once, then G (or the button) applies it to each box you draw. Double-click: 0 dB.");
        addAndMakeVisible (gainSlider);
        gainBtn.setTooltip ("Gain: makes the sound inside the box louder or quieter (from silence up to +18 dB), and nothing else: for a resonance, a ring or a note in one spot that should stand out less, or more. "
                            "Draw the box round it and press G. The gain fades in and out smoothly at the box's edges.");
        addAndMakeVisible (gainBtn); gainBtn.setWantsKeyboardFocus (false);
        declickBtn.onClick = [this] { preview (1); };
        compareBtn.onClick = [this] { toggleCompare(); };
        revertBtn.onClick = [this] { revert(); };
        finishBtn.onClick = [this] { finish(); };
        close.onClick = [this] { if (running) job.requestCancel(); else if (mayClose()) closeDialogOf (this); };
        previewBtn.setColour (juce::TextButton::buttonColourId, theme::accent); declickBtn.setColour (juce::TextButton::buttonColourId, theme::accent);
        finishBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1f7a46));
        declickBtn.setTooltip ("Finds and mends the clicks in the time span of the box (or everywhere if there is no box; the pitch range of the box is ignored) and shows the result. Set how many it finds with Click sensitivity. A digital dropout (a block of samples that went missing: the sound jumps in one sample on every track at once) is mended by putting the missing samples back, on every track in the same way. Same rules as ReTouch: nothing is written yet, Play to listen, Undo if it did not work.");
        previewBtn.setTooltip ("ReTouch: rebuilds or turns down the box you drew from its surroundings and shows the result. (A small box round a click that reaches down into the bass: if the click is a digital dropout, the samples that went missing are put back first, on every track in the same way, and then the box is left alone; Click sensitivity decides how readily such a dropout is found.) It does not play, and nothing is written to the files yet: press Play to listen, and Undo if it did not work. You can then draw a box round another problem and press ReTouch or De-click again: the earlier fixes stay.");
        finishBtn.setTooltip ("Writes all the fixes into the files (everything between the marks is rendered again in one go). The Undo command of the window brings the original back afterwards.");
        close.setTooltip ("Closes the window and puts everything back as it was (nothing is kept).");
        revertBtn.setTooltip ("Takes the last fix off again. Press it again to take off the one before.");
        compareBtn.setTooltip ("Switches the picture between the original and the repaired sound.");
        view.onSelection = [this] { refreshControls(); invalidate(); };
        setupTabs();
        setupConsonants();
        loadControls();
        refreshControls();
        close.setButtonText ("Cancel");
        if (ctx.pairWritten)
        {
            finishBtn.setButtonText ("Write back this stream"); close.setButtonText ("Cancel both");
            finishBtn.setTooltip ("Gets this stream ready and closes this window (press it on an untouched stream to leave that one as it is). When BOTH streams have been written back the corrected audio goes into the edit, with the edit point and its fades exactly where they are. Cancel (or the X) closes both windows and changes nothing.");
        }
        if (ctx.acceptText.isNotEmpty()) finishBtn.setButtonText (ctx.acceptText);
        if (ctx.acceptTip.isNotEmpty()) finishBtn.setTooltip (ctx.acceptTip);
        setWantsKeyboardFocus (true);                                              // Space plays / stops, whatever was clicked last
        for (auto* ch : getChildren()) if (dynamic_cast<juce::Button*> (ch) != nullptr || dynamic_cast<juce::ComboBox*> (ch) != nullptr) ch->setWantsKeyboardFocus (false);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RepairDialog> (this)] { if (safe != nullptr) safe->grabKeyboardFocus(); });
        status.setText ("Scanning all the tracks...", juce::dontSendNotification);
        auto load = ctx.load; double rate = ctx.rate; double secs = (double) ctx.length / ctx.rate;
        juce::Component::SafePointer<RepairDialog> self (this);
        auto spec = std::make_shared<MergedSpectrogram>();
        origTracks = std::make_shared<std::vector<std::vector<float>>>();
        auto keep = origTracks;
        const int detailN = detailSize();
        runJob ([load, rate, spec, keep, detailN] (AudioJob& j) -> juce::String
                {
                    auto tracks = load (j);
                    if (j.cancelled()) return "Cancelled.";
                    if (tracks.empty()) return "There is no audio here.";
                    *spec = SpectralRepair::spectrogram (tracks, rate, 1600, detailN);
                    *keep = std::move (tracks);
                    return spec->frames > 0 ? juce::String() : juce::String ("The marked area is too short to show (it needs at least about 0.05 s).");
                },
                [self, spec, secs] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isNotEmpty()) { self->status.setText (err, juce::dontSendNotification); return; }
                    self->origSpec = std::make_shared<MergedSpectrogram> (*spec);
                    self->view.setData (std::move (*spec), secs);
                    self->status.setText ("Press Space (or Play) to listen to what is in view (left / right arrows zoom in time, down / up zoom in / out in pitch, click the ruler to place the playhead, drag it to move; R = ReTouch, D = De-click). Drag a box around the noise (drag its edges or corners to resize it), set the controls, then press ReTouch (or De-click), then Play to listen. Undo if it did not work; or draw a box round the next problem and ReTouch / De-click again (the earlier fixes stay). Write back to file when you are happy.  Double-click the picture to clear the box.", juce::dontSendNotification);
                    self->scanned = true; self->refreshControls();
                });
    }
    juce::String prefKey (const char* k) const { return juce::String ("repair.") + k; }
    /** What the controls were set to last time (the picture settings, the repair settings): put back when the window opens, saved when it closes. */
    void loadControls()
    {
        auto& p = uiPrefs();
        auto num = [&] (const char* k, juce::Slider& s) { const auto key = prefKey (k); if (p.containsKey (key)) s.setValue (juce::jlimit (s.getMinimum(), s.getMaximum(), p.getDoubleValue (key)), juce::dontSendNotification); };
        auto id = [&] (const char* k, juce::ComboBox& b) { const auto key = prefKey (k); if (p.containsKey (key) && b.indexOfItemId (p.getIntValue (key)) >= 0) b.setSelectedId (p.getIntValue (key), juce::dontSendNotification); };
        num ("sens", sens); num ("strength", strength); num ("surround", surround); num ("weight", weight); num ("amount", amount); num ("residual", residual);
        id ("method", methodBox); id ("bands", bandsBox); id ("dir", dirBox); id ("scale", scaleBox); id ("detail", detailBox);
        if (p.containsKey (prefKey ("multi"))) multiBtn.setToggleState (p.getBoolValue (prefKey ("multi")), juce::dontSendNotification);
        if (p.containsKey (prefKey ("link"))) linkBtn.setToggleState (p.getBoolValue (prefKey ("link")), juce::dontSendNotification);
        if (p.containsKey (prefKey ("roll"))) view.setRoll (p.getDoubleValue (prefKey ("roll")));
        num ("consLevel", level);
        if (p.containsKey (prefKey ("humHard"))) humPanel.setHardness (p.getDoubleValue (prefKey ("humHard")));
        tab = juce::jlimit (0, (int) kTabCount - 1, p.getIntValue (prefKey ("tab"), kTabTouch)); lightTab();
        const int sc = scaleBox.getSelectedId();
        if (sc == 6) view.setMel(); else { static const double w[] = { 0.0, 0.25, 0.5, 0.75, 1.0 }; view.setScale (w[juce::jlimit (0, 4, sc - 1)]); }
    }
    void saveControls()
    {
        auto& p = uiPrefs();
        p.setValue (prefKey ("sens"), sens.getValue()); p.setValue (prefKey ("strength"), strength.getValue()); p.setValue (prefKey ("surround"), surround.getValue()); p.setValue (prefKey ("weight"), weight.getValue());
        p.setValue (prefKey ("amount"), amount.getValue()); p.setValue (prefKey ("residual"), residual.getValue());
        p.setValue (prefKey ("method"), methodBox.getSelectedId()); p.setValue (prefKey ("bands"), bandsBox.getSelectedId()); p.setValue (prefKey ("dir"), dirBox.getSelectedId());
        p.setValue (prefKey ("scale"), scaleBox.getSelectedId()); p.setValue (prefKey ("detail"), detailBox.getSelectedId());
        p.setValue (prefKey ("multi"), multiBtn.getToggleState()); p.setValue (prefKey ("link"), linkBtn.getToggleState()); p.setValue (prefKey ("roll"), view.rollValue()); p.setValue (prefKey ("tab"), tab);
        p.setValue (prefKey ("consLevel"), level.getValue());
        p.setValue (prefKey ("humHard"), humPanel.hardness());
    }
    /** Closing (Cancel, the X or Escape) with fixes made but not written back asks first (they used to go without a word - Escape is easily pressed).
        False when it has asked: the answer closes the window. */
    bool mayClose()
    {
        if (committed.empty() || accepted) return true;
        const int n = (int) history.size();                                     // (3.30.1: fixes, not rectangles - a fix over several boxes is one)
        juce::Component::SafePointer<RepairDialog> self (this);
        confirmAsync ("Spectral Repair", juce::String (n) + (n == 1 ? " fix has" : " fixes have") + (ctx.acceptText.isEmpty() ? " not been written back to the file yet" : " not been kept yet")
                      + ". Close the window and throw " + (n == 1 ? "it" : "them") + " away? (" + keepName() + " keeps " + (n == 1 ? "it" : "them") + ".)",
                      n == 1 ? "Throw it away" : "Throw them away", [self] { if (self != nullptr) closeDialogOf (self.getComponent()); });
        return false;
    }
    ~RepairDialog() override
    {
        saveControls();
        ticker.stopTimer(); ticker.owner = nullptr;
        if (detailCancel != nullptr) detailCancel->store (true);
        if (ctx.stop) ctx.stop();
        if (! accepted)                                                    // closing without Commit puts everything back as it was
        {
            if (applied && ctx.unapply) ctx.unapply();
            if (prepared && ctx.discard) ctx.discard();
        }
        if (ctx.hold) ctx.hold (false);
    }
    // ---- the tools at the bottom: one tab for each tool (only one set of controls shows at a time, so the picture keeps the room), then the transport and the fixes ----
    enum { kTabTouch, kTabGain, kTabClick, kTabDenoise, kTabConsonant, kTabHum, kTabCount };       // (3.44.0: De-hum)
    static constexpr int kTabRows = (kTabCount + 1) / 2;                          // the tabs stand in two columns
    static constexpr int kRowH = 24, kRowGap = 4, kPanelPad = 6, kTabW = 104, kTabGap = 12, kCapGap = 12, kTransportH = 26, kActionW = 116;      // (kActionW: the button that does the work, the same size and in the same place on every tab)
    static constexpr int sliderW = 140;                                           // a slider with its number: short on purpose
    static constexpr int kChoicesW = 184 + 8 + 180 + 8 + 108 + 8 + 142 + 8 + 112;                       // the five choices of ReTouch in a row
    static constexpr int kAmountsW = (58 + sliderW) + kCapGap + (116 + sliderW) + kCapGap + (100 + sliderW) + 3 * 26 + kCapGap + 16 + kActionW;   // its three sliders (L C R after the last) and the button in a row
    static constexpr int kListenW = 70 + 6 + 60 + 6 + 64 + 12 + 62 + 170, kBarMinW = 90, kFixesW = 120 + 8 + 70 + 8 + 176 + 8 + 96;
    int tab = kTabTouch;
    juce::TextButton tabBtn[kTabCount];
    juce::Label gainHint;
    juce::Rectangle<int> panelR, contentR;                                       // the tool panel and the part of it that holds the controls (set by layoutBottom)
    /** ReTouch has three rows (the choices; the sliders and the button; the harmonics) and, where the window is narrow, one more for each that does not fit in a row. */
    static int reTouchRows (int w) { return (w >= kChoicesW ? 1 : 2) + (w >= kAmountsW ? 1 : 2) + (w - kActionW - 16 >= HarmonicsBar::kOneRowW ? 1 : 2); }
    static int transportRows (int w) { return w >= kListenW + 8 + kBarMinW + 16 + kFixesW ? 1 : 2; }
    static juce::Rectangle<int> rowOf (juce::Rectangle<int> inner, int row) { return { inner.getX(), inner.getY() + row * (kRowH + kRowGap), inner.getWidth(), kRowH }; }
    /** Puts a control at the left of the row (nothing if it is hidden: the ones after it move up to fill the space). */
    static void putLeft (juce::Rectangle<int>& row, juce::Component& c, int w, int gap = 8)
    {
        if (! c.isVisible()) return;
        c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap);
    }
    /** A caption and the slider after it. */
    static void putSlider (juce::Rectangle<int>& row, juce::Label& cap, juce::Slider& sl, int capW)
    {
        if (! sl.isVisible()) { cap.setBounds ({}); return; }
        cap.setBounds (row.removeFromLeft (capW)); sl.setBounds (row.removeFromLeft (sliderW)); row.removeFromLeft (kCapGap);
    }
    std::vector<juce::Component*> tabControls (int t)
    {
        switch (t)
        {
            case kTabTouch:   return { &methodBox, &dirBox, &bandsBox, &multiBtn, &linkBtn, &strengthCap, &strength, &surroundCap, &surround, &weightCap, &weight, &sideL, &sideC, &sideR, &previewBtn, &harmBar };
            case kTabGain:    return { &gainBtn, &gainHint, &gainCap, &gainSlider };
            case kTabClick:   return { &sensCap, &sens, &declickBtn };
            case kTabConsonant: return { &copyBtn, &copyMoveBtn, &consHint, &consCap, &consBox, &renameBtn, &forgetBtn, &levelCap, &level,
                                         &nudgeCap, &nudgeL5, &nudgeL1, &nudgeRead, &nudgeR1, &nudgeR5, &pasteBtn };
            case kTabHum:     return { &humPanel, &dehumBtn };
            default:          return { &amountCap, &amount, &residualCap, &residual, &roomBtn, &roomCap, &denoiseBtn, &quietBox };
        }
    }
    /** Lights the tab that is showing and no other. (By hand, not with a radio group: JUCE's radio group runs the click action of the key it turns off, and that put ReTouch back on.) */
    void lightTab() { for (int i = 0; i < kTabCount; ++i) tabBtn[i].setToggleState (i == tab, juce::dontSendNotification); }
    void selectTab (int t)
    {
        tab = juce::jlimit (0, (int) kTabCount - 1, t);
        lightTab();
        refreshControls();                                                       // (which also lays the controls out again)
    }
    /** 3.49.0: for a tab's key - true when its tab is the one open (the key does the tab's work); otherwise the tab is opened and the key does no more. */
    bool tabWasOpen (int t) { if (tab == t) return true; selectTab (t); return false; }
    void setupTabs()
    {
        const char* names[] = { "ReTouch [R]", "Gain [G]", "De-click [D]", "Denoise [N]", "Consonants [C]", "De-hum [H]" };
        const char* tips[] = { "ReTouch: rebuilds or turns down the box you drew from its surroundings (key R).", "Gain: makes the sound in the box louder or quieter (key G).",
                               "De-click: finds and mends clicks (key D).", "Denoise: takes the room noise out of the box (keys L and N).",
                               "Consonants: copy a consonant (every track, every channel, as each heard it), move an early or late one, or paste a kept one where one is missing (key C; P pastes; comma and full stop nudge).",
                               "De-hum: finds the hums in the box - electrical hum, a buzz and its harmonics, a whistle - lists them for you to tick and tune, and takes them out (key H finds them)." };
        for (int i = 0; i < kTabCount; ++i)
        {
            auto& t = tabBtn[i]; t.setButtonText (names[i]); t.setTooltip (tips[i]); t.setClickingTogglesState (false);
            t.setColour (juce::TextButton::buttonOnColourId, theme::accent);
            t.onClick = [this, i] { selectTab (i); };
            addAndMakeVisible (t);
        }
        lightTab();
        gainHint.setText ("Louder or quieter inside the box, from silence up to +18 dB, and nothing else. Set the amount, draw a box, press G - and G again for the next box.", juce::dontSendNotification);
        gainHint.setColour (juce::Label::textColourId, theme::dimText); gainHint.setMinimumHorizontalScale (0.8f);
        addAndMakeVisible (gainHint);
    }
    /** The controls of the tab that is showing, in the panel's content area. */
    void placeTab (juce::Rectangle<int> in, int rows)
    {
        auto lastRow = rowOf (in, rows - 1);                                     // the button that does the work is in the same place whichever tab shows: the bottom right corner of the panel
        const auto action = lastRow.removeFromRight (kActionW);
        switch (tab)
        {
            case kTabTouch:
            {   // first row what to do and how, then how much, and the button that does it
                int row = 0;
                auto r = rowOf (in, row++);
                putLeft (r, methodBox, 184); putLeft (r, dirBox, 180); putLeft (r, bandsBox, 108);     // (3.71.0: 108 and 142 - "Bands: 1024" and "Multi-resolution" were squeezed)
                if (in.getWidth() < kChoicesW) r = rowOf (in, row++);                                        // narrow: the two ticks on a row of their own
                putLeft (r, multiBtn, 142); putLeft (r, linkBtn, 112);
                r = rowOf (in, row++);
                if (in.getWidth() >= kAmountsW)
                {
                    previewBtn.setBounds (action); r.removeFromRight (kActionW + 16);
                    putSlider (r, strengthCap, strength, 58); putSlider (r, surroundCap, surround, 116); putSlider (r, weightCap, weight, 100); putSides (r);
                }
                else                                                                                           // narrow: Before <-> After and the button go on a second row
                {
                    putSlider (r, strengthCap, strength, 58); putSlider (r, surroundCap, surround, 116);
                    r = rowOf (in, row++);
                    putSlider (r, weightCap, weight, 100); putSides (r);
                    previewBtn.setBounds (action);
                }
                r = rowOf (in, row++);                                                                         // 3.33.0: the harmonics (beside the button, on the last row)
                if (in.getWidth() - kActionW - 16 < HarmonicsBar::kOneRowW) r = r.getUnion (rowOf (in, row++));      // (narrow: two rows)
                if (r.getBottom() >= action.getY()) r.removeFromRight (kActionW + 16);
                harmBar.setBounds (r);
                break;
            }
            case kTabGain:
            {   // 3.29.0: the amount in the panel itself; the button (or G) applies it straight away
                auto r0 = rowOf (in, 0); r0.removeFromRight (kActionW + 8);
                gainCap.setBounds (r0.removeFromLeft (44)); gainSlider.setBounds (r0.removeFromLeft (juce::jmin (r0.getWidth(), sliderW + 90)));
                gainBtn.setBounds (action); gainHint.setBounds (rowOf (in, 1));
                break;
            }
            case kTabClick:
            {
                auto r = rowOf (in, 0);
                sensCap.setBounds (r.removeFromLeft (112)); r.removeFromLeft (8); sens.setBounds (r.removeFromLeft (sliderW));
                declickBtn.setBounds (action);
                break;
            }
            case kTabHum:
            {   // 3.44.0: the panel has the rows to itself (Find and the summary, the list, Add and Soft <-> Hard); De-hum in the action corner
                auto r = in.withBottom (lastRow.getBottom());
                humPanel.setBounds (r.withTrimmedRight (kActionW + 8));
                dehumBtn.setBounds (action);
                break;
            }
            case kTabConsonant:
            {   // copy; the kept ones; then the level, the nudge and Paste (in the action corner)
                auto r0 = rowOf (in, 0);
                putLeft (r0, copyBtn, 150); putLeft (r0, copyMoveBtn, 150, 12); consHint.setBounds (r0);
                auto r1 = rowOf (in, 1);
                consCap.setBounds (r1.removeFromLeft (44)); r1.removeFromLeft (4);
                forgetBtn.setBounds (r1.removeFromRight (70)); r1.removeFromRight (6); renameBtn.setBounds (r1.removeFromRight (84)); r1.removeFromRight (8);
                consBox.setBounds (r1.withWidth (juce::jmin (r1.getWidth(), 520)));
                pasteBtn.setBounds (action);
                levelCap.setBounds (lastRow.removeFromLeft (44)); level.setBounds (lastRow.removeFromLeft (sliderW)); lastRow.removeFromLeft (kCapGap + 4);
                nudgeCap.setBounds (lastRow.removeFromLeft (48));
                nudgeL5.setBounds (lastRow.removeFromLeft (34)); lastRow.removeFromLeft (3); nudgeL1.setBounds (lastRow.removeFromLeft (28)); lastRow.removeFromLeft (3);
                nudgeRead.setBounds (lastRow.removeFromLeft (70)); lastRow.removeFromLeft (3);
                nudgeR1.setBounds (lastRow.removeFromLeft (28)); lastRow.removeFromLeft (3); nudgeR5.setBounds (lastRow.removeFromLeft (34));
                break;
            }
            default:
            {
                auto r1 = rowOf (in, 0), r2 = rowOf (in, 1);
                amountCap.setBounds (r1.removeFromLeft (62)); amount.setBounds (r1.removeFromLeft (sliderW)); r1.removeFromLeft (kCapGap);
                residualCap.setBounds (r1.removeFromLeft (66)); residual.setBounds (r1.removeFromLeft (sliderW));
                denoiseBtn.setBounds (action); r2.removeFromRight (kActionW + 8);
                roomBtn.setBounds (r2.removeFromLeft (190)); r2.removeFromLeft (8); roomCap.setBounds (r2);
                auto r3 = rowOf (in, 2); r3.removeFromRight (kActionW + 8); quietBox.setBounds (r3);        // (3.29.0)
                break;
            }
        }
    }
    /** The tool panel (the tabs in two columns on the left, the controls of the chosen one beside them) and under it the transport and the fixes.
        Returns the height they need at this width (place = false only measures). */
    int layoutBottom (juce::Rectangle<int> area, bool place)
    {
        const int tabsW = 2 * kTabW + kRowGap;
        const int contentW = juce::jmax (100, area.getWidth() - 2 * kPanelPad - tabsW - kTabGap);
        const int rows = juce::jmax ((int) kTabRows, reTouchRows (contentW), tab == kTabHum ? HumPanel::kRows : 0);     // (the same height whichever tab shows, so the picture does not jump; the tabs need their rows. 3.44.0: De-hum's list needs two more)
        const int panelH = 2 * kPanelPad + rows * kRowH + (rows - 1) * kRowGap;
        const int trRows = transportRows (area.getWidth());
        const int total = panelH + trRows * (kRowGap + kTransportH);
        if (! place) return total;
        panelR = { area.getX(), area.getY(), area.getWidth(), panelH };
        auto inner = panelR.reduced (kPanelPad);
        auto tabsArea = inner.removeFromLeft (tabsW); inner.removeFromLeft (kTabGap);
        for (int i = 0; i < kTabCount; ++i) tabBtn[i].setBounds (tabsArea.getX() + (i % 2) * (kTabW + kRowGap), tabsArea.getY() + (i / 2) * (kRowH + kRowGap), kTabW, kRowH);
        contentR = inner;
        placeTab (contentR, rows);
        auto placeFixes = [this] (juce::Rectangle<int> r)
        {
            close.setBounds (r.removeFromRight (96)); r.removeFromRight (8);
            finishBtn.setBounds (r.removeFromRight (176)); r.removeFromRight (8);
            revertBtn.setBounds (r.removeFromRight (70)); r.removeFromRight (8);
            compareBtn.setBounds (r.removeFromRight (120));
        };
        auto row = juce::Rectangle<int> (area.getX(), panelR.getBottom() + kRowGap, area.getWidth(), kTransportH);
        if (trRows == 1) placeFixes (row.removeFromRight (kFixesW));
        playBtn.setBounds (row.removeFromLeft (70)); row.removeFromLeft (6);
        stopBtn.setBounds (row.removeFromLeft (60)); row.removeFromLeft (6);
        loopBtn.setBounds (row.removeFromLeft (64)); row.removeFromLeft (12);
        hearCap.setBounds (row.removeFromLeft (62)); hearBox.setBounds (row.removeFromLeft (170)); row.removeFromLeft (8);
        bar.setBounds (row.withTrimmedRight (trRows == 1 ? 16 : 0).reduced (0, 3));                      // (the progress bar of a scan or a fix, shown only while it runs)
        if (trRows == 2) placeFixes ({ area.getX(), row.getBottom() + kRowGap, area.getWidth(), kTransportH });
        return total;
    }
    void paint (juce::Graphics& g) override
    {
        if (panelR.isEmpty()) return;
        const auto b = panelR.toFloat().reduced (0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.16f)); g.fillRoundedRectangle (b, 5.0f);
        const auto line = theme::manuscript() ? theme::giltDim() : theme::border.brighter (0.25f);
        g.setColour (line); g.drawRoundedRectangle (b, 5.0f, 1.0f);
        const float dx = (float) contentR.getX() - (float) kTabGap * 0.5f;
        g.setColour (line.withAlpha (0.7f)); g.drawLine (dx, b.getY() + 5.0f, dx, b.getBottom() - 5.0f, 1.0f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        intro.placeTopRight (*this, 6);                                          // the explanation is behind the blue i
        const int bottomH = layoutBottom (r, false);                             // how high the tools need to be at this width
        auto bottom = r.removeFromBottom (bottomH);
        layoutBottom (bottom, true);
        r.removeFromBottom (4);
        { auto sr = r.removeFromBottom (22); toolsBar.setBounds (sr.removeFromRight (juce::jmin (SelectToolsBar::kWidth, sr.getWidth() / 2))); status.setBounds (sr.reduced (2, 0)); }     // (3.33.0: the tools under Spectrogram settings)
        { auto ir = r.removeFromBottom (22); specBtn.setBounds (ir.removeFromRight (150)); ir.removeFromRight (6); scaleBox.setBounds (ir.removeFromRight (300)); ir.removeFromRight (6); detailBox.setBounds (ir.removeFromRight (280)); info.setBounds (ir.reduced (2, 0)); }     // (3.71.0: 280 - its longest choice was squeezed at 210)
        view.setBounds (r);
        repaint (bottom.expanded (2));
    }
protected:
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey)                                      // Space: play what is loaded, or stop
        {
            if (spaceIsForRecording()) return true;
            if (scanned && ! running) { if (isPlayingNow()) { if (ctx.stop) ctx.stop(); } else playPressed(); }
            return true;
        }
        if ((k.getTextCharacter() == 'r' || k.getTextCharacter() == 'R') && ! k.getModifiers().isAnyModifierKeyDown())      // R: do the fix with the settings as they are
        {
            // 3.49.0 (asked for on 10 November 2026): R, G, D, N and H do their tab's work only when that tab is already open; pressed from another
            // tab they open theirs and do nothing more (a second press does the work). They used to open the tab and do the work in one press.
            if (! tabWasOpen (kTabTouch)) return true;
            if (previewBtn.isEnabled()) preview (0);
            return true;
        }
        if ((k.getTextCharacter() == 'h' || k.getTextCharacter() == 'H') && k.getModifiers().withoutMouseButtons() == juce::ModifierKeys::shiftModifier)      // 3.33.0: Shift+H (3.44.0: was H): the harmonics of the box, on or off
        {
            selectTab (kTabTouch);
            harmBar.toggle();
            return true;
        }
        if ((k.getTextCharacter() == 'h' || k.getTextCharacter() == 'H') && ! k.getModifiers().isAnyModifierKeyDown())      // 3.44.0: H: the De-hum tab, and look for the hums in the box
        {
            if (! tabWasOpen (kTabHum)) return true;
            findHums();
            return true;
        }
        if (sideKey (k)) return true;                                                                                         // 3.33.0: Z, \ and X: L, C and R
        if (toolsBar.key (k)) return true;                                                                                    // 3.33.0: S, T, B, [ and ]
        if ((k.getTextCharacter() == 'n' || k.getTextCharacter() == 'N') && ! k.getModifiers().isAnyModifierKeyDown())      // N: denoise the box
        {
            if (! tabWasOpen (kTabDenoise)) return true;
            if (denoiseBtn.isEnabled()) preview (2);
            return true;
        }
        if ((k.getTextCharacter() == 'l' || k.getTextCharacter() == 'L') && ! k.getModifiers().isAnyModifierKeyDown())      // L: this box is the room tone
        {
            selectTab (kTabDenoise);
            if (roomBtn.isEnabled()) roomFromBox();
            return true;
        }
        if ((k.getTextCharacter() == 'g' || k.getTextCharacter() == 'G') && ! k.getModifiers().isAnyModifierKeyDown())      // G: change the gain of the box
        {
            if (! tabWasOpen (kTabGain)) return true;
            if (gainBtn.isEnabled()) applyGain();
            return true;
        }
        if ((k.getTextCharacter() == 'd' || k.getTextCharacter() == 'D') && ! k.getModifiers().isAnyModifierKeyDown())
        {
            if (! tabWasOpen (kTabClick)) return true;
            if (declickBtn.isEnabled()) preview (1);
            return true;
        }
        if ((k.getTextCharacter() == 'c' || k.getTextCharacter() == 'C') && ! k.getModifiers().isAnyModifierKeyDown())      // C: the Consonants tab
        {
            selectTab (kTabConsonant);
            return true;
        }
        if ((k.getTextCharacter() == 'p' || k.getTextCharacter() == 'P') && ! k.getModifiers().isAnyModifierKeyDown())      // P: paste the chosen consonant at the playhead
        {
            selectTab (kTabConsonant);
            if (pasteBtn.isEnabled()) pasteConsonant();
            return true;
        }
        {   // , and . nudge the consonant just pasted 1 ms earlier / later; < and > (Shift with them) 5 ms
            const auto ch = k.getTextCharacter();
            const int ms = ch == ',' ? -1 : ch == '.' ? 1 : ch == '<' ? -5 : ch == '>' ? 5 : 0;
            if (ms != 0 && ! k.getModifiers().isCommandDown() && ! k.getModifiers().isAltDown())
            {
                if (scanned && (running ? pasteRunning : lastIsPaste())) { selectTab (kTabConsonant); nudge (ms); }
                return true;
            }
        }
        if (scanned && view.zoomKey (k)) return true;                           // arrows: right / left zoom in / out in time, up / down in pitch
        return false;
    }
private:
    /** The room tone Denoise uses: a box of this picture marked with L (its time span; the whole spectrum of it), else the project's. */
    bool haveRoomTone() const { if (rtBoxT1 > rtBoxT0) return true; for (auto& p : ctx.roomMono) if (p.valid()) return true; return false; }
    /** The same in a few words, for the little caption in the Denoise box (the full sentence is its tooltip). */
    juce::String roomSpanText() const
    {
        if (rtSpans.size() > 1) return juce::String ((int) rtSpans.size()) + " stretches, " + specTime (rtBoxT0, 2) + " - " + specTime (rtBoxT1, 2);
        return "box at " + specTime (rtBoxT0, 2) + " - " + specTime (rtBoxT1, 2);
    }
    juce::String roomShortText() const
    {
        if (rtBoxT1 > rtBoxT0) return "Room tone: " + roomSpanText() + (rtQuietest ? " (quietest moments)" : "");
        if (ctx.roomSource.isNotEmpty()) return "Room tone: the project's";
        return "Room tone: none yet";
    }
    juce::String roomText() const
    {
        if (rtBoxT1 > rtBoxT0) return "Room tone: the " + roomSpanText() + " of this picture" + (rtQuietest ? ", learned from its quietest moments (the notes left out)" : "");
        if (ctx.roomSource.isNotEmpty()) return "Room tone: the project's (" + ctx.roomSource + ")";
        return "Room tone: none yet - draw a box over room only and press L, or use the edit window's Room tone button";
    }
    void roomFromBox()
    {
        if (! scanned || running || ! view.hasSelection()) return;
        rtBoxT0 = view.selT0(); rtBoxT1 = view.selT1(); rtSpans = view.selectionSpans(); rtQuietest = quietBox.getToggleState();     // (3.29.0: every stretch of the selection; the quietest moments)
        double total = 0.0; for (auto& sp : rtSpans) total += sp.second - sp.first;
        if (total < 0.5) { status.setText ("The room-tone box is too short: make it at least half a second long (a second or more is better).", juce::dontSendNotification); rtBoxT0 = rtBoxT1 = -1.0; rtSpans.clear(); refreshControls(); return; }     // (what learnDenoise needs: a shorter box was taken here, then refused at Write back)
        status.setText (roomText() + ". Now draw a box round the noise to take out and press Denoise (N).", juce::dontSendNotification);
        refreshControls();
    }      // one window for both: ReTouch and De-click share the picture, the box and the list of fixes
    void updateSurround() { const bool lr = dirBox.getSelectedId() != 2; view.setSurround (scanned && ! consonantMethod(), surround.getValue(), dirBox.getSelectedId() - 1, lr ? weight.getValue() / 100.0 : 0.5); }
    bool consonantMethod() const { return methodBox.getSelectedId() == 3; }
    bool lookingAcross() const { return dirBox.getSelectedId() != 2 || consonantMethod(); }
    /** The changes that make up the repaired sound: everything accepted so far, plus the change being previewed. */
    std::vector<RepairOp> currentOps() const
    {
        return committed;
    }
    bool haveRepair() const { return ! committed.empty(); }
    void invalidate()
    {
        if (running) { changedWhileRunning = true; return; }
        dropPendingPrepared();
    }
    /** Back to the original sound, and the repaired files thrown away (they no longer match the settings). */
    void dropPrepared()
    {
        if (applied) { if (ctx.stop) ctx.stop(); if (ctx.unapply) ctx.unapply(); applied = false; }
        if (prepared) { if (ctx.discard) ctx.discard(); prepared = false; }
        preparedOps = 0;
    }
    /** Files that include a change which is not accepted (yet) are dropped; files that match the accepted changes are kept. */
    void dropPendingPrepared() { if (prepared && preparedOps != committed.size()) dropPrepared(); }
    bool isPlayingNow() const { return ctx.position && ctx.position() >= 0.0; }
    void tick()
    {
        const double t = ctx.position ? ctx.position() : -1.0;
        view.setPlayhead (t);
        playBtn.setButtonText (t >= 0.0 ? "Restart" : "Play");
    }
    struct Ticker : juce::Timer { RepairDialog* owner = nullptr; void timerCallback() override { if (owner != nullptr) owner->tick(); } } ticker;
    /** What is played: the part of the picture that is in view (zoom in to hear less, out to hear more), not just the box. */
    void playRange()
    {
        if (ctx.play) ctx.play (view.playFrom(), juce::jmin (seconds(), view.visT1()), loopBtn.getToggleState());
    }
    void playOriginal()
    {
        if (applied) { if (ctx.stop) ctx.stop(); if (ctx.unapply) ctx.unapply(); applied = false; }
        playRange();
        status.setText ("Playing the ORIGINAL sound, the part you can see. The playhead is on the picture.", juce::dontSendNotification);
        refreshControls();
    }
    void playRepaired()
    {
        if (! applied)
        {
            if (! ctx.apply || ! ctx.apply()) { status.setText ("The audio changed while the repair was being made, so it cannot be played. Press Fix again.", juce::dontSendNotification); prepared = false; preparedOps = 0; refreshControls(); return; }
            applied = true;
        }
        playRange();
        status.setText ("Playing the REPAIRED sound (through the repair). " + keepName() + " keeps it, Undo takes the last fix off, or draw another box and ReTouch / De-click again.", juce::dontSendNotification);
        refreshControls();
    }
    void startRepairedPlayback()
    {
        const auto ops = currentOps();
        if (running || ops.empty()) return;
        if (prepared && preparedOps == ops.size()) { playRepaired(); return; }
        dropPrepared();
        auto prep = ctx.prepare;
        status.setText ("Making the repaired sound ready to listen to...", juce::dontSendNotification);
        previewBtn.setEnabled (false); declickBtn.setEnabled (false); finishBtn.setEnabled (false); revertBtn.setEnabled (false); compareBtn.setEnabled (false); playBtn.setEnabled (false);
        juce::Component::SafePointer<RepairDialog> self (this);
        const auto n = ops.size();
        runJob ([prep, ops] (AudioJob& j) { return prep (ops, j); },
                [self, n] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isNotEmpty()) { self->status.setText (err == "Cancelled." ? juce::String ("Cancelled.") : err, juce::dontSendNotification); self->refreshControls(); return; }
                    self->prepared = true; self->preparedOps = n;
                    if (self->changedWhileRunning) { self->changedWhileRunning = false; self->invalidate(); return; }
                    self->playRepaired();
                });
    }
    void playPressed()
    {
        if (! scanned || running) return;
        if (ctx.stop) ctx.stop();
        if (hearBox.getSelectedId() == 2)
        {
            if (haveRepair()) { startRepairedPlayback(); return; }
            status.setText ("There is no repair to listen to yet: press ReTouch or De-click first. Playing the original.", juce::dontSendNotification);
        }
        playOriginal();
    }
    void refreshControls()
    {
        for (auto* c : tabControls (tab)) c->setVisible (true);                    // (the rules below hide what the chosen method does not use)
        updateSurround();
        const bool cons = consonantMethod();
        const bool att = methodBox.getSelectedId() == 1 || cons;
        for (auto* c : std::initializer_list<juce::Component*> { &strength, &strengthCap, &methodBox }) c->setVisible (true);
        for (auto* c : std::initializer_list<juce::Component*> { &dirBox, &surround, &surroundCap }) c->setVisible (! cons);      // a stray consonant: always the sound before / after, its own search length
        weightCap.setText (cons ? "Side (50 = auto)" : "Before <-> After", juce::dontSendNotification);
        bandsBox.setVisible (att); multiBtn.setVisible (att); linkBtn.setVisible (att);
        weight.setVisible (lookingAcross()); weightCap.setVisible (lookingAcross());
        for (auto* b : { &sideL, &sideC, &sideR }) b->setVisible (lookingAcross());               // 3.33.0: L C R go with the slider
        sens.setVisible (true); sensCap.setVisible (true);
        for (int t = 0; t < kTabCount; ++t) if (t != tab) for (auto* c : tabControls (t)) c->setVisible (false);      // only the tab that is showing
        if (view.hasSelection())
            info.setText ("Selected: " + (view.boxCount() > 1 ? juce::String (view.boxCount()) + " boxes, " : juce::String()) + specTime (view.selT0(), 3) + " - " + specTime (view.selT1(), 3) + "  ("
                          + juce::String ((view.selT1() - view.selT0()) * 1000.0, 0) + " ms),  " + juce::String ((int) view.selF0()) + " - " + juce::String ((int) view.selF1()) + " Hz"
                          + (committed.empty() ? juce::String() : juce::String ("      ") + juce::String ((int) history.size()) + " fix(es) made"), juce::dontSendNotification);
        else info.setText (juce::String ("Nothing selected")
                           + (committed.empty() ? juce::String() : juce::String ("      ") + juce::String ((int) history.size()) + " fix(es) made"), juce::dontSendNotification);
        const bool ready = scanned && ! running;
        playBtn.setEnabled (ready); stopBtn.setEnabled (ready); loopBtn.setEnabled (ready); hearBox.setEnabled (ready);
        previewBtn.setEnabled (ready && view.hasSelection()); declickBtn.setEnabled (ready); gainBtn.setEnabled (ready && view.hasSelection());
        denoiseBtn.setEnabled (ready && view.hasSelection() && haveRoomTone()); roomBtn.setEnabled (ready && view.hasSelection());
        dehumBtn.setEnabled (ready && humPanel.anyOn()); humPanel.setReady (ready, view.hasSelection());                     // 3.44.0
        roomCap.setText (roomShortText(), juce::dontSendNotification); roomCap.setTooltip (roomText());
        finishBtn.setEnabled (ready && (haveRepair() || ctx.pairWritten != nullptr));
        revertBtn.setEnabled (ready && ! history.empty());
        compareBtn.setEnabled (ready && ! history.empty());
        compareBtn.setButtonText (showingAfter ? "Show before" : "Show fixed");
        refreshConsonantList();
        const bool canCopy = ready && view.hasSelection() && ctx.readChannels != nullptr && ctx.keepConsonant != nullptr;
        copyBtn.setEnabled (canCopy); copyMoveBtn.setEnabled (canCopy);
        const bool haveSel = selectedConsonant() != nullptr;
        consBox.setEnabled (! consList.empty()); renameBtn.setEnabled (haveSel && ctx.renameConsonant != nullptr); forgetBtn.setEnabled (haveSel && ctx.forgetConsonant != nullptr);
        pasteBtn.setEnabled (ready && haveSel);
        const bool adj = scanned && (running ? pasteRunning : lastIsPaste());
        for (auto* b : { &nudgeL5, &nudgeL1, &nudgeR1, &nudgeR5 }) b->setEnabled (adj);
        nudgeRead.setText (nudgeText(), juce::dontSendNotification);
        resized();                                   // (what is shown decides how the boxes are packed)
    }
    /** The settings as they are now (and the part of the area they apply to). what: 0 = ReTouch (or Gain), 1 = De-click, 2 = Denoise, 3 = De-hum. */
    FixSpec buildSpec (juce::int64& t0, juce::int64& t1, int what) const
    {
        FixSpec spec; t0 = 0; t1 = ctx.length;
        if (view.hasSelection()) { t0 = (juce::int64) std::llround (view.selT0() * ctx.rate); t1 = (juce::int64) std::llround (view.selT1() * ctx.rate); }
        if (what == 3)
        {
            spec.kind = FixSpec::Kind::Dehum; spec.hum = humPanel.params();
            spec.f0 = view.hasSelection() ? view.selF0() : 0.0; spec.f1 = view.hasSelection() ? view.selF1() : ctx.rate * 0.5;
        }
        else if (what == 2)
        {
            spec.kind = FixSpec::Kind::Denoise; spec.f0 = view.selF0(); spec.f1 = view.selF1();
            spec.dn.amountDb = amount.getValue(); spec.dn.residualDb = residual.getValue(); spec.dn.quietest = rtBoxT1 > rtBoxT0 && rtQuietest;
            spec.dnBoxT0 = rtBoxT1 > rtBoxT0 ? rtBoxT0 : -1.0; spec.dnBoxT1 = rtBoxT1 > rtBoxT0 ? rtBoxT1 : -1.0;
            if (rtBoxT1 > rtBoxT0 && rtSpans.size() > 1) spec.dnSpans = rtSpans;
        }
        else if (what == 0)
        {
            spec.kind = FixSpec::Kind::Patch; spec.f0 = view.selF0(); spec.f1 = view.selF1(); spec.sensitivity = sens.getValue();         // (the click sensitivity also decides how readily lost samples (a digital dropout) in a small box are found)
            spec.repair.mode = methodBox.getSelectedId() == 2 ? RepairMode::Replace : RepairMode::Attenuate;
            spec.repair.consonant = consonantMethod();
            spec.repair.bands = bandsBox.getSelectedId(); spec.repair.multiRes = multiBtn.getToggleState(); spec.repair.linkChannels = linkBtn.getToggleState();
            spec.repair.strength = strength.getValue() / 100.0;
            spec.repair.direction = spec.repair.consonant ? RepairDirection::LeftRight : (RepairDirection) (dirBox.getSelectedId() - 1);
            spec.repair.contextPct = surround.getValue();
            spec.repair.weighting = weight.getValue() / 100.0;
            if (gainNext) { spec.repair.mode = RepairMode::Gain; spec.repair.gainDb = gainNextDb; spec.repair.strength = 1.0; spec.repair.consonant = false; }
        }
        else { spec.kind = FixSpec::Kind::Declick; spec.sensitivity = sens.getValue(); }
        return spec;
    }
    double seconds() const { return (double) ctx.length / ctx.rate; }
    juce::String keepName() const { return ctx.acceptText.isNotEmpty() ? ctx.acceptText : juce::String ("Write back to file"); }     // (the keep button, as it is named)
    /** 3.44.0: the box as the hum finder reads it - the picture's tracks (one mono channel each) over the selection's time span (all of it with no box). */
    dehum::Source humSource (double& f0, double& f1) const
    {
        juce::int64 t0 = 0, t1 = ctx.length;
        if (view.hasSelection()) { t0 = (juce::int64) std::llround (view.selT0() * ctx.rate); t1 = (juce::int64) std::llround (view.selT1() * ctx.rate); }
        f0 = view.hasSelection() ? view.selF0() : 20.0; f1 = view.hasSelection() ? view.selF1() : ctx.rate * 0.5;
        auto tracks = origTracks;                                                // (kept alive by the source's lambda)
        dehum::Source src; src.channels = tracks != nullptr ? (int) tracks->size() : 0; src.length = juce::jmax ((juce::int64) 0, t1 - t0);
        src.read = [tracks, t0] (int ch, juce::int64 at, int n, float* dst)
        {
            if (tracks == nullptr || ch < 0 || ch >= (int) tracks->size()) return false;
            const auto& x = (*tracks)[(size_t) ch];
            for (int i = 0; i < n; ++i) { const juce::int64 j = t0 + at + i; dst[i] = j >= 0 && j < (juce::int64) x.size() ? x[(size_t) j] : 0.0f; }
            return true;
        };
        return src;
    }
    /** H: looks for the hums in the box (on a job thread: a long box is many frames) and lists them in the De-hum tab. */
    void findHums()
    {
        if (! scanned || running || origTracks == nullptr) return;
        double f0 = 0.0, f1 = 0.0; auto src = humSource (f0, f1); const double rate = ctx.rate;
        const double secs = (double) src.length / rate;
        auto found = std::make_shared<dehum::Found>();
        status.setText ("Looking for hums in " + juce::String (secs, 1) + " s, " + juce::String ((int) f0) + " - " + juce::String ((int) f1) + " Hz...", juce::dontSendNotification);
        juce::Component::SafePointer<RepairDialog> self (this);
        runJob ([src, rate, f0, f1, found] (AudioJob& j) -> juce::String { *found = dehum::find (src, rate, f0, f1, [&j] { return j.cancelled(); }); return {}; },
                [self, found, secs, f0, f1] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isNotEmpty()) { self->status.setText (err, juce::dontSendNotification); self->refreshControls(); return; }
                    self->humPanel.setHums (found->hums, found->note, secs, f0, f1);
                    self->status.setText (found->hums.empty() ? found->note : juce::String ((int) found->hums.size()) + (found->hums.size() == 1 ? " hum found" : " hums found") + " (loudest first). Untick what is music, then press De-hum; Play to listen, Undo if it did not work.", juce::dontSendNotification);
                    self->refreshControls();
                });
    }
    /** A pitch typed in the De-hum tab (k < 0: Add; else the k-th hum's): measured in the box near what was typed, then listed. */
    void measureHum (int k, double hz)
    {
        if (! scanned || running || origTracks == nullptr || hz <= 0.0) return;
        double f0 = 0.0, f1 = 0.0; auto src = humSource (f0, f1); const double rate = ctx.rate;
        auto out = std::make_shared<dehum::Hum>();
        juce::Component::SafePointer<RepairDialog> self (this);
        runJob ([src, rate, hz, out] (AudioJob& j) -> juce::String { *out = dehum::measureAt (src, rate, hz, juce::jmax (5.0, 0.02 * hz), [&j] { return j.cancelled(); }); return {}; },
                [self, out, k, hz] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isNotEmpty()) { self->status.setText (err, juce::dontSendNotification); self->refreshControls(); return; }
                    if (k < 0) self->humPanel.add (*out); else self->humPanel.set (k, *out);
                    self->status.setText (out->aboveDb > 0.0 ? juce::String (hz, 1) + " Hz typed: a steady tone at " + juce::String (out->hz, 1) + " Hz, " + out->loudness() + ". Ticked; press De-hum to take it out."
                                                             : juce::String (hz, 1) + " Hz typed: nothing steady stands out within 2 % of it, so exactly that pitch is taken out.", juce::dontSendNotification);
                    self->refreshControls();
                });
    }
    /** G: the gain set in the panel applied to the box (3.29.0: no pop-up - the slider sits in the panel), a fix like ReTouch (Undo takes it off). */
    void applyGain()
    {
        if (! scanned || running || ! view.hasSelection()) return;
        const double db = gainSlider.getValue();
        if (std::abs (db) < 0.05) { status.setText ("Gain 0 dB: nothing to change. Set the amount beside Gain first.", juce::dontSendNotification); return; }
        gainNext = true; gainNextDb = db <= -59.95 ? -200.0 : db;
        preview (0);
        gainNext = false;
    }
    void preview (int what)
    {
        if (! scanned || running || origTracks == nullptr) return;
        if (what == 2 && ! haveRoomTone()) { status.setText ("No room tone: draw a box over a part that is only room and press L, or use the edit window's Room tone button first.", juce::dontSendNotification); return; }
        if (what == 3 && ! humPanel.anyOn()) { status.setText (humPanel.hasScanned() ? "No hum is ticked: tick one in the list, or add one by hand." : "Press Find hums (H) first: it lists the hums in the box for De-hum to take out.", juce::dontSendNotification); return; }
        if (what != 1 && what != 3 && ! view.hasSelection()) return;               // (3.30.1: only De-click works without a box; 3.44.0: and De-hum, over everything loaded)
        juce::int64 t0 = 0, t1 = 0; const auto spec = buildSpec (t0, t1, what);
        startFixes (opsOverSelection (spec, t0, t1));
    }
    /** 3.29.0: one change per rectangle of the selection (several boxes, or a box with parts taken out), the same settings each; no selection: the whole stretch.
        3.30.1: De-click pays no heed to pitch, so it gets one change per STRETCH OF TIME the selection covers (rectangles stacked in pitch over the same
        stretch used to de-click it once each, on the already mended sound). And whether a ReTouch is "a box round a click" (which looks for lost samples and
        puts them back instead of repairing) is decided from the whole shape that was drawn, not from each rectangle: a sliver left by a cut-out is not a click box. */
    std::vector<RepairOp> opsOverSelection (const FixSpec& spec, juce::int64 t0, juce::int64 t1) const
    {
        std::vector<RepairOp> group;
        if (! view.hasSelection()) { group.push_back ({ spec, t0, t1 }); return group; }
        if (spec.kind == FixSpec::Kind::Declick || (spec.kind == FixSpec::Kind::Dehum && ! view.brushed()))     // (3.44.0: De-hum takes its own tones, whatever the box's pitch range: one change per stretch of time; a brush shape keeps its mask)
        {
            for (auto& sp : view.selectionSpans()) group.push_back ({ spec, (juce::int64) std::llround (sp.first * ctx.rate), (juce::int64) std::llround (sp.second * ctx.rate) });
            return group;
        }
        const bool clickBox = ! view.brushed() && audioops::findsLost (spec, ctx.rate, (juce::int64) std::llround (view.selT0() * ctx.rate), (juce::int64) std::llround (view.selT1() * ctx.rate));
        for (auto& r : clickBox ? view.hardParts() : view.selectionParts())     // (3.33.0: a rectangle, or a piece of a brush shape with its mask; 3.34.0: or feathered - a box round a click is mended exactly)
        {
            auto sp = spec; sp.f0 = r.f0; sp.f1 = r.f1; sp.mask = r.mask; sp.maskLead = (juce::int64) std::llround (r.lead * ctx.rate);
            if (! clickBox) { sp.lostKnown = true; sp.lost.clear(); }           // (nothing to look for: the shape is no click box, whatever this piece of it is)
            group.push_back ({ sp, (juce::int64) std::llround (r.t0 * ctx.rate), (juce::int64) std::llround (r.t1 * ctx.rate) });
        }
        return group;
    }
    /** Makes one change on a job thread - on the picture's sound as it is now (every fix so far), track by track - and keeps it as the next fix (Undo takes it
        off). 'placed': a pasted consonant, where it was first put (a nudge keeps it, so the window can say how far it has been moved). */
    void startFix (const FixSpec& spec, juce::int64 t0, juce::int64 t1, juce::int64 placed = -1)
    {
        startFixes ({ RepairOp { spec, t0, t1, placed } });
    }
    /** 3.29.0: a group of changes made together, as one fix (one Undo step): one per rectangle of a selection made of several boxes, each with its own time span and
        pitch range and the same settings. Every op has the same kind. */
    void startFixes (const std::vector<RepairOp>& group)
    {
        if (! scanned || running || origTracks == nullptr || group.empty()) return;
        dropPendingPrepared();
        const auto spec = group.front().spec;
        pvSpec = spec; pvT0 = group.front().t0; pvT1 = group.back().t1; pvPlaced = group.front().placed;
        for (auto& o : group) { pvT0 = juce::jmin (pvT0, o.t0); pvT1 = juce::jmax (pvT1, o.t1); }
        pasteRunning = spec.kind == FixSpec::Kind::Paste;
        auto src = origTracks; const double rate = ctx.rate;
        const auto trackIds = ctx.trackIds;
        struct Out { MergedSpectrogram spec; std::shared_ptr<std::vector<std::vector<float>>> tracks; std::vector<RepairOp> ops; SpectralRepair::DeclickReport rep { 0, 0, 0, 0 }; };
        auto out = std::make_shared<Out>();
        status.setText (spec.kind == FixSpec::Kind::Denoise ? "Denoising the box..." : spec.kind == FixSpec::Kind::Paste ? "Pasting the consonant..." : spec.kind == FixSpec::Kind::Dehum ? "Taking the hums out..." : "Fixing...", juce::dontSendNotification);
        previewBtn.setEnabled (false); declickBtn.setEnabled (false); denoiseBtn.setEnabled (false); dehumBtn.setEnabled (false); finishBtn.setEnabled (false); revertBtn.setEnabled (false); compareBtn.setEnabled (false);
        juce::Component::SafePointer<RepairDialog> self (this);
        const int detailN = detailSize();
        auto roomMono = std::make_shared<std::vector<denoise::Profile>> (ctx.roomMono);
        auto firstTracks = history.empty() ? origTracks : history.front().tracks;   // the sound as it was loaded, before any of these fixes
        runJob ([src, firstTracks, out, group, spec, rate, detailN, roomMono, trackIds] (AudioJob& j) -> juce::String
                {
                    auto tracks = std::make_shared<std::vector<std::vector<float>>> (*src);       // from the last accepted state
                    // a denoise: each picture track's room tone - the box of this picture, learned from the track as it was loaded (as the files are, when they are
                    // written: once a fix had denoised the room tone itself, the picture learned a profile far too low from it), or the project's for that track
                    std::vector<std::shared_ptr<const std::vector<denoise::Profile>>> dnProf;
                    if (spec.kind == FixSpec::Kind::Denoise)
                        for (size_t i = 0; i < tracks->size(); ++i)
                        {
                            denoise::Profile pr;
                            if (spec.dnBoxT1 > spec.dnBoxT0)
                            {
                                const auto& x = firstTracks != nullptr && i < firstTracks->size() ? (*firstTracks)[i] : (*tracks)[i];
                                std::vector<std::vector<float>> regions;                                     // (3.29.0: every stretch of the selection)
                                for (auto& sp : spec.roomSpans())
                                {
                                    const auto a = juce::jlimit ((juce::int64) 0, (juce::int64) x.size(), (juce::int64) std::llround (sp.first * rate)), b = juce::jlimit ((juce::int64) 0, (juce::int64) x.size(), (juce::int64) std::llround (sp.second * rate));
                                    if (b > a) regions.push_back (std::vector<float> (x.begin() + a, x.begin() + b));
                                }
                                if (! regions.empty()) pr = denoise::learnProfile (regions, rate, spec.dn);
                            }
                            else if (i < roomMono->size()) pr = (*roomMono)[i];
                            if (! pr.valid()) return "There is no room tone for track " + juce::String ((int) i + 1) + " (learn it with the edit window's Room tone button, or mark a box of this picture with L).";
                            dnProf.push_back (std::make_shared<const std::vector<denoise::Profile>> (std::vector<denoise::Profile> { pr }));
                        }
                    out->ops = group;
                    for (size_t gi = 0; gi < group.size(); ++gi)                                  // (3.29.0: each rectangle of the selection in turn, on the result of the last)
                    {
                        const juce::int64 t0 = group[gi].t0, t1 = group[gi].t1;
                        // lost samples (a digital jump where a block of samples went missing) are one event on every track: they are found on all the tracks together, and the
                        // list goes with the fix, so the files are written with exactly the same mend
                        auto fix = group[gi].spec;
                        if (audioops::findsLost (fix, rate, t0, t1) && ! tracks->empty())
                        {
                            const long L = (long) (*tracks)[0].size();
                            fix.lost = audioops::findLost (*tracks, rate, (long) juce::jlimit ((juce::int64) 0, (juce::int64) L, t0), (long) juce::jlimit ((juce::int64) 0, (juce::int64) L, t1), fix.sensitivity);
                            for (auto& l : fix.lost) l.at += juce::jlimit ((juce::int64) 0, (juce::int64) L, t0) - t0;          // (from t0 itself)
                            fix.lostKnown = true;
                        }
                        // a stray consonant: which side of the box the main consonant is on is ONE answer for all the tracks (found on all of them together); it goes with
                        // the fix so the files are written the same way
                        if (fix.kind == FixSpec::Kind::Patch && fix.repair.mode == RepairMode::Attenuate && fix.repair.consonant && std::abs (fix.repair.weighting - 0.5) < 0.04 && ! tracks->empty())
                        {
                            const long L = (long) (*tracks)[0].size();
                            std::vector<const std::vector<float>*> tp; for (auto& t : *tracks) tp.push_back (&t);
                            const auto d = SpectralRepair::consonantSides (tp, rate, (long) juce::jlimit ((juce::int64) 0, (juce::int64) L, t0), (long) juce::jlimit ((juce::int64) 0, (juce::int64) L, t1), fix.f0, fix.f1);
                            fix.repair.consonantBefore = d.first; fix.repair.consonantEdge = d.second;
                        }
                        // 3.30.0: the tracks side by side, on as many threads as the machine has to spare (each track's audio, settings and declick report are its own)
                        std::atomic<int> doneTracks { 0 }; std::mutex repLock;
                        const int threads = workers::threadsForMemory ((int) tracks->size(), (t1 - t0 + (juce::int64) (20.0 * rate)) * 40);     // (3.30.1: within the memory)
                        const auto err = workers::forEach ((int) tracks->size(), [&] (int ti) -> juce::String
                        {
                            const size_t i = (size_t) ti;
                            std::vector<std::vector<float>> ch; ch.push_back (std::move ((*tracks)[i]));
                            auto fi = fix; if (i < dnProf.size()) fi.dnProfiles = dnProf[i];
                            if (fi.kind == FixSpec::Kind::Paste) audioops::resolvePaste (fi, i < trackIds.size() ? trackIds[i] : juce::Uuid::null());     // (the picture's row: the track's channels, averaged)
                            audioops::applyFix (ch, rate, (long) t0, (long) t1, fi, (long) (0.02 * rate));
                            (*tracks)[i] = std::move (ch[0]);
                            if (fix.kind == FixSpec::Kind::Declick) { const auto r = SpectralRepair::lastDeclickReport(); std::lock_guard<std::mutex> g (repLock); out->rep.mended += r.mended; out->rep.lines += r.lines; out->rep.linesMended += r.linesMended; out->rep.linesLeft += r.linesLeft; }
                            j.setProgress (((float) gi + (float) (doneTracks.fetch_add (1) + 1) / (float) tracks->size()) / (float) group.size());
                            return juce::String();
                        }, [&j] { return j.cancelled(); }, threads);
                        if (err.isNotEmpty()) return err;
                        fix.dnProfiles = nullptr;                                                     // (the profiles are not part of the fix that is kept)
                        fix.pasteChannels = nullptr;                                                  // (nor is the one track's part of a pasted consonant: each file gets its own)
                        out->ops[gi].spec = fix;
                    }
                    out->spec = SpectralRepair::spectrogram (*tracks, rate, 1600, detailN);
                    out->tracks = tracks;
                    return {};
                },
                [self, out] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isNotEmpty()) { self->nextNote.clear(); self->pendingNudge = 0; self->pendingLevel = false; self->status.setText (err, juce::dontSendNotification); self->refreshControls(); return; }
                    // the fix is kept straight away (it stays temporary until "Write back to file"); Undo takes it off again - the whole group at once
                    double from = 1.0e30, to = -1.0e30;                                  // 3.38.0: where the sound changed (the boxes, feathering included, and a fix's cross-fades)
                    for (auto& o : out->ops) { from = juce::jmin (from, (double) o.t0 / self->ctx.rate); to = juce::jmax (to, (double) o.t1 / self->ctx.rate); }
                    from -= 0.05; to += 0.05;
                    const bool shownCurrent = self->showingAfter || self->history.empty();      // (the picture showed the sound this fix was made on - not Compare's "before")
                    self->history.push_back ({ self->origTracks, self->origSpec, (int) out->ops.size(), from, to });
                    self->pvSpec = out->ops.back().spec;                                 // (with the lost samples found on all the tracks)
                    for (auto& o : out->ops) self->committed.push_back (o);
                    const auto& fixRef = out->ops.back().spec;
                    self->origTracks = out->tracks;
                    self->origSpec = std::make_shared<MergedSpectrogram> (out->spec);
                    self->view.setData (std::move (out->spec), self->seconds(), true, shownCurrent ? from : 0.0, shownCurrent ? to : -1.0);   // (else: where the last fix was too)
                    self->showingAfter = true;
                    self->dropPendingPrepared();
                    self->changedWhileRunning = false;
                    self->hearBox.setSelectedId (2, juce::dontSendNotification);
                    juce::String lostNote = self->nextNote; self->nextNote.clear();
                    for (auto& o : out->ops) for (auto& l : o.spec.lost)
                        lostNote << "Digital dropout at " << juce::String ((double) (o.t0 + l.at) / self->ctx.rate, 3) << " s: " << l.count << " samples were missing on every track; put back. ";
                    if (fixRef.kind == FixSpec::Kind::Patch && fixRef.repair.mode == RepairMode::Gain)
                        lostNote << "Gain " << (fixRef.repair.gainDb <= -120.0 ? juce::String ("-inf") : (fixRef.repair.gainDb > 0 ? "+" : "") + juce::String (fixRef.repair.gainDb, 1)) << " dB on the box. ";
                    if (fixRef.kind == FixSpec::Kind::Denoise)
                        lostNote << "Denoised the box (" << (int) fixRef.f0 << " - " << (int) fixRef.f1 << " Hz, residual " << juce::String (fixRef.dn.residualDb, 0) << " dB). ";
                    if (fixRef.kind == FixSpec::Kind::Dehum)
                    {
                        lostNote << "De-hum: " << fixRef.hum.summary() << " taken out of " << specTime ((double) self->pvT0 / self->ctx.rate, 2) << " - " << specTime ((double) self->pvT1 / self->ctx.rate, 2) << " on every track. ";
                        self->humPanel.setStatus ("Taken out: " + fixRef.hum.summary() + ". Play to listen; Undo takes it off; Find hums again shows what is left.");
                    }
                    if (fixRef.kind == FixSpec::Kind::Declick)
                    {
                        const auto& r = out->rep;
                        lostNote << "De-click, all tracks: " << r.linesMended << " vertical line(s) seen in the picture and mended whole, " << r.mended << " click(s) found in the waveform and mended"
                                 << (r.linesLeft > 0 ? ", " + juce::String (r.linesLeft) + " line(s) STILL THERE (try a higher sensitivity, or ReTouch a box round each)" : juce::String (", none left in the picture")) << ". ";
                    }
                    if (fixRef.kind == FixSpec::Kind::Paste && fixRef.paste != nullptr)
                    {
                        const auto missing = consonants::tracksMissing (*fixRef.paste, self->ctx.trackIds);
                        int here = 0; for (auto& id : self->ctx.trackIds) if (fixRef.paste->forTrack (id) != nullptr) ++here;
                        lostNote << "Consonant pasted at " << juce::String ((double) (self->pvT0 + fixRef.paste->lead) / self->ctx.rate, 3) << " s on " << here << (here == 1 ? " track" : " tracks")
                                 << (std::abs (fixRef.pasteGainDb) >= 0.05 ? ", " + juce::String (fixRef.pasteGainDb, 1) + " dB" : juce::String())
                                 << (self->pvPlaced >= 0 && self->pvT0 != self->pvPlaced ? ", nudged " + self->nudgeText() : juce::String()) << ". ";
                        int none = 0; for (auto& id : self->ctx.trackIds) if (fixRef.paste->forTrack (id) == nullptr) ++none;
                        if (none > 0) lostNote << none << (none == 1 ? " track of this window was" : " tracks of this window were") << " not in it and get nothing. ";
                        if (! missing.isEmpty()) lostNote << "(It also has " << missing.joinIntoString (", ") << ", not in this window.) ";
                        lostNote << "Press Play to listen; nudge it with comma / full stop (1 ms) or Shift with them (5 ms), or change Level; Undo takes it off. ";
                        self->status.setText (lostNote, juce::dontSendNotification);
                        self->refreshControls();
                        self->followPending();
                        return;
                    }
                    if (fixRef.kind == FixSpec::Kind::Patch && fixRef.repair.consonant)
                    {
                        const auto& r = fixRef.repair;
                        if (r.consonantBefore < 0.0) lostNote << "Stray consonant: matched to the side you chose with Before / After. ";
                        else if (r.consonantEdge > 0) lostNote << "Stray consonant: the main one is just AFTER the box, so every track was matched to the sound before it. ";
                        else if (r.consonantEdge < 0) lostNote << "Stray consonant: the main one is just BEFORE the box, so every track was matched to the sound after it. ";
                        else lostNote << "Stray consonant: no main consonant right next to the box, so both sides were used (" << juce::roundToInt (r.consonantBefore * 100.0) << " % before). ";
                    }
                    self->status.setText (lostNote + "Fix " + juce::String ((int) self->history.size()) + (self->ctx.acceptText.isEmpty() ? " done (not written yet)" : " done (not kept yet)")
                                          + ". Press Play to listen to it. If it worked, draw a box round the next problem and press ReTouch or De-click again, or press " + self->keepName() + ". If not, press Undo and try again.", juce::dontSendNotification);
                    self->refreshControls();
                });
        for (auto* b : { &nudgeL5, &nudgeL1, &nudgeR1, &nudgeR5 }) b->setEnabled (pasteRunning);     // (a nudge pressed now follows when this one is made)
    }
    int detailSize() const { return juce::jlimit (0, 2, detailBox.getSelectedId() - 1); }
    /** Redraws the picture of the sound as it is now with another low-end detail (a longer or shorter analysis window). */
    void rescan()
    {
        if (! scanned || running || origTracks == nullptr) return;
        auto src = origTracks; const double rate = ctx.rate; const int n = detailSize();
        auto out = std::make_shared<MergedSpectrogram>();
        status.setText ("Redrawing with another low-end detail...", juce::dontSendNotification);
        juce::Component::SafePointer<RepairDialog> self (this);
        runJob ([src, out, rate, n] (AudioJob&) -> juce::String { *out = SpectralRepair::spectrogram (*src, rate, 1600, n); return {}; },
                [self, out] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isEmpty() && out->frames > 0)
                    {
                        self->origSpec = std::make_shared<MergedSpectrogram> (*out);
                        self->view.setData (std::move (*out), self->seconds(), true);
                        self->showingAfter = true;
                        self->status.setText ("Redrawn. Blended: fine bass below, sharp clicks above. Choose \"off\" for the sharpest timing everywhere.", juce::dontSendNotification);
                    }
                    self->refreshControls();
                });
    }
    /** The audio the picture shows now (the fixed sound, or the sound before the last fix). */
    std::shared_ptr<std::vector<std::vector<float>>> shownTracks() const
    {
        if (! showingAfter && ! history.empty() && history.back().tracks != nullptr) return history.back().tracks;
        return origTracks;
    }
    /** Makes the sharp picture of what is in view on its own thread (the previous request, if still running, is dropped). */
    void startDetail (const PictureRequest& rq)
    {
        auto tr = shownTracks(); if (tr == nullptr || tr->empty()) return;
        if (detailCancel != nullptr) detailCancel->store (true);
        auto cancel = std::make_shared<std::atomic<bool>> (false); detailCancel = cancel;
        const double rate = ctx.rate; juce::Component::SafePointer<RepairDialog> self (this);
        std::thread ([tr, rq, rate, cancel, self]
        {
            try                                                                  // (nothing may escape a thread: out of memory leaves the quick picture, nothing worse)
            {
                picture::RenderPart part; part.c0 = rq.c0; part.c1 = rq.c1;             // (3.38.0: a part, when only some columns are out of date)
                auto pic = picture::render (*tr, rate, rq.t0, rq.t1, rq.w, rq.h, rq.axis, rq.opt, [cancel] { return cancel->load(); }, {}, part);
                pic.id = rq.id;
                if (cancel->load()) return;
                juce::MessageManager::callAsync ([self, p = std::make_shared<DetailPicture> (std::move (pic))] { if (self != nullptr) self->view.setDetail (std::move (*p)); });
            }
            catch (...) {}
        }).detach();
    }
    void openSpectroSettings();
    void toggleCompare()
    {
        if (history.empty() || origSpec == nullptr || history.back().spec == nullptr) return;
        showingAfter = ! showingAfter;
        view.setData (showingAfter ? *origSpec : *history.back().spec, seconds(), true, history.back().from, history.back().to);
        status.setText (showingAfter ? "Showing the fixed sound." : "Showing the sound before the last fix.", juce::dontSendNotification);
        refreshControls();
    }
    /** Undo: takes the last fix off again (as many times as there are fixes). */
    void revert()
    {
        if (history.empty() || running) return;
        if (ctx.stop) ctx.stop();
        if (humPanel.hasScanned()) humPanel.setStatus ("Undo: the sound is as it was before the last fix. Press Find hums again to see the hums as they are now.");     // (3.44.0: the list may be stale)
        origTracks = history.back().tracks; origSpec = history.back().spec; const int n = juce::jmax (1, history.back().ops);
        const double from = showingAfter ? history.back().from : 0.0, to = showingAfter ? history.back().to : -1.0;     // (showing the sound before it already: anywhere)
        history.pop_back();
        for (int k = 0; k < n && ! committed.empty(); ++k) committed.pop_back();
        dropPendingPrepared();
        showingAfter = true;
        if (committed.empty()) hearBox.setSelectedId (1, juce::dontSendNotification);
        view.setData (*origSpec, seconds(), true, from, to);
        status.setText (committed.empty() ? "Undone: back to the original. Nothing is changed." : "Last fix undone (" + juce::String ((int) history.size()) + " fix(es) left).", juce::dontSendNotification);
        refreshControls();
    }
    /** Finish: everything between the marks is rendered again in one go, with every accepted change, and put back. */
    void finish()
    {
        if (running) return;
        const auto ops = currentOps();
        if (ops.empty())
        {
            if (ctx.pairWritten) { if (ctx.stop) ctx.stop(); accepted = true; ctx.pairWritten ([] { return true; }); return; }          // an untouched stream is left as it is; this window goes away
            closeDialogOf (this); return;
        }
        if (ctx.mayAccept)                                                         // (3.35.0: the Fixer cannot keep a version with no session: it says so and the fixes stay here)
            if (const auto why = ctx.mayAccept(); why.isNotEmpty()) { status.setText (why, juce::dontSendNotification); return; }
        if (prepared && preparedOps == ops.size()) { finishCommit(); return; }
        dropPrepared();
        auto prep = ctx.prepare;
        previewBtn.setEnabled (false); declickBtn.setEnabled (false); finishBtn.setEnabled (false); revertBtn.setEnabled (false); compareBtn.setEnabled (false); playBtn.setEnabled (false);
        status.setText ("Writing: repairing every track from the first mark to the last and making the new files...", juce::dontSendNotification);
        juce::Component::SafePointer<RepairDialog> self (this);
        const auto n = ops.size();
        runJob ([prep, ops] (AudioJob& j) { return prep (ops, j); },
                [self, n] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isEmpty()) { self->prepared = true; self->preparedOps = n; self->finishCommit(); }
                    else { self->status.setText (err, juce::dontSendNotification); self->refreshControls(); }
                });
    }
    void finishCommit()
    {
        if (ctx.stop) ctx.stop();
        if (ctx.pairWritten)
        {
            accepted = true;                                         // (the corrected files wait in the pair; this window goes away and the other stream is next)
            ctx.pairWritten ([] { return true; });
            return;
        }
        if (! applied)
        {
            if (! ctx.apply || ! ctx.apply()) { status.setText ("The audio changed while the repair was being made, so nothing was changed. Please try again.", juce::dontSendNotification); prepared = false; preparedOps = 0; refreshControls(); return; }
            applied = true;
        }
        if (ctx.accept) ctx.accept();
        accepted = true;
        closeDialogOf (this);
    }
    RepairContext ctx;
    InfoNote intro; juce::Label info, sensCap, strengthCap, surroundCap, weightCap, amountCap, residualCap, roomCap;
    SpectrogramView view; juce::ComboBox dirBox, scaleBox, detailBox, methodBox, bandsBox; juce::ToggleButton multiBtn { "Multi-resolution" }, linkBtn { "Link stereo" }; juce::TextButton specBtn { "Spectrogram settings..." };
    HarmonicsBar harmBar { view };                                 // 3.33.0: the harmonics of the box (after the picture: it works on it)
    SelectToolsBar toolsBar { view };                              // 3.33.0: Rectangle, Time, Brush
    std::shared_ptr<std::atomic<bool>> detailCancel; juce::Slider sens, strength, surround, weight, amount, residual;
    // 3.33.0: L, C and R beside Before <-> After (Side, for a stray consonant): the sound before the box only (0 %), both / found for each track (50 %), after it only (100 %)
    juce::TextButton sideL { "L" }, sideC { "C" }, sideR { "R" };
    static constexpr int kSideW = 26;
    void setupSides()
    {
        const double at[] = { 0.0, 50.0, 100.0 };
        const char* tips[] = { "L (key Z): only the sound before the box (0 %).",
                               "C (key \\): both sides (50 %) - for a stray consonant, the side is found for each track.",
                               "R (key X): only the sound after the box (100 %)." };
        juce::TextButton* bs[] = { &sideL, &sideC, &sideR };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = bs[i];
            b->setConnectedEdges (i == 0 ? juce::Button::ConnectedOnRight : i == 2 ? juce::Button::ConnectedOnLeft : juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
            b->setTooltip (tips[i]);
            b->onClick = [this, v = at[i]] { weight.setValue (v, juce::sendNotificationSync); };
            addAndMakeVisible (*b);
        }
    }
    /** Z, \\ and X: L, C and R (the ReTouch tab comes to the front, so the change can be seen). */
    bool sideKey (const juce::KeyPress& k)
    {
        if (k.getModifiers().isAnyModifierKeyDown()) return false;
        const auto ch = k.getTextCharacter();
        const double v = (ch == 'z' || ch == 'Z') ? 0.0 : ch == '\\' ? 50.0 : (ch == 'x' || ch == 'X') ? 100.0 : -1.0;
        if (v < 0.0) return false;
        selectTab (kTabTouch);
        weight.setValue (v, juce::sendNotificationSync);
        return true;
    }
    /** L, C and R after the slider, when it shows. */
    void putSides (juce::Rectangle<int>& r)
    {
        if (! weight.isVisible()) return;
        for (auto* b : { &sideL, &sideC, &sideR }) b->setBounds (r.removeFromLeft (juce::jmin (kSideW, r.getWidth())));
        r.removeFromLeft (juce::jmin (kCapGap, r.getWidth()));
    }
    juce::TextButton roomBtn { "This box is the room tone [L]" }, denoiseBtn { "Denoise [N]" }; double rtBoxT0 = -1.0, rtBoxT1 = -1.0;
    HumPanel humPanel; juce::TextButton dehumBtn { "De-hum" };                                               // 3.44.0: the De-hum tab
    std::vector<std::pair<double, double>> rtSpans; bool rtQuietest = false; juce::ToggleButton quietBox;      // 3.29.0: the room tone's stretches; learned from the quietest moments
    juce::Slider gainSlider; juce::Label gainCap;                                                             // 3.29.0: the Gain tab's amount, in the panel (G applies it at once)
    juce::TextButton playBtn { "Play" }, stopBtn { "Stop" };
    juce::ToggleButton loopBtn { "Loop" };
    juce::Label hearCap; juce::ComboBox hearBox;
    bool prepared = false, applied = false, accepted = false, changedWhileRunning = false;
    size_t preparedOps = 0;                                        // how many changes the prepared files contain
    std::vector<RepairOp> committed;                               // the changes accepted so far (in order)
    juce::TextButton previewBtn { "ReTouch [R]" }, declickBtn { "De-click [D]" }, compareBtn { "Show before" }, revertBtn { "Undo" }, finishBtn { "Write back to file" }, close { "Cancel" };
    juce::TextButton gainBtn { "Gain [G]" };
    bool gainNext = false; double gainNextDb = 0.0;                // the next fix is a gain change of the box (set by the Gain pop-up)
    struct Step { std::shared_ptr<std::vector<std::vector<float>>> tracks; std::shared_ptr<MergedSpectrogram> spec; int ops = 1;      // ops: how many changes the step made (3.29.0: one per rectangle of the selection)
                  double from = 0.0, to = -1.0; };      // 3.38.0: the seconds its sound changed in (to < from: anywhere), so the sharp picture is made again only there
    std::vector<Step> history;                                     // the state before each fix, for Undo
    bool scanned = false, showingAfter = false;
    std::shared_ptr<std::vector<std::vector<float>>> origTracks;
    std::shared_ptr<MergedSpectrogram> origSpec;
    FixSpec pvSpec; juce::int64 pvT0 = 0, pvT1 = 0, pvPlaced = -1;

    // ---- consonants (3.6.0): Copy / Copy and remove keep what the stray-consonant ReTouch takes out of the box (every track, every channel); Paste adds a kept one
    //      at the playhead; the last paste can be nudged and its level changed (it is taken off and put back) ----
    juce::TextButton copyBtn { "Copy consonant" }, copyMoveBtn { "Copy and remove" }, renameBtn { "Rename..." }, forgetBtn { "Delete" }, pasteBtn { "Paste here [P]" };
    juce::TextButton nudgeL5 { "<<" }, nudgeL1 { "<" }, nudgeR1 { ">" }, nudgeR5 { ">>" };
    juce::Label consHint, consCap, levelCap, nudgeCap, nudgeRead;
    juce::ComboBox consBox;
    juce::Slider level;
    std::vector<ConsonantDef> consList;                            // the project's kept consonants, as the list shows them
    juce::String consListKey;                                      // (what the list was made from: it is made again only when that changes)
    juce::Uuid consSel;                                            // the one chosen in the list
    std::map<juce::String, std::shared_ptr<const ConsonantAudio>> consCache;    // the audio of the ones pasted while this window is open
    juce::String lastConsName { "t" };
    juce::String nextNote;                                         // said first when the fix under way is done (Copy and remove: what to do next)
    bool pasteRunning = false;                                     // the fix under way is a paste (or a nudge of one)
    juce::int64 pendingNudge = 0; bool pendingLevel = false;       // nudges / a level change pressed while it is being made

    void setupConsonants()
    {
        auto label = [this] (juce::Label& l, const juce::String& t, bool dim = false)
        {
            l.setText (t, juce::dontSendNotification); l.setColour (juce::Label::textColourId, dim ? theme::dimText : theme::text); l.setMinimumHorizontalScale (0.8f); addChildComponent (l);
        };
        label (consHint, "Draw the box round the consonant from about 2 kHz up, then Copy.", true);
        label (consCap, "Kept:"); label (levelCap, "Level"); label (nudgeCap, "Nudge");
        nudgeRead.setText ({}, juce::dontSendNotification); nudgeRead.setJustificationType (juce::Justification::centred); nudgeRead.setColour (juce::Label::textColourId, theme::text); addChildComponent (nudgeRead);
        nudgeRead.setTooltip ("How far the consonant just pasted has been nudged from where it was first put.");
        copyBtn.setTooltip ("Keeps the consonant in the box with the project: what the stray-consonant ReTouch would take out of it (the consonant itself, above the vowel and the room), "
                            "on EVERY track and every channel, each as that microphone heard it - nothing is mixed down. The audio here is not changed. "
                            "It can be pasted into any ReTouch window of this project, today or weeks later (until you delete it).");
        copyMoveBtn.setTooltip ("For an early or late consonant: keeps it (as Copy consonant does) and takes it out here, as the stray-consonant ReTouch does at full strength. "
                                "Then click the time ruler where it should sound and press Paste here [P]: each track's own part of it goes back on that track.");
        renameBtn.setTooltip ("Gives the chosen consonant another name (t, s, sh, soft d ...).");
        forgetBtn.setTooltip ("Takes the chosen consonant off this project's list and deletes its audio. Fixes already made with it stay as they are.");
        pasteBtn.setTooltip ("Adds the chosen consonant on top of the sound, the start of the box it was copied from at the playhead you placed by clicking the time ruler. "
                             "Each track gets its own part of it (a track it was not copied from gets nothing). Same rules as ReTouch: Play to listen, Undo takes it off, " + keepName() + " keeps it. Key: P");
        if (ctx.keepConsonant == nullptr)                                          // (3.35.0: fermata:Live's Fixer - consonants are copied in fermata, which keeps them with the project)
            consHint.setText ("Consonants are copied in fermata. The ones this project keeps can be pasted here.", juce::dontSendNotification);
        consBox.setTooltip ("The consonants kept with this project (Copy consonant adds one; Copy and remove adds one too). Paste uses the one chosen here.");
        consBox.onChange = [this]
        {
            const int i = consBox.getSelectedItemIndex();
            if (juce::isPositiveAndBelow (i, (int) consList.size())) consSel = consList[(size_t) i].id;
            refreshControls();
        };
        level.setSliderStyle (juce::Slider::LinearHorizontal); level.setRange (-24.0, 12.0, 0.5); level.setValue (0.0, juce::dontSendNotification); level.setScrollWheelEnabled (false);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22); level.setTextValueSuffix (" dB"); level.setDoubleClickReturnValue (true, 0.0);
        level.setTooltip ("How loud the consonant goes in, against how it was copied (0 dB = as it was). Changing it changes the consonant just pasted, too. Double-click for 0 dB.");
        level.onValueChange = [this] { if (! level.isMouseButtonDown()) levelChanged(); };
        level.onDragEnd = [this] { levelChanged(); };
        addChildComponent (level);
        auto nb = [this] (juce::TextButton& b, int ms, const juce::String& tip)
        {
            b.setTooltip (tip); b.onClick = [this, ms] { nudge (ms); }; b.setWantsKeyboardFocus (false); addChildComponent (b);
        };
        nb (nudgeL5, -5, "The consonant just pasted 5 ms earlier (Shift + comma)."); nb (nudgeL1, -1, "The consonant just pasted 1 ms earlier (comma).");
        nb (nudgeR1, 1, "The consonant just pasted 1 ms later (full stop)."); nb (nudgeR5, 5, "The consonant just pasted 5 ms later (Shift + full stop).");
        copyBtn.onClick = [this] { copyConsonant (false); };
        copyMoveBtn.onClick = [this] { copyConsonant (true); };
        renameBtn.onClick = [this] { renamePressed(); };
        forgetBtn.onClick = [this] { forgetPressed(); };
        pasteBtn.onClick = [this] { pasteConsonant(); };
        pasteBtn.setColour (juce::TextButton::buttonColourId, theme::accent);
        for (auto* c : std::initializer_list<juce::Component*> { &copyBtn, &copyMoveBtn, &renameBtn, &forgetBtn, &pasteBtn, &consBox }) { addChildComponent (c); c->setWantsKeyboardFocus (false); }
    }
    const ConsonantDef* selectedConsonant() const { for (auto& c : consList) if (c.id == consSel) return &c; return nullptr; }
    bool lastIsPaste() const { return ! committed.empty() && committed.back().spec.kind == FixSpec::Kind::Paste; }
    juce::String nudgeText() const
    {
        if (! lastIsPaste() || committed.back().placed < 0) return {};
        const double ms = 1000.0 * (double) (committed.back().t0 - committed.back().placed) / ctx.rate;
        return (ms > 0.04 ? "+" : "") + juce::String (ms, std::abs (ms - std::round (ms)) < 0.05 ? 0 : 1) + " ms";
    }
    /** The list, from the project (made again only when the project's list has changed). */
    void refreshConsonantList()
    {
        if (! ctx.consonantList) return;
        auto list = ctx.consonantList();
        juce::String key; for (auto& c : list) key << c.id.toString() << c.name << c.from << ";";
        if (key == consListKey) return;
        consListKey = key; consList = std::move (list);
        consBox.clear (juce::dontSendNotification);
        for (int i = 0; i < (int) consList.size(); ++i) consBox.addItem (consList[(size_t) i].listText(), i + 1);
        int at = -1; for (int i = 0; i < (int) consList.size(); ++i) if (consList[(size_t) i].id == consSel) at = i;
        if (at < 0 && ! consList.empty()) { at = (int) consList.size() - 1; consSel = consList.back().id; }      // (none chosen yet: the newest)
        if (at >= 0) consBox.setSelectedItemIndex (at, juce::dontSendNotification);
        consBox.setTextWhenNothingSelected (consList.empty() ? "None kept yet: draw a box round a consonant and press Copy" : "Choose one");
    }
    /** Copy (and remove): on a job thread, every track's channels round the box are read from the files, this window's fixes so far put on them, and the
        stray-consonant ReTouch's difference kept (consonants::extract). Then it is named (Copy) or named for where it came from (Copy and remove, which then
        takes it out here like the ReTouch). */
    void copyConsonant (bool remove)
    {
        if (! scanned || running || ! view.hasSelection() || ! ctx.readChannels || ! ctx.keepConsonant) return;
        juce::int64 t0 = 0, t1 = 0;
        auto spec = buildSpec (t0, t1, 0);
        spec = consonants::copySpec (spec);
        if (t1 - t0 < (juce::int64) (0.005 * ctx.rate)) { status.setText ("The box is too short to hold a consonant.", juce::dontSendNotification); return; }
        const auto pad = (juce::int64) std::llround ((1.0 + 2.0 * (double) (t1 - t0) / ctx.rate) * ctx.rate);       // (the ReTouch measures the sound round the box)
        const auto a = juce::jmax ((juce::int64) 0, t0 - pad), b = juce::jmin (ctx.length, t1 + pad);
        auto read = ctx.readChannels; const auto ids = ctx.trackIds; const auto names = ctx.trackNames; const double rate = ctx.rate;
        const auto ops = committed; auto picture = origTracks;
        auto out = std::make_shared<ConsonantAudio>();
        status.setText (remove ? "Copying the consonant (every track) and taking it out here..." : "Copying the consonant (every track)...", juce::dontSendNotification);
        juce::Component::SafePointer<RepairDialog> self (this);
        runJob ([read, ids, names, rate, ops, picture, out, spec, t0, t1, a, b] (AudioJob& j) -> juce::String
                {
                    auto tracks = read (a, b, j);
                    if (j.cancelled()) return "Cancelled.";
                    if (tracks.size() != ids.size()) return "The tracks could not all be read.";
                    // this window's fixes so far, where they reach this stretch (a denoise is left out: it needs each file's room tone, and only touches the background)
                    for (auto& o : ops)
                    {
                        if (o.spec.kind == FixSpec::Kind::Denoise || o.t1 <= a || o.t0 >= b) continue;
                        for (size_t i = 0; i < tracks.size(); ++i)
                        {
                            auto s = o.spec; if (s.kind == FixSpec::Kind::Paste) audioops::resolvePaste (s, ids[i]);
                            audioops::applyFix (tracks[i], rate, (long) (o.t0 - a), (long) (o.t1 - a), s, (long) (0.02 * rate));
                        }
                    }
                    // which side of the box is the background: one answer for all the tracks, found on the picture as the ReTouch finds it
                    auto s = spec;
                    if (std::abs (s.repair.weighting - 0.5) < 0.04 && picture != nullptr && ! picture->empty())
                    {
                        const long L = (long) (*picture)[0].size();
                        std::vector<const std::vector<float>*> tp; for (auto& t : *picture) tp.push_back (&t);
                        const auto d = SpectralRepair::consonantSides (tp, rate, (long) juce::jlimit ((juce::int64) 0, (juce::int64) L, t0), (long) juce::jlimit ((juce::int64) 0, (juce::int64) L, t1), s.f0, s.f1);
                        s.repair.consonantBefore = d.first; s.repair.consonantEdge = d.second;
                    }
                    *out = consonants::extract (ids, names, tracks, rate, (long) (t0 - a), (long) (t1 - a), s);
                    if (out->tracks.empty()) return "Nothing to copy: the ReTouch found nothing in the box that stands out from the sound beside it. Draw the box round the consonant only, from about 2 kHz up.";
                    return {};
                },
                [self, out, remove, spec, t0, t1] (const juce::String& err)
                {
                    if (self == nullptr) return;
                    if (err.isNotEmpty()) { self->status.setText (err, juce::dontSendNotification); self->refreshControls(); return; }
                    const auto from = self->ctx.where + ", " + timefmt::minSecText (self->ctx.areaStart + (double) t0 / self->ctx.rate, 2);
                    if (remove)
                    {
                        juce::Uuid id; const auto e = self->ctx.keepConsonant (*out, "moved", from, id);
                        if (e.isNotEmpty()) { self->status.setText (e, juce::dontSendNotification); self->refreshControls(); return; }
                        self->consSel = id; self->consCache[id.toString()] = out;
                        self->refreshControls();
                        self->nextNote = "Copied (kept as \"moved\") and taken out here. Now click the time ruler where it should start and press Paste here [P]. ";
                        self->startFix (spec, t0, t1);                         // (taken out here as the stray-consonant ReTouch does, at full strength)
                        return;
                    }
                    auto keep = [self, out, from] (const juce::String& name)
                    {
                        if (self == nullptr) return;
                        juce::Uuid id; const auto e = self->ctx.keepConsonant (*out, name, from, id);
                        if (e.isNotEmpty()) { self->status.setText (e, juce::dontSendNotification); self->refreshControls(); return; }
                        self->lastConsName = name; self->consSel = id; self->consCache[id.toString()] = out;
                        self->status.setText ("Kept \"" + name + "\" (" + juce::String ((int) out->tracks.size()) + (out->tracks.size() == 1 ? " track, " : " tracks, ")
                                              + juce::String (juce::roundToInt (1000.0 * (double) out->length() / out->sampleRate)) + " ms) with the project: paste it here, or in any ReTouch window of this project, whenever you need it.",
                                              juce::dontSendNotification);
                        self->refreshControls();
                    };
                    askName ("Keep this consonant", "A name for it (t, s, sh, soft d ...). It is kept with the project, every track and channel as recorded.", self->lastConsName, keep);
                    self->refreshControls();
                });
    }
    static void askName (const juce::String& title, const juce::String& message, const juce::String& suggestion, std::function<void (const juce::String&)> done)
    {
        auto* aw = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("n", suggestion);
        aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([aw, done] (int res)
        {
            const auto n = aw->getTextEditorContents ("n").trim();
            delete aw;
            if (res == 1 && n.isNotEmpty() && done) done (n);
        }));
    }
    void renamePressed()
    {
        const auto* c = selectedConsonant(); if (c == nullptr || ! ctx.renameConsonant) return;
        const auto id = c->id;
        juce::Component::SafePointer<RepairDialog> self (this);
        askName ("Rename consonant", "A new name for \"" + c->name + "\" (" + c->from + ").", c->name, [self, id] (const juce::String& n)
        {
            if (self == nullptr) return;
            self->ctx.renameConsonant (id, n); self->refreshControls();
        });
    }
    void forgetPressed()
    {
        const auto* c = selectedConsonant(); if (c == nullptr || ! ctx.forgetConsonant) return;
        const auto id = c->id;
        juce::Component::SafePointer<RepairDialog> self (this);
        confirmAsync ("Delete consonant", "Take \"" + c->name + "\" (" + c->from + ") off this project's list and delete its audio? Fixes already made with it stay as they are.", "Delete",
                      [self, id] { if (self == nullptr) return; self->ctx.forgetConsonant (id); self->consCache.erase (id.toString()); self->consSel = juce::Uuid::null(); self->refreshControls(); });
    }
    /** Paste: the chosen consonant, its box's start at the placed playhead, at the level set; a fix like the others. */
    void pasteConsonant()
    {
        if (! scanned || running) return;
        const auto* c = selectedConsonant();
        if (c == nullptr) { status.setText ("Choose a kept consonant in the list first (or copy one: draw a box round it and press Copy consonant).", juce::dontSendNotification); return; }
        const double cue = view.cueTime();
        if (cue < 0.0) { status.setText ("Click the time ruler where the consonant should start (a red playhead appears), then press Paste here [P].", juce::dontSendNotification); return; }
        auto audio = consonantAudio (*c);
        if (audio == nullptr) return;
        if (std::abs (audio->sampleRate - ctx.rate) > 0.5) { status.setText ("\"" + c->name + "\" was copied at " + juce::String (audio->sampleRate / 1000.0, 1) + " kHz and this audio is at " + juce::String (ctx.rate / 1000.0, 1) + " kHz: it cannot be pasted here.", juce::dontSendNotification); return; }
        int here = 0; for (auto& id : ctx.trackIds) if (audio->forTrack (id) != nullptr) ++here;
        if (here == 0)
        {
            status.setText ("None of the tracks \"" + c->name + "\" was copied from (" + juce::StringArray (audio->trackNames.data(), (int) audio->trackNames.size()).joinIntoString (", ")
                            + ") are in this window, so there is nothing to paste on.", juce::dontSendNotification);
            return;
        }
        const auto t0 = (juce::int64) std::llround (cue * ctx.rate) - audio->lead, t1 = t0 + audio->length();
        const auto edge = (juce::int64) std::llround (0.04 * ctx.rate);                   // (clear of the joins at the ends of the part, where the new audio is crossfaded in)
        if (t0 < edge || t1 > ctx.length - edge) { status.setText ("The consonant would run past the part loaded in this window. Place the playhead further in, or mark a wider part (keys 1 and 2) and open the window again.", juce::dontSendNotification); return; }
        FixSpec spec; spec.kind = FixSpec::Kind::Paste; spec.paste = audio; spec.pasteGainDb = level.getValue();
        startFix (spec, t0, t1, t0);
    }
    std::shared_ptr<const ConsonantAudio> consonantAudio (const ConsonantDef& c)
    {
        auto it = consCache.find (c.id.toString());
        if (it != consCache.end()) return it->second;
        if (! ctx.loadConsonant) return nullptr;
        juce::String err;
        auto a = ctx.loadConsonant (c.id, err);
        if (a == nullptr) { status.setText (err.isNotEmpty() ? err : "The consonant's audio could not be read.", juce::dontSendNotification); return nullptr; }
        consCache[c.id.toString()] = a;
        return a;
    }
    /** The consonant just pasted, again: moved by 'ms' (and at the level set now). It is taken off and put back, so Undo still takes it off in one go. */
    void nudge (int ms)
    {
        const auto d = (juce::int64) std::llround ((double) ms * 0.001 * ctx.rate);
        if (running && pasteRunning) { pendingNudge += d; return; }            // (pressed again while the last nudge is still being made: it follows when that is done)
        adjustPaste (d);
    }
    void levelChanged()
    {
        if (running && pasteRunning) { pendingLevel = true; return; }
        if (lastIsPaste() && std::abs (committed.back().spec.pasteGainDb - level.getValue()) >= 0.01) adjustPaste (0);
    }
    /** A paste (or a nudge of one) has been made: nudges and level changes that came in meanwhile follow now, in one go. */
    void followPending()
    {
        const auto d = pendingNudge; const bool lv = pendingLevel;
        pendingNudge = 0; pendingLevel = false;
        if (d != 0 || (lv && lastIsPaste() && std::abs (committed.back().spec.pasteGainDb - level.getValue()) >= 0.01)) adjustPaste (d);
    }
    void adjustPaste (juce::int64 delta)
    {
        if (! scanned || running || ! lastIsPaste()) return;
        auto op = committed.back();
        op.t0 += delta; op.t1 += delta; op.spec.pasteGainDb = level.getValue();
        const auto edge = (juce::int64) std::llround (0.04 * ctx.rate);
        if (op.t0 < edge || op.t1 > ctx.length - edge) { status.setText ("It cannot go further: it would run past the part loaded in this window.", juce::dontSendNotification); return; }
        revert();
        startFix (op.spec, op.t0, op.t1, op.placed);
    }
};

/** The spectrogram settings (the ones iZotope RX has, as far as they apply here). Changes show at once and are remembered. */
class SpectroSettingsDialog : public juce::Component
{
public:
    SpectroSettingsDialog (const SpectroSettings& s, std::function<void (const SpectroSettings&)> apply) : cur (s), onApply (std::move (apply))
    {
        setSize (440, 330);
        auto cap = [this] (juce::Label& l, const juce::String& t) { l.setText (t, juce::dontSendNotification); l.setColour (juce::Label::textColourId, theme::text); addAndMakeVisible (l); };
        cap (cPal, "Colour map"); cap (cLo, "Lowest level (dB)"); cap (cHi, "Highest level (dB)"); cap (cDet, "Detail"); cap (cRed, "Fewer points above (s)");
        sharpBtn.setButtonText ("Sharp picture for what is in view (high-quality rendering)"); sharpBtn.setToggleState (cur.sharp, juce::dontSendNotification);
        reBtn.setButtonText ("Enable reassignment (crisp lines)"); reBtn.setToggleState (cur.reassign, juce::dontSendNotification);
        pal.addItem ("Cyan to orange", 1); pal.addItem ("Heat (standard)", 2); pal.addItem ("Grey", 3); pal.addItem ("Ice", 4); pal.addItem ("Rainbow", 5);
        pal.setSelectedId (cur.palette + 1, juce::dontSendNotification);
        auto sl = [this] (juce::Slider& x, double a, double b, double st, double v) { x.setRange (a, b, st); x.setValue (v, juce::dontSendNotification); x.setSliderStyle (juce::Slider::LinearHorizontal); x.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22); addAndMakeVisible (x); x.onValueChange = [this] { push(); }; };
        sl (lo, -160.0, -40.0, 0.1, cur.lo); sl (hi, -60.0, 0.0, 0.1, cur.hi); sl (det, 0.0, 1.0, 0.05, cur.detail); sl (red, 20.0, 1200.0, 10.0, cur.reduceAbove);
        det.setTooltip ("Left: finer pitch (long analysis window). Right: finer time (short window).");
        reset.setButtonText ("Back to the RX-style defaults");
        reset.onClick = [this] { SpectroSettings d; cur = d; pal.setSelectedId (1, juce::dontSendNotification); lo.setValue (d.lo, juce::dontSendNotification); hi.setValue (d.hi, juce::dontSendNotification); det.setValue (d.detail, juce::dontSendNotification); red.setValue (d.reduceAbove, juce::dontSendNotification); sharpBtn.setToggleState (true, juce::dontSendNotification); reBtn.setToggleState (true, juce::dontSendNotification); onApply (cur); };
        for (auto* c : std::initializer_list<juce::Component*> { &sharpBtn, &reBtn, &pal, &reset }) addAndMakeVisible (c);
        sharpBtn.onClick = [this] { push(); }; reBtn.onClick = [this] { push(); }; pal.onChange = [this] { push(); };
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        auto row = [&r] { auto x = r.removeFromTop (30); r.removeFromTop (6); return x; };
        sharpBtn.setBounds (row()); reBtn.setBounds (row());
        auto a = row(); cPal.setBounds (a.removeFromLeft (150)); pal.setBounds (a);
        a = row(); cLo.setBounds (a.removeFromLeft (150)); lo.setBounds (a);
        a = row(); cHi.setBounds (a.removeFromLeft (150)); hi.setBounds (a);
        a = row(); cDet.setBounds (a.removeFromLeft (150)); det.setBounds (a);
        a = row(); cRed.setBounds (a.removeFromLeft (150)); red.setBounds (a);
        reset.setBounds (r.removeFromTop (30).removeFromLeft (230));
    }
private:
    void push()
    {
        cur.sharp = sharpBtn.getToggleState(); cur.reassign = reBtn.getToggleState(); cur.palette = juce::jmax (0, pal.getSelectedId() - 1);
        cur.lo = lo.getValue(); cur.hi = hi.getValue(); if (cur.hi - cur.lo < 6.0) cur.hi = cur.lo + 6.0;
        cur.detail = det.getValue(); cur.reduceAbove = red.getValue();
        onApply (cur);
    }
    SpectroSettings cur; std::function<void (const SpectroSettings&)> onApply;
    juce::Label cPal, cLo, cHi, cDet, cRed; juce::ComboBox pal; juce::Slider lo, hi, det, red;
    juce::ToggleButton sharpBtn, reBtn; juce::TextButton reset;
};

inline void RepairDialog::openSpectroSettings()
{
    juce::Component::SafePointer<RepairDialog> self (this);
    openSmallDialog (std::make_unique<SpectroSettingsDialog> (view.settings(), [self] (const SpectroSettings& n)
    {
        if (self == nullptr) return;
        self->view.setSettings (n); n.save (gSpecPrefs);
        if (gSpecPrefs != nullptr) gSpecPrefs->saveIfNeeded();
    }), "Spectrogram settings", this);
}

} } // namespace td::fixtools
