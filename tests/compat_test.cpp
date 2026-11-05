// The compatibility check between fermata and fermata:Live (fermata 3.31.0 / fermata:Live 0.3.2). Built and run by tools/compat-check.sh, by
// tools/sandbox-check.sh and by the GitHub build on every upload. It checks that:
//   - COMPATIBILITY.md and the shared code (src/core/Compat.h, Project.h) give the same numbers: the contract, the project file, the sample rates;
//   - every file COMPATIBILITY.md lists as shared is there;
//   - the shared code uses nothing from either program's own folders (it is built into both, so it may only use itself);
//   - fermata's look (the src/ui files listed as shared) uses only itself and src/core, and fermata:Live uses nothing else from src/ui;
//   - a project written by the shared code - as either program writes it - says it is a "Fermata project" of the contract's version, and reads
//     back the same, with fermata:Live's flags, replies and fix versions (contract 3), while an ordinary marker is saved exactly as before.
// fermata:Live's own files (the live session index, contract 2) are named here; that a live recording merges into a take fermata opens is shown
// by tests/live_recorder_test.cpp (tools/live-check.sh), which builds the recorder's code too.
// Usage: compat_test <the repository folder>
#include "../src/core/Project.h"
#include "../src/core/Compat.h"
#include <iostream>
#include <vector>

using namespace td;

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

/** The text after "label:" on the first line of 'lines' that starts with it (spaces trimmed), or "" when no line does. */
static juce::String valueAfter (const juce::StringArray& lines, const juce::String& label)
{
    for (auto& l : lines)
        if (l.trim().startsWithIgnoreCase (label + ":"))
            return l.trim().fromFirstOccurrenceOf (":", false, false).trim();
    return {};
}

/** The file and folder names written in backticks (`src/core/`, `CLAUDE.md`: a name with a '/' in it, or a .md file) in the section of
    COMPATIBILITY.md that starts with the heading 'heading', up to the next "## " heading. */
static juce::StringArray pathsInSection (const juce::StringArray& lines, const juce::String& heading)
{
    juce::StringArray out;
    bool inside = false;
    for (auto& l : lines)
    {
        if (l.startsWith ("## ")) { inside = l.trim() == heading; continue; }
        if (! inside) continue;
        auto rest = l;
        while (rest.contains ("`"))
        {
            const auto p = rest.fromFirstOccurrenceOf ("`", false, false).upToFirstOccurrenceOf ("`", false, false);
            rest = rest.fromFirstOccurrenceOf ("`", false, false).fromFirstOccurrenceOf ("`", false, false);
            if (! p.containsAnyOf (" <>*") && (p.contains ("/") || p.endsWith (".md"))) out.addIfNotAlreadyThere (p);
        }
    }
    return out;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::cout << "Usage: compat_test <the repository folder>" << std::endl; return 2; }
    const juce::File repo (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])));
    const auto contractFile = repo.getChildFile ("COMPATIBILITY.md");
    if (! contractFile.existsAsFile()) { std::cout << "COMPATIBILITY.md not found in " << repo.getFullPathName() << std::endl; return 2; }
    juce::StringArray lines; lines.addLines (contractFile.loadFileAsString());

    std::cout << "== the contract (COMPATIBILITY.md) and the shared code say the same" << std::endl;
    check (valueAfter (lines, "Contract version").getIntValue() == compat::kContractVersion,
           "Contract version: " + juce::String (compat::kContractVersion) + " (COMPATIBILITY.md says \"" + valueAfter (lines, "Contract version") + "\")");
    const auto projectLine = valueAfter (lines, "Project file");
    const auto wantProject = "\"" + juce::String (compat::kProjectFormatName) + "\", version " + juce::String (compat::kProjectFormatVersion);
    check (projectLine == wantProject, "Project file: " + wantProject + " (COMPATIBILITY.md says \"" + projectLine + "\")");
    juce::String rates;
    for (auto r : Project::kProjectRates) rates << (rates.isEmpty() ? "" : " ") << juce::String ((int) r);
    check (valueAfter (lines, "Sample rates (Hz)") == rates, "Sample rates (Hz): " + rates + " (COMPATIBILITY.md says \"" + valueAfter (lines, "Sample rates (Hz)") + "\")");
    const auto liveLine = valueAfter (lines, "Live session index");
    const auto wantLive = "\"" + juce::String (compat::kLiveSessionFormatName) + "\", version " + juce::String (compat::kLiveSessionFormatVersion);
    check (liveLine == wantLive, "Live session index: " + wantLive + " (COMPATIBILITY.md says \"" + liveLine + "\")");

    std::cout << "== every shared file is there" << std::endl;
    const auto shared = pathsInSection (lines, "## Shared code");
    check (shared.size() > 0, "COMPATIBILITY.md lists the shared code (" + juce::String (shared.size()) + " entries)");
    for (auto& p : shared)
        check (repo.getChildFile (p).exists(), p);

    std::cout << "== the shared code uses only itself" << std::endl;
    {
        // A file in src/core is built into both programs, so it may include only src/core and the outside libraries listed as shared - never a
        // program's own code (src/ui, src/reharmoniser's own files, src/live, src/Main.cpp), which the other program does not have.
        const juce::StringArray allowed { "../reharmoniser/third_party/", "../../third_party/" };
        juce::StringArray bad;
        for (auto& f : repo.getChildFile ("src/core").findChildFiles (juce::File::findFiles, false))
        {
            juce::StringArray code; code.addLines (f.loadFileAsString());
            for (int i = 0; i < code.size(); ++i)
            {
                const auto l = code[i].trim();
                if (! l.startsWith ("#include") || ! l.contains ("\"")) continue;
                const auto path = l.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\"", false, false);
                if (! path.contains ("/")) continue;                                        // (a file beside it in src/core)
                bool ok = false;
                for (auto& a : allowed) ok = ok || path.startsWith (a);
                if (! ok) bad.add ("src/core/" + f.getFileName() + ":" + juce::String (i + 1) + " includes \"" + path + "\"");
            }
        }
        check (bad.isEmpty(), bad.isEmpty() ? juce::String ("nothing in src/core includes a program's own code")
                                            : "src/core includes a program's own code:\n        " + bad.joinIntoString ("\n        "));
    }

    std::cout << "== fermata's look, shared with fermata:Live, uses only itself and the shared core" << std::endl;
    {
        // the src/ui files listed under Shared code may include each other and src/core only; src/live may include no other file of src/ui
        juce::StringArray sharedUi;
        for (auto& p : shared) if (p.startsWith ("src/ui/")) sharedUi.add (p.fromFirstOccurrenceOf ("src/ui/", false, false));
        check (sharedUi.size() > 0, "COMPATIBILITY.md lists fermata's look as shared (" + juce::String (sharedUi.size()) + " files)");
        auto includesOf = [] (const juce::File& f)
        {
            juce::StringArray out, code; code.addLines (f.loadFileAsString());
            for (int i = 0; i < code.size(); ++i)
            {
                const auto l = code[i].trim();
                if (l.startsWith ("#include") && l.contains ("\""))
                    out.add (juce::String (i + 1) + ":" + l.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\"", false, false));
            }
            return out;
        };
        juce::StringArray bad;
        for (auto& name : sharedUi)
            for (auto& inc : includesOf (repo.getChildFile ("src/ui/" + name)))
            {
                const auto path = inc.fromFirstOccurrenceOf (":", false, false);
                const bool ok = path.startsWith ("../core/") || (! path.contains ("/") && sharedUi.contains (path));
                if (! ok) bad.add ("src/ui/" + name + ":" + inc.upToFirstOccurrenceOf (":", false, false) + " includes \"" + path + "\"");
            }
        check (bad.isEmpty(), bad.isEmpty() ? juce::String ("the shared src/ui files include nothing but each other and src/core")
                                            : "a shared src/ui file includes code that is not shared:\n        " + bad.joinIntoString ("\n        "));
        bad.clear();
        for (auto& f : repo.getChildFile ("src/live").findChildFiles (juce::File::findFiles, true))
        {
            if (! (f.hasFileExtension ("h") || f.hasFileExtension ("cpp"))) continue;
            for (auto& inc : includesOf (f))
            {
                const auto path = inc.fromFirstOccurrenceOf (":", false, false);
                if (path.startsWith ("../ui/") && ! sharedUi.contains (path.fromFirstOccurrenceOf ("../ui/", false, false)))
                    bad.add ("src/live/" + f.getRelativePathFrom (repo.getChildFile ("src/live")) + ":" + inc.upToFirstOccurrenceOf (":", false, false) + " includes \"" + path + "\"");
            }
        }
        check (bad.isEmpty(), bad.isEmpty() ? juce::String ("fermata:Live uses nothing from src/ui but the shared look")
                                            : "fermata:Live includes a file of fermata's windows that is not shared:\n        " + bad.joinIntoString ("\n        "));
    }

    std::cout << "== a project written by the shared code reads back the same" << std::endl;
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fermata_compat_test");
        tmp.deleteRecursively(); tmp.createDirectory();
        Project p;
        p.projectFile = tmp.getChildFile ("Concert.fermata");
        p.setInputCount (8);
        p.setOutputCount (4);
        p.tracks.clear();
        p.addTrack ("Main pair", TrackFormat::Stereo, 0);
        p.addTrack ("Soloist", TrackFormat::Mono, 2);
        p.sampleRate = 96000.0;
        p.takeWindows.front()->name = "First half";
        {   // contract 3: an ordinary marker, a flag with a reply, and a fix version (as fermata:Live's Primary window writes them)
            auto& w = *p.takeWindows.front();
            Marker plain; plain.id = juce::Uuid(); plain.name = "3"; plain.seconds = 12.0; w.markers.push_back (plain);
            Marker f; f.id = juce::Uuid(); f.seconds = 34.5; f.flag = true; f.note = "horn crack bar 34"; f.pinnedAt = juce::Time (1762250000000);
            FlagReply r; r.text = "fixed, version 1"; r.by = "Fixer"; r.at = juce::Time (1762250060000); f.replies.push_back (r);
            f.name = flagName (f.note, f.replies); w.markers.push_back (f);
            FixVersion v; v.id = juce::Uuid(); v.takeId = juce::Uuid(); v.takeNumber = 4; v.startSample = 1000; v.endSample = 49000; v.sampleRate = 96000.0;
            FixFile ff; ff.trackId = p.tracks[1].id; ff.trackName = "Soloist"; ff.file = tmp.getChildFile ("Recorded Media/First half/Fixes/004 fix 1 - Soloist.wav"); ff.numChannels = 1;
            v.files.push_back (ff); v.what = "De-click (sensitivity 6)"; v.note = "the click at 0:34"; v.flagId = f.id; v.versionNumber = 2; v.replaces = juce::Uuid();
            v.by = "Fixer"; v.madeAt = juce::Time (1762250090000); w.fixVersions.push_back (v);
            FixVersion first; first.id = juce::Uuid(); first.takeNumber = 5; first.startSample = 10; first.endSample = 20;   // (0.4.0: a first attempt that answers no flag)
            first.what = "ReTouch window: ReTouch"; first.by = "Fixer"; w.fixVersions.push_back (first);
        }
        p.syncMixers();
        if (! p.mixers.empty())                                          // 3.31.0: the strips' polarity key (one strip inverted, the other not)
            if (auto* st = p.mixers.front()->stripFor (p.tracks[1].id)) st->polarity.set (true);
        juce::String err;
        const bool saved = p.save (err);
        check (saved, "the project is saved" + (err.isEmpty() ? juce::String() : " (" + err + ")"));
        const auto root = juce::JSON::parse (p.projectFile);
        check (root["format"].toString() == compat::kProjectFormatName, "its file says \"format\": \"" + root["format"].toString() + "\"");
        check ((int) root["version"] == compat::kProjectFormatVersion, "its file says \"version\": " + root["version"].toString());
        Project q;
        err.clear();
        const bool loaded = q.load (p.projectFile, err);
        check (loaded, "it is read back" + (err.isEmpty() ? juce::String() : " (" + err + ")"));
        check (q.sampleRate == 96000.0, "its sample rate (96 kHz) is kept");
        check (q.tracks.size() == 2 && q.tracks[0].name == "Main pair" && q.tracks[0].format == TrackFormat::Stereo
                 && q.tracks[1].name == "Soloist" && q.tracks[1].format == TrackFormat::Mono, "its tracks are kept");
        check (! q.takeWindows.empty() && q.takeWindows.front()->name == "First half", "its take window is kept");
        {
            auto* inv = q.mixers.empty() ? nullptr : q.mixers.front()->stripFor (q.tracks[1].id);
            auto* nor = q.mixers.empty() ? nullptr : q.mixers.front()->stripFor (q.tracks[0].id);
            check (inv != nullptr && inv->polarity.get() && nor != nullptr && ! nor->polarity.get(), "a strip's polarity key is kept (3.31.0: on for one strip, off for the other)");
        }
        if (! q.takeWindows.empty() && ! p.takeWindows.empty())
        {
            const auto& a = *p.takeWindows.front(); const auto& b = *q.takeWindows.front();
            check (b.markers.size() == 2 && ! b.markers[0].flag && b.markers[0].name == "3" && b.markers[0].note.isEmpty(), "an ordinary marker stays an ordinary marker");
            const bool flagOk = b.markers.size() == 2 && b.markers[1].flag && b.markers[1].note == "horn crack bar 34" && b.markers[1].pinnedAt == a.markers[1].pinnedAt
                                && b.markers[1].replies.size() == 1 && b.markers[1].replies[0].text == "fixed, version 1" && b.markers[1].replies[0].by == "Fixer"
                                && b.markers[1].replies[0].at == a.markers[1].replies[0].at && b.markers[1].name == "Flag: horn crack bar 34 | Fixer: fixed, version 1";
            check (flagOk, "a flag keeps its note, its time and the Fixer's reply (contract 3)");
            const bool fixOk = b.fixVersions.size() == 2 && b.fixVersions[0].id == a.fixVersions[0].id && b.fixVersions[0].takeId == a.fixVersions[0].takeId
                               && b.fixVersions[0].takeNumber == 4 && b.fixVersions[0].startSample == 1000 && b.fixVersions[0].endSample == 49000
                               && b.fixVersions[0].sampleRate == 96000.0 && b.fixVersions[0].files.size() == 1 && b.fixVersions[0].files[0].trackName == "Soloist"
                               && b.fixVersions[0].files[0].file == a.fixVersions[0].files[0].file && b.fixVersions[0].what == "De-click (sensitivity 6)"
                               && b.fixVersions[0].flagId == a.markers[1].id && b.fixVersions[0].versionNumber == 2 && b.fixVersions[0].replaces == a.fixVersions[0].replaces
                               && b.fixVersions[0].madeAt == a.fixVersions[0].madeAt && b.fixVersions[0].state == 0;
            check (fixOk, "a fix version keeps its take, span, files, flag and attempt number (contract 3)");
            check (b.fixVersions.size() == 2 && b.fixVersions[1].flagId.isNull() && b.fixVersions[1].replaces.isNull() && b.fixVersions[1].takeId.isNull()
                     && a.fixVersions[1].flagId.isNull() && a.fixVersions[1].replaces.isNull(),
                   "a fix version that answers no flag, a first attempt, keeps no flag and no earlier attempt (0.4.0: they were made-up ids)");
            // and in the file itself: the ordinary marker has none of the flag's keys
            const auto wf = tmp.getChildFile ("Take Windows").findChildFiles (juce::File::findFiles, false, "*.fmtake");
            bool plainKeys = false;
            for (auto& file : wf)
            {
                const auto parsed = juce::JSON::parse (file);                // (kept: the list below points into it)
                if (auto* ms = parsed["window"]["markers"].getArray())
                    for (auto& m : *ms) if (m["name"].toString() == "3") plainKeys = ! m.hasProperty ("flag") && ! m.hasProperty ("note") && ! m.hasProperty ("replies");
            }
            check (plainKeys, "the ordinary marker is written as before (no flag keys in its take window file)");
        }
        tmp.deleteRecursively();
    }

    std::cout << (failures == 0 ? "COMPATIBILITY CHECK PASSED" : "COMPATIBILITY CHECK FAILED: " + std::to_string (failures) + " problem(s)") << std::endl;
    return failures == 0 ? 0 : 1;
}
