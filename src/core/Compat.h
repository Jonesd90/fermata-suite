#pragma once
/** The compatibility contract between fermata and fermata:Live, in code (fermata 3.30.2 / fermata:Live 0.1.0). COMPATIBILITY.md says the same in
    words, and tests/compat_test.cpp checks that the two agree: change a number here and the check fails until COMPATIBILITY.md says the same (and
    the other way round). Everything here is used by both programs - see COMPATIBILITY.md before changing it. */

namespace td::compat
{
/** The contract's own number: raised whenever a rule both programs must follow changes (a new file format they share, a changed one). */
constexpr int kContractVersion = 1;

/** The project file: what its "format" and "version" say. It is written and read by src/core/Project.cpp - the same code in both programs - and
    its "version" has been 1 from the start because the file only ever grows: keys are added, never renamed or given a new meaning, and a key that
    is missing means its old default. A change that breaks that rule must raise kProjectFormatVersion AND teach both programs to read both. */
constexpr const char* kProjectFormatName = "Fermata project";
constexpr int kProjectFormatVersion = 1;

// The sample rates both programs offer: Project::kProjectRates (src/core/Project.h) - one list, never a copy.
// The live recording's chunk index, its flags and the Fixer's replies get their format names and numbers here when fermata:Live's recorder is built.
} // namespace td::compat
