# fermata and fermata:Live: the compatibility contract

This repository builds **two programs from one shared core**:

| Program | Its own code (only its own work changes this) | Its version number is set in |
|---|---|---|
| **fermata**, the studio recording and editing program | `src/Main.cpp`, `src/ui/`, `src/reharmoniser/` | `CMakeLists.txt` (`FERMATA_VERSION`), history in `CHANGELOG.md` |
| **fermata:Live**, the live-concert recorder (file name `fermata-Live.exe`) | `src/live/` | `src/live/Live.cmake` (`FERMATA_LIVE_VERSION`), history in `LIVE-CHANGELOG.md` |
| **shared**, built into both | see [Shared code](#shared-code) below | `src/core/Compat.h` (the contract's numbers) |

The two programs are worked on together (one conversation for both, since 6 November 2026) but built and released separately, each with its own version. They must always be able to open each other's projects. This file
lists what they share and the rules that keep them compatible. **Read it before changing anything shared.**
`tests/compat_test.cpp` checks the numbers in the box below against the code, so the two cannot drift apart without the check failing.

## The numbers

These lines are checked by `tests/compat_test.cpp`. Keep each line's wording exactly as it is and change only the values, together with the code
they name (`src/core/Compat.h`, `Project::kProjectRates` in `src/core/Project.h`).

```text
Contract version: 3
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
- `src/reharmoniser/third_party/pocketfft_hdronly.h`: the FFT the shared denoiser uses (`src/core/DenoiseFft.cpp`), and (fermata 3.37.0) the spectral
  pictures' and the ReTouch fixes' (`dsp::FastFFT`, in the same file).
- `third_party/signalsmith-stretch/`: the time-stretcher the shared ReTimEr code uses (`src/core/Retime.cpp`).
- `tests/compat_test.cpp`, `tools/compat-check.sh`, `COMPATIBILITY.md`, `CLAUDE.md`: the check, and the rules themselves.
- **fermata's look** (contract 3: fermata:Live's windows are drawn with it). These files stay in fermata's folder, unchanged, and are shared from
  there: `src/ui/Uikit.h`, `src/ui/FermataLookAndFeel.h`, `src/ui/FontData.cpp`, `src/ui/FontData2.cpp`, `src/ui/WaveColor.h`, `src/ui/WaveStore.h`,
  `src/ui/WaveThumb.h`, `src/ui/WinOwner.h`, `src/ui/WinOwner.cpp`, `src/ui/MixerLook.h`, `src/ui/DeskLook.h`, `src/ui/ValueEntry.h`,
  `src/ui/WaveDraw.h`, `src/ui/MarkerStrip.h`, `src/ui/TimeReadout.h`, `src/ui/TalkbackGlow.h`, `src/ui/BusyWindow.h`. Each of them may include only
  the others in this list and `src/core` (never `AppContext.h` or any other file of fermata's windows), and fermata:Live includes nothing else from
  `src/ui`. The check enforces both. A change to one of them changes how both programs look: build and look at both.
- **fermata's closing log and closing safety net** (fermata 3.15.0 / 3.17.0; shared from fermata:Live 0.3.1, which uses them in its window, its
  recorder and its Fixer): `src/ui/ClosingLog.h`. It stays in fermata's folder, unchanged, under the same rules as the look files above. A change to
  it changes how both programs close. (Its safety net's log lines name "Fermata" in fermata:Live's logs too.)
- **fermata's ReTouch window** (fermata 3.35.0; shared from fermata:Live 0.4.0, whose Fixer opens it - the Primary has none): `src/ui/SpectralWindow.h`.
  The spectral picture with its selection tools (Rectangle, Time, Brush, feathering) and harmonics, and the window round it (ReTouch, Gain,
  De-click, Denoise, Consonants). It stays in fermata's folder under the same rules as the look files above (it includes only them and `src/core`).
  It knows neither program: each hands it a `RepairContext` - how to read the audio, make the fixes, play them and keep them (fermata: new files
  in the take or edit; the Fixer: a fix version). A change to it changes both programs' ReTouch: build and try both.

A file from one program's own folder joins this list as soon as the other program uses it (as fermata's look did in contract 3). From then on it is
shared, and the rules below apply to it.

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

**Flags, replies and fix versions (contract 3).** They live in the project file, read and written by the shared code, so fermata keeps them:
- **A flag** is a take window marker (`Marker` in `src/core/Markers.h`) with `flag` set, the Primary's `note`, the time it was pinned
  (`pinnedAt`) and the Fixer's `replies` (text, who, when). Its `name` is `flagName (note, replies)` - "Flag: horn crack bar 34 | Fixer: fixed" - so
  fermata shows it as an ordinary marker. An ordinary marker is saved exactly as before (the new keys are only written for a flag).
- **A note** (fermata:Live 0.5.0, still contract 3) is a take window marker with `liveNote` set: a station's own jotting, its text in `note`, who
  wrote it (`by`, the name the station was given) and its station (`station`: "Primary" or "Fixer"), and `pinnedAt`. Its `name` is
  `noteName (text, by)` - "Note (Sam): the cello's chair creaks" - so fermata shows it as an ordinary marker. A flag also keeps `by` and `station`
  from 0.5.0. The keys are written only when set, so an older flag and an ordinary marker are saved exactly as before.
- **A fix version** is an entry of the take window's `fixVersions` (`FixVersion` in `src/core/Takes.h`): the take, the span it replaces (the
  take's samples), one file per track holding the fixed span, what was done, the flag it answers, the version it retries, and its `state`
  (0 waiting for review, 1 approved, 2 not used). The recording itself is never changed. **fermata's part, still to be built:** a review that goes
  through the versions one at a time, plays each against the original, and sets `state` (approve, or ask the Fixer for another attempt); an
  approved version is put into an edit the way fermata puts any fix in. Until then fermata keeps the versions as they are.
- fermata:Live's recorder also keeps the flags and versions of a session in its index (`flags`, `fixes`), so nothing is lost if the Primary's
  window is closed while the Fixer works; the Primary's window puts them into the project.
- **Reading a project another program writes**: `Project::readOnlyLoads` (off unless a program sets it) makes `Project::load` mend nothing on
  disk - a file it cannot read is read from its `.bak`, but neither is moved or copied (`filesync::readJsonWithBackup`'s `repair`). fermata:Live's
  Fixer sets it: a read that fell between the Primary's two renames of a save would otherwise put the previous save back. fermata does not set it.

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
- **Contract 3** (fermata 3.30.2, fermata:Live 0.3.0): flags (with notes and replies) on take window markers, fix versions in take windows (above),
  and fermata's look shared with fermata:Live (the `src/ui` files listed under Shared code). fermata's own behaviour is unchanged: an ordinary marker
  and a take window without fix versions are saved byte for byte as before.
  - Still contract 3 (fermata 3.35.0, fermata:Live 0.4.0): fermata's ReTouch window (`src/ui/SpectralWindow.h`) joined the shared files, for the
    Fixer. No file format changed, so the number stays.
  - Still contract 3 (fermata 3.39.0): surround Int and Ext Buses (`docs/SURROUND.md`). The project file gained keys, none changed: a bus's
    `"format"` ("9.1.6" ...; missing = stereo, as every bus was), a send's `"surroundPan"` and an output's `"outSurroundPan"` (missing = the
    defaults), an Ext Bus's `"hpFirst"` (missing = -1). fermata:Live reads and writes them through the shared code and never looks at them: its own
    mixer only uses a project's first mixer's main Ext Bus, which is always stereo (a surround Ext Bus is never a main output). The shared engine
    runs a stereo project exactly as before.
  - Still contract 3 (fermata:Live 0.5.0): notes on take window markers (`liveNote`, `by`, `station`; above). The project file gained keys, none
    changed, written only when set. fermata keeps them when it saves (through `src/core/Project.cpp`) and shows a note as an ordinary marker.
    fermata's WaveColour analysis of one block became `WaveColorCache::BlockAnalyser` (`src/ui/WaveColor.h`), unchanged in what it gives, so
    fermata:Live colours its live waveforms the same way.
  - Still contract 3 (fermata 3.40.0): the post-fader slot. The project file gained keys, none changed: the project's `"postFaderSlots"` (true;
    missing = off) and a strip's or bus's `"postSlot"` (a list of one slot, as `"slots"`; missing = empty). Both are written only when used, so a
    project that does not use them is saved as before, and the `"slots"` list still holds the four slots before the fader (an older version reads a
    project with a post-fader plug-in as one without it). The shared engine runs a project without the switch exactly as before; fermata:Live has no
    switch for it.
  - Still contract 3 (fermata 3.40.0): the Atmos tab. The mastering settings gained one key, `"atmos"` (the ticked buses, the LFE low-pass and its
    frequency, dither, the folder), written only once something differs from the defaults, and `"view"` can now be 3 (an older fermata clamps it
    to its own tabs). New shared file `src/core/AdmWriter.h / .cpp`, in `cmake/SharedCore.cmake`, used by fermata's Mastering window only.
