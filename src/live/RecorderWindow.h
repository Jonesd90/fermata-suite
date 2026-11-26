#pragma once
/** fermata:Live 0.2.0: the recorder's test bench - a plain window (fermata's look and the real Primary and Fixer windows come in the next
    builds) that drives the recorder process end to end on real hardware: the project, the audio device, the drives, the session, Record and
    Stop, the meters, each drive's state, the merge, and the take going into the project for fermata to open. */
#include "RecorderLink.h"
#include "../ui/FitLayout.h"
#include "SessionIndex.h"
#include "../core/Project.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <set>

namespace td::live
{
class RecorderPanel final : public juce::Component, private juce::Timer
{
public:
    RecorderPanel (const juce::String& version, int port);
    ~RecorderPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    /** Closing the window: the recorder is told to quit unless a take is recording (then it is left running and found again next time). */
    void windowClosing();

private:
    void onEvent (const juce::var&);
    void onConnection (bool);
    void addLog (const juce::String&);
    void setProject (const juce::File& projectFile, bool create);
    void refreshProjectLine();
    void fillDeviceBoxes();
    void openDevice();
    void startSession (bool resume);
    void registerTake (const juce::var& mergeEvent);
    void askForUnregistered (const juce::var& status);
    void updateButtons();
    void timerCallback() override;
    void sendCommand (const juce::String& cmd, std::function<void (juce::var&)> fill = {});

    const juce::String version;
    RecorderLink link;
    std::unique_ptr<Project> project;
    juce::File projectFile;
    juce::var devicesInfo, lastStatus;
    std::vector<float> peaks, shown;
    bool connected = false, deviceOpen = false, sessionOn = false; int recordingTake = 0;
    double deviceRate = 0.0; juce::StringArray inputNames, outputNames;
    juce::int64 sessionPosition = 0;

    juce::Label title, projectLine, deviceStatus, sessionStatus, takeStatus, drivesStatus, mergeStatus, versionLabel;
    juce::Label lWin, lInputs, lPre, lBuf, lLabel, lBackups;
    juce::TextButton newProject { "New project..." }, openProject { "Open project..." }, openDev { "Open device" }, closeDev { "Close device" };
    juce::TextButton addBackup { "Add backup drive..." }, clearBackups { "Clear backups" }, startBtn { "Start session (pre-take)" }, resumeBtn { "Carry on last session" };
    juce::TextButton recordBtn { "RECORD" }, stopBtn { "STOP" }, endBtn { "End session" }, mergeBackupBtn { "Merge last take from backup" };
    juce::ComboBox typeBox, deviceBox, rateBox, bufferBox;
    juce::TextEditor windowName, backupsBox, inputsToRecord, preTake, bufferSeconds, takeLabel, log;
    std::unique_ptr<juce::FileChooser> chooser;

    struct Meters final : juce::Component { std::vector<float>* levels = nullptr; void paint (juce::Graphics&) override; };
    Meters meters;
    juce::Viewport meterView;
    int lastTakeNumber = 0; juce::String lastBackupFolder;
    juce::var lastOpen, lastSession;                        // the last open / session commands, sent again when a recorder had to be started afresh
    bool wasConnected = false, restoreAfterStatus = false; juce::int64 lastAskMs = 0;
    std::set<int> registeredTakes, askedTakes;
};

class RecorderWindow final : public juce::DocumentWindow
{
public:
    RecorderWindow (const juce::String& version, int port);
    void closeButtonPressed() override;
    // 0.10.18: fitted to the screen and its contents (FitLayout.h)
    fit::WindowFit fitter { *this };
    void resized() override { juce::DocumentWindow::resized(); fitter.windowResized(); }
    void childBoundsChanged (juce::Component* c) override { if (! fitter.adjusting()) juce::DocumentWindow::childBoundsChanged (c); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (! fitter.wheel (w)) juce::DocumentWindow::mouseWheelMove (e, w); }
private:
    RecorderPanel* panel = nullptr;
};
} // namespace td::live
