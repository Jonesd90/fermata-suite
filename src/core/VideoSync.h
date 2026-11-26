#pragma once
#include "Project.h"
#include "Ltc.h"
#include "VideoExport.h"
#include "AudioOps.h"
#include "Dsp.h"
#include <atomic>
#include <functional>

namespace td::videosync
{
/** 3.68.0: lining a video clip up with an edit (EditVideo, src/core/Edit.h) - asked for on 25 November 2026: "First, the program should see if there
    is a timecode sync available and if so, use it. If not, it will do it by sound. If it has no sound then just place it at 00:00:00."

    1. TIMECODE. The video's own: a timecode track in the MP4 / MOV file (what most cameras that take timecode write), or a Sony camera's XML
       file beside it, or LTC on one of the camera's sound channels (a Tentacle box into the camera's second input). It is compared with the
       timecode recorded with the takes the edit's pieces came from (each take's file on the LTC track): the clip goes where its timecode meets
       the piece it overlaps most.
    2. SOUND. The camera's own sound against the edit's: their onsets (where the sound gets suddenly louder) are compared, first over the whole
       length a hundred times a second, then to the millisecond around the best match. The camera hears the hall from where it stands, so its
       sound may arrive a few milliseconds after the stage mics' (about 3 ms a metre further away) - the clip can be nudged by hand afterwards.
    3. Neither: 0:00:00 (or, for a file added together with others, straight after the one before it - a camera's split files run on).

    No video decoding here: the camera's sound has already been read out of the video into a WAV file (src/ui/VideoSource.h), and the timecode
    track is read straight from the file's own structure, the same on every computer. Header only: fermata's video lane and the tests use it. */

/** The timecode of a video file's first frame. */
struct Timecode
{
    bool found = false;
    double seconds = 0.0;          // time of day, as LtcResult's: the frame's number since midnight / the frame rate (29.97 DF: real seconds)
    double fps = 0.0;
    bool drop = false;
    juce::String from;             // where it was read ("the file's timecode track", "the XML file C0001M01.XML", "LTC on the camera's sound, channel 2")
};

// ============================================================================================================== the file's own timecode track
namespace detail
{
inline juce::uint32 be32 (const juce::uint8* p) noexcept { return ((juce::uint32) p[0] << 24) | ((juce::uint32) p[1] << 16) | ((juce::uint32) p[2] << 8) | (juce::uint32) p[3]; }
inline juce::uint64 be64 (const juce::uint8* p) noexcept { return ((juce::uint64) be32 (p) << 32) | (juce::uint64) be32 (p + 4); }

/** Reads n bytes at 'pos' (false: not all there). */
inline bool readAt (juce::InputStream& in, juce::int64 pos, void* dest, int n)
{
    return in.setPosition (pos) && in.read (dest, n) == n;
}
/** One box of an ISO media file (MP4 / MOV): where its contents start and end, and its type. */
struct Box { juce::int64 start = 0, body = 0, end = 0; char type[5] = {}; };
/** The boxes inside [from, to). Damaged sizes end the list. At most 4096 (a file is not trusted). */
inline std::vector<Box> boxesIn (juce::InputStream& in, juce::int64 from, juce::int64 to)
{
    std::vector<Box> out;
    juce::int64 pos = from;
    while (pos + 8 <= to && out.size() < 4096)
    {
        juce::uint8 h[16];
        if (! readAt (in, pos, h, 8)) break;
        juce::uint64 size = be32 (h); int head = 8;
        if (size == 1) { if (! readAt (in, pos + 8, h + 8, 8)) break; size = be64 (h + 8); head = 16; }
        else if (size == 0) size = (juce::uint64) (to - pos);
        if (size < (juce::uint64) head || (juce::int64) size > to - pos) break;
        Box b; b.start = pos; b.body = pos + head; b.end = pos + (juce::int64) size;
        for (int i = 0; i < 4; ++i) b.type[i] = (char) h[4 + i];
        out.push_back (b);
        pos = b.end;
    }
    return out;
}
inline const Box* find (const std::vector<Box>& v, const char* type) { for (auto& b : v) if (std::memcmp (b.type, type, 4) == 0) return &b; return nullptr; }
/** a - b in seconds of the day, the short way round midnight. */
inline double daySpan (double a, double b) noexcept { double d = std::fmod (a - b, 86400.0); if (d > 43200.0) d -= 86400.0; if (d < -43200.0) d += 86400.0; return d; }
} // namespace detail

/** The timecode track of an MP4 / MOV file ('tmcd': its first sample is the frame number of the first frame, counted from midnight). */
inline Timecode containerTimecode (const juce::File& file)
{
    using namespace detail;
    Timecode tc;
    juce::FileInputStream in (file);
    if (in.failedToOpen()) return tc;
    const auto top = boxesIn (in, 0, in.getTotalLength());
    const Box* moov = find (top, "moov");
    if (moov == nullptr) return tc;
    for (auto& trak : boxesIn (in, moov->body, moov->end))
    {
        if (std::memcmp (trak.type, "trak", 4) != 0) continue;
        const auto tk = boxesIn (in, trak.body, trak.end);
        const Box* mdia = find (tk, "mdia");
        if (mdia == nullptr) continue;
        const auto md = boxesIn (in, mdia->body, mdia->end);
        const Box* hdlr = find (md, "hdlr");
        juce::uint8 hb[12];
        if (hdlr == nullptr || hdlr->end - hdlr->body < 12 || ! readAt (in, hdlr->body, hb, 12) || std::memcmp (hb + 8, "tmcd", 4) != 0) continue;
        const Box* minf = find (md, "minf");
        if (minf == nullptr) continue;
        const auto mi = boxesIn (in, minf->body, minf->end);                  // (kept: find points into it)
        const Box* stbl = find (mi, "stbl");
        if (stbl == nullptr) continue;
        const auto st = boxesIn (in, stbl->body, stbl->end);
        const Box* stsd = find (st, "stsd");
        // stsd: version / flags (4), entry count (4); the entry: size (4), 'tmcd' (4), reserved (6), data reference (2), reserved (4), flags (4),
        // time scale (4), frame duration (4), frames a second (1), reserved (1)
        juce::uint8 sd[8 + 34];
        if (stsd == nullptr || stsd->end - stsd->body < (juce::int64) sizeof (sd) || ! readAt (in, stsd->body, sd, (int) sizeof (sd))) continue;
        if (std::memcmp (sd + 12, "tmcd", 4) != 0) continue;
        const juce::uint32 flags = be32 (sd + 8 + 20), scale = be32 (sd + 8 + 24), dur = be32 (sd + 8 + 28);
        const int nominal = sd[8 + 32];
        if (scale == 0 || dur == 0 || nominal <= 0) continue;
        // where its first sample is: the first chunk's offset (stco 32-bit, co64 64-bit)
        juce::int64 at = -1;
        if (const Box* stco = find (st, "stco"))
        {
            juce::uint8 c[12];
            if (readAt (in, stco->body, c, 12) && be32 (c + 4) > 0) at = (juce::int64) be32 (c + 8);
        }
        else if (const Box* co64 = find (st, "co64"))
        {
            juce::uint8 c[16];
            if (readAt (in, co64->body, c, 16) && be32 (c + 4) > 0) at = (juce::int64) be64 (c + 8);
        }
        juce::uint8 f[4];
        if (at < 0 || ! readAt (in, at, f, 4)) continue;
        const juce::uint32 frame = be32 (f);
        tc.fps = (double) scale / (double) dur;
        tc.drop = (flags & 1u) != 0;
        if (tc.fps < 1.0 || tc.fps > 240.0) continue;
        tc.seconds = std::fmod ((double) frame / tc.fps, 86400.0);
        tc.found = true;
        tc.from = "the file's timecode track";
        juce::ignoreUnused (nominal);
        return tc;
    }
    return tc;
}

// ============================================================================================================== a Sony camera's XML file
/** The XML file a Sony camera writes beside each clip (C0001.MP4 -> C0001M01.XML): <LtcChangeTable tcFps="25"> with <LtcChange frameCount="0"
    value="ffssmmhh"> - the timecode of the first frame, as four BCD bytes frames first. The frame rate: <VideoFrame formatFps="29.97p">.
    (Written from the layout these files are known to have; not yet tried on a file from a real Sony camera.) */
inline Timecode sidecarTimecode (const juce::File& video)
{
    Timecode tc;
    const auto dir = video.getParentDirectory();
    const auto base = video.getFileNameWithoutExtension();
    juce::File xml;
    for (auto name : { base + "M01.XML", base + "M01.xml", base + ".XML", base + ".xml" })
        if (dir.getChildFile (name).existsAsFile()) { xml = dir.getChildFile (name); break; }
    if (xml == juce::File() || xml.getSize() > 4 * 1024 * 1024) return tc;
    auto doc = juce::XmlDocument::parse (xml);
    if (doc == nullptr) return tc;
    const juce::XmlElement* table = nullptr; const juce::XmlElement* frame = nullptr;
    std::function<void (const juce::XmlElement&)> walk = [&] (const juce::XmlElement& e)
    {
        if (e.getTagName() == "LtcChangeTable" && table == nullptr) table = &e;
        if (e.getTagName() == "VideoFrame" && frame == nullptr) frame = &e;
        for (auto* c : e.getChildIterator()) walk (*c);
    };
    walk (*doc);
    if (table == nullptr) return tc;
    const juce::XmlElement* first = nullptr;
    for (auto* c : table->getChildIterator())
        if (c->getTagName() == "LtcChange" && (first == nullptr || c->getIntAttribute ("frameCount") < first->getIntAttribute ("frameCount"))) first = c;
    if (first == nullptr || first->getIntAttribute ("frameCount") != 0) return tc;
    const auto v = first->getStringAttribute ("value").trim();
    if (v.length() != 8 || ! v.containsOnly ("0123456789abcdefABCDEF")) return tc;
    auto byte = [&v] (int i) { return v.substring (i * 2, i * 2 + 2).getHexValue32(); };
    auto bcd = [] (int b) { return (b >> 4) * 10 + (b & 15); };
    const int fb = byte (0), sb = byte (1), mb = byte (2), hb = byte (3);
    const bool dropBit = (fb & 0x40) != 0;
    const int f = bcd (fb & 0x3f), s = bcd (sb & 0x7f), m = bcd (mb & 0x7f), h = bcd (hb & 0x3f);
    int nominal = juce::jlimit (1, 120, table->getIntAttribute ("tcFps", 25));
    if (h > 23 || m > 59 || s > 59 || f >= nominal) return tc;
    // the real rate: 23.98 / 29.97 / 59.94 when the clip's format says so
    const auto fmt = frame != nullptr ? frame->getStringAttribute ("formatFps") + " " + frame->getStringAttribute ("captureFps") : juce::String();
    const bool pulled = fmt.contains ("23.98") || fmt.contains ("23.97") || fmt.contains ("29.97") || fmt.contains ("59.94");
    tc.fps = pulled ? nominal * 1000.0 / 1001.0 : (double) nominal;
    tc.drop = dropBit && (nominal == 30 || nominal == 60);
    const long long n = frameIndexOf (h, m, s, f, nominal, tc.drop && nominal == 30);
    tc.seconds = (double) n / tc.fps;
    tc.found = true;
    tc.from = "the XML file " + xml.getFileName();
    return tc;
}

// ============================================================================================================== LTC on the camera's sound
/** LTC on one channel of the camera's sound (read out of the video into a WAV): the timecode of its first sample, and which channel (-1: none).
    Only the first 30 seconds are looked at. */
inline Timecode soundTimecode (juce::AudioFormatManager& fm, const juce::File& wav, int& ltcChannel)
{
    Timecode tc; ltcChannel = -1;
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (wav));
    if (r == nullptr) return tc;
    const int nch = (int) r->numChannels;
    r.reset();
    for (int ch = 0; ch < nch && ch < 8; ++ch)
    {
        const auto res = scanFileForLtc (fm, wav, ch, 30.0);
        if (! res.found || res.frameCount < 25) continue;
        tc.found = true; tc.seconds = res.startSeconds; tc.fps = res.fps; tc.drop = res.drop; ltcChannel = ch;
        tc.from = "LTC on the camera's sound, channel " + juce::String (ch + 1);
        return tc;
    }
    return tc;
}

// ============================================================================================================== the edit's timecode
/** One piece of the edit and the timecode of its first sample (from its take's file on the LTC track). */
struct PieceCode
{
    double start = 0.0, length = 0.0;          // seconds on the edit's timeline (srcIn's place, and how long it plays)
    juce::String name;
    juce::File ltcFile; juce::int64 ltcSample = 0;
    bool found = false; double tc = 0.0;       // filled in by readPieceCodes
};
/** Message thread: the pieces (not the overdubs) and where their timecode is. Empty when the project has no LTC track. */
inline std::vector<PieceCode> pieceCodesOf (const Project& p, const EditDef& e)
{
    std::vector<PieceCode> out;
    std::vector<juce::Uuid> ltc;
    for (auto& t : p.tracks) if (t.isLtc) ltc.push_back (t.id);
    if (ltc.empty() || e.sampleRate <= 0.0) return out;
    for (auto& r : e.regions)
    {
        PieceCode pc;
        pc.start = (double) r.startSample / e.sampleRate; pc.length = (double) r.length() / e.sampleRate; pc.name = r.takeName;
        for (auto& f : r.files)
            if (std::find (ltc.begin(), ltc.end(), f.trackId) != ltc.end()) { pc.ltcFile = f.file; pc.ltcSample = r.srcIn - f.fileStart; break; }
        if (pc.ltcFile != juce::File() && pc.length > 0.0) out.push_back (pc);
    }
    return out;
}
/** Any thread: decodes each piece's timecode (a few seconds of its LTC file each). */
inline void readPieceCodes (std::vector<PieceCode>& pieces, juce::AudioFormatManager& fm, const std::atomic<bool>* cancel = nullptr)
{
    for (auto& pc : pieces)
    {
        if (cancel != nullptr && cancel->load()) return;
        const auto c = videoexport::timecodeAt (fm, pc.ltcFile, 0, pc.ltcSample);
        pc.found = c.found; pc.tc = c.seconds;
    }
}
/** Where a clip goes by its timecode: the piece whose timecode overlaps the clip's most decides. */
struct Placed { bool ok = false; double start = 0.0; double overlap = 0.0; juce::String piece; };
inline Placed placeByTimecode (const Timecode& tc, double clipLength, const std::vector<PieceCode>& pieces)
{
    Placed best;
    if (! tc.found || clipLength <= 0.0) return best;
    for (auto& pc : pieces)
    {
        if (! pc.found) continue;
        const double d = detail::daySpan (tc.seconds, pc.tc);                     // the clip's first frame, from the piece's first sample (in timecode)
        const double from = juce::jmax (0.0, d), to = juce::jmin (pc.length, d + clipLength);    // the overlap, in the piece's own time
        const double ov = to - from;
        if (ov > best.overlap + 1.0e-9) { best.ok = true; best.overlap = ov; best.start = pc.start + d; best.piece = pc.name; }
    }
    if (best.overlap < 0.5) best.ok = false;                                   // (half a second at least: not a chance touch)
    return best;
}

// ============================================================================================================== the sound: onsets
/** A sound's energy, a thousand times a second, high-passed at about 200 Hz first (a camera's microphone and the stage mics differ most in the
    bass - and a hall's rumble says nothing about where the music is). */
struct Energy
{
    std::vector<float> ms;                       // [n]: the energy of the millisecond from n ms on
    void addAt (juce::int64 msIndex, float e) { if (msIndex >= 0) { if ((size_t) msIndex >= ms.size()) ms.resize ((size_t) msIndex + 1, 0.0f); ms[(size_t) msIndex] += e; } }
};
namespace detail
{
/** Adds the high-passed energy of samples x[0..n) (at 'rate', the first at 'firstSecond' on the timeline) to 'en'. 'hp' carries the filter. */
struct HighPass { double a = 0.0, y = 0.0, xPrev = 0.0; void prepare (double rate) { const double rc = 1.0 / (2.0 * dsp::kPi * 200.0); a = rc / (rc + 1.0 / rate); y = xPrev = 0.0; } };
inline void addEnergy (Energy& en, const float* x, int n, double rate, double firstSecond, HighPass& hp)
{
    const double perMs = rate / 1000.0;
    juce::int64 bin = (juce::int64) std::floor (firstSecond * 1000.0);
    double binEnd = (std::floor (firstSecond * 1000.0) + 1.0 - firstSecond * 1000.0) * perMs;     // samples left in the first millisecond
    double acc = 0.0, inBin = 0.0;
    for (int i = 0; i < n; ++i)
    {
        hp.y = hp.a * (hp.y + (double) x[i] - hp.xPrev); hp.xPrev = (double) x[i];
        acc += hp.y * hp.y; inBin += 1.0;
        if (inBin >= binEnd) { en.addAt (bin, (float) acc); acc = 0.0; inBin = 0.0; ++bin; binEnd = perMs; }
    }
    if (inBin > 0.0) en.addAt (bin, (float) acc);
}
} // namespace detail

/** Any thread: the energy of a sound file (every channel but 'skipChannel' - LTC - summed in energy), from its first sample. */
inline bool energyOfFile (juce::AudioFormatManager& fm, const juce::File& f, int skipChannel, Energy& out, const std::function<bool (float)>& progress = {})
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->sampleRate <= 0.0) return false;
    const int nch = (int) r->numChannels;
    std::vector<detail::HighPass> hp ((size_t) nch);
    for (auto& h : hp) h.prepare (r->sampleRate);
    const juce::int64 len = r->lengthInSamples;
    std::vector<std::vector<float>> buf;
    constexpr juce::int64 chunk = 1 << 16;
    for (juce::int64 pos = 0; pos < len; pos += chunk)
    {
        if (progress && ! progress ((float) ((double) pos / (double) juce::jmax ((juce::int64) 1, len)))) return false;
        const auto n = juce::jmin (chunk, len - pos);
        if (! audioops::readRange (*r, pos, n, buf)) return false;
        for (int c = 0; c < nch && c < (int) buf.size(); ++c)
            if (c != skipChannel) detail::addEnergy (out, buf[(size_t) c].data(), (int) n, r->sampleRate, (double) pos / r->sampleRate, hp[(size_t) c]);
    }
    return true;
}

/** What the edit's own sound is made of, for the comparison: each piece's files on the chosen tracks - the tracks marked as main mics (Time
    alignment's Main) when there are any, else up to six of the project's tracks (not the LTC track). Message thread; read with energyOfEdit. */
struct EditSound
{
    struct Part { juce::File file; juce::int64 from = 0, count = 0; double at = 0.0; };   // file samples [from, from + count) play from 'at' seconds on
    std::vector<Part> parts;
    juce::StringArray tracks;                    // the tracks' names (for the log)
};
inline EditSound editSoundOf (const Project& p, const EditDef& e)
{
    EditSound out;
    std::vector<juce::Uuid> use;
    for (auto& t : p.tracks) if (! t.isLtc && t.alignRole == 1) { use.push_back (t.id); out.tracks.add (t.name); }
    if (use.empty())
        for (auto& t : p.tracks) if (! t.isLtc && use.size() < 6) { use.push_back (t.id); out.tracks.add (t.name); }
    if (e.sampleRate <= 0.0) return out;
    for (auto* list : { &e.regions, &e.overdubs })
        for (auto& r : *list)
            for (auto& f : r.files)
                if (std::find (use.begin(), use.end(), f.trackId) != use.end() && r.length() > 0)
                    out.parts.push_back ({ f.file, r.srcIn - f.fileStart, r.length(), (double) r.startSample / e.sampleRate });
    return out;
}
/** Any thread: the energy of the edit's sound along its timeline (from 0:00). */
inline bool energyOfEdit (juce::AudioFormatManager& fm, const EditSound& es, Energy& out, const std::function<bool (float)>& progress = {})
{
    std::vector<std::vector<float>> buf;
    for (size_t i = 0; i < es.parts.size(); ++i)
    {
        const auto& pt = es.parts[i];
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (pt.file));
        if (r == nullptr || r->sampleRate <= 0.0) continue;                    // (a file that is missing: the rest still counts)
        std::vector<detail::HighPass> hp ((size_t) r->numChannels);
        for (auto& h : hp) h.prepare (r->sampleRate);
        const juce::int64 from = juce::jmax ((juce::int64) 0, pt.from), to = juce::jmin (r->lengthInSamples, pt.from + pt.count);
        constexpr juce::int64 chunk = 1 << 16;
        for (juce::int64 pos = from; pos < to; pos += chunk)
        {
            if (progress && ! progress ((float) ((double) i + (double) (pos - from) / (double) juce::jmax ((juce::int64) 1, to - from)) / (float) es.parts.size())) return false;
            const auto n = juce::jmin (chunk, to - pos);
            if (! audioops::readRange (*r, pos, n, buf)) break;
            for (int c = 0; c < (int) buf.size() && c < (int) hp.size(); ++c)
                detail::addEnergy (out, buf[(size_t) c].data(), (int) n, r->sampleRate, pt.at + (double) (pos - pt.from) / r->sampleRate, hp[(size_t) c]);
        }
    }
    return ! out.ms.empty();
}

/** What the comparison found. */
struct SoundMatch
{
    bool ok = false;
    double start = 0.0;            // where the clip goes: seconds on the edit's timeline of the camera sound's first sample
    double strength = 0.0;         // how far the best match stands above the rest (in standard deviations)
    double margin = 0.0;           // the best match over the next best one more than half a second away (1 = as good)
    double overlap = 0.0;          // seconds the two sounds were compared over at the best match
    juce::String why;              // when not ok
};

namespace detail
{
/** Onsets: where the energy rises, from 'ms' grouped by 'step' milliseconds, each rise compared with 'back' groups before. Made zero-mean, unit-sd. */
inline std::vector<double> onsets (const std::vector<float>& ms, int step, int back)
{
    const size_t n = ms.size() / (size_t) step;
    std::vector<double> lg (n), o (n, 0.0);
    double mean = 0.0;
    for (size_t k = 0; k < n; ++k) { double s = 0.0; for (int j = 0; j < step; ++j) s += ms[k * (size_t) step + (size_t) j]; lg[k] = s; mean += s; }
    mean = n > 0 ? mean / (double) n : 0.0;
    const double floorE = juce::jmax (1.0e-20, mean * 1.0e-4);                 // (quieter than 40 dB under the average: silence, whatever its noise)
    for (auto& v : lg) v = std::log (v + floorE);
    for (size_t k = (size_t) back; k < n; ++k) o[k] = juce::jmax (0.0, lg[k] - lg[k - (size_t) back]);
    double m = 0.0, sq = 0.0;
    for (double v : o) { m += v; sq += v * v; }
    if (n > 0) { m /= (double) n; const double sd = std::sqrt (juce::jmax (1.0e-30, sq / (double) n - m * m)); for (auto& v : o) v = (v - m) / sd; }
    return o;
}
} // namespace detail

/** Any thread: where the camera's sound ('cam', from its first sample) best matches the edit's ('edit', from 0:00). The two must overlap by at
    least 20 seconds (or all of the shorter one). */
inline SoundMatch matchBySound (const Energy& cam, const Energy& edit)
{
    SoundMatch res;
    if (cam.ms.size() < 3000 || edit.ms.size() < 3000) { res.why = "the camera's sound or the edit is too short to compare (under 3 seconds)"; return res; }
    // coarse: onsets a hundred times a second, every placement at once (by FFT)
    const auto oc = detail::onsets (cam.ms, 10, 1), oe = detail::onsets (edit.ms, 10, 1);
    const int nc = (int) oc.size(), ne = (int) oe.size();
    int N = 1; while (N < nc + ne) N <<= 1;
    dsp::FastFFT fft (N);
    std::vector<std::complex<double>> A ((size_t) N), B ((size_t) N);
    for (int i = 0; i < ne; ++i) A[(size_t) i] = oe[(size_t) i];
    for (int i = 0; i < nc; ++i) B[(size_t) i] = oc[(size_t) i];
    fft.forward (A); fft.forward (B);
    for (int i = 0; i < N; ++i) A[(size_t) i] *= std::conj (B[(size_t) i]);
    fft.inverse (A);                                                           // A[lag] = sum over k of oe[k + lag] * oc[k] (lag below 0 at N + lag)
    const int minOv = juce::jmin (2000, juce::jmin (nc, ne));                  // (20 s, or the shorter of the two)
    std::vector<double> score; std::vector<int> lags;
    score.reserve ((size_t) (nc + ne)); lags.reserve ((size_t) (nc + ne));
    for (int lag = -(nc - 1); lag <= ne - 1; ++lag)
    {
        const int ov = juce::jmin (nc, ne - lag) - juce::jmax (0, -lag);
        if (ov < minOv) continue;
        const double c = A[(size_t) (lag >= 0 ? lag : N + lag)].real();
        score.push_back (c / std::sqrt ((double) ov)); lags.push_back (lag);
    }
    if (score.size() < 10) { res.why = "the camera's sound and the edit do not overlap enough to compare"; return res; }
    size_t bi = 0; for (size_t i = 1; i < score.size(); ++i) if (score[i] > score[bi]) bi = i;
    double second = -1.0e300;
    for (size_t i = 0; i < score.size(); ++i) if (std::abs (lags[i] - lags[bi]) > 50) second = juce::jmax (second, score[i]);
    double m = 0.0, sq = 0.0; for (double v : score) { m += v; sq += v * v; }
    m /= (double) score.size(); const double sd = std::sqrt (juce::jmax (1.0e-30, sq / (double) score.size() - m * m));
    res.strength = (score[bi] - m) / sd;
    res.margin = second > 0.0 ? score[bi] / second : 99.0;
    const int lag0 = lags[bi];
    res.overlap = 0.01 * (double) (juce::jmin (nc, ne - lag0) - juce::jmax (0, -lag0));
    // fine: onsets a thousand times a second, within 15 ms of the coarse match, then between the milliseconds
    const auto fc = detail::onsets (cam.ms, 1, 5), fe = detail::onsets (edit.ms, 1, 5);
    const int mc = (int) fc.size(), me = (int) fe.size();
    auto corrAt = [&] (int lag)
    {
        double s = 0.0;
        const int k0 = juce::jmax (0, -lag), k1 = juce::jmin (mc, me - lag);
        for (int k = k0; k < k1; ++k) s += fc[(size_t) k] * fe[(size_t) (k + lag)];
        return k1 > k0 ? s / std::sqrt ((double) (k1 - k0)) : -1.0e300;
    };
    int bestL = lag0 * 10; double bestV = -1.0e300;
    for (int l = lag0 * 10 - 15; l <= lag0 * 10 + 15; ++l) if (const double v = corrAt (l); v > bestV) { bestV = v; bestL = l; }
    const double vm = corrAt (bestL - 1), vp = corrAt (bestL + 1);
    const double den = vm - 2.0 * bestV + vp;
    const double frac = std::abs (den) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (vm - vp) / den) : 0.0;
    res.start = ((double) bestL + frac) / 1000.0;
    res.ok = res.strength >= 8.0 && res.margin >= 1.25;
    if (! res.ok) res.why = "the camera's sound did not match the edit clearly anywhere (best match " + juce::String (res.strength, 1) + " standard deviations above the rest, "
                            + juce::String (res.margin, 2) + " times the next best)";
    return res;
}
} // namespace td::videosync
