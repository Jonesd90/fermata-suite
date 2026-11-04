// fermata:Live - the live-concert recorder (see docs/LIVE-BRIEF.md). One program, two ways of running:
//   fermata-Live.exe                      the window (0.2.0: the recorder's test bench, RecorderWindow), which starts and talks to
//   fermata-Live.exe --recorder <port>    the recorder process (RecorderService): the audio device, the session, the merge - and nothing else,
//                                         so nothing else can stop a recording. It runs at high priority and keeps going when the window closes.
// Both are built from the shared core (src/core), like fermata.
#include <juce_gui_extra/juce_gui_extra.h>
#include "RecorderWindow.h"
#include "RecorderService.h"
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
        logFolder().createDirectory();
        const auto logFile = logFolder().getChildFile (recorderMode ? "fermata-live-recorder-log.txt" : "fermata-live-log.txt");
        crashLogFile = logFile;
        // (the log is kept across starts - the previous run's log is what shows why a recorder died; the newest 4 MB stay, as fermata keeps its own)
        logger = std::make_unique<juce::FileLogger> (logFile, juce::String (kDisplayName) + " " + FERMATA_LIVE_VERSION_STRING + (recorderMode ? " (recorder process)" : "") + " started", 4 * 1024 * 1024);
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
            window = std::make_unique<RecorderWindow> (FERMATA_LIVE_VERSION_STRING, port);
    }
    void shutdown() override
    {
        window.reset();
        service.reset();
        juce::Logger::writeToLog (juce::String (kDisplayName) + " closed");
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }
    void systemRequestedQuit() override                  { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (window != nullptr) window->toFront (true); }

private:
    bool recorderMode = false;
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<RecorderWindow> window;
    std::unique_ptr<RecorderService> service;
};
} // namespace td::live

START_JUCE_APPLICATION (td::live::LiveApplication)
