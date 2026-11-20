#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "Ltc.h"
#include <atomic>
#include <cmath>
#include <memory>

namespace td
{
/** 3.56.0 (asked for on 20 November 2026: the Hapi's LTC_in and LTC_out, reached through rtpMIDI): timecode as MIDI (MTC) instead of as audio.

    A Merging Hapi or Horus turns the LTC on its sync connector into MIDI timecode on the network, and turns MIDI timecode from the network into LTC
    on its connector; rtpMIDI makes each of those a MIDI port on the PC. fermata's timecode lives on the LTC track as audio (the takes' source
    timecode, the AAF, the video export all read it from there), so MIDI timecode coming in is turned back into LTC audio here, following the MTC,
    and that is what the LTC track records - everything after it works as it always has. Going out, the same LTC generator as for an audio output
    runs, and where it is is sent as MTC.

    MTC is less exact than LTC on an audio channel: it comes as small messages a quarter of a frame apart, with the network's wobble on them. The
    LTC made from it is placed by when those messages arrive (smoothed, and set again only when it is more than a frame out), so it can sit a
    millisecond or so off where LTC on an audio input would have been - the price of timecode that is not audio.

    A header only; the audio engine is the one user. */
namespace mtc
{
/** The MTC rate code (0 = 24, 1 = 25, 2 = 29.97 drop-frame, 3 = 30) -> LtcGenerator's format code. 'pulledDown': the code says 24 or 30 but the
    frames come 1000/1001 slower (23.976 / 29.97 non-drop, which MTC has no codes of its own for). */
inline int formatCodeOf (int rateCode, bool pulledDown) noexcept
{
    switch (rateCode)
    {
        case 0:  return pulledDown ? 5 : 0;
        case 1:  return 1;
        case 2:  return 2;
        default: return pulledDown ? 3 : 4;
    }
}
/** LtcGenerator's format code -> the MTC rate code. */
inline int rateCodeOf (int formatCode) noexcept
{
    switch (formatCode) { case 0: case 5: return 0; case 2: return 2; case 3: case 4: return 3; default: return 1; }
}
/** The steady clock that MIDI arrivals and audio blocks are both timed by, in seconds. */
inline double now() noexcept { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
inline double wrapDay (double s) noexcept { s = std::fmod (s, 86400.0); return s < 0.0 ? s + 86400.0 : s; }
/** a - b, the short way round midnight. */
inline double daySpan (double a, double b) noexcept { double d = std::fmod (a - b, 86400.0); if (d > 43200.0) d -= 86400.0; if (d < -43200.0) d += 86400.0; return d; }
} // namespace mtc

/** Reads MIDI timecode: quarter-frame messages (running) and full-frame messages (a locate while stopped). feed() on the MIDI thread; ref() from any
    thread (the audio thread). */
class MtcReader
{
public:
    struct Ref
    {
        bool   valid = false;       // timecode has been read
        bool   running = false;     // from quarter frames (false: a full-frame locate)
        double tc = 0.0;            // the timecode at 'at', as time of day in seconds (frames / fps)
        double at = 0.0;            // when, on mtc::now()'s clock
        int    formatCode = 1;      // LtcGenerator's
    };

    void feed (const juce::uint8* d, int n, double arrived) noexcept
    {
        if (n >= 2 && d[0] == 0xF1) { quarter ((d[1] >> 4) & 7, d[1] & 15, arrived); return; }
        // full frame: F0 7F <device> 01 01 hh mm ss ff F7 (some drivers hand it over without the F0)
        const int o = (n >= 1 && d[0] == 0xF0) ? 1 : 0;
        if (n - o >= 8 && d[o] == 0x7F && d[o + 2] == 0x01 && d[o + 3] == 0x01)
        {
            const int hr = d[o + 4], rateCode = (hr >> 5) & 3, h = hr & 31, m = d[o + 5] & 63, s = d[o + 6] & 63, f = d[o + 7] & 31;
            const int nominal = rateCode == 0 ? 24 : rateCode == 1 ? 25 : 30;
            if (h > 23 || m > 59 || s > 59 || f >= nominal) return;
            have = false; lastPiece = -1; mask = 0; runAt = -1.0;
            const int code = mtc::formatCodeOf (rateCode, pulled && rateCode == rate);
            publish (false, framesToSeconds ((double) frameIndexOf (h, m, s, f, nominal, rateCode == 2), code), arrived, code);
        }
    }
    void feed (const juce::MidiMessage& msg, double arrived) noexcept { feed (msg.getRawData(), msg.getRawDataSize(), arrived); }

    Ref ref() const noexcept
    {
        for (int tries = 0; tries < 8; ++tries)
        {
            const auto s0 = seq.load (std::memory_order_acquire);
            if (s0 & 1u) continue;
            Ref r; r.valid = rValid.load (std::memory_order_relaxed); r.running = rRunning.load (std::memory_order_relaxed);
            r.tc = rTc.load (std::memory_order_relaxed); r.at = rAt.load (std::memory_order_relaxed); r.formatCode = rCode.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_relaxed) == s0) return r;
        }
        return {};
    }
    /** Arriving now (a quarter frame within the last quarter of a second). */
    bool isRunning (double nowSeconds) const noexcept { const auto r = ref(); return r.valid && r.running && nowSeconds - r.at < 0.25 && nowSeconds - r.at > -1.0; }

private:
    static double framesToSeconds (double frames, int code) noexcept { double fps; int nom; bool dr; LtcGenerator::formatInfo (code, fps, nom, dr); return frames / fps; }

    void quarter (int piece, int value, double arrived) noexcept
    {
        const bool inOrder = lastPiece >= 0 && piece == (lastPiece + 1) % 8;
        lastPiece = piece;
        if (! inOrder) { have = false; runAt = -1.0; mask = 0; if (piece != 0) return; }      // a gap, or running backwards: wait for a whole sequence
        if (piece == 0) { mask = 0; qf = 0; if (have) baseFrames += 2; }                       // (a sequence of eight spans two frames)
        else ++qf;
        nib[piece] = value; mask |= 1 << piece;
        if (piece == 7 && mask == 0xFF)
        {
            const int f = nib[0] | ((nib[1] & 1) << 4), s = nib[2] | ((nib[3] & 3) << 4), m = nib[4] | ((nib[5] & 3) << 4), h = nib[6] | ((nib[7] & 1) << 4);
            const int rc = (nib[7] >> 1) & 3, nominal = rc == 0 ? 24 : rc == 1 ? 25 : 30;
            if (h > 23 || m > 59 || s > 59 || f >= nominal) { have = false; runAt = -1.0; return; }
            const long long frames = frameIndexOf (h, m, s, f, nominal, rc == 2);
            if (! have || frames != baseFrames || rc != rate) runAt = -1.0;                   // (a jump: the rate is measured again from here)
            if (rc != rate) pulled = false;
            rate = rc; baseFrames = frames; have = true; qf = 7;
        }
        if (! have) return;
        // 23.976 and 29.97 non-drop send the codes of 24 and 30: told apart by how fast the frames really come (0.1% slower), over 6 s or more
        const long long q = baseFrames * 4 + qf;
        if (runAt < 0.0) { runAt = arrived; runQ = q; }
        else if (arrived - runAt > 6.0 && (rate == 0 || rate == 3))
        {
            const double nominal = rate == 0 ? 24.0 : 30.0, measured = (double) (q - runQ) * 0.25 / (arrived - runAt);
            pulled = measured < nominal * (1.0 - 0.0005);
        }
        const int code = mtc::formatCodeOf (rate, pulled);
        publish (true, mtc::wrapDay (framesToSeconds ((double) baseFrames + qf * 0.25, code)), arrived, code);
    }

    void publish (bool running, double tc, double at, int code) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);
        rValid.store (true, std::memory_order_relaxed); rRunning.store (running, std::memory_order_relaxed);
        rTc.store (tc, std::memory_order_relaxed); rAt.store (at, std::memory_order_relaxed); rCode.store (code, std::memory_order_relaxed);
        seq.fetch_add (1, std::memory_order_release);
    }

    // MIDI thread
    int nib[8] = {}; int mask = 0, lastPiece = -1, qf = 0, rate = 1; bool have = false, pulled = false;
    long long baseFrames = 0, runQ = 0; double runAt = -1.0;
    // published
    std::atomic<unsigned> seq { 0 };
    std::atomic<bool> rValid { false }, rRunning { false }; std::atomic<double> rTc { 0.0 }, rAt { 0.0 }; std::atomic<int> rCode { 1 };
};

/** LTC (audio) made to follow a MtcReader: what the LTC track records when the timecode comes in as MIDI. Audio thread. */
class MtcChase
{
public:
    void prepare (double sampleRate) { gen.prepare (sampleRate); errAvg = 0.0; }
    /** Fills 'out' with LTC for a block whose first sample was heard at 'blockTime' (mtc::now()'s clock), or with silence when no timecode is
        arriving (as a stopped LTC source would leave the track). */
    void fill (const MtcReader& reader, double blockTime, float* out, int count) noexcept
    {
        const auto r = reader.ref();
        if (! r.valid || ! r.running || blockTime - r.at > 0.25 || blockTime - r.at < -1.0)
        {
            gen.stop(); errAvg = 0.0;
            juce::FloatVectorOperations::clear (out, count);
            return;
        }
        const double want = mtc::wrapDay (r.tc + (blockTime - r.at));
        if (! gen.isRunning() || gen.formatCode() != r.formatCode) { gen.startExact (r.formatCode, want); errAvg = 0.0; }
        else
        {
            double fps; int nom; bool dr; LtcGenerator::formatInfo (r.formatCode, fps, nom, dr);
            const double err = mtc::daySpan (want, gen.positionSeconds());
            if (std::abs (err) > 0.5) { gen.startExact (r.formatCode, want); errAvg = 0.0; }               // a jump in the timecode: follow it at once
            else
            {
                errAvg += 0.02 * (err - errAvg);                                                         // (the network's wobble averaged out)
                if (std::abs (errAvg) > 1.0 / fps) { gen.startExact (r.formatCode, mtc::wrapDay (gen.positionSeconds() + errAvg)); errAvg = 0.0; }
            }
        }
        gen.fill (out, count);
    }
    bool isRunning() const noexcept { return gen.isRunning(); }

private:
    LtcGenerator gen;
    double errAvg = 0.0;
};

/** A MIDI input port that timecode comes in on (opened and closed on the message thread). */
class MtcInput : private juce::MidiInputCallback
{
public:
    ~MtcInput() override { close(); }
    /** Opens the port of this name (the name rtpMIDI gave the session). False when there is no such port or it would not open. */
    bool open (const juce::String& name)
    {
        if (name == openName && port != nullptr) return true;
        close();
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (d.name == name)
            {
                port = juce::MidiInput::openDevice (d.identifier, this);
                if (port != nullptr) { port->start(); openName = name; return true; }
            }
        return false;
    }
    void close() { if (port != nullptr) { port->stop(); port.reset(); } openName = {}; }
    bool isOpen() const noexcept { return port != nullptr; }
    const juce::String& name() const noexcept { return openName; }
    const MtcReader& reader() const noexcept { return mtcReader; }

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override { mtcReader.feed (m, mtc::now()); }
    std::unique_ptr<juce::MidiInput> port;
    juce::String openName;
    MtcReader mtcReader;
};

/** Sends MIDI timecode on a port, following where an LtcGenerator is: the audio thread stamps that once a block, a 1 ms timer sends each quarter
    frame as its moment comes (and a full-frame message when the timecode starts or jumps). */
class MtcOutput : private juce::HighResolutionTimer
{
public:
    ~MtcOutput() override { close(); }
    bool open (const juce::String& name)
    {
        if (name == openName && port != nullptr) return true;
        close();
        for (auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.name == name)
            {
                port = juce::MidiOutput::openDevice (d.identifier);
                if (port != nullptr) { openName = name; sending = false; startTimer (1); return true; }
            }
        return false;
    }
    void close() { stopTimer(); port.reset(); openName = {}; }
    bool isOpen() const noexcept { return port != nullptr; }
    const juce::String& name() const noexcept { return openName; }

    /** Audio thread, once a block: the generator is running (or not), at 'positionSeconds' at the moment 'at' (mtc::now()'s clock). */
    void stamp (bool running, double positionSeconds, double at, int formatCode) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);
        sRunning.store (running, std::memory_order_relaxed); sPos.store (positionSeconds, std::memory_order_relaxed);
        sAt.store (at, std::memory_order_relaxed); sCode.store (formatCode, std::memory_order_relaxed);
        seq.fetch_add (1, std::memory_order_release);
    }

    /** The eight quarter-frame data bytes (after 0xF1) of the sequence that starts at frame 'frameIndex' (an even one). */
    static void quarterFrames (long long frameIndex, int formatCode, juce::uint8 out[8]) noexcept
    {
        double fps; int nominal; bool drop; LtcGenerator::formatInfo (formatCode, fps, nominal, drop);
        int h, m, s, f; timecodeFields (frameIndex, nominal, drop, h, m, s, f);
        const int v[8] = { f & 15, f >> 4, s & 15, s >> 4, m & 15, m >> 4, h & 15, (h >> 4) | (mtc::rateCodeOf (formatCode) << 1) };
        for (int i = 0; i < 8; ++i) out[i] = (juce::uint8) ((i << 4) | v[i]);
    }

private:
    void hiResTimerCallback() override
    {
        bool running = false; double pos = 0.0, at = 0.0; int code = 1; bool got = false;
        for (int tries = 0; tries < 8 && ! got; ++tries)
        {
            const auto s0 = seq.load (std::memory_order_acquire);
            if (s0 & 1u) continue;
            running = sRunning.load (std::memory_order_relaxed); pos = sPos.load (std::memory_order_relaxed);
            at = sAt.load (std::memory_order_relaxed); code = sCode.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            got = seq.load (std::memory_order_relaxed) == s0;
        }
        const double t = mtc::now();
        if (! got || ! running || port == nullptr || t - at > 0.5) { sending = false; return; }
        double fps; int nominal; bool drop; LtcGenerator::formatInfo (code, fps, nominal, drop);
        const long long dayFrames = drop ? 2589408LL : (long long) nominal * 86400LL;
        const long long q = (long long) std::floor (mtc::wrapDay (pos + (t - at)) * fps * 4.0);
        if (! sending || code != sentCode || q < lastQ || q - lastQ > 16)
        {
            // starting, or a jump: say where with a full frame, then begin the quarter frames at the next sequence
            const long long frame = (q / 4) % dayFrames;
            int h, m, s, f; timecodeFields (frame, nominal, drop, h, m, s, f);
            const juce::uint8 full[] = { 0xF0, 0x7F, 0x7F, 0x01, 0x01, (juce::uint8) ((mtc::rateCodeOf (code) << 5) | h), (juce::uint8) m, (juce::uint8) s, (juce::uint8) f, 0xF7 };
            port->sendMessageNow (juce::MidiMessage (full, (int) sizeof (full)));
            sending = true; sentCode = code; lastQ = q - (q & 7) + 7;                      // (the next quarter frame sent is a sequence's first)
            return;
        }
        while (lastQ < q)
        {
            ++lastQ;
            const int piece = (int) (lastQ & 7);
            if (piece == 0 || seqBytesFor != (lastQ >> 3)) { seqBytesFor = lastQ >> 3; quarterFrames (((lastQ >> 3) * 2) % dayFrames, code, seqBytes); }
            port->sendMessageNow (juce::MidiMessage (0xF1, (int) seqBytes[piece]));
        }
    }

    std::unique_ptr<juce::MidiOutput> port;
    juce::String openName;
    // the timer's own
    bool sending = false; int sentCode = -1; long long lastQ = 0, seqBytesFor = -1; juce::uint8 seqBytes[8] = {};
    // stamped by the audio thread
    std::atomic<unsigned> seq { 0 };
    std::atomic<bool> sRunning { false }; std::atomic<double> sPos { 0.0 }, sAt { 0.0 }; std::atomic<int> sCode { 1 };
};
} // namespace td
