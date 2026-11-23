#pragma once
// 3.23.0: a stereo track made from two mono tracks (the take window: select the two track names, right-click > Make a stereo track ...). Fermata's files
// are one per track per take, so the pair becomes one new stereo WAV per take (an exact copy of the samples; the mono files stay on the disk), and every
// take and every piece of an edit that used the two tracks uses the stereo track instead. The two mono tracks then leave the project.
// plan() works out what to write (message thread), writeFiles() writes it (a job thread), apply() makes the change (message thread). Ctrl+Z cannot step
// back past it (the tracks are gone), so apply() starts the undo history again.
// 3.46.1: the frozen versions of an edit (Edit 1, Edit 2 ...: EditDef::versions, 3.42.0) are converted with it - their pieces, their lists of tracks,
// their automation and the mixer settings kept with each - exactly as the edit itself is. (They were left on the two tracks, which are gone.)
#include "Project.h"
#include "AudioOps.h"
#include "Automation.h"
#include <limits>
#include <map>
#include <set>

namespace td { namespace stereojoin
{
/** One stereo file to write: the take range [from, to) from a left and a right mono file (each with the take sample its own sample 0 stands for; an empty
    File = silence on that side). A whole take: wantTo < 0, and it runs to the end of the longer file. A piece of an edit: [wantFrom, wantTo) is what the
    piece may need (itself, its fades and 2 s of handle each side), cut by the job to where either file has audio. */
struct FileJob
{
    juce::File left, right;
    juce::int64 leftStart = 0, rightStart = 0;
    juce::int64 wantFrom = 0, wantTo = -1;
    juce::File dest;
    juce::int64 from = 0, to = 0;                 // what was written (set by writeFiles)
};

/** A take whose two mono files become one stereo file. */
struct TakeUse { juce::Uuid window, take; juce::File left, right; size_t job = 0; };
/** A piece of an edit (or an overdub) that played the two tracks. 'version': 0 = the edit as it is now, else the number of the frozen version
    of that edit the piece is in (a frozen version's pieces keep the ids they had, so the edit and the piece alone do not say which). */
struct PieceUse
{
    juce::Uuid edit, piece;
    int version = 0;
    bool hasLeft = false, hasRight = false;
    juce::File left, right; juce::int64 leftStart = 0, rightStart = 0;
    size_t job = 0;
};

struct Plan
{
    juce::Uuid left, right, stereo;               // the two mono tracks and the new stereo track (its id is chosen here)
    juce::String name;
    std::vector<FileJob> jobs;
    std::vector<TakeUse> takes;
    std::vector<PieceUse> pieces;
    std::vector<juce::Uuid> takeWindows, edits;   // the windows it changes
};

inline const TrackDef* trackOf (const Project& p, const juce::Uuid& id)
{
    for (auto& t : p.tracks) if (t.id == id) return &t;
    return nullptr;
}

/** A name for the stereo track: "Omni" from "Omni L" + "Omni R" (or "Omni Left" / "Omni-R" ...), otherwise both names ("Tr01 + Tr02"). */
inline juce::String suggestName (const juce::String& a, const juce::String& b)
{
    auto stripSide = [] (const juce::String& n, bool& had) -> juce::String
    {
        had = false;
        const auto t = n.trim();
        for (auto* suffix : { "Left", "Right", "L", "R" })
        {
            const juce::String s (suffix);
            if (t.length() > s.length() && t.endsWithIgnoreCase (s))
            {
                const auto rest = t.dropLastCharacters (s.length());
                const auto last = rest.getLastCharacter(), first = t[t.length() - s.length()];
                const bool apart = last == ' ' || last == '-' || last == '_' || last == '.';
                const bool camel = juce::CharacterFunctions::isLowerCase (last) && juce::CharacterFunctions::isUpperCase (first);       // "OmniL", "OmniLeft"
                if (apart || camel)
                {
                    had = true;
                    return rest.trimCharactersAtEnd (" -_.").trim();
                }
            }
        }
        return t;
    };
    bool ha = false, hb = false;
    const auto ra = stripSide (a, ha), rb = stripSide (b, hb);
    if (ha && hb && ra.isNotEmpty() && ra.equalsIgnoreCase (rb)) return ra;
    return a.trim() + " + " + b.trim();
}

/** Does this waiting part (Export for Processing / Edit in RX) involve one of the two tracks? ('files': the tracks of the take / piece it is in.) */
template <typename Files>
inline bool waitingTouches (const WaitingPiece& w, const Files& files, const juce::Uuid& a, const juce::Uuid& b)
{
    if (! w.active()) return false;
    if (w.tracks.empty()) { for (auto& f : files) if (f.trackId == a || f.trackId == b) return true; return false; }
    for (auto& t : w.tracks) if (t == a || t == b) return true;
    return false;
}

/** The edit a piece of the plan is in: the edit itself, or one of its frozen versions (null when it has gone). */
inline EditDef* editOfUse (Project& p, const PieceUse& u)
{
    auto* e = p.findEdit (u.edit);
    if (e == nullptr || u.version == 0) return e;
    auto* v = e->version (u.version);
    return v != nullptr ? v->edit.get() : nullptr;
}

/** A frozen version's mixer settings (Project::mixSnapshot) as they read once the two tracks are one: the stereo track's strip where the left
    track's was, with what a live mixer's new strip starts with - the left track's fader, input gain, output, VCAs and sends (no plug-ins, the
    pans at their start) - and the right track's strip gone. The snapshot itself is not touched: a changed copy comes back (the same one when it
    names neither track). */
inline juce::var joinedMix (const juce::var& snap, const juce::Uuid& left, const juce::Uuid& right, const juce::Uuid& stereo)
{
    const auto* strips = snap["strips"].getArray();
    if (strips == nullptr) return snap;
    const auto l = left.toString(), r = right.toString();
    bool any = false;
    for (auto& s : *strips) { const auto t = s["track"].toString(); any = any || t == l || t == r; }
    if (! any) return snap;
    juce::var root = snap.clone();
    juce::Array<juce::var> next;
    for (auto& s : *root["strips"].getArray())
    {
        const auto t = s["track"].toString();
        if (t == r) continue;
        if (t != l) { next.add (s); continue; }
        auto* o = new juce::DynamicObject(); juce::var so (o);
        o->setProperty ("track", stereo.toString());
        for (auto* k : { "gainDb", "inGainDb", "outOn", "outDest", "vcaMask" }) if (s.hasProperty (k)) o->setProperty (k, s[k]);
        o->setProperty ("pan", 0.0); o->setProperty ("panL", -1.0); o->setProperty ("panR", 1.0);
        o->setProperty ("mute", false); o->setProperty ("solo", false);
        juce::Array<juce::var> sends;
        if (const auto* sl = s["sendList"].getArray())
            for (auto& sd : *sl)
            {
                const auto d = sd["dest"].toString();
                if (d == l || d == r) continue;
                auto* x = new juce::DynamicObject();
                for (auto* k : { "dest", "gainDb", "mutedGainDb", "pre", "followMute", "mute" }) if (sd.hasProperty (k)) x->setProperty (k, sd[k]);
                sends.add (juce::var (x));
            }
        o->setProperty ("sendList", sends);
        next.add (so);
    }
    if (auto* d = root.getDynamicObject()) d->setProperty ("strips", next);
    return root;
}

/** Works out the change. "" when it can be done (with 'out' filled in), else the reason it cannot, in plain words. */
inline juce::String plan (const Project& p, const juce::Uuid& leftId, const juce::Uuid& rightId, const juce::String& newName, Plan& out)
{
    out = Plan();
    const auto* L = trackOf (p, leftId); const auto* R = trackOf (p, rightId);
    if (L == nullptr || R == nullptr || leftId == rightId) return "Choose two different tracks.";
    if (L->isLtc || R->isLtc) return "The LTC (timecode) track cannot be part of a stereo track.";
    if (L->format != TrackFormat::Mono || R->format != TrackFormat::Mono) return "Both tracks must be mono: " + (L->format != TrackFormat::Mono ? L->name : R->name) + " is not.";
    const auto name = newName.trim();
    if (name.isEmpty()) return "Give the stereo track a name.";
    out.left = leftId; out.right = rightId; out.name = name; out.stereo = juce::Uuid();

    // nothing on the two tracks may be out being processed (its corrected files would come back to a track that is gone)
    for (auto& w : p.takeWindows)
        for (auto& g : w->groups)
            for (auto& wp : g.waiting)
                if (waitingTouches (wp, g.files, leftId, rightId))
                    return "Take " + w->displayName (g) + " in the take window '" + w->name + "' has a part waiting for corrected audio on these tracks (Export for Processing). "
                           "Re-link it, or stop waiting (right-click the block), first.";
    for (auto& e : p.edits)
        for (auto* list : { &e->regions, &e->overdubs })
            for (auto& r : *list)
                if (waitingTouches (r.waiting, r.files, leftId, rightId))
                    return "The edit '" + e->name + "' has a part on these tracks that is " + juce::String (r.waiting.rx ? "out in iZotope RX" : "waiting for corrected audio")
                           + " (" + r.waiting.name + "). Bring it back, or cancel it (right-click the block), first.";

    std::map<juce::String, size_t> wholeTake;                         // "left|right" -> the job of that pair of whole take files
    auto key = [] (const juce::File& a, const juce::File& b) { return a.getFullPathName() + "|" + b.getFullPathName(); };
    for (auto& w : p.takeWindows)
    {
        bool touched = false;
        for (auto& g : w->groups)
        {
            const TakeFile* fl = nullptr; const TakeFile* fr = nullptr;
            for (auto& f : g.files) { if (f.trackId == leftId) fl = &f; else if (f.trackId == rightId) fr = &f; }
            if (fl == nullptr && fr == nullptr) continue;
            if ((fl != nullptr && fl->numChannels != 1) || (fr != nullptr && fr->numChannels != 1))
                return "Take " + w->displayName (g) + " in '" + w->name + "' has a file on " + (fl != nullptr && fl->numChannels != 1 ? L->name : R->name) + " that is not mono.";
            FileJob j;
            if (fl != nullptr) j.left = fl->file;
            if (fr != nullptr) j.right = fr->file;
            j.dest = audioops::uniqueFile (p.takeFolder (*w).getChildFile (TakeWindowDef::fileNameFor (g.number, w->labelFor (g), name)));
            const auto k = key (j.left, j.right);
            size_t idx;
            if (auto it = wholeTake.find (k); it != wholeTake.end()) idx = it->second;      // (the same two files in two takes: written once)
            else { idx = out.jobs.size(); out.jobs.push_back (j); wholeTake[k] = idx; }
            out.takes.push_back ({ w->id, g.id, j.left, j.right, idx });
            touched = true;
        }
        if (touched) out.takeWindows.push_back (w->id);
    }
    const auto fixes = p.audioFolder().getChildFile ("Fixes");
    // 3.46.1: the pieces of the frozen versions too. A version's piece is nearly always a piece the edit still has, or one an earlier version has:
    // it then plays the SAME new file (the one written for that piece, made longer if this one needs more of it), so the changes lane goes on
    // seeing the two as the same audio. 'pieceJobs': the files planned for single pieces so far, by what they are made from.
    struct PieceJob { juce::String from; size_t job = 0; juce::Uuid edit, piece; };
    std::vector<PieceJob> pieceJobs;
    for (auto& e : p.edits)
    {
        bool touched = false;
        auto planEdit = [&] (const EditDef& ed, int version)
        {
            for (auto* list : { &ed.regions, &ed.overdubs })
                for (auto& r : *list)
                {
                    const RegionFile* fl = nullptr; const RegionFile* fr = nullptr;
                    for (auto& f : r.files) { if (f.trackId == leftId) fl = &f; else if (f.trackId == rightId) fr = &f; }
                    if (fl == nullptr && fr == nullptr) continue;
                    PieceUse u;
                    u.edit = e->id; u.piece = r.id; u.version = version;
                    u.hasLeft = fl != nullptr; u.hasRight = fr != nullptr;
                    if (fl != nullptr) { u.left = fl->file; u.leftStart = fl->fileStart; }
                    if (fr != nullptr) { u.right = fr->file; u.rightStart = fr->fileStart; }
                    // the whole take files of a take being joined: the piece plays the take's new stereo file, from the same place
                    const auto it = (u.leftStart == 0 && u.rightStart == 0) ? wholeTake.find (key (u.left, u.right)) : wholeTake.end();
                    if (it != wholeTake.end()) u.job = it->second;
                    else
                    {
                        // anything else (a corrected or repaired piece, a piece with only one of the two): a stereo file of its own, as long as the piece may need
                        const double rate = r.sampleRate > 0 ? r.sampleRate : 48000.0;
                        const auto h = (juce::int64) std::llround (2.0 * rate);
                        const auto wantFrom = r.srcIn + juce::jmin ((juce::int64) 0, (juce::int64) std::llround (r.inStart * rate)) - h;
                        const auto wantTo = r.srcOut + juce::jmax ((juce::int64) 0, (juce::int64) std::llround (r.outEnd * rate)) + h;
                        const auto from = key (u.left, u.right) + "|" + juce::String (u.leftStart) + "|" + juce::String (u.rightStart);
                        const PieceJob* same = nullptr;
                        if (version != 0)                                  // (the edit's own pieces: a file each, as ever)
                        {
                            for (auto& pj : pieceJobs) if (pj.from == from && pj.edit == e->id && pj.piece == r.id) { same = &pj; break; }      // the same piece
                            if (same == nullptr) for (auto& pj : pieceJobs) if (pj.from == from) { same = &pj; break; }                        // the same audio
                        }
                        if (same != nullptr)
                        {
                            auto& j = out.jobs[same->job];
                            j.wantFrom = juce::jmin (j.wantFrom, wantFrom); j.wantTo = juce::jmax (j.wantTo, wantTo);
                            u.job = same->job;
                        }
                        else
                        {
                            FileJob j;
                            j.left = u.left; j.right = u.right; j.leftStart = u.leftStart; j.rightStart = u.rightStart;
                            j.wantFrom = wantFrom; j.wantTo = wantTo;
                            j.dest = audioops::uniqueFile (fixes.getChildFile (sanitiseForFile (r.takeName.upToFirstOccurrenceOf (" (", false, false) + " - " + name) + " (stereo).wav"));
                            u.job = out.jobs.size(); out.jobs.push_back (j);
                            pieceJobs.push_back ({ from, u.job, e->id, r.id });
                        }
                    }
                    out.pieces.push_back (u);
                    touched = true;
                }
        };
        planEdit (*e, 0);
        for (auto& v : e->versions) if (v.edit != nullptr) planEdit (*v.edit, v.number);
        if (touched) out.edits.push_back (e->id);
    }
    if (out.takes.empty() && out.pieces.empty()) return "There is no audio on " + L->name + " or " + R->name + " in any take window or edit.";
    return {};
}

/** Writes these files (any thread). "" when all went well; otherwise what went wrong, and every file it had made is deleted again. */
inline juce::String writeJobs (juce::AudioFormatManager& fm, std::vector<FileJob>& jobs, AudioJob* job = nullptr)
{
    std::vector<juce::File> made;
    auto fail = [&] (const juce::String& e) { for (auto& f : made) f.deleteFile(); return e; };
    for (size_t i = 0; i < jobs.size(); ++i)
    {
        if (job != nullptr) { if (job->cancelled()) return fail ("Cancelled."); job->setProgress ((float) i / (float) jobs.size()); }
        auto& j = jobs[i];
        juce::int64 lenL = 0, lenR = 0;
        if (j.left != juce::File())  { auto r = audioops::openReader (fm, j.left);  if (r == nullptr) return fail ("Could not read " + j.left.getFullPathName() + ".");  lenL = r->lengthInSamples; }
        if (j.right != juce::File()) { auto r = audioops::openReader (fm, j.right); if (r == nullptr) return fail ("Could not read " + j.right.getFullPathName() + "."); lenR = r->lengthInSamples; }
        if (j.wantTo < 0) { j.from = 0; j.to = juce::jmax (j.leftStart + lenL, j.rightStart + lenR); }
        else
        {
            juce::int64 lo = std::numeric_limits<juce::int64>::max(), hi = std::numeric_limits<juce::int64>::min();
            auto take = [&] (bool has, juce::int64 start, juce::int64 len)
            {
                if (! has) return;
                const auto a = juce::jmax (start, j.wantFrom), b = juce::jmin (start + len, j.wantTo);
                if (b > a) { lo = juce::jmin (lo, a); hi = juce::jmax (hi, b); }
            };
            take (j.left != juce::File(), j.leftStart, lenL);
            take (j.right != juce::File(), j.rightStart, lenR);
            if (hi <= lo) { lo = juce::jmax ((juce::int64) 0, j.wantFrom); hi = juce::jmax (lo + 1, j.wantTo); }     // (no audio there at all: silence, as it played)
            j.from = lo; j.to = hi;
        }
        if (j.to <= j.from) j.to = j.from + 1;
        juce::String err;
        if (! audioops::joinToStereo (fm, j.left, j.leftStart, j.right, j.rightStart, j.from, j.to, j.dest, err,
                                      [job] { return job == nullptr || ! job->cancelled(); }))
            return fail (err);
        made.push_back (j.dest);
    }
    return {};
}
/** Writes every file of the plan (any thread): see writeJobs. */
inline juce::String writeFiles (juce::AudioFormatManager& fm, Plan& plan, AudioJob* job = nullptr) { return writeJobs (fm, plan.jobs, job); }

/** Makes the change, once the files are written (message thread). False with 'why' - and nothing changed - when the project changed since the plan
    (the caller then deletes the files). */
inline bool apply (Project& p, const Plan& plan, juce::String& why)
{
    // still as planned?
    const auto* L = trackOf (p, plan.left); const auto* R = trackOf (p, plan.right);
    if (L == nullptr || R == nullptr) { why = "One of the two tracks has gone."; return false; }
    for (auto& j : plan.jobs) if (! j.dest.existsAsFile()) { why = "A new file is missing: " + j.dest.getFullPathName(); return false; }
    auto sameTake = [&] (const TakeUse& u, TakeGroup*& g) -> bool
    {
        auto* w = p.findTakeWindow (u.window); g = w != nullptr ? w->findGroup (u.take) : nullptr;
        if (g == nullptr) return false;
        juce::File l, r;
        for (auto& f : g->files) { if (f.trackId == plan.left) l = f.file; else if (f.trackId == plan.right) r = f.file; }
        return l == u.left && r == u.right;
    };
    auto samePiece = [&] (const PieceUse& u, EditRegion*& rg) -> bool
    {
        auto* e = editOfUse (p, u); rg = e != nullptr ? e->findAny (u.piece) : nullptr;
        if (rg == nullptr) return false;
        const RegionFile* fl = nullptr; const RegionFile* fr = nullptr;
        for (auto& f : rg->files) { if (f.trackId == plan.left) fl = &f; else if (f.trackId == plan.right) fr = &f; }
        if ((fl != nullptr) != u.hasLeft || (fr != nullptr) != u.hasRight) return false;
        if (fl != nullptr && (fl->file != u.left || fl->fileStart != u.leftStart)) return false;
        if (fr != nullptr && (fr->file != u.right || fr->fileStart != u.rightStart)) return false;
        if (u.version != 0) return true;                 // (a frozen version waits for nothing: what was out being processed when it was frozen comes back to the edit)
        return ! rg->waiting.active() || ! waitingTouches (rg->waiting, rg->files, plan.left, plan.right);
    };
    for (auto& u : plan.takes)  { TakeGroup* g = nullptr; if (! sameTake (u, g))    { why = "The takes changed while the files were being written."; return false; } }
    for (auto& u : plan.pieces) { EditRegion* r = nullptr; if (! samePiece (u, r)) { why = "An edit changed while the files were being written."; return false; } }
    // 3.27.1: nothing on the two tracks that the plan does not know of (a take recorded while the files were written, a piece placed meanwhile):
    // it would be left on tracks that are about to go
    for (auto& w : p.takeWindows)
        for (auto& g : w->groups)
            for (auto& f : g.files)
                if ((f.trackId == plan.left || f.trackId == plan.right)
                    && std::none_of (plan.takes.begin(), plan.takes.end(), [&] (const TakeUse& u) { return u.window == w->id && u.take == g.id; }))
                { why = "The takes changed while the files were being written."; return false; }
    for (auto& e : p.edits)
    {
        bool unknown = false;                            // (3.46.1: a version frozen meanwhile is found here too - its pieces are not in the plan)
        auto look = [&] (const EditDef& ed, int version)
        {
            for (auto* list : { &ed.regions, &ed.overdubs })
                for (auto& rg : *list)
                    for (auto& f : rg.files)
                        if ((f.trackId == plan.left || f.trackId == plan.right)
                            && std::none_of (plan.pieces.begin(), plan.pieces.end(), [&] (const PieceUse& u) { return u.edit == e->id && u.version == version && u.piece == rg.id; }))
                            unknown = true;
        };
        look (*e, 0);
        for (auto& v : e->versions) if (v.edit != nullptr) look (*v.edit, v.number);
        if (unknown) { why = "An edit changed while the files were being written."; return false; }
    }

    // the new track, where the left one was: its colour, its spot-mic role, the inputs of both
    TrackDef S;
    S.id = plan.stereo; S.name = plan.name; S.format = TrackFormat::Stereo;
    S.colour = L->colour; S.alignRole = L->alignRole;
    S.inputs[0] = L->inputs[0]; S.inputs[1] = R->inputs[0];
    S.micsLeft = L->mics; S.micsRight = R->mics;                         // (3.57.0: the mics remembered for each mono track go to its side)
    {
        size_t at = 0;
        for (size_t i = 0; i < p.tracks.size(); ++i) if (p.tracks[i].id == plan.left) at = i;
        p.tracks.insert (p.tracks.begin() + (long) at, S);
    }

    // the takes: one stereo file where the two mono ones were (the left one's place)
    for (auto& u : plan.takes)
    {
        TakeGroup* g = nullptr; sameTake (u, g);
        int il = -1, ir = -1;
        for (int i = 0; i < (int) g->files.size(); ++i) { if (g->files[(size_t) i].trackId == plan.left) il = i; else if (g->files[(size_t) i].trackId == plan.right) ir = i; }
        TakeFile tf;
        tf.trackId = plan.stereo; tf.trackName = plan.name; tf.file = plan.jobs[u.job].dest; tf.numChannels = 2;
        if (il >= 0) tf.sourceName = g->files[(size_t) il].sourceName;
        if (ir >= 0) tf.sourceName2 = g->files[(size_t) ir].sourceName.isNotEmpty() ? g->files[(size_t) ir].sourceName : g->files[(size_t) ir].file.getFileName();
        if (il >= 0 && tf.sourceName.isEmpty() && ir >= 0) tf.sourceName = g->files[(size_t) il].file.getFileName();      // (both names kept, for an AAF to find them again)
        const int keep = il >= 0 ? il : ir, drop = il >= 0 ? ir : -1;
        g->files[(size_t) keep] = tf;
        if (drop >= 0) g->files.erase (g->files.begin() + drop);
    }
    // the pieces of the edits: one stereo file in the left one's place; a volume change keeps the level of the two (their average, if they differed)
    for (auto& u : plan.pieces)
    {
        EditRegion* r = nullptr; samePiece (u, r);
        int il = -1, ir = -1;
        for (int i = 0; i < (int) r->files.size(); ++i) { if (r->files[(size_t) i].trackId == plan.left) il = i; else if (r->files[(size_t) i].trackId == plan.right) ir = i; }
        const auto& j = plan.jobs[u.job];
        RegionFile rf;
        rf.trackId = plan.stereo; rf.trackName = plan.name; rf.file = j.dest; rf.numChannels = 2; rf.fileStart = j.from;
        const int keep = il >= 0 ? il : ir, drop = il >= 0 ? ir : -1;
        if (keep < 0) continue;                          // (cannot be: samePiece found one of the two a moment ago)
        for (auto& gc : r->gains)
        {
            auto dbOf = [&] (int i) { return juce::isPositiveAndBelow (i, (int) gc.db.size()) ? gc.db[(size_t) i] : 0.0f; };
            const float lvl = drop >= 0 ? 0.5f * (dbOf (keep) + dbOf (drop)) : dbOf (keep);
            if (juce::isPositiveAndBelow (keep, (int) gc.db.size())) gc.db[(size_t) keep] = lvl;
            if (drop >= 0 && juce::isPositiveAndBelow (drop, (int) gc.db.size())) gc.db.erase (gc.db.begin() + drop);
        }
        r->files[(size_t) keep] = rf;
        if (drop >= 0) r->files.erase (r->files.begin() + drop);
    }

    // every list that names the two tracks names the stereo one instead (where the left one was)
    auto replaceIn = [&] (std::vector<juce::Uuid>& v)
    {
        const auto il = std::find (v.begin(), v.end(), plan.left), ir = std::find (v.begin(), v.end(), plan.right);
        if (il == v.end() && ir == v.end()) return;
        const auto at = (il != v.end() ? il : ir) - v.begin();
        v.erase (std::remove_if (v.begin(), v.end(), [&] (const juce::Uuid& x) { return x == plan.left || x == plan.right; }), v.end());
        v.insert (v.begin() + juce::jmin ((long) at, (long) v.size()), plan.stereo);
    };
    for (auto& w : p.takeWindows)
    {
        const bool bothHidden = w->isHidden (plan.left) && w->isHidden (plan.right);
        w->hiddenTracks.erase (std::remove_if (w->hiddenTracks.begin(), w->hiddenTracks.end(), [&] (const juce::Uuid& x) { return x == plan.left || x == plan.right; }), w->hiddenTracks.end());
        if (bothHidden) w->hiddenTracks.push_back (plan.stereo);
        replaceIn (w->editTracks);
    }
    auto convertLists = [&] (EditDef& e)
    {
        replaceIn (e.trackIds);
        replaceIn (e.fixTracks);
        if (e.guide.trackId == plan.left || e.guide.trackId == plan.right) { e.guide.trackId = plan.stereo; e.guide.trackName = plan.name; }
        // automation: the left track's fader, input gain and sends carry on on the stereo track; its pan and plug-in lanes, and the right track's lanes, go
        e.lanes.erase (std::remove_if (e.lanes.begin(), e.lanes.end(), [&] (const AutoLane& l)
                        {
                            if (l.trackId == plan.right) return true;
                            return l.trackId == plan.left && ! (l.param == autoparam::fader || l.param == autoparam::gain || autoparam::isSend (l.param));
                        }), e.lanes.end());
        for (auto& l : e.lanes) if (l.trackId == plan.left) l.trackId = plan.stereo;
    };
    for (auto& e : p.edits)
    {
        convertLists (*e);
        for (auto& v : e->versions)                      // 3.46.1: each frozen version as the edit itself, and the mixer settings kept with it
        {
            if (v.edit != nullptr) convertLists (*v.edit);
            if (v.mixer.isObject()) v.mixer = joinedMix (v.mixer, plan.left, plan.right, plan.stereo);
        }
    }

    // the mixers: the stereo track's strip starts with the left track's fader, input gain, output, sends and VCAs (no plug-ins); hidden only if both were
    p.syncMixers();
    for (auto& m : p.mixers)
    {
        auto* a = m->stripFor (plan.left); auto* s = m->stripFor (plan.stereo);
        s->gainDb.set (a->gainDb.get()); s->inGainDb.set (a->inGainDb.get());
        s->outOn.set (a->outOn.get()); s->outDest = a->outDest;
        s->vcaMask.store (a->vcaMask.load());
        for (auto& snd : a->sends.pool)
            if (snd->active() && snd->dest != plan.left && snd->dest != plan.right)
            {
                auto* t = s->sends.get (snd->dest);
                t->gainDb.set (snd->gainDb.get()); t->pre.set (snd->pre.get()); t->followMute.set (snd->followMute.get()); t->mute.set (snd->mute.get());
            }
        const bool bothHidden = m->isStripHidden (plan.left) && m->isStripHidden (plan.right);
        m->hiddenStrips.erase (std::remove_if (m->hiddenStrips.begin(), m->hiddenStrips.end(), [&] (const juce::Uuid& x) { return x == plan.left || x == plan.right; }), m->hiddenStrips.end());
        if (bothHidden) m->hiddenStrips.push_back (plan.stereo);
        for (auto& pl : m->stripPlaces)                  // 3.58.0: a bus or VCA moved next to one of them in the mixer's window stays next to the stereo track
            if (pl.after == plan.left || pl.after == plan.right) pl.after = plan.stereo;
    }

    // the two mono tracks leave the project (their files stay on the disk)
    p.tracks.erase (std::remove_if (p.tracks.begin(), p.tracks.end(), [&] (const TrackDef& t) { return t.id == plan.left || t.id == plan.right; }), p.tracks.end());
    p.structureChanged();
    p.undoReset();                                       // (Ctrl+Z cannot go back to the two tracks: they are gone)
    p.keepAllWindows();                                  // (nor can "keep / revert this window" at quit - 3.27.1: every window, the mixers too: their kept state had the two mono strips, so each one was listed as changed and a revert would have undone the strip settings carried over)
    return true;
}
// ======================================================================================================== 3.57.0: into a stereo track that is there already
/** Two mono tracks' audio in ONE take window, joined into stereo files on a stereo track the project already has ('onto'): a take window's takes
    moved onto the tracks of another take window that has the pair as one stereo track (tracklayout, "Move this window's takes onto the tracks of
    another take window"). Only that window's takes change, and the pieces of edits made from them; the frozen versions of edits stay as they are
    (as when clips are moved: decided for 3.47.0). The two mono tracks stay in the project with whatever else they hold (they can be deleted
    afterwards when nothing is left on them). A take with only one of the two: silence on the other side. An ordinary change: Ctrl + Z puts the
    mono files back. */
struct IntoPlan
{
    juce::Uuid window, left, right, onto;     // 'left' or 'right' may be null: only one side has a track
    juce::String ontoName;
    std::vector<FileJob> jobs;
    std::vector<TakeUse> takes;
    std::vector<PieceUse> pieces;             // (version always 0: the edits themselves)
};

/** Works out the change. "" when it can be done (with 'out' filled in), else the reason, in plain words. */
inline juce::String planInto (const Project& p, const juce::Uuid& windowId, const juce::Uuid& leftId, const juce::Uuid& rightId, const juce::Uuid& ontoId, IntoPlan& out)
{
    out = IntoPlan();
    const TakeWindowDef* w = nullptr;
    for (auto& x : p.takeWindows) if (x->id == windowId) w = x.get();
    const auto* L = leftId.isNull() ? nullptr : trackOf (p, leftId);
    const auto* R = rightId.isNull() ? nullptr : trackOf (p, rightId);
    const auto* S = trackOf (p, ontoId);
    if (w == nullptr || S == nullptr || (L == nullptr && R == nullptr)) return "Nothing to join.";
    if ((L != nullptr && L == R) || ontoId == leftId || ontoId == rightId) return "Choose two different tracks.";
    if (S->format != TrackFormat::Stereo) return S->name + " is not a stereo track.";
    for (auto* t : { L, R })
        if (t != nullptr && (t->isLtc || t->format != TrackFormat::Mono)) return t->isLtc ? juce::String ("The LTC (timecode) track cannot be part of a stereo track.") : t->name + " is not a mono track.";
    if (S->isLtc) return "The LTC (timecode) track cannot be part of a stereo track.";
    out.window = windowId; out.left = leftId; out.right = rightId; out.onto = ontoId; out.ontoName = S->name;
    auto isPair = [&] (const juce::Uuid& id) { return ! id.isNull() && (id == leftId || id == rightId); };
    const auto nameOf = [&] (const juce::Uuid& id) { auto* t = trackOf (p, id); return t != nullptr ? t->name : juce::String ("?"); };
    std::map<juce::String, size_t> wholeTake;
    auto key = [] (const juce::File& a, const juce::File& b) { return a.getFullPathName() + "|" + b.getFullPathName(); };
    std::set<juce::Uuid> takeIds;
    for (auto& g : w->groups)
    {
        const TakeFile* fl = nullptr; const TakeFile* fr = nullptr; bool onS = false;
        for (auto& f : g.files) { if (! leftId.isNull() && f.trackId == leftId) fl = &f; else if (! rightId.isNull() && f.trackId == rightId) fr = &f; else if (f.trackId == ontoId) onS = true; }
        if (fl == nullptr && fr == nullptr) continue;
        if (onS) return "Take " + w->displayName (g) + " already has audio on " + S->name + ".";
        if ((fl != nullptr && fl->numChannels != 1) || (fr != nullptr && fr->numChannels != 1))
            return "Take " + w->displayName (g) + " has a file on " + (fl != nullptr && fl->numChannels != 1 ? nameOf (leftId) : nameOf (rightId)) + " that is not mono.";
        for (auto& wp : g.waiting)
            if (wp.active() && (wp.tracks.empty() || std::any_of (wp.tracks.begin(), wp.tracks.end(), isPair)))
                return "Take " + w->displayName (g) + " has a part waiting for corrected audio (Export for Processing). Re-link it, or stop waiting (right-click the block), first.";
        FileJob j;
        if (fl != nullptr) j.left = fl->file;
        if (fr != nullptr) j.right = fr->file;
        j.dest = audioops::uniqueFile (p.takeFolder (*w).getChildFile (TakeWindowDef::fileNameFor (g.number, w->labelFor (g), S->name)));
        const auto k = key (j.left, j.right);
        size_t idx;
        if (auto it = wholeTake.find (k); it != wholeTake.end()) idx = it->second;
        else { idx = out.jobs.size(); out.jobs.push_back (j); wholeTake[k] = idx; }
        out.takes.push_back ({ w->id, g.id, j.left, j.right, idx });
        takeIds.insert (g.id);
    }
    if (out.takes.empty()) return "There is no audio on " + juce::String (L != nullptr ? L->name : R->name) + " in this take window.";
    // the pieces of edits made from these takes
    const auto fixes = p.audioFolder().getChildFile ("Fixes");
    for (auto& e : p.edits)
    {
        bool touched = false;
        for (auto* list : { &e->regions, &e->overdubs })
            for (auto& r : *list)
            {
                if (takeIds.count (r.takeId) == 0) continue;
                const RegionFile* fl = nullptr; const RegionFile* fr = nullptr; bool onS = false;
                for (auto& f : r.files) { if (! leftId.isNull() && f.trackId == leftId) fl = &f; else if (! rightId.isNull() && f.trackId == rightId) fr = &f; else if (f.trackId == ontoId) onS = true; }
                if (fl == nullptr && fr == nullptr) continue;
                if (onS) return "A piece of the edit '" + e->name + "' (" + r.takeName + ") already has audio on " + S->name + ".";
                if (r.waiting.active() && (r.waiting.tracks.empty() || std::any_of (r.waiting.tracks.begin(), r.waiting.tracks.end(), isPair)))
                    return "The edit '" + e->name + "' has a part of these takes that is " + juce::String (r.waiting.rx ? "out in iZotope RX" : "waiting for corrected audio")
                           + " (" + r.waiting.name + "). Bring it back, or cancel it (right-click the block), first.";
                PieceUse u; u.edit = e->id; u.piece = r.id;
                u.hasLeft = fl != nullptr; u.hasRight = fr != nullptr;
                if (fl != nullptr) { u.left = fl->file; u.leftStart = fl->fileStart; }
                if (fr != nullptr) { u.right = fr->file; u.rightStart = fr->fileStart; }
                const auto it = (u.leftStart == 0 && u.rightStart == 0) ? wholeTake.find (key (u.left, u.right)) : wholeTake.end();
                if (it != wholeTake.end()) u.job = it->second;
                else
                {
                    const double rate = r.sampleRate > 0 ? r.sampleRate : 48000.0;
                    const auto h = (juce::int64) std::llround (2.0 * rate);
                    FileJob j;
                    j.left = u.left; j.right = u.right; j.leftStart = u.leftStart; j.rightStart = u.rightStart;
                    j.wantFrom = r.srcIn + juce::jmin ((juce::int64) 0, (juce::int64) std::llround (r.inStart * rate)) - h;
                    j.wantTo = r.srcOut + juce::jmax ((juce::int64) 0, (juce::int64) std::llround (r.outEnd * rate)) + h;
                    j.dest = audioops::uniqueFile (fixes.getChildFile (sanitiseForFile (r.takeName.upToFirstOccurrenceOf (" (", false, false) + " - " + S->name) + " (stereo).wav"));
                    u.job = out.jobs.size(); out.jobs.push_back (j);
                }
                out.pieces.push_back (u);
                touched = true;
            }
        if (! touched) continue;
        for (auto& l : e->lanes)                                          // automation on the mono tracks would have nowhere to go
            if (isPair (l.trackId))
                return "The edit '" + e->name + "' has automation on " + nameOf (l.trackId) + ". Move these takes before automating, or delete that automation first.";
    }
    return {};
}

/** Makes the change once the files are written (message thread). False with 'why' - and nothing changed - when the project changed since the plan
    (the caller then deletes the files). */
inline bool applyInto (Project& p, const IntoPlan& plan, juce::String& why)
{
    const auto* S = trackOf (p, plan.onto);
    auto* w = p.findTakeWindow (plan.window);
    if (S == nullptr || w == nullptr) { why = "A track or the take window has gone."; return false; }
    for (auto& j : plan.jobs) if (! j.dest.existsAsFile()) { why = "A new file is missing: " + j.dest.getFullPathName(); return false; }
    auto isL = [&] (const juce::Uuid& id) { return ! plan.left.isNull() && id == plan.left; };
    auto isR = [&] (const juce::Uuid& id) { return ! plan.right.isNull() && id == plan.right; };
    auto groupOf = [&] (const TakeUse& u) -> TakeGroup*
    {
        auto* g = w->findGroup (u.take);
        if (g == nullptr) return nullptr;
        juce::File l, r; bool onS = false;
        for (auto& f : g->files) { if (isL (f.trackId)) l = f.file; else if (isR (f.trackId)) r = f.file; else if (f.trackId == plan.onto) onS = true; }
        return (l == u.left && r == u.right && ! onS) ? g : nullptr;
    };
    auto pieceOf = [&] (const PieceUse& u) -> EditRegion*
    {
        auto* e = p.findEdit (u.edit);
        auto* rg = e != nullptr ? e->findAny (u.piece) : nullptr;
        if (rg == nullptr) return nullptr;
        const RegionFile* fl = nullptr; const RegionFile* fr = nullptr;
        for (auto& f : rg->files) { if (isL (f.trackId)) fl = &f; else if (isR (f.trackId)) fr = &f; else if (f.trackId == plan.onto) return nullptr; }
        if ((fl != nullptr) != u.hasLeft || (fr != nullptr) != u.hasRight) return nullptr;
        if (fl != nullptr && (fl->file != u.left || fl->fileStart != u.leftStart)) return nullptr;
        if (fr != nullptr && (fr->file != u.right || fr->fileStart != u.rightStart)) return nullptr;
        return rg;
    };
    for (auto& u : plan.takes)  if (groupOf (u) == nullptr)  { why = "The takes changed while the files were being written."; return false; }
    for (auto& u : plan.pieces) if (pieceOf (u) == nullptr) { why = "An edit changed while the files were being written."; return false; }
    for (auto& g : w->groups)                                             // nothing on the two tracks in this window that the plan does not know of
        for (auto& f : g.files)
            if ((isL (f.trackId) || isR (f.trackId)) && std::none_of (plan.takes.begin(), plan.takes.end(), [&] (const TakeUse& u) { return u.take == g.id; }))
            { why = "The takes changed while the files were being written."; return false; }

    std::set<juce::Uuid> takeIds;
    for (auto& u : plan.takes)
    {
        auto* g = groupOf (u);
        takeIds.insert (g->id);
        int il = -1, ir = -1;
        for (int i = 0; i < (int) g->files.size(); ++i) { if (isL (g->files[(size_t) i].trackId)) il = i; else if (isR (g->files[(size_t) i].trackId)) ir = i; }
        TakeFile tf; tf.trackId = plan.onto; tf.trackName = S->name; tf.file = plan.jobs[u.job].dest; tf.numChannels = 2;
        if (il >= 0) tf.sourceName = g->files[(size_t) il].sourceName.isNotEmpty() ? g->files[(size_t) il].sourceName : g->files[(size_t) il].file.getFileName();
        if (ir >= 0) tf.sourceName2 = g->files[(size_t) ir].sourceName.isNotEmpty() ? g->files[(size_t) ir].sourceName : g->files[(size_t) ir].file.getFileName();
        for (auto& wp : g->waiting) for (auto& t : wp.tracks) if (isL (t) || isR (t)) t = plan.onto;
        const int keep = il >= 0 ? il : ir, drop = il >= 0 ? ir : -1;
        g->files[(size_t) keep] = tf;
        if (drop >= 0) g->files.erase (g->files.begin() + drop);
    }
    std::set<juce::Uuid> editsTouched;
    for (auto& u : plan.pieces)
    {
        auto* r = pieceOf (u);
        int il = -1, ir = -1;
        for (int i = 0; i < (int) r->files.size(); ++i) { if (isL (r->files[(size_t) i].trackId)) il = i; else if (isR (r->files[(size_t) i].trackId)) ir = i; }
        const auto& j = plan.jobs[u.job];
        RegionFile rf; rf.trackId = plan.onto; rf.trackName = S->name; rf.file = j.dest; rf.numChannels = 2; rf.fileStart = j.from;
        const int keep = il >= 0 ? il : ir, drop = il >= 0 ? ir : -1;
        if (keep < 0) continue;
        for (auto& gc : r->gains)
        {
            auto dbOf = [&] (int i) { return juce::isPositiveAndBelow (i, (int) gc.db.size()) ? gc.db[(size_t) i] : 0.0f; };
            const float lvl = drop >= 0 ? 0.5f * (dbOf (keep) + dbOf (drop)) : dbOf (keep);
            if (juce::isPositiveAndBelow (keep, (int) gc.db.size())) gc.db[(size_t) keep] = lvl;
            if (drop >= 0 && juce::isPositiveAndBelow (drop, (int) gc.db.size())) gc.db.erase (gc.db.begin() + drop);
        }
        for (auto& t : r->waiting.tracks) if (isL (t) || isR (t)) t = plan.onto;
        r->files[(size_t) keep] = rf;
        if (drop >= 0) r->files.erase (r->files.begin() + drop);
        editsTouched.insert (u.edit);
    }
    // the edits' lists of tracks: the stereo track where the left one was; the mono ones only go when no piece of the edit uses them any more
    for (auto& id : editsTouched)
    {
        auto* e = p.findEdit (id);
        if (e == nullptr) continue;
        bool usesL = false, usesR = false;
        for (auto* list : { &e->regions, &e->overdubs }) for (auto& r : *list) for (auto& f : r.files) { usesL = usesL || isL (f.trackId); usesR = usesR || isR (f.trackId); }
        auto fix = [&] (std::vector<juce::Uuid>& v, bool addOnto)
        {
            const auto at = std::find_if (v.begin(), v.end(), [&] (const juce::Uuid& x) { return isL (x) || isR (x); });
            const auto pos = at - v.begin();
            if (addOnto && at != v.end() && std::find (v.begin(), v.end(), plan.onto) == v.end()) v.insert (v.begin() + pos, plan.onto);
            v.erase (std::remove_if (v.begin(), v.end(), [&] (const juce::Uuid& x) { return (isL (x) && ! usesL) || (isR (x) && ! usesR); }), v.end());
        };
        if (! e->trackIds.empty())
        {
            if (std::find (e->trackIds.begin(), e->trackIds.end(), plan.onto) == e->trackIds.end()
                && std::none_of (e->trackIds.begin(), e->trackIds.end(), [&] (const juce::Uuid& x) { return isL (x) || isR (x); }))
                e->trackIds.push_back (plan.onto);
            fix (e->trackIds, true);
        }
        fix (e->fixTracks, true);
        if ((isL (e->guide.trackId) && ! usesL) || (isR (e->guide.trackId) && ! usesR)) { e->guide.trackId = plan.onto; e->guide.trackName = S->name; }
    }
    // this window: the stereo track shown; marks on chosen tracks follow
    w->unhide (plan.onto);
    if (takeIds.count (w->editTake) != 0)
    {
        bool any = false;
        for (auto& t : w->editTracks) if (isL (t) || isR (t)) { t = plan.onto; any = true; }
        if (any) { std::vector<juce::Uuid> once; for (auto& t : w->editTracks) if (std::find (once.begin(), once.end(), t) == once.end()) once.push_back (t); w->editTracks = once; }
    }
    p.changed();
    return true;
}
}} // namespace td::stereojoin
