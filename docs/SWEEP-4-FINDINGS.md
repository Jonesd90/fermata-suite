# The fourth code sweep: findings (started 9 November 2026, Claude Fable, from fermata 3.46.0 / fermata:Live 0.7.0)

The brief is `docs/CODE-SWEEP-BRIEF.md` (written for the third sweep; this one follows its areas, and adds fermata:Live, which did not exist then).
The person's choice (9 November, 21:20): areas A, B, fermata:Live's recorder and C back to back at xhigh, nothing delivered between them - clear
bugs are fixed as they are found, each with a test where one can be written; anything that would change how a feature behaves, or the project
file format, is HELD and listed for the person to decide. One combined zip after C. Then areas D to I and the rest of fermata:Live at high.

How to read this file: one section per area. Each finding says what could go wrong for the person, where it is in the code, and its state:
FIXED (with the test's name), HELD (the person decides), or NOTED (looked at, no change wanted). "Read" lists the files read line by line.

Progress (update as each area ends): A, B, the Live recorder, C - done and delivered (fermata 3.46.1 / fermata:Live 0.7.1, all six checks passed). D, E, F, G, H, fermata:Live's windows and I - done: the sweep is finished. Their fixes, and what the person decided on 10 November (D8, D9, LW9, LW10, I1), are in fermata 3.47.0 / fermata:Live 0.8.0. Nothing is left open.

---

## Area A: the audio thread, playback and threads; starting and closing

### Read

`core/AudioEngine.h`, `core/AudioEngine.cpp`, `core/Automation.h`, `core/MixerWorkers.h`, `core/HostTransport.h`, `core/Playback.h`, `core/Playback.cpp`,
`core/RateBridge.h`, `core/Ltc.h`, `core/Ltc.cpp`, `core/PitchPipe.h`, `core/Dsp.h`, `core/SessionClock.h`, `core/TalkbackKey.h`, `core/Workers.h`,
`core/Common.h`, `core/CoreReservation.h`, `core/CoreReservation.cpp`, `core/CrashReport.h`, `core/CrashReport.cpp`, `core/Surround.h`, `Main.cpp`,
`ui/WinOwner.h`, `ui/WinOwner.cpp`, `ui/ClosingLog.h`, `ui/Splash.h`, `ui/AppContext.h`, `ui/AppContext.cpp`, `ui/StallWatch.h`, `ui/DeviceGuard.h`, `ui/HelpMenu.h`,
`ui/TalkbackGlow.h`, `ui/CpuCoresPanel.h`, `ui/ClickWindow.h`, `ui/PitchPipeWindow.h`, `ui/PitchPipeWindow.cpp`

### Findings

Most serious first.

- **A6 FIXED - closing right after Stop could cut off the end of a take.** `Main.cpp shutdown()` starts the closing safety net (it ends the program
  15 s later) straight after `AppContext::shutdown()`, but the files of a take that had just been stopped were only waited for later, in
  `~AudioEngine` (`waitForFinishedTakes (60000)`, then 10 s for the second drive's copies). On a drive that is behind, what is still in the
  recording buffer is written by the finisher thread - and the safety net could end the program in the middle of that. Now
  `AppContext::finishTakesBeforeClosing()` waits for the takes and the copies (the same limits) before the last save and before the safety net
  starts, marks a take whose file refused its last moments (`takeInFinishedTakes (false)`), and tells the stall watch that this wait is meant
  (`StallWatch::touch()`). No test here (the windows' code does not run on Linux); the engine calls it uses are covered by the core tests.
- **A1 FIXED - session mode (Pre): a device block bigger than the engine was prepared for put old sound into the take.** `AudioEngine::process`
  left such a block out of the pre-roll ring but still moved the ring's position on, so the take's files got what the ring held there (the sound
  of some seconds before) in place of the block. The block is now written whatever its size (silence for an input that is missing).
  Test: core_test "3.46.1 session mode (Pre): a device block bigger than the engine was prepared for ..." (fails on the old engine, both tracks).
- **A7 FIXED - a new project that could not be made left a broken project open.** `AppContext::newProject`: when the project file cannot be
  written, the program now goes back to the start panel (`startBlankProject()`) and says so, instead of staying as an empty project that points
  at a file that is not there (every automatic save failed from then on). No test here (windows' code).
- **A8 FIXED (the person's decision, 9 November) - Record while a ReTouch or Pitch window is trying something out.** In that state
  (`saveHold > 0`) the project is not saved at Record or at Stop, so a new take was in no project file until the fix window closed. New, Open,
  Close and Quit refused there; Record did not (it can only be reached from the Stream Deck or the controller: the fix window has the
  keyboard). `AppContext::toggleRecord` now refuses with the same sentence. fermata only: in fermata:Live the Fixer fixes while the Primary
  records, by design. No test here (windows' code).
- A2 FIXED (cannot happen today) `AudioEngine::process`, engineer audition: `pl->mixers.front()` was reached with an audition mixer set even if
  the plan had no mixers. Guarded.
- A3 NOTED `AudioEngine::currentGroupId` keeps the id of the last take after recording stops. Every caller of `currentTakeId()` was looked at
  (`AppContext.cpp`, `Main.cpp`, `TakeWindow.cpp`, `TakeDisplay.cpp`, `TakeChart.h`): each one asks `isRecording()` first.
- A4 NOTED idle wake-ups: `RingBuilder::run` wakes every 500 ms and `Finisher::run` every second with nothing to do; the stall watch 50 times
  a second (its job). Left.
- A5 NOTED `LivePeaks::kMaxSeconds` = 3 hours: the waveform of a take being recorded stops growing on the screen after three hours (the recording
  goes on, and the whole waveform is drawn from the file after Stop). Left: fermata:Live is the program for a take that long.
- A9 NOTED for H: `followAutomation` (20 times a second) works out a number from every point of every edit each time, also with nothing
  happening: small for a few thousand points; to be measured with a big project.
- A10 NOTED `crashreport::describeThreads` holds each thread for a moment to read where it is. Nothing is allocated while a thread is held; the
  only lock it needs is Windows' own table of functions. Left as it is.
- A11 NOTED `sanitiseForFile` (Common.h) takes out the characters Windows refuses, but not the names Windows keeps for itself (CON, NUL,
  COM1 ...) nor a full stop at the end. A take's file name starts with its number, so it is never such a name; a take window or edit named
  exactly CON / AUX / NUL would get a window file Windows refuses, and the save says so. Not changed: the function is also what finds existing
  files by name (re-linking), so changing what it returns would stop files being found.
- A12 NOTED for I: `AppContext::restoreLastProject()` is not called from anywhere.
- A13 NOTED `ClickTrack` / `PitchPipeVoices` / `RateBridge` / `MixerWorkers` / `PlaybackSession` / LTC: read for locks, allocation and bounds on
  the audio thread - nothing found. `Project::load` puts the open project's lists back when a load fails (read here because `openProject`
  depends on it).

Also in this build, asked for by the person during the sweep (not a sweep finding): the Mastering window's "Number the files" uses each piece's
number in the list (`makeFilesSpec`, `listNo`), also for the track-number tag. Test: core_test, after "an edit left out".

---

## Area B: recording and saving

### Read

`core/TakeWriter.h`, `core/BackupCopier.h`, `core/WavRepair.h`, `core/SaveWriter.h`, `core/FileSync.h`, `core/LostSamples.h`, `core/Takes.h`,
`core/Project.h`, `core/Project.cpp`, `core/ProjectCopy.h`, `core/AudioRelink.h`, `core/Preamp.h`, `core/PreampMemory.h`, `core/Markers.h`,
`core/Ravenna.h`, `core/Ravenna.cpp`, `ui/RecordMonitor.h`, `ui/RecPrePanel.h`, `ui/PreampSetup.h`, `ui/ProjectRateDialog.h`, `ui/MediaWindow.h`,
`ui/MediaWindow.cpp`, `ui/OrganiserWindow.h`, `ui/OrganiserWindow.cpp`, `ui/DesignWindow.h`, `ui/DesignWindow.cpp`, `ui/GridEditor.h` (the Design
window's tables), `ui/MainWindow.h`, `ui/MainWindow.cpp`; and from other areas, where B's code leans on them: `MasterDef.cpp` (sync / toVar /
fromVar), `AudioEngine::startRecording` (file names, the second drive's folder).

### Findings

Most serious first.

- **B7 FIXED - New project with the name of a project that is already there replaced that project's file.** The file chooser's "replace it?"
  question looks at the name typed (`Documents/Fermata/Name.fermata`), but the project file is made inside a folder of that name
  (`.../Name/Name.fermata`), so nothing asked: `AppContext::newProject` wrote an empty project over the first one's project file (its `.bak` held
  the old one until the next save, a moment later, replaced that too). The default name is "New project", so pressing New project and accepting
  the name twice did it. The takes' audio, and the take-window / edit / mixer files, were never touched (those are never written over), but the
  project file - the design, the input names, the mastering list, which windows belong to it - was gone. Now `AppContext::newProjectProblem`
  refuses (the project file, its `.bak`, or the old `.takedaw` of that name is there): said in the New project dialog before the sample rate is
  asked, and again in `newProject` itself. No test here (windows' code); the check is three lines.
- **B1 FIXED - the second drive: a file of the same name that was already there was deleted.** `BackupCopier::service` did `dst.deleteFile()`
  before it wrote a take's copy, and a take that never started (`cancel`) deleted it too. A project of the same name on another drive - or one
  started again after its main drive failed - gives its takes the same file names, so recording take 1 again deleted the second drive's copy of
  the first take 1: perhaps the only copy left. Now a file that is already there is moved aside as "<name> (earlier copy).wav" (never deleted,
  never written over; if it cannot be moved, the new copy takes another name), and a copy only ever deletes a file it made itself (`Job::mine`).
  Also: when the second drive's folder is the folder the project's own folder is in, the "copy" would be the recording's own file - that is now
  said (the 2ND lamp and the notice: "no second copy"), and nothing is written.
  Test: core_test "3.46.1 the second drive: a file of the same name that is already there ..." (4 checks fail on the old copier).
- **B4 FIXED - an edit whose file could not be read lost its mastering entry, its CD clip and its alignment at the next save.** When a `.fmedit`
  is missing or unreadable at open, the edit is left out and the person is told to put the file back. But `MasteringDef::sync` then dropped the
  edit's Virtual Master entry (file name, tags) and CD clip (ISRC, pause), the save dropped its alignment set (`alignSets` are kept only for
  windows that exist), and a new window of the same name took the missing file's very name. Putting the file back brought the edit back without
  any of that. Now the entries wait (`Project::parkedMasterItems` / `parkedDdpClips`, written back at their old places by `toVar`), alignment sets
  of missing windows are kept, and a missing window's file name is not given to another window (`planWindowFiles`). Nothing new in the file format.
  Test: core_test "3.46.1 a window whose file is missing: its mastering entries and alignment wait for it ...".
- **B8 FIXED - a preamp change waiting to be sent when the connection dropped was sent when it came back.** `RavennaDevice::outgoing` was not
  emptied when a device's connection ended, so a gain / 48 V change made in the last moment before the drop went to the device at the next
  connection - seconds or hours later, perhaps during a take. Emptied with the connection now (`RavennaDevice::run`). No test (needs a device).
- **B6 FIXED - importing a mixer or window file with a track or bus that has no id never ended.** `Project::importMixerFile` /
  `importWindowFile` replace old ids by new ones in the file's text; an empty old id made `String::replace ("", ...)` loop for ever (the program
  hung). Ids shorter than 8 characters are now left alone.
- **B9 FIXED - the Media window listed whole drives every 3 seconds.** `MediaComponent::signature` (on the screen's thread, every 3 s while the
  window is open and nothing plays) listed every root folder to the bottom - and the folder of any file that lives outside the project is a
  root: a file imported from `D:\` or from a music library made it list all of that. Only the project's own folder is listed to the bottom now;
  the others one level deep. Also: a file that lives somewhere new is highlighted at the first click (it used to need a second one).
- **B2 LEFT AS IT IS (the person's decision, 9 November: another computer means another session or stage of the project).** Opening a
  project on an audio device with fewer channels forgets settings for good: `Project::syncWithDevice` cuts the lists of inputs and outputs down
  to the device's, and the next save writes that - the names and stored preamp settings of the inputs above that number, and at the next open
  the control-room mic inputs, talkback outputs and the pitch pipe's / click's outputs above it. Tracks keep their inputs and buses their
  outputs. Not changed.
- B10 FIXED (small, the Design window and its tables): a value typed into a table row that has just gone (the audio device changed, an undo) was
  written past the end of the project's list (`GridEditor::Cell::commit` now checks the row); "Open selected mixer" from a menu checks its row;
  Up on the first bus / Down on the last no longer takes the selection away. The Display Organiser's list of layouts checks its rows the same
  way (`paintListBoxItem`, `listBoxItemClicked`). The cue-mixer Delete menu looks at the list again after the menu closes.
- B11 FIXED (the person's decision, 9 November): the sample-rate box (`ProjectRateDialog.h`) had Cancel on the left and "Create the project"
  on the right; now the action is on the left and Cancel on the right, as in every other box.
- B3 NOTE for G: numbers read from a project file that are not limited: a take's bars (`barIn` / `barOut`, `pieceBars`: the screen limits them to
  99999, the file does not - `coverage::counts` makes a list that long), `outFirst`, a track's inputs, `nextNumber`, a file's channel count, how
  deep versions are nested, a relative path with ".." in it.
- B12 NOTED for H: the free disk space is asked on the screen's thread every 2 seconds (`MainComponent::timerCallback`): a network drive that
  has gone can hold the screen while Windows waits for it. `InputRow::tick` makes a small list per input 30 times a second.
- B13 NOTED for D: `moveTakeClips` / `removeEmptyTracks` do not look at frozen edit versions or the guide.
- B14 NOTED: `repairInterruptedRecordings` mends the header of an interrupted recording also when the file is outside the project's folder (in
  place; the length is only extended for the project's own files). `ProjectCopy` compares paths by their letters, so a path saved with other
  capitals is copied twice into "From elsewhere" (right, but bigger). A damaged window file that is read from its `.bak` can leave the last take
  out of the project (the person is told at open).
- B15 NOTED: 21 places take the screen's size from JUCE without asking whether a screen exists (`getPrimaryDisplay()->`,
  `getDisplayForRect (...)->`). JUCE on Windows always lists at least one (the desktop itself when no monitor answers), so these are safe there.
- B16 NOTED: `TakeWriter`, `WavRepair`, `FileSync`, `SaveWriter`, `AudioRelink`, `PreampMemory`, `LostSamples`, `Markers`, the recording monitor,
  the Rec / Pre panel, the preamp set-up window: read for a full or vanished drive, a crash half way, locks and threads - nothing found. A take's
  file never replaces an existing file (`startRecording` picks the next free name); a window's file never replaces a file that is not its own.

---

## fermata:Live's recorder (the recorder process and its core)

### Read

`live/Ring.h`, `live/SessionIndex.h`, `live/DriveWriter.h`, `live/DriveWriter.cpp`, `live/LiveRecorder.h`, `live/LiveRecorder.cpp`, `live/Merge.h`,
`live/Merge.cpp`, `live/ChunkTimeline.h`, `live/MonitorMix.h`, `live/MonitorMix.cpp`, `live/StreamProtocol.h`, `live/StreamServer.h`,
`live/StreamServer.cpp`, `live/StreamClient.h`, `live/PeakTap.h`, `live/TakeRegister.h`, `live/RecorderLink.h`, `live/RecorderLink.cpp`,
`live/RecorderService.h`, `live/RecorderService.cpp`, `live/Main.cpp`, `core/RemoteHub.h` (the control port), `tests/live_recorder_test.cpp`.
The windows (`PrimaryApp`, `FixerApp`, the take views, the mixer, the second seat ...) come later, with the rest of fermata:Live.

### Findings

Most serious first. The recording path itself (the audio callback into the ring, each drive's own thread out of it) was read for locks, waits,
allocation and bounds: nothing found. What was found is round it.

- **L1 FIXED - what was noted while a session closed reached no drive's index.** `LiveRecorder::close` took the drive writers out of the session's
  list for as long as it waited for them to finish (a moment normally; minutes on a slow drive). A flag or a note pinned meanwhile, or a take
  marked merged or "in the project" by a merge that ended meanwhile, was kept in memory only and never written to any index - gone if the
  recorder then closed, and the take merged a second time at the next start. The writers now stay in the list while they drain.
  Test: live_recorder_test "0.7.1: a flag pinned and a take marked merged while the session closes still reach every drive's index".
- **L2 FIXED - an index update at the very end of a drive's writing could be lost, and two updates after the session could cross.**
  `DriveWriter::updateIndex` wrote the index itself only once the drive's thread was seen to have ended; an update that arrived while the thread
  was writing its last index was written by nobody. And after the session, two callers (the merge and a station's note) could write at the same
  moment and leave the older state on the disk. The thread now hands the index over as its last step, under a lock, with nothing unwritten
  (`handoverLock` / `threadDone`), and later writes go one at a time. The last write ("the session ended") is tried up to ten times when
  something holds `session.json` for a moment. Covered by the same test (40 flags from two threads after the session: all 40 in both indexes).
- **L3 FIXED - taking a dropped drive on again while its old writer was still inside the drive.** `LiveRecorder::resumeDrive` waited 2 s for the
  dropped writer's thread and then destroyed the writer whether or not the thread had ended. Destroying it waits for that thread for as long as
  the drive holds it (a failing disk can take a minute to answer) - with the session's lock held, so the status, Record's note of the take and
  every other call waited behind it - and the old thread could then write its "dropped" index over the new writer's. Now the drive is not taken
  on until the old writer has ended: "That drive is not answering yet ... try again in a moment". (The audio and the other drives were never
  held up either way.) Test: live_recorder_test "0.7.1: a dropped drive is not taken on again while its old writer is still inside the drive".
- **L4 FIXED - an error inside one drive's writer, the waveform or the recorder's timer would have ended the recorder.** An exception that leaves
  a thread ends the whole program - and the recorder is the program that must not end. `DriveWriter::run` now drops that one drive (the others
  and the audio go on); `PeakTap::run` (the waveform for the stations, whose memory grows with the session) stops making the waveform; the
  50 ms timer (`RecorderService::timerCallback`: meters, status, waveform points) skips that round. The commands (`handleLine`) and the merge
  already did this. No test (it needs the machine to run out of memory).
- **L5 FIXED - the merge measured chunks that are no part of the take.** `Merge::pieces` asked for the length of every chunk before the take's
  end - also the thousands before the take in a long session (a file opened per track per chunk), and, when the take ended in a hole, the chunk
  after it, which may be the one the drive is writing (measuring mends a header that looks unfinished: never to be tried on a file being
  written; Windows refuses it, but the code should not ask). Only the chunks of the span are measured now.
  Test: live_recorder_test "0.7.1: the merge measures only the chunks of the take's span ...".
  (The three new tests were also run against the recorder as it was in 0.7.0: 9 checks fail there, none now.)
- L6 NOTED: `Ring::push` assumes a device block is never longer than the whole ring (at least 2 s of audio, normally 30 to 60 s): true for any
  driver. Left as it is (the audio path stays as simple as it is).
- L7 NOTED: what a station sends to be heard (`StreamServer::mix`) is added to the outputs as it comes - a mix gone wrong in a station (a
  plug-in putting out nonsense) is heard as such. Monitoring only; nothing of it can reach the recording.
- L8 NOTED: at the end of a session a drive with less free space than the next chunk would need is marked "dropped: less than ... free" as its
  last chunk closes, although everything is on it. A truthful warning (the drive is nearly full), shown at an odd moment.
- L9 NOTED: if the recorder stops between giving a merged file its name and writing that into the index, the next merge writes the take again
  as "... (2).wav" and the take can appear twice in the take window. A narrow window; nothing is lost.
- L10 NOTED: `RecorderLink` (the window's end of the control port) treats a send that cannot be taken at once as a lost connection and drops
  what it was sending; commands are a few hundred bytes on the PC's own loopback, so this does not arise.
- L11 NOTED: `MonitorMix` (the lists handed to the audio thread), `StreamServer` (slots, generations, the two-block wait before a slot is used
  again), `PeakTap`, `RemoteHub` (one thread per connection, queues with a ceiling): read for lifetimes and locks - nothing found.

---

## Area C: files that leave fermata and come back

### Read

`core/RxWatch.h`, `core/StereoJoin.h`, `core/AudioOps.h`, `core/AudioOps.cpp`, `core/ImportPlan.h`, `core/ImportPlan.cpp`, `core/Aaf.h`, `core/Aaf.cpp`,
`core/AafModel.h` (generated tables), `ui/RxLink.h`, `ui/RxLink.cpp`, `ui/FixTools.h`, `ui/FixTools.cpp`, `ui/FixToolsCommon.h`, `ui/FixToolsDialogs.h`,
`ui/FixToolsEdits.cpp`; and, followed from them, `core/Edit.h` (the waiting pieces and the frozen versions), `core/EditFix.h` (`applyRegionJobs`),
the save path in `ui/AppContext.cpp` and `Project::prepareSave`.

### Findings

Most serious first.

- **C1 FIXED - Noise mode: Undo, or "Throw them away", could leave the saved project naming audio that had been deleted.** Every fix of the
  Noise mode window is in the edit the moment it is made, and - unlike the ReTouch and Pitch windows, whose previews hold the automatic save
  back (`PreviewApplied`, 3.27.1) - the automatic save writes it into the project file. Undo (`takeOffLast`), a nudge of a pasted consonant
  (taken off and put in again each time) and closing with "Throw them away" (`discardUncommitted`) then deleted the fix's files there and then.
  Until the next save (30 seconds to 5 minutes later; never, after "Quit without saving" or "Close without saving") the project file named
  files that were gone: a crash, a power cut or a quit in that time, and the edit opened with holes where the fixes had been. The original
  takes were never touched, but each hole had to be mended by hand. Now those files go to `AppContext::deleteOnceSaved` (`ui/OnceSavedBin.h`):
  they are deleted when a save whose snapshot was taken after they left the edit is on the disk; if no such save is ever made they stay,
  where the project file that names them can find them. Test: core_test "3.46.1 a file taken out of the project is deleted only once a save
  made WITHOUT it is on the disk (Noise mode's Undo)" (the list's rules). The wiring - `saveNow`, `savedInBackground`, Noise mode - is window
  code: compiled here by the syntax check, not run. To try on Windows: Noise mode, one fix, wait about a minute, Undo, close the window and
  "Quit without saving" at once; open the project again - the edit plays whole (the fix is in it again, as the last automatic save had it).
- **C2 FIXED - "Make a stereo track" left the frozen versions of an edit on tracks that were gone.** The stereo join (3.23.0) rewrites every
  take, piece, mixer and lane of the two tracks and cannot be undone. The frozen versions of an edit (Edit 1, Edit 2 ...: 3.42.0) came later
  and the join did not know of them: their pieces stayed on the two tracks, which no longer exist - silent when the version was compared,
  listened to or put back. First it was made to refuse; then, by the person's decision (C8, 9 November), to convert them: `stereojoin::plan`
  lists the versions' pieces too (`PieceUse::version`), a version's piece plays the same new file as the edit's piece of the same id or the
  same audio (the file made longer when the version's piece needs more of it), `apply` converts each version's lists, guide and lanes as it
  does the edit's (`convertLists`) and the mixer settings kept with it (`joinedMix`: the stereo strip with the left track's fader, input gain,
  output, VCAs and sends, as a live mixer's new strip starts). A version frozen while the files are written is caught (its pieces are not in
  the plan). Test: core_test, the stereo-join section ("3.46.1: the frozen versions of an edit ... are converted with it").
- **C3 FIXED - two fixes could write silence and report success.** `audioops::spliceFile` (putting corrected audio into a take: "Re-link
  corrected audio", a save brought in from a linked program) did not look at the result of reading the corrected file, and
  `audioops::fixWholeFile` (a fix written as a whole new take file) not at that of reading the take: a read that failed in the middle - a
  drive dropping out - put silence into the new file, and the fix was reported as made. Both now stop with an error and delete the half-made
  file. No test: a read that fails in the middle of a file cannot be provoked from a test here; the two lines follow the pattern of the
  checked reads beside them.
- **C4 FIXED - importing an AAF with the audio inside it froze the program, and could end it.** `Aaf.cpp Cfb::open` reads the whole file into
  memory, on the screen's thread. An AAF that names its audio files is a few megabytes; one with embedded audio (Pro Tools offers it) is as
  big as the session's audio. Reading several gigabytes froze the window for as long as that took, and with too little memory ended the
  program (`juce::MemoryBlock` throws). A file over 1 GB is now refused before anything is read, with what to do ("Export the AAF again with
  the audio linked, not embedded ..."), and running out of memory under that is caught. Test: core_test "3.46.1: an AAF with the audio inside
  it ..." (a sparse file of 1 GB and one byte: refused in under half a second).
- **C5 FIXED - a file deleted by hand made every later save from a linked program arrive half a minute late.** `RxSaveWatch::ready` counts a
  watched file that is missing as "being replaced" (a program that writes a new file and renames it leaves the path empty for an instant) and
  holds the other saves back for it, up to `maxHold` looks. A file that stays away - deleted in Explorer once its work was done, while its
  track still follows it - counted as that for ever, so each later save of any other file came back 30 looks late instead of at once. Now
  only for 5 looks in a row (`kMissingLooks`). Test: core_test "3.46.1: a watched file that stays away ...".
- C6 FIXED (small): the multitrack import (Pyramix names) left a file out without a word when its take already had a file for the same
  track; the import box now says which file, and why. No test (a line of text).
- **C7 FIXED (the person's decision, 9 November) - "Re-link corrected audio" in an edit made the piece play the file in Processing Media
  itself.** The take-side re-link splices a copy; the edit-side re-link by hand (`FixTools.cpp relinkEdit`) pointed the piece at the very file
  the other program saves over: saving it again (with another length, say) changed or broke the edit with no undo step, and the job's folder
  could never be cleared. It now copies the files into the project's Fixes folder ("<name> (re-linked).wav", on a job thread, each copy checked
  header against bytes) and the piece plays the copies. **A correction to what was first written here:** the linked-programs path did copy each
  save ("(saved N)"), but beside the original in the Processing Media job folder, not into Fixes. Those copies now go to Fixes too
  (`RxLink.cpp copyFor`), so nothing an edit plays lives in Processing Media any more. Copies made by earlier versions stay where they are and
  go on playing. No test here (windows' code); the core calls are covered.
- C8 DONE (the person's decision, 9 November): "Make a stereo track" converts the frozen versions - see C2.
- C9 NOTED: bringing a save in from a linked program (`RxLink.cpp bringIn`) - length, sample rate and channels checked, the copy named
  "(saved N)", one undo step, skipped while recording, while a window tries something out, while a box is open, with the mouse down, and in
  the Trim window - read closely, nothing found. The watcher asks the disk about every watched file once a second on the screen's thread
  (idle cost: for area H).
- C10 NOTED: `Project::rxSeen` is pruned at every save to the files something still waits for or follows (3.27.1); `RxSaveWatch::pending`,
  `tries` and the new `missing` are pruned to the files watched.
- C11 NOTED: `audioops::uniqueFile` never hands a name out twice in one run (a set that only grows: one short string per file made - a
  few kilobytes in a long day), so two files of one plan cannot collide; the stereo join's plan relies on it.
- C12 NOTED: `processingJobFolder` hands back an existing folder after 999 jobs of one name in one place (a failed job's clean-up would then
  delete that folder's files). 999 exports of one name: not reachable in practice.
- C13 NOTED: the import box: closed in the last instant of the copy, the copied files stay on the disk in no take (found again by the Media
  window); a take recorded while an import copies can get a number that differs from its file's name. Neither loses anything.
- C14 NOTED: the file-name patterns of the imports (`std::regex` on names) and the AAF reader's bounds are covered by the fuzz tests
  (3.0.0 on); nothing new found reading them.
- C15 NOTED: reads whose result is not looked at, where only a picture or a measurement is made from them (`FixToolsCommon.h`, the `load`
  and `readChannels` of the ReTouch and Pitch windows): a failed read draws silence; the fix itself reads again and checks.
- C16 NOTED: `pitchEdit`'s finish leaves the new files in Fixes when the edit changed under the job (they are not in the edit; the Media
  window lists them as unused). Deleting a kept consonant deletes its audio for good - by design, and it asks first.
- C17 NOTED: "Save project as" / "Copy the whole project" refuse a folder that is not empty, so they cannot write over a project.
- C18 NOTED: the Noise mode, ReTouch and Pitch windows are modal, hold the undo history (`undoHold`) and look the edit up again by its id in
  every callback; the picture threads hold their own copies (`shared_ptr`) and a `SafePointer` to the window - nothing found.

---

## Area D: edit operations and undo

At high effort (the person switched down from xhigh on 9 November, as planned). How it was read (the person agreed to this for the rest of the sweep on 9 November: "the shorter, cheaper version, assuming nothing has been changed"): the code of the operations themselves
and everything written or changed since the last sweeps line by line; the window code that has not changed since the third sweep read it
(3.27.1) by search for the shapes that go wrong (callbacks that outlive their window, files written or deleted, drags that end without their
one undo step), with the paths that change the edit read through; the signal-processing files that have not changed, not again.

### Read

Line by line: `core/Edit.h`, `core/EditFix.h`, `core/EditVersions.h`, `core/DeHum.h`; `ui/EditWindow.cpp` - the version strip (freeze, put back,
remove, the changes lane's menus), the window's own operations (delete, move, snip, glue, marks, place from a take window, remove a track, the
keys), the end of every drag (`mouseUpImpl`, `finishClipMove`, `insertDragMove`) and everything changed since 3.30.1; `ui/SpectralWindow.h` - the
De-hum panel; `ui/TrimWindow.cpp` - the drags; `ui/TakeWindow.cpp` - moving clips between tracks, deleting tracks, the stereo join, the take's
details (bars, notes, dud, room tone, rename, remove), the right-click menus; `core/Project.cpp` - `moveTakeClips`, `trackHasAudio`,
`removeEmptyTracks`, `renameTake`, and every walk over the edits (for the frozen versions).
By their changes since 3.30.1: `core/SpectralPicture.h`, `SpectralRepair.h`, `SpectralPyramid.h`, `Feedback.h`, `DenoiseFft.cpp`,
`ui/VolumeDialog.h`, `ui/TrimWindow.cpp`, `ui/TakeWindow.cpp`, `reharmoniser/ReHarmoniserEditor.cpp` (the job thread of 3.45.1 read through).
By search (see above): `ui/TakeWindow.cpp`, `TrimWindow.cpp`, `TakeDisplay.*`, `TakeChart.h`, `BarCoverage.h`, `VersionsWindow.h`,
`RetimerWindow.cpp`, `AlignWindow.*`, `TakeMatchPanel.h`, `TakeMatchService.h`, `FeedbackUi.h`, `MarkerStrip.h`, the rest of `EditWindow.cpp`.
Unchanged since the third sweep and not read again: `core/Retime.*`, `Consonants.h`, `ConsonantData.h`, `Denoise.h`, `PitchShift.h`, `Tuning.h`,
`TimeAlign.h`, `TakeMatch.*`, the rest of `src/reharmoniser/`.

### Findings

Most serious first.

- **D1 FIXED - an opening fade dragged earlier moved the first piece's volume changes off their music.** When the edit's opening fade is dragged
  to begin before the first piece, the piece itself reaches back to cover it (`EditDef::anchorStart`, 3.17.1; the Trim window's
  `trimEdgeToFade` does the same at the start of the edit). A piece's volume changes are counted from its start - which had just moved earlier -
  and they were not moved with it: every volume change inside the first piece then happened that much earlier in the music than it had been set
  (a fade dragged two seconds earlier: two seconds). In the Trim window's version the level the piece starts with was moved too, so the piece no
  longer had a level of its own and its Level fader read 0. Both now use `rebaseGains`: the changes stay on their music, the piece's own level
  at its start. Test: core_test "3.47.0: the first piece's volume changes stay on their music when it reaches back to its fade" (both ways).
- **D2 FIXED (the person's decision, 9 November: "it all needs to stay with the content, rather than the timeline") - Delete moved the pieces
  after it up, but not what lay over them.** `EditDef::removeRegion` (Delete in the edit window) closed the gap for the later pieces only: the
  overdubs, markers, comment flags, marks 1 / 2, the playhead and automation points fixed in time after it stayed where they were - each the
  deleted piece's length late against its music afterwards. Putting a piece in has moved all of them with the music since 3.5.0
  (`rippleAfter`). Delete now does too, and so do R (`closeGap`) and glue (`glueSnip`, when a moment had been rolled out). A marker that lay
  over the deleted piece itself stays where it is; marks 1 / 2 left the wrong way round by it are cleared.
  **And undo follows:** markers, comment flags, marks 1 / 2 and the playhead are not part of an undo (a marker added is never taken away by
  Ctrl+Z), so undoing a change that had moved them with the music left them where the change had put them - true of a piece put in ever since
  3.5.0, and it would have been true of every Delete now. `restorePiece` (Project.cpp) now keeps each of them ON ITS MUSIC through an undo and
  a redo (`editversions::mapSeconds`: the take and the place in it under it, found in the edit as it comes back); one that the change did not
  move, and that has not been moved since, stays exactly where it is (a piece slid along under a marker leaves the marker alone, and so does
  undoing that). For this each undo snapshot of an edit notes where those things stood (`Project::UndoTimes`: as made, and as of the last look).
  Test: core_test "3.47.0 Delete, R and glue take what lies over the music with them, and an undo brings it back with the music".
  Not changed: sliding pieces along (the edit window's drags, the Trim window's slides with "everything after moves") still leaves markers
  and overdubs where they are - D9.
- **D7 FIXED - a track that a frozen version still plays on could be deleted as "empty".** Moving clips onto another track (the take window's
  "Move its clips onto ...", "Move clips by name") takes the edit's pieces along but leaves the frozen versions as they were; the track the
  clips came from then looked empty (`Project::trackHasAudio` did not look at the versions) and was offered for deleting - "there is no audio
  on it in any take window or edit, so nothing is lost", and deleting a track cannot be undone - while a frozen version's pieces were still on
  it. A frozen version's pieces now count: such a track is not offered and `removeEmptyTracks` leaves it. (fermata:Live's Project Designer
  uses the same question.) Test: core_test "3.47.0: a frozen version of an edit that still plays on a track ...".
- **D8 DECIDED (the person, 10 November: "frozen clips should stay in place") - nothing changed: moving clips to another track does
  not take the frozen versions' pieces along.** `Project::moveTakeClips` moves the takes' clips and the edit's pieces ("the same audio, so the
  same track"); a frozen version's pieces of the same takes stay on the old track. They still play there, but the changes lane then reports
  every such piece as changed, and the old track can never be deleted (D7). The stereo join converts the frozen versions; renaming a take and
  re-linking already reach them. One catch if this is done: Ctrl+Z puts the clips back but never changes a frozen version.
- **D9 DONE (the person, 10 November: "sliding pieces should also move markers and overdubs")** - see "Decided on 10 November" at the end. The
  question was: by the same rule ("with the content"), should what lies over the pieces also follow when pieces are SLID along -
  the edit window's drag with "everything after moves", the Trim window's slides, shuffle and re-ordering? Today only the pieces move.
- D3 NOTED: `EditDef::slipIn` (Trim window: the IN take slid under the join) leaves the piece's later volume changes where they are on the
  timeline, so they are off their music by the amount slid - milliseconds in practice. Not changed: doing it for a slide the other way must fold
  the changes that fall before the new start into the start level without losing a "between the marks" group, in the Trim window's most used
  drag; the risk is bigger than the gain.
- D4 NOTED: putting a frozen version's piece back (`editversions::putBack`, "Put back everything") brings its "waiting for corrected audio"
  state with it, if it was frozen while out for processing (re-link or Stop waiting deals with it); a piece of the edit that is out in a linked
  program and is replaced by a put-back loses its save when it comes (the message says the piece has changed).
- D5 NOTED: the De-Hummer's grouping of tones (`dehum::detail::bestRow`) does not look at its cancel flag; two hundred evenly spaced steady
  tones would make "Find hums" slow. Not seen with real sound (its list is cut to 200 tones first).
- D6 NOTED: the version strip, the ReHarmoniSer's job thread (3.45.1), the volume boxes (3.43.0 / 3.45.0), glue mode (3.35.0) and the guide's own
  size (3.43.0): read for callbacks that outlive their window, undo steps and bounds - nothing found.

---

## Areas E, F, G and H (the shorter way)

The person's choice on 9 November for the rest of the sweep: what has been written or changed since the last sweeps is read; the rest is taken
as read then (the third sweep at 3.27.1, the targeted ones at 3.30.1 and 3.40.1, the mastering section line by line at 3.46.0).

### Read

E (mixers and routing), by its changes since 3.30.1 / 3.40.1: `core/Mixer.h` (slot layouts, the surround pan of a send, the per-channel meters),
`core/LowCut.h`, `ui/Surface.cpp` (the S1's keys and port), `ui/MixerWindow.cpp` (the transport row of 3.43.0, the send dials' typed level, the
Changes key), `ui/PluginHost.cpp` (3.45.1: slow plug-ins named); `ui/MixChangesPanel.h` whole (new in 3.42.0).
F (mastering and exports): read line by line at 3.46.0, two builds ago; nothing changed since but this sweep's own change (the list numbers).
G (readers): the numbers a project file is believed for (B3's list), `Feedback.h`'s new field, the AAF reader's size (C4).
H (drawing and idle cost): `ui/WaveColor.h` and `ui/Uikit.h` by their changes; the version strip's drawing (`EditWindow.cpp`, 3.42.0); the notes
the other areas left for H.

### Findings

- **H1 FIXED - the changes lane wrote every frozen version's mixer out as text ten times a second.** While an edit's changes lane is open,
  the version strip checks at every repaint (10 a second) whether the comparison must be made again. It asked `Project::saveHash` for that,
  which goes through every frozen version and writes each one's mixer settings - the plug-ins' settings with them, megabytes for a full mix -
  out as text to hash it: a steady load on the screen's thread for as long as the lane showed. It now asks `Project::undoHash` of the two
  edits compared: the pieces, overdubs and automation, which is all the comparison reads. No test (windows' code); the syntax check compiles it.
- G1 FIXED (small): a bar number in a project file is believed up to 99999 (what the bars boxes take). A damaged file's bar of two thousand
  million made the take chart count that many bars (`coverage::counts`). Test: core_test "3.47.0 a damaged project file's bar numbers ...".
- E1 NOTED: the mixer's Changes list (`MixChangesPanel`) asks every plug-in of the mixer for its settings twice a second while it is open, to
  see whether anything differs. It is a box that is open for a moment; with heavy plug-ins it may feel slow while it is.
- E2 NOTED: the Changes list's Put back key rebuilds the list from inside its own click (the key is deleted while its click is being
  answered). JUCE allows for that (`Button` checks whether it still exists); nothing reads the key afterwards.
- E3 NOTED: the mixer's transport row (3.43.0) - not while recording; with the edit's or take's window closed it works through the project -
  the send dial's typed level, the surround pan's values, the S1 port choice: nothing found.
- H2 NOTED: the version strip repaints 10 times a second whether or not anything changed (it follows the timeline's scroll and zoom that
  way); with H1 fixed that is cheap. The edit window's own 10 Hz timer counts the edit's takes twice a tick for its info line.
- H3 NOTED (from A, B and C, unchanged): `followAutomation` works through every point of every edit 20 times a second; the free disk space
  is asked on the screen's thread every 2 s (a network drive that has gone can hold the screen); the linked-programs watcher asks the disk
  about every watched file once a second on the screen's thread. None changed: each wants measuring on a big project on Windows first.
- G2 NOTED (B3's list, unchanged): `outFirst`, a track's inputs, `nextNumber`, a file's channel count and how deep versions are nested are
  read from a project file without a ceiling. Each is clamped where it is used, or only costs time; none was found to do harm.

---

## fermata:Live's windows (the Primary's, the Fixer's, the second seat)

Never in a sweep before, so read properly: everything but the drawing (the `paint` functions were read for what they compute, not line by line).

### Read

`live/PrimaryApp.h`, `live/PrimaryApp.cpp`, `live/StationModel.h`, `live/StationModel.cpp`, `live/PeakStore.h`, `live/RecorderLink.cpp`, `live/FixerApp.h`,
`live/FixerApp.cpp`, `live/FixerWindow.h`, `live/FixerWindow.cpp`, `live/Player.h`, `live/TakeLayout.h`, `live/ChunkTimeline.h`, `live/StationNotes.h`,
`live/NotePanel.h`, `live/SecondSeat.h`, `live/SecondSeat.cpp`, `live/LiveMainWindow.h`, `live/LiveMainWindow.cpp`, `live/Main.cpp`, `live/LiveTakeView.h`,
`live/LiveTakeView.cpp`, `live/PrimaryTakeWindow.cpp`, `live/PrimaryWindows.cpp`, `live/LiveMixer.h`, `live/LiveMixer.cpp`, `live/LiveColours.h`,
`live/LiveReport.h`; skimmed: `live/RecorderWindow.cpp` (the test bench), `live/TestSignalDevice.h`, `live/LiveDesk.h` (looks only).

### Findings

Most serious first. None of these can be tried here (the windows only run on Windows); the changed files compile in the syntax check.

- **LW1 FIXED - a new project could take the place of an earlier concert's project.** As B7 in fermata: the file chooser asks "replace it?"
  about the name typed (`Documents/Name.fermata`), but fermata:Live makes a folder and puts the project inside it (`Documents/Name/Name.fermata`).
  A new project with the name of an earlier one wrote an empty project over that one's project file, and the next save over its `.bak`. The
  recordings stayed on the disk, but the project that listed them was gone. `PrimaryApp::newProject` now refuses when a project of that
  name (or its `.bak`) is already there, and says so; the recorder's test bench (`RecorderWindow.cpp setProject`) the same.
- **LW2 FIXED - the second seat could let go without saying so, and the Fixer's keyboard then typed into the Primary's window.** With a Learn
  still waiting for its key (15 s), "Start the second seat" started the seat - and the first key of the seat's own keyboard was then taken as
  the answer to Learn, which ends by switching the driver's filter off. The panel went on saying ON and the orange pointer stayed, but both
  devices were Windows' again: whatever was typed on the Fixer's keyboard went to the window in front. `SecondSeat::start` now ends a Learn
  that is still waiting.
- **LW3 FIXED - "Kept as fix version" was said when the recorder was not there to keep it.** The Fixer went on showing the session it last
  heard of after the recorder stopped answering; keeping a version then wrote its files and said it was kept, and the recorder that came back
  (a new one, with no session) refused it - the fix was lost. `whyNoVersion` now also asks that the recorder is answering: nothing is
  written, and the fix stays to be kept when it is back.
- **LW4 FIXED - the Project Designer's rows.** (a) With a timecode track in the project (one made in fermata) the rows were made again at
  every message from the recorder - about once a second: a name being typed lost the keyboard, an open list closed. (b) Another project
  opened with the same number of tracks kept the old project's rows; leaving one of their name boxes wrote the old project's track name over
  the new project's track. The rows are now made again when the project or its tracks are others (`rowsKey`), and only then.
- **LW5 FIXED - a level typed into a mixer strip (both stations' mixers).** The box closed by itself within half a second (the strip is
  refreshed twice a second, and setting a label's text closes its box), and anything with no number in it - "-inf", a slip of the keys - was
  read as 0: the fader jumped to 0 dB. The box now stays while it is typed in; "-inf" closes the fader; what is no number leaves it alone.
- LW6 FIXED: a new project that could not be saved (a full or read-only drive) stayed as an open, empty project whose every save failed, and a
  session could be started in it. No project is open then, and the message says so (as fermata 3.46.1).
- LW7 FIXED: the second seat took the "pretend Shift" a PS/2 keyboard sends round the arrows, Home, End and the Page keys (Num Lock on) for
  the Shift key: every arrow was Shift + arrow on such a keyboard (a laptop's own). USB keyboards do not send it.
- LW8 FIXED (small): Bounce out's list jumped to the newest take whenever a take was put into the project; the report gave the second before
  midnight as the time of day of a fix version whose take has gone (on the time of day).
- **LW9 DONE (the person, 10 November: yes - see "Decided on 10 November" at the end) - a flag cannot be taken away in fermata:Live.** Enter pins one; Esc "leaves it empty" - but the flag stays, the Fixer
  has seen it, and it goes into the project as a marker. The code to take one away exists on both sides (`PrimaryApp::removeFlag`, the
  recorder's "flag-remove") and nothing calls it. If wanted: a "Take the flag away" key in the flag's note row, or Delete on a chosen flag.
- **LW10 DONE (the person, 10 November: yes - see "Decided on 10 November" at the end) - a whole long take in the Fixer's "Try it".** P chooses a whole take on every track; Try it (and the ReTouch window)
  then reads all of it into memory, twice. An hour on 16 tracks does not fit: the job ends with "Something went wrong while processing" -
  no harm (the Fixer is a program of its own, the recording is not touched), but no fix either. It could say plainly that the part is too
  long and how long a part works, or work through the take in pieces (a bigger change).
- LW11 NOTED: a piece's name typed in the Primary's take window makes a new take window in the project for every name typed and left (a
  typing mistake corrected leaves an empty take window behind, to be removed in fermata). Renaming the piece in place would be a change in
  how it behaves.
- LW12 NOTED: a click on the Fixer's timeline while a fix is being heard throws the try away (a click clears the part chosen, as a new drag
  does). "Stop the take?" is answered Yes by Enter - the same key that pins a flag; ending a take loses nothing (the drives go on writing).
- LW13 NOTED, could not be checked here: a station keeps up to 96 chunk files open for reading while it plays; on Windows a file deleted
  while it is open keeps its name until it is closed, so a merged session's chunk folder may stay (empty-looking) until the station moves
  on or closes. Nothing is lost by it.
- LW14 NOTED: read and found sound - the closing questions and the waiting box (`Main.cpp`), the cut-off session offer, takes asked for
  again after a merge was cut short, the notes' way through the recorder, the waveform history requests (no loop: a point with no data is
  still marked as had), the player's lock (held only to copy), the second seat's thread (every change of filter drains what the driver
  held; closing gives every device back), the time-of-day layout, the report.

---

## Area I: clean-up

Looked for, over the whole source: functions nothing calls, and helpers written twice. **Nothing was removed**: the brief allows clean-up
only where tests cover it, and all but one of these are in window code. They are listed for a quiet build.

- I1 DONE (the person, 10 November: "remove them" - see the end) - nothing calls these any more: `AppContext::playDisc` (the mastering window plays its renders with
  `playRenders` now; `discAlign`, `discToken` and `MasteringComponent::discItems` are its remains), `AppContext::restoreLastProject`,
  `TakeWindowComponent::playMarked`, `ReHarmoniserEditor::pushHistory` (empty), `Project::inputsOfTrack`, `RavennaDevice::sendRaw`,
  `PrimaryApp::removeFlag` (see LW9: wanted, perhaps); and the small ones `AdmWriter::framesWritten`, `surround::channelLongName`,
  `DeviceGuard::deviceIsGone` / `questionsWaiting`, `SpectrogramView::harmonicSettings` / `harmonicBoxPitch`, `PeakStore::hasFine` /
  `hasCoarse`, `StationModel::trackOfChannel`, `SecondSeat::pointerPosition`. Taking them out changes nothing the programs do.
- I2 NOTED - written more than once in fermata:Live: `obj()` and `put()` (five files), the hours-minutes-seconds text (`hmsF` in
  `FixerWindow.cpp` is `TakeLayout::hmsText`), dB to gain and the pan law (`FixerApp.cpp`, `PrimaryApp.cpp`). Harmless; one copy each
  would be tidier.

---

## Decided on 10 November 2026, and done in fermata 3.47.0 / fermata:Live 0.8.0

- **D9 - sliding pieces takes what lies over them along.** `editversions::Riders` (EditVersions.h): made before pieces are slid, shuffled or put
  in another order, it remembers where the overdubs, markers, comment flags, marks 1 / 2, the playhead and the automation points fixed in
  time stood; `carry` puts each back on its music afterwards. The rule is the one undo already uses (`mapSeconds`): a thing stays over the
  same place in the same piece; in a silence, or past the end, it stays with the piece before it; one whose music is no longer there (cut
  off by a join in the Trim window) stays where it was; marks 1 / 2 left the wrong way round are cleared. A drag makes one at its start and
  carries from it at every move, so a piece sliding over a marker in a silence does not pick it up. Used by: the edit window's drag of a
  piece (with Slip Left / Slip Right), its shuffle drag, the Move earlier / Move later keys (`moveSelected`), the Trim
  window's slides of A and B (with their Slip switches) and its trim of the edit's first fade with Slip right. Undo needed nothing new: the
  markers follow their music back through the same rule. `EditDef::slideRegion` and the others still move pieces only - the windows carry.
  Test: core_test "3.47.0 sliding, shuffling and re-ordering pieces: what lies over them goes with them, and an undo brings it back".
  Not carried: a piece's own volume changes when the Trim window slides its audio under the join (D3, noted; milliseconds).
- **D8 - frozen versions stay where they are** when clips are moved to another track: nothing to change. D7's fix keeps the track they
  still play on from being deleted as empty.
- **LW9 - "Remove flag"** in the Primary's take window, in the flag's note row (which opens when a flag is pinned, and on a double-click on
  a flag): the session's flag goes through the recorder ("flag-remove": the Fixer's list and the project's marker lose it), one the project
  has from an earlier session goes straight out of the project (`PrimaryTakeWindow.cpp removeFlag`, `PrimaryApp::removeFlag`).
- **LW10 - a part too long for Try it is refused in plain words** (`FixerWindow.cpp whyTooLong`, for Try it and for the ReTouch window): how
  long, on how many tracks, about how much memory that needs and how much the PC has, and about how long a part works on those tracks.
  The sum is a rough one on the safe side (half the PC's memory at most).
- **I1 - the unused functions are out:** `AppContext::playDisc` with `discAlign`, `discToken` and the watcher's lines for them (the
  mastering window's `discItems` and `DiscPlayItem` are still used and stay), `AppContext::restoreLastProject`,
  `TakeWindowComponent::playMarked`, `ReHarmoniserEditor::pushHistory`, `Project::inputsOfTrack`, `RavennaDevice::sendRaw`,
  `AdmWriter::framesWritten`, `surround::channelLongName`, `DeviceGuard::deviceIsGone` / `questionsWaiting`,
  `SpectrogramView::harmonicSettings` / `harmonicBoxPitch`, `PeakStore::hasFine` / `hasCoarse` (and the `has` only they used),
  `StationModel::trackOfChannel`, `SecondSeat::pointerPosition`. `PrimaryApp::removeFlag` stays: LW9 uses it now. Nothing the programs do
  changes; every file they were in compiles.
- I3 NOTED (found while writing LW10's message): `juce::String (number, 0)` does not mean "no decimals" - with 0 JUCE writes the number its
  default way, six figures. It is right for the sample rates it is mostly used for ("48000"), but a dozen messages meant as whole numbers
  show decimals: "Moved up against the piece before it (41.6667 ms earlier)", the Trim window's "... (123.458 ms is the most)", the About
  box's memory in GB, the low-cut panel's frequency. Looks only; for a quiet build (`juce::roundToInt` where a whole number is meant).

