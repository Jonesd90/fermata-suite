// The compatibility check between fermata and fermata:Live (fermata 3.30.2 / fermata:Live 0.1.0). Built and run by tools/compat-check.sh, by
// tools/sandbox-check.sh and by the GitHub build on every upload. It checks that:
//   - COMPATIBILITY.md and the shared code (src/core/Compat.h, Project.h) give the same numbers: the contract, the project file, the sample rates;
//   - every file COMPATIBILITY.md lists as shared is there;
//   - the shared code uses nothing from either program's own folders (it is built into both, so it may only use itself);
//   - a project written by the shared code - as either program writes it - says it is a "Fermata project" of the contract's version, and reads
//     back the same.
// When fermata:Live's recorder arrives, its files (the chunk index, the flags, the Fixer's replies) are checked here too: a live recording must open in
// fermata as a take window.
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
        tmp.deleteRecursively();
    }

    std::cout << (failures == 0 ? "COMPATIBILITY CHECK PASSED" : "COMPATIBILITY CHECK FAILED: " + std::to_string (failures) + " problem(s)") << std::endl;
    return failures == 0 ? 0 : 1;
}
