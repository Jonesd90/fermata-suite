#pragma once
/** fermata:Live 0.3.0: a station's end of the playback stream (see StreamProtocol.h): a thread that keeps the recorder's window full from the
    station's player and tells the player where the sound is. The player renders on this thread (render), as far ahead as the window allows and no
    further; a new generation (Play, Stop, a locate) drops what the recorder still has queued, so the sound follows the transport at once. */
#include "StreamProtocol.h"
#include <atomic>
#include <functional>
#include <mutex>

namespace td::live
{
class StreamClient final : private juce::Thread
{
public:
    /** port: the recorder's STREAM port (its control port + 1). name: how the recorder's log calls this station. */
    StreamClient (int port, const juce::String& name, int stationNumber)
        : juce::Thread ("fermata:Live playback stream"), streamPort (port), stationName (name), station (stationNumber) {}
    ~StreamClient() override { stop(); }

    /** Called on the stream's thread: fill 'frames' stereo frames (interleaved L R) from the player; return how many were filled (fewer, or 0, when
        the player has nothing more yet - the rest is sent later). Only called while playing. */
    std::function<int (float* interleaved, int frames)> render;

    void start()                               { if (! isThreadRunning()) startThread (juce::Thread::Priority::high); }
    void stop()                                { signalThreadShouldExit(); stopThread (4000); }
    bool isConnected() const noexcept          { return connected.load(); }

    /** The driver outputs this station plays to (0-based; right = -1 for one output). Any thread. */
    void setOutputs (int left, int right)      { outL = left; outR = right; dirty = true; notify(); }
    /** A new generation: what the recorder still has queued is dropped, and 'playing' says whether audio follows. Returns its number. Any thread. */
    juce::uint32 newGeneration (bool playing)
    {
        const auto g = generation.fetch_add (1) + 1;
        isPlaying = playing; dirty = true; notify();
        return g;
    }
    juce::uint32 currentGeneration() const noexcept { return generation.load(); }
    /** Frames of generation 'g' that have reached the speakers (0 until the recorder has started on it). */
    juce::uint64 playedFrames (juce::uint32 g) const noexcept { return creditGen.load() == g ? played.load() : 0; }
    double deviceRate() const noexcept         { return rate.load(); }
    juce::uint32 underruns() const noexcept    { return under.load(); }

private:
    void run() override
    {
        std::vector<float> buf ((size_t) stream::kMaxFramesPerPacket * 2);
        std::string in;
        char tmp[4096];
        while (! threadShouldExit())
        {
            juce::StreamingSocket sock;
            if (! sock.connect ("127.0.0.1", streamPort, 500)) { connected = false; wait (1000); continue; }
            char probe = 0; sock.read (&probe, 0, false);
            connected = true; in.clear();
            juce::uint32 sentGen = 0; juce::uint64 sent = 0;
            {
                stream::Hello h; h.station = station; h.outL = outL.load(); h.outR = outR.load(); h.generation = generation.load(); h.playing = isPlaying.load() ? 1 : 0;
                stationName.copyToUTF8 (h.name, sizeof (h.name) - 1);
                juce::MemoryBlock mb; stream::pack (mb, stream::kHello, &h, sizeof h);
                if (! stream::writeAll (sock, mb)) { connected = false; sock.close(); wait (500); continue; }
                sentGen = h.generation; dirty = false;
            }
            bool lost = false;
            while (! threadShouldExit() && ! lost)
            {
                if (dirty.exchange (false) || generation.load() != sentGen)
                {
                    stream::State st; st.outL = outL.load(); st.outR = outR.load(); st.generation = generation.load(); st.playing = isPlaying.load() ? 1 : 0;
                    if (st.generation != sentGen) { sentGen = st.generation; sent = 0; }
                    juce::MemoryBlock mb; stream::pack (mb, stream::kState, &st, sizeof st);
                    if (! stream::writeAll (sock, mb)) { lost = true; break; }
                }
                // how much the recorder has room for: its window less what it still holds of ours (what we sent less what it reported played)
                const juce::uint64 p = creditGen.load() == sentGen ? played.load() : 0;
                const juce::int64 held = (juce::int64) sent - (juce::int64) p;
                const int allowed = (int) juce::jlimit<juce::int64> (0, stream::kMaxFramesPerPacket, (juce::int64) window.load() - held);
                bool sentSomething = false;
                if (isPlaying.load() && allowed >= 256 && render)
                {
                    const int n = render (buf.data(), allowed);
                    if (n > 0)
                    {
                        stream::AudioHead ah; ah.generation = sentGen; ah.frames = (juce::uint32) n;
                        juce::MemoryBlock mb; stream::pack (mb, stream::kAudio, &ah, sizeof ah, buf.data(), (size_t) n * 2 * sizeof (float));
                        if (! stream::writeAll (sock, mb)) { lost = true; break; }
                        sent += (juce::uint64) n; sentSomething = true;
                    }
                }
                const int ready = sock.waitUntilReady (true, sentSomething ? 0 : 3);
                if (ready < 0) { lost = true; break; }
                if (ready > 0)
                {
                    const int n = sock.read (tmp, (int) sizeof (tmp), false);
                    if (n <= 0) { lost = true; break; }
                    in.append (tmp, (size_t) n);
                    if (! stream::unpack (in, [this] (juce::uint32 type, const char* d, size_t bytes)
                        {
                            if (type != stream::kCredit || bytes < sizeof (stream::Credit)) return;
                            stream::Credit c; std::memcpy (&c, d, sizeof c);
                            window = (int) c.window; played = c.played; creditGen = c.generation; rate = c.sampleRate; under = c.underruns;
                        }))
                        lost = true;
                }
            }
            connected = false;
            sock.close();
            if (! threadShouldExit()) wait (500);
        }
    }

    const int streamPort;
    const juce::String stationName;
    const int station;
    std::atomic<int> outL { 0 }, outR { 1 };
    std::atomic<juce::uint32> generation { 1 };
    std::atomic<bool> isPlaying { false }, dirty { true }, connected { false };
    std::atomic<int> window { 4096 };
    std::atomic<juce::uint64> played { 0 };
    std::atomic<juce::uint32> creditGen { 0 }, under { 0 };
    std::atomic<double> rate { 0.0 };
    JUCE_DECLARE_NON_COPYABLE (StreamClient)
};
} // namespace td::live
