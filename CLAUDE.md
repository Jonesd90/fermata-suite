# Working in this repository

This repository holds **two programs built from one shared core**. **Read `COMPATIBILITY.md` first**: it says which code is shared and gives
the rules that keep the programs compatible.

- **fermata**: the studio recording and editing program. Its own code is `src/Main.cpp`, `src/ui/` and `src/reharmoniser/`; its version is in
  `CMakeLists.txt`; its history is in `CHANGELOG.md`; its notes are in `docs/DEVELOPMENT-NOTES.md`.
- **fermata:Live**: the live-concert recorder (`fermata-Live.exe`). Its own code is `src/live/`; its version is in `src/live/Live.cmake`; its history
  is in `LIVE-CHANGELOG.md`; its brief is `docs/LIVE-BRIEF.md` and its notes are in `docs/LIVE-DEVELOPMENT-NOTES.md`.
- **shared**: `src/core/` and the other files listed in `COMPATIBILITY.md`. These are built into both programs.

The user works on each program in a conversation of its own. Work out from the request which program this conversation is for, and keep to that
program's folders. Change shared code when the work needs it, but follow `COMPATIBILITY.md`, and say so in the delivery (see below).

## Staying in step with the other conversation

The repository on GitHub is the truth. The user uploads every delivery zip there by hand, and the zips contain only the files that changed, so a
zip made from an out-of-date copy would put old versions of files back.
- **Before starting, and again just before making a delivery zip,** fetch the latest from GitHub and build on it. If a file you are delivering
  changed on GitHub since you started, merge the two versions. Never deliver a file without that merge.
- Never edit the other program's own folders. Keep changes to the files both programs touch (`CMakeLists.txt`, `cmake/SharedCore.cmake`,
  `COMPATIBILITY.md`, the GitHub workflow) small.

## Before every delivery

Run these on Linux against JUCE 8.0.9 (`git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git`):
- `tools/compat-check.sh <JUCE>`: the compatibility check. It must end with `COMPATIBILITY CHECK PASSED`.
- `tools/syntax-check-ui.sh <JUCE>`: compiles both programs' window code (src/ui, src/Main.cpp, src/live).
- `tools/check-windows-code.sh <JUCE>`: the Windows-only code, and names that Windows' headers break.
- `tools/sandbox-check.sh <JUCE>`: the shared core's tests (long; run it in the background).
- `tools/live-check.sh <JUCE>`: fermata:Live's recorder core tests (fake audio, drives that fail and stall); `tools/tool-checks.sh <JUCE> live`
  runs them under the address, thread and realtime checks.

The programs themselves only build on Windows (MSVC), on GitHub: every upload builds both and runs the compatibility check (`.github/workflows/build-windows.yml`).
Say plainly what could and could not be checked here.

## Deliveries

- A zip of **only the files changed** since the previous delivery, with paths from the repository's top folder. The user uploads the contents over
  the repository. **If the GitHub workflow (`.github/workflows/build-windows.yml`) changed, it goes in the zip too**, so one upload makes one build
  (asked for on 4 November 2026: a workflow given separately meant a second upload and a second build). GitHub's web upload takes fewer than 100
  files at a time, so split a bigger delivery into numbered parts.
  If a delivery deletes or renames a file, list it in the message.
- Raise the program's version (MAJOR.MINOR.PATCH) and add its history entry. Give one line saying what to test in that build.
- **If shared code changed, say so in one line** ("Shared: ... changed - the other program needs a rebuild and test"), and start the history entry's
  line with **Shared:**.

## Names

The user writes the names as **fermata** (no capital) and **fermata:Live** (one capital L). Windows allows no colon in a file name, so fermata:Live's
files use `fermata-Live`. fermata's own windows still say "Fermata" (unchanged as of 3.30.2): do not rename anything unless the user asks.
