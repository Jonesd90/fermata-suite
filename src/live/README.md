# fermata:Live

The recorder for live concerts, from the makers of fermata. It is built for recordings that can never stop or restart, and its projects open in
fermata.

## Installing

fermata:Live is a single program, `fermata-Live.exe`. Put it anywhere and double-click it. The first time, Windows may say it "protected your PC"
because the program is not signed: click **More info**, then **Run anyway**.

## Trying this version (0.6.0)

1. **Audio settings**: choose the driver type and the device ("Test signal" is a pretend 16-input device for trying everything without an
   interface), the sample rate and the buffer ("Largest the driver offers" is the safest), and press **Open device**. Add a folder on each
   backup drive, and set the pre-take (how far back a take reaches before you press REC, up to 300 seconds).
2. **Project > New project...** (or open a fermata project). **Project Designer**: name the tracks, mono or stereo, and the input each records.
   The main window shows every input of the driver with its meter.
3. **Take Window**: type the piece's name, arm the tracks (ARM), and press **REC** (or R). The session and the take start together; the take grows
   on the timeline as it records. **Enter** pins a flag: type its note and press Enter again. **STOP TAKE** asks first, then the take is merged
   and put into the project (the Merge square is blue meanwhile). The R key never stops a take.
   **Time of day** turns the timeline into the clock (each take at the time it was recorded, the date at each midnight). While a take records,
   click the flag band to pick a moment that has passed (the flag point) and press Enter or N there; a click on the tracks goes back to now.
   **N** pins a note of your own and opens a box to write in (the Fixer does not see it); **Report...** writes a PDF of every note, flag and
   reply of the piece. Left / Right zoom round the playhead, Up / Down change the track height, **P** puts marks 1 and 2 round a selection or a
   take whose header you clicked, **M** shows and hides the mixer, **Colours** colours the waveforms. Project > Your name puts your name on
   your flags and notes.
4. **Fixer** (in the menu) opens the Fixer's window: the same takes, a few seconds behind the disk (Follow Primary). The Primary's flags appear
   at once on the right: click one and type a reply. To fix something: drag over the tracks, choose the fix, **Try it** (it plays from a second
   before), then **Keep as a fix version**. Or press **ReTouch window...** for fermata's own ReTouch window on that part (draw a box round the
   noise, ReTouch, listen, **Keep as a fix version**). The recording itself is never changed; the version goes into the project for fermata's review.
   A drag takes every track of the take; **Alt + drag** only the tracks you drag over. The Fixer has its own notes (N), its own Time of day
   switch and a box for its name.
   **Second seat...** (0.6.0, Windows): with the Fixer on the same PC as the Primary, a USB keyboard and mouse of the Fixer's own that work the
   Fixer only - an orange pointer with the Fixer's name, kept on the Fixer's screen - while the Primary's carry on as normal. It needs the free
   Interception driver: `docs/LIVE-SECOND-SEAT.md` says how to install it, how to choose the two devices (Learn), and how to give them back
   (Esc five times on the Fixer's keyboard, Let go, or closing the Fixer).
5. **Mixer**: what you hear (live and when playing back) and what **Bounce out** uses. The Fixer's **Mixer** button opens its own, on its own
   outputs (3-4 unless you choose others; 1-2 on a device with two outputs).
6. **Bounce out**: a take, whole or between marks 1 and 2 (keys 1 and 2 in the take window), to a stereo WAV in the project's Bounced Media folder.
7. Close fermata:Live, then open the project in fermata: the takes, the flags (as markers, with the notes and replies) and the fix versions are
   there. (Open a project in one program at a time: the two do not yet stop each other from saving over the same project.)

If the PC or fermata:Live stops in the middle of a session, start it again: it offers to carry the session on. A take that was recording goes on
(after a short stop, with the missing time marked as a gap) or is ended where its audio stops and put into the project (after a longer one).

fermata:Live never changes the audio device by itself: plugging in or pulling out an interface is only noted in the log. If the audio device stops
giving audio during a session, both windows say **NO AUDIO - NOT RECORDING** in red at once; when it comes back, a flag marks the place on the take
with how long it was missing. If it does not come back: STOP TAKE, end the session (the Pre-take square), open the device again in Audio settings,
then REC.

The recording runs in a separate program, `fermata-Live.exe --recorder`, started by the main window (it has no window of its own; it is in Task
Manager). It keeps recording if the windows are closed or crash; start fermata:Live again and it is found. Closing the main window while a take
records leaves it recording on purpose - press STOP TAKE first if you mean to stop.

Closing fermata:Live with nothing running closes everything, the recorder too. With the pre-take on, or a take still being merged, it asks: **Close
everything** (the pre-take ends, the audio device is let go at once, and fermata:Live closes once the merge has finished and the take is in the
project), **Keep the pre-take running** (only the window closes; the recorder goes on, and is found again next time) or **Cancel**. While it waits
for a merge, **Close now** closes at once: the recorder finishes the merge by itself, and the take goes into the project the next time fermata:Live
opens. A recorder left with no window for a minute, and nothing recording, merging or running, closes by itself.

Logs, in `Documents\Fermata`: `fermata-live-log.txt` (the main window), `fermata-live-recorder-log.txt` (the recorder) and
`fermata-live-fixer-log.txt` (the Fixer). Each program writes its closing steps there, ending "Closing: finished cleanly in ... s"; one that does not
finish closing within 15 seconds writes where it is stuck and ends itself.
