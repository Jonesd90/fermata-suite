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

## 0.2.0: the recorder core

### The shape of it
- **Two processes.** `src/live/Main.cpp`: with `--recorder <port>` the program is the recorder (`RecorderService`): no window, the audio device, the
  session, the merge, a control port on 127.0.0.1 (the shared `remote::Hub`, JSON lines, one object per line). Without it, the window
  (`RecorderWindow`), whose `RecorderLink` connects to the port and starts a recorder when none answers (the default port is 57120; `--port n`
  changes both). A recorder that is still running from before (the window was closed while recording) is found on the same port and carried on
  with. Only the recorder opens the audio device: ASIO allows one process. `juce::Process::setPriority (HighPriority)` in the recorder.
- **The timeline** (`LiveRecorder`): sample 0 is the first block after prepare(); the audio callback pushes every block into the `Ring`; every
  drive's `DriveWriter` thread reads the ring at its own pace and writes chunks. Record = a `TakeRec` {start, recordPoint, end} noted in every
  drive's index (`SessionIndex`), nothing more; Stop sets `end`. So the only thing that can disturb a recording is the machine.
- **The ring** (`Ring.h`): one float ring per channel, overwritten by the audio thread whether or not every drive has read; a reader validates
  its copy afterwards against the writer's published `claimed` / `head` positions (acquire loads) and, when it has fallen more than the ring's
  length behind, is moved to half a ring behind the writer with a gap to record. The copies are deliberately unsynchronised (a lossy broadcast
  ring); `tools/tsan-suppressions.txt` says so for ThreadSanitizer, and the ring test checks that no torn or stale copy is ever accepted. The
  ring's size: bufferSeconds (2 - 60) x rate x channels floats, kept within a quarter of the machine's memory (`Setup::memoryBudgetBytes`).
- **The drive writer** (`DriveWriter`): minimum writes of 0.5 s (few, big writes, as fermata's TakeWriter), pieces of at most 32768 frames,
  never past a chunk boundary; a chunk closes as soon as the audio up to its end is there (not held back by the minimum write). Header refresh
  every 10 s. On open, the chunk is listed in the index with samples = -1 and the index written; on close its length is set, free space checked
  (below 500 MB: dropped), old chunks deleted, the index written again. A write failure anywhere drops the drive (`drop`): files closed as they
  are, index "dropped" with the reason, the owner told. The index is written with `filesync::replaceTextSafely` (a complete file or the old one,
  .bak kept) through the `Disk` layer so the tests can break it.
- **Gaps**: a reader overrun closes the chunk, notes a `GapRec` {first, samples, time of day, why}, and opens the next chunk at the oldest audio
  still in the ring (an eighth of the ring behind the newest): nothing is written for the gap itself - a drive that has just shown it cannot keep
  up is not given a buffer's worth of silence to write - and the merge fills the hole with silence. A dropped-and-resumed drive, or a resumed
  session, gets a GapRec for the stretch it missed the same way. The merge joins a hole and the drive's own note of it into one gap.
- **The index and the drive**: an index write that fails (a scanner or OneDrive holding session.json for a moment, a read falling between the
  writer's two renames) never drops the drive: the write is tried again (at most every 2 s), the audio goes on, and the count shows in the status
  (`indexFailures`). Only an AUDIO write or open that fails, or free space under two chunks' worth (or 500 MB), drops a drive. `SessionIndex::load`
  repairs nothing unless asked (`repair = true`, only for a session whose writer is gone); a reader of a live index must never move its files.
- **The close**: `requestClose()` then `waitForClose()`; every drive drains at once, for as long as it keeps making progress (a drive that writes
  nothing for a minute at the end is dropped). A writer still inside a disk call after three minutes is left alive for good, never killed (a thread
  killed inside the heap or a file call can hang the whole process). `stalledMs()` shows a drive that has stopped taking audio while there is
  audio to take; the status carries it as `stalledSeconds`.
- **Chunk files**: `<stem>.c000012.wav` where 12 = firstSample / chunkSamples; a drive that begins again inside chunk 12 writes `c000012-r1`.
  Each is a complete BWF WAV (originator "fermata:Live", time reference = the time of day of its first sample, description "<window> / live chunk
  <name> / <track>"), so a chunk is usable on its own by any program. The session folder: `<project>/Recorded Media/<window>/Live Chunks/<yyyy-mm-dd
  hh-mm-ss>`; on a backup drive `<backup>/<project folder name>/Recorded Media/<window>/Live Chunks/<the same>`.
- **The merge** (`Merge`): the take's span as pieces (chunks clipped to it, holes between: `Merge::pieces`); each chunk's real length is measured
  from its files (`chunkLength`: WavRepair mends a header the recorder never finished), never more than the index says; writes `<name>.merging.wav`
  with fermata's metadata, then `verifyFile` reads it back against the chunks sample for sample, then renames (never over an existing file:
  "(2)"). Progress per track goes to the index's `mergedFiles` (`saveProgress`, kept also when the merge fails), so a cancelled or crashed merge
  resumes from the next track. Waits while a take records (`paused`). **A live drive is merged only once it has written the whole take**: the
  merge refuses (`allowOpenChunks = false`) while the drive's `lastSample` is before the take's end or a chunk inside the take is still open, and
  the service waits for that as long as it takes - a drive that gets dropped meanwhile, or a session that is over, is merged from what is there
  (`allowOpenChunks = true`, holes as gaps). The merge of a backup takes the take's span from the recorder (a dropped drive's index may lack its
  end) and the drive's own merged state (`takeOnDrive`); backups keep their chunks until merged (their index takes carry their own merged flag,
  not the primary's: `LiveRecorder::updateTake`, and a backup taken on again or a session carried on reads that state from the drive's own index).
  The result (files, length, gaps) is kept in the take's record (`mergedSamples`, `mergeGaps`), so the take can be put into the project later from
  the index alone.
- **The merge thread and the session**: the merge job holds the session (`std::shared_ptr<LiveRecorder>`) and talks to it only through its
  thread-safe calls (`driveIndex`, `recordingTake`, `updateTake...`) - no round trips to the message thread, nothing captured by reference across
  threads. A session that is ended while a merge runs stays alive, closed, until the merge is over; the merge's result still reaches the drive's
  index (`DriveWriter::updateIndex` writes it there and then once the drive thread has ended). `end-session`, `close-device`, `open` and `quit`
  are refused while a take records; `quit` also while a merge is queued or running (the window leaves the recorder running in both cases and says
  so). A merge already queued or running for a take is not queued again (`mergeQueuedFor`).
- **Into the project** (`TakeRegister`): the take window by name (made if missing), the project track by id, else by name, else made; a
  `TakeGroup` as `AudioEngine::startRecording` makes one (number, label when it differs from the window's name, startSeconds after the last
  take + 2 s, lengthSamples, recordedAt, lostSamples, overruns = gaps, notes), and a `Marker` per gap on the window's timeline; the same take
  registered again keeps its place and loses its old gap markers. The window does this on the merge's "done" event - which carries everything it
  needs (the window, the rate, the tracks, the files, the gaps), so the window never reads a live drive's index - and saves the project (shared
  `Project::save`), then tells the recorder (`registered`). A take merged while the window was away (or before a project was open in it) shows in
  the status as merged and not registered: the window asks for its result again (`take-info`) and registers it.
- **Resume** (`Setup::resume`): the newest session folder's index is read; the timeline continues at `max (lastSample, (now - origin) x rate)`;
  chunks left open are measured and closed in the index (`DriveWriter` constructor); takes come back (one still recording keeps recording). The
  service merges, on resume, every take that ended but was never merged.
- **The protocol** (`RecorderService::handleLine`): commands `hello, status, devices, open {type, device, rate, buffer (0 = the largest the driver
  offers)}, close-device, session {projectName, projectFolder, window, tracks [{id, name, channels, inputs}], backups [], preTakeSeconds,
  bufferSeconds, chunkSeconds, resume}, end-session, record {number, label}, stop, registered {number}, take-info {number, folder?},
  merge-backup {folder, number}, resume-drive {folder, role}, quit`. Folders must be full paths. `open`, `close-device`, `end-session` and `quit`
  are refused while a take records (`refused` event); `quit` also while a merge is going; `record` refuses a take number already used in the
  session. Events: `hello, status` (every second: device, session with takes and drives, merge), `meters` (20 Hz, the inputs' peaks), `log,
  refused, device, session, take {state recording | stopped | registered | refused}, drive (dropped), gap, merge {queued | started | progress | done
  | failed | refused}` - "done" with the files, gaps, length, window, rate and tracks.
- **The window and a recorder that died**: `RecorderLink` starts a new recorder when none answers; on the first status after a reconnection the
  window puts back what the new recorder lacks (the device, then the session with `resume`) without waiting for anyone, and says so in the log. A
  reconnection to the same, still-running recorder changes nothing. The link's socket belongs to its thread alone: commands are queued and sent
  from there.
- **"Test signal"** (`TestSignalDevice.h`): a `juce::AudioIODeviceType` of its own with a clocked thread, 16 tones, at any project rate; added to
  the recorder's `AudioDeviceManager`. The end-to-end check here ran the recorder process on it from a Python client: session, take, merge,
  registered, backup merge, end, quit.

### The code review (two readers, after the first version) and what changed
Two reviewers read the recorder core and found 6 serious things, all fixed, and a dozen smaller ones, most fixed:
- A take could be merged, marked merged and its chunks later deleted while the drive had not yet written the take's end (the merge only waited for an
  open chunk, and the writer leaves half a second with no open chunk after every boundary); and the wait for an open chunk gave up after 45 s and
  measured a chunk still being written. Now a live drive is merged only when it has written the whole take (see the merge above), with no time limit.
- A backup drive taken on again, or a session carried on, took the primary's merged flags and would have deleted the backup's chunks of merged takes.
  Now a drive's own index gives its own merged state, and a backup starts unmerged.
- An index write that failed dropped the drive - the write most exposed to a scanner or OneDrive holding the file. Now it never drops a drive.
- The merge thread posted lambdas capturing stack variables by reference to the message thread and stopped waiting after a timeout (memory
  corruption when the message thread was busy, as it is at End session). Gone: the merge talks to the session directly.
- `end-session`, `close-device` and `open` ended a recording take. Now refused while a take records.
- Also fixed: the ring's fences for the C++ memory model and a smaller overrun headroom (an eighth of the ring, not half); a stall no longer writes
  the gap as silence; the free-space check after the rolling deletion and scaled to the chunk size; the close by progress rather than a fixed 30 s,
  and a stuck writer never killed; a damaged index clamps its channel counts; a take number used twice refused; a plain read of a live index never
  "repairs" it; a session folder whose index is momentarily only a .bak is still found; a buffer too big for the memory is an error, not a crash;
  the log files are no longer deleted at every start (4 MB kept) and the recorder's crash goes into the recorder's own log; a failed merge keeps the
  progress of the tracks already done; the same take registered twice keeps its place and loses its old gap markers; the link's socket is used by
  one thread; folders from the window are checked to be full paths; the device start is logged from the message thread.
- Noted, not changed: the device is initialised twice on open (a probe for its channels and buffer sizes, then the real open) - to be watched on the
  MAD driver; a hung disk write is shown (`stalledSeconds`) but cannot be interrupted; the resumed timeline comes from the PC clock (the drift
  between it and the audio clock is the join's error); the ring is not locked in memory (VirtualLock) and is zero-filled on the message thread at
  session start; the minimum write at 192 kHz is 32768 frames (0.17 s), the piece size, not 0.5 s.

### Checked here
- `tools/live-check.sh`: `tests/live_recorder_test.cpp` - the ring (no torn copy accepted over 20-30 million samples with thousands of overruns);
  a session on two drives with a take and pre-take (the merge refused until the drive has written the take's end), merged from either drive sample
  for sample, fermata's names and BWF fields, chunk deletion, the index round trip, the take into a project and back; a drive that fails (dropped,
  the other goes on, its own merge gives what it has with a hole); a drive that stalls (a gap, the other untouched); the program dying mid-chunk
  and the session carried on (the cut-off chunk mended, the gap, the merge); pre-take and rolling deletion; the merge cancelled and resumed, a
  tampered merged file found; a backup dropped and taken on again keeping its own chunks of a take the primary has merged, a take number refused
  twice; an index that cannot be written leaving the recording untouched and catching up later; the damaged index falling back to its .bak, a
  plain read repairing nothing. About 13 s.
- `tools/tool-checks.sh live`: the same under AddressSanitizer (0 reports), ThreadSanitizer (0 reports with the ring suppression) and the
  realtime guard on `LiveRecorder::process` (see below for the result).
- The window: started on a virtual screen, it starts the recorder, connects, lists the devices (picture in the delivery).
- `tools/syntax-check-ui.sh` (both programs), `tools/check-windows-code.sh`, `tools/compat-check.sh`, `tools/sandbox-check.sh`.

### Not checked here (needs Windows and the MAD driver)
- ASIO on the real driver, the channel counts, the largest buffer size the driver offers, the time a real drive takes, a USB drive pulled out
  during a take, the process priority, the recorder surviving the window being killed (Task Manager), the "Carry on last session" after a real
  crash, and the window's file choosers. The test bench exists for exactly these.

### Known limits (0.2.0)
- No monitoring yet: the recorder's outputs are silence. The main window, Primary's live monitoring and the Fixer come in the next builds.
- One take window per session (the window's "Take window" box); the session must be ended and started again to change it.
- A session's backups are chosen at its start; a drive added later (`resume-drive`) is taken on from that moment with a gap before.
- The merge waits for a take to end but runs alongside the pre-take writing (low thread priority, one track at a time).
- A project open in fermata and fermata:Live at the same time is not prevented (see COMPATIBILITY.md).

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
