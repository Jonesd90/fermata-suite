#include "AdmWriter.h"
#include "Dither.h"

namespace td
{
namespace adm
{
using surround::Spk;

// ----------------------------------------------------------------------------- the layouts
Layout layoutFor (BusFormat f)
{
    Layout L;
    const auto& lay = surround::layout (f);
    // the bed: the largest of the profile's bed sets the bus holds whole (7.1.2 needs the top middles; 7.1 the sides and rears; 5.1 Ls / Rs)
    auto hasAll = [&lay] (std::initializer_list<Spk> s) { for (auto k : s) if (! lay.has (k)) return false; return true; };
    if (hasAll ({ Spk::L, Spk::R, Spk::C, Spk::LFE, Spk::Lss, Spk::Rss, Spk::Lrs, Spk::Rrs, Spk::Ltm, Spk::Rtm })) L.bed = BusFormat::S712;
    else if (hasAll ({ Spk::L, Spk::R, Spk::C, Spk::LFE, Spk::Lss, Spk::Rss, Spk::Lrs, Spk::Rrs })) L.bed = BusFormat::S71;
    else if (hasAll ({ Spk::L, Spk::R, Spk::C, Spk::LFE, Spk::Ls, Spk::Rs })) L.bed = BusFormat::S51;
    else L.bed = BusFormat::Stereo;
    const auto& bed = surround::layout (L.bed);
    for (int i = 0; i < bed.count; ++i)                              // the bed's channels, in the profile's order (= fermata's order for these formats)
    {
        const auto s = bed.spk[i];
        if (s == Spk::HpL || s == Spk::HpR) continue;
        L.tracks.push_back ({ s, lay.indexOf (s), true });
    }
    for (int i = 0; i < lay.count; ++i)                              // the rest as objects, in the bus's order
    {
        const auto s = lay.spk[i];
        if (s == Spk::HpL || s == Spk::HpR || bed.has (s)) continue;
        L.tracks.push_back ({ s, i, false });
    }
    return L;
}

void objectPosition (Spk s, float& x, float& y, float& z) noexcept
{
    // Table 2-14 of the profile for the bed speakers (the same grid as surround::info); the wides (not in the profile's tables) half way between
    // the front and the side, as Dolby's own panners place them (surround::info gives y 0.5 there too)
    const auto& i = surround::info (s);
    x = i.x; y = i.y; z = i.z;
    if (s == Spk::LFE) { x = -1.0f; y = 1.0f; z = -1.0f; }
}

juce::String timeText (juce::int64 frames, double sampleRate)
{
    const double secs = sampleRate > 0.0 ? (double) frames / sampleRate : 0.0;
    const juce::int64 whole = (juce::int64) secs;
    const int h = (int) (whole / 3600), m = (int) ((whole / 60) % 60), s = (int) (whole % 60);
    const int frac = (int) std::llround ((secs - (double) whole) * 100000.0);
    // (a fraction that rounds up to a whole second is written as 99999: the five decimals are the profile's precision, never a carry)
    return juce::String (h).paddedLeft ('0', 2) + ":" + juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (s).paddedLeft ('0', 2) + "." + juce::String (juce::jmin (99999, frac)).paddedLeft ('0', 5);
}

juce::String check (const Description& d)
{
    if (d.beds.empty()) return "Nothing to write: no bus is ticked.";
    if (std::abs (d.sampleRate - 48000.0) > 0.5 && std::abs (d.sampleRate - 96000.0) > 0.5) return "A Dolby Atmos master is 48 or 96 kHz only (the project runs at " + juce::String (d.sampleRate / 1000.0, 1) + " kHz).";
    if (d.bitDepth != 24) return "A Dolby Atmos master is 24-bit.";
    if (d.beds.size() > 64) return "At most 64 beds.";
    int objects = 0;
    for (auto& b : d.beds) objects += layoutFor (b.format).objects();
    if (objects > 118) return "At most 118 objects (" + juce::String (objects) + " asked for).";
    if ((int) d.beds.size() + objects > 123) return "At most 123 beds and objects together.";
    if (d.totalTracks() > 128) return "At most 128 channels in one file (" + juce::String (d.totalTracks()) + " asked for: fewer buses).";
    if (d.frames <= 0) return "Nothing to write: the piece is empty.";
    return {};
}

// ----------------------------------------------------------------------------- the axml
namespace
{
    juce::String hex4 (int v) { return juce::String::toHexString (v).paddedLeft ('0', 4); }
    juce::String hex8 (int v) { return juce::String::toHexString (v).paddedLeft ('0', 8); }
    juce::String coord (float v) { return juce::String ((double) v, 10); }
    juce::String xmlName (const juce::String& s, const juce::String& fallback)
    {
        auto t = s.trim();
        if (t.isEmpty()) t = fallback;
        if (t.length() > 64) t = t.substring (0, 64).trim();        // (1..64 characters, the profile)
        return t.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;");
    }
    const char* roomCentricName (Spk s)
    {
        switch (s)
        {
            case Spk::L: return "RoomCentricLeft";   case Spk::R: return "RoomCentricRight";  case Spk::C: return "RoomCentricCenter";
            case Spk::LFE: return "RoomCentricLFE"; case Spk::Ls: return "RoomCentricLeftSurround"; case Spk::Rs: return "RoomCentricRightSurround";
            case Spk::Lss: return "RoomCentricLeftSideSurround"; case Spk::Rss: return "RoomCentricRightSideSurround";
            case Spk::Lrs: return "RoomCentricLeftRearSurround"; case Spk::Rrs: return "RoomCentricRightRearSurround";
            case Spk::Ltm: return "RoomCentricLeftTopSurround";  case Spk::Rtm: return "RoomCentricRightTopSurround";      // (the 7.1.2 bed's Lts / Rts)
            default: return "RoomCentricLeft";
        }
    }
    const char* speakerLabel (Spk s)
    {
        switch (s)
        {
            case Spk::L: return "RC_L";     case Spk::R: return "RC_R";     case Spk::C: return "RC_C";     case Spk::LFE: return "RC_LFE";
            case Spk::Ls: return "RC_Ls";   case Spk::Rs: return "RC_Rs";   case Spk::Lss: return "RC_Lss"; case Spk::Rss: return "RC_Rss";
            case Spk::Lrs: return "RC_Lrs"; case Spk::Rrs: return "RC_Rrs"; case Spk::Ltm: return "RC_Lts"; case Spk::Rtm: return "RC_Rts";
            default: return "RC_L";
        }
    }

    /** The IDs of everything in the file, worked out once so the axml and the chna agree (the profile's section 3). */
    struct Ids
    {
        struct Obj { bool bed; int ao; int pack; juce::String name; std::vector<int> trackIdx; };     // pack: the xxxx of AP_ / AC_ (one per bed; one per object)
        struct Trk { bool bed; int packIdx; int chan; int uid; int streamIdx; juce::String channelName; Spk spk; };    // chan: AC_ xxxx; streamIdx: AS_ / AT_ xxxx
        std::vector<Obj> objs;
        std::vector<Trk> trks;
        static Ids of (const Description& d)
        {
            Ids ids;
            // (a stream's / track's xxxx is its channel's: the profile wants AS_yyyyxxxx to match the AC_yyyyxxxx it refers to (2.3) and AT_ to match
            //  AS_ (2.2); the reference master numbers them so, per type)
            int aoNext = 0x1001, bedPack = 0x1001, objPack = 0x1001, bedChan = 0x1001, objChan = 0x1001, uid = 1;
            for (size_t b = 0; b < d.beds.size(); ++b)
            {
                const auto L = layoutFor (d.beds[b].format);
                const auto baseName = xmlName (d.beds[b].name, "Atmos_Bed_" + juce::String ((int) b + 1));
                Obj bed; bed.bed = true; bed.ao = aoNext; bed.pack = bedPack++; bed.name = baseName;
                for (auto& t : L.tracks)
                {
                    if (! t.inBed) continue;
                    Trk k; k.bed = true; k.packIdx = (int) ids.objs.size(); k.chan = bedChan++; k.uid = uid++; k.streamIdx = k.chan; k.channelName = roomCentricName (t.spk); k.spk = t.spk;
                    bed.trackIdx.push_back ((int) ids.trks.size()); ids.trks.push_back (k);
                }
                aoNext += (int) bed.trackIdx.size();                     // (a bed of T tracks at O makes O+1 .. O+T-1 unusable: section 3.5)
                if (aoNext < 0x100b) aoNext = 0x100b;                     // (objects start at 0x100b at the earliest)
                ids.objs.push_back (bed);
                for (auto& t : L.tracks)
                {
                    if (t.inBed) continue;
                    Obj o; o.bed = false; o.ao = aoNext++; o.pack = objPack++; o.name = xmlName (baseName + " " + surround::info (t.spk).shortName, "Atmos_Obj");
                    Trk k; k.bed = false; k.packIdx = (int) ids.objs.size(); k.chan = objChan++; k.uid = uid++; k.streamIdx = k.chan; k.channelName = o.name; k.spk = t.spk;
                    o.trackIdx.push_back ((int) ids.trks.size()); ids.trks.push_back (k);
                    ids.objs.push_back (o);
                }
            }
            return ids;
        }
        static juce::String ap (const Obj& o) { return "AP_" + hex4 (o.bed ? 1 : 3) + hex4 (o.pack); }
        static juce::String ac (const Trk& t) { return "AC_" + hex4 (t.bed ? 1 : 3) + hex4 (t.chan); }
        static juce::String as (const Trk& t) { return "AS_" + hex4 (t.bed ? 1 : 3) + hex4 (t.streamIdx); }
        static juce::String at (const Trk& t) { return "AT_" + hex4 (t.bed ? 1 : 3) + hex4 (t.streamIdx) + "_01"; }
        static juce::String atu (const Trk& t) { return "ATU_" + hex8 (t.uid); }
        static juce::String ao (const Obj& o) { return "AO_" + hex4 (o.ao); }
    };
}

juce::String buildAxml (const Description& d)
{
    const auto ids = Ids::of (d);
    const auto start = timeText (0, d.sampleRate), end = timeText (d.frames, d.sampleRate);
    juce::String x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<ebuCoreMain xmlns=\"urn:ebu:metadata-schema:ebuCore_2016\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
         "xsi:schemaLocation=\"urn:ebu:metadata-schema:ebuCore_2016 ebucore.xsd\" xml:lang=\"en\">\n"
      << "\t<coreMetadata>\n\t\t<format>\n\t\t\t<audioFormatExtended>\n";
    // the programme and the one content (the order Dolby's tools write: programme, content, objects, packs, channels, streams, tracks, UIDs)
    x << "\t\t\t\t<audioProgramme audioProgrammeID=\"APR_1001\" audioProgrammeName=\"" << xmlName (d.programmeName, "Atmos_Master") << "\" start=\"" << start << "\" end=\"" << end << "\">\n"
      << "\t\t\t\t\t<audioContentIDRef>ACO_1001</audioContentIDRef>\n\t\t\t\t</audioProgramme>\n";
    x << "\t\t\t\t<audioContent audioContentID=\"ACO_1001\" audioContentName=\"Atmos_Master_Content\">\n";
    for (auto& o : ids.objs) x << "\t\t\t\t\t<audioObjectIDRef>" << Ids::ao (o) << "</audioObjectIDRef>\n";
    x << "\t\t\t\t\t<dialogue mixedContentKind=\"0\">2</dialogue>\n\t\t\t\t</audioContent>\n";
    for (auto& o : ids.objs)
    {
        x << "\t\t\t\t<audioObject audioObjectID=\"" << Ids::ao (o) << "\" audioObjectName=\"" << o.name << "\" start=\"" << start << "\" duration=\"" << end << "\">\n"
          << "\t\t\t\t\t<audioPackFormatIDRef>" << Ids::ap (o) << "</audioPackFormatIDRef>\n";
        for (int ti : o.trackIdx) x << "\t\t\t\t\t<audioTrackUIDRef>" << Ids::atu (ids.trks[(size_t) ti]) << "</audioTrackUIDRef>\n";
        x << "\t\t\t\t</audioObject>\n";
    }
    for (auto& o : ids.objs)
    {
        x << "\t\t\t\t<audioPackFormat audioPackFormatID=\"" << Ids::ap (o) << "\" audioPackFormatName=\"" << (o.bed ? "AtmosCustomPackFormat" + juce::String (o.pack - 0x1000) : o.name)
          << "\" typeDefinition=\"" << (o.bed ? "DirectSpeakers" : "Objects") << "\" typeLabel=\"" << (o.bed ? "0001" : "0003") << "\">\n";
        for (int ti : o.trackIdx) x << "\t\t\t\t\t<audioChannelFormatIDRef>" << Ids::ac (ids.trks[(size_t) ti]) << "</audioChannelFormatIDRef>\n";
        x << "\t\t\t\t</audioPackFormat>\n";
    }
    for (auto& t : ids.trks)
    {
        x << "\t\t\t\t<audioChannelFormat audioChannelFormatID=\"" << Ids::ac (t) << "\" audioChannelFormatName=\"" << t.channelName
          << "\" typeDefinition=\"" << (t.bed ? "DirectSpeakers" : "Objects") << "\" typeLabel=\"" << (t.bed ? "0001" : "0003") << "\">\n";
        float px, py, pz; objectPosition (t.spk, px, py, pz);
        if (t.bed)
        {
            x << "\t\t\t\t\t<audioBlockFormat audioBlockFormatID=\"AB_" << hex4 (1) << hex4 (t.chan) << "_00000001\">\n"
              << "\t\t\t\t\t\t<speakerLabel>" << speakerLabel (t.spk) << "</speakerLabel>\n"
              << "\t\t\t\t\t\t<cartesian>1</cartesian>\n"
              << "\t\t\t\t\t\t<position coordinate=\"X\">" << coord (px) << "</position>\n"
              << "\t\t\t\t\t\t<position coordinate=\"Y\">" << coord (py) << "</position>\n"
              << "\t\t\t\t\t\t<position coordinate=\"Z\">" << coord (pz) << "</position>\n"
              << "\t\t\t\t\t</audioBlockFormat>\n";
        }
        else
        {   // one block for the whole programme (a fixed object): as Dolby's tools write a static object - Z only when it is not 0
            x << "\t\t\t\t\t<audioBlockFormat audioBlockFormatID=\"AB_" << hex4 (3) << hex4 (t.chan) << "_00000001\" rtime=\"" << start << "\" duration=\"" << end << "\">\n"
              << "\t\t\t\t\t\t<cartesian>1</cartesian>\n"
              << "\t\t\t\t\t\t<position coordinate=\"X\">" << coord (px) << "</position>\n"
              << "\t\t\t\t\t\t<position coordinate=\"Y\">" << coord (py) << "</position>\n";
            if (std::abs (pz) > 1.0e-6f) x << "\t\t\t\t\t\t<position coordinate=\"Z\">" << coord (pz) << "</position>\n";
            x << "\t\t\t\t\t\t<jumpPosition interpolationLength=\"0.000000\">1</jumpPosition>\n"
              << "\t\t\t\t\t</audioBlockFormat>\n";
        }
        x << "\t\t\t\t</audioChannelFormat>\n";
    }
    for (auto& t : ids.trks)
        x << "\t\t\t\t<audioStreamFormat audioStreamFormatID=\"" << Ids::as (t) << "\" audioStreamFormatName=\"PCM_" << t.channelName << "\" formatDefinition=\"PCM\" formatLabel=\"0001\">\n"
          << "\t\t\t\t\t<audioChannelFormatIDRef>" << Ids::ac (t) << "</audioChannelFormatIDRef>\n"
          << "\t\t\t\t\t<audioPackFormatIDRef>" << Ids::ap (ids.objs[(size_t) t.packIdx]) << "</audioPackFormatIDRef>\n"
          << "\t\t\t\t\t<audioTrackFormatIDRef>" << Ids::at (t) << "</audioTrackFormatIDRef>\n"
          << "\t\t\t\t</audioStreamFormat>\n";
    for (auto& t : ids.trks)
        x << "\t\t\t\t<audioTrackFormat audioTrackFormatID=\"" << Ids::at (t) << "\" audioTrackFormatName=\"PCM_" << t.channelName << "\" formatDefinition=\"PCM\" formatLabel=\"0001\">\n"
          << "\t\t\t\t\t<audioStreamFormatIDRef>" << Ids::as (t) << "</audioStreamFormatIDRef>\n"
          << "\t\t\t\t</audioTrackFormat>\n";
    for (auto& t : ids.trks)
        x << "\t\t\t\t<audioTrackUID UID=\"" << Ids::atu (t) << "\" bitDepth=\"" << d.bitDepth << "\" sampleRate=\"" << (int) std::llround (d.sampleRate) << "\">\n"
          << "\t\t\t\t\t<audioTrackFormatIDRef>" << Ids::at (t) << "</audioTrackFormatIDRef>\n"
          << "\t\t\t\t\t<audioPackFormatIDRef>" << Ids::ap (ids.objs[(size_t) t.packIdx]) << "</audioPackFormatIDRef>\n"
          << "\t\t\t\t</audioTrackUID>\n";
    x << "\t\t\t</audioFormatExtended>\n\t\t</format>\n\t</coreMetadata>\n</ebuCoreMain>\n";
    return x;
}

juce::MemoryBlock buildChna (const Description& d)
{
    const auto ids = Ids::of (d);
    juce::MemoryBlock mb;
    const juce::uint16 n = (juce::uint16) ids.trks.size();
    mb.append (&n, 2); mb.append (&n, 2);                                       // numTracks, numUIDs (equal: one UID per track, no unused entries)
    for (size_t i = 0; i < ids.trks.size(); ++i)
    {
        const auto& t = ids.trks[i];
        juce::uint8 e[40] = {};
        const juce::uint16 ti = (juce::uint16) (i + 1);
        std::memcpy (e, &ti, 2);
        auto putStr = [&e] (int at, int len, const juce::String& s) { const auto u = s.toRawUTF8(); std::memcpy (e + at, u, (size_t) juce::jmin (len, (int) std::strlen (u))); };
        putStr (2, 12, Ids::atu (t)); putStr (14, 14, Ids::at (t)); putStr (28, 11, Ids::ap (ids.objs[(size_t) t.packIdx]));
        mb.append (e, 40);
    }
    return mb;
}

// ----------------------------------------------------------------------------- the LFE low-pass
void LfeLowPass::design (double fc, double sr) noexcept
{
    const double k = std::tan (3.14159265358979323846 * juce::jlimit (1.0, sr * 0.45, fc) / sr), k2 = k * k;
    const double q[2] = { 0.5411961, 1.3065630 };                    // (the two sections of a 4th-order Butterworth)
    for (int i = 0; i < 2; ++i)
    {
        const double n = 1.0 / (1.0 + k / q[i] + k2);
        s[i].b0 = k2 * n; s[i].b1 = 2.0 * k2 * n; s[i].b2 = k2 * n;
        s[i].a1 = 2.0 * (k2 - 1.0) * n; s[i].a2 = (1.0 - k / q[i] + k2) * n;
        s[i].z1 = s[i].z2 = 0.0;
    }
}
void LfeLowPass::process (float* x, int n) noexcept { for (int i = 0; i < n; ++i) x[i] = (float) run (x[i]); }

// ----------------------------------------------------------------------------- the file
namespace
{
    void le32 (juce::uint8* p, juce::uint32 v) { for (int i = 0; i < 4; ++i) p[i] = (juce::uint8) (v >> (8 * i)); }
    void le16 (juce::uint8* p, juce::uint16 v) { p[0] = (juce::uint8) v; p[1] = (juce::uint8) (v >> 8); }
    void le64 (juce::uint8* p, juce::uint64 v) { for (int i = 0; i < 8; ++i) p[i] = (juce::uint8) (v >> (8 * i)); }
}

FileWriter::FileWriter (const juce::File& f, const Description& d, bool dith, juce::int64 rf64At)
    : file (f), desc (d), useDither (dith)
{
    if (const auto e = check (d); e.isNotEmpty()) { fail (e); return; }
    channels = d.totalTracks();
    dataBytes = (juce::int64) channels * 3 * d.frames;
    rf64 = dataBytes + 4096 >= rf64At;                                  // (the audio alone decides: the metadata is small next to 4 GB)
    file.deleteFile();
    out.reset (file.createOutputStream().release());
    if (out == nullptr || out->failedToOpen()) { out.reset(); fail ("Cannot write " + file.getFullPathName()); return; }
    seeds.resize ((size_t) channels);
    { dither::Bank bank; for (int c = 0; c < channels; ++c) seeds[(size_t) c] = bank.state[c & 255]; }
    // RIFF / RF64 header, then a 64-byte JUNK (the profile: at most 64) or the ds64 in its place, fmt, and the data chunk's header
    juce::uint8 h[12]; std::memcpy (h, rf64 ? "RF64" : "RIFF", 4); le32 (h + 4, rf64 ? 0xFFFFFFFFu : 0u); std::memcpy (h + 8, "WAVE", 4);
    riffSizeAt = 4;
    put (h, 12);
    {
        juce::uint8 c[8 + 64] = {};
        std::memcpy (c, rf64 ? "ds64" : "JUNK", 4); le32 (c + 4, rf64 ? 28u : 64u);
        ds64At = out != nullptr ? out->getPosition() + 8 : 0;
        put (c, rf64 ? 8 + 28 : 8 + 64);                                 // (the ds64's sizes are patched at the end: riff, data, sample count, no table)
    }
    {
        juce::uint8 c[8 + 16]; std::memcpy (c, "fmt ", 4); le32 (c + 4, 16u);
        const juce::uint32 rate = (juce::uint32) std::llround (d.sampleRate), blockAlign = (juce::uint32) channels * 3u;
        le16 (c + 8, 1); le16 (c + 10, (juce::uint16) channels); le32 (c + 12, rate); le32 (c + 16, rate * blockAlign); le16 (c + 20, (juce::uint16) blockAlign); le16 (c + 22, 24);
        put (c, sizeof (c));
    }
    {
        juce::uint8 c[8]; std::memcpy (c, "data", 4); le32 (c + 4, rf64 ? 0xFFFFFFFFu : (juce::uint32) dataBytes);
        dataSizeAt = out != nullptr ? out->getPosition() + 4 : 0;
        put (c, 8);
        dataStart = out != nullptr ? out->getPosition() : 0;
    }
}

FileWriter::~FileWriter()
{
    if (out != nullptr && ! finished) { out.reset(); file.deleteFile(); }     // (a file that was not finished is no master: gone)
}

bool FileWriter::put (const void* data, size_t bytes)
{
    if (out == nullptr || ! error.isEmpty()) return false;
    if (! out->write (data, bytes)) { fail ("Cannot write " + file.getFullPathName() + " (is the drive full?)"); return false; }
    return true;
}

bool FileWriter::write (const float* const* chans, int numSamples)
{
    if (! ok() || numSamples <= 0) return ok();
    if (written + numSamples > desc.frames) { fail ("More audio than the description says."); return false; }
    bytes.resize ((size_t) numSamples * (size_t) channels * 3);
    juce::uint8* p = bytes.data();
    for (int i = 0; i < numSamples; ++i)
        for (int c = 0; c < channels; ++c)
        {
            const int v = dither::quantise (chans[c][i], 24, useDither, seeds[(size_t) c]);
            p[0] = (juce::uint8) v; p[1] = (juce::uint8) (v >> 8); p[2] = (juce::uint8) (v >> 16);
            p += 3;
        }
    if (! put (bytes.data(), bytes.size())) return false;
    written += numSamples;
    return true;
}

bool FileWriter::putChunk (const char* id, const juce::MemoryBlock& payload)
{
    juce::uint8 c[8]; std::memcpy (c, id, 4); le32 (c + 4, (juce::uint32) payload.getSize());
    if (! put (c, 8) || ! put (payload.getData(), payload.getSize())) return false;
    if (payload.getSize() & 1) { const juce::uint8 pad = 0; if (! put (&pad, 1)) return false; }
    return true;
}

bool FileWriter::finish()
{
    if (! ok()) return false;
    if (finished) return true;
    if (written != desc.frames) { fail ("The audio written (" + juce::String (written) + " samples) is not the length described (" + juce::String (desc.frames) + ")."); return false; }
    if (dataBytes & 1) { const juce::uint8 pad = 0; if (! put (&pad, 1)) return false; }
    const auto axml = buildAxml (desc);
    juce::MemoryBlock ax (axml.toRawUTF8(), std::strlen (axml.toRawUTF8()));
    if (! putChunk ("axml", ax) || ! putChunk ("chna", buildChna (desc))) return false;
    const juce::int64 total = out->getPosition();
    out->flush();
    if (rf64)
    {
        juce::uint8 d[28] = {};
        le64 (d, (juce::uint64) (total - 8)); le64 (d + 8, (juce::uint64) dataBytes); le64 (d + 16, (juce::uint64) desc.frames); le32 (d + 24, 0u);
        if (! out->setPosition (ds64At) || ! put (d, 28)) return false;
    }
    else
    {
        juce::uint8 s[4]; le32 (s, (juce::uint32) (total - 8));
        if (! out->setPosition (riffSizeAt) || ! put (s, 4)) return false;
    }
    out->flush();
    out.reset();
    finished = true;
    return ok();
}
}
} // namespace td
