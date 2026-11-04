// fermata:Live - the live-concert recorder (see docs/LIVE-BRIEF.md). One program, three ways of running:
//   fermata-Live.exe                      the Primary station (0.3.0): the main window, the take window, the mixer... (PrimaryApp), which starts
//                                         and talks to
//   fermata-Live.exe --recorder <port>    the recorder process (RecorderService): the audio device, the session, the merge, the monitoring - and
//                                         nothing else, so nothing else can stop a recording. It runs at high priority and keeps going when the
//                                         windows close while a take records (or the user keeps the pre-take running); otherwise closing the
//                                         window closes it too (0.3.1).
//   fermata-Live.exe --fixer [--port n]   the Fixer station (0.3.0): the delayed take window, its own mixer, ReTouch, replies (FixerApp).
//   (--testbench: 0.2.0's recorder test bench, kept for checking the recorder on its own.)
// All are built from the shared core (src/core) and fermata's shared look (COMPATIBILITY.md), like fermata.
#include <juce_gui_extra/juce_gui_extra.h>
#include "RecorderWindow.h"
#include "RecorderService.h"
#include "LiveMainWindow.h"
#include "FixerApp.h"
#include "../ui/FermataLookAndFeel.h"
#include "../ui/ClosingLog.h"
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
                primary->onRecorderClosed = [this] { finishClosing(); };
                mainWindow = std::make_unique<LiveMainWindow> (*primary);
                mainWindow->onClose = [this] { quitPrimary(); };
            }
        }
    }
    /** The Primary's window closes (its X, Alt+F4, or Windows asking). 0.3.1: closing lets go of everything - the recorder, the audio device, the
        drives - unless the user says otherwise. (Until 0.3.0 the recorder was left running whenever the pre-take was on, which after any take it
        always is: it kept the audio device and went on writing to the drives after fermata:Live had closed.)
        - A take recording: asked first; the recording goes on (the recorder is a program of its own).
        - The pre-take on, or a take still to be merged: asked - Close everything (the session ends and the audio device is let go at once;
          fermata:Live waits for the merge, so that its take is in the project, then closes), Keep the pre-take running (only the window closes:
          the recorder goes on and is found again next time), or Cancel.
        - Nothing running: it closes at once, and the recorder with it. */
    void quitPrimary()
    {
        if (primary == nullptr) { quit(); return; }
        if (closingBox != nullptr) { closeNow(); return; }                     // (closed again while it waits for the merge: close now)
        if (asking) return;                                                     // (the question is up already)
        if (primary->isRecording())
        {
            asking = true;
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("A take is recording")
                .withMessage ("Close fermata:Live while a take records? The recording goes on (the recorder is a program of its own); start fermata:Live again to see it and stop it.")
                .withButton ("Close the window, keep recording").withButton ("Cancel"),
                [this] (int r) { asking = false; if (r == 1 && primary != nullptr) { primary->closeWindowOnly(); quit(); } });
            return;
        }
        const auto g = primary->stillGoing();
        if (! g.session && g.takesToMerge == 0) { closeEverything(); return; }
        juce::String msg;
        if (g.session) msg << "The pre-take is still running: the recorder keeps the audio device and goes on writing to the drives until the session ends.\n\n";
        if (g.takesToMerge > 0) msg << (g.takesToMerge == 1 ? juce::String ("A take is") : juce::String (g.takesToMerge) + " takes are") << " still being merged.\n\n";
        msg << "Close everything: " << (g.session ? "the session ends, " : "") << "the audio device is let go at once"
            << (g.takesToMerge > 0 ? ", and fermata:Live closes as soon as the merge has finished and its take is in the project." : ", and fermata:Live closes.");
        if (g.session) msg << "\n\nKeep the pre-take running: only the window closes. The recorder goes on, and fermata:Live finds it again when it starts.";
        auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Close fermata:Live").withMessage (msg)
                                                .withButton (g.session || g.takesToMerge == 0 ? "Close everything" : "Close when it has finished");
        if (g.session) options = options.withButton ("Keep the pre-take running");
        options = options.withButton ("Cancel");
        asking = true;
        // (the results: with three buttons 1, 2 and 0 for the last; with two, 1 and 0)
        juce::AlertWindow::showAsync (options, [this, keepOffered = g.session] (int r)
        {
            asking = false;
            if (primary == nullptr) return;
            if (r == 1) closeEverything();
            else if (r == 2 && keepOffered) { primary->closeWindowOnly(); quit(); }
        });
    }
    /** Close everything: the recorder ends the session and lets go of the audio device at once, and closes once its merges have finished. With a
        merge still to finish, fermata:Live waits for it (the box), so that its take is in the project, and closes when the recorder has. */
    void closeEverything()
    {
        if (primary->isRecording()) { quitPrimary(); return; }                 // (a take started meanwhile: asked about that instead)
        if (! primary->closeEverything()) { quit(); return; }
        closingProgress = -1.0;
        closingBox = std::make_unique<juce::AlertWindow> ("Closing fermata:Live", closingText(), juce::MessageBoxIconType::NoIcon, mainWindow.get());
        closingBox->addProgressBarComponent (closingProgress);
        closingBox->addButton ("Close now", 1);
        closingBox->setEscapeKeyCancels (false);                               // (it stays until the recorder has closed, or Close now)
        // (however the box goes - Close now, Alt+F4, the taskbar's Close window, which JUCE's alert turns into result 0 - it means Close now: a box
        // that went without closing would leave the window usable while it waits)
        closingBox->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int) { if (! closingDone) closeNow(); }), false);
        closingTicker = std::make_unique<Ticker>();
        closingTicker->tick = [this]
        {
            if (closingBox == nullptr || primary == nullptr) return;
            closingBox->setMessage (closingText());
            closingProgress = primary->model.mergeActive ? juce::jlimit (0.0, 1.0, primary->model.mergeFraction) : -1.0;
        };
        closingTicker->startTimer (250);
    }
    juce::String closingText() const
    {
        juce::String t;
        const auto& m = primary->model;
        if (m.mergeActive && m.mergeTake > 0) t << "Take " << m.mergeTake << " is being merged";
        else t << "The last take is being merged";
        if (m.mergeQueued > 0) t << " (" << m.mergeQueued << " more waiting)";
        t << ". When it has finished it goes into the project, and fermata:Live closes. The audio device has been let go.\n\n"
          << "Close now: fermata:Live closes at once. The recorder finishes the merge by itself and then closes; the take goes into the project the next time fermata:Live opens.";
        return t;
    }
    void closeNow()
    {
        if (primary != nullptr) primary->addLog ("Closing now: the recorder finishes the merge by itself and closes; the take goes into the project the next time fermata:Live opens");
        finishClosing();
    }
    /** The recorder has closed (or the user stopped waiting): fermata:Live closes. */
    void finishClosing()
    {
        if (closingDone) return;
        closingDone = true;
        closingTicker.reset();
        if (closingBox != nullptr) closingBox->exitModalState (0);
        quit();
    }
    void shutdown() override
    {
        closing::begin();                       // 0.3.1: fermata's closing log - every step into this program's log, with its time (src/ui/ClosingLog.h)
        closingTicker.reset();
        closingBox.reset();
        if (service != nullptr) { service->closeForQuit(); closing::step ("the session ended (its drives finished)"); }
        if (primary != nullptr) { primary->finalSave(); closing::step ("the project saved"); }
        if (fixer != nullptr)   { fixer->saveSettings(); closing::step ("the Fixer's settings saved"); }
        // fermata's closing safety net (3.17.0): from here on nothing can be lost - if closing has not finished 15 s from now, or the program is still
        // running 20 s after it finished, where every part of it is goes into its log and it ends itself. A closed fermata:Live can then never stay
        // behind holding the audio device, the drives or memory.
        ClosingWatchdog::start ((unsigned long) (juce::pointer_sized_int) juce::Thread::getCurrentThreadId(), crashLogFile);
        if (mainWindow != nullptr) { if (auto* m = mainWindow->main()) m->closeAllWindows(); closing::step ("the windows closed"); }
        if (mainWindow != nullptr) { mainWindow.reset(); closing::step ("the main window closed"); }
        if (primary != nullptr)    { primary.reset(); closing::step ("the project and the Primary's connections let go"); }
        if (fixer != nullptr)      { fixer.reset(); closing::step ("the Fixer let go"); }
        if (window != nullptr)     { window.reset(); closing::step ("the test bench closed"); }
        if (service != nullptr)    { service.reset(); closing::step ("the recorder let go"); }
        tooltips.reset();
        focusKeeper.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        look.reset();
        stationLock.reset();
        closing::end();
        ClosingWatchdog::finished();
        juce::Logger::writeToLog (juce::String (kDisplayName) + " closed");
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }
    void systemRequestedQuit() override
    {
        if (primary != nullptr && closingBox != nullptr)
        {   // (0.3.1: Windows shutting down or logging off while fermata:Live waits for a merge: it stays until the merge is done, so Windows shows it
            // as keeping the PC from shutting down - "Shut down anyway" ends it, and the take is merged again the next time fermata:Live opens)
            primary->addLog ("Windows asked fermata:Live to close: it closes as soon as the merge has finished (Close now closes it at once)");
            return;
        }
        if (primary != nullptr) quitPrimary(); else quit();
    }
    void anotherInstanceStarted (const juce::String&) override { if (window != nullptr) window->toFront (true); }

private:
    struct Ticker final : juce::Timer { std::function<void()> tick; void timerCallback() override { if (tick) tick(); } };
    bool recorderMode = false, fixerMode = false;
    bool asking = false;                                    // (a closing question is up)
    bool closingDone = false;                               // (finishClosing has run)
    std::unique_ptr<juce::AlertWindow> closingBox;          // Close everything, waiting for the merge (0.3.1)
    double closingProgress = -1.0;
    std::unique_ptr<Ticker> closingTicker;
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
