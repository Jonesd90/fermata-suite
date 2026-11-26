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
            add ("align-take", [this, take] { if (app.showAlign) app.showAlign (false, take); });
        }
        if (! edit.isNull())
        {
            add ("editwindow", [this, edit] { if (app.showEdit) app.showEdit (edit); });
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
        }
        if (! mixer.isNull())
        {
            add ("mixer", [this, mixer] { if (app.showMixer) app.showMixer (mixer); });
            add ("mixchanges", [this, mixer] { showMixChangesPanel (app, mixer); });
        }
        begin();
    }

private:
    AppContext& app;
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
