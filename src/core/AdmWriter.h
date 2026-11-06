#pragma once
#include "Surround.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace td
{
/** 3.40.0: fermata's own Dolby Atmos master files - ADM BWF (ITU-R BS.2076 metadata in a Broadcast Wave file) as the Dolby Atmos Master ADM
    Profile v1.1 and the Dolby Atmos Music Master Delivery Specification 2022.07 ask (the user's copies of both; docs/ATMOS-EXPORT.md has the rules
    that shaped this file). Each surround bus goes into the file as the Composer itself writes a Composite: a bed of the speaker channels the
    profile allows as a bed (2.0, 5.1, 7.1 or 7.1.2) plus one fixed object for every other speaker (the wides, the tops), sitting where that speaker
    is. The file's tracks: bed by bed, each bed's channels in the profile's order, then that bed's objects.

    The writer makes only the audio and the ADM metadata (axml + chna). Dolby's own metadata chunk (dbmd: the binaural render modes, the downmix
    settings, the trims) is not public; the user adds it in the Fiedler Mastering Console, which also sets the loudness and writes the delivery
    files. Everything here is pure (no project, no engine), so the tests can write a file and read it back.

    Checked against a master that Dolby's tools wrote and Apple Music accepted (6 November 2026): the chunk order (JUNK, fmt, data, axml, chna,
    dbmd), the 40-byte chna entries, the ID counters, the names, the element order, the object blocks (cartesian, X, Y, Z only when not 0, one
    block with rtime 0 and the programme's duration, jumpPosition 1 with interpolationLength 0.000000). */
namespace adm
{
    /** One channel of the file: the speaker it carries (from the bus's own channel order: Surround.h) and whether it is in the bed or an object. */
    struct Track
    {
        surround::Spk spk;
        int busChannel = 0;            // index in the bus's channels (fermata's order)
        bool inBed = true;
    };

    /** How a bus format goes into the file: the bed's format (the profile's bed sets: 2.0, 5.1, 7.1, 7.1.2 - the 7.1.2 bed's top pair is Lts /
        Rts, fermata's Ltm / Rtm) and the tracks, bed first then the objects. A bus that is already a bed format has no objects. */
    struct Layout
    {
        BusFormat bed = BusFormat::Stereo;
        std::vector<Track> tracks;
        int bedChannels() const noexcept { int n = 0; for (auto& t : tracks) if (t.inBed) ++n; return n; }
        int objects() const noexcept { return (int) tracks.size() - bedChannels(); }
    };
    Layout layoutFor (BusFormat busFormat);

    /** One bus in the file. 'name': the bed's audioObjectName and the objects' prefix ("Verb 9.1.6": the bed is "Verb 9.1.6", its objects
        "Verb 9.1.6 Lw" ...); the profile allows 1..64 characters, so it is trimmed (and "Atmos_Bed_M" used when empty). */
    struct Bed
    {
        juce::String name;
        BusFormat format = BusFormat::S916;
    };

    /** The ADM description of a file: the beds in the order their channels are interleaved, the programme's length and rate. */
    struct Description
    {
        std::vector<Bed> beds;
        juce::int64 frames = 0;
        double sampleRate = 48000.0;
        int bitDepth = 24;
        juce::String programmeName { "Atmos_Master" };
        int totalTracks() const { int n = 0; for (auto& b : beds) n += (int) layoutFor (b.format).tracks.size(); return n; }
    };

    /** The profile's limits: 128 channels, 64 beds, 118 objects, 123 elements; 48 / 96 kHz; 24 bit. "" when the description fits. */
    juce::String check (const Description&);

    /** The axml chunk's text (ebuCore_2016; UTF-8 when written). */
    juce::String buildAxml (const Description&);
    /** The chna chunk's bytes (numTracks, numUIDs, one 40-byte entry per track). */
    juce::MemoryBlock buildChna (const Description&);
    /** A time as the ADM writes it: "hh:mm:ss.fffff" (five decimals). */
    juce::String timeText (juce::int64 frames, double sampleRate);
    /** The room-centric position of a speaker as an object (the profile's Table 2-14 for the bed speakers; the wides half way to the sides). */
    void objectPosition (surround::Spk, float& x, float& y, float& z) noexcept;

    /** The LFE low-pass of the export: a 4th-order Butterworth (two biquads) between 100 and 150 Hz, as the delivery spec asks for the LFE. */
    struct LfeLowPass
    {
        struct Sec { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0; } s[2];
        void design (double fc, double sampleRate) noexcept;
        void process (float* x, int n) noexcept;
        double run (double v) noexcept { for (auto& c : s) { const double y = c.b0 * v + c.z1; c.z1 = c.b1 * v - c.a1 * y + c.z2; c.z2 = c.b2 * v - c.a2 * y; v = y; } return v; }
    };

    /** Writes the file: the header is written at once (RIFF, or RF64 once the audio will not fit 4 GB), the audio in calls to write(), the axml
        and chna after it, the sizes patched by finish(). 24-bit words made here with TPDF dither (Dither.h) from the floats given; a sample at or
        beyond full scale is clipped (the file says nothing: the Mastering Console measures the true peak). Not thread-bound; one writer per file. */
    class FileWriter
    {
    public:
        /** 'rf64At': the RIFF data size (bytes) from which the file is RF64 - 4 GB by the profile (the tests lower it). */
        FileWriter (const juce::File&, const Description&, bool dither = true, juce::int64 rf64At = (juce::int64) 0xFFFFFFFFll - 1024);
        ~FileWriter();
        bool ok() const { return error.isEmpty(); }
        const juce::String& getError() const { return error; }
        /** 'chans': totalTracks() channels of numSamples floats, in the file's track order. False on a write error (getError says). */
        bool write (const float* const* chans, int numSamples);
        /** Writes the metadata chunks and patches the sizes. False if the frames written differ from the description's, or on an error. */
        bool finish();
        juce::int64 framesWritten() const noexcept { return written; }
    private:
        bool put (const void* data, size_t bytes);
        bool putChunk (const char* id, const juce::MemoryBlock& payload);
        void fail (const juce::String& why) { if (error.isEmpty()) error = why; }
        juce::File file;
        Description desc;
        std::unique_ptr<juce::FileOutputStream> out;
        juce::String error;
        bool useDither, rf64 = false, finished = false;
        int channels = 0;
        juce::int64 written = 0, dataStart = 0, dataBytes = 0, riffSizeAt = 0, dataSizeAt = 0, ds64At = 0;
        std::vector<juce::uint32> seeds;
        std::vector<juce::uint8> bytes;
    };
}
} // namespace td
