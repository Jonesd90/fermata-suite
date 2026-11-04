#pragma once
/** The compatibility contract between fermata and fermata:Live, in code (fermata 3.30.2 / fermata:Live 0.3.0). COMPATIBILITY.md says the same in
    words, and tests/compat_test.cpp checks that the two agree: change a number here and the check fails until COMPATIBILITY.md says the same (and
    the other way round). Everything here is used by both programs - see COMPATIBILITY.md before changing it. */

namespace td::compat
{
/** The contract's own number: raised whenever a rule both programs must follow changes (a new file format they share, a changed one). */
constexpr int kContractVersion = 3;

/** The project file: what its "format" and "version" say. It is written and read by src/core/Project.cpp - the same code in both programs - and
    its "version" has been 1 from the start because the file only ever grows: keys are added, never renamed or given a new meaning, and a key that
    is missing means its old default. A change that breaks that rule must raise kProjectFormatVersion AND teach both programs to read both. */
constexpr const char* kProjectFormatName = "Fermata project";
constexpr int kProjectFormatVersion = 1;

/** Contract 2: fermata:Live's recording session index (session.json beside the chunks of a live recording, see src/live/SessionIndex.h): the
    timeline of 30-second chunks, the gaps and the takes as spans of it. fermata:Live merges the chunks of a take into ordinary take files (named and
    tagged exactly as fermata records them) and puts the take into the project's take window with the shared Project code, so fermata never needs
    to read the index itself - but it must leave a "Live Chunks" folder alone (it holds recordings not yet merged), and the format is named here so
    that any program that does read it can tell what it has. */
constexpr const char* kLiveSessionFormatName = "fermata:Live session";
constexpr int kLiveSessionFormatVersion = 1;

/** Contract 3: the Primary's flags (with their notes and the Fixer's replies) are take window markers with a flag's fields (src/core/Markers.h), and
    the Fixer's fixes are versions in the take window (FixVersion, src/core/Takes.h) - both in the project file, read and written by the shared
    Project code. fermata's look (the src/ui files listed in COMPATIBILITY.md) is shared too. Nothing here changes the project file's version: the
    file only grew. */

// The sample rates both programs offer: Project::kProjectRates (src/core/Project.h) - one list, never a copy.
} // namespace td::compat
