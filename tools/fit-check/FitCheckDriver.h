#pragma once
/** The small-screen check's scenes for fermata (3.71.0; tools/fit-check.sh): every window and dialog it can open on the project given (Checker.h
    does the looking). Built into fermata only by that script (FERMATA_FIT_CHECK=1), never into the program the user runs. */
#include "Checker.h"
#include "../../src/ui/PreampSetup.h"
#include "../../src/ui/CpuCoresPanel.h"
#include "../../src/ui/PluginManager.h"
#include "../../src/ui/MixChangesPanel.h"
#include "../../src/ui/VersionsWindow.h"
#include "../../src/ui/AafExportWindow.h"
#include "../../src/ui/VideoExportWindow.h"
#include "../../src/ui/ProjectRateDialog.h"
#include "../../src/ui/VolumeDialog.h"
#include "../../src/ui/FixTools.h"
#include "../../src/core/EditVersions.h"
#include "../../src/core/MasterRender.h"
#include <set>

namespace td::fitcheck
{
class Driver : public Checker
{
public:
    Driver (AppContext& c, juce::Component& mainWin, juce::File outDir) : Checker (mainWin, std::move (outDir)), app (c) {}

    void start()
    {
        auto& P = app.project;
        const juce::Uuid take = ! P.takeWindows.empty() ? P.takeWindows.front()->id : juce::Uuid::null();
        const juce::Uuid edit = ! P.edits.empty() ? P.edits.front()->id : juce::Uuid::null();
        const juce::Uuid mixer = ! P.mixers.empty() ? P.mixers.front()->id : juce::Uuid::null();
        juce::Uuid region;
        if (! P.edits.empty() && P.edits.front()->regions.size() > 1) region = P.edits.front()->regions[1].id;
        auto* M = &main;
        add ("main", [] {}, {}, true);
        add ("audio-settings", [this] { if (app.showAudioSettings) app.showAudioSettings(); });
        add ("preamps", [this] { showPreampSetup (app); });
        add ("designer", [this] { if (app.showDesign) app.showDesign(); });
        add ("media", [this] { if (app.showMedia) app.showMedia(); });
        add ("mastering", [this] { if (app.showMastering) app.showMastering(); }, { "Virtual Master", "DDP builder", "Listening copy", "Atmos" });
        add ("organiser", [this] { if (app.showOrganiser) app.showOrganiser(); });
        add ("takedisplay", [this] { if (app.showTakeDisplay) app.showTakeDisplay(); });
        add ("pitchpipe", [this] { if (app.showPitchPipe) app.showPitchPipe(); });
        add ("click", [this] { if (app.showClick) app.showClick(); });
        add ("surface", [this] { if (app.showSurfacePanel) app.showSurfacePanel(); });
        add ("hotkeys", [this] { if (app.showHotKeys) app.showHotKeys(); });
        add ("recmonitor", [this] { if (app.showRecordMonitor) app.showRecordMonitor(); });
        add ("bridge", [this] { if (app.showBridge) app.showBridge(); });
        add ("cpucores", [this] { showCpuCoresPanel (app); });
        add ("plugins", [this] { showPluginManager (app); });
        add ("samplerate", [this, M] { chooseProjectRate (app, "Project sample rate", "Which sample rate is this project?", 48000.0, "OK", true, [] (double) {}, M); });
        if (! take.isNull())
        {
            add ("takewindow", [this, take] { if (app.showTakeWindow) app.showTakeWindow (take); });
            add ("takewindow-folded", [this, take] { foldAll (take); if (app.showTakeWindow) app.showTakeWindow (take); foldAll (take, false); });     // 3.72.0: its lanes folded
            add ("align-take", [this, take] { if (app.showAlign) app.showAlign (false, take); });
        }
        if (! edit.isNull())
        {
            add ("editwindow", [this, edit] { if (app.showEdit) app.showEdit (edit); });
            add ("editwindow-folded", [this, edit] { foldAll (edit); if (app.showEdit) app.showEdit (edit); foldAll (edit, false); });              // 3.72.0: its lanes folded
            add ("versions", [this, edit, M] { mark (edit); if (app.showVersions) app.showVersions (edit, M); });
            add ("retouch", [this, edit, M] { mark (edit); fixtools::repairEdit (app, edit, M); });
            add ("noisemode", [this, edit, M] { mark (edit); fixtools::noiseModeEdit (app, edit, M); });
            add ("pitch", [this, edit, M] { mark (edit); fixtools::pitchEdit (app, edit, M); });
            add ("pitchcurve", [this, edit, M] { mark (edit); fixtools::pitchCurveEdit (app, edit, M); });
            add ("reharmoniser", [this, edit, M] { mark (edit); fixtools::reharmoniserEdit (app, edit, M); });
            add ("video-export", [this, edit, M] { if (app.showVideoExport) app.showVideoExport (edit, M); });
            add ("aaf-export", [this, edit, M] { if (app.showAafExport) app.showAafExport (edit, M); });
            add ("bounce-edit", [this, edit] { if (app.showBounce) { BounceContext b; b.kind = BounceContext::Kind::Edit; b.id = edit; b.defaultName = "Fit check"; app.showBounce (b); } });
            add ("volume-area", [this, edit] { showNewVolumeArea (app, edit, 0, 48000 * 4, false); });
            if (! region.isNull()) add ("trim", [this, edit, region] { if (app.showTrim) app.showTrim (edit, region, false); });
            // 3.72.0: with a frozen version (its comments' lane, its pieces and the changes since it, above the timeline) - open, then folded. Last:
            // the edit is changed by it (in the project's copy)
            add ("editwindow-versions", [this, edit] { freezeOnce (edit); if (app.showEdit) app.showEdit (edit); });
            add ("editwindow-versions-folded", [this, edit] { freezeOnce (edit); foldAll (edit); if (app.showEdit) app.showEdit (edit); foldAll (edit, false); });
        }
        if (! mixer.isNull())
        {
            add ("mixer", [this, mixer] { if (app.showMixer) app.showMixer (mixer); });
            add ("mixchanges", [this, mixer] { showMixChangesPanel (app, mixer); });
        }
        // 3.74.0: the Spectrogram Editing window - it renders the piece and measures it, so it is looked at a second time, once that is done
        if (! edit.isNull())
        {
            add ("specedit", [this, edit] { if (app.showSpecEdit) app.showSpecEdit (edit); });
            add ("specedit-measured", [this, edit] { soundInRender (edit); if (app.showSpecEdit) app.showSpecEdit (edit); });
        }
        begin();
    }

private:
    AppContext& app;
    /** 3.72.0: every lane above the tracks folded (LaneFold.h) in the window with this id, for when it opens next (fold false: none). */
    void foldAll (const juce::Uuid& windowId, bool fold = true)
    {
        if (auto* st = app.props.getUserSettings())
        {
            if (fold) st->setValue ("laneFolds_" + windowId.toString(), "markers,feedback,guide,tuning,video,pieces,changes,comments:1,comments:2,comments:3");
            else st->removeValue ("laneFolds_" + windowId.toString());
        }
    }
    /** 3.74.0: the piece's render (made by the scene before) filled with a sound to measure - notes that swell and die away, different on the
        two sides, with a few clipped crests - so the Spectrogram Editing window is looked at with figures and meters in it, whatever the
        project's own mixer lets through. */
    void soundInRender (const juce::Uuid& edit)
    {
        const auto dir = MasterRenders::folderFor (app.project);
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, edit.toString() + "-*.wav"))
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            double rate = 48000.0; juce::int64 frames = 48000 * 20;
            { std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f)); if (r != nullptr) { rate = r->sampleRate; frames = r->lengthInSamples; } }
            juce::AudioBuffer<float> b (2, (int) frames);
            juce::Random rnd (7);
            for (int i = 0; i < (int) frames; ++i)
            {
                const double t = (double) i / rate, note = std::floor (t / 1.7), in = t - note * 1.7;
                const double hz = 110.0 * std::pow (2.0, std::fmod (note * 7.0, 24.0) / 12.0), env = std::exp (-in * 1.4) * (0.25 + 0.75 * std::abs (std::sin (t * 0.23)));
                const double l = env * (0.8 * std::sin (6.283185307 * hz * t) + 0.25 * std::sin (6.283185307 * hz * 3.01 * t) + 0.04 * (rnd.nextDouble() - 0.5));
                const double r = env * (0.7 * std::sin (6.283185307 * hz * t + 0.9) + 0.3 * std::sin (6.283185307 * hz * 2.0 * t) + 0.04 * (rnd.nextDouble() - 0.5));
                b.setSample (0, i, (float) juce::jlimit (-1.0, 1.0, l * 1.25)); b.setSample (1, i, (float) juce::jlimit (-1.0, 1.0, r * 1.1));
            }
            f.deleteFile();
            if (auto out = f.createOutputStream())
                if (std::unique_ptr<juce::AudioFormatWriter> w { juce::WavAudioFormat().createWriterFor (out.release(), rate, 2, 32, {}, 0) })
                    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
    }
    /** 3.72.0: the edit frozen as Edit 1, with a listener's comment on it, and compared with (once). */
    void freezeOnce (const juce::Uuid& edit)
    {
        auto* e = app.project.findEdit (edit);
        if (e == nullptr || ! e->versions.empty()) return;
        FeedbackNote n; n.key = "fit-check-1"; n.who = "Fit check"; n.text = "A comment"; n.bar = "bar 3"; n.seconds = 4.0;
        e->feedback.push_back (n);
        e->showFeedback = true;
        e->compareWith = editversions::freeze (*e, {});
        app.project.refreshCommentsCopy (*e);
    }
    /** Marks 1 and 2 on a part of the edit's first piece (the tools work on the marked part). */
    void mark (const juce::Uuid& edit)
    {
        if (auto* e = app.project.findEdit (edit); e != nullptr && ! e->regions.empty() && e->sampleRate > 0.0)
        {
            const auto& r = e->regions.front();
            e->fixIn = (double) r.startSample / e->sampleRate + 1.0;
            e->fixOut = juce::jmin (e->fixIn + 3.0, (double) r.endSample() / e->sampleRate - 0.1);
        }
    }
};
} // namespace td::fitcheck
