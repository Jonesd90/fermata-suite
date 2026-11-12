# fermata:Live - version history

fermata:Live is the live-concert recorder that goes with fermata (see `docs/LIVE-BRIEF.md`). Version numbers are MAJOR.MINOR.PATCH. The number is set in one place, the first lines of `src/live/Live.cmake`, and appears in the window's title and in the properties of `fermata-Live.exe`.

- **PATCH** (0.1.1, 0.1.2 ...): bug fixes and small tweaks only.
- **MINOR** (0.2.0, 0.3.0 ...): a new feature or a visible change.
- **MAJOR** (1.0.0 ...): the first version for real concerts, and later large changes in how the program works.

A line that starts with **Shared:** is a change to the code fermata shares (see `COMPATIBILITY.md`), so fermata's next build mentions it too.

## 0.10.4
Built 11 November 2026 (the conversation for both programs), with Claude Fable. No change to the recorder and none to fermata:Live's own
code: what the shared code brings.
- **Shared (fermata 3.54.0):** `Project::stashMixer` / `dropMixerStash` (an edit's working mixer kept in
  `Recovery/mixer-<id>.json` while fermata has an older version's settings in it). fermata:Live never writes or reads one.
- **Shared (fermata 3.54.0):** a new header in the shared folder, `src/core/ScoreBars.h` (the bars of a score's pages and the takes'
  brackets on them). fermata:Live does not include it, so nothing of it is built into the recorder.

## 0.10.3
Built 11 November 2026 (the conversation for both programs), with Claude Fable.
- **Windows stays awake while the recorder runs.** An ASIO driver does not keep Windows awake by itself, so with a power plan that
  allows sleep a concert that nobody touched the mouse during could end with the computer asleep. While a session is recording, and
  while a take is being merged, the recorder asks Windows not to sleep and not to switch the screen off (its log says when).
- **Shared (fermata 3.53.0):** the listening page shows a comment ticked done at full strength with a tick; a reply's version number
  follows when a freeze is undone and is put right when a project opens (`editversions::settleMadeIn`).
- **Shared (fermata 3.53.0):** the engine's take events, the silence for a stopped device and the take-notes file are in the shared
  code for fermata's own recording; fermata:Live's recorder is its own and is not changed by them.

## 0.10.2
Built 10 November 2026 (the conversation for both programs), with Claude Fable. No change to the recorder and none to fermata:Live's own
code: what the shared code brings.
- **Shared (fermata 3.52.0): the ReTouch picture's playhead.** In the Fixer's ReTouch window the picture redraws only the strip the
  playhead is in when the line moves (it redrew the whole picture), and the log names the picture when its drawing takes long.
- **Shared (fermata 3.52.0): comments and versions.** The listening copy's page shows each round of comments on two lines (the
  listeners', the editor's replies) with a tab for each version; a listening copy can be made of an older, frozen version of an edit
  (the project file may carry `"formVersion"` on a Mastering piece); any frozen version can be removed; the safety copy of imported
  comments follows a freeze. fermata:Live reads and writes these through the shared code and never looks at them.

## 0.10.1
Built 10 November 2026 (the conversation for both programs), with Claude Fable. No change to the recorder and none to fermata:Live's own
code: what the shared code brings.
- **Shared (fermata 3.51.0): any number of windows can be pinned.** The pin next to the sun / moon in the corner of the take window, the
  mixer, the second seat and the other windows used to belong to one window at a time - pinning another unpinned the first. Now each
  window keeps its own pin, and all the pinned ones stay in front of fermata:Live's other windows (`src/ui/Uikit.h`).
- **Shared (fermata 3.51.0): the comments' new fields and an edit's score.** A project in which fermata has written replies to listeners'
  comments, or given an edit a score (a PDF in the project's new "PDFs" folder), keeps all of that when fermata:Live saves it (the shared
  code reads and writes it); fermata:Live does not show comments or scores, so nothing else changes here. A copy of the whole project
  takes the PDFs folder with it, like every file in the project's folder.
- Looked at here: fermata:Live built for Linux and started on a virtual screen; the pins themselves were tried in fermata (two windows
  pinned at once, both staying in front), which uses the same code. The recorder's tests and the compatibility check pass. Not run on
  Windows.

## 0.10.0
Built 10 November 2026 (the conversation for both programs), with Claude Fable. No change to the recorder. The type of the Fermata style
UI, and what the shared code brings.
- **The Fermata style UI is in Inter throughout** (decided on 10 November 2026, from the typeface boards, which were drawn on this
  program's take window). Ordinary text is Inter Medium - a step heavier than it was - and the displays are in Inter too: the project,
  rate, disk and recorder displays of the main window, the take length and playhead clocks of the take window and the Fixer, the
  drivers' names. Their figures are all one width, so a running clock does not shiver. They were in the system's console typeface. The
  Manuscript UI is as it was. The log and the report keep the console typeface (they line up in columns).
- **Shared (fermata 3.49.0): the ReTouch window's tab keys.** In the Fixer's ReTouch window R, G, D, N and H did their work the moment
  they were pressed, whichever tab was open. Now a key pressed from another tab only opens its tab; pressed with its tab already open it
  does the tab's work.
- **Shared (fermata 3.50.0): volume areas.** A project fermata has given volume areas keeps them when fermata:Live saves it (the shared
  code reads and writes them); fermata:Live does not show or play edits, so nothing else changes here.
- **Shared (fermata 3.50.0): a muted send's ring pulses red and a send's band is green** in the mixer's look (`src/ui/MixerLook.h`);
  fermata:Live's own mixer has no send dials, so there is nothing to see of it here.
- Looked at here: fermata:Live built for Linux and run on a virtual screen in the Fermata style UI - the main window's displays and
  the take window's clocks in Inter. The recorder's tests and the compatibility check pass. Not run on Windows.

## 0.9.0
Built 10 November 2026 (the conversation for both programs), with Claude Fable. Asked for on 10 November 2026: "give the same 'fermata' style UI
option to fermata:Live".
- **fermata's looks, in fermata:Live.** The Primary's Project menu has **UI style** - the Fermata style UI (the blue-grey desk) or the
  Manuscript UI (midnight blue and gilt, the Renaissance serif) - and the row of **colour themes** (the desk in blue, red, orange, yellow,
  green or purple), exactly as fermata's Project menu has them. Every open window changes at once.
- **The sun / moon key works.** It has been in the corner of every fermata:Live window from the start (the windows are fermata's), with
  nothing behind it: it now switches the light and the dark look, as in fermata.
- **The Fixer has the same choice**, behind a "Look..." key at the right end of its status line.
- Each station remembers its own look - the Primary in its settings, the Fixer in its own - and starts in it next time. They are separate
  programs, often on two screens for two people, so one may be the blue desk and the other the Manuscript UI. fermata's own choice stays
  fermata's: the two programs do not share settings.
- The M and S keys beside the track names (take window, Fixer) are written in the look's own ink: in the Manuscript UI their dark letters
  could not be seen on its navy keys.
- **Shared:** `src/ui/ThemeSwitch.h` - fermata's code for switching looks - is now one of the look files both programs use (added to the list
  in `COMPATIBILITY.md`; the file itself is unchanged, and so is fermata).
- Looked at here: fermata:Live was built for Linux and run on a virtual screen - both styles, a desk colour, light and dark, in the
  Primary's main window, take window and mixer and in the Fixer, and that the choice is remembered. Not run on Windows.

## 0.8.0
Built 10 November 2026 (the conversation for both programs), with Claude Fable. fermata:Live's windows - the Primary's, the Fixer's, the second
seat - read in the fourth code sweep (they had never been in one; the list is in `docs/SWEEP-4-FINDINGS.md`), and two things asked for on
10 November 2026: the first two below.
- **A flag can be taken away** (asked for). Primary's take window: the flag's note row - it opens when a flag is pinned, and on a double-click
  on a flag - has a "Remove flag" key. A flag pinned by mistake used to stay for good: the Fixer had been told of it, and it went into the
  project as a marker. Removed, it goes from the Fixer's list and from the project too.
- **A part too long for "Try it" is refused in plain words** (asked for). Fixer: P chooses a whole take on every track, and Try it (or the
  ReTouch window) then needs all of it in memory - an hour on 16 tracks does not fit, and all it said was "Something went wrong while
  processing". It now says how long the part is, on how many tracks, about how much memory that would need and how much the PC has, and
  about how long a part works on those tracks. (The sum is a careful one: half the PC's memory at most.)
- **A new project can no longer take the place of an earlier concert's.** A new project given the name of one already in that place wrote an
  empty project over the first one's project file (the recordings stayed on the disk; the project that listed them was gone). It is refused
  now, with the reason, before anything is written.
- **The second seat cannot let go without saying so.** "Start the second seat" pressed while a Learn was still waiting for its key started the
  seat - and its first key then answered the Learn, which switched the driver off: the panel still said ON, but the Fixer's keyboard typed
  into whichever window was in front (the Primary's). Starting the seat now ends a Learn that is waiting.
- **"Kept as fix version" is only said when the recorder is there to keep it.** With the recorder not answering, the Fixer wrote the fix's
  files and said it was kept; the recorder that came back refused it. Nothing is written then, and the fix stays to be kept when it is back.
- **Project Designer:** with a timecode track in the project (one made in fermata) the rows were made again about once a second - a name
  being typed lost the keyboard; and after opening another project with as many tracks, the old project's names could be written over the
  new one's tracks.
- **A level typed into a mixer strip** (both stations): the box closed by itself within half a second, and "-inf" or a slip of the keys was
  read as 0 dB - the fader jumped up. The box stays while you type; "-inf" closes the fader; what is no number changes nothing.
- A new project that could not be saved (a full or read-only drive) no longer stays open as an empty project in which a session could be
  started. On the second seat, a laptop-type (PS/2) keyboard's arrows are no longer Shift + arrow while Num Lock is on. Bounce out's list
  keeps the take chosen when a new take arrives. The report gives no time of day to a fix version whose take has gone.
- Clean-up: three small functions nothing called are out (`PeakStore.h`, `StationModel.h`, `SecondSeat.h`).
- **Shared:** fermata 3.47.0 changed `src/core/Edit.h`, `EditVersions.h`, `Project.h` and `Project.cpp` (edit operations, undo, a bar-number
  limit), and took unused functions out of `Ravenna.h/.cpp`, `AdmWriter.h` and `Surround.h` (see `CHANGELOG.md`). fermata:Live's own code
  calls none of the changed parts. No file format changes.

## 0.7.1
Built 9 November 2026 (the conversation for both programs), with Claude Fable. The recorder read line by line in the fourth code sweep (the whole
list is in `docs/SWEEP-4-FINDINGS.md`). The recording path itself - the audio into the buffer, each drive's own writer out of it - had nothing to
put right; what was found is round it. Nothing here changes how the program is used.
- **What is noted while a session closes reaches the drives.** A flag or a note pinned, or a take marked as merged, while the recorder was
  waiting for the drives to finish (a moment normally, minutes on a slow drive) was kept in memory only and written to no drive: gone if the
  recorder then closed, and the take merged a second time at the next start. It now reaches every drive's index.
- **The last word to a drive's index cannot be lost**, and two written at the same moment after a session (the merge, a station's note) can no
  longer leave the older one on the disk. The last write of a session is tried again when something holds the file for a moment.
- **A dropped drive is taken on again only once its old writer has let go of it.** Taken on while the old writer was still waiting for a
  failing disk to answer, the recorder's status and commands waited with it (the audio and the other drives never did), and the old writer
  could then mark the drive as dropped over the new one. The key now says "That drive is not answering yet ... try again in a moment".
- **One drive's error cannot end the recorder.** An error inside one drive's writer (running out of memory, say) now drops that one drive - the
  audio and the other drives go on; one in the waveform for the stations stops the waveform only; one in the recorder's timer skips that round.
- **The merge looks only at its own take's files.** It measured every chunk recorded before the take's end - thousands in a long session - and
  could ask about the chunk a drive was still writing.
- **Shared:** fermata 3.46.1's sweep changed `src/core/AudioEngine.cpp`, `BackupCopier.h`, `Project.h/.cpp`, `Ravenna.cpp`, `MasterExport.cpp`,
  `RxWatch.h`, `StereoJoin.h`, `AudioOps.cpp`, `ImportPlan.cpp`, `Aaf.cpp` (see `CHANGELOG.md`). fermata:Live's own code calls none of the changed
  parts. No file format changes.

## 0.7.0
Built 9 November 2026 (the conversation for both programs), with Claude Fable.
- **De-hum in the Fixer** (asked for on 9 November 2026). The Fixer's fix list has "De-hum: find the hums in the part and take them out": Try looks
  through the chosen part of the take (every track) for tones that never change - the mains, a buzz and its harmonics, a whistle - and takes out
  every one that stands 15 dB or more above the room, each through its own narrow notch that follows the hum's level; the slider is Soft <-> Hard
  (6 dB off, gently, to 40 dB - all of it; 30 to start). What it found is said as the try plays ("De-hum (30 dB off): Buzz at 30.18 Hz: 16
  harmonics, 211 - 1479 Hz (28 dB above the room), 1439.0 Hz (25 dB...)"); nothing found: it says so and does nothing. Save it as a version as any
  other try. The Fixer's ReTouch window (the shared one) has the full De-hum tab too: key H lists the hums for you to tick, untick and retune.
- **Shared:** fermata 3.44.0's De-Hummer - `src/core/DeHum.h` (new), `FixSpec::Kind::Dehum` (`src/core/AudioOps.h/.cpp`) and the ReTouch window's
  De-hum tab (`src/ui/SpectralWindow.h`; Harmonics moved from H to Shift+H). fermata:Live's own changes are in `FixerWindow.cpp` only. No file
  format changes.

## 0.6.0
**The second seat** (asked for on 6 November 2026: two people working fermata:Live on one PC, each with a mouse and keyboard of their own - the
user chose the driver way, option 3 of the five set out). Built in the fermata:Live conversation. The mixers, which were to be 0.6.0, are now 0.7.0.
- **The Fixer's own keyboard and mouse.** With the Fixer on the same PC as the Primary, a USB keyboard and mouse can be the Fixer's: its mouse
  moves an orange pointer of its own, with the Fixer's name, kept on the Fixer's screen, and works only the Fixer's windows (clicks, drags with
  Shift / Ctrl / Alt, the wheel, the mixer, the ReTouch window); its keyboard types only into the Fixer - into what its mouse last clicked (the
  reply box, a note, the name box), else the Fixer's keys (Space, N, M, P, the arrows). The Primary's mouse and keyboard carry on as normal and
  are never touched, and the PC's keyboard stays with the Primary's window.
- **Second seat...** (top right of the Fixer's window) opens its panel: Learn the Fixer's keyboard (press a key on it) and mouse (click with it),
  the pointer's speed, **Start the second seat** / **Let go**. It is remembered: the next time the Fixer opens, the seat starts by itself. The
  button says **Seat: ON** in orange while it is on.
- **Giving them back**: Esc five times quickly on the Fixer's keyboard, Let go (either mouse can click it), or closing the Fixer. The PC's only
  keyboard or only mouse is refused, and the seat never starts with one of its chosen devices missing (nor guesses between two of the same model).
- **Drop-down lists** clicked with the seat's mouse open as the seat's own list (an orange edge): click an item, or press, drag and let go on one;
  the wheel, Up / Down, Enter and Esc work it. (fermata's own menus follow the ordinary mouse only.)
- A chosen device that goes while the seat is on (unplugged) makes the seat let go at once, with a message (otherwise it would be an ordinary
  keyboard again, typing into whichever window is in front). Learn takes any key, the arrows too.
- It needs the free **Interception** driver installed on the PC (and `interception.dll` beside `fermata-Live.exe`): `docs/LIVE-SECOND-SEAT.md`
  says how, and its licence (free for non-commercial use; the author offers commercial licences). Without the driver, the panel says so and
  nothing else changes. Windows only.
- **M in the Fixer's mixer** closes it again (with either keyboard): the keys the mixer has no use for go on to the Fixer's window, as from the
  Primary's mixer. It did nothing before.
- Shared: only the check tools - `tools/check-windows-code.sh` also compiles the second seat's Windows code, and `tools/win-stubs/windows.h` has
  the Windows calls it makes. No code built into fermata changed: fermata needs no rebuild.

## 0.5.0
**The timeline and notes** (the first half of the changes asked for on 6 November 2026; the mixers come next, in 0.7.0). Built in the fermata:Live conversation.
- **Time of day** (a switch in the Primary's take window, and one in the Fixer's): the timeline becomes the clock. Every take sits at the time it was recorded, with the gaps between takes; the clock runs along the timeline all the time (a blue NOW line), and REC just switches the recording on and off at that moment. Past midnight it runs on into the next day, with the date (DD/MM/YYYY) shown once at each midnight. A take whose pre-take reached back over the end of the one before cuts that one short where it starts (it is the same recording, heard once). Off: fermata's timeline, the takes one after another from 00:00:00.000, as before. It only changes what fermata:Live shows - the project, and fermata, are as before. The take's time comes from the session's clock, or from the time stamp in its files (the BWF time reference fermata and fermata:Live write).
- **The flag point**: while a take records, the playhead is no longer used (it is not drawn). Click the flag band where there is no flag to choose a moment that has passed - a yellow marker - and Enter (a flag) or N (a note) pins there, with its text; it goes back to NOW once pinned. A click on the tracks goes back to NOW too (and the view follows the recording again). A flag at the flag point reaches the Fixer like any other.
- **When nothing records** the playhead is there to audition the takes, as before: click to put it anywhere.
- **Zoom as in fermata**: Left and Right zoom out and in round the playhead (the flag point when one is chosen; now, while recording), which goes to the middle of the screen (SADiE's way); near the start it cannot go further left. Up and Down make every track smaller and bigger (the same way round as fermata - it was the other way). C puts the playhead in the middle without zooming; . and , make the waveforms bigger and smaller (+ and - still do). Ctrl + the mouse wheel still zooms round the mouse. Fit shows every take (on the clock from the first one, not from midnight), with room after the live end while a take records.
- **Scroll bars** along the bottom (and down the right for the tracks), covering everything recorded. Scrolling away while recording switches Follow off; the Follow button, or a click on the tracks, brings it back.
- **Marks 1 and 2 with P** (for Bounce out): drag over a part and press P, and marks 1 and 2 go round it; click a take's header (the strip with its number) and press P, and they go round the whole take. 5 takes them away. In the Fixer, P with a take's header clicked makes all of that take (every track) the part to fix.
- **Notes (N)** - markers are gone (they were fermata's plain numbered markers: no text, and the Fixer never saw them). Each station has its own notes: N pins one at the moment (the flag point, or the playhead when nothing records) and a box opens at the side of the window to write in, as much as you like - it is kept as you type; Done or Esc closes it, Delete note takes it away, and a note left empty goes by itself. A note shows in the NOTES row at the top of the timeline (click it to open it again). The Primary never sees the Fixer's notes and the Fixer never sees the Primary's. They travel through the recorder exactly as flags do (kept on every drive), and go into the project as markers once their take is there, so fermata shows them ("Note (Sam): the cello's chair creaks"). The project's own plain markers (the gaps of a recording) show in the same row, grey.
- **The report** (Report... in the Primary's take window): a PDF in the project's Reports folder - every take of the piece with a line showing where each note, flag, gap and fix version is, then the list at their places (time of day and time in the take), who wrote each (both stations' notes), the Fixer's replies and the fix versions.
- **Names**: the Primary's name (Project menu > Your name) and the Fixer's (a box in its window) go on flags, notes, replies and fix versions - "by David", and the Fixer's flag list says "FLAGS FROM DAVID".
- **Coloured waveforms** on the Primary (the Colours switch): fermata's WaveColour - the colour says how high the sound is, how vivid how tonal, black a thump below 100 Hz. A take still recording is coloured as it goes (worked out from the drive in the window's own program, never in the recorder), a finished one from its files, the same as fermata colours it.
- **M** opens and closes the station's mixer (from the take window, or from the mixer itself).
- **The Fixer's drag** now chooses every track recorded in the take, for ReTouch; **Alt + drag** chooses only the tracks you drag over (what a drag did until now).
- Shared: a take window marker can be a note (`src/core/Markers.h`: `liveNote`, `by`, `station`; `noteName`), saved and read by `src/core/Project.cpp` only when set - an ordinary marker and an older flag are saved exactly as before (still contract 3; `tests/compat_test.cpp` checks it). fermata's WaveColour analysis of one block moved into `WaveColorCache::BlockAnalyser` (`src/ui/WaveColor.h`, a shared look file) so fermata:Live colours its live waveforms the same way; fermata colours exactly as before. fermata needs a rebuild and test (it is in the same zip).

## 0.4.0
**fermata's own ReTouch window, in the Fixer** (the Fixer only - the Primary has none). Drag over the tracks on the Fixer's timeline as before, then press **ReTouch window...** (beside the fix's menu): the part you chose, and 2 seconds either side, opens in fermata's ReTouch window, all the chosen tracks as one picture.
- **Everything fermata's window has**: ReTouch (all three methods), Gain, De-click, Denoise, harmonics, the Rectangle / Time / Brush selections with feathering, L C R, the spectrogram settings, Show before / Show fixed, Undo - the same window and the same processing as fermata's, not a copy of it.
- **Listening**: Space or Play plays what is in view through the Fixer's own mixer and output pair, and stops at the right edge of the picture (Loop goes round); "Listen to" chooses the fixed sound or the original. The playhead moves on the picture and on the Fixer's clock. Playing switches Follow Primary off, as Try it does.
- **Keep as a fix version** (the window's green button) keeps every fix made in the window as one new fix version of the take, for fermata's review: one file per track holding the part that changed, and the versions list shows it ("ReTouch window: ReTouch x2, De-click"). The recording and the project are never changed. The note for the review, Tell the Primary and the flag chosen in the Fixer's window go with it, as with Keep as a fix version there. With no session running it says so and the fixes stay in the window to be kept once the Primary has started it.
- **A click** in the picture (no drag) puts the playhead there and takes the selection away; Shift or Alt + click keeps it, as does a click on the time ruler (as in fermata 3.35.0).
- **Closing the window** with fixes not kept asks first. Its size and place are remembered.
- **Consonants**: the consonants kept with the project can be pasted; copying one (which keeps it with the project) is done in fermata, since the Fixer never writes the project.
- **Room tone for Denoise**: a room-tone box drawn in the window (key L), or the project's room tone if it has one.
- Shared: fermata's ReTouch window (`src/ui/SpectralWindow.h`, moved there from fermata's own files; built for fermata 3.35.0) and a fix version's flag and earlier attempt starting as none (`src/core/Takes.h`: a version that answered no flag was put into the project pointing at a flag, and an earlier attempt, that never existed); also, from fermata 3.35.0's own work, the edit's glue (`src/core/Edit.h`) and three icons (`src/ui/Uikit.h`), which fermata:Live does not use - fermata needs a rebuild and test (it is in the same zip).

## 0.3.2
- **A polarity key (Ø) on the mixer's strips**, beside M and S: inverts every channel of the track in what you hear live, in playback and in Bounce out (the recording is never changed). It is the same setting as on fermata's mixer strips (the project's processing mixer), so fermata shows it too. The Fixer's own mixer does not have it yet.
- The pretend "Test signal" device no longer says it has more channels than its 16 inputs and 2 outputs when asked for more.
- Shared: the strips' polarity (`src/core/Mixer.h`, `src/core/AudioEngine.cpp`, `src/core/Project.cpp`), built for fermata 3.31.0 - fermata needs a rebuild and test (it is in the same zip).

## 0.3.1
Closing fermata:Live lets go of everything, and it can no longer stay behind after it has closed. Tried here on the "Test signal" device (Linux); the real test is on Windows with your MAD driver.
- **Why it hung on to things.** Until now, closing fermata:Live left its recorder (`fermata-Live.exe --recorder`, in Task Manager) running whenever the pre-take was on - and after any take it always is. That recorder kept the audio device (so fermata could not open the MAD driver) and went on writing to the drives until fermata:Live was opened again. A merge still running at close left it for good.
- **Closing asks, when something is still running.** With the pre-take on, or a take still being merged: **Close everything** (the session ends and the audio device is let go at once; fermata:Live waits until the merge has finished and the take is in the project, then closes), **Keep the pre-take running** (only the window closes - the recorder goes on and is found again next time) or **Cancel**. With nothing running it closes at once, and the recorder with it. A take recording is asked about as before: the recording goes on.
- **Waiting for the merge**: a box shows the take being merged and how far it is. **Close now** (or closing the box) closes at once: the recorder finishes the merge by itself and then closes, and the take goes into the project the next time fermata:Live opens. If Windows is shut down meanwhile, fermata:Live keeps the PC from shutting down until the merge is done ("Shut down anyway" still ends it).
- **A take whose merge was cut short** - the PC shut down, or the recorder stopped, while it merged - is merged again the next time fermata:Live opens the project, and put in. (Until now such a take was left out of the project for good once its session had ended; its audio was only in the chunks.)
- **A recorder left on its own closes itself**: if no fermata:Live window has been there for a minute and nothing is recording, merging or running (the window crashed, say), it lets go of the audio device and closes. With the pre-take or a take running it never does.
- **fermata's closing log**: when any of fermata:Live's three programs (the window, the recorder, the Fixer) closes, each step goes into its log with the time it took, and the last line says "Closing: finished cleanly in 0.3 s".
- **fermata's closing safety net**: once nothing can be lost (the project saved; in the recorder, the session ended and its drives finished), if closing has not finished 15 seconds later - or the program is still running 20 seconds after it finished - where every part of it is goes into its log and it ends itself. So a driver that hangs as the device is closed, or anything else that sticks, can no longer keep fermata-Live.exe running. (Those emergency lines say "Fermata": they are fermata's own.) The device let go at once by Close everything has a guard of its own: a driver that has not let go of it 20 seconds later ends the recorder the same way.
- Shared: fermata's `src/ui/ClosingLog.h` (its closing log and safety net, 3.15.0 / 3.17.0) is now used by fermata:Live too, unchanged, and is listed as shared in `COMPATIBILITY.md`. fermata's code and build are unchanged; from now on a change to that file changes how both programs close.

## 0.3.0
The working demo: the Primary and the Fixer stations around the 0.2.0 recorder, in fermata's look. Tried end to end here on the "Test signal" device (Linux); the real test is on Windows with your MAD driver.
- **The main window** (fermata's desk look): the menu bar (Project, Audio settings, Preamps, Project Designer, Take Window, Mixer, Bounce out, Fixer, Help), the status squares (Talkback, Pre-take, REC, Drives, Merge), the take counter, the project and piece, the rate, the disk space left (and how many hours that is), the recorder's state; every driver input as a desk strip (name, meter, preamp gain where a Merging device controls it, TALK); the driver outputs along the bottom with what feeds them, their meters and TB.
- **The take window** (fermata's take window, reduced): REC starts the session and the take at once (or Pre-take in the main window starts writing first); the take grows live on the timeline from the recorder's waveform; STOP TAKE asks first, then the take is merged and put into the project and drawn from its files. ARM per track, markers (M), playback through the processing mixer, marks 1 and 2 (keys 1 and 2) for Bounce out. The R key never stops a take.
- **Flags**: Enter pins a flag at the moment you heard it and opens a box for its note (Enter saves it). The Fixer sees it at once; its replies appear under the flag. When the take is in the project, each flag becomes a marker of the take window - with its note and the replies - so fermata shows it.
- **The Fixer** (Fixer menu, or `fermata-Live.exe --fixer`): a program of its own, so it can be closed or crash without touching the recording. Its take window follows a few seconds behind what is on the disk (Follow Primary; untick it to move about), with its own mixer and outputs. Click a flag to reply. **ReTouch**: drag over the tracks to choose a part, pick a fix (De-click, Repair, Denoise, Gain - fermata's own), **Try it** to hear it in place (switch between fixed and original), then **Keep as a fix version**: new files beside the recording, which is never changed. The version goes into the project with its take, for fermata's review.
- **The mixer** (Mixer menu): the project's processing mixer - levels, pans, mute and solo, the output pair, and meters on every track and on the main output. It is what you hear live and what Bounce out uses. The Fixer has a mixer of its own.
- **Bounce out**: a take, whole or between marks 1 and 2, through the processing mixer to a stereo 24-bit WAV in Bounced Media. It says if the mix clipped.
- **After a crash or a power cut**: when fermata:Live starts and finds that the last session was cut off with a take not yet in the project, it offers to carry it on. A take that was recording goes on if the stop was short (the time it missed is marked as a gap), or ends where its audio stops and is put into the project after a longer one. REC or Pre-take carry such a session on too, so nothing recorded in it is left out.
- **Your audio interface is listed**: until now (0.2.0 too) the recorder offered only "Test signal" on Windows - the pretend device was added before
  JUCE made its list of drivers, and JUCE makes that list only while it is empty. ASIO and Windows Audio are there now, with Test signal beside them.
- **fermata:Live never changes the audio device by itself.** The recorder opens the device you choose in Audio settings, and only that; a device
  plugged in or pulled out is noted in the log, and the one in use stays. (Through JUCE's device manager, a device that left its driver's list was
  closed and another one opened in its place, without asking - in the middle of a session too.)
- **NO AUDIO, at once**: if the session gets no audio for 2 seconds (the driver stopped or restarted the device, gives nothing, or runs it at another
  rate), the main window and the take window say NO AUDIO - NOT RECORDING in red while it lasts, and the log says what to do. When the audio is back
  a flag marks the place on the take, with how long it was missing: in the Fixer, and in fermata as a marker. A device that its driver starts again
  at a rate other than the session's is never recorded into the session.
- **Talkback** (the Talkback square or numpad +): the inputs marked TALK to the outputs marked TB while it is open.
- **Audio settings, Preamps, Project Designer** in their own windows. Windows fit the screen they open on.
- **A take merged after the session ended** still gets its flags and fix versions into the project (they come with the merge's result), and one that finished merging while fermata:Live was closed is put into the project the next time it opens.
- **Closing fermata:Live** always lets the recorder go (and with it the audio device), unless it records, merges or has a session on, as before.
- A session that cannot be carried on (for whatever reason) no longer stops you recording: a new session starts, and the old one is left on the disk as it was. REC pressed twice while a session starts is taken once. A session that has ended is not carried on if the recorder later has to be started again.
- After a crash, chunks the session index had not listed yet (its writes had been failing) are found and taken in; a take that was recording ends where the furthest drive got, never earlier.
- Open device is greyed out while the session is on (it would have ended the pre-take). The log window keeps scrolling past 400 lines.
- The recorder and the Fixer are started without any of the window's open files or network connections handed on to them.
- The "Test signal" device is quieter (-24 dBFS a tone), so its 16 tones added up in the mixer do not clip.
- Shared: the compatibility contract is now 3: flags on markers (`src/core/Markers.h`) and fix versions in take windows (`src/core/Takes.h`), saved and read by `src/core/Project.cpp`. fermata shows a flag as an ordinary marker and keeps the new fields when it saves; files without them load as before.
- Shared: 17 of fermata's look files in `src/ui` (listed in `COMPATIBILITY.md`) are now built into fermata:Live too, unchanged; the compatibility check makes sure they use nothing but shared code.
- Shared: the GitHub build (`.github/workflows/build-windows.yml`, now in the zip) uses the versions of GitHub's actions that run on Node 24 (checkout v5, upload-artifact v6), so its Node 20 warning goes. It builds and checks exactly as before.
- Shared: `Project::readOnlyLoads` (`src/core/Project.h/.cpp`, `src/core/FileSync.h`): a program that only reads a project can load it without mending anything on the disk. The Fixer sets it; fermata does not, and loads exactly as before.

## 0.2.0
The recorder core: the part of fermata:Live that must never lose a recording. Built and checked on fake audio here (sample for sample, with drives made to fail, stall and die mid-chunk), and then run end to end on the "Test signal" device; the real test is on your MAD driver.
- **The recorder is a process of its own.** `fermata-Live.exe` starts `fermata-Live.exe --recorder` beside it and talks to it. The recorder runs at high priority and does nothing but take in audio and write it; the window can be closed (or crash) and the recording goes on, and the window finds the recorder again when it starts. Closing the window while a take records leaves the recorder running on purpose.
- **Chunks of 30 seconds**, one complete 24-bit BWF file per track per chunk (with the time of day in it), on every drive at once; an index (`session.json`) beside them lists the timeline. A session starts writing as soon as it is started - that is the pre-take, up to 5 minutes on disk - and **Record only notes where the take begins** (up to the pre-take before the moment you pressed it); Stop notes its end. Neither touches the audio or the drives.
- **A memory buffer of 30 - 60 seconds** per drive: a drive may stall that long before anything is lost. A drive that falls further behind gets a gap (noted with its time of day, silence in the merged take) and carries on; a drive that refuses a write, or runs out of room, is dropped with a warning while the others continue. If every drive stalls, the audio that was lost becomes a timed gap marker on the take in fermata. Nothing but a failed audio write drops a drive: a session index that cannot be written for a moment (a virus scanner, OneDrive) is simply tried again.
- **After Stop the take is merged** from the primary drive's chunks into fermata's own take files (named and tagged exactly as fermata records them), each checked back sample for sample against its chunks before it is given its name; chunks are only ever deleted once their take has passed that check and they are older than the pre-take. The merge runs in the background, waits while a take records, and carries on from the last finished track if it was interrupted. Backup drives keep their chunks; "Merge last take from backup" merges them on demand.
- **The take goes into the project** (a fermata project, chosen or made in the window) as fermata would have recorded it: a take in the take window, with its files, and a marker for every gap. Open the project in fermata and the take is there.
- **A session that was cut off** (the recorder or the PC stopped) is carried on with "Carry on last session": the timeline continues from the time of day, the take that was recording keeps recording, the cut-off chunk is mended, and the stretch missed is a gap.
- **"Test signal"**, a pretend 16-input device beside the real drivers, so the whole chain can be tried without an audio interface.
- **The window** is a test bench, not the final look: project, device, drives, pre-take and buffer length, Record and Stop, meters, each drive's state, the merge, and a log. If the recorder process dies, the window starts a new one, opens the device again and carries the session on by itself. While a take records, nothing the window (or anything else on the control port) sends can end it: Stop first.
- Two reviewers read the recorder core before this was built; everything serious they found is fixed (the list is in docs/LIVE-DEVELOPMENT-NOTES.md).
- Shared: `src/core/Compat.h` names the live session index (contract 2). Nothing else in the shared core changed; fermata's behaviour is unchanged.

## 0.1.0
The first build: the program's frame, built from fermata's shared core in the same GitHub build as fermata.
- `fermata-Live.exe` opens one window, titled "fermata:Live 0.1.0". It shows the sample rates the program will offer (44.1, 48, 96 and 192 kHz, read from fermata's own list) and the version of the compatibility contract it was built with. It does not record yet.
- Only one copy runs at a time.
- Its icon is fermata's with the dot in red. This is a placeholder until a real one is chosen.
