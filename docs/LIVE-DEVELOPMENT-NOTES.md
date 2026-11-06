# fermata:Live: development notes

The brief is in `docs/LIVE-BRIEF.md`, and the rules shared with fermata are in `COMPATIBILITY.md`. Newest entries come first.

## 0.6.0: the second seat (6 November 2026, built in the fermata:Live conversation)
Asked for on 6 November 2026: two people on one PC, each with their own mouse and keyboard, the second only working the program. Five ways were set
out (take turns on one mouse; a Stream Deck; the Interception driver; Windows' Raw Input; a second PC with remote control); the user chose the
driver (option 3) and will plan the Stream Decks later. The mixers move to 0.7.0. The user's guide is `docs/LIVE-SECOND-SEAT.md`.
- **`SecondSeat` (`SecondSeat.h/.cpp`), the Fixer's process only.** The Interception API is loaded at run time from `interception.dll`
  (`LoadLibraryW` + `GetProcAddress`: create / destroy context, set filter, wait with timeout, send, receive, hardware id), so fermata:Live builds
  and runs without it; no driver = the panel says why. Devices 1-10 are keyboards, 11-20 mice. The filter predicates are plain functions reading
  atomics (`gSeatKeyboard` / `gSeatMouse`). Only the chosen devices are ever filtered (`filterAll` for them); Learn filters key-downs or left
  downs of every device but sends every stroke straight on at once, and picks the first one (15 s at most; the only keyboard / mouse refused).
  The driver is used on the seat's thread only (high priority: a stroke not taken is the PC's input waiting), which sends everything not the
  seat's straight on and queues the seat's (capped: the oldest go, never the PC's input) for the message thread (`handleAsyncUpdate`; moves
  between clicks coalesced, but always sent before a click or a release, so a drag's last moves arrive).
- **The pointer**: `PointerWindow`, a desktop window (windowIsTemporary | windowIgnoresMouseClicks, always on top, on Windows WS_EX_TRANSPARENT
  | WS_EX_NOACTIVATE), placed at the hotspot + 1 px so JUCE's hit test (Linux `contains` asks the desktop windows above; Windows WindowFromPoint)
  never finds it; raised again when a window the Fixer opens comes over it. Relative moves with a gentle acceleration (`speed`), absolute ones
  (a tablet, remote desktop) scaled to the area; kept in `area()` (the Fixer's screen).
- **Clicks**: `peerAt` (the Fixer's desktop windows front to back, skipping the pointer, the seat's list, click-through windows such as tooltips,
  hidden and minimised ones; a drag stays with its window) and `ComponentPeer::handleMouseEvent` with **`InputSourceType::pen`** - a source of its
  own beside the ordinary mouse, so the two never take each other's drag. JUCE makes the pen's source on the first pen event and drops that
  event (`MouseInputSourceList::getOrCreateMouseInputSource` returns null when it adds one): `makePenSource` sends one first, or the seat's very
  first click was lost (it was: that was the "clicks do nothing" of the first tries). JUCE takes a mouse event's Shift / Ctrl / Alt from the
  global `ModifierKeys::currentModifiers`: `SeatKeys` puts the seat keyboard's there for the moment of each click, drag and wheel, then the real
  ones back.
- **The PC's keyboard stays the Primary's.** `NoActivate`, for each seat click: nothing clicked brings its window to the front as the active one
  (setBroughtToFrontOnMouseClick off for the moment) or takes the keyboard (setMouseClickGrabsKeyboardFocus off for the moment); the window is
  raised with toFront (false). Windows: `keepForeground` (20 times a second while the seat has its keyboard): when a Fixer window is the
  foreground window anyway - one the Fixer opened (TopLevelWindow shows itself with toFront (true)), the ordinary mouse clicking it, or Windows
  allowing a background program forward (the foreground lock time-out passes when the Primary has not touched anything for a while) - it calls
  `SetForegroundWindow` on the window that had it (allowed, being in front at that moment), else the Primary's window found by its title.
- **Keys**: scan codes tracked for Shift / Ctrl / Alt (left and right; the Windows and menu keys swallowed); Windows: `MapVirtualKeyW` (scan to
  VK, then JUCE's key codes; the number pad by scan code) and `ToUnicodeEx` with the seat's own Shift (not the PC's) on the PC's layout, so the
  seat's keys never touch the system's keyboard state. Delivered to `keyTarget` - what the seat last clicked (the deepest component wanting the
  keyboard; a window: its contents) or opened (`setKeyTarget`: the note panel's box, the reply box after Enter, the Fixer after a box lets go) -
  else the Fixer's main component; a modal component in front takes them; `keyPressed` up the parents. Never the component with JUCE's focus:
  that is the PC keyboard's. Esc five times within 2 s: `stop`.
- **Drop-down lists** (`SeatList`): JUCE's PopupMenu follows `MouseInputSource::getScreenPosition()`, which for a pen is the PC's own pointer,
  polls it 20 times a second, and closes when no JUCE window has the keyboard - a pen cannot work it. A ComboBox clicked by the seat (text-editable
  ones excepted) gets the seat's own list instead: its items (`PopupMenu::MenuItemIterator`, separators and headings shown, sub-menus' items in
  line) under the box, the menu's colours, an orange edge; a click-through window the seat works directly (no JUCE dispatch: hover, click, press
  - drag - let go, wheel, Up / Down / Home / End / Page keys, Enter, Esc; a click elsewhere closes it and goes no further). Choosing calls
  `setSelectedId` as the menu would. The Fixer has no other popup menus (checked: FixerWindow, LiveTakeView, LiveMixer, SpectralWindow).
- **Choosing again at start**: a device is found by its number and hardware id, else by its id alone only when no other device has that id (two
  of the same model: never a guess, it could be the Primary's). The seat never starts with a chosen device missing.
- **The Fixer** (`FixerWindow.cpp`): `SeatPanel` in a tool window; `Second seat...` / `Seat: ON`; the seat is made with the window and
  `startIfWanted` after the first layout; the window's destructor ends it first. `KeysToFixer` wraps the mixer's and the seat panel's contents so
  the keys they do not use go to the Fixer's main component (a ToolWindow's forwardKeys hands them to the last take window - the Fixer has none,
  so M in the Fixer's mixer did nothing, with either keyboard).
- **`NotePanel::refreshText`** never puts back the saved text over typing not yet kept (the seat types into the box without it having JUCE's
  focus, so `typing()` was false and the first letters went).
- **The Linux test feed** (`FERMATA_SEAT_TEST_FEED`, Linux only): a file of strokes appended to while the Fixer runs - `move dx dy`, `down` / `up`
  `L|R|M`, `wheel n`, `scan hex e0 down|up`, `key juceKeyCode char down|up` - with two pretend keyboards and two pretend mice (Learn picks the
  second of each). Everything after the driver is the real code.

### Checked here
- The Windows code compiles against the stubs (`tools/check-windows-code.sh`).
- On screen (Linux build, the Test signal device, a session recording, the test feed): Learn; Start; the pointer with the name, kept on the
  screen; the Note button and N (a note pinned, typed into with the seat's keys, every letter kept, Esc closing it); a flag chosen in the list,
  the reply box clicked, a reply typed and sent with Enter ("Sam: fixing it now" under the flag); M opening the mixer, a fader dragged (-7.9 dB),
  M closing it from the mixer (and with the ordinary keyboard too); the kind of fix chosen from the seat's list by click, by press-drag-release and
  by the keys; Alt + drag taking one track; Esc five times letting go; Let go clicked with the seat's mouse; the seat starting by itself with the
  Fixer, and refusing to with its keyboard missing. The ordinary mouse working the Fixer meanwhile (its own source: type 0, the seat's 2).

### Not checked here (needs Windows, the driver and two keyboards and mice)
- The driver itself: installing, Learn picking the right device, the filter taking only the chosen two, the devices coming back on Let go, on
  Esc x5, on closing and on a crash of the Fixer.
- The keys' names and characters from real scan codes (`MapVirtualKeyW`, `ToUnicodeEx`, the UK layout, Shift, the number pad).
- That the PC's keyboard stays with the Primary while the seat clicks, opens the mixer or the ReTouch window (`keepForeground`), with the
  Primary idle for several minutes (the foreground lock time-out), and that no taskbar flashing gets in the way.
- The pointer and the seat's list being click-through (WS_EX_TRANSPARENT) and staying above the Fixer's windows; two screens with different
  scaling (the area is the Fixer's screen; positions are JUCE's logical pixels).
- Worth a review by a second reader (the user offered Fable on Monday): `SecondSeat.cpp` - the thread and the filter, the dispatch, `keepForeground`.

## 0.5.0: the timeline and notes (6 November 2026, built in the fermata:Live conversation)
Asked for on 6 November 2026 in one list (the timeline, notes, names, colours, the Fixer's drag; the mixers and the mix exchange are 0.7.0 - their
answers are in the brief). Built in two halves on the user's wish for bigger builds; this is the first.
- **The time of day lives in the layout** (`TakeLayout.h`). `ShownTake::start` is the place on the timeline SHOWN; `windowStart` is fermata's (what the
  project keeps); `clockMs` the time of day of the first sample (the session's origin + its start, else the first file's BWF date + time reference -
  `clockOfFile`, read once per file - else `recordedAt`). `build (..., timeOfDay)` then `placeOnTheClock()`: seconds count from the local midnight of
  the first take's day (`originMs`). Everything a station does goes through `start` (the player, flags via `windowSecondsOf`, the ReTouch spans,
  marks 1 and 2 relative to the take), so it works on either timeline unchanged; only project positions are converted (`shownOfWindow`,
  `windowOfShown`; markers and notes in the project stay on fermata's timeline). On the clock, `windowSecondsOf` / `sessionSampleAt` cover the whole
  session (a flag between takes has a place). Overlap: a take whose pre-take reached back over the one before sets that one's `cutAt`; the player
  (`Player::mixInto`) and the drawing stop it there, `takeAt` prefers the later take - it is the same stretch of the same recording.
- **Switching** converts the cursor through fermata's timeline (`windowOfShown` on the old layout, `shownOfWindow` on the new), clears the flag point and
  the selection, and fits the view. Each station keeps its own switch (`timeOfDay` in its settings).
- **The view** (`LiveTakeView`): the NOTES row (22 px) replaces fermata's marker strip (the Primary's markers were draggable fermata markers; they are not
  made any more, and the project's ordinary markers - the gaps - show grey, read only). New fields: `nowLine`, `flagPoint`, `notes`, `selectedTake`,
  `selectedNote`, `colourWaves` / `fileColours` / `sessionColours`, `allTracksByDefault`; callbacks `onLaneClick` (before `onSeek`, always),
  `onFlagBandClick`, `onTakeClick`, `onNoteClick`, `onUserScrolled`. `zoomCentred` is fermata's `TakeTimeline::zoom` (the anchor to the middle; off the
  screen, the middle; out as far as the content fits; never left of the start). `changeLaneHeight`: Up smaller, Down bigger (fermata's way; it was
  reversed). Scroll bars: `contentRange()` (fermata's timeline from 0; the clock from the first take less a little room) and the tracks.
- **The flag point** (`PrimaryTakeComponent::target`): flag point, else the playhead while playing back, else now in the session, else the playhead. A
  flag at a point needs a session sample (`sessionSampleAt`): an earlier session's take cannot reach the Fixer, and says so. Pinning sends the recorder
  `flag` with `sample`; the point goes back to NOW once used. The playhead is hidden and cannot be moved while a take records (`canSeek`, `seek`).
- **Notes** (`StationNotes.h`): a `FlagRec` with `isNote` and `station` (`SessionIndex.h`: "kind": "note", "station"; written only when set), sent as
  `flag` with "kind": "note" (`RecorderService::putFlag` takes the kind and station once, when the flag is made; `flag-note` / `flag-remove` as for
  flags). So notes are kept on every drive and come back with a carried-on session exactly as flags do. The Primary's notes on an earlier session's
  take go straight into the project (`PrimaryApp::pinNote`); the Fixer's need the session (it never writes the project) and say so. `sentNotes`:
  a note's text sent before the recorder's answer is back still goes to the recorder. `PrimaryApp::syncNotes` puts both stations' notes into the
  project as markers (`liveNote`, `noteName`) once their take is there. Everything that lists flags skips notes (`FlagList`, `FixerFlags`,
  `LiveTakeView::flagsShown`, `FixerApp::onEvent` - a note never lights the Fixer's window). `NotePanel.h`: the side box; saves 0.7 s after typing
  stops and on closing, only when the text changed.
- **The report** (`LiveReport.h`): `gather` (session flags, project markers - flags, gaps, plain markers -, everyone's notes, session and project fix
  versions), grouped by take (`takeAt`; a fix by its take number), sorted by place; `write` uses the shared `pdf::Document` (A4, a line per take with
  numbered ticks, then the rows; replies in italic). Into `<project>/Reports/`.
- **Coloured waveforms** (`LiveColours.h`): a low-priority thread in the window's process reads the session's chunks (its own `ChunkTimeline`) up to
  `writtenTo`, only inside the takes, 16 blocks of 4096 at a time per track, and works out each block with fermata's `WaveColorCache::BlockAnalyser`
  (moved out of `analyse`, unchanged). Results by session block, under a lock held only to copy (`colours`, one call per waveform drawn). Takes in the
  project use fermata's `WaveColorCache` on their files (kept in the project's Waveforms folder, as fermata does). The recorder is never involved.
- **Names**: `stationName()` in each station's settings ("Primary" / "Fixer" when empty), sent as `by` on flags, notes, replies and fix versions; flags
  and notes also carry `station`, so who sees what never depends on a name. The stations' stream names stay "Primary" / "Fixer".
- **Keys**: Primary - Enter flag, N note, M mixer (`mixer-toggle`: closes it later, as the key may come from the mixer window itself), P marks round
  the selection or the chosen take (one take: a selection running past its end is cut there), 5 marks away, C centre, L loop, . , waveform size,
  Shift + Left / Right 10 ms (not while recording). Fixer - N, M, P (the chosen take, every recorded track, as the part to fix), C, . ,.

### Checked here
- Tests: `tests/live_recorder_test.cpp` "0.5.0 notes" (kind and station kept on the drives; an older flag written as before) and "0.5.0 the time of
  day" (clock places, the gap, past midnight, the overlap cut, both conversions); `tests/compat_test.cpp` (a note marker saved and read, an ordinary
  marker without the new keys).
- On screen (Linux build, the Test signal device, a project with two takes recorded 75 and 40 minutes earlier): the switch both ways (the takes at
  12:41 and 13:16 with the gap, NOW, back to fermata's timeline with the takes one after another); Fit from the first take; the arrows zooming
  round the playhead / now into the middle, Up smaller; a flag point while recording with a note pinned there (the panel, saved as typed) and a flag
  at now; the coloured waveform of the take recording (and of the files after the merge); M opening and closing the mixer (from the mixer too); P
  round a clicked take and round a selection; the name ("by David", "FLAGS FROM DAVID", the Fixer's "Sam: on it" on the Primary); the Fixer's drag
  (both tracks) and Alt + drag (one); the Fixer's note shown only on the Fixer and the Primary's only on the Primary; the report (all of it, with
  both stations' notes and the reply). After the merge the project's take window had the flag and both notes as markers with their keys.
- The live colours offline: `LiveColours` over the recorded session's chunks, fed a growing written-to point, coloured every block.

### Not checked here
- Windows (the build is on GitHub), a real interface, the report opened in a PDF reader on Windows, DST changes during a recording on the time of
  day (the clock labels are worked out from the local time, so they stay right; a take's place is its clock time).

## 0.4.0: fermata's ReTouch window in the Fixer (5 November 2026, built with fermata 3.35.0)
Asked for on 5 November 2026: "Yes, it absolutely should be the same tool as fermata. Definitely! Remember though, it will only be avaliable to the fixer
and not the primary!"
- **The same window, not a copy.** fermata's ReTouch window moved into `src/ui/SpectralWindow.h` (shared, COMPATIBILITY.md) and knows neither program: a
  program hands it a `RepairContext` (fermata 3.35.0's notes). Only `FixerWindow.cpp` includes it: the Primary has no ReTouch.
- **The Fixer's RepairContext** (`FixerComponent::openReTouchWindow`): the area is the part dragged on the timeline plus 2 s either side, inside the take and
  what is on the disk. `load` and `readChannels` read the take's own samples per track (`readTakeSpan`: the session's chunks for a take of the session, else the
  take's file - the same reading Try it uses, now shared with it). `prepare` makes the audition (`renderOps`): every op of the window on each track one after
  the other, as AudioOps' fixWholeFile makes fermata's new files (the take's samples round the changes read: 8 s of context for a ReTouch or a denoise, 1 s
  for the rest; a lost-sample mend reaches half a second past its box), a denoise with its room-tone box learned from the take (or the project's room tone
  for the track), a pasted consonant with its track's part. The audition is the span from the first changed sample to the last, per track.
  `apply` / `unapply` put it in and take it out of the Fixer's player (`setAudition`). `play` plays from the window's start through the Fixer's mixer (Follow
  off, as Try it); not looping, `position` stops it at the right edge of the view (fermata's playTake stops there); looping uses the player's loop.
  `accept` keeps it as a fix version (`keepVersion`, which Keep as a fix version under Try it uses too: the files in the take's Fixes folder, `announceFix`,
  the note, the reply, the chosen flag), then takes the audition away. `mayAccept` refuses with no project or no session (the fixes stay in the window).
  Consonants: the project's list and loading, read only (no copy, rename or forget: the Fixer never writes the project).
- The window is modal, in a DialogWindow of the Fixer's own (`ReTouchWindow`: asks before closing with fixes not kept, remembers its bounds as
  "retouchWindow" in the Fixer's settings). The Fixer's window behind it keeps showing flags; a flag that comes in while it is open is not chosen by itself
  (the version kept belongs to the flag chosen for it). Closing the Fixer deletes it first (it plays through the app).
- Shared: `FixVersion`'s `flagId` / `replaces` / `takeId` start as null (fermata 3.35.0's notes): the Primary's `FixVersion v;` gave a version with no flag a
  made-up flag and earlier attempt.

### Checked here (Linux, the Test signal device, the test display)
- fermata:Live linked on Linux from the shared core, src/live and the shared look's .cpp files only.
- A test project (two 30 s takes on Mic L and Mic R, a cough at 12.0 s of take 1). Without a recorder: the timeline, the part chosen, the window opened on
  both tracks (the picture, every tab and tool); a ReTouch on the cough; Keep refused with no session ("the Primary must start it first"), the fix still in the
  window; Escape asked "1 fix has not been kept yet ... (Keep as a fix version keeps it.)", Throw it away closed it, nothing left playing.
- With the Primary, the recorder and a session (Pre-take): Play through the window (its playhead and the Fixer's clock together: 9.166 s area start + 1.96 s
  = 11.129 s), the stop at the view's edge (15.993 s), Loop going round from the view's start; Keep as a fix version: "Fix version kept by the recorder
  (version 1 of take 1)", the versions list "ReTouch window: ReTouch", two files of 0.521 s (exactly the box) in Fixes, the cough's band 19 dB down in them
  and nothing above the box changed; the Primary put the version into the project (take id, span, files) - and showed the made-up flag and earlier attempt,
  fixed above.

### Not checked here
- The MSVC build (GitHub builds it); hearing it (here only the playhead and the meters show the sound); the window on Windows' own title bar.

## 0.3.2: the polarity key (5 November 2026, built with fermata 3.31.0)
- `MixStrip::polarity`, `MixModel::hasPolarity / setPolarity` (the Primary's model: the processing mixer's `StripState::polarity`, shared with fermata;
  the Fixer's own mixer: none yet). The strip shows M | S | Ø.
- What is heard: `PrimaryApp::playbackGains` makes the track's gains negative; `sendMix` now passes routes with a negative gain (it dropped every gain
  `<= 0`), and the recorder's `setMix` accepts -16 .. +16 (it refused `<= 0`). MonitorMix just multiplies, so a negative gain inverts. Bounce out goes
  through the shared AudioEngine, which applies the key itself.
- `TestSignalDevice::open` clips the channel masks to its 16 inputs and 2 outputs (asked for 256, as fermata asks, it reported them all active and
  JUCE's manager cleared 256 output pointers - found by the device guard's test, see fermata's notes for 3.31.0).

## 0.3.1: closing lets go of everything (4 November 2026)

Asked for: "fermata:Live is having the same problem as fermata in that when the program is closed it is hanging on to resources and not closing
properly. Can we find the fix for that from fermata and use it in fermata:Live." GitHub could not be reached; built on the local copy (0.3.0).

### What kept it running
- **Mostly on purpose, and the wrong way round.** `PrimaryApp::shuttingDown` (0.2.0 - 0.3.0) left the recorder process running whenever a take
  recorded, a merge ran, **or the session (the pre-take) was on** - and the pre-take stays on after every take (STOP TAKE ends the take, not the
  session). So after any take, closing fermata:Live left `fermata-Live.exe --recorder` behind with the audio device open (the MAD driver taken, so
  fermata could not have it) and writing pre-take chunks to every drive, until fermata:Live was opened again. A merge running at close did the same,
  and the recorder then never closed at all (no one sent it "quit" afterwards). The log said so ("The session is left running (the pre-take)"), but
  nothing on screen did.
- **And nothing would have caught a real hang.** fermata's 3.17.0 problem (a closed Fermata.exe that went on running: its take matching) had no
  counterpart here, but neither was there fermata's answer to it: no closing log to show where a closing stopped, no safety net to end a process
  whose closing does not finish. Any of the three programs could have stayed behind (a driver that hangs as it is closed, a thread that does not
  stop) and nobody would have known why.

### What changed
- **The question** (`LiveApplication::quitPrimary`, Main.cpp): closing the window with the pre-take on, or a take not yet merged, asks: **Close
  everything** (the session ends and the audio device is let go at once; fermata:Live waits for the merge, so that its take is in the project, then
  closes), **Keep the pre-take running** (only the window closes; the recorder goes on and is found again - the old behaviour, now chosen), or
  **Cancel**. With only a merge it is "Close when it has finished" / Cancel. Nothing running: it closes at once, and the recorder with it. A take
  recording: asked as before ("Close the window, keep recording" / Cancel). `PrimaryApp::stillGoing` counts a take just stopped as one to merge before
  the recorder's "queued" arrives (its index entry is stopped and not merged).
- **The wait** (the "Closing fermata:Live" box): which take is merging, the progress bar (the status's merge fraction), and **Close now** (the window
  closes at once; the recorder finishes the merge by itself and closes; the take is put into the project the next time fermata:Live opens - the
  0.3.0 mechanism for a take merged while the window was closed). A second close of the window is Close now. fermata:Live closes when the recorder's
  connection goes (its "merge done" events have all come first: the link reads them in order) - `RecorderLink::stopLaunching` first, so that the
  link does not start a new recorder when this one has gone. If the recorder says no merge is going for 10 s and is still there, it closes anyway.
- **The recorder's `quit {whenDone: true}`** (new; plain `quit` is unchanged): refused while a take records. With no merge to finish: the session
  ends and the recorder closes at once (the device is then closed as the program closes, under the safety net). With one: the session ends and the
  device is let go now - so fermata can open it meanwhile - and the recorder closes 1.5 s after its last merge and its merge events have gone out
  (`mergeEventsUnsent` counts them: the window must hear "done" before the connection closes; the 1.5 s lets the window's `registered` reach the
  index). An `open` or `session` meanwhile calls it off (`keepRunning`: a fermata:Live started again wants this recorder).
- **A recorder left alone closes itself** (`watchClosing`, `kAloneQuitMs` = 60 s): no window connected for a minute, and no session, no take, no
  merge - the window died, or its quit never arrived. Never with the pre-take or a take running (that is what the recorder is for).
- **fermata's closing log** (`src/ui/ClosingLog.h`, unchanged, now listed as shared in COMPATIBILITY.md): every program's shutdown logs each step with
  its time ("Closing: the session ended (its drives finished) (0 ms)" ... "Closing: finished cleanly in 0.3 s") - LiveApplication::shutdown, and
  inside ~PrimaryApp, ~FixerApp and ~RecorderService.
- **fermata's closing safety net** (`ClosingWatchdog` from the same file, 3.17.0): started in every program once nothing can be lost - the Primary
  after its project is saved, the Fixer after its settings, the recorder after its session has ended (its drives finished) and BEFORE its audio
  device is closed, so a driver that hangs as it is closed is covered. If closing has not finished 15 s later, or the program is still running 20 s
  after it finished, where every thread is goes into that program's log (Windows) and it ends itself. Its lines say "Fermata" (fermata's wording:
  the file is shared unchanged); the closing step before them says what had been done.
- Thread names for that listing: the playback stream (`StreamClient`), the Test signal device, and the driver's audio thread in the recorder
  ("fermata:Live audio callback (the driver's thread)", once per thread, as fermata's).
- "Test signal" pretends one more fault: `test-signal {action: hang-on-close, seconds}` - its next close takes that long.

### The review (one reader, after it worked) and what changed
- **A merge cut short after Close everything was never done again (serious).** Close everything ends the session first, so its index says
  "closed" while the merge still runs; if the recorder then died (the PC shut down - Windows ends the windowless recorder at once - a crash, a
  power cut), nothing ever merged that take: `findCutOffSession` looks only at "writing" sessions and the scan of ended ones only at takes merged
  but not put in. (The same was true in 0.3.0 of a session ended by hand during a merge.) Now the scan (`askForUnregistered`, once per project
  opened) also finds an ended session's take that is stopped and not merged (no error, or "Cancelled") and sends the new **`merge-take {folder,
  number}`**: the recorder merges it as the session's own merge would have (no session object: the index on the drive gets the result), and the
  window puts it into the project from the "done" event. Asked twice, or while it is being merged, it is not merged twice.
- **The closing box could be dismissed without closing.** JUCE's AlertWindow ends its modal state with 0 when its window is closed (Alt+F4, the
  taskbar), whatever `setEscapeKeyCancels` says: the box stayed, dead, and the main window was usable while the closing waited. Now any way the box
  goes means Close now (`closingDone` keeps the programme's own dismissal from doing it twice).
- **A driver hanging as the device is closed during the wait** blocked the recorder's message thread outside the safety net. The close there now
  runs under a guard (`closeWithGuard`): not back in 20 s, the thread listing goes into the recorder's log and it ends itself (the merge is done
  again at the next start, above). The device is still closed on the message thread - the thread it was opened on.
- **Windows shutting down while the box waits** no longer closes fermata:Live at once (`systemRequestedQuit`): it stays until the merge is done,
  so Windows shows it as keeping the PC from shutting down ("Shut down anyway" ends it; the merge is then done again at the next start).
- **A window that connects calls the closing off** (`keepRunning` on a new connection, not only on `open` / `session`): a fermata:Live started
  again during the 1.5 s before the recorder closes had it close under it. And a closing window no longer opens the device again 0.9 s after it
  connected, or puts anything back (`closingDown`): that "open" would have called off its own Close everything.
- The 10 s fallback of the wait never closes the window while a take records.
- Found while testing: closing within a second after "Take n is in the project" asked about a merge still going (the window knew the merge had
  ended only from the next status). `StationModel` now takes a merge's start and end from the `merge` events at once (the take is then merged);
  the status puts anything else right a second later.

### Checked here (Linux, the Test signal device, the test display)
- The recorder alone, from a Python client: Close everything with the pre-take on (closed in 0.4 s, its index "closed", the closing log to
  "finished cleanly"); just after a 40 s take at 96 kHz x 16 tracks (the device let go 0.8 s after, the merge done, closed 1.9 s after its result,
  the window's `registered` in the index); called off by an `open` during the merge; left alone with nothing running (closed by itself after 61 s,
  saying why) and with the pre-take on (still running at 75 s); quit and quit-when-done refused while a take records; a driver hanging 60 s as it is
  closed (the safety net ended the recorder 15.1 s after the session had ended; the session's index "closed"). After the review: a driver hanging
  90 s as the device is let go during the wait (the guard ended the recorder 20.1 s after Close everything, saying why); a window connecting during
  the wait (the closing called off); the recorder killed in the middle of a 60 s take's merge at 192 kHz (the index "closed", the take not merged),
  then `merge-take` from a new recorder (merged again: 16 files into the project's Recorded Media, the index merged; asked again, not merged twice).
- In the windows: nothing running (both programs gone at once, both closing logs complete); the pre-take on - the question, Keep the pre-take
  running (the recorder went on and was found again), then Close everything (both gone); a 90 s take at 192 kHz x 16 tracks still merging - the
  question, the box, the take in the project, both gone; Close now during a 150 s take's merge (the window gone at once, the recorder after its
  merge, the take put into the project at the next start); a take recording - Cancel, then Close the window, keep recording (the recorder went on
  recording); the Fixer closing on its own (its closing log complete) while fermata:Live went on. After the review: the box closed by its window
  (as Alt+F4) - Close now; Close now, then the recorder killed in the middle of the merge (the PC shut down, say) - started again, fermata:Live
  merged the take again and put it into the project, and closed at once 0.3 s after (no question about a merge that had ended). All of it again
  on the final build.
- Test display only: a window close sent to a JUCE window sat undelivered until the next event (the same Xlib queue as clicks, see 0.3.0) - the
  close helper nudges the pointer after it; and the question can open behind the main window with the test window manager (it has the focus: the
  keys reach it). Neither happens on Windows.

### Not checked here
- The thread listing of the safety net (Windows only: `crashreport::describeThreads`); a real ASIO driver being let go and fermata opening it at
  once; Windows asking fermata:Live to close (shutting down or logging off).

### Known limits (0.3.1)
- A take merged after Close now (or merged again after its merge was cut short) goes into the project only when fermata:Live opens the project
  again: opened in fermata first, the take is not there yet.
- A take whose merge FAILED (an error, not cut short) in a session that has ended is not tried again by itself (it was warned about when it failed).
- The Fixer stays open when fermata:Live closes everything: it says the recorder is not answering, and closes when you close it.

## 0.3.0: the working demo (Primary and Fixer stations)

Asked for on 4 November 2026: "carry on building so I can try a fully working demo". The design below is what was built; what changed or was
added while it was tried out follows it.

### Three processes, one program
- `fermata-Live.exe` - **the Primary station**: the main window (inputs, outputs, menus), the Primary take window, the processing mixer (the
  Primary's monitoring and Bounce out), Preamps, Project Designer, Audio settings. It owns the project file: it is the only process that writes it.
  One Primary at a time (an `InterProcessLock`).
- `fermata-Live.exe --recorder <port>` - **the recorder** (0.2.0) plus what both stations need from the audio device: the live monitoring mix, the
  talkback, the stations' playback streams, the waveform peaks and the flags. Nothing in it reads a project or draws anything.
- `fermata-Live.exe --fixer` - **the Fixer station**: the delayed take window, its own mixer (kept in the Fixer's own settings, not the project),
  ReTouch, replies. It reads the project and the session's chunks; it never writes the project. Started from the Primary's menu or on its own.

### The recorder's new services
- **Monitoring** (`MonitorMix`): the Primary window turns the processing mixer (levels, pans, mutes, solos, the main Ext Bus's output pair) into
  a gain matrix, input -> output, and sends it (`mix`). The audio callback applies it after the block went into the ring (so nothing in the
  monitoring can touch the recording), ramping every gain over the block. No plug-ins in the recorder (the brief: plug-ins run outside it).
- **Talkback**: the inputs marked TALK, to the talkback outputs, while the talkback is open (`talkback`; the Talkback square is purple then).
- **Playback streams** (`StreamServer`, control port + 1, binary): a station's player renders its own mix and sends stereo to the recorder,
  which adds it to the output pair the station chose. Credit-based (the station sends only what the recorder's small ring has room for, so the
  latency is that ring: about 85 ms), with a generation number so Stop and a locate drop what is queued at once. A station that stalls is silence.
- **Waveform peaks** (`PeakTap`, its own thread, reads the ring like a drive does): min / max of every ~10.7 ms (512 samples at 48 kHz) of each
  recorded channel, companded to 8 bits (sqrt: about -84 dB floor). Sent to the stations as they come (`peaks`), and kept for the whole
  session so a station that starts later gets the history (`peaks-history`).
- **Flags, replies and fix versions**: the Primary pins a flag (`flag` {id, sample, note}); the Fixer replies (`reply`) and announces a fix
  version (`fix`). The recorder keeps them in the session (in every drive's index: `flags`, `fixes`) and sends each to every station at once.
  The Primary window puts them into the project (flags as take-window markers, fix versions in the take window) as soon as their take is there.
- **Device names**: the status carries the inputs' and outputs' names and counts, and the outputs' meters.

### The shared model (contract 3)
- `Marker` (core/Markers.h) gets the flag fields: `flag` (true for a flag), `note`, `replies` [{text, by, at}], `pinnedAt`. fermata shows a flag
  as an ordinary marker whose name carries the note and the last reply, and keeps the fields when it saves.
- `TakeWindowDef::fixVersions` (core/Takes.h): every fix the Fixer made - the take, the span (take samples), one file per track holding the fixed
  span, what was done, the flag it answers, the version it retries, its state (waiting / approved / rejected: set by fermata's review, to come).
  The original recording is never changed.
- Shared from fermata's own folder (they join the shared list, unchanged): `src/ui/Uikit.h`, `FermataLookAndFeel.h`, `FontData.cpp`,
  `FontData2.cpp`, `WaveColor.h`, `WaveStore.h`, `WaveThumb.h`, `WinOwner.h/.cpp`, `MixerLook.h`, `DeskLook.h`, `ValueEntry.h`, `WaveDraw.h`. The
  compatibility check makes sure fermata:Live uses nothing else from `src/ui` and that these use nothing but shared code.

### The Primary station
- **Main window** (fermata's desk look): the menu bar (Project, Audio settings, Preamps, Project Designer, Take Window, Mixer, Bounce out, Fixer,
  Help) and the status squares (Talkback, recorder, drives); the top row (take counter, project, rate, disk free, the recorder's state); every input
  of the driver as a desk strip (your name, the SEO input number and the device: "01-Anubis-01", the gain dial and preamp keys where a Merging
  device controls the input, the meter, TALK); the driver outputs along the bottom (name, what feeds it, meter, TB).
- **Primary take window** (fermata's take window, reduced): piece name, Next Take, the REC light, take length, the H:M:S.MS display, transport,
  ARM per track with Arm all / Arm none, markers (M), flags (Enter: pin and type the note, Enter again: saved), the Fixer's replies under the flag,
  the takes on the timeline with the one recording growing at the end (from the peaks), the merged takes from their files.
- **Mixer**: the project's processing mixer as faders, pans, mute / solo and the output pair - what the Primary hears live, and what Bounce out uses.
- **Bounce out** (where fermata has Mastering): a take, whole or between markers 1 and 2, through the processing mixer (the shared `BounceJob`),
  to a stereo WAV in Bounced Media.

### The Fixer station
- Reads the session's index and chunks (primary drive) as one timeline (`ChunkTimeline`: the chunks, the holes as silence, the merged take files
  once the chunks are gone), fills as audio is written, the flags from the recorder at once (even before their audio is on disk).
- **Follow Primary** (on by default): the playhead stays a little behind the written audio and cannot be moved; untick it to move about.
- Its own mixer (levels, pans, mute / solo, output pair), applied by its player, sent to the recorder's stream port.
- **ReTouch**: a selection (time, tracks) and a fix (De-click, Repair, Denoise - the shared processing in core/AudioOps.h), auditioned in place,
  then saved as a fix version (new files in `Recorded Media/<window>/Fixes`, the version announced to the recorder). Never changes the recording.

### Changed or added while it was tried out
- **The recorder and the Fixer are started with `startProgram`** (src/ui/WinOwner.cpp), which hands them none of the window's open handles. With
  JUCE's `ChildProcess` (bInheritHandles = TRUE on Windows) a child was given the window's sockets, which then stayed open in it after the window
  had closed them. RecorderLink no longer asks whether "its" child runs: it starts a recorder at most every 8 s, and a second one started while the
  first comes up cannot take the port and quits. The Primary starts the Fixer at most every 10 s (a Fixer already open says so and closes).
- **A session that was cut off** (the PC or fermata:Live stopped while it wrote): `PrimaryApp::findCutOffSession` finds the newest session of the
  piece whose primary index still says "writing" and that has a take not in the project yet (recording, not merged, or not registered). The window
  offers to carry it on as soon as the device runs (once per project opened; not while it restores its own session after the recorder was started
  again), and REC / Pre-take carry it on instead of starting a new one - a new session would leave those takes out of the project for good.
  A session that only wrote its pre-take is not offered (a new one starts).
- **The take left recording by such a stop** (`LiveRecorder::Setup::resumeTakeMaxGapSeconds`, 600): after a shorter stop it goes on, the missed
  stretch a gap in it (0.2.0's behaviour, right when the recorder was started again at once); after a longer one it ends where the audio on the
  disk ends (the primary writer measures the chunk the stop left open) and is merged at once, so a session carried on the next day does not make a
  take of hours of silence. The "session" event says which (`recordingTake`, `endedAtCut`). Tested in live_recorder_test.
- **The processing mixer was silent in projects made by fermata:Live**: the program never set `Project::newStripsToMain` (fermata does, in
  AppContext), so new strips had their output off - nothing was heard live or played back, and Bounce out wrote silence (found by bouncing: peak
  -100 dBFS). The Primary sets it now, and a project whose processing mixer has no track going to its main output gets every track sent there when
  it is opened (logged).
- **A fix version needs the session**: the recorder keeps the versions (every drive's index) and the Primary puts them into the project, so with no
  session there is nowhere safe for one. The Fixer now says so and writes nothing (it used to write the files and then be refused).
- **The flag lists** (both stations) show the project's flags from earlier sessions too (the take window's flag markers): the Primary can edit
  their notes (in the project directly); the Fixer shows them but replies only to the running session's flags. A flag pinned between takes says
  so on the Fixer ("between takes"), not "on its way to the disk".
- **The Fixer's output pair**: 3-4 by default; on a device without them (the Test signal has two outputs) it plays on 1-2, beside the Primary,
  rather than not at all (`FixerApp::playPair`), and says so once.
- **Meters in the mixers**: the Fixer's strips show what its player plays (`Player::takePeak`, before the fader); both mixers' MAIN OUT show the
  output pair's level (the recorder's output meters: everything played there).
- **The timeline repaints only when something drawn changed** (`LiveTakeView::repaintIfChanged`: playhead pixel, live end, new peaks, flags, view,
  M / S / ARM...), and twice a second regardless; it is opaque, so the window behind it is not painted again. Both windows used to repaint all of it
  30 times a second.
- **Windows fit the screen** they open on (the take window was 1360 x 820, too big for a laptop).
- **Keyboard**: every button of the take window, the Fixer and the mixer strips is set not to take the keyboard - a button that has it presses
  itself on Enter, and Enter must always pin a flag (found while testing: Enter pressed REC, which asked to stop the take).
- The Primary's log has the operator's actions (REC, STOP TAKE, flag pinned, flag note) - what was pressed when, for after a concert.
- The "Test signal" tones are at -24 dBFS (0.2.0: -12): 16 of them added up in the mixer clipped the outputs and the bounce.

### The audio device: the recorder's own, and watched (4 November 2026)
Asked for after fermata crashed while a new interface was plugged in (the crash was in Merging's WASM driver; see the user's fermata conversation):
"fermata should never try to switch to a new driver without asking the user first". Looking at fermata:Live for the same found more:
- **The real drivers were never listed (0.2.0 and 0.3.0 until now).** `RecorderService` added "Test signal" to its `AudioDeviceManager` before
  anything else, and JUCE makes its own list of driver types only while that list is empty (`createDeviceTypesIfNeeded`): on Windows the recorder
  offered "Test signal" and nothing else. Now JUCE's list is made first (`createAudioDeviceTypes`), then "Test signal" is added beside it. (Shown
  here with a small program and a manager that stands in for Windows: the old order listed "Test signal" only, the new one all three.)
- **The device is the recorder's own** (`RecorderService::device`): made from its driver type, opened and started by the recorder, and closed only by
  it. The `AudioDeviceManager` is kept only as the list of driver types and never has a device open. Through the manager (0.2.0), JUCE itself
  closed a device that had left its driver's list and opened the saved or default one in its place (`audioDeviceListChanged`, private in JUCE):
  another device, other inputs, maybe another rate, in the middle of a session - with nobody asked. One initialisation of the driver now, not two
  (0.2.0 made it once to read its channels, then the manager made it again).
- **Devices plugged in or pulled out** (`ListWatch`, a listener on every driver type): noted in the log ("plugged in: ... The recorder keeps using
  ...") and told to the window (`device-list`), whose device list is sent again. Never acted on. If the device in use has gone, the window says so.
  (ASIO's list is the installed drivers, so plugging in an ASIO interface changes Windows Audio's list, not ASIO's.)
- **No audio for the session** (`watchAudio`, the timer; and the audio thread): the session gets no blocks - the driver stopped the device,
  restarted it, gives nothing, or started it again at another rate. While it lasts beyond 2 s: `audio {state: missing, why, seconds, session}`,
  the status carries `audioMissing` / `audioWhy`, and the Primary shows NO AUDIO - NOT RECORDING (recorder box, status line, the take window's hint
  line, in red) with what to do in the log. When it comes back, a **flag** (by "Recorder") is pinned where it happened: "NO AUDIO for 1.2 s here (the
  driver stopped the audio device and started it again): that time is not in the recording" - so the take shows it in the Fixer, and in fermata as
  a marker. The stretch is measured by the audio thread when the next block comes (the time since the block before, less one block; more than
  four blocks and at least half a second), because a driver's reset runs on the message thread, where the timer cannot look while it happens.
  With no session, "the audio device gives no audio" after 2 s, and again when it is back.
- **Another rate** (`rateWrong`): a device that its driver starts again at a rate other than the session's is not fed into the session at all (its
  files are at the session's rate); the monitoring still runs. JUCE's ASIO device restarts itself with the old rate when its driver asks to be reset,
  and JUCE's WASAPI device with the new one when Windows' format changes.
- **"Test signal" pretends the faults** (`test-signal {action}`: `pause {seconds}`, `restart {rate}` - stop, 0.6 s, start, like a reset, at
  another rate if given - `die`, `plug`, `unplug`), so all of the above can be tried without hardware. Only the Test signal device obeys.
- The window forgets a session the recorder says has ended, whoever ended it: it is no longer carried on if the recorder later has to be started
  again (found here: a test ended a session from outside, the recorder was then stopped, and the window carried the ended session on).

### The code review (one reader, after the demo worked) and what changed
- **Critical: the carry-on cut used the primary drive alone.** A primary dropped long before the stop would have ended the take where it dropped,
  and the backups would then have deleted the rest of it as chunks of no take. The cut is now where the furthest drive got
  (`LiveRecorder::prepare`), and each writer of a session carried on takes in the whole-numbered chunks its index never listed (index writes that had
  been failing), so the end is where the audio really ends and the merge has them (`DriveWriter`'s constructor; tested).
- Takes merged after End session lost their flags and fix versions (the station clears them when the session ends): the merge's "done" event now
  carries the session's notes (`RecorderService::mergeDoneEvent`, read again when the merge ends) and `PrimaryApp::registerTake` puts them in
  (`syncNotes`). A take that finished merging with no window open was never put into the project: with no session on, the window now looks
  through the ended sessions once per project and asks for those takes (`take-info`); the recorder notes them registered in the ended session's
  index (`takeRegistered`, only while no merge runs).
- The Fixer's `Project::load` could put the previous save back: the shared read mends a file it cannot read from its .bak, and a read between the
  Primary's two renames found none. `Project::readOnlyLoads` (shared, default off) - the Fixer sets it.
- `Player`: a render under way put its old place back over a `play()` (a seek, Follow's corrections, the ReTouch audition "from a second before"),
  so what was heard did not match the playhead. A seek count now stops it.
- The take window kept a pointer to the piece's markers, freed when a project was made or opened: the view now asks for them each time.
- The "quit" queued as the window closes could be lost (the link thread left without sending): `RecorderLink` sends what is queued before it goes.
- (0.2.0) A drive writer stuck in a disk call outlived its session's memory: the session object is now kept for good when that happens, and the
  stuck writers' list is never destroyed (not even at exit).
- Smaller: a new flag no longer takes the Fixer's chosen flag while a fix is tried; the log window refreshed only while the list grew (it stops
  at 400 lines); Open device is greyed out while the session is on; the playback window is at least two device blocks; REC pressed twice while a
  session starts; `findCutOffSession` reads the index without mending it (it may be a live drive's).

### Checked here (Linux, the Test signal device, a test display)
- The unit tests: live_recorder_test (with the new carry-on rule), compat_test (contract 3: a flag and a fix version round trip, a plain marker
  gains no new keys, the shared look files use only shared code), and the address / thread / realtime checks of the recorder core.
- End to end in the windows: the main window and its menus; REC starting the session and the take; the take growing live; flags with notes; the
  Fixer following about 3 s behind the disk with the hatched not-yet-written part; a reply shown under the flag on the Primary; a Repair tried on
  three tracks and kept as a fix version (three files in Fixes); STOP TAKE, the merge, the take in the project with its flag markers (notes and
  replies), its gap marker and the fix version; playback on both stations through the streams (playhead, meters); the mixers with meters; Bounce
  out (silent before the routing fix, then the right level); talkback open and closed; Audio settings, Preamps and Project Designer; the processes
  killed in the middle of a take and started again - after a short stop the take went on with a gap of 1 min 21 s marked, after a long one it was
  ended where its audio stopped (13 min 16 s) and put into the project.
- The audio device's faults (the recorder process alone, from a Python client, on the Test signal device): 3.5 s of no audio (warned after 2 s,
  in the status while it lasted, back after 3.49 s, a flag); a restart of 0.6 s by the "driver" on the message thread (caught: 0.59 s, a flag, no
  alarm); a restart at 44.1 kHz in a 48 kHz session (nothing recorded, warned, a flag of 4.7 s when it was back at 48 kHz); a second device plugged
  in and pulled out (noted, the device in use unchanged); the three flags in the index and inside the take, and the take short by exactly their
  time; the device dying in a take (warned, no other device opened, the take merged once the session ended); the device opened again by the window.
  In the windows: the take window and the main window showing NO AUDIO during an 8 s stop, the flag in the list afterwards, and after STOP TAKE the
  flag as a marker on the take in the project file.
- About the test display: JUCE on X11 can leave a click read into Xlib's queue undelivered until the next event arrives (and with no window
  manager the focus misbehaves), so the first tests looked as if clicks were lost. With a small click-to-focus window manager and a nudge of the
  pointer after each action everything arrived. This is the Linux test setup only; Windows does not work that way.

### Not checked here (needs Windows and the MAD driver)
- The MSVC build (GitHub builds it), Direct2D drawing speed, real audio out of the outputs (here only their meters show it), ANEMAN and the
  preamps, two stations on one PC under load, and how long the buffer and chunks must really be.

### Known limits (0.3.0)
- No plug-ins in the monitoring yet (stage 4: they run in a helper process).
- A session carried on takes its takes from every drive's index (0.3.0: a take begun or ended while the primary's index could not be written is
  known to the backups'); one that no drive's index got before the stop is not known at all - its audio is in the chunks, but no take says so.
- Fix versions only while a session is on (see above).
- In the Primary's take window a flag that is in the project shows twice: in the flag row and, as fermata will show it, in the marker row.
- One take window (piece) per session; another piece means ending the session.
- The load-test program for the studio PC (stage 5) is still to come.
- Time with no audio is not in the take (a flag says where and how long): the audio after it follows straight on, so the chunks' time of day is
  behind the clock by that much from there on. (A session carried on after a crash keeps in step with the clock: its missing stretch is a gap.)
- A take stopped while the device gives no audio is merged when the session ends: its last chunk is only finished then.
- The device is never opened again by itself: if it dies, STOP TAKE, end the session, open it again (the warning says so).
- fermata:Live does not yet ask "keep the device, or change to the new one?" when one is plugged in (fermata's version of that question is to be
  designed in its own conversation first); it notes the new device and keeps the one in use.

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
  merge-backup {folder, number}, resume-drive {folder, role}, quit` (0.3.0 adds `mix, talkback, flag, flag-note, flag-remove, reply, fix, notes,
  peaks-get, test-signal {action}`; and the events `audio {state missing | back, seconds, why, session, flag}` and `device-list {added, gone,
  current, currentGone}`; the status's device has `audioMissing`, `audioWhy` and `audioSession` while there is no audio; 0.3.1 adds
  `quit {whenDone: true}`, `merge-take {folder, number}` and `test-signal {action: hang-on-close, seconds}` - see 0.3.1 above). Folders must be full paths. `open`, `close-device`, `end-session` and `quit`
  are refused while a take records (`refused` event); plain `quit` also while a merge is going; `record` refuses a take number already used in the
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
