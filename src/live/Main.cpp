// fermata:Live - the live-concert recorder (see docs/LIVE-BRIEF.md). 0.1.0 is its frame only: the program, its window, its name and version, built
// from the shared core (src/core) like fermata - the sample rates it shows are fermata's own list, read from that core, not a copy. The recorder
// (its own small process), the Primary and Fixer windows and the rest follow in the next builds.
#include <juce_gui_extra/juce_gui_extra.h>
#include "../core/Project.h"
#include "../core/Compat.h"
#include <cmath>

#ifndef FERMATA_LIVE_VERSION_STRING
 #define FERMATA_LIVE_VERSION_STRING "0.0.0"      // only when built without CMake
#endif

namespace td::live
{
/** The program's name on screen. Never use it in a file or folder name - Windows allows no colon there: kFileName is the name for files. */
constexpr const char* kDisplayName = "fermata:Live";
constexpr const char* kFileName    = "fermata-Live";

/** "44.1 / 48 / 96 / 192 kHz": the project sample rates fermata offers - the same list (Project::kProjectRates in the shared core). */
static juce::String sampleRatesText()
{
    juce::StringArray parts;
    for (auto r : Project::kProjectRates)
        parts.add (juce::String (r / 1000.0, std::fmod (r, 1000.0) == 0.0 ? 0 : 1));
    return parts.joinIntoString (" / ") + " kHz";
}

/** The window's content in this first build: the name, the version and what the program is built from. */
class FrameComponent final : public juce::Component
{
public:
    FrameComponent() { setSize (640, 256); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1b1f23));
        auto r = getLocalBounds().reduced (36, 30);

        auto top = r.removeFromTop (56);
        g.setColour (juce::Colour (0xffd8342c));                                      // the record light
        g.fillEllipse (top.removeFromLeft (22).withSizeKeepingCentre (18, 18).toFloat());
        top.removeFromLeft (12);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (40.0f, juce::Font::bold));
        g.drawText (kDisplayName, top, juce::Justification::centredLeft, false);

        g.setColour (juce::Colour (0xff8f9aa5));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (juce::String ("Version ") + FERMATA_LIVE_VERSION_STRING + " - the recorder for live concerts", r.removeFromTop (24), juce::Justification::centredLeft, true);
        r.removeFromTop (22);

        const std::pair<const char*, juce::String> rows[] = {
            { "This build",    "The program's frame. The recorder and its windows come in the next builds." },
            { "Sample rates",  sampleRatesText() + " (the same list as fermata)" },
            { "Built with",    "fermata's shared core - compatibility contract " + juce::String (compat::kContractVersion) },
        };
        for (auto& [label, text] : rows)
        {
            auto line = r.removeFromTop (30);
            g.setColour (juce::Colour (0xff8f9aa5));
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (label, line.removeFromLeft (130), juce::Justification::centredLeft, false);
            g.setColour (juce::Colour (0xffe4e8ec));
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (text, line, juce::Justification::centredLeft, true);
        }
    }
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : juce::DocumentWindow (juce::String (kDisplayName) + " " + FERMATA_LIVE_VERSION_STRING, juce::Colour (0xff1b1f23),
                                juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new FrameComponent(), true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class LiveApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return kDisplayName; }
    const juce::String getApplicationVersion() override { return FERMATA_LIVE_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }          // one recorder at a time: two would fight over the audio driver

    void initialise (const juce::String&) override      { window = std::make_unique<MainWindow>(); }
    void shutdown() override                             { window.reset(); }
    void systemRequestedQuit() override                  { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (window != nullptr) window->toFront (true); }

private:
    std::unique_ptr<MainWindow> window;
};
} // namespace td::live

START_JUCE_APPLICATION (td::live::LiveApplication)
