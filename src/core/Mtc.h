#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "Ltc.h"
#include <atomic>
#include <cmath>
#include <memory>
#include <thread>

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

    3.67.0 (the pass with Claude Fable over the timing, 25 November 2026): the audio blocks are timed by a steadied clock (mtc::BlockClock) and placed
    by the driver's own latencies, not by "one block ago"; the LTC made from MIDI timecode is trimmed in speed to stay on the timecode (a source
    whose clock is not the audio's drifts: it is followed without a break in the code) and set again only after a real jump; what the two threads
    hand each other can no longer be missed for a block (a missed read used to stop the LTC for that block); and what is left - the time the MIDI
    takes to arrive - is one offset, which LtcCompare measures against the same timecode as LTC on an audio input.

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

/** 3.67.0: when each audio block starts being worked on, steadied. The driver calls for a block a little late, by a different amount every time
    (and with the sample-rate converter the engine's blocks do not come one per call at all), so now() read at each block wobbles by up to a
    millisecond or more - and that wobble went straight into the MIDI timecode sent, and into where the LTC made from MIDI timecode was first
    set. The audio itself is the steadier clock: each block is expected one block's length after the last, and the time read only pulls that
    gently (settling in about a second, so a PC clock and an audio clock that run 0.01% apart sit 0.1 ms apart here). A block that comes far from
    where it was expected (the device stopped, a dropout) starts it again from the time read. Audio thread; reset() when the device starts. */
class BlockClock
{
public:
    void reset() noexcept { have = false; }
    double tick (double raw, int numSamples, double sampleRate) noexcept
    {
        const double len = sampleRate > 0.0 ? (double) numSamples / sampleRate : 0.0;
        const double expected = t + len;
        if (! have || std::abs (raw - expected) > 0.02 + 0.5 * len) { t = raw; have = true; return t; }
        t = expected + (len / (len + 1.0)) * (raw - expected);
        return t;
    }
private:
    double t = 0.0; bool have = false;
};
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
            have = false; lastPiece = -1; mask = 0; runAt = -1.0; otherWay = 0;
            const int code = mtc::formatCodeOf (rateCode, pulled && rateCode == rate);
            publish (false, framesToSeconds ((double) frameIndexOf (h, m, s, f, nominal, rateCode == 2), code), arrived, code);
        }
    }
    void feed (const juce::MidiMessage& msg, double arrived) noexcept { feed (msg.getRawData(), msg.getRawDataSize(), arrived); }

    /** The latest reference, for the audio thread: never waits. False when it could not be read this time - the MIDI thread was in the middle of
        writing it (and may have been put aside there by the system for a while): 'r' is left as it was, and the caller goes on with the one it
        read before. (3.67.0: until then a missed read came back as "no timecode", which stopped the LTC for that block - seldom, but in a take.) */
    bool read (Ref& r) const noexcept
    {
        for (int tries = 0; tries < 8; ++tries)
        {
            const auto s0 = seq.load (std::memory_order_acquire);
            if (s0 & 1u) continue;
            Ref x; x.valid = rValid.load (std::memory_order_relaxed); x.running = rRunning.load (std::memory_order_relaxed);
            x.tc = rTc.load (std::memory_order_relaxed); x.at = rAt.load (std::memory_order_relaxed); x.formatCode = rCode.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_relaxed) == s0) { r = x; return true; }
        }
        return false;
    }
    /** The same for the message thread and the tests: tries until it has it (the writer is only ever a few instructions from done). */
    Ref ref() const noexcept
    {
        Ref r;
        for (int tries = 0; tries < 4000; ++tries) { if (read (r)) return r; std::this_thread::yield(); }
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
        if (! inOrder) { have = false; runAt = -1.0; otherWay = 0; mask = 0; if (piece != 0) return; }      // a gap, or running backwards: wait for a whole sequence
        if (piece == 0) { mask = 0; qf = 0; if (have) baseFrames += 2; }                       // (a sequence of eight spans two frames)
        else ++qf;
        nib[piece] = value; mask |= 1 << piece;
        if (piece == 7 && mask == 0xFF)
        {
            const int f = nib[0] | ((nib[1] & 1) << 4), s = nib[2] | ((nib[3] & 3) << 4), m = nib[4] | ((nib[5] & 3) << 4), h = nib[6] | ((nib[7] & 1) << 4);
            const int rc = (nib[7] >> 1) & 3, nominal = rc == 0 ? 24 : rc == 1 ? 25 : 30;
            if (h > 23 || m > 59 || s > 59 || f >= nominal) { have = false; runAt = -1.0; otherWay = 0; return; }
            const long long frames = frameIndexOf (h, m, s, f, nominal, rc == 2);
            if (! have || frames != baseFrames || rc != rate) { runAt = -1.0; otherWay = 0; }   // (a jump: the rate is measured again from here)
            if (rc != rate) pulled = false;
            rate = rc; baseFrames = frames; have = true; qf = 7;
        }
        if (! have) return;
        // 23.976 and 29.97 non-drop send the codes of 24 and 30: told apart by how fast the frames really come (0.1% slower), over 6 s or more
        const long long q = baseFrames * 4 + qf;
        if (runAt < 0.0) { runAt = arrived; runQ = q; }
        else if (arrived - runAt > 6.0 && (rate == 0 || rate == 3))
        {
            // (3.67.0: it changes its mind only when eight messages in a row - two frames - say so, each clearly: past three quarters of the way to
            // the other speed. One message that came a few milliseconds late just after the six seconds used to flip it for a moment, and each
            // flip set the LTC again.)
            const double nominal = rate == 0 ? 24.0 : 30.0, measured = (double) (q - runQ) * 0.25 / (arrived - runAt);
            const bool saysPulled = measured < nominal * (1.0 - 0.00075), saysNot = measured > nominal * (1.0 - 0.00025);
            if ((saysPulled && ! pulled) || (saysNot && pulled)) { if (++otherWay >= 8) { pulled = saysPulled; otherWay = 0; } }
            else otherWay = 0;
        }
        const int code = mtc::formatCodeOf (rate, pulled);
        publish (true, mtc::wrapDay (framesToSeconds ((double) baseFrames + qf * 0.25, code)), arrived, code);
    }

    void publish (bool running, double tc, double at, int code) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);          // (3.67.0: so that no reader can see new values and still the old count - by the language's rules, and on ARM)
        rValid.store (true, std::memory_order_relaxed); rRunning.store (running, std::memory_order_relaxed);
        rTc.store (tc, std::memory_order_relaxed); rAt.store (at, std::memory_order_relaxed); rCode.store (code, std::memory_order_relaxed);
        seq.fetch_add (1, std::memory_order_release);
    }

    // MIDI thread
    int nib[8] = {}; int mask = 0, lastPiece = -1, qf = 0, rate = 1; bool have = false, pulled = false; int otherWay = 0;
    long long baseFrames = 0, runQ = 0; double runAt = -1.0;
    // published
    std::atomic<unsigned> seq { 0 };
    std::atomic<bool> rValid { false }, rRunning { false }; std::atomic<double> rTc { 0.0 }, rAt { 0.0 }; std::atomic<int> rCode { 1 };
};

/** LTC (audio) made to follow a MtcReader: what the LTC track records when the timecode comes in as MIDI. Audio thread.

    3.67.0: it stays on the timecode by a trim of its speed (LtcGenerator::setSpeed), not by standing where it was first set until it is a whole
    frame out. The first setting is one message's arrival, a millisecond or so either way; and a timecode source that does not run from the
    audio's clock (a camera's, a recorder's) drifts against it for as long as the take lasts. Both are now followed smoothly - the code has no
    break in it, as LTC on an audio input has none - at up to 0.02% (kMaxTrim), far inside what tells 24 from 23.976. It is set again (a break
    in the code) only when the timecode really jumped: half a second at once, or 5 ms on the average of the last half second. */
class MtcChase
{
public:
    void prepare (double sampleRate) { rate = sampleRate; gen.prepare (sampleRate); errAvg = 0.0; last = {}; startPos = 0.0; sets = 0; }
    /** Fills 'out' with LTC for a block whose first sample was heard at 'blockTime' (mtc::now()'s clock), or with silence when no timecode is
        arriving (as a stopped LTC source would leave the track). 'offset' (seconds) is added to the timecode: how late the MIDI arrives. */
    void fill (const MtcReader& reader, double blockTime, float* out, int count, double offset = 0.0) noexcept
    {
        reader.read (last);                                            // (not read this time: the one before stands - it is good for a quarter of a second)
        const auto& r = last;
        if (! r.valid || ! r.running || blockTime - r.at > 0.25 || blockTime - r.at < -1.0)
        {
            gen.stop(); errAvg = 0.0;
            juce::FloatVectorOperations::clear (out, count);
            return;
        }
        const double want = mtc::wrapDay (r.tc + (blockTime - r.at) + offset);
        if (! gen.isRunning() || gen.formatCode() != r.formatCode) { gen.startExact (r.formatCode, want); errAvg = 0.0; ++sets; }
        else
        {
            const double err = mtc::daySpan (want, gen.positionSeconds());
            const double len = rate > 0.0 ? (double) count / rate : 0.0;
            // the average of the last half second or so; one block counts for 20 ms at most, so a single message that came late cannot look like a jump
            errAvg += (len / (len + kAverage)) * (juce::jlimit (-0.02, 0.02, err) - errAvg);
            if (std::abs (err) > 0.5 || std::abs (errAvg) > kJump) { gen.startExact (r.formatCode, want); errAvg = 0.0; ++sets; }      // a jump in the timecode: follow it
            else gen.setSpeed (1.0 + juce::jlimit (-kMaxTrim, kMaxTrim, errAvg / kPull));
        }
        startPos = gen.positionSeconds();
        gen.fill (out, count);
    }
    bool isRunning() const noexcept { return gen.isRunning(); }
    /** Where the LTC was at the first sample of the block last filled (time of day in seconds), and its format: for LtcCompare. */
    double positionAtBlockStart() const noexcept { return startPos; }
    int formatCode() const noexcept { return gen.formatCode(); }
    /** How many times the LTC was set to the timecode (started, or set again after a jump) since prepare(): each is a break in the code. */
    int timesSet() const noexcept { return sets; }

    static constexpr double kAverage = 0.5;        // seconds the error is averaged over
    static constexpr double kPull = 8.0;           // the trim takes the averaged error out over about this long (16 x kAverage: it cannot swing past)
    static constexpr double kMaxTrim = 2.0e-4;     // the most the speed is trimmed by
    static constexpr double kJump = 0.005;         // an averaged error over this is a jump in the timecode, not drift

private:
    LtcGenerator gen;
    MtcReader::Ref last;
    double rate = 48000.0, errAvg = 0.0, startPos = 0.0; int sets = 0;
};

/** 3.67.0: how far the LTC on an audio input is from where a timecode generator is - for measuring the offset of MIDI timecode. The same timecode
    is fed to the MIDI route and, as LTC, to an audio input; the audio thread reads the LTC on that input and compares each frame's own time with
    where the generator (the LTC made from MIDI timecode) was at the sample the frame began. The average of that is the offset to set.

    The audio thread calls block() or idle() every block; the message thread arms it and reads the result. prepare() not while audio runs. */
class LtcCompare
{
public:
    struct Result
    {
        int    frames = 0;        // frames compared so far (each followed the one before: a misread frame is not counted)
        double mean = 0.0;        // seconds: the LTC on the input minus the generator (above 0: the generator is behind)
        double spread = 0.0;      // seconds: how much that varied from frame to frame (the standard deviation)
    };
    void prepare (double sampleRate)
    {
        rate = sampleRate; dec = std::make_unique<LtcDecoder> (sampleRate); dec->setStoreFrames (false);
        fed = 0.0; seenFrames = 0; seenArm = -1; prevN = -1; n = 0; sum = sumSq = 0.0;
    }
    /** Message thread: start again from nothing on this input, or stop (-1). */
    void arm (int inputChannel) noexcept
    {
        channel.store (-1, std::memory_order_relaxed);
        outN.store (0, std::memory_order_relaxed); outMean.store (0.0, std::memory_order_relaxed); outSpread.store (0.0, std::memory_order_relaxed);
        armCount.fetch_add (1, std::memory_order_release);
        channel.store (inputChannel, std::memory_order_release);
    }
    int input() const noexcept { return channel.load (std::memory_order_acquire); }
    Result result() const noexcept
    {
        Result r; r.frames = outN.load (std::memory_order_relaxed); r.mean = outMean.load (std::memory_order_relaxed); r.spread = outSpread.load (std::memory_order_relaxed);
        return r;
    }

    /** Audio thread: the chosen input's samples for this block, whether the generator is running, where it was at the block's first sample (time
        of day in seconds) and its format. */
    void block (const float* in, int count, bool genRunning, double genPosAtBlockStart, int formatCode) noexcept
    {
        noticeArm();
        if (dec == nullptr || in == nullptr || rate <= 0.0) { prevN = -1; return; }
        const double blockStart = fed;                                 // (the decoder counts the samples it was given: 'fed' is that count)
        dec->feed (in, count); fed += (double) count;
        if (dec->frameTotal() == seenFrames) return;
        seenFrames = dec->frameTotal();
        const auto& f = dec->lastFrame();
        double fps; int nominal; bool drop; LtcGenerator::formatInfo (formatCode, fps, nominal, drop);
        const long long fn = frameIndexOf (f.h, f.m, f.s, f.f, nominal, drop);
        // only a frame that follows the one read before it, a frame's length after it, is believed
        const bool follows = prevN >= 0 && fn > prevN && fn - prevN <= 8 && std::abs ((f.pos - prevPos) / rate - (double) (fn - prevN) / fps) < 0.25 / fps;
        prevN = fn; prevPos = f.pos;
        if (! follows || ! genRunning) return;
        const double d = mtc::daySpan ((double) fn / fps, genPosAtBlockStart + (f.pos - blockStart) / rate);     // (the frame began before this block: the generator's place then)
        if (std::abs (d) > 2.0) return;                                                                          // (not the same timecode at all)
        ++n; sum += d; sumSq += d * d;
        const double mean = sum / n;
        outMean.store (mean, std::memory_order_relaxed); outSpread.store (std::sqrt (std::max (0.0, sumSq / n - mean * mean)), std::memory_order_relaxed);
        outN.store (n, std::memory_order_relaxed);
    }
    /** Audio thread: a block with nothing to compare (not armed, or the input is not there). */
    void idle() noexcept { noticeArm(); prevN = -1; }

private:
    // armed afresh: start from nothing, and say so (what an earlier measurement left must not be read as this one's)
    void noticeArm() noexcept
    {
        const int a = armCount.load (std::memory_order_acquire);
        if (a == seenArm) return;
        seenArm = a; n = 0; sum = sumSq = 0.0; prevN = -1;
        outN.store (0, std::memory_order_relaxed); outMean.store (0.0, std::memory_order_relaxed); outSpread.store (0.0, std::memory_order_relaxed);
    }
    std::unique_ptr<LtcDecoder> dec;
    double rate = 0.0, fed = 0.0, prevPos = 0.0, sum = 0.0, sumSq = 0.0;
    long long seenFrames = 0, prevN = -1; int seenArm = -1, n = 0;
    std::atomic<int> channel { -1 }, armCount { 0 }, outN { 0 };
    std::atomic<double> outMean { 0.0 }, outSpread { 0.0 };
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
                if (port != nullptr) { port->start(); openName = name; timesMissing = 0; openFlag.store (true, std::memory_order_release); return true; }
            }
        return false;
    }
    void close() { openFlag.store (false, std::memory_order_release); if (port != nullptr) { port->stop(); port.reset(); } openName = {}; }
    /** Any thread (3.67.0: the audio thread asks every block, and used to read the pointer the message thread was changing). */
    bool isOpen() const noexcept { return openFlag.load (std::memory_order_acquire); }
    /** Message thread, every few seconds: a port that is open here but no longer in the system's list (its rtpMIDI session was taken away) is
        closed, so that it is opened afresh when it is back - until 3.67.0 it stayed "open" and silent, and choosing it again did nothing.
        Never while timecode is still arriving on it, and only when it was missing from the list twice running: a port must not be closed
        under a take because the system's list was wrong for a moment. */
    void closeIfGone()
    {
        if (port == nullptr) return;
        if (mtcReader.isRunning (mtc::now())) { timesMissing = 0; return; }
        for (auto& d : juce::MidiInput::getAvailableDevices()) if (d.name == openName) { timesMissing = 0; return; }
        if (++timesMissing >= 2) { timesMissing = 0; close(); }
    }
    const juce::String& name() const noexcept { return openName; }
    const MtcReader& reader() const noexcept { return mtcReader; }

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override { mtcReader.feed (m, mtc::now()); }
    std::unique_ptr<juce::MidiInput> port;
    juce::String openName;
    std::atomic<bool> openFlag { false };
    int timesMissing = 0;
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
                if (port != nullptr) { openName = name; sending = false; timesMissing = 0; openFlag.store (true, std::memory_order_release); startTimer (1); return true; }
            }
        return false;
    }
    void close() { openFlag.store (false, std::memory_order_release); stopTimer(); port.reset(); openName = {}; }
    bool isOpen() const noexcept { return openFlag.load (std::memory_order_acquire); }      // (any thread: see MtcInput::isOpen)
    void closeIfGone()                                                                       // (see MtcInput::closeIfGone: missing from the list twice running)
    {
        if (port == nullptr) return;
        for (auto& d : juce::MidiOutput::getAvailableDevices()) if (d.name == openName) { timesMissing = 0; return; }
        if (++timesMissing >= 2) { timesMissing = 0; close(); }
    }
    const juce::String& name() const noexcept { return openName; }

    /** Audio thread, once a block: the generator is running (or not), and is at 'positionSeconds' at the moment 'at' (mtc::now()'s clock; 3.67.0:
        the moment the block's first sample is HEARD - a little in the future - so the MIDI timecode says where the LTC at the output is). */
    void stamp (bool running, double positionSeconds, double at, int formatCode) noexcept
    {
        seq.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);          // (as MtcReader::publish)
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
        // (3.67.0: not read this time - the audio thread was in the middle of stamping it: the next call, a millisecond on, reads it. It used to count
        // as "stopped", and the timecode started again with a locate.)
        if (! got) return;
        const double t = mtc::now();
        if (! running || port == nullptr || t - at > 0.5) { sending = false; return; }
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
    std::atomic<bool> openFlag { false };
    int timesMissing = 0;
    // the timer's own
    bool sending = false; int sentCode = -1; long long lastQ = 0, seqBytesFor = -1; juce::uint8 seqBytes[8] = {};
    // stamped by the audio thread
    std::atomic<unsigned> seq { 0 };
    std::atomic<bool> sRunning { false }; std::atomic<double> sPos { 0.0 }, sAt { 0.0 }; std::atomic<int> sCode { 1 };
};
} // namespace td
