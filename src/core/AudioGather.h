#pragma once
#include "Project.h"
#include "ProjectCopy.h"
#include <functional>
#include <map>
#include <set>

namespace td
{
/** 3.62.0 (asked for on 23 November 2026): "Import source tracks to project folders" - "if any audio file is being used by fermata that isn't in
    its own project folder system, it will copy a version into them and use that copied file, rather than the original ... if a user imports
    files but doesn't copy them over ... the option to copy them over at a later time".

    Every audio file the project PLAYS from outside its own folder - a multitrack import made with "Use the audio files where they are", an AAF
    import that found its audio in another folder, Find missing audio files pointed at another drive - is copied into the project's folder,
    read back and checked against the original, and only then does the project play the copy. The originals are not changed or deleted.

    Where a copy goes: a take's file into its take window's own folder in Recorded Media (beside the takes recorded there); a file that only an
    edit plays (an AAF import's audio, a visual guide) into Recorded Media/From elsewhere, as Create copy of entire project puts such files. A
    name already used there gets " (2)", " (3)" ... A file that cannot be found is listed and left as it is. Parts waiting for corrected audio
    (Export for Processing) are left alone: those files are the other program's to write. fermata only (fermata:Live does not use it). */
namespace gather
{
struct Item
{
    juce::File   from, to;               // the original (outside the project) and where its copy goes
    juce::int64  size = 0;
    juce::String usedBy;                 // the first place that plays it ("Takes - Piece 1, 003 - Gravity", "Edit - Gravity")
    int          uses = 0;               // how many places play it (a take's file and the edit pieces made from it ...)
    bool         take = false;           // a take's file (its copy goes into the take window's folder)
};

struct Plan
{
    std::vector<Item>  items;
    juce::StringArray  missing;          // outside the project and not found: left as they are
    juce::int64        bytes = 0;
    juce::File         folder;           // the project's folder
    juce::File         projectFile;      // the project it was made for (the copies are used only by that project)
};

/** Message thread: every file the project plays from outside its folder, once each, and where its copy goes. */
inline Plan plan (Project& p)
{
    Plan out;
    out.folder = p.projectFolder(); out.projectFile = p.projectFile;
    if (p.projectFile == juce::File() || out.folder == juce::File()) return out;
    std::map<juce::String, size_t> index;                                  // the original's full path -> its item
    std::set<juce::String> seenMissing;
    std::vector<juce::File> chosen;                                         // the copies' places so far (two files of one name in one folder)
    auto placeFor = [&] (const juce::File& dir, const juce::File& f)
    {
        auto d = dir.getChildFile (f.getFileName());
        auto used = [&] (const juce::File& c) { return c.exists() || std::find (chosen.begin(), chosen.end(), c) != chosen.end(); };
        for (int k = 2; used (d) && k < 10000; ++k)
            d = dir.getChildFile (f.getFileNameWithoutExtension() + " (" + juce::String (k) + ")" + f.getFileExtension());
        chosen.push_back (d);
        return d;
    };
    auto add = [&] (const juce::File& f, const juce::File& dir, const juce::String& where, bool take)
    {
        if (f == juce::File() || f.isAChildOf (out.folder)) return;
        const auto path = f.getFullPathName();
        auto it = index.find (path);
        if (it != index.end()) { ++out.items[it->second].uses; return; }
        if (! f.existsAsFile())
        {
            if (seenMissing.insert (path).second) out.missing.add (f.getFileName() + "   (" + where + ")");
            return;
        }
        Item i; i.from = f; i.to = placeFor (dir, f); i.size = f.getSize(); i.usedBy = where; i.uses = 1; i.take = take;
        index[path] = out.items.size();
        out.bytes += i.size;
        out.items.push_back (std::move (i));
    };
    for (auto& w : p.takeWindows)
        for (auto& g : w->groups)
            for (auto& tf : g.files) add (tf.file, p.takeFolder (*w), "Takes - " + w->name + ", " + w->displayName (g), true);
    const auto elsewhere = p.audioFolder().getChildFile ("From elsewhere");
    for (auto& e : p.edits)
        withVersions (*e, [&] (EditDef& ed)                                // (the frozen versions' pieces too: they play when looked back at)
        {
            for (auto* rs : { &ed.regions, &ed.overdubs })
                for (auto& r : *rs)
                    for (auto& rf : r.files) add (rf.file, elsewhere, "Edit - " + e->name, false);
            if (ed.guide.active()) add (ed.guide.file, elsewhere, "Edit - " + e->name + ", its visual guide", false);
        });
    return out;
}

/** Any thread: copies every item to a temporary file beside its place, reads it back and compares it with what was read from the original, and
    only then gives it its name - so a copy that is not whole never has the name. 'progress' gets 0..1 and a short text; false from it cancels.
    On a cancel or a failure every copy this made is deleted again (the project has not been changed yet). "" when every copy is made and checked. */
inline juce::String copy (const Plan& plan, const std::function<bool (float, const juce::String&)>& progress)
{
    if (plan.items.empty()) return {};
    const auto free = plan.folder.getBytesFreeOnVolume();
    if (free > 0 && free < plan.bytes + 200LL * 1024 * 1024)
        return "Not enough room: the copies need " + juce::File::descriptionOfSizeInBytes (plan.bytes) + " and the project's drive has "
               + juce::File::descriptionOfSizeInBytes (free) + " free. Nothing was copied.";
    std::vector<juce::File> made;
    auto undo = [&made] (const juce::String& why) { for (auto& f : made) f.deleteFile(); return why; };
    juce::int64 done = 0; const double all = (double) juce::jmax ((juce::int64) 1, plan.bytes) * 2.0;
    std::vector<unsigned char> buf (4 * 1024 * 1024);
    for (auto& it : plan.items)
    {
        const auto text = "Copying " + it.from.getFileName();
        if (! progress ((float) ((double) done / all), text)) return undo ("Cancelled.");
        if (! it.to.getParentDirectory().createDirectory()) return undo ("Cannot make the folder " + it.to.getParentDirectory().getFullPathName() + ". Nothing was changed.");
        if (it.to.exists()) return undo ("A file appeared at " + it.to.getFullPathName() + " while copying. Nothing was changed: try again.");
        juce::TemporaryFile tmp (it.to);
        juce::uint32 crc = 0xffffffffu; juce::int64 got = 0;
        {
            juce::FileInputStream in (it.from);
            if (in.failedToOpen()) return undo ("Cannot read " + it.from.getFullPathName() + ". Nothing was changed.");
            juce::FileOutputStream out (tmp.getFile());
            if (out.failedToOpen()) return undo ("Cannot write into " + it.to.getParentDirectory().getFullPathName() + ". Nothing was changed.");
            for (;;)
            {
                const auto n = in.read (buf.data(), (int) buf.size());
                if (n <= 0) break;
                crc = projectcopy::detail::crcUpdate (crc, buf.data(), (size_t) n);
                if (! out.write (buf.data(), (size_t) n)) return undo ("The drive is full or not writable: " + it.to.getFullPathName() + ". Nothing was changed.");
                got += n; done += n;
                if (! progress ((float) ((double) done / all), text)) { out.flush(); return undo ("Cancelled."); }
            }
            out.flush();
            if (out.getStatus().failed()) return undo ("Could not write " + it.to.getFileName() + " (" + out.getStatus().getErrorMessage() + "). Nothing was changed.");
        }
        if (got != it.from.getSize()) return undo ("Could not read all of " + it.from.getFullPathName() + " (it changed while it was copied?). Nothing was changed.");
        {   // read back and compare
            juce::FileInputStream in (tmp.getFile());
            if (in.failedToOpen()) return undo ("Cannot read back the copy of " + it.from.getFileName() + ". Nothing was changed.");
            juce::uint32 back = 0xffffffffu;
            for (;;)
            {
                const auto n = in.read (buf.data(), (int) buf.size());
                if (n <= 0) break;
                back = projectcopy::detail::crcUpdate (back, buf.data(), (size_t) n);
                done += n;
                if (! progress ((float) ((double) done / all), "Checking " + it.from.getFileName())) return undo ("Cancelled.");
            }
            if (back != crc || tmp.getFile().getSize() != got)
                return undo ("The copy of " + it.from.getFileName() + " does not match the original (the drive may be faulty). Nothing was changed.");
        }
        tmp.getFile().setLastModificationTime (it.from.getLastModificationTime());
        if (! tmp.overwriteTargetFileWithTemporary()) return undo ("Could not name the copy " + it.to.getFullPathName() + ". Nothing was changed.");
        made.push_back (it.to);
    }
    return {};
}

/** Deletes the copies of a plan again (after a cancel, or when the project they were for has been closed meanwhile). */
inline void removeCopies (const Plan& plan) { for (auto& it : plan.items) if (it.to.existsAsFile()) it.to.deleteFile(); }

/** Message thread, once every copy is made: every place that plays an original now plays its copy - the takes' files, the edit pieces and
    overdubs (the frozen versions' too) and the visual guides. Not an undo step (as Find missing audio files): Ctrl+Z must never quietly point
    the project back at the originals; the save before it is the project's .bak. A copy that is not there (deleted meanwhile) is not used.
    Returns how many places now play a copy. */
inline int apply (Project& p, const Plan& plan)
{
    if (p.projectFile != plan.projectFile) return 0;
    std::map<juce::String, juce::File> to;
    for (auto& it : plan.items) if (it.to.existsAsFile()) to[it.from.getFullPathName()] = it.to;
    if (to.empty()) return 0;
    int n = 0;
    auto fix = [&] (juce::File& f) { auto m = to.find (f.getFullPathName()); if (m == to.end()) return false; f = m->second; ++n; return true; };
    for (auto& w : p.takeWindows)
        for (auto& g : w->groups)
        {
            bool any = false;
            for (auto& tf : g.files) any = fix (tf.file) || any;
            if (any) g.verified = false;                                    // (looked at again when the project next opens)
        }
    for (auto& e : p.edits)
        withVersions (*e, [&] (EditDef& ed)
        {
            for (auto* rs : { &ed.regions, &ed.overdubs })
                for (auto& r : *rs)
                    for (auto& rf : r.files) fix (rf.file);
            if (ed.guide.active()) fix (ed.guide.file);
        });
    if (n > 0)
    {
        p.structureChanged();
        p.undoReset();
        p.keepAllWindows();                                                 // (as relinkAudio: the windows' kept copies held the old paths)
    }
    return n;
}
} // namespace gather
} // namespace td
