// fermata:Live - the live-concert recorder (see docs/LIVE-BRIEF.md). One program, three ways of running:
//   fermata-Live.exe                      the Primary station (0.3.0): the main window, the take window, the mixer... (PrimaryApp), which starts
//                                         and talks to
//   fermata-Live.exe --recorder <port>    the recorder process (RecorderService): the audio device, the session, the merge, the monitoring - and
//                                         nothing else, so nothing else can stop a recording. It runs at high priority and keeps going when the
//                                         windows close.
//   fermata-Live.exe --fixer [--port n]   the Fixer station (0.3.0): the delayed take window, its own mixer, ReTouch, replies (FixerApp).
//   (--testbench: 0.2.0's recorder test bench, kept for checking the recorder on its own.)
// All are built from the shared core (src/core) and fermata's shared look (COMPATIBILITY.md), like fermata.
#include <juce_gui_extra/juce_gui_extra.h>
#include "RecorderWindow.h"
#include "RecorderService.h"
#include "LiveMainWindow.h"
#include "FixerApp.h"
#include "../ui/FermataLookAndFeel.h"
#include "../core/CrashReport.h"
#include <csignal>

#ifndef FERMATA_LIVE_VERSION_STRING
 #define FERMATA_LIVE_VERSION_STRING "0.0.0"      // only when built without CMake
#endif

namespace td::live
{
/** The program's name on screen. Never use it in a file or folder name - Windows allows no colon there: kFileName is the name for files. */
constexpr const char* kDisplayName = "fermata:Live";
constexpr const char* kFileName    = "fermata-Live";

static juce::File logFolder() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Fermata"); }
static juce::File crashLogFile;                      // this process's own log (the window's or the recorder's)

/** Called by Windows when the program crashes: what happened, where, into the log (and a crash report, as fermata's). */
static void crashHandler (void* exceptionPointers)
{
    const auto report = crashreport::write (exceptionPointers, "crash");
    auto f = crashLogFile != juce::File() ? crashLogFile : logFolder().getChildFile ("fermata-live-log.txt");
    juce::String text ("\n*** CRASH ***");
   #if JUCE_WINDOWS
    if (const auto what = crashreport::describeException (exceptionPointers); what.isNotEmpty()) text << " " << what;
   #endif
    text << "\n";
    if (report != juce::File()) text << "Crash report written: " << report.getFullPathName() << "\n";
    text << juce::SystemStats::getStackBacktrace() << "\n";
    f.appendText (text);
}

class LiveApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return kDisplayName; }
    const juce::String getApplicationVersion() override { return FERMATA_LIVE_VERSION_STRING; }
    /** The recorder process runs beside the window (and fermata's freeze-report helper beside either). */
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = getCommandLineParameterArray();
        recorderMode = args.contains ("--recorder");
        fixerMode = args.contains ("--fixer");
        logFolder().createDirectory();
        const auto logFile = logFolder().getChildFile (recorderMode ? "fermata-live-recorder-log.txt" : fixerMode ? "fermata-live-fixer-log.txt" : "fermata-live-log.txt");
        crashLogFile = logFile;
        // (the log is kept across starts - the previous run's log is what shows why a recorder died; the newest 4 MB stay, as fermata keeps its own)
        logger = std::make_unique<juce::FileLogger> (logFile, juce::String (kDisplayName) + " " + FERMATA_LIVE_VERSION_STRING + (recorderMode ? " (recorder process)" : fixerMode ? " (Fixer)" : "") + " started", 4 * 1024 * 1024);
        juce::Logger::setCurrentLogger (logger.get());
        crashreport::install (logFolder().getChildFile ("Crash reports"), FERMATA_LIVE_VERSION_STRING);
        juce::SystemStats::setApplicationCrashHandler (crashHandler);
        juce::Logger::writeToLog ("Command line: " + commandLine);
        int port = RecorderService::kDefaultPort;
        for (int i = 0; i + 1 < args.size(); ++i)
            if ((args[i] == "--recorder" || args[i] == "--port") && args[i + 1].getIntValue() > 0) port = args[i + 1].getIntValue();
        if (recorderMode)
        {
            juce::Process::setPriority (juce::Process::HighPriority);        // (the recording comes before anything else on the machine)
            service = std::make_unique<RecorderService> (port, FERMATA_LIVE_VERSION_STRING);
            service->onQuit = [this] { juce::MessageManager::callAsync ([this] { quit(); }); };
            if (! service->isListening()) { juce::Logger::writeToLog ("The recorder could not listen on port " + juce::String (port) + ": quitting"); quit(); }
        }
        else
        {
            look = std::make_unique<FermataLookAndFeel>();
            juce::LookAndFeel::setDefaultLookAndFeel (look.get());
            tooltips = std::make_unique<juce::TooltipWindow> (nullptr, 900);
            focusKeeper = std::make_unique<ToolWindow::FocusKeeper>();
            if (args.contains ("--testbench")) { window = std::make_unique<RecorderWindow> (FERMATA_LIVE_VERSION_STRING, port); return; }
            // one station of each kind on a PC: a second Primary (or Fixer) would fight the first over the project and the windows
            stationLock = std::make_unique<juce::InterProcessLock> (fixerMode ? "fermata-Live-fixer-" + juce::String (port) : "fermata-Live-primary-" + juce::String (port));
            if (! stationLock->enter (0))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, kDisplayName,
                    juce::String (fixerMode ? "The Fixer" : "fermata:Live") + " is already open on this PC (look for its window, or in the taskbar).", "OK", nullptr,
                    juce::ModalCallbackFunction::create ([this] (int) { quit(); }));
                return;
            }
            if (fixerMode)
            {
                fixer = std::make_unique<FixerApp> (FERMATA_LIVE_VERSION_STRING, port);
                fixer->onQuit = [this] { quit(); };
            }
            else
            {
                primary = std::make_unique<PrimaryApp> (FERMATA_LIVE_VERSION_STRING, port);
                if (primary->recentProject().existsAsFile()) { juce::String err; if (! primary->openProject (primary->recentProject(), err)) primary->addLog ("The last project did not open: " + err); }
                mainWindow = std::make_unique<LiveMainWindow> (*primary);
                mainWindow->onClose = [this] { quitPrimary(); };
            }
        }
    }
    /** The Primary's window closes: the project is saved and the recorder told to quit - unless it records, merges or has a session on (then it is
        left running on purpose, and found again next time). */
    void quitPrimary()
    {
        if (primary == nullptr) { quit(); return; }
        if (primary->isRecording())
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("A take is recording")
                .withMessage ("Close fermata:Live while a take records? The recording goes on (the recorder is a program of its own); start fermata:Live again to see it and stop it.")
                .withButton ("Close the window, keep recording").withButton ("Cancel"),
                [this] (int r) { if (r == 1) { primary->shuttingDown(); quit(); } });
            return;
        }
        primary->shuttingDown();
        quit();
    }
    void shutdown() override
    {
        if (mainWindow != nullptr) { if (auto* m = mainWindow->main()) m->closeAllWindows(); }
        mainWindow.reset();
        primary.reset();
        fixer.reset();
        window.reset();
        service.reset();
        tooltips.reset();
        focusKeeper.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        look.reset();
        stationLock.reset();
        juce::Logger::writeToLog (juce::String (kDisplayName) + " closed");
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }
    void systemRequestedQuit() override                  { if (primary != nullptr) quitPrimary(); else quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (window != nullptr) window->toFront (true); }

private:
    bool recorderMode = false, fixerMode = false;
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<FermataLookAndFeel> look;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<ToolWindow::FocusKeeper> focusKeeper;
    std::unique_ptr<juce::InterProcessLock> stationLock;
    std::unique_ptr<RecorderWindow> window;
    std::unique_ptr<PrimaryApp> primary;
    std::unique_ptr<LiveMainWindow> mainWindow;
    std::unique_ptr<FixerApp> fixer;
    std::unique_ptr<RecorderService> service;
};
} // namespace td::live

START_JUCE_APPLICATION (td::live::LiveApplication)
