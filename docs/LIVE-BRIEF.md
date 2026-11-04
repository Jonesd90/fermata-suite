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
- Keep: piece name, next take, record light, take length, transport, waveform display and loop, markers, time code, arm all and arm none. Align
  spot mics is optional.
- Remove: bars system, matching, import takes, pitch and curve, ReTouch, export for processing, search for bar, take sheet.

### Fixer window

- Like the Primary take window, but delayed, with a timeline that fills as audio is finalised.
- Has its own mixer, so the Fixer's changes affect only the Fixer's monitoring and playback.
- Has ReTouch.
- Has "Follow Primary", on by default. Follow keeps the playhead at the buffer distance behind live. While Follow is ticked, the playhead can't be
  moved, so the user unticks it first.
- Flags from Primary appear straight away at the point in the timeline where their audio will land. They show even before that audio has been
  finalised.
- Fixer can type a reply to a flag. Replies are text and are shown on Primary's screen straight away.
- Fixes are saved as new versions. The original recording is never changed.

### Fix review

- Fixer's audio does not go back to Primary.
- When the file is opened in full fermata, the user goes through each fix and approves or retries it, one case at a time.
- Each fix is stored as its own version, so a retry never overwrites an earlier attempt.

## Flags

- Pinned by Primary, with a typed note and a timestamp.
- Shown on both screens at the timestamp.
- Replies from Fixer are shown on Primary's screen.

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
- Drive-failure and gap handling, to be confirmed.
- Whether the same-PC setup works under load.
