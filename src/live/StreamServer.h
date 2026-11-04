#pragma once
/** fermata:Live 0.3.0: the recorder's end of the playback streams (see StreamProtocol.h). A listening thread on 127.0.0.1:<port>; one thread per
    connected station, which reads its packets into that station's slot (a ring the audio thread reads) and sends it credits; the audio thread
    adds every playing slot to its output pair. The audio thread never waits for a station: a slot with nothing in it is silence (counted as an
    underrun while the station says it is playing). Up to stream::kSlots stations at once.

    Threads: start / stop on the message thread; mix() on the audio thread; everything else on the server's own threads. */
#include "StreamProtocol.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

namespace td::live
{
class StreamServer : private juce::Thread
{
public:
    StreamServer();
    ~StreamServer() override;

    /** Listens on 127.0.0.1:port. False when the port cannot be opened. */
    bool start (int port);
    void stop();
    bool isListening() const noexcept         { return listening; }
    /** The device's rate (credits carry it; the window is sized from it). Any thread. */
    void setSampleRate (double r) noexcept    { rate.store (r); }
    /** 0.3.0: the device's block size: the window is at least two blocks (a driver block as big as the 85 ms window would leave a station no slack,
        and one bigger than it could never be filled). Any thread. */
    void setBlockSize (int frames) noexcept   { blockFrames.store (juce::jmax (0, frames)); }
    /** Called by the recorder's audio callback after every block (so a slot is never reset while the audio thread may still be in it). */
    void blockDone() noexcept                 { blocks.fetch_add (1, std::memory_order_release); }

    /** Audio thread: adds every playing station's audio to its output pair. */
    void mix (float* const* outputs, int numOutputs, int n) noexcept;

    struct SlotInfo { bool connected = false; juce::String name; int outL = -1, outR = -1; bool playing = false; juce::uint32 underruns = 0; int fill = 0; };
    std::vector<SlotInfo> info() const;
    std::function<void (const juce::String&)> log;      // (called on the server's threads)

private:
    struct Slot
    {
        std::atomic<bool> active { false }, playing { false };
        std::atomic<int> outL { -1 }, outR { -1 };
        std::atomic<juce::uint64> w { 0 }, r { 0 };      // frames written (by the station's thread) and read (by the audio thread)
        std::atomic<juce::uint64> flushTo { 0 };
        std::atomic<juce::uint32> flushSeq { 0 };
        std::atomic<juce::uint32> pendingGen { 0 }, audioGen { 0 };   // the generation asked for, and the one the audio thread plays now
        juce::uint32 flushSeen = 0;                      // audio thread
        std::atomic<juce::uint64> played { 0 };          // frames the audio thread took in this generation
        std::atomic<juce::uint32> underruns { 0 };
        std::vector<float> data;                         // kRingFrames x 2, interleaved
        juce::String name;                               // (the station's thread; read under nameLock)
        bool inUse = false;                              // a connection owns it (the listening thread; cleared by the connection when it ends)
    };
    class Connection;
    void run() override;                                 // accepts stations
    Slot* claimSlot();

    juce::StreamingSocket listener;
    bool listening = false;
    std::atomic<double> rate { 48000.0 };
    std::atomic<int> blockFrames { 0 };
    std::atomic<juce::uint64> blocks { 0 };
    Slot slots[stream::kSlots];
    mutable juce::CriticalSection slotLock;              // inUse and the names
    std::vector<std::unique_ptr<Connection>> connections;   // (slotLock)
    JUCE_DECLARE_NON_COPYABLE (StreamServer)
};
} // namespace td::live
