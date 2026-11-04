#include "RecorderLink.h"
#include "../core/TextBytes.h"
#include "../core/CrashReport.h"
#include "../ui/WinOwner.h"

namespace td::live
{
RecorderLink::RecorderLink (int p) : juce::Thread ("fermata:Live recorder link"), port (p) {}
RecorderLink::~RecorderLink() { stop(); }

void RecorderLink::start (bool launchIfNone)
{
    wantLaunch = launchIfNone;
    if (! isThreadRunning()) startThread();
}

void RecorderLink::stop()
{
    signalThreadShouldExit();                                     // (the thread looks every 100 ms at most, and closes the socket itself)
    stopThread (5000);
    connected = false;
}

bool RecorderLink::launchRecorder()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::String error;
    if (! startProgram ({ exe.getFullPathName(), "--recorder", juce::String (port) }, error)) { juce::Logger::writeToLog ("The recorder could not be started: " + error); return false; }
    launched = true;
    return true;
}

void RecorderLink::send (const juce::var& command)
{
    // queued for the link thread: the socket is its alone (a write from here could land on a socket it is closing or making again)
    const auto line = (juce::JSON::toString (command, true) + "\n").toStdString();
    { const std::lock_guard<std::mutex> l (outLock); if (outbox.size() < (size_t) 4 << 20) outbox += line; }
    notify();
}

void RecorderLink::run()
{
    crashreport::nameThisThread (getThreadName());
    int failed = 0;
    juce::int64 lastLaunchMs = 0;
    std::string in;
    char buf[8192];
    while (! threadShouldExit())
    {
        if (! connected.load())
        {
            if (sock.connect ("127.0.0.1", port, 400))
            {
                connected = true; failed = 0; in.clear();
                { const std::lock_guard<std::mutex> l (outLock); outbox = "{\"cmd\":\"hello\"}\n" + outbox; }      // (hello first; what was queued while away follows)
                juce::MessageManager::callAsync ([this] { if (onConnection) onConnection (true); });
                continue;
            }
            ++failed;
            const auto now = juce::Time::currentTimeMillis();
            if (wantLaunch.load() && failed >= 4 && now - lastLaunchMs > 8000) { lastLaunchMs = now; launchRecorder(); }
            wait (500);
            continue;
        }
        // first what is to be sent
        std::string out;
        { const std::lock_guard<std::mutex> l (outLock); out.swap (outbox); }
        bool lost = false;
        for (size_t sent = 0; sent < out.size() && ! lost;)
        {
            const int n = sock.write (out.data() + sent, (int) juce::jmin ((size_t) 65536, out.size() - sent));
            if (n < 0) lost = true; else sent += (size_t) n;
        }
        if (lost) { connected = false; sock.close(); juce::MessageManager::callAsync ([this] { if (onConnection) onConnection (false); }); continue; }
        const int ready = sock.waitUntilReady (true, 100);
        if (ready < 0) { connected = false; sock.close(); juce::MessageManager::callAsync ([this] { if (onConnection) onConnection (false); }); continue; }
        if (ready == 0) continue;
        const int n = sock.read (buf, (int) sizeof (buf), false);
        if (n <= 0) { connected = false; sock.close(); juce::MessageManager::callAsync ([this] { if (onConnection) onConnection (false); }); continue; }
        in.append (buf, (size_t) n);
        size_t from = 0;
        for (;;)
        {
            const auto nl = in.find ('\n', from);
            if (nl == std::string::npos) break;
            const auto line = textFromBytes (in.data() + from, nl - from).trim();
            from = nl + 1;
            if (line.isEmpty()) continue;
            const auto v = juce::JSON::parse (line);
            if (v.isObject()) juce::MessageManager::callAsync ([this, v] { if (onEvent) onEvent (v); });
        }
        in.erase (0, from);
        if (in.size() > (size_t) 4 << 20) in.clear();
    }
    // the last lines before the socket goes - the window's "quit" as it closes (else the recorder, with nobody left, would keep the audio device):
    // sent as far as the recorder takes them within a second
    if (connected.load())
    {
        std::string out;
        { const std::lock_guard<std::mutex> l (outLock); out.swap (outbox); }
        const auto until = juce::Time::getMillisecondCounter() + 1000;
        for (size_t sent = 0; sent < out.size() && juce::Time::getMillisecondCounter() < until;)
        {
            const int n = sock.write (out.data() + sent, (int) juce::jmin ((size_t) 65536, out.size() - sent));
            if (n < 0) break;
            sent += (size_t) n;
            if (n == 0) juce::Thread::sleep (10);
        }
    }
    sock.close();
    connected = false;
}
} // namespace td::live
