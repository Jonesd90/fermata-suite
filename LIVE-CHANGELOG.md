# fermata:Live - version history

fermata:Live is the live-concert recorder that goes with fermata (see `docs/LIVE-BRIEF.md`). Version numbers are MAJOR.MINOR.PATCH. The number is set in one place, the first lines of `src/live/Live.cmake`, and appears in the window's title and in the properties of `fermata-Live.exe`.

- **PATCH** (0.1.1, 0.1.2 ...): bug fixes and small tweaks only.
- **MINOR** (0.2.0, 0.3.0 ...): a new feature or a visible change.
- **MAJOR** (1.0.0 ...): the first version for real concerts, and later large changes in how the program works.

A line that starts with **Shared:** is a change to the code fermata shares (see `COMPATIBILITY.md`), so fermata's next build mentions it too.

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
