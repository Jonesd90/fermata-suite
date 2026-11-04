#pragma once
/** fermata:Live 0.2.0: the window's end of the control port. Connects to the recorder process on 127.0.0.1:port (starting one -
    "fermata-Live.exe --recorder <port>" - when none answers), sends it commands and hands its events to the window on the message thread.
    A recorder that is already running (the window was closed, or died, while it recorded) is found again on the same port and carried on with.
    If the connection goes, it tries again every half second, and starts a recorder again when none comes back (at most every 8 seconds: a second
    recorder started while the first is still coming up cannot take the port, and quits at once).
    0.3.0: the recorder is started with startProgram (src/ui/WinOwner.h), which hands it none of this program's open handles - with JUCE's
    ChildProcess it was given the window's network connections, which then stayed open in the recorder after the window had closed them. */
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <mutex>

namespace td::live
{
class RecorderLink final : private juce::Thread
{
public:
    explicit RecorderLink (int port);
    ~RecorderLink() override;

    std::function<void (const juce::var& event)> onEvent;          // message thread: every event the recorder sends
    std::function<void (bool connected)> onConnection;              // message thread: connected / lost

    /** Starts looking for the recorder; launchIfNone: starts one when nothing answers for about two seconds. */
    void start (bool launchIfNone);
    void send (const juce::var& command);
    bool isConnected() const noexcept        { return connected.load(); }
    bool startedTheRecorder() const noexcept { return launched.load(); }
    int  portNumber() const noexcept         { return port; }
    /** Stops looking (and the connection); the recorder is left running. */
    void stop();
    /** 0.3.1: closing - from now on no recorder is started when none answers (the one told to close must not be replaced by a new one). */
    void stopLaunching() noexcept            { wantLaunch = false; }

private:
    void run() override;
    bool launchRecorder();

    const int port;
    juce::StreamingSocket sock;                                     // (only the link thread touches it)
    std::mutex outLock; std::string outbox;                         // lines to send, written by the link thread
    std::atomic<bool> connected { false }, launched { false }, wantLaunch { false };
};
} // namespace td::live
