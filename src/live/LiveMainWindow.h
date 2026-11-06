#pragma once
/** fermata:Live 0.3.0: the Primary's main window - fermata's main window, reduced: the menu bar (Project, Audio settings, Preamps, Project
    Designer, Take Window, Mixer, Bounce out, Fixer, Help) with the status squares at its right end (Talkback, Pre-take, REC, Drives, Merge), the
    top row (the take counter, the project, the rate, the disk, the recorder's state), every input of the driver as a strip of the desk (its name,
    the input number and the device, the gain and the preamp keys where a Merging device controls it, the meter, TALK), and the driver's outputs
    along the bottom (their names, what feeds them, the meter, TB). The other windows are opened from here and belong to it. */
#include "PrimaryApp.h"
#include "LiveDesk.h"
#include "../ui/Uikit.h"
#include <map>

namespace td::live
{
class InputStrip;
class OutputStrip;

class LiveMainComponent final : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit LiveMainComponent (PrimaryApp&);
    ~LiveMainComponent() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    /** Opens (or brings to the front) one of the Primary's windows: "audio", "preamps", "designer", "take", "mixer", "bounce", "log". */
    void openWindow (const juce::String& key);
    void closeAllWindows();

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildStrips();
    void refreshTop();
    void showProjectMenu();
    void showHelpMenu();
    void askStationName();
    void newProjectDialog();
    void openProjectDialog();
    void showStartPanel (bool show);

    PrimaryApp& app;
    MenuEntry projectButton { "Project" }, audioButton { "Audio settings" }, preampsButton { "Preamps" }, designButton { "Project Designer" },
              takeButton { "Take Window" }, mixerButton { "Mixer" }, bounceButton { "Bounce out" }, fixerButton { "Fixer" }, helpButton { "Help" };
    std::vector<MenuEntry*> menuItems() { return { &projectButton, &audioButton, &preampsButton, &designButton, &takeButton, &mixerButton, &bounceButton, &fixerButton, &helpButton }; }
    StatusSquare talkSquare { "Talkback", juce::Colour (0xffa83cff), "Talkback (purple while open): the inputs marked TALK go to the outputs marked TB. Click (or numpad +) to open or close it." },
                 preSquare { "Pre-take", juce::Colour (0xff2e9e4f), "Green: the session is on - everything is being written to the drives (the pre-take), so a take can start up to the pre-take before Record. Click to start or end the session." },
                 recSquare { "REC", juce::Colour (0xffe53935), "Red: a take is recording." },
                 drivesSquare { "Drives", juce::Colour (0xff2e9e4f), "The recording drives: green = all writing, orange = one is behind or has a gap, red = one was dropped (the others go on)." },
                 mergeSquare { "Merge", juce::Colour (0xff2a9cc0), "Blue: a take is being merged from its chunks into fermata's take files (in the background)." };
    LiveBadge badge;
    NextTakeBox takeBox;
    InfoDisplay projectBox, rateBox { "RATE" }, diskBox { "DISK FREE" }, recorderBox { "RECORDER" };
    juce::Label statusLine;
    DeskCaption inputsCaption { "DRIVER INPUTS" }, outputsCaption { "DRIVER OUTPUTS" };
    juce::Viewport inputsView, outputsView;
    juce::Component inputsHolder, outputsHolder;
    juce::OwnedArray<InputStrip> inputStrips;
    juce::OwnedArray<OutputStrip> outputStrips;
    std::unique_ptr<juce::Component> startPanel;
    std::unique_ptr<juce::FileChooser> chooser;
    std::map<juce::String, std::unique_ptr<ToolWindow>> windows;
    int menuH = 34, topH = 64, outputsH = 118;
    static constexpr int kCheekW = 20, kRailH = 30;
    juce::uint32 lastDiskMs = 0;
    juce::String diskText;
    int shownInputs = -1, shownOutputs = -1;
};

class LiveMainWindow final : public juce::DocumentWindow
{
public:
    explicit LiveMainWindow (PrimaryApp&);
    void closeButtonPressed() override;
    LiveMainComponent* main() const { return content; }
    std::function<void()> onClose;
private:
    LiveMainComponent* content = nullptr;
};

/** The Primary's other windows (PrimaryWindows.cpp, PrimaryTakeWindow.cpp, LiveMixer.cpp): each is the contents of a ToolWindow. */
std::unique_ptr<juce::Component> makeAudioSettings (PrimaryApp&);
std::unique_ptr<juce::Component> makePreampsPanel (PrimaryApp&);
std::unique_ptr<juce::Component> makeDesigner (PrimaryApp&);
std::unique_ptr<juce::Component> makeBouncePanel (PrimaryApp&);
std::unique_ptr<juce::Component> makeLogPanel (PrimaryApp&);
std::unique_ptr<juce::Component> makePrimaryTakeWindow (PrimaryApp&, std::function<void (const juce::String&)> openWindow);
std::unique_ptr<juce::Component> makePrimaryMixer (PrimaryApp&);
} // namespace td::live
