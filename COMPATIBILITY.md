# fermata and fermata:Live: the compatibility contract

This repository builds **two programs from one shared core**:

| Program | Its own code (only its own work changes this) | Its version number is set in |
|---|---|---|
| **fermata**, the studio recording and editing program | `src/Main.cpp`, `src/ui/`, `src/reharmoniser/` | `CMakeLists.txt` (`FERMATA_VERSION`), history in `CHANGELOG.md` |
| **fermata:Live**, the live-concert recorder (file name `fermata-Live.exe`) | `src/live/` | `src/live/Live.cmake` (`FERMATA_LIVE_VERSION`), history in `LIVE-CHANGELOG.md` |
| **shared**, built into both | see [Shared code](#shared-code) below | `src/core/Compat.h` (the contract's numbers) |

The two programs are worked on separately, often in separate conversations. They must always be able to open each other's projects. This file
lists what they share and the rules that keep them compatible. **Read it before changing anything shared.**
`tests/compat_test.cpp` checks the numbers in the box below against the code, so the two cannot drift apart without the check failing.

## The numbers

These lines are checked by `tests/compat_test.cpp`. Keep each line's wording exactly as it is and change only the values, together with the code
they name (`src/core/Compat.h`, `Project::kProjectRates` in `src/core/Project.h`).

```text
Contract version: 2
Project file: "Fermata project", version 1
Sample rates (Hz): 44100 48000 96000 192000
Live session index: "fermata:Live session", version 1
```

## Shared code

Everything listed here is built into BOTH programs, so a change to any of it changes both.

- `src/core/`: every file in it. That covers the project file and its takes, windows and mixers; the audio engine, playback and recording; the
  preamps; ReTouch's, Denoise's and De-click's processing; the workers and CPU core reservation; the control port (`RemoteHub.h`, which the
  recorder process also listens on); and the crash reports. Its `.cpp` files are listed once, in `cmake/SharedCore.cmake`.
- `cmake/SharedCore.cmake`: that list, plus the JUCE settings and Windows audio drivers (ASIO, WASAPI) that both programs are built with.
- `src/reharmoniser/third_party/pocketfft_hdronly.h`: the FFT the shared denoiser uses (`src/core/DenoiseFft.cpp`).
- `third_party/signalsmith-stretch/`: the time-stretcher the shared ReTimEr code uses (`src/core/Retime.cpp`).
- `tests/compat_test.cpp`, `tools/compat-check.sh`, `COMPATIBILITY.md`, `CLAUDE.md`: the check, and the rules themselves.

A file from one program's own folder joins this list as soon as the other program uses it. For example, fermata's look, its mixer strip and its preamp
buttons will join when fermata:Live's windows are built. From then on it is shared, and the rules below apply to it.

## The rules

1. **Share, never copy.** When fermata:Live needs something fermata has, it uses fermata's file and that file joins the shared list. A copy would
   drift apart from the original without anyone noticing.
2. **Shared code uses only shared code.** Nothing in `src/core` includes or calls a program's own code (`src/ui`, `src/live`, `src/Main.cpp`,
   Re-HarmoniSer's own files), because the other program does not have it. The check enforces this. A program hands the shared code what it needs
   through the shared code's own interfaces (`std::function` hooks, abstract classes), as fermata already does with its plug-in hosting.
3. **One project file, read and written only by the shared code.** Each program rebuilds the project file from what it understands when it saves,
   and anything it does not understand is dropped. So everything either program keeps in a project must be in the shared model and in its loading
   and saving in `src/core/Project.cpp`, never in a program's own code. This includes fermata:Live's takes, flags, Fixer replies and fix versions.
   Otherwise the other program would silently lose them the first time it saved.
4. **The project file only grows.** Keys are added and never renamed, removed or given a new meaning. A key that is missing means its old default,
   so an older project still opens. That is why its version is still 1. A change that cannot follow this rule raises the project file's version (in
   `src/core/Compat.h` and the box above), and both programs must then read both versions.
5. **One list of sample rates.** Both programs offer `Project::kProjectRates` and nothing else. The number of channels at each rate comes from the
   audio driver and is never written into the code.
6. **A change to shared code keeps both programs building and the checks passing**: `tools/compat-check.sh`, `tools/sandbox-check.sh`, and the
   syntax checks of both programs' windows (see `CLAUDE.md`). The GitHub build builds both programs and runs the compatibility check on every upload.
7. **Numbers change on purpose.** A new shared file format, or a change to one, gets its name and number in `src/core/Compat.h` and a line in the
   box above, raises the contract version, and gets a check in `tests/compat_test.cpp` showing that the other program reads it. The check fails
   until all of these agree.
8. **Each program keeps its own version and history.** fermata's version is in `CMakeLists.txt` and its history in `CHANGELOG.md`. fermata:Live's
   version is in `src/live/Live.cmake` and its history in `LIVE-CHANGELOG.md`. A change to shared code goes in the history of the program whose
   work it was, under a line that starts **Shared:**, so the other program's next build knows to mention it.

## The files the programs share

| File | What it is | Written and read by |
|---|---|---|
| `<project>.fermata` | the project file (JSON), `"format": "Fermata project"` | `src/core/Project.cpp` |
| `Take Windows/*.fmtake`, `Edit Windows/*.fmedit`, `Mixer Windows/*.fmmix` | each take window, edit and mixer, in a file of its own | `src/core/Project.cpp` |
| `Recorded Media/*.wav` | the recordings: one WAV (BWF) per track per take | `src/core/TakeWriter.h`, `src/core/AudioEngine.cpp` |
| `Recorded Media/<window>/Live Chunks/<session>/` | a live recording as fermata:Live writes it (contract 2): `session.json`, the index (`"format": "fermata:Live session"`), and the chunk files `<track>.c000012.wav`, 30 seconds each, complete BWF WAVs | `src/live/SessionIndex.h`, `src/live/DriveWriter.cpp` - written and read by fermata:Live only |

**The Live Chunks folder (contract 2).** fermata:Live records every session in chunks, in a `Live Chunks` folder inside the take window's
folder (and in the same place on each backup drive, under a folder named after the project). After Stop it merges a take's chunks into ordinary
take files in the take window's folder - named and tagged exactly as fermata records them (`005 - First half - Main pair.wav`, 24-bit BWF with
the time of day and the "<window> / <take> / <track>" description) - checks them sample for sample against the chunks, and puts the take into the
project with the shared `Project` code (`src/live/TakeRegister.h`): a take group with its files, length, time, and a marker per gap. So fermata
opens a live recording as its own, with nothing to read in the index. fermata must leave a `Live Chunks` folder alone: it holds recordings not yet
merged (fermata:Live merges them when it next opens the project), and the chunks of merged takes go by themselves. "Create copy of entire project"
copies it like everything else, which is fine.

Still to come, each with its numbers here and a check in `tests/compat_test.cpp`: the Primary's flags and notes, the Fixer's replies, and the fix
versions (every fix as a version of its own, reviewed in fermata one at a time).

## Still to decide

- **A project open in two programs at once.** Nothing stops fermata and fermata:Live from opening the same project together today, and their saves
  would overwrite each other. A project lock in the shared code, which both programs respect, is still to be added (fermata:Live's Primary window
  build is the natural place). Until then: open a project in one program at a time.
- **The program's name in recorded files.** fermata:Live's own recorder writes "fermata:Live <version>" as the software and "Fermata" as the BWF
  originator (as fermata does, so re-linking reads the files the same way). The shared audio engine is not used for recording by fermata:Live.

## History

- **Contract 1** (fermata 3.30.2, fermata:Live 0.1.0): the repository holds both programs. The shared core is `src/core` as it was in fermata
  3.30.1, with nothing in it changed. Added: `src/core/Compat.h`, this file, the check, and the frame of fermata:Live.
- **Contract 2** (fermata 3.30.2, fermata:Live 0.2.0): the live session index and the Live Chunks folder (above). `src/core/Compat.h` names the
  index's format; nothing else in the shared core changed.
