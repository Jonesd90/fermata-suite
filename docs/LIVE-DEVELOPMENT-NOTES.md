# fermata:Live: development notes

The brief is in `docs/LIVE-BRIEF.md`, and the rules shared with fermata are in `COMPATIBILITY.md`. Newest entries come first.

## Agreed with the user (4 November 2026)

- **The name**: **fermata:Live** on screen (one capital L), `fermata-Live` in file names, because Windows allows no colon there.
- **Sample rates**: the same as fermata, `Project::kProjectRates`. The channel count at each rate comes from the driver.
- **One repository, one shared core**: fermata and fermata:Live are built together, and both are built by every GitHub build. The two programs are
  worked on in separate conversations that stay in step through the repository (`CLAUDE.md`). The old fermata repository is kept as it was.
- **The stages**, as planned with the user:
  1. **The recorder core**: a small process of its own, running at high priority, that only takes in and writes audio. It covers the 30-second
     chunks and their index, writing to several drives at once (dropping a drive that fails, with a warning), the memory buffer, gap markers when
     every drive stalls, the merge after recording (interruptible, checked against the chunks before they are deleted) and the pre-take on disk.
     Planned for Fable.
  2. **The Primary window**: the live take window, the flags with their notes, and the main window with the inputs, gain dials, preamps and talkback.
  3. **The Fixer window**: the delayed timeline, Follow Primary, replies, a mixer of its own, and ReTouch.
  4. **Plug-ins in a separate helper process** for monitoring, and Bounce out.
  5. **A load-test program** for the user to run on the studio PC (both stations on one PC, ReTouch during a recording, every drive busy).
- **What can only be tested on the user's hardware**: real stability, the buffer length, the MAD driver's channel counts, and whether one PC can run
  both stations. These checks run on Linux, without ASIO or Ravenna.

## 0.1.0: the frame

- `src/live/Main.cpp`: the program and one window showing its name, its version, the sample rates (read from the shared core) and the contract
  version. There is no audio yet. Only one copy of the program can run at a time (`moreThanOneInstanceAllowed` is false), because two recorders
  would fight over the driver.
- `src/live/Live.cmake`: the program's build lines. They use the shared core's file list, JUCE settings and Windows drivers
  (`cmake/SharedCore.cmake`), so fermata:Live is built from the same core, with the same settings, as fermata.
- `FERMATA_VERSION_STRING` is "Live 0.1.0" in this program, because the shared audio engine writes "Fermata " plus that text into recorded files. The
  window uses `FERMATA_LIVE_VERSION_STRING`.
- The icon is fermata's with its dot in red (`assets/fermata-live-icon.png`). It is a placeholder until the user picks one.

### For the recorder build (stage 1)

- **A project lock.** Nothing stops fermata and fermata:Live from opening the same project at once. Add a lock in the shared code (`src/core`) so
  both programs respect it (see `COMPATIBILITY.md`, "Still to decide").
- **Where the recording lives in the project.** Live takes should be ordinary takes in a take window as the shared model already knows them, so
  fermata opens them as they are. The chunk index, flags, replies and fix versions need places in the shared model and in its loading and saving
  (`src/core/Project.cpp`), and the compatibility check must show fermata reading them (`tests/compat_test.cpp`).
- **What the shared code already gives.** `src/core/TakeWriter.h` and `src/core/WavRepair.h` (recordings mended after a crash), `src/core/CoreReservation.*`
  (keeping the audio on chosen cores), `src/core/Ravenna.*`, `src/core/Preamp*.h`, `src/core/Workers.h`, `src/core/CrashReport.*` and
  `src/core/BackupCopier.h` (the second drive), all already used by fermata.
- **No name of the program in a file or folder name.** Use `kFileName` ("fermata-Live") or a name without a colon. JUCE's
  `getApplicationName()` returns "fermata:Live".
