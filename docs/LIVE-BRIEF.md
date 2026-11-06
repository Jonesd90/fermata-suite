# fermata:Live: the brief

The user gave this brief on 4 November 2026. It is kept here as written, with one correction about sample rates: fermata:Live offers **the same
sample rates as fermata** (`Project::kProjectRates`: 44.1, 48, 96 and 192 kHz). The 352.8 and 384 kHz rates in the original are left out, and so
are 88.2 and 176.4 kHz, which fermata does not offer as project rates either. Decisions made since are in `docs/LIVE-DEVELOPMENT-NOTES.md`.

## Purpose

A companion program to fermata, in the same spirit as Nuendo Live alongside Nuendo. It records live concerts, where recording can never stop or
restart. There are no second takes. A project usually holds one piece, or two halves of a concert, plus perhaps some rehearsal, and around 10 takes
at most.

## Core requirements

- The primary objective is a program so stable and simple that it cannot crash or stop recording.
- Once recording starts, nothing the disk, driver or machine does should interrupt it.
- Use the safest buffering the MAD ASIO driver allows. Input comes through the MAD ASIO driver on the Ravenna setup used for fermata.

## Channels

The number of input channels is whatever the MAD panel allows at each sample rate, in and out. The user's figures from the MAD panel (not yet
checked against Merging's documentation) are 288 at 44.1/48 kHz, 144 at 96 kHz and 72 at 192 kHz. Typical use is 8 to 30 tracks. The program
reads the count from the driver and never writes it into the code.

## Look and feel

Looks like fermata, but much reduced.

### Main window

- Like fermata's main window: all inputs, with driver outputs at the bottom.
- Gain dials at the top, each named, showing the SEO input number and device name (for example, "01", "Anubis 1").
- Preamp buttons for each device type, reused from the existing code.
- Talkback stays, including the option to assign an input as talkback.
- Unused inputs still appear, so the layout matches the full channel count.

### Windows and menus

Keep unchanged: Projects menu, Audio settings, Preamps, Project Designer, Plug-in list and CPU cores, VST3 support.

### Primary window

- The same as the take window in full fermata, filling live. Primary hears the recording straight from the input, not from disk.
- Primary pins a flag with the keyboard. One press of Enter pins the flag and opens the note box. A second press of Enter closes the box and saves
  the note.
- Flags are sent to Fixer immediately, so Fixer knows to look.
- Keep: piece name, next take, record light, take length, transport, waveform display and loop, time code, arm all and arm none. Align
  spot mics is optional. (Markers were replaced by notes in 0.5.0: see Notes.)
- A switch between a **time of day** timeline and fermata's timecode (starting at 00:00:00.000, as before). With the time of day, the playhead
  runs on with the clock and the Primary only switches recording on and off. The timeline runs on past midnight into the next day, and shows the
  date once at each midnight, as DD/MM/YYYY. (0.5.0; the Fixer has the same switch.)
- While a take records, the Primary does not move a playhead. A small **flag point** in the flag band is clicked to place a flag (and its text)
  at a moment that has passed; a click on the tracks puts it back to NOW. When nothing records, the playhead is there to audition the takes.
- **Zoom** follows fermata's arrow keys (left and right zoom, up and down change the track height) and fermata's way of putting what it zooms
  round in the middle of the screen (SADiE's). It zooms round the flag point when one is chosen. Scroll bars along the bottom show any part of
  the recording.
- **P** puts marks 1 and 2 round a selection, or round a whole take after its header is clicked (for Bounce out). In the Fixer, P with a take's
  header clicked makes the whole take the part to fix.
- **Coloured waveforms** (fermata's WaveColour), switched on and off.
- **M** shows and hides the station's mixer.
- Each station can be given a **name** (a person's name or nickname), so flags, notes and replies read "by NAME".
- Remove: bars system, matching, import takes, pitch and curve, ReTouch, export for processing, search for bar, take sheet.

### Fixer window

- Like the Primary take window, but delayed, with a timeline that fills as audio is finalised.
- Has its own mixer, so the Fixer's changes affect only the Fixer's monitoring and playback.
- Has ReTouch. A drag on the timeline chooses every recorded track of the take for the ReTouch window; Alt + drag chooses only the tracks dragged
  over (0.5.0).
- Has "Follow Primary", on by default. Follow keeps the playhead at the buffer distance behind live. While Follow is ticked, the playhead can't be
  moved, so the user unticks it first.
- Flags from Primary appear straight away at the point in the timeline where their audio will land. They show even before that audio has been
  finalised.
- Fixer can type a reply to a flag. Replies are text and are shown on Primary's screen straight away.
- Fixes are saved as new versions. The original recording is never changed.
- **The second seat** (0.6.0, asked for on 6 November 2026): when the Fixer runs on the same PC as the Primary, the Fixer can have a mouse and a
  keyboard of its own, which work the Fixer only. Its mouse moves an orange pointer of its own, with the Fixer's name, kept on the Fixer's
  screen; the Primary's mouse and keyboard carry on as normal and are never touched. It needs the free Interception driver installed on the PC
  (docs/LIVE-SECOND-SEAT.md). Stream Deck control is for the user to plan and ask for later.

### Fix review

- Fixer's audio does not go back to Primary.
- When the file is opened in full fermata, the user goes through each fix and approves or retries it, one case at a time.
- Each fix is stored as its own version, so a retry never overwrites an earlier attempt.

## Flags

- Pinned by Primary, with a typed note and a timestamp.
- Shown on both screens at the timestamp.
- Replies from Fixer are shown on Primary's screen.

## Notes

- Asked for on 6 November 2026, replacing markers (which were fermata's plain numbered markers, and never reached the Fixer). The N key.
- The Primary and the Fixer each have their own notes. Clicking one gives part of the screen to a text box, so anything can be written in it.
- An export report of the recording lists all the notes at the places they were made, with who wrote each (0.5.0: with the flags, replies,
  gaps and fix versions too).

## Recording and redundancy

- Recording is in separate mono and stereo files, one per track.
- The recording is dry. No plug-ins sit in the recording path.
- Chunks are 30 seconds. An index lists every chunk in order, so playback reads them as one timeline.
- Recording goes to more than one drive at once, as many as possible without reducing stability. One drive is primary. The others are backups.
- If a drive fails, it is dropped and the others keep recording, with a warning shown on screen. (Proposed.)
- If every drive stalls and the buffer runs out, recording continues. Lost audio is replaced by a timestamped gap marker. (Proposed.)
- Buffer: 30 to 60 seconds in memory. (Proposed.)

## Merging

- Once recording stops, the primary drive's chunks are merged automatically, one track at a time.
- Backup drives keep separate files and can be merged on demand with a button, but only when nothing is recording.
- The merge is interruptible. A new recording can start at any time, and the merge pauses and resumes from its last completed track.
- Each merged file is checked against its chunks before the chunks are deleted.
- The merge runs as a separate background job and must not slow recording or freeze the interface.

## Pre-take

Pre-take goes up to 5 minutes, written to disk, not held in memory.

## Mixers and monitoring

- Each user has their own mixer, on any station, including Primary.
- Mixers can use VST effects, including on Primary's live monitoring. These apply only to monitoring, after the audio has been written to disk.
  Plug-ins never sit in the recording path.
- Each mixer's output is a stereo pair of ASIO outputs chosen by the user. Any pair can be chosen. Two mixers may share a pair, and their audio
  sums.
- To build next (0.7.0, asked for on 6 November 2026 - it was to be 0.6.0; the second seat came first): mixers that look better - VCAs, internal buses, an insert slot for a reverb on an internal
  bus, a fader cap that looks like a real one, wider meters, and a master with a phase scope, a peak meter, an RMS meter and a LUFS meter
  (momentary, short-term, and integrated since REC). Stereo strips keep a pan for each channel, labelled L and R.
- **The reverb on the Primary's live monitoring runs outside the recording path**, in a small program of its own: the dry sound stays instant
  (the recorder's routes) and the reverb comes back slightly late (aim: about two audio buffers). The user accepts that: it is an extra, and it
  must never be in the recording path. (The user's answer, 6 November 2026.)
- **The stations can send each other their mix** during a recording, without interrupting the audio stream and without clicks in the recording
  or the monitoring: it copies the mixer settings (not the sound). The receiving station shows "Listening to NAME's mix" with Keep or Revert;
  both mixes are kept, with a toggle button to flip between them. (The user's answer, 6 November 2026.)

## Bounce out

Replaces Mastering in the top row. It creates a stereo mix through the main processing mixer, of either the whole take or the span between markers 1
and 2.

## Project file

The same format as fermata. It opens in fermata as a take window, where edits and fix reviews are made.

## Stations and testing

- Two users: Primary and Fixer. The Safety window has been dropped.
- Plug-ins used in monitoring must run outside the recorder process, so a plug-in crash cannot stop the recording.
- Test whether both stations can run on the same PC. Keep the recorder as its own high-priority process, and run the Fixer as a separate process that
  can be closed without affecting the recording.
- Test with ReTouch running during a recording, playback reads from chunks being written, and disk load on all drives at once.
- If the test fails, a second PC for the Fixer is the fallback.

## Still to test or decide

- Buffer length, to be tested on real hardware.
- Two people on one PC: the second seat is built (0.6.0, the Interception driver, chosen by the user from five ways set out on 6 November
  2026). Still to test on the real PC: the driver, the user's own keyboards and mice, and that the PC's keyboard never moves to the Fixer.
  Stream Deck (the user has a Neo and a Stream Deck + XL): the user will say later how they should work.
- Drive-failure and gap handling, to be confirmed.
- Whether the same-PC setup works under load.
