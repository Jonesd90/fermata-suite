# fermata:Live - version history

fermata:Live is the live-concert recorder that goes with fermata (see `docs/LIVE-BRIEF.md`). Version numbers are MAJOR.MINOR.PATCH. The number is set in one place, the first lines of `src/live/Live.cmake`, and appears in the window's title and in the properties of `fermata-Live.exe`.

- **PATCH** (0.1.1, 0.1.2 ...): bug fixes and small tweaks only.
- **MINOR** (0.2.0, 0.3.0 ...): a new feature or a visible change.
- **MAJOR** (1.0.0 ...): the first version for real concerts, and later large changes in how the program works.

A line that starts with **Shared:** is a change to the code fermata shares (see `COMPATIBILITY.md`), so fermata's next build mentions it too.

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
