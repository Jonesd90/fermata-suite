#pragma once
/** fermata:Live 0.3.0: the playback streams - how a station (the Primary's or the Fixer's window) plays recorded audio through the recorder's audio
    device. ASIO lets one program use the driver, and that program is the recorder; so a station renders its own mix (its own player, its own
    mixer, in its own process) and sends the stereo result here, and the recorder adds it to the output pair the station chose. A station that
    crashes or stalls is silence; it can never reach the recording.

    The stream port is the control port + 1 (127.0.0.1 only). Binary packets, little-endian: a header {magic, type, payload bytes} and the payload.
      station -> recorder   Hello  {station, outL, outR, generation, playing, name[32]}  first, once
                            State  {outL, outR, generation, playing}                    the output pair, a new generation (Stop, a locate: what
                                                                                         is queued from the old one is dropped at once), playing
                            Audio  {generation, frames, frames x 2 floats (L R L R ...)}
      recorder -> station   Credit {free frames, window, frames played in this generation, underruns, sample rate}
    Credits: the station sends only as much as the recorder has room for in its window (about 85 ms of audio at any rate), so the latency from
    the station's player to the speakers is that window and no more; the frames played tell the station where its playhead is. */
#include <juce_core/juce_core.h>
#include <cstring>

namespace td::live::stream
{
constexpr juce::uint32 kMagic = 0x534c4d46;          // "FMLS"
enum Type : juce::uint32 { kHello = 1, kState = 2, kAudio = 3, kCredit = 10 };
constexpr int kMaxFramesPerPacket = 4096;
constexpr int kMaxPayload = 16 + kMaxFramesPerPacket * 2 * (int) sizeof (float);
constexpr int kSlots = 4;                            // stations playing at once
constexpr int kRingFrames = 32768;                   // each slot's ring (the window is a part of it)

struct Header  { juce::uint32 magic = kMagic, type = 0, bytes = 0; };
struct Hello   { juce::int32 station = 0, outL = 0, outR = 1; juce::uint32 generation = 1; juce::int32 playing = 0; char name[32] = {}; };
struct State   { juce::int32 outL = 0, outR = 1; juce::uint32 generation = 1; juce::int32 playing = 0; };
struct AudioHead { juce::uint32 generation = 0, frames = 0; };
struct Credit  { juce::uint32 freeFrames = 0, window = 0; juce::uint64 played = 0; juce::uint32 underruns = 0, generation = 0; double sampleRate = 0.0; };   // (played: frames of 'generation')

/** The window (frames a station may have queued) for a sample rate: the power of two nearest 85 ms - and (0.3.0) at least two of the device's
    blocks, so a station has a block of slack whatever buffer the driver runs at (at most half the ring). */
inline int windowFor (double rate, int deviceBlock = 0) noexcept
{
    const double want = 0.085 * juce::jmax (8000.0, rate);
    int w = 1024;
    while (w * 2 <= kRingFrames / 2 && (double) w * 1.41 < want) w *= 2;
    while (w * 2 <= kRingFrames / 2 && w < 2 * deviceBlock) w *= 2;
    return w;
}

/** A packet into 'out' (header + payload). */
inline void pack (juce::MemoryBlock& out, juce::uint32 type, const void* payload, size_t bytes, const void* extra = nullptr, size_t extraBytes = 0)
{
    Header h; h.type = type; h.bytes = (juce::uint32) (bytes + extraBytes);
    out.append (&h, sizeof h);
    if (bytes > 0) out.append (payload, bytes);
    if (extraBytes > 0) out.append (extra, extraBytes);
}

/** Sends all of 'data' on a socket that may be non-blocking (JUCE makes a socket non-blocking once it is read without waiting): a part at a time,
    waiting for room. False: the connection is gone, or nothing could be sent for 'timeoutMs'. */
inline bool writeAll (juce::StreamingSocket& sock, const void* data, size_t bytes, int timeoutMs = 2000)
{
    const char* p = static_cast<const char*> (data);
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (bytes > 0)
    {
        const int w = sock.waitUntilReady (false, 20);
        if (w < 0) return false;
        if (w == 0) { if (juce::Time::getMillisecondCounter() > until) return false; continue; }
        const int n = sock.write (p, (int) juce::jmin<size_t> (bytes, 65536));
        if (n <= 0) return false;
        p += n; bytes -= (size_t) n;
    }
    return true;
}
inline bool writeAll (juce::StreamingSocket& sock, const juce::MemoryBlock& mb, int timeoutMs = 2000) { return writeAll (sock, mb.getData(), mb.getSize(), timeoutMs); }

/** Takes whole packets off the front of 'buf' (the bytes read so far); 'handle' gets each one's type and payload. False: the stream is not ours
    (a wrong magic or an impossible size) and must be closed. */
template <typename Handler>
inline bool unpack (std::string& buf, Handler&& handle)
{
    size_t at = 0;
    while (buf.size() - at >= sizeof (Header))
    {
        Header h; std::memcpy (&h, buf.data() + at, sizeof h);
        if (h.magic != kMagic || h.bytes > (juce::uint32) kMaxPayload) return false;
        if (buf.size() - at - sizeof h < h.bytes) break;
        handle (h.type, buf.data() + at + sizeof h, (size_t) h.bytes);
        at += sizeof h + h.bytes;
    }
    buf.erase (0, at);
    return true;
}
} // namespace td::live::stream
