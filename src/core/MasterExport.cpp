#include "MasterExport.h"
#include "CrashReport.h"
#include "Playback.h"
#include "AudioOps.h"
#include "Resampler.h"
#include "Dither.h"
#include "ReviewPage.h"
#include "ReviewHistory.h"
#include "Loudness.h"
#include "AudioFormats.h"
#include "FileSync.h"
#include <set>

namespace td
{
namespace
{
constexpr int kChunk = 32768;

MasterTags mergeTags (MasterTags t, const MasterTags& album, const juce::String& defaultTitle)
{
    if (t.title.isEmpty()) t.title = defaultTitle;
    if (t.albumArtist.isEmpty()) t.albumArtist = album.albumArtist.isNotEmpty() ? album.albumArtist : album.artist;
    if (t.artist.isEmpty()) t.artist = album.artist.isNotEmpty() ? album.artist : album.albumArtist;
    if (t.album.isEmpty()) t.album = album.album;
    if (t.composer.isEmpty()) t.composer = album.composer;
    if (t.genre.isEmpty()) t.genre = album.genre;
    if (t.year.isEmpty()) t.year = album.year;
    if (t.comment.isEmpty()) t.comment = album.comment;
    if (t.copyright.isEmpty()) t.copyright = album.copyright;
    return t;
}

void appendLe32 (juce::MemoryBlock& b, juce::uint32 v) { for (int i = 0; i < 4; ++i) { const juce::uint8 x = (juce::uint8) (v >> (8 * i)); b.append (&x, 1); } }

bool isRateOkForMp3 (double r) { return std::abs (r - 32000) < 1 || std::abs (r - 44100) < 1 || std::abs (r - 48000) < 1; }

/** 3.26.0 (the stems): a stereo file at rate T, written as a 32-bit float WAV. "" when it went well, "Cancelled." or the problem. */
juce::String resampleFile (juce::AudioFormatManager& fm, const juce::File& src, double T, const juce::File& outFile, const std::atomic<bool>& cancel)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (src));
    if (r == nullptr) return "Cannot read " + src.getFileName();
    auto w = audioops::makeWavWriter (outFile, T, 2, true);
    if (w == nullptr) return "Cannot write " + outFile.getFullPathName();
    SincResampler rl (r->sampleRate, T), rr (r->sampleRate, T);
    std::vector<float> ol, orr; juce::int64 written = 0; bool writeFailed = false;
    auto flush = [&]
    {
        const size_t m = juce::jmin (ol.size(), orr.size());
        if (m == 0) return;
        const float* ptrs[2] = { ol.data(), orr.data() };
        if (! w->writeFromFloatArrays (ptrs, 2, (int) m)) writeFailed = true;
        written += (juce::int64) m;
        ol.erase (ol.begin(), ol.begin() + (long) m); orr.erase (orr.begin(), orr.begin() + (long) m);
    };
    juce::AudioBuffer<float> buf (2, 65536);
    for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += 65536)
    {
        if (cancel.load()) { w.reset(); outFile.deleteFile(); return "Cancelled."; }
        const int m = (int) juce::jmin ((juce::int64) 65536, r->lengthInSamples - pos);
        r->read (&buf, 0, m, pos, true, true);
        rl.process (buf.getReadPointer (0), (size_t) m, ol); rr.process (buf.getReadPointer (1), (size_t) m, orr);
        flush();
    }
    rl.finish (ol); rr.finish (orr); flush();
    w.reset();
    if (writeFailed) { outFile.deleteFile(); return "Could not write " + outFile.getFileName() + ": the drive did not take it (is it full?)"; }
    return audioops::checkWrittenLength (&fm, outFile, written);
}
} // namespace

double masterTargetRate (const MasterExportSettings& o, double editRate)
{
    if (o.format == MasterFormat::Mp3)
    {
        if (o.sampleRate > 0.0) return o.sampleRate;
        if (isRateOkForMp3 (editRate)) return editRate;
        return std::fmod (editRate, 11025.0) < 1.0 ? 44100.0 : 48000.0;
    }
    return o.sampleRate > 0.0 ? o.sampleRate : editRate;
}

juce::String masterFileExtension (MasterFormat f)
{
    switch (f) { case MasterFormat::Wav: return ".wav"; case MasterFormat::Aiff: return ".aif"; case MasterFormat::Flac: return ".flac"; case MasterFormat::Ogg: return ".ogg"; case MasterFormat::Mp3: return ".mp3"; }
    return ".wav";
}

juce::String findLame()
{
    const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (auto name : { "lame.exe", "lame" })
        for (auto& dir : { exeDir, exeDir.getChildFile ("lame"), exeDir.getChildFile ("tools") })
        {
            auto f = dir.getChildFile (name);
            if (f.existsAsFile()) return f.getFullPathName();
        }
    juce::ChildProcess p;                                                    // or one that is installed on the PATH
    if (p.start (juce::StringArray { "lame", "--version" }))
        if (p.waitForProcessToFinish (4000) && p.getExitCode() == 0) return "lame";
    return {};
}

bool addFlacTags (const juce::File& flac, const MasterTags& t, int trackNo)
{
    juce::MemoryBlock data;
    if (! flac.loadFileAsData (data) || data.getSize() < 8 || std::memcmp (data.getData(), "fLaC", 4) != 0) return false;
    auto* b = static_cast<juce::uint8*> (data.getData());
    size_t pos = 4, lastHeader = 0; bool done = false;
    while (! done)
    {
        if (pos + 4 > data.getSize()) return false;
        lastHeader = pos;
        done = (b[pos] & 0x80) != 0;
        const size_t len = ((size_t) b[pos + 1] << 16) | ((size_t) b[pos + 2] << 8) | b[pos + 3];
        pos += 4 + len;
        if (pos > data.getSize()) return false;
    }
    std::vector<std::pair<juce::String, juce::String>> kv {
        { "TITLE", t.title }, { "ARTIST", t.artist }, { "ALBUM", t.album }, { "ALBUMARTIST", t.albumArtist }, { "COMPOSER", t.composer }, { "GENRE", t.genre },
        { "DATE", t.year }, { "COMMENT", t.comment }, { "COPYRIGHT", t.copyright }, { "ISRC", compactIsrc (t.isrc) }, { "TRACKNUMBER", trackNo > 0 ? juce::String (trackNo) : juce::String() } };
    juce::MemoryBlock vc;
    const juce::String vendor = "Fermata";
    appendLe32 (vc, (juce::uint32) vendor.getNumBytesAsUTF8()); vc.append (vendor.toRawUTF8(), vendor.getNumBytesAsUTF8());
    juce::uint32 count = 0; for (auto& p : kv) if (p.second.isNotEmpty()) ++count;
    appendLe32 (vc, count);
    for (auto& p : kv)
        if (p.second.isNotEmpty())
        {
            const auto line = p.first + "=" + p.second;
            appendLe32 (vc, (juce::uint32) line.getNumBytesAsUTF8()); vc.append (line.toRawUTF8(), line.getNumBytesAsUTF8());
        }
    juce::MemoryBlock out;
    b[lastHeader] &= 0x7f;                                                   // the old last block is not the last any more
    out.append (data.getData(), pos);
    const juce::uint8 hdr[4] = { 0x84, (juce::uint8) (vc.getSize() >> 16), (juce::uint8) (vc.getSize() >> 8), (juce::uint8) vc.getSize() };   // VORBIS_COMMENT, last
    out.append (hdr, 4); out.append (vc.getData(), vc.getSize());
    out.append (static_cast<const char*> (data.getData()) + pos, data.getSize() - pos);
    juce::TemporaryFile tmp (flac);                                           // (checked, and the untagged file stays if the tagged one cannot be written: 3.1.3)
    return filesync::writeDataChecked (tmp.getFile(), out.getData(), out.getSize()) && tmp.overwriteTargetFileWithTemporary();
}

// ----------------------------------------------------------------------------- building the lists
juce::String resolveRenderSource (const Project& p, const MasteringDef& def, juce::Uuid& mixerId, juce::Uuid& sourceId)
{
    mixerId = juce::Uuid::null(); sourceId = juce::Uuid::null();
    if (p.mixers.empty()) return "The project has no mixer.";
    mixerId = p.mixers.front()->id;
    if (! def.mixerId.isNull()) for (auto& m : p.mixers) if (m->id == def.mixerId) mixerId = m->id;
    // (3.39.0: a surround bus is never the source: the Mastering window makes stereo masters; a Dolby Atmos master is the Composer's file)
    if (! def.sourceId.isNull() && p.kindOf (def.sourceId) != NodeKind::None && ! surround::isSurround (p.busFormat (def.sourceId))) sourceId = def.sourceId;
    else for (auto& b : p.buses) if (b.external && ! surround::isSurround (b.format)) { sourceId = b.id; break; }
    if (sourceId.isNull()) return "There is nothing to render yet: add a stereo Ext bus (and route the tracks to it) in the Project Designer, or choose an output in the Mastering window.";
    return {};
}

namespace
{
juce::String commonChecks (const Project& p, const MasteringDef& def, MasterJobSpec& spec, std::vector<EditDef*>& edits, bool needSource = true)
{
    juce::String err = resolveRenderSource (p, def, spec.mixerId, spec.sourceId);
    if (err.isNotEmpty() && (needSource || spec.mixerId.isNull())) return err;       // (3.46.0: without a stereo output only when none is needed; never without a mixer)
    spec.tailSeconds = def.tailSeconds;
    spec.ownMixers = def.mixerId.isNull();               // "each piece's own mixer" unless one mixer was chosen for everything
    if (edits.empty()) return "Tick at least one edit that has audio in it.";
    for (auto* e : edits) if (std::abs (e->sampleRate - edits.front()->sampleRate) > 0.5)
        return "The ticked edits have different sample rates (" + juce::String (edits.front()->sampleRate, 0) + " and " + juce::String (e->sampleRate, 0) + " Hz). Tick edits with the same rate and export the others separately.";
    return {};
}
} // namespace

juce::String makeFilesSpec (const Project& p, const MasteringDef& def, MasterJobSpec& spec, bool listeningCopies, bool needSource)
{
    spec = MasterJobSpec();
    spec.kind = MasterJobSpec::Kind::Files; spec.out = def.vm;
    if (def.vm.folder.trim().isEmpty()) return "Choose the folder the files go into.";
    spec.destFolder = juce::File (def.vm.folder.trim());
    spec.workFolder = spec.destFolder.getChildFile ("_fermata_work");
    std::vector<EditDef*> edits; std::vector<const MasterItem*> its;
    std::vector<int> listNo;                                               // 3.46.1: each exported piece's number in the Mastering window's list (1 = the first row)
    for (size_t k = 0; k < def.items.size(); ++k)
    {
        auto& it = def.items[k];
        if (! it.include) continue;
        auto* e = const_cast<Project&> (p).findEdit (it.editId);
        if (e == nullptr || e->isEmpty()) continue;
        edits.push_back (e); its.push_back (&it); listNo.push_back ((int) k + 1);
    }
    auto err = commonChecks (p, def, spec, edits, needSource);
    if (err.isNotEmpty()) return err;
    if (def.vm.format == MasterFormat::Mp3 || (def.vm.extraFormats & (1 << (int) MasterFormat::Mp3)) != 0)
    {
        if (findLame().isEmpty()) return "MP3 files need the free LAME encoder. Put lame.exe in the same folder as Fermata, then try again. (LAME is not included because it is a separate program.)";
        const double r = def.vm.sampleRate > 0.0 ? def.vm.sampleRate : edits.front()->sampleRate;
        if (! isRateOkForMp3 (masterTargetRate (def.vm, r))) return "MP3 supports 32, 44.1 and 48 kHz only. Choose one of those sample rates.";
    }
    for (auto& b : p.buses)                                                // 3.26.0: the stems, in the project's order of buses (gone ones and the master's own source left out)
        if (b.id != spec.sourceId && std::find (def.stemIds.begin(), def.stemIds.end(), b.id) != def.stemIds.end())
        { spec.stemIds.push_back (b.id); spec.stemNames.push_back (b.name.trim().isNotEmpty() ? b.name.trim() : juce::String ("Bus")); }
    for (size_t i = 0; i < edits.size(); ++i)
    {
        // 3.46.1: a piece keeps the number it has in the list, whichever pieces are ticked - pieces 11 and 12 exported on their own are "11 - ..." and
        // "12 - ...", with track numbers 11 and 12 in their tags. (They used to be counted afresh from 1 among the ticked ones: asked for on 9 November 2026.)
        const int n = listNo[i];
        MasterJobItem j; j.editId = edits[i]->id; j.trackNo = n;
        j.editName = edits[i]->name; j.projectName = p.name;
        j.editOffsetSeconds = edits[i]->sampleRate > 0.0 ? (double) edits[i]->audibleStart() / edits[i]->sampleRate : 0.0;     // (where the render starts on the edit's timeline)
        auto base = its[i]->fileName.trim().isNotEmpty() ? its[i]->fileName.trim() : edits[i]->name;
        auto title = its[i]->tags.title.isNotEmpty() ? its[i]->tags.title : base;
        if (listeningCopies)
        {   // 3.42.0: a listening copy is of a version of the edit, and says which: "The Lion - Edit 2 - More reverb" (its comments come back to that version)
            // 3.52.0: the newest version - the edit as it is now - unless an older, frozen one is chosen for this piece (MasterItem::formVersion)
            int ver = edits[i]->versionNumber();
            if (const int want = its[i]->formVersion; want > 0 && want < ver)
            {
                const auto* ve = edits[i]->versionEdit (want);
                if (ve == nullptr) return "\"" + edits[i]->name + "\": Edit " + juce::String (want) + " is not there any more. Choose another version for its listening copy.";
                if (ve->isEmpty()) return "\"" + edits[i]->versionTitle (want) + "\" has no audio in it.";
                if (std::abs (ve->sampleRate - edits[i]->sampleRate) > 0.5) return "\"" + edits[i]->versionTitle (want) + "\" is at another sample rate than the edit is now: its listening copy cannot be made.";
                ver = want; j.frozenVersion = true;
                j.editOffsetSeconds = ve->sampleRate > 0.0 ? (double) ve->audibleStart() / ve->sampleRate : 0.0;         // (the version's own start)
            }
            const auto versionTail = edits[i]->versionTail (ver);                                                      // " - Edit 2 - More reverb"
            j.editVersion = ver; j.editName = edits[i]->versionTitle (ver);
            j.history = reviewpage::historyFor (*edits[i], ver, j.editOffsetSeconds);                                  // 3.51.0: the comments so far go into the copy (3.52.0: as they stood for that version)
            base += versionTail; title += versionTail;
        }
        j.baseName = def.vm.numberFiles ? juce::String (n).paddedLeft ('0', 2) + " - " + base : base;
        j.tags = mergeTags (its[i]->tags, def.vm.album, title);
        spec.items.push_back (j);
    }
    return {};
}

juce::String makeFormsSpec (const Project& p, const MasteringDef& def, MasterJobSpec& spec)
{
    auto d = def;                                            // the Virtual Master's pieces, names and levels; the audio as set on the Listening copy tab
    d.vm.format = MasterFormat::Wav; d.vm.extraFormats = 0;
    d.vm.sampleRate = def.vm.formAudio == 2 && (def.vm.formRate <= 0.0 || def.vm.formRate > 48000.0) ? 48000.0 : def.vm.formRate;
    d.vm.bitDepth = def.vm.formBits == 16 ? 16 : 24;
    d.vm.folder = def.vm.formFolder.trim().isNotEmpty() ? def.vm.formFolder.trim() : p.masteredFolder().getChildFile ("Listening copies").getFullPathName();
    d.vm.normalise = def.vm.formNormalise; d.vm.loudnessMode = def.vm.formLoudnessMode; d.vm.peakDb = def.vm.formPeakDb;      // the tab's own levels
    d.vm.targetLufs = def.vm.formTargetLufs; d.vm.ceilingDbTp = def.vm.formCeilingDbTp; d.vm.peakTogether = def.vm.formTogether;
    auto err = makeFilesSpec (p, d, spec, true);
    if (err.isNotEmpty()) return err;
    if (def.vm.formAudio == 2 && findLame().isEmpty())
        return "MP3 inside the listening copy needs the free LAME encoder. Put lame.exe in the same folder as Fermata, or choose WAV or FLAC.";
    spec.formsOnly = true;
    spec.stemIds.clear(); spec.stemNames.clear();            // (no stems with the listening copies)
    spec.out.feedbackForm = true;
    spec.out.formAudio = def.vm.formAudio;
    spec.formSingle = def.vm.formSingle;
    spec.formTitle = def.vm.album.album.trim().isNotEmpty() ? def.vm.album.album.trim() : (p.name.trim().isNotEmpty() ? p.name.trim() : juce::String ("Listening copy"));
    if (! spec.destFolder.createDirectory()) return "Cannot make the folder " + spec.destFolder.getFullPathName();
    return {};
}

juce::String makeDiscSpec (const Project& p, const MasteringDef& def, MasterJobSpec& spec)
{
    spec = MasterJobSpec();
    spec.kind = MasterJobSpec::Kind::Disc; spec.disc = def.ddp;
    if (def.ddp.folder.trim().isEmpty()) return "Choose the folder the DDP goes into.";
    spec.destFolder = juce::File (def.ddp.folder.trim());
    spec.workFolder = spec.destFolder.getChildFile ("_fermata_work");
    spec.out = MasterExportSettings();
    spec.out.format = MasterFormat::Wav; spec.out.sampleRate = 44100.0; spec.out.bitDepth = 16; spec.out.dither = DitherMode::Tpdf;
    spec.out.normalise = def.ddp.normalise; spec.out.peakDb = def.ddp.peakDb; spec.out.peakTogether = def.ddp.peakTogether;
    spec.discName = def.ddp.title.trim().isNotEmpty() ? def.ddp.title.trim() : juce::String ("CD master");
    std::vector<EditDef*> edits;
    for (size_t i = 0; i < def.ddp.clips.size(); ++i)
    {
        auto& c = def.ddp.clips[i];
        if (! c.include) continue;
        auto* e = const_cast<Project&> (p).findEdit (c.editId);
        if (e == nullptr || e->isEmpty()) continue;
        edits.push_back (e); spec.clipIndex.push_back ((int) i);
    }
    auto err = commonChecks (p, def, spec, edits);
    if (err.isNotEmpty()) return err;
    int n = 0;
    for (auto* e : edits) { MasterJobItem j; j.editId = e->id; j.trackNo = ++n; j.baseName = "clip-" + juce::String (n).paddedLeft ('0', 2); spec.items.push_back (j); }
    return {};
}

// ----------------------------------------------------------------------------- the job
MasterExportJob::MasterExportJob (const Project& live, const MasterJobSpec& s, std::function<void (MasterExportResult)> done, bool startNow)
    : juce::Thread ("Mastering export"), spec (s), onDone (std::move (done))
{
    std::vector<juce::Uuid> ids;
    itemBatch.assign (spec.items.size(), -1);
    for (size_t i = 0; i < spec.items.size(); ++i)
    {
        auto* e = const_cast<Project&> (live).findEdit (spec.items[i].editId);
        if (e == nullptr || e->isEmpty()) { earlyError = "An edit no longer exists or is empty."; break; }
        if (spec.items[i].frozenVersion && spec.formsOnly) continue;      // (3.52.0: a frozen version has its own render, below)
        itemBatch[i] = (int) ids.size(); ids.push_back (e->id);
    }
    if (earlyError.isEmpty())
    {
        RenderParams rp; rp.sourceId = spec.sourceId; rp.mixerId = spec.mixerId; rp.ownMixers = spec.ownMixers; rp.tailSeconds = spec.tailSeconds;
        const bool stems = spec.kind == MasterJobSpec::Kind::Files && ! spec.formsOnly && ! spec.stemIds.empty();
        if (! ids.empty())
        {
            batch = std::make_unique<MasterRenderBatch> (live, ids, rp, false, stems ? spec.stemIds : std::vector<juce::Uuid>(), spec.workFolder.getChildFile ("stems"));
            if (batch->getPrepareError().isNotEmpty()) earlyError = batch->getPrepareError();
        }
        // 3.52.0: the frozen versions. Each is rendered like a piece of the batch - 32-bit float, its own rate, the tail - but from the version:
        // its pieces, fades, levels, volume areas and automation as they were frozen, through its edit's mixer set as it was frozen (plug-ins and
        // all), in the render's private copy of the project. With "all pieces through one mixer" chosen, or a version frozen before its edit
        // had a mixer of its own, the mixer is as it is now, and the result says so.
        for (size_t i = 0; i < spec.items.size() && earlyError.isEmpty(); ++i)
        {
            if (itemBatch[i] >= 0) continue;
            auto* e = const_cast<Project&> (live).findEdit (spec.items[i].editId);
            const auto* v = e->version (spec.items[i].editVersion);
            if (v == nullptr || v->edit == nullptr || v->edit->isEmpty()) { earlyError = "\"" + spec.items[i].editName + "\" is not there any more."; break; }
            const auto& ve = *v->edit;
            VersionRender vr; vr.item = (int) i; vr.name = spec.items[i].editName;
            BounceSettings bs;
            bs.folder = spec.workFolder.getChildFile ("versions"); bs.mixerId = rp.mixerId; bs.sources.push_back (rp.sourceId);
            bs.addOutputName = false; bs.floatFiles = true; bs.normalise = false; bs.tailSeconds = rp.tailSeconds; bs.sampleRate = ve.sampleRate;
            BounceItem bi;
            bi.name = "version-" + juce::String ((int) i + 1);
            bi.start = ve.audibleStart(); bi.end = ve.audibleEnd();
            bi.segments = segmentsForEdit (live, ve);
            bi.automationEdit = ve.hasAutomation() ? ve.id : juce::Uuid::null();
            bi.alignOwner = live.alignOwnerFor (true, e->id);
            BounceEditOverride ov; ov.edit = v->edit;
            if (rp.ownMixers)
            {
                if (auto* em = live.mixerOfEdit (e->id))
                {
                    bi.mixerId = em->id;
                    if (! v->mixer.isVoid()) { ov.mixer = v->mixer; ov.mixerId = em->id; }
                    else vr.note = "\"" + vr.name + "\": this version kept no mixer settings of its own, so it went through the edit's mixer as it is now.";
                }
            }
            else vr.note = "\"" + vr.name + "\": \"all pieces through one mixer\" is chosen, so it went through that mixer as it is now (its pieces and automation are the version's).";
            bs.overrides.push_back (std::move (ov));
            bs.items.push_back (std::move (bi));
            vr.bouncer = std::make_unique<Bouncer> (live, bs);
            if (vr.bouncer->getPrepareError().isNotEmpty()) earlyError = "\"" + vr.name + "\": " + vr.bouncer->getPrepareError();
            versionRenders.push_back (std::move (vr));
        }
    }
    if (startNow) startThread (juce::Thread::Priority::normal);
}

MasterExportJob::~MasterExportJob()
{
    cancelFlag = true;
    decide (3);                                                          // (a job waiting for the loudness decision is let go)
    stopThread (15000);
}

float MasterExportJob::getProgress() const
{
    const float p = sub.load();
    switch (stage.load()) { case 0: return 0.55f * p; case 1: return 0.55f + 0.20f * p; default: return 0.75f + 0.25f * p; }
}
juce::String MasterExportJob::getStatus() const { const juce::ScopedLock sl (lock); return status; }

void MasterExportJob::run()
{
    td::crashreport::nameThisThread (getThreadName());                // (3.17.0: named for the log)
    auto result = execute();
    auto fn = onDone;
    juce::MessageManager::callAsync ([fn, result] { if (fn) fn (result); });
}

// ============================================================================ the loudness check
LoudnessCheck planLoudness (const std::vector<juce::String>& names, const std::vector<double>& lufs, const std::vector<double>& truePeakDb, double albumLufs,
                            double targetLufs, double ceilingDbTp, bool perAlbum)
{
    LoudnessCheck c; c.targetLufs = targetLufs; c.ceilingDbTp = ceilingDbTp; c.perAlbum = perAlbum; c.albumLufs = albumLufs;
    const double albumGain = albumLufs > -99.0 ? targetLufs - albumLufs : 0.0;
    for (size_t i = 0; i < names.size(); ++i)
    {
        LoudnessCheck::Piece p; p.name = names[i]; p.lufs = i < lufs.size() ? lufs[i] : -100.0; p.truePeakDb = i < truePeakDb.size() ? truePeakDb[i] : -100.0;
        p.gainDb = perAlbum ? albumGain : (p.lufs > -99.0 ? targetLufs - p.lufs : 0.0);
        p.peakAtTargetDb = p.truePeakDb > -99.0 ? p.truePeakDb + p.gainDb : -100.0;
        p.excessDb = juce::jmax (0.0, p.peakAtTargetDb - ceilingDbTp);
        c.pieces.push_back (p);
    }
    return c;
}

std::vector<double> loudnessGains (const LoudnessCheck& c, int decision)
{
    std::vector<double> g;
    const double worst = c.worstExcessDb();
    for (auto& p : c.pieces)
    {
        double v = p.gainDb;
        if (decision == 1)                                               // retain the peak: the ceiling holds, the piece (per album: every piece, by the worst one) comes out below the target
            v -= c.perAlbum ? worst : p.excessDb;
        g.push_back (v);
    }
    return g;
}

juce::String LoudnessCheck::text() const
{
    juce::StringArray lines;
    auto db = [] (double v, int places = 1) { return v <= -99.0 ? juce::String ("-inf") : (v >= 0.0 ? "+" : "") + juce::String (v, places); };
    lines.add ("Target " + juce::String (targetLufs, 1) + " LUFS, true peak no higher than " + db (ceilingDbTp) + " dBTP, " + (perAlbum ? "one gain for the album (measured as one programme: " + juce::String (albumLufs, 1) + " LUFS)" : "each piece on its own") + ".");
    for (auto& p : pieces)
        lines.add (p.name + ": " + db (p.lufs) + " LUFS, true peak " + db (p.truePeakDb) + " dBTP; gain " + db (p.gainDb) + " dB -> peak " + db (p.peakAtTargetDb) + " dBTP"
                   + (p.excessDb > 0.0 ? "  **  " + juce::String (p.excessDb, 1) + " dB over the ceiling: needs that much limiting or compression to reach the target" : juce::String()));
    return lines.joinIntoString ("\n");
}

juce::String MasterExportJob::finalise (const juce::File& src, const juce::File& dest, double rate, float gain, const MasterJobItem& item, float& peakOut, MasterFormat fmt)
{
    juce::AudioFormatManager fm; td::registerAudioFormats (fm);
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (src));
    if (r == nullptr) return "Cannot read " + src.getFileName();
    const auto& o = spec.out;
    const bool mp3 = fmt == MasterFormat::Mp3;
    const auto lameIn = spec.workFolder.getChildFile ("lame-in.wav"), lameOut = spec.workFolder.getChildFile ("lame-out.mp3");
    const auto target = mp3 ? lameIn : dest;

    int bits = 24;
    switch (fmt) { case MasterFormat::Wav: bits = o.bitDepth; break; case MasterFormat::Aiff: case MasterFormat::Flac: bits = o.bitDepth == 16 ? 16 : 24; break; case MasterFormat::Ogg: bits = 16; break; case MasterFormat::Mp3: bits = 16; break; }
    const bool floatOut = fmt == MasterFormat::Wav && bits == 32;
    const bool lossy = fmt == MasterFormat::Ogg;
    const bool quantise = ! floatOut && ! lossy;
    const float scale = bits == 16 ? 32768.0f : 8388608.0f;
    const bool dither = quantise && o.dither == DitherMode::Tpdf;

    std::unordered_map<juce::String, juce::String> md;
    const auto& t = item.tags;
    auto add = [&] (const char* k, const juce::String& v) { if (v.isNotEmpty()) md[juce::String (k)] = v; };
    if (fmt == MasterFormat::Wav)
    {
        add (juce::WavAudioFormat::riffInfoTitle, t.title); add (juce::WavAudioFormat::riffInfoArtist, t.artist); add (juce::WavAudioFormat::riffInfoProductName, t.album);
        add (juce::WavAudioFormat::riffInfoGenre, t.genre); add (juce::WavAudioFormat::riffInfoDateCreated, t.year); add (juce::WavAudioFormat::riffInfoComment, t.comment);
        add (juce::WavAudioFormat::riffInfoCopyright, t.copyright); add (juce::WavAudioFormat::riffInfoMusicBy, t.composer);
        if (item.trackNo > 0) add (juce::WavAudioFormat::riffInfoTrackNumber, juce::String (item.trackNo));
    }
    else if (fmt == MasterFormat::Ogg)
    {
        add (juce::OggVorbisAudioFormat::id3title, t.title); add (juce::OggVorbisAudioFormat::id3artist, t.artist); add (juce::OggVorbisAudioFormat::id3album, t.album);
        add (juce::OggVorbisAudioFormat::id3comment, t.comment); add (juce::OggVorbisAudioFormat::id3date, t.year); add (juce::OggVorbisAudioFormat::id3genre, t.genre);
        if (item.trackNo > 0) add (juce::OggVorbisAudioFormat::id3trackNumber, juce::String (item.trackNo));
    }

    std::unique_ptr<juce::AudioFormat> af;
    switch (fmt)
    {
        case MasterFormat::Wav: case MasterFormat::Mp3: af = std::make_unique<juce::WavAudioFormat>(); break;
        case MasterFormat::Aiff: af = std::make_unique<juce::AiffAudioFormat>(); break;
        case MasterFormat::Flac: af = std::make_unique<juce::FlacAudioFormat>(); break;
        case MasterFormat::Ogg:  af = std::make_unique<juce::OggVorbisAudioFormat>(); break;
    }
    target.getParentDirectory().createDirectory();
    target.deleteFile();
    audioops::CheckedAudioFile checked (target);                   // (the file itself is kept, so what the writer's destructor flushes can be checked too: 3.1.3)
    if (checked.failedToOpen()) return "Cannot write " + target.getFullPathName();
    auto stream = checked.stream();
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (bits);
    if (floatOut) opts = opts.withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    if (fmt == MasterFormat::Ogg) opts = opts.withQualityOptionIndex (juce::jlimit (0, 10, o.oggQuality));
    if (fmt == MasterFormat::Flac) opts = opts.withQualityOptionIndex (juce::jlimit (0, 8, o.flacLevel));
    if (! md.empty()) opts = opts.withMetadataValues (md);
    auto w = af->createWriterFor (stream, opts);
    if (w == nullptr) return "This file format cannot be written with these settings (" + juce::String (bits) + " bit, " + juce::String (rate, 0) + " Hz).";

    juce::AudioBuffer<float> buf (2, kChunk);
    DitherOut dout (*w, dither, 20260101u);          // TPDF dither, then the integer words are made exactly (not by the writer's own rounding)
    float peak = 0.0f;
    for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += kChunk)
    {
        if (cancelFlag.load()) { w.reset(); target.deleteFile(); return "Cancelled."; }
        const int n = (int) juce::jmin ((juce::int64) kChunk, r->lengthInSamples - pos);
        r->read (&buf, 0, n, pos, true, true);
        for (int c = 0; c < 2; ++c)
        {
            float* d = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                float x = d[i] * gain;
                peak = juce::jmax (peak, std::abs (x));
                if (quantise && ! dither)                      // dither Off: plain rounding to the word length
                    x = juce::jlimit (-scale, scale - 1.0f, std::nearbyint (x * scale)) / scale;
                d[i] = x;
            }
        }
        if (! dout.write (buf, 0, n)) { w.reset(); target.deleteFile(); return "Could not write " + target.getFileName() + ": the drive did not take it (is it full?)"; }     // (3.1.3)
        sub = (float) ((double) (pos + n) / (double) juce::jmax ((juce::int64) 1, r->lengthInSamples));
    }
    const auto frames = r->lengthInSamples;
    w.reset(); r.reset();
    // every byte reached the disk, and the file holds every sample (a full drive can leave it short with every write having said it went well: 3.1.3)
    if (const auto e = checked.finish(); e.isNotEmpty()) { target.deleteFile(); return e; }
    if (const auto e = audioops::checkWrittenLength (&fm, target, frames); e.isNotEmpty()) { target.deleteFile(); return e; }
    peakOut = peak;
    if (fmt == MasterFormat::Flac && ! addFlacTags (target, t, item.trackNo)) { target.deleteFile(); return "Could not write the tags into " + target.getFileName() + " (is the drive full?)"; }
    if (fmt == MasterFormat::Flac) { if (const auto e = audioops::checkWrittenLength (&fm, target, frames); e.isNotEmpty()) { target.deleteFile(); return e; } }    // (written again with its tags)
    if (mp3)
    {
        const auto lame = findLame();
        if (lame.isEmpty()) return "LAME was not found.";
        juce::StringArray args; args.add (lame);
        if (o.mp3Kbps > 0) { args.add ("-b"); args.add (juce::String (o.mp3Kbps)); args.add ("--cbr"); } else { args.add ("-V"); args.add (juce::String (juce::jlimit (0, 9, o.mp3Vbr))); }
        args.add ("--quiet");
        auto tag = [&] (const char* opt, const juce::String& v) { if (v.isNotEmpty()) { args.add (opt); args.add (v); } };
        tag ("--tt", t.title); tag ("--ta", t.artist); tag ("--tl", t.album); tag ("--ty", t.year); tag ("--tc", t.comment); tag ("--tg", t.genre);
        if (item.trackNo > 0) tag ("--tn", juce::String (item.trackNo));
        if (t.composer.isNotEmpty()) { args.add ("--tv"); args.add ("TCOM=" + t.composer); }
        if (t.albumArtist.isNotEmpty()) { args.add ("--tv"); args.add ("TPE2=" + t.albumArtist); }
        if (t.copyright.isNotEmpty()) { args.add ("--tv"); args.add ("TCOP=" + t.copyright); }
        if (compactIsrc (t.isrc).isNotEmpty()) { args.add ("--tv"); args.add ("TSRC=" + compactIsrc (t.isrc)); }
        args.add (lameIn.getFullPathName()); args.add (lameOut.getFullPathName());
        lameOut.deleteFile();
        juce::ChildProcess cp;
        if (! cp.start (args)) return "Could not start LAME.";
        while (cp.isRunning())
        {
            if (cancelFlag.load()) { cp.kill(); lameIn.deleteFile(); return "Cancelled."; }
            juce::Thread::sleep (80);
        }
        const auto output = cp.readAllProcessOutput();
        if (cp.getExitCode() != 0 || ! lameOut.existsAsFile()) { lameIn.deleteFile(); return "LAME failed: " + output.trim(); }
        lameIn.deleteFile();
        dest.deleteFile();
        if (! lameOut.moveFileTo (dest)) return "Cannot write " + dest.getFullPathName();
    }
    return {};
}

MasterExportResult MasterExportJob::execute()
{
    auto R = executeInner();
    if (spec.workFolder.getFileName() == "_fermata_work") spec.workFolder.deleteRecursively();
    return R;
}

MasterExportResult MasterExportJob::executeInner()
{
    MasterExportResult R;
    auto setStatus = [&] (const juce::String& s) { const juce::ScopedLock sl (lock); status = s; };
    if (earlyError.isNotEmpty()) { R.error = earlyError; return R; }
    const int n = (int) spec.items.size();
    if (n == 0 || (batch == nullptr && versionRenders.empty())) { R.error = "Nothing to export."; return R; }
    if (! spec.destFolder.createDirectory()) { R.error = "Cannot create " + spec.destFolder.getFullPathName(); return R; }

    // ---- 1. every edit through its mixer (32-bit float, nothing clipped, native sample rate). Renders that are up to date are reused; the others are made now and kept.
    stage = 0; sub = 0.0f; setStatus ("Rendering through the mixer...");
    std::vector<juce::File> rendered ((size_t) n);
    if (batch != nullptr)
    {
        const auto rerr = batch->run (&sub, &cancelFlag);
        if (rerr == "Cancelled.") { R.cancelled = true; return R; }
        if (rerr.isNotEmpty()) { R.error = rerr; return R; }
        if (batch->total() != n - (int) versionRenders.size()) { R.error = "The renderer made a different number of files than expected."; return R; }
    }
    for (int i = 0; i < n; ++i)
        if (const int b = itemBatch[(size_t) i]; b >= 0)
        {
            rendered[(size_t) i] = batch->fileFor (b);
            if (batch->isPrevious (b)) R.previousPieces.add (batch->nameFor (b));      // (3.46.0: said in the box at the end)
        }
    for (auto& vr : versionRenders)                                       // 3.52.0: the frozen versions, each from its own render copy
    {
        setStatus ("Rendering " + vr.name + " through the mixer as it was frozen...");
        const auto br = vr.bouncer->render (&sub, &cancelFlag);
        if (br.cancelled) { R.cancelled = true; return R; }
        if (br.error.isNotEmpty()) { R.error = "\"" + vr.name + "\": " + br.error; return R; }
        if (br.files.size() != 1) { R.error = "The renderer made a different number of files than expected."; return R; }
        rendered[(size_t) vr.item] = juce::File (br.files[0]);
        if (vr.note.isNotEmpty()) R.versionNotes.add (vr.note);
    }
    const int nStems = batch != nullptr ? batch->numStems() : 0;          // 3.26.0: each piece's stems (their renders are in the work folder)
    std::vector<std::vector<juce::File>> stemProc ((size_t) n);
    for (int i = 0; i < n; ++i) if (const int b = itemBatch[(size_t) i]; b >= 0) for (int st = 0; st < nStems; ++st) stemProc[(size_t) i].push_back (batch->stemFileFor (b, st));
    // (the batch - and the render copies' plug-in instances inside its Bouncers - is kept until the job is destroyed on the message thread: plug-ins
    // expect to be released on the thread that made them - 3.27.1)

    // ---- 2. sample rate, and the peak of every file
    stage = 1; sub = 0.0f;
    juce::AudioFormatManager fm; td::registerAudioFormats (fm);
    std::vector<juce::File> proc; std::vector<float> peaks ((size_t) n, 0.0f); std::vector<double> rates ((size_t) n, 0.0);
    R.frames.assign ((size_t) n, 0);
    for (int i = 0; i < n; ++i)
    {
        if (cancelFlag.load()) { R.cancelled = true; return R; }
        setStatus ("Converting " + juce::String (i + 1) + " of " + juce::String (n) + "...");
        const juce::File src (rendered[(size_t) i]);
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (src));
        if (r == nullptr) { R.error = "Cannot read " + src.getFileName(); return R; }
        const double native = r->sampleRate, T = masterTargetRate (spec.out, native);
        rates[(size_t) i] = T;
        juce::AudioBuffer<float> buf (2, 65536);
        if (std::abs (T - native) < 0.5)
        {
            for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += 65536)
            {
                const int m = (int) juce::jmin ((juce::int64) 65536, r->lengthInSamples - pos);
                r->read (&buf, 0, m, pos, true, true);
                for (int c = 0; c < 2; ++c) { const auto mm = buf.findMinMax (c, 0, m); peaks[(size_t) i] = juce::jmax (peaks[(size_t) i], std::abs (mm.getStart()), std::abs (mm.getEnd())); }
            }
            proc.push_back (src); R.frames[(size_t) i] = r->lengthInSamples;
        }
        else
        {
            const auto outFile = spec.workFolder.getChildFile ("rs-" + juce::String (i + 1).paddedLeft ('0', 2) + ".wav");
            auto w = audioops::makeWavWriter (outFile, T, 2, true);
            if (w == nullptr) { R.error = "Cannot write " + outFile.getFullPathName(); return R; }
            SincResampler rl (native, T), rr (native, T);
            std::vector<float> ol, orr; juce::int64 written = 0; bool writeFailed = false;
            auto flush = [&] ()
            {
                const size_t m = juce::jmin (ol.size(), orr.size());
                if (m == 0) return;
                for (auto* v : { &ol, &orr }) for (size_t k = 0; k < m; ++k) peaks[(size_t) i] = juce::jmax (peaks[(size_t) i], std::abs ((*v)[k]));
                const float* ptrs[2] = { ol.data(), orr.data() };
                if (! w->writeFromFloatArrays (ptrs, 2, (int) m)) writeFailed = true;      // (3.1.3: a refused write is an error, not a shorter file)
                written += (juce::int64) m;
                ol.erase (ol.begin(), ol.begin() + (long) m); orr.erase (orr.begin(), orr.begin() + (long) m);
            };
            for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += 65536)
            {
                if (cancelFlag.load()) { R.cancelled = true; return R; }
                const int m = (int) juce::jmin ((juce::int64) 65536, r->lengthInSamples - pos);
                r->read (&buf, 0, m, pos, true, true);
                rl.process (buf.getReadPointer (0), (size_t) m, ol); rr.process (buf.getReadPointer (1), (size_t) m, orr);
                flush();
                sub = (float) (((double) i + (double) (pos + m) / (double) r->lengthInSamples) / n);
            }
            rl.finish (ol); rr.finish (orr); flush();
            w.reset();
            if (writeFailed) { R.error = "Could not write " + outFile.getFileName() + ": the drive did not take it (is it full?)"; return R; }
            if (const auto e = audioops::checkWrittenLength (&fm, outFile, written); e.isNotEmpty()) { R.error = e; return R; }
            proc.push_back (outFile); R.frames[(size_t) i] = written;
        }
        for (int st = 0; st < nStems; ++st)                               // the stems at the same rate (only the master's peaks decide the level)
        {
            if (std::abs (T - native) < 0.5) continue;
            setStatus ("Converting the stems of " + juce::String (i + 1) + " of " + juce::String (n) + "...");
            const auto outFile = spec.workFolder.getChildFile ("rs-" + juce::String (i + 1) + "-stem-" + juce::String (st + 1) + ".wav");
            const auto e = resampleFile (fm, stemProc[(size_t) i][(size_t) st], T, outFile, cancelFlag);
            if (e == "Cancelled.") { R.cancelled = true; return R; }
            if (e.isNotEmpty()) { R.error = e; return R; }
            stemProc[(size_t) i][(size_t) st].deleteFile();
            stemProc[(size_t) i][(size_t) st] = outFile;
        }
        sub = (float) (i + 1) / (float) n;
    }

    // ---- 2b. a loudness target: every piece measured (integrated loudness, true peak) and, per album, the album as one programme; then the check
    const auto& o = spec.out;
    const bool byLoudness = o.normalise && o.loudnessMode != 0;
    std::vector<double> loudGains;
    if (byLoudness)
    {
        std::vector<double> lufs ((size_t) n, -100.0), tps ((size_t) n, -100.0); std::vector<juce::String> names;
        bool oneRate = true; for (int i = 1; i < n; ++i) oneRate = oneRate && std::abs (rates[(size_t) i] - rates[0]) < 0.5;
        LoudnessMeter album; if (n > 0) album.prepare (rates[0], 2);
        double wsum = 0.0, wpow = 0.0;                                   // (pieces at different rates: the album as the length-weighted mean of their loudness)
        for (int i = 0; i < n; ++i)
        {
            if (cancelFlag.load()) { R.cancelled = true; return R; }
            setStatus ("Measuring the loudness of " + juce::String (i + 1) + " of " + juce::String (n) + "...");
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (proc[(size_t) i]));
            if (r == nullptr) { R.error = "Cannot read " + proc[(size_t) i].getFileName(); return R; }
            LoudnessMeter one; one.prepare (rates[(size_t) i], 2);
            juce::AudioBuffer<float> buf (2, 65536);
            for (juce::int64 pos = 0; pos < r->lengthInSamples; pos += 65536)
            {
                if (cancelFlag.load()) { R.cancelled = true; return R; }
                const int m = (int) juce::jmin ((juce::int64) 65536, r->lengthInSamples - pos);
                r->read (&buf, 0, m, pos, true, true);
                juce::AudioBuffer<float> v (buf.getArrayOfWritePointers(), 2, m);
                one.process (v); if (oneRate) album.process (v);
                sub = (float) (((double) i + (double) (pos + m) / (double) juce::jmax ((juce::int64) 1, r->lengthInSamples)) / n);
            }
            lufs[(size_t) i] = one.integratedLufs(); tps[(size_t) i] = one.truePeakDb();
            if (lufs[(size_t) i] > -99.0) { const double w = (double) r->lengthInSamples; wsum += w; wpow += w * std::pow (10.0, (lufs[(size_t) i] + 0.691) / 10.0); }
            names.push_back (spec.items[(size_t) i].baseName);
        }
        const double albumLufs = oneRate ? album.integratedLufs() : (wsum > 0.0 ? -0.691 + 10.0 * std::log10 (wpow / wsum) : -100.0);
        auto check = planLoudness (names, lufs, tps, albumLufs, (double) o.wantedLufs(), (double) o.wantedCeiling(), o.peakTogether);
        int decision = spec.loudnessDecision;
        if (check.problems() > 0 && decision == 0)
        {
            if (onLoudnessCheck)                                         // the job waits here, nothing written, while the person decides
            {
                setStatus ("Waiting: " + juce::String (check.problems()) + " piece(s) cannot reach the target under the ceiling - see the window.");
                decided.reset(); waiting.store (true);
                juce::MessageManager::callAsync ([cb = onLoudnessCheck, check] { cb (check); });
                decided.wait();
                waiting.store (false);
                decision = decision_.load();
            }
            else decision = 1;
            if (decision == 3 || cancelFlag.load()) { R.cancelled = true; R.loudness = check; return R; }
        }
        loudGains = loudnessGains (check, decision);
        R.loudness = check;
        R.report = check.text();
        if (check.problems() > 0) R.report += decision == 1 ? "\nRetain peak: those pieces stop at the ceiling, below the target by the amount shown." : "\nIgnore: those pieces got the full gain; their peaks are clipped.";
    }

    // ---- 3. levels, dither, encoding, tags
    stage = 2; sub = 0.0f;
    float loudest = 0.0f; for (auto pk : peaks) loudest = juce::jmax (loudest, pk);
    std::vector<juce::File> finals;
    std::vector<reviewpage::Spec> onePage;                   // all the pieces in one listening copy: their audio waits in the work folder until the page is written
    auto dropOnePage = [&onePage] { for (auto& s : onePage) s.audio.deleteFile(); onePage.clear(); };
    float overall = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (cancelFlag.load()) { R.cancelled = true; for (auto& f : finals) if (spec.kind == MasterJobSpec::Kind::Files) f.deleteFile(); return R; }
        float gain = 1.0f;
        if (byLoudness) gain = juce::Decibels::decibelsToGain ((float) loudGains[(size_t) i]);
        else if (o.normalise)
        {
            const float ref = o.peakTogether ? loudest : peaks[(size_t) i];
            if (ref > 1.0e-9f) gain = juce::Decibels::decibelsToGain (o.peakDb) / ref;
        }
        const auto& item = spec.items[(size_t) i];
        std::vector<MasterFormat> fmts;
        if (spec.kind == MasterJobSpec::Kind::Files)
        {
            if (! spec.formsOnly)                            // (listening copies on their own: no audio files at all)
            {
                fmts.push_back (o.format);
                for (int f = 0; f < 5; ++f) if ((o.extraFormats & (1 << f)) != 0 && (MasterFormat) f != o.format) fmts.push_back ((MasterFormat) f);
            }
        }
        else fmts.push_back (o.format);
        float pk = 0.0f;
        for (auto fmtNow : fmts)
        {
            const auto ext = masterFileExtension (fmtNow);
            const juce::File dest = spec.kind == MasterJobSpec::Kind::Files ? spec.destFolder.getNonexistentChildFile (sanitiseForFile (item.baseName), ext, true)
                                                                             : spec.workFolder.getChildFile (item.baseName + ".wav");
            setStatus ("Writing " + juce::String (i + 1) + " of " + juce::String (n) + ": " + dest.getFileName());
            float pkOne = 0.0f;
            const auto e = finalise (proc[(size_t) i], dest, rates[(size_t) i], gain, item, pkOne, fmtNow);
            if (e == "Cancelled.") { R.cancelled = true; for (auto& f : finals) if (spec.kind == MasterJobSpec::Kind::Files) f.deleteFile(); return R; }
            if (e.isNotEmpty()) { R.error = e; for (auto& f : finals) if (spec.kind == MasterJobSpec::Kind::Files) f.deleteFile(); return R; }
            pk = juce::jmax (pk, pkOne);
            finals.push_back (dest);
            if (spec.kind == MasterJobSpec::Kind::Files) R.files.add (dest.getFullPathName());
        }
        if (spec.kind == MasterJobSpec::Kind::Files && ! spec.formsOnly)
            for (int st = 0; st < nStems && st < (int) spec.stemNames.size(); ++st)
            {   // 3.26.0: the stems - the master's gain (they keep its balance), its main format, in the Stems folder, named after the piece and the bus
                const auto stemsDir = spec.destFolder.getChildFile ("Stems");
                const juce::File dest = stemsDir.getNonexistentChildFile (sanitiseForFile (item.baseName + " - " + spec.stemNames[(size_t) st]), masterFileExtension (o.format), true);
                MasterJobItem si = item;
                si.tags.title = (item.tags.title.isNotEmpty() ? item.tags.title : item.baseName) + " (" + spec.stemNames[(size_t) st] + ")";
                setStatus ("Writing " + juce::String (i + 1) + " of " + juce::String (n) + ": " + dest.getFileName());
                float pkStem = 0.0f;
                const auto e = finalise (stemProc[(size_t) i][(size_t) st], dest, rates[(size_t) i], gain, si, pkStem, o.format);
                if (e == "Cancelled.") { R.cancelled = true; for (auto& f : finals) f.deleteFile(); return R; }
                if (e.isNotEmpty()) { R.error = e; for (auto& f : finals) f.deleteFile(); return R; }
                finals.push_back (dest);
                R.files.add (dest.getFullPathName()); ++R.stemFiles;
                if (pkStem > 1.0f && ! (o.format == MasterFormat::Wav && o.bitDepth == 32))      // (a 32-bit float file keeps what is over 0 dB: nothing is lost)
                    R.clippedStems.add (dest.getFileNameWithoutExtension() + "  +" + juce::String (juce::Decibels::gainToDecibels (pkStem), 1) + " dB");
            }
        if (spec.kind == MasterJobSpec::Kind::Files && o.feedbackForm)
        {
            const auto fmtForm = o.formAudio == 2 ? MasterFormat::Mp3 : o.formAudio == 1 ? MasterFormat::Flac : MasterFormat::Wav;
            const auto audioFile = spec.workFolder.getChildFile ("form-" + juce::String (i + 1).paddedLeft ('0', 2) + masterFileExtension (fmtForm));
            const bool single = spec.formSingle && spec.formsOnly;
            setStatus ("Making the listening copy " + juce::String (i + 1) + " of " + juce::String (n) + "...");
            float pkForm = 0.0f;
            auto e = finalise (proc[(size_t) i], audioFile, rates[(size_t) i], gain, item, pkForm, fmtForm);
            if (spec.formsOnly) pk = juce::jmax (pk, pkForm);
            if (e == "Cancelled.") { R.cancelled = true; for (auto& f : finals) f.deleteFile(); dropOnePage(); return R; }
            if (e.isEmpty())
            {
                reviewpage::Spec ps;
                ps.audio = audioFile; ps.mime = fmtForm == MasterFormat::Mp3 ? "audio/mpeg" : fmtForm == MasterFormat::Flac ? "audio/flac" : "audio/wav";
                ps.piece = item.tags.title.isNotEmpty() ? item.tags.title : item.editName; ps.project = item.projectName; ps.editId = item.editId.toString(); ps.version = item.editVersion; ps.history = item.history;
                ps.editor = spec.editorName;                                   // (3.61.0)
                ps.offsetSeconds = item.editOffsetSeconds; ps.durationSeconds = (double) R.frames[(size_t) i] / juce::jmax (1.0, rates[(size_t) i]);
                ps.peaks = reviewpage::computePeaks (proc[(size_t) i], gain, reviewpage::peakColumnsFor (ps.durationSeconds), &cancelFlag);
                if (single) onePage.push_back (ps);                                   // written once every piece is ready (below); its audio waits till then
                else
                {
                    const auto page = spec.destFolder.getNonexistentChildFile (sanitiseForFile (item.baseName) + " - listening copy", ".html", true);
                    e = reviewpage::write (page, ps, &cancelFlag);
                    if (e.isEmpty()) { R.files.add (page.getFullPathName()); finals.push_back (page); }     // (in the list so that Cancel takes it away again)
                }
            }
            if (! single || e.isNotEmpty()) audioFile.deleteFile();
            if (e == "Cancelled.") { R.cancelled = true; for (auto& f : finals) f.deleteFile(); dropOnePage(); return R; }
            if (e.isNotEmpty()) { R.error = "The listening copy could not be made: " + e; for (auto& f : finals) f.deleteFile(); dropOnePage(); return R; }
        }
        overall = juce::jmax (overall, pk);
        sub = (float) (i + 1) / (float) n * (spec.kind == MasterJobSpec::Kind::Disc ? 0.3f : 1.0f);
    }
    if (! onePage.empty())
    {   // all the pieces in one listening copy
        setStatus ("Writing the listening copy with all " + juce::String ((int) onePage.size()) + " pieces...");
        const auto page = spec.destFolder.getNonexistentChildFile (sanitiseForFile (spec.formTitle) + " - listening copy", ".html", true);
        const auto e = reviewpage::writeMany (page, spec.formTitle, onePage.front().project, onePage, &cancelFlag);
        dropOnePage();
        if (e == "Cancelled.") { R.cancelled = true; for (auto& f : finals) f.deleteFile(); return R; }
        if (e.isNotEmpty()) { R.error = "The listening copy could not be made: " + e; for (auto& f : finals) f.deleteFile(); return R; }
        R.files.add (page.getFullPathName()); finals.push_back (page);
    }
    R.peakDb = juce::Decibels::gainToDecibels (overall, -100.0f);
    R.clipped = overall > 1.0f;

    // ---- 4. the disc: layout from the real lengths, then DDP / zip / WAV + CUE
    if (spec.kind == MasterJobSpec::Kind::Disc)
    {
        setStatus ("Building the DDP...");
        std::vector<juce::int64> frames (spec.disc.clips.size(), 0);
        for (int i = 0; i < n; ++i) frames[(size_t) spec.clipIndex[(size_t) i]] = R.frames[(size_t) i];
        const auto pq = computePq (spec.disc, frames);
        std::vector<juce::File> files;
        for (auto& t : pq.tracks)
            for (int i = 0; i < n; ++i) if (spec.clipIndex[(size_t) i] == t.clipIndex) files.push_back (finals[(size_t) i]);
        const auto name = sanitiseForFile (spec.discName);
        const auto ddpFolder = spec.destFolder.getChildFile (name + " DDP");
        const auto zip = spec.disc.makeZip ? spec.destFolder.getChildFile (name + " DDP.zip") : juce::File();
        std::atomic<float> inner { 0.0f };
        stage = 2; sub = 0.3f;
        auto e = ddp::writeFileset (ddpFolder, spec.disc, pq, files, zip, &inner, &cancelFlag);
        if (e == "Cancelled.") { R.cancelled = true; return R; }
        if (e.isNotEmpty()) { R.error = e; return R; }
        R.files.add (ddpFolder.getFullPathName()); if (zip != juce::File()) R.files.add (zip.getFullPathName());
        sub = 0.8f;
        juce::String rep;
        const bool consistent = ddp::verifyFileset (ddpFolder, rep); R.report = rep;
        if (! consistent) { R.error = "The DDP was written but did not pass its own check - do not send it:\n" + rep; return R; }      // (3.1.3: a fileset that is not whole is an error, not a note)
        if (spec.disc.makeCue)
        {
            setStatus ("Writing the WAV and CUE...");
            const auto wav = spec.destFolder.getChildFile (name + ".wav"), cue = spec.destFolder.getChildFile (name + ".cue");
            e = ddp::writeWavAndCue (wav, cue, spec.disc, pq, files, &inner, &cancelFlag);
            if (e == "Cancelled.") { R.cancelled = true; return R; }
            if (e.isNotEmpty()) { R.error = e; return R; }
            R.files.add (wav.getFullPathName()); R.files.add (cue.getFullPathName());
        }
        sub = 1.0f;
    }
    return R;
}
} // namespace td

// ----------------------------------------------------------------------------- 3.40.0: the Atmos export
#include "AdmWriter.h"

namespace td
{
namespace
{
    juce::String atmosBaseName (const MasterJobItem& j) { return j.baseName + " (Atmos)"; }
}

juce::String makeAtmosSpec (const Project& p, const MasteringDef& def, AtmosJobSpec& spec)
{
    spec = AtmosJobSpec();
    MasterJobSpec files;
    {   // the pieces, their numbers and names, the render source, exactly as the Virtual Master's - with the Atmos tab's own folder, and none of the
        // Virtual Master's file-format conditions (an MP3 setting without LAME, say, is no reason to refuse an Atmos file)
        auto d = def;
        d.vm.folder = def.atmos.folder.trim().isNotEmpty() ? def.atmos.folder.trim() : p.masteredFolder().getChildFile ("Atmos").getFullPathName();
        d.vm.format = MasterFormat::Wav; d.vm.extraFormats = 0; d.stemIds.clear();
        // 3.46.0: and without a stereo output to render from - the file holds the ticked surround buses, and a project with no stereo bus at all
        // (surround only) was refused with "There is nothing to render yet: add a stereo Ext bus"
        if (const auto err = makeFilesSpec (p, d, files, false, false); err.isNotEmpty()) return err;
    }
    spec.items = files.items; spec.mixerId = files.mixerId; spec.sourceId = files.sourceId; spec.ownMixers = files.ownMixers; spec.tailSeconds = files.tailSeconds;
    for (auto& b : p.buses)                                               // the ticked buses in the settings' order, gone ones and stereo ones left out
        if (surround::isSurround (b.format) && std::find (def.atmos.busIds.begin(), def.atmos.busIds.end(), b.id) != def.atmos.busIds.end())
        {
            spec.busIds.push_back (b.id); spec.busNames.push_back (b.name.trim().isNotEmpty() ? b.name.trim() : surround::name (b.format)); spec.busFormats.push_back (b.format);
        }
    if (spec.busIds.empty()) return "Tick at least one surround bus to write into the Atmos files.";
    {
        adm::Description d; d.sampleRate = 48000.0; d.frames = 1;
        for (size_t i = 0; i < spec.busIds.size(); ++i) d.beds.push_back ({ spec.busNames[i], spec.busFormats[i] });
        if (const auto e = adm::check (d); e.isNotEmpty()) return e;
    }
    {
        auto* e0 = const_cast<Project&> (p).findEdit (spec.items.front().editId);
        const double r = e0 != nullptr ? e0->sampleRate : 0.0;
        if (std::abs (r - 48000.0) > 0.5 && std::abs (r - 96000.0) > 0.5)
            return "A Dolby Atmos master is 48 or 96 kHz only; these pieces are at " + juce::String (r / 1000.0, 1) + " kHz. (Apple and the delivery spec want 48 kHz.)";
    }
    spec.lfeLowPass = def.atmos.lfeLowPass; spec.lfeHz = juce::jlimit (AtmosExportSettings::kMinLfeHz, AtmosExportSettings::kMaxLfeHz, def.atmos.lfeHz);
    spec.dither = def.atmos.dither;
    spec.destFolder = def.atmos.folder.trim().isNotEmpty() ? juce::File (def.atmos.folder.trim()) : p.masteredFolder().getChildFile ("Atmos");
    spec.workFolder = spec.destFolder.getChildFile ("_fermata_work");
    return {};
}

juce::StringArray atmosExportNotes (const Project& p, const MasteringDef& def)
{
    juce::StringArray notes;
    std::vector<const BusDef*> ticked;
    for (auto& b : p.buses) if (surround::isSurround (b.format) && std::find (def.atmos.busIds.begin(), def.atmos.busIds.end(), b.id) != def.atmos.busIds.end()) ticked.push_back (&b);
    if (ticked.empty()) return notes;
    auto nameOf = [&p] (const juce::Uuid& id) { for (auto& b : p.buses) if (b.id == id) return b.name; return juce::String ("a bus"); };
    // the mixers the pieces go through: each included edit's own, or the one chosen
    std::vector<MixerState*> mixers;                                      // (busFor is not const; nothing here changes a mixer)
    if (! def.mixerId.isNull()) { if (auto* m = p.mixerById (def.mixerId)) mixers.push_back (m); }
    else for (auto& it : def.items) if (it.include) if (auto* m = p.mixerOfEdit (it.editId)) if (std::find (mixers.begin(), mixers.end(), m) == mixers.end()) mixers.push_back (m);
    if (mixers.empty() && ! p.mixers.empty()) mixers.push_back (p.mixers.front().get());
    std::set<juce::String> said;
    for (auto* m : mixers)
        for (auto* b : ticked)
        {
            auto* bs = m->busFor (b->id);
            if (bs == nullptr) continue;
            // this ticked bus into another ticked bus: twice in the file
            auto isTicked = [&ticked] (const juce::Uuid& id) { for (auto* t : ticked) if (t->id == id) return true; return false; };
            if (bs->outOn.get() && ! bs->outDest.isNull() && isTicked (bs->outDest))
                if (said.insert ("twice" + b->id.toString()).second)
                    notes.add (b->name + " goes into " + nameOf (bs->outDest) + ", which is ticked too: its sound would be in the file twice.");
            for (auto& sd : bs->sends.pool)
                if (sd->active() && isTicked (sd->dest) && said.insert ("twice" + b->id.toString()).second)
                    notes.add (b->name + " is sent to " + nameOf (sd->dest) + ", which is ticked too: its sound would be in the file twice.");
            // where it goes (not in the file): that bus's fader and plug-ins
            if (bs->outOn.get() && ! bs->outDest.isNull() && ! isTicked (bs->outDest))
                if (auto* dest = m->busFor (bs->outDest))
                {
                    bool plugs = false;
                    for (int i = 0; i < kNumSlots; ++i) plugs = plugs || dest->slots[i].mightBeLoaded();
                    if (p.postFaderSlots) plugs = plugs || dest->slots[kPostSlot].mightBeLoaded();
                    if ((std::abs (dest->gainDb.get()) > 0.05f || plugs) && said.insert ("dest" + bs->outDest.toString()).second)
                        notes.add ("The ticked buses go into " + nameOf (bs->outDest) + ", whose " + juce::String (plugs ? (std::abs (dest->gainDb.get()) > 0.05f ? "fader and plug-ins are" : "plug-ins are") : "fader is")
                                   + " not in the file (the file holds the ticked buses as they leave their own faders).");
                }
        }
    if (p.hasAtmosComposer())
    {
        bool spacelab = false;
        for (auto& m : p.mixers)
            for (auto& bs : m->busPool)
                for (int i = 0; i < kChanSlots; ++i)
                    if (const auto* pr = bs->slots[i].getProcessorUnsafe(); pr != nullptr && Project::atmosLinkOf (pr->getDescription()) == Project::AtmosLink::Spacelab) spacelab = true;
        if (spacelab) notes.add ("Spacelab is left out of the render while a Dolby Atmos Composer is in the project (a copy would join the live Atmos mix): bounce without the Composer, or use another reverb for the file.");
    }
    return notes;
}

AtmosExportJob::AtmosExportJob (const Project& live, const AtmosJobSpec& s, std::function<void (AtmosExportResult)> done, bool startNow)
    : juce::Thread ("Atmos export"), spec (s), onDone (std::move (done))
{
    std::vector<juce::Uuid> ids;
    for (auto& it : spec.items)
    {
        auto* e = const_cast<Project&> (live).findEdit (it.editId);
        if (e == nullptr || e->isEmpty()) { earlyError = "An edit no longer exists or is empty."; break; }
        ids.push_back (e->id);
    }
    if (earlyError.isEmpty())
    {
        RenderParams rp; rp.sourceId = spec.sourceId; rp.mixerId = spec.mixerId; rp.ownMixers = spec.ownMixers; rp.tailSeconds = spec.tailSeconds;
        // (3.46.0, the last 'true': the surround buses only - the stereo master render is neither needed nor made here)
        batch = std::make_unique<MasterRenderBatch> (live, ids, rp, false, spec.busIds, spec.workFolder.getChildFile ("atmos"), true, true);
        if (batch->getPrepareError().isNotEmpty()) earlyError = batch->getPrepareError();
    }
    if (startNow) startThread (juce::Thread::Priority::normal);
}

AtmosExportJob::~AtmosExportJob() { cancelFlag = true; stopThread (15000); }
float AtmosExportJob::getProgress() const { const float p = sub.load(); return stage.load() == 0 ? 0.7f * p : 0.7f + 0.3f * p; }
juce::String AtmosExportJob::getStatus() const { const juce::ScopedLock sl (lock); return status; }

void AtmosExportJob::run()
{
    td::crashreport::nameThisThread (getThreadName());
    auto result = execute();
    auto fn = onDone;
    juce::MessageManager::callAsync ([fn, result] { if (fn) fn (result); });
}

AtmosExportResult AtmosExportJob::execute()
{
    juce::ScopedNoDenormals noDenormals;
    AtmosExportResult R;
    auto setStatus = [this] (const juce::String& t) { const juce::ScopedLock sl (lock); status = t; };
    if (earlyError.isNotEmpty()) { R.error = earlyError; return R; }
    if (! spec.destFolder.createDirectory()) { R.error = "Cannot create the folder " + spec.destFolder.getFullPathName(); return R; }
    spec.workFolder.createDirectory();
    struct Tidy { juce::File f; ~Tidy() { f.deleteRecursively(); } } tidy { spec.workFolder };
    stage = 0; setStatus ("Rendering the buses...");
    {
        const auto err = batch->run (&sub, &cancelFlag);
        if (err == "Cancelled.") { R.cancelled = true; return R; }
        if (err.isNotEmpty()) { R.error = err; return R; }
    }
    stage = 1;
    juce::AudioFormatManager fm; td::registerAudioFormats (fm);
    // the file's layout: the ticked buses, each a bed + objects; the LFE tracks get the low-pass
    adm::Description d;
    for (size_t b = 0; b < spec.busIds.size(); ++b) d.beds.push_back ({ spec.busNames[b], spec.busFormats[b] });
    std::vector<adm::Layout> layouts;
    for (auto& bed : d.beds) layouts.push_back (adm::layoutFor (bed.format));
    const int channels = d.totalTracks();
    R.channels = channels;
    const int total = batch->total();
    for (int i = 0; i < total; ++i)
    {
        if (cancelFlag) { R.cancelled = true; break; }
        const auto& item = spec.items[(size_t) i];
        setStatus ("Writing " + atmosBaseName (item) + "...");
        std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;
        juce::int64 frames = -1; double rate = batch->rateFor (i);
        for (int s = 0; s < batch->numStems(); ++s)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (batch->stemFileFor (i, s)));
            if (r == nullptr) { R.error = "Cannot read the render of " + spec.busNames[(size_t) s] + " for " + item.baseName; return R; }
            if ((int) r->numChannels != surround::channels (spec.busFormats[(size_t) s])) { R.error = "The render of " + spec.busNames[(size_t) s] + " has " + juce::String ((int) r->numChannels) + " channels, not " + juce::String (surround::channels (spec.busFormats[(size_t) s])) + "."; return R; }
            if (frames < 0) frames = r->lengthInSamples; else frames = juce::jmin (frames, r->lengthInSamples);
            if (r->sampleRate > 0.0) rate = r->sampleRate;
            readers.push_back (std::move (r));
        }
        if ((int) readers.size() != (int) spec.busIds.size() || frames <= 0) { R.error = "A render is missing for " + item.baseName + "."; return R; }
        d.frames = frames; d.sampleRate = rate; d.bitDepth = 24;
        d.programmeName = item.baseName;
        if (const auto e = adm::check (d); e.isNotEmpty()) { R.error = e; return R; }
        const auto dest = spec.destFolder.getChildFile (juce::File::createLegalFileName (atmosBaseName (item)) + ".wav");
        adm::FileWriter w (dest, d, spec.dither, rf64At);
        if (! w.ok()) { R.error = w.getError(); return R; }
        // the track map: file track -> (bus, bus channel); the LFE tracks' filters
        std::vector<std::pair<int, int>> map;
        std::vector<int> lfeTracks;
        for (size_t b = 0; b < layouts.size(); ++b)
            for (auto& t : layouts[b].tracks)
            {
                if (t.spk == surround::Spk::LFE && spec.lfeLowPass) lfeTracks.push_back ((int) map.size());
                map.push_back ({ (int) b, t.busChannel });
            }
        std::vector<adm::LfeLowPass> lfe (lfeTracks.size());
        for (auto& f : lfe) f.design (spec.lfeHz, rate);
        const int block = 8192;
        std::vector<juce::AudioBuffer<float>> src;
        for (auto& r : readers) src.emplace_back ((int) r->numChannels, block);
        juce::AudioBuffer<float> out (channels, block);
        std::vector<const float*> ptrs ((size_t) channels);
        float peak = 0.0f;
        for (juce::int64 pos = 0; pos < frames; pos += block)
        {
            if (cancelFlag) { R.cancelled = true; return R; }
            const int n = (int) juce::jmin ((juce::int64) block, frames - pos);
            for (size_t s = 0; s < readers.size(); ++s)
                if (! readers[s]->read (&src[s], 0, n, pos, true, true)) { R.error = "Cannot read the render of " + spec.busNames[s] + "."; return R; }
            for (int c = 0; c < channels; ++c) out.copyFrom (c, 0, src[(size_t) map[(size_t) c].first], map[(size_t) c].second, 0, n);
            for (size_t k = 0; k < lfeTracks.size(); ++k) lfe[k].process (out.getWritePointer (lfeTracks[k]), n);
            for (int c = 0; c < channels; ++c) { ptrs[(size_t) c] = out.getReadPointer (c); peak = juce::jmax (peak, out.getMagnitude (c, 0, n)); }
            if (! w.write (ptrs.data(), n)) { R.error = w.getError(); return R; }
            sub = ((float) i + (float) (pos + n) / (float) frames) / (float) juce::jmax (1, total);
        }
        if (! w.finish()) { R.error = w.getError(); return R; }
        R.files.add (dest.getFullPathName()); R.frames.push_back (frames); R.sampleRate = rate;
        R.peakDb = juce::jmax (R.peakDb, peak > 0.0f ? juce::Decibels::gainToDecibels (peak) : -100.0f);
        if (peak >= 1.0f) R.clipped = true;
    }
    sub = 1.0f;
    return R;
}
} // namespace td
