# Fermata - development notes (internal: change history, build steps, details of every feature)

The look is a dark console look (dark greys, near-white ink) with the Fermata logo teal as the accent and square 1-pixel-outlined controls. It applies to every window. The typeface is Inter (bundled, SIL Open Font License, see assets/Inter-LICENSE.txt). Buttons share one size and one column grid.

## PART 1 - Get the program (no installing anything, about 20 minutes, one time)

GitHub builds the program for you on its own computers. You only click buttons.

1. Go to https://github.com and create a free account (or sign in).
2. Click the **+** at the top right, then **New repository**. Name it `Fermata`. Choose **Private**. Click **Create repository**.
3. Unzip the `Fermata.zip` I gave you on your computer. You get a folder called `Fermata`.
4. On your new repository page click **uploading an existing file**.
5. Open the unzipped folder, select EVERYTHING inside it (including the hidden `.github` folder - if you can't see it, in File Explorer click View > Show > Hidden items) and drag it all into the browser window. Wait until every file has uploaded.
   *The `.github` folder is essential: it contains the build recipe.*
6. Scroll down and click **Commit changes**.
7. Click the **Actions** tab at the top. If GitHub asks, click **I understand my workflows, enable them**.
8. The build starts by itself (or click **Build Windows app** on the left, then **Run workflow**). It takes 10-20 minutes. A yellow dot means working, a green tick means done, a red cross means failed.
9. When it is green, click the run, scroll down to **Artifacts**, and click **Fermata-windows**. A zip downloads. Unzip it: inside is `Fermata.exe`.
10. Double-click `Fermata.exe`. If Windows says "protected your PC", click **More info** then **Run anyway** (it is your own program, not signed).

**If the build shows a red cross:** click the failed run, click the red step, copy the last 50 lines of the red text and send them to me. I will fix it. This is the first-ever Windows build, so one round of fixes is likely.

## PART 2 - First start

1. Press **Audio settings...** Choose the **ASIO** type and your Merging device. Set the sample rate. Press **Apply**, then **Close**. All driver inputs and outputs (e.g. 40 and 40) are switched on automatically.
2. The main window's **Driver Inputs** page lists every driver input (numbered in the order the driver shows them) with a live meter, a box for your own second name (e.g. ORTF L), and the preamp controls: gain, MIC/LINE, 48V, polarity, low cut. These are the only preamp controls; every mixer shares them. Each input row also has an **ARM** button next to the preamp controls, and the monitor buttons **E / S / S-L / T**. They apply to every track that is fed by that input (they are greyed out until a track uses the input). The tracks themselves are listed in the Project Designer, **2. Audio tracks** tab. (On a small laptop screen the main window shrinks to fit.)
3. Press **Project Designer**. The **I/O** tab lists every driver input and output (you can name them). In **Tracks**, add tracks (mono / stereo / surround); the list fills as you add. Rename, drag to reorder, right-click to delete. Choose each track's input (a stereo track on input 5 auto-fills 5 & 6; change the second if you need to).
4. Each driver input row in the main window has monitor buttons for the tracks it feeds: **E** (live only when armed), **S** (always live, muted while a take plays back), **S-L** (live even during playback), **T** (talkback: on between takes, completely off while recording).
5. Project > Save. Recordings go in `Audio/<Take window name>/`.

**A new project is completely empty:** one mixer (the processing mixer), no tracks, no buses. There is **no Main / master output and no effects channel**. Sound reaches the outputs only through an **Ext bus**: add one in the Project Designer (tab 4), send your tracks to it with the dial on each strip, and pick its pair of driver outputs on its own strip in the mixer.

## Main window layout

Every window has the "desk" look: the "dark" palette in Uikit.h (theme::setDark) is the blue desk, tinted to the colour chosen in Project > Colour theme (setting `deskColour`, applied with applyDeskColour in ThemeSwitch.h, which recolours every open window; theme::generation() tells the mixers to rebuild). Every ToolWindow draws wooden side panels and a wooden bottom strip outside its content (ToolWindow::getContentComponentBorder / paint; not in the light look). The main window (src/ui/MainWindow.cpp, src/ui/DeskLook.h) has one top line (mark, project, driver state, DRIVER INPUTS) with the stage speaker boxes (square, ring knobs) at its right, the driver inputs as a row of vertical strips, the driver outputs as a row below with the audition buttons on their caption line, and a wooden rail along the bottom carrying the tabs of minimised windows and the maker's name. The menu bar always stays on one line (layoutMenuBar shrinks the spacing, then the writing). The key pictures are SVG drawings kept as strings in DeskLook.h.

The top row of buttons: **Project**, **Audio settings**, **Project Designer**, **Take Window**, **Edit Window**, **Mixer** (and **Meter bridge**). There is no Record button here: recording is always started with **REC** in a take window. A faint Fermata mark sits behind the window.

## Mixers

- **Look.** Strips are drawn fine and dense: a coloured bar on top says what kind of channel it is (AUDIO, INT BUS, EXT BUS), a coloured name plate sits at the bottom, with recessed insert and send wells, a finely divided fader scale and a segmented meter that shares the fader's scale. Fermata starts in the **light look** (the desk colours); the sun / moon button (top right of every window) switches to the dark look (the same colours a shade darker) and is remembered. The background of each strip is tinted with the channel's colour (click the coloured bar or name plate to change it), and the waveforms in the Take and Edit windows use the same colour.
- **Channel colours.** Click the coloured bar at the top or the name plate at the bottom of any strip, pick a colour (or *Other...* for any colour, or *Automatic*). The colour goes on the fader cap, the send dials that feed it, the pan line, and on that track in the **Take and Edit windows** (name plate and clips). You can also right-click a track name in a take or edit window and choose *Colour...*. Audio tracks are blue, Int buses violet and Ext buses red until you pick one. Colours are saved with the project.
- Under the coloured bar is the ASIO input selector for that track (a stereo track has two). It is the same setting as in the main window and the Project Designer.

- Type a new name in the **Mixer name** box at the top and press Enter.
- Every pan slider is as wide as the strip and its exact position is written above it: **L100 ... C ... R100**. A stereo track has two (Pan L, Pan R): put the left channel on the right and the right channel on the left if you like. Mono tracks have one. Double-click a slider to reset it.
- **Delete this mixer** (top of a cue mixer's window, or **Mixer > Delete a cue mixer** in the main window, or the Project Designer's Mixers tab). The processing mixer (the first one) can never be deleted.
- Every channel strip has dials near the top for its sends (see "Track types and routing" below).
- Meters have a dB scale. Only the top changes colour (orange from -9 dB, red from -2 dB). The peak marker holds 10 seconds, but a peak above -2 dBFS stays until you press **Reset peaks** (the last strip).

## Keys that work in every window

- **Space stops the recording and R starts one, from any Fermata window** (a mixer, a plug-in window, the main window, a dialog), whatever has the keyboard - except while you are typing in a text box, with Shift / Ctrl / Alt held, or when another program is in front. R records into the take window that is in front, or the one that was in front last. In the edit window and the process windows R keeps its own job (butt / audition / do the fix), and R typed into a plug-in window is the plug-in's; Space still stops a recording there.
- **M** opens or closes the **processing mixer** (the first mixer, the one you run the session from). The other mixers are called **cue mixers**.
- In the **take window and the edit window**, **Left / Right arrows** zoom the time axis out / in and **Up / Down arrows** make the tracks smaller / bigger. They work on whichever window you are in.
- **B** opens or closes the **meter bridge**: a floating window (it stays on top) with a tall vertical meter for every driver input (before any fader, as they arrive from the preamps) and every driver output (after the faders). Peak hold works as in the mixers; **Reset peaks** clears it. The same window opens from the **Meter bridge** button in the main window.
- (Typing in a text box, such as a mixer's name, never triggers these keys.)

## Track types and routing

There are three kinds of mixer channel. Add and arrange them in the **Project Designer** (tabs 2, 3 and 4). Each list shows the items in the order you added them; **Up / Down** rearrange them and **Delete** removes them.

- **Audio track** - no input, or an input from the audio driver; mono, stereo (or surround). It can be sent to other audio tracks, Int Buses and Ext Buses.
- **Int bus** - fed by audio tracks and other Int Buses. It can be sent on to audio tracks, other Int Buses and Ext Buses.
- **Ext bus** - fed by any audio track or Int Bus. Its only way out is a pair of driver outputs (chosen on its own strip, in each mixer) or nowhere. **Ext buses are the only things that reach the driver outputs** (there is no Main or master output).

Sending is done with a **rotary dial** near the top of the channel's strip, one dial for each Int Bus and Ext Bus:
- Click, hold and move the mouse **up** to send more, **down** to send less (bottom = off).
- **Click once** (without dragging) for a small panel: the send level on a small fader, **PRE / POST** fader, and **one or two VST3 inserts** that sit between the signal and the send. These inserts only affect that send, never the audio going through the channel's own fader.
- To send to another **audio track**, press **+ send to track...** under the dials; a dial for it then appears.
- A send that would make a loop (A feeds B feeds A) is refused ("no loops!").
- Pre-fader sends ignore the fader but still obey Mute and Solo.
- Routing lives in each mixer, so every mixer can have its own routing. Old projects open with their effects channels as Int Buses.

## Cue mixer audition and copying mixes

The FIRST mixer in the list is the processing mixer (the engineer's). Every other mixer is a cue mixer (producer, composer...) and gets an **<its name> Audition** button, both at the top of its own mixer window and in the row under the main window's status line. The name on the button is the mixer's name, so rename the mixer and the button follows.

- Press it: whatever that mixer sends to its Ext buses is also played on the first output pair of the processing mixer's first Ext bus, and your own Ext buses go quiet, its window opens so you can edit it while you listen, and it stays that way until you press the (orange) button again. The other person keeps hearing their own mix on their own outputs the whole time.
- **Copy this mix to...** (top of the processing mixer's window) copies your levels, pans, mutes, solos, sends (with their pre/post setting) and bus levels onto one other mixer, or onto ALL of them. On a cue mixer the same button reads **Copy processing mixer's mix here...**. The Ext buses' output pairs are never copied. Tick "Also copy plug-in inserts" if you want those too.
- **Undo last copy** is in the same menu (one level).

## PART 3 - Recording

- Press ARM on the driver input(s) you want to record (main window), then press **R** (or the REC button) in a take window. **Space** stops the recording. Every armed track makes its own file; takes recorded together are grouped. The waveform appears live as you record.
- While recording (and while playing) in a take window, a red playhead moves left to right. When it reaches the right edge the window turns a whole page, so the playhead is at the left again, instead of scrolling continuously. If you scroll away yourself, it will not pull you back until it reaches the edge of the view again.
- Files are named like `005 - Symphony 2 - violin.wav`.
- Right-click a track name in a take window to remove that track from that window only (other take windows, the mixer strip, the edits and the audio files are untouched).
- **Take Window** (main window) opens a take window (one per piece, unlimited). Takes and Edits are independent: deleting a take leaves it in the edit.

## PART 4 - Editing (SADiE style)

1. In a take window, click the **ruler** at the top (the tall strip with the times) to place the **playhead**; clicking a take only selects it, and clicking an empty patch deselects every take. Press **1** / **2** to mark the edit IN / OUT: you can press them at the playhead, or *while the take is playing or recording* to mark by ear. (**I** / **O** are only for Bounce Out marks.) Press **3** to send the marked part to the edit (it goes after the last piece), or **4** to place it at the edit window's playhead (what is there is cut and the rest moves later). **Space** plays from the playhead (and stops again); during a recording Space stops it. When a recording ends the playhead goes to the start of that take, so Space plays it back. **Right / Left arrows** zoom the time axis in / out (they zoom around the playhead; Shift + arrow nudges the playhead); **Up / Down arrows** make all tracks smaller / bigger (8 steps, from 32 tracks per page to one stereo track per page). Send the piece to an edit with **To edit [3]** or **At edit playhead [4]**.
   - **Overdub box** (take window): when ticked, 3 and 4 lay the piece *over* the edit at the edit's playhead, and it plays at the same time as what is already there. Overdubs are drawn as narrower strips in the middle of each track row; click one to select it, drag it sideways (nothing else moves), Delete removes it. The edit's playhead is the purple marker on its ruler: click in the edit to move it.
   - **Volume inside a piece:** right-click a piece (or overdub) in the Edit window, **Volume change here...**. A small mixer opens with a fader for every file of the grouped piece and an **All** fader. Tick **Fade the change** to make it a glide of 0.1 - 1.0 s instead of an instant step. **Whole piece** sets one level from the start. The change is shown as a teal line (the glide shaded). **Remove this change** or **Remove all volume changes** undo it. Changes made while the edit is playing are heard the next time you press play.
2. **Edit Window** (main window) opens an Edit window. Drag a piece left/right to slide it. **Slip Left / Slip Right** decide whether the pieces to the left / right move along.
3. Click a join and press **T** (or double-click it) to open the **Trim window**:
   - **F2** auditions (the yellow playhead moves). **Enter** accepts, closes the Trim window and returns you to the Edit window.
   - Drag the top (A, out) or bottom (B, in) waveform sideways to slide that audio. Slip Left / Right toggles in the Trim window are separate from the Edit window's.
   - Crossfade: drag the corners in the top half of A for the out-fade only, the bottom half of B for the in-fade only, the middle band for both. The white line stays where the join originally was.
   - **A** copies the out-fade onto the in-fade; **Z** copies in to out.
   - **Right / Left arrows** zoom in / out (mouse wheel too).
   - In the Edit window the same arrows work: Left / Right zoom time, Up / Down change the track height.
   - **Accept**, **Next edit >>**, **<< Previous edit** accept this join and move on.

## PART 5 - Bounce Out (making the master)

**Bounce Out...** is a button in the Edit window and in the take window. It plays the audio **faster than real time** through the settings of the **processing mixer** (the first mixer) - or any other mixer you pick - and writes 24-bit WAV files. Your live mixers, plug-ins and audio device are not disturbed: the bounce works on a private copy.

**What to bounce**
- **Full Track** (Edit window) - from the first bit of audio to the last bit of audio used in the edit.
- **Use I/O points** - in the Edit window press **I** to set the IN and **O** to set the OUT at the playhead (or at the cursor if nothing is playing). They are drawn in green / red. In a take window the take's own IN / OUT marks are used.
- **Selected pieces** (Edit window) - click a piece of the edit, **Ctrl + click** more of them. Each selected piece becomes its own file (name - 01, name - 02 ...).
- **Selected takes** (take window) - click a take (it goes a little darker), **Ctrl + click** more. Each selected take becomes its own file (name - take 005 ...). They must all be at the same sample rate.
- **Whole take** (take window) - the take you clicked or marked.
- **Add before / Add after (0 - 9 seconds)** works with every range above: the file becomes exactly that much longer. Nothing but silence goes into the mixer during the extra time, so plug-ins settle and reverb tails ring out in it.

**Options**
- **Outputs to bounce**: tick any Ext buses, Int buses and / or audio tracks (their signal after their faders). Ext buses are ticked to begin with. **Every ticked output becomes its own stereo file**, so you can print a stereo mix and stems in one go. The file name ends with the output's name, for example `Symphony 2 - Out.wav`, `Symphony 2 - violin.wav`. (With only one output ticked, the "Add the output's name" box decides.)
- **Normalise**: the loudest peak ends up at the level you type (for example -0.1 or -1 dBFS). Normally every file is normalised on its own. Tick **Normalise all the files together** to apply one gain to all the files of a piece (the loudest reaches the level), so stems keep their balance with each other. If you do not normalise and something goes over 0 dBFS, the window tells you it clipped.
- **File name** and **Folder** (the last folder is remembered).
- **Mixer**: which mixer's faders, pans, sends and plug-ins are used (the processing mixer by default).

Bounced files are never overwritten: if the name exists you get "name (2).wav".

## Known limits (honest list)

- **Preamp control** (Hapi / Anubis / MT48 over Ravenna): the screens are there, but the link to the hardware is NOT connected yet. I need a capture of the control traffic from your rig. Use MADPanel for gains meanwhile.
- LTC timecode chase: not built yet. (3.56.0: LTC can come in, and go out, as MIDI timecode - the LTC fermata makes from it is recorded; nothing chases it.)
- Each recorded file must stay under 4 GB (about 58 minutes of stereo at 192 kHz).
- VST3 plug-ins run inside the program: one that crashes takes the program with it (autosave and the .bak files limit the loss). A plug-in that crashes the scan is skipped at the next scan (2.71).
- Surround tracks are folded to stereo for monitoring (placeholder).
- Insert plug-ins run as stereo effects.
- Windows/ASIO has been run on the real rig since 2.67 (40 mono / 20 stereo tracks at 24/96); keep sending the log lines.

## Preamp control (Anubis, Hapi, MT48)

Fermata talks to each device through the same web interface that its own web page uses (a WebSocket on port 80 of the device). Press **Preamps...** in the main window:

1. **Add my usual four** fills in Anubis .20, MT48 .30, THAP .40 and BHAP .50 (change any address that differs), or **Add device** for your own.
2. Press **Apply and connect**. The status column shows each device connecting and how many preamp channels it has.
3. **First input** says which driver input the device's first preamp feeds; its other channels follow in order. Press **Stack in order** to fill these in from the channel counts, then **Apply and connect** again. Change them if your Ravenna routing differs.
4. The gain slider, MIC/LINE, 48V, polarity and low-cut buttons on each input in the main window now control the real preamp. Only the setting you change is sent.

Safety: when Fermata connects it only *reads* the device (the controls show what the hardware has); it never sends the project's stored settings. The device list is saved with the project and remembered for new projects.


## Mixer outputs, main bus, fader (latest)
- Every Audio Track and Int Bus strip has an **OUT** button and a drop-down under its fader: this is the strip's output *after* the big fader. "Main" goes to the mixer's main Ext Bus; or pick any Int / Ext Bus.
- On an Ext Bus strip, tick **Main out** to make it the mixer's main output (only one can be ticked).
- Mute is now a stronger red; the fader is longer and its cap taller and rounded.
- The main window has no time read-out and shows the project and driver info on one line.

## This build
- **Piece:** the Take window has one name box. Every take is named after it ("001 - Piece - violin.wav"); a take can still be given its own name (double-click it).
- **ARM** now lives in the Take window: click ARM on a track's name to arm it, or use **Arm all / Arm none**. The main window no longer has ARM.
- **Crossfade shapes** (Trim window, "Fade shape"): Equal power, Linear, Cosine, Smooth S, Logarithmic, Exponential, Fast, Slow.
- **Minimise** sends a window to a title bar along the bottom of the main window; click it to bring the window back. New windows always open in front.
- **I / O** (bounce in / out) work while a take is playing (live position). **P** marks a whole selected take (Take window) or the selected pieces (Edit window) as the bounce range.
- **. and ,** make the waveforms bigger / smaller in the Take and Edit windows together (`AppContext::waveZoom`) and, separately, in the Trim windows (`AppContext::trimWaveZoom`, remembered in the user settings as `trimWaveZoom`; display only).
- **Splash screen** with the build number for about 4 seconds; the logo is the program icon, and saved projects (.fermata) get a paper-and-logo icon (set up automatically the first time the program runs).
- **Media** button: a folder tree of where every file is saved. Clicking a take or a piece in the Take / Edit window highlights its file there (even if the Media window is behind). Double-click a file to open its folder.
- **Session mode** (button in the main window): the inputs are kept for the last 6 seconds all the time, so a take starts 6 seconds BEFORE Record was pressed. A thin red line shows round the main window while it is on. Pressing Record or Stop never interrupts what you hear.

### Latest changes
- Stereo (and wider) tracks show every channel's waveform while recording.
- Edit flags read "Edit IN" / "Edit OUT".
- Mixer windows cannot be made bigger than the strips need; strips are slimmer (90 px).
- "Next Take / This Take" box in the Take window and on the main page.

### This build (5 Oct)
- Mixer: slimmer top bar (copy-mix icon, sun / moon look icon; Scan plug-ins moved to Audio settings), one insert slot until one is used (then every strip shows one more), black border below the strips.
- Meters strip: Peak / RMS / Peak + RMS, rise, fall, peak hold, and a Phase scope window for any track, Int Bus or Ext Bus.
- Bounce Out: "All takes as individual files"; normalise default -1 dBFS.
- Display Organiser (main window): 1 to 4 windows tiled on the screen.
- Take and Edit window toolbars flow onto as few lines as fit. Sending to the edit keeps you in the take window. Key 4 works in the Edit window.
- Playhead follows the time after Stop (button with a triangle and a line toggles it).
- Trim window: double-click a piece in the Edit window; Accept + next fade / previous fade (keys ] and [).

## Edge fades (5 Oct, later)
Every piece of audio now starts and ends with a 1 ms fade, so nothing clicks when it starts or stops. The recorded files are never changed; the fade is applied when playing and bouncing.
- Take window: each take has its own fade-in and fade-out. Drag the little white triangle at the top-left corner of a take to the right for a longer fade-in, and the top-right corner to the left for a longer fade-out.
- Edit window: the start of the first piece and the end of the last piece (and both ends of an overdub) have the same corner handles. Joins between pieces are crossfades, edited in the Trim window.
- Play from the middle / stop at a mark also uses a 1 ms fade. Drag a fade to nothing for a hard cut.
- Older projects get the 1 ms fades on the ends of their edits when opened.

## Sharper waveforms, mixer height (5 Oct, later)
- Waveforms in the Take and Edit windows: the overview is now 4x finer (one value per 128 samples), and once you zoom in far enough that fewer than 128 samples share a pixel, the window reads the audio itself, so the peaks are exact; zoomed right in it draws the real sample curve (with a dot on each sample when very close). Drawn from `src/ui/WaveDraw.h`.
- Mixer window: when the strips are wider than the window, the horizontal scroll bar was taking 12 px from the height, so the window could never be tall enough to hide the vertical scroll bar. The maximum height (and width) now allows for the scroll bars.

- Trim window: drag in the dark band between the two waveforms to move the view left and right (the edit itself does not change). The Centre button brings the join back to the middle.

## Windows belong to the main window; naming the edit (5 Oct, later)
- Every Fermata window (take, edit, mixer, trim, scope, bounce, media, organiser, audio settings) is now OWNED by the main window (Windows). None can disappear behind it when you click it, they have no taskbar buttons of their own, and minimising the main window minimises all of them (the taskbar button of the main window brings them all back). `src/ui/WinOwner.cpp`. On other systems the old "raise on click" is used.
- The first time you send a piece from a take window with no edit yet (keys 3 / 4, or the buttons), you are asked to name the edit. After that, sending pieces never raises or focuses the edit window: it opens behind the take window the first time, and stays where it is afterwards.
- Plug-in windows (VST3 editors) now belong to the main window too, like every other window. Where windows cannot be owned, the fallback now puts the windows back in front of the main window in the order they were last used (the one you were working in stays on top), and plug-in windows stay above the rest.

## Take Display (5 Oct, later)
Main window > "Take Display" opens its settings. It fills other screens (or any screen you tick) with: the project line (an artist name or album title), a logo (the Fermata mark, none, or your own PNG / JPG / GIF), the piece name, the take number (green "NEXT TAKE", red "THIS TAKE" while recording), the time of day (24 hour HH:MM:SS) and the session countdown. Everything scales to the size of the screen.
- Session timer: "Ends at (time of day)" with HH:MM, or "Length of the session" with HH:MM, then press Start (the length counts from that moment). At 00:00:00 it goes on as -00:04:53 in red, labelled OVER TIME. A time of day that passed less than 12 hours ago counts as today (so you are over time); an older one means tomorrow. "No countdown" hides it.
- A screen is closed with a double-click or Esc on it, or its button in the settings. The settings window has a preview, so you can try it with one screen.
- The settings are remembered for next time (they belong to the computer, not to the project).

## Menu bar, mixer scroll bars, Display Organiser (5 Oct, later)
- Main window: all the buttons are now a menu bar along the top (Project, Audio settings, Preamps, Project Designer, Take Window, Edit Window, Mixer, Meter bridge, Media, Session mode, Take Display, Display Organiser), with no "..." in the names. The window cannot be made narrower than the bar (it would only wrap onto a second line on a very narrow screen).
- Display Organiser: unless the main window is given a part of the screen itself, it goes to the top-left corner of the display and the other windows are arranged below its menu bar, so the bar is never covered.
- Mixer window: the biggest it can be is exactly what the strips need (no scroll bars at all at that size). Previously the limit depended on whether a scroll bar happened to be showing, so the window could never reach a size without them. Scroll bars appear only when the window is smaller than the strips (or the strips are bigger than the screen).

## Talkback (CR mic and TB speakers)

Set-up (main window): press **CR** on the input that is the Control Room microphone, and **TB** on the first of the two outputs that feed the talkback speakers (the pair is shown as TB L / TB R). Both are saved with the project.

* **Numeric keypad +** opens the CR mic to the TB pair. A tap (under 1/3 s) latches it on, the next tap latches it off. Holding it longer keeps it open only while it is held. The main-keyboard + does nothing.
* **Numeric keypad -** switches playback to the TB pair on, and off again on the next press. The main-keyboard - and the one beside 0 do nothing.
* While the CR mic is open a thin, slowly pulsing **purple** border shows on the main screen and on every Take Display screen; while TB playback is on a slowly pulsing **yellow** glow shows just inside it.
* **Safety, built into the audio engine:** the two TB outputs are cleared every audio block, whatever the mixers have routed to them. They then receive only (1) the CR mic, while open, and (2) while something is playing back and TB playback is on, the processing mixer's main output computed with *every live input forced off*, so it can only contain recorded audio. A live microphone going out of an Ext bus therefore can never reach the TB pair, whatever the monitor modes or routing. Nothing is muted or warned about; the path simply does not exist. Covered by tests.
* The keys are read while Fermata is the front program, from any of its windows.

## Automation (Edit window)

Press **Automation** in the Edit window. Each track row then shows an automation lane over its waveform, with a small **parameter label** (click it to choose Fader, Pan, a Bus send or a Plug-in parameter, and which mixer the lane drives: the Processing mixer by default) and a **padlock** (click to lock the lane: a locked lane cannot be changed).

* Click on a lane to add a point, drag a point to move it, right-click (or Shift/Ctrl-click) a point to delete it. Between points the value ramps in a straight line (in dB for faders).
* **Points follow the audio, not the clock.** Every point remembers which piece of which take it sits on, so if the edit ripples, slides, is re-ordered, split or trimmed, the point stays on the same musical moment. If the audio under a point is removed, the point moves by a guess (like its nearest surviving neighbours) and is drawn as an orange ring so you can check it; drag it to put it right.
* While an edit plays from its window, the strip's fader / pan follows the lane (the mixer faders move by themselves). Bounce Out of the edit includes the automation. Restart playback to hear changes made to points. Stereo tracks can be automated on the fader; pan lanes are for mono tracks.
* **Bus sends:** the label menu lists the sends the track already has in the chosen mixer (set a send up on the mixer first). A send lane uses the same dB scale as the fader; the very bottom is off.
* **Plug-in parameters:** the label menu lists every automatable parameter of the VST3s in the track's four insert slots (grouped 25 to a page). The lane runs 0 to 1 (bottom to top, the plug-in's own scale) and the parameter is only sent to the plug-in when its value changes. Load the plug-in before choosing it; if the plug-in is replaced, the old lane is ignored.
* Lanes are kept per track and per mixer, saved with the project, and included in Bounce Out.

## Preamp Boost

Each input row has a **BOOST** button next to LC. It is sent to the device as its "lift" switch (the Hapi manual lists the mic preamp's Mic / Boost setting; the device's own settings call the field "lift").

## Preamp: PAD, Z HI, scroll wheel, memory

* Each input row now has **PAD** (`pad`) and **Z HI** (`z_in`) buttons as well as BOOST (`lift`). All of them follow the hardware, and the web page follows Fermata.
* The mouse wheel no longer changes a preamp gain.
* **Preamp memory.** While the devices are connected, the full state of every preamp Fermata can read (gain, MIC/LINE, 48V, polarity, low cut, PAD, Z HI, BOOST) is written, overwriting the last one, once a second (and only when something changed) to "<project name> - preamp memory.json" next to the project file. When you open the project again, Fermata waits (up to about 12 s) for the devices to answer; if the hardware differs from the memory it asks "Keep preamp settings or load stored preamp settings from last time this project was used?" with a choice box. Nothing is written until you have chosen, so the stored state is never lost. An input whose device is not connected keeps its old stored entry.

## Smaller changes (this round)

* **Take window:** each track's name plate has a little horizontal level meter (live input signal, with a short peak mark) so you can see signal before recording.
* **Zoom (Take and Edit windows):** left / right arrows zoom around the middle of the screen (in the Take window, the playhead is moved to the middle if it is on screen). The scroll bars never take the keyboard any more, so the up / down arrows always change the track heights, even after using a scroll bar.
* **Main window menu bar:** it is now a strip of its own that always stays on top of every Fermata window, laid over the top of the main window and following it when it moves. The rest of the main window is a normal window. It is only shown while Fermata is the front program (so it does not float over other programs).
* **Seeking while playing:** in the Take and Edit windows, clicking the ruler while music plays moves the playhead and carries on playing from there (dragging along the ruler in the Take window carries on from where you let go).
* **Trim window:** Space stops an audition (or the original) that is playing.

## Dark / light look (whole program)

A small sun / moon button sits in the top-right corner of every window (and the mixer's own button does the same). It switches the whole program, all windows and all mixers, between the dark console look and a light look, and remembers the choice. Anything drawn with a fixed colour of its own (for example channel colours) stays as it is.

## Importing takes, pitch correction and noise repair

**Import takes** (Take window; or drag audio files onto the window). Pick or drop all the files of a session made in another DAW.
PolyWAV files become one take each (track names are read from the file when it has them). Mono/stereo files with the same
length are grouped as one take; the take number is found in the file names, otherwise the order the files were made is used.
Files are copied into the project (24-bit WAV; 32-bit float stays float). Tracks are matched to existing ones by name.

**Pitch...** (Take or Edit window). Mark a range with keys 1 and 2, click Pitch..., choose semitones (+/-12) and/or cents.
The pitch moves but the length and timing never change (no "slower playback"). All tracks of the take are processed.
In the Edit window you can also process 0-9 s extra either side (1 s steps) so the edit point can still be moved.

**Repair...** (Take or Edit window). Mark an area a little bigger than the noise with 1 and 2. A merged spectrogram of all
tracks is shown; drag a box round the noise. Patch rebuilds that part of the spectrum from the clean audio to the left, right or both
sides; Declick mends short clicks. Keys 1/2 now also work in the Edit window.

Nothing is destructive: the results are new files in the project's `Fixes` folder, placed over the originals. **Undo fix** puts the
originals back (one level). In the Take window each fix writes full-length copies of the files, so it uses disk space.
Edit window repair needs both marks inside one piece. Overdubs are not processed.

## Take counter in the menu bar
The green (next take) / red (recording take) box with the take number now sits at the left end of the menu bar.

## Mastering window (menu bar > Mastering)
Pick where the sound comes from at the top right (normally the processing mixer and its Ext bus, plus a tail of 0-10 s for reverb to ring out).

**Virtual Master** - one file per Edit window. Tick the edits to include, drag the grip on the left to reorder, type a new file name in the list.
Formats: WAV (16/24/32-bit float), AIFF, FLAC, Ogg Vorbis, MP3 (needs the free `lame.exe` placed next to Fermata.exe - it cannot be shipped with the program).
Sample rate, bit depth, TPDF dither, peak level (one gain for all files, or each file on its own), numbering, tags (album-wide and per file).

**DDP builder** - the edits become tracks of a CD. Drag a track left/right on the timeline (or type the pause) to set the gap; dragging it back over the previous track swaps them.
"Auto PQ" sets every pause (first track starts at 2 s). The PQ list is calculated live from the positions and the pauses, so if an edit changes length the pauses are kept.
Disc and track CD-Text, UPC/EAN, ISRC, pre-emphasis and copy flags. "Make DDP" writes the DDP folder (+ optional zip and WAV + CUE sheet); "Check DDP" re-reads it.
**Important:** the DDP layout follows the published format but has not been tested with a real DDP player - open the result in a free DDP player before sending to a plant.

## Noise repair: preview, commit, revert
The repair window now stays open. Draw a box, set the controls and press **Preview**: the spectrogram redraws with the repaired sound (nothing is written yet).
**Show original / Show repaired** flips the picture; **Revert** goes back; **Commit** writes the new files (the originals stay; Undo fix brings them back). Changing any setting asks for a new Preview before Commit.
Controls (like iZotope RX Spectral Repair): **Strength**, **Direction** (left and right = sound before/after; up and down = pitches above/below; both), **Surrounding length** (% of the box) and **Before <-> After** weighting.

## Windows and the menu bar
A tool window whose title bar would end up under the floating menu bar is moved down below it (a maximised one is fitted below it).

## DDP builder timeline
Flags mark the PQ points: grey = CD start (0:00), green = INDEX 01 (track start), orange outline = INDEX 00 (start of the pause), purple (pointing back) = end of each track, red = end of CD (lead-out).

## DDP builder: playhead, audition, zoom
The timeline now runs the full width of the Mastering window. Click the ruler (or anywhere in the picture) to put the **playhead** there; drag on the ruler to scrub.
**Play** (or Space) plays the disc from the playhead with the real pauses between the tracks, through the processing mixer (automation included). Clicking while it plays jumps there.
**Play the gap** plays the pause before the selected track: 3 s before, the silence, 3 s of the track. **Track start** moves the playhead to the selected track's INDEX 01.
**Right arrow** zooms in on the playhead, **Left arrow** zooms out (also the + / - buttons; Ctrl + mouse wheel too); the wheel scrolls; **Fit** shows the whole disc; Home goes to the start.
(The arrow keys work when the cursor is not in a text box: click the timeline first.)

## Auto PQ, C, drag-to-mark, repair surround box
- **Auto PQ** (DDP builder) moved to the transport row so it never scrolls away. Its box starts on **Keep positions**: Auto PQ then makes the PQ points from where the tracks are and moves nothing (only a first track earlier than 2 s is moved to 2 s). The other choices first set every pause to 0 / 1 / 1.5 / 2 / 3 / 4 / 5 s. The selected-track panel scrolls when the window is short.
- **C** centres the view on the playhead without changing the zoom (DDP builder, Edit window, Take window).
- **Drag to mark:** in the Take window, dragging over a take's tracks sets Edit IN (1) where the mouse went down and Edit OUT (2) where it is let go. In the Edit window the same sets marks 1 and 2, when the piece under the mouse was not already selected (a selected piece still slides when dragged; the first drag over audio deselects it, so the next drag marks again).
- **Noise repair:** the dotted blue box(es) show the surrounding sound the repair learns from (before/after for left-right, above/below for up-down, all four for both), sized by the Surrounding length %.

## Automatic preamp set-up (ANEMAN), window size, keyboard focus
**Preamps > Find automatically** asks ANEMAN (the Merging network manager, port 6167 on this PC) for the devices and the patch, and sets each device's preamp channels to the ASIO inputs they are actually patched to, channel by channel, in any order. The same is done quietly when Fermata starts or a project opens (if ANEMAN is not running, your saved list is kept). Typing a number in "First input" goes back to "consecutive from here". The MT48 only appears if it is patched in ANEMAN.
Mastering opens at 1320 x 720 (windows never open bigger than the screen); the DDP bottom block and the Virtual Master settings use the width. Windows now take the keyboard as soon as they are in front, so the arrow keys, C and Space work without clicking first.

## Bars: IN Bar / OUT Bar and Search for bar
Right-click a take in a take window: two boxes, **IN Bar** and **OUT Bar** (the bars the take starts and ends in; it contains those bars and every bar between). They are saved with the project and written along the bottom of each track's waveform for that take, e.g. "Bar 17 - 39". (On a narrow take as much of it as fits: "17 - 39", "17-39", "17-", "17"; 3.18.4.) (The old rename / remove menu is behind "More...".) Pieces sent to an edit carry the bars with them.
In the Edit window, **Search for bar** asks for a bar number, lists every take that contains it (and how many pieces of it are in this edit) and outlines those pieces in blue-green. Click the button again to clear the list and the outlines.

## Bars box while recording, Dud, draggable PQ flags, Undo
- Right-click a take (also one still being recorded): a small box with IN Bar and OUT Bar opens with the cursor already in IN. It does not block anything, so R / Space still work. Enter in IN goes to OUT, Enter in OUT saves, Esc closes. "More..." leads to rename / remove.
- **D** marks the selected take (or, while recording, the take being recorded) as a DUD: a red "DUD" is written before the bars along the bottom of the take. Press D again to clear.
- DDP builder, the PQ flags: click a flag to select that track and put the playhead exactly on it; drag **INDEX 01** (T1, T2 ...) to start the track earlier or later than its audio, drag **INDEX 00** (n.00) to choose where the pause code begins, drag **End of CD** later to add silence before the lead-out. Double-click a flag to put it back to automatic. "Set every pause to N s" also puts all flags back. Moving a track's box still changes its pause; moving flags never moves audio.
- **Undo / Redo** (Ctrl+Z / Ctrl+Y, or Ctrl+Shift+Z): the last 30 steps of the takes' details (bars, duds, names, positions, removed takes), the edits (pieces, fades, automation, repairs) and the Mastering / DDP settings. Marks, playheads, mixer faders and preamps are not undo steps, and a take that is being recorded is never touched. The status line says what happened.

## PQ flags the SADiE way
As in SADiE, **the end of a track is the INDEX 00 of the next track** (the player counts down from there to the next INDEX 01). So the purple **End Tn** flag is draggable: it moves that INDEX 00. It can go anywhere after the previous track's INDEX 01 and up to the next INDEX 01; putting it on top of INDEX 01 means no countdown, a continuous join. The green INDEX 01 flags are draggable independently (INDEX 00 is never left after INDEX 01), the first track's INDEX 01 can be dragged later to hide a track in the pregap, and the last track's End flag (and End of CD) move the lead-out, adding silence at the end. Double-click a flag to put it back to automatic. The audio never moves when a flag does.

## Dither (every new audio file, and the mixer output)

* **Every audio file Fermata writes is dithered whenever it loses word length.** 24-bit and 16-bit integer files are made with TPDF dither (triangular, ±1 LSB) added before rounding, so the quantisation error is noise and not distortion. This covers Bounce (24-bit files, and the final pass when "Normalise" is on), the pitch / noise-repair files, take import when a source is wider than 24 bit, and every Virtual Master / DDP export (16 or 24 bit, with the Virtual Master "Dither" box still able to switch it off). 32-bit float intermediates are not dithered, because nothing is lost in them. Recordings are written exactly as the converters deliver them (24-bit in, 24-bit out), so there is nothing to dither.
* **Output dither on every mixer.** In the METERS column of each mixer, under "Reset peaks", the **OUTPUT DITHER** box offers Off / 24 bit / 16 bit (default 24 bit). It is applied to the mixer's driver outputs just before the audio goes to the interface, is saved with the project, and is separate for each mixer. Digital silence stays silent, and anything at or above full scale is passed on untouched. Bounces and exports never use it (they are dithered once, when written).
* **Disc master (DDP, WAV + CUE) is always 44.1 kHz, 16 bit, TPDF-dithered**, whatever the Virtual Master is set to: the programme is rendered in 32-bit float, sample-rate converted to 44.1 kHz, dithered to 16 bit, and the DDP builder refuses any file that is not 44.1 kHz / 16 bit.

## Pitch curve (draw the pitch against time)

For a note or a whole passage that drifts flat or sharp. In the **Edit window** mark the part with keys 1 and 2, then press **Pitch curve...** (the same button exists in the Take window, using that window's Edit IN / OUT marks).

* The lane shows the marked audio faintly behind a level line. **Click** to add a point, **drag** to move it (up = sharper, down = flatter; +100 cents at the top, -100 at the bottom, 0 in the middle), **double-click or Delete** removes one, **Reset line** clears all. Any number of points; straight lines join them, and the line stays level before the first and after the last point. Your example: a point at 0 cents at 5 s and one at +15 cents at the end raises the pitch gradually over those 10 seconds; add more points for a slow drift followed by a sudden fall.
* **Audition** calculates the corrected sound (it cannot run live; a few seconds per take) on every track and plays it through your mixers, from 2 s before the marked part to 2 s after. **Accept** keeps it, **Revert** puts the original back; closing the window without Accept also reverts. If you change the line after auditioning, press Audition again (Accept waits for that).
* The audio is replaced by new files (dithered 24-bit, in the project's Fixes folder or next to the take's files); the originals are never touched, and **Undo fix** brings them back after an Accept. In the Edit window "Process extra, each end" can correct more audio each side (the line stays level there) so the edit points can be moved outward later.
* Method: the same phase-vocoder pitch shifter as the fixed Pitch tool, with its analysis hop and play-back speed following the line, so the length never changes. In the tests a 440 Hz note that sinks 30 cents is brought back within 2 cents all the way along, and a jump of 40 cents is reproduced to within 2 cents.

## Loop, and listening in the repair and pitch-curve windows

* **Loop** (button next to Play, or key **L**) in the Edit and Take windows: when lit, Play repeats the marked area over and over, with no gap, until you press Stop. Edit window: the area between marks 1 and 2. Take window: Edit IN / OUT [1] [2] if set, otherwise Bounce IN / OUT [I] [O] (the same marks "Play marked" uses); both Play and Play marked loop. Switching Loop on while playing starts the loop at once; switching it off lets the pass finish and stop. With no area marked it plays normally and says so in the status line.
* **Pitch curve window:** Audition now shows a progress bar (full width, with a percentage) while the corrected sound is calculated. While it plays, a red playhead runs across the line with a dot on the line itself and the line's value in cents shown below. **Play** replays the current sound (the original before an Audition), **Loop** goes round the marked part, **Stop** stops.
* **Noise repair window:** it now has a transport. **Play** plays the selected box (with a second of sound either side), or everything if nothing is selected, through your mixers with a playhead moving across the spectrogram - no repair needed, so you can listen to find what has to be mended. **Listen to** chooses Original or Repaired. **Preview and listen** shows the repaired spectrogram and then plays the repaired sound (it makes the repaired audio ready first, with a progress bar), so you hear it "through" the repair; Play with Loop lit goes round it as often as you like, and changing the box or any setting brings back the original until you Preview again. Commit keeps what you heard (no second calculation); closing the window without Commit puts everything back.

## Status squares (Pre-Rec, C-R, P-B)
The coloured borders that used to flash round the screen are gone. Three squares now sit at the right end of the top menu bar and are always visible: dark when off, lit when on.
- **Pre-Rec** (red): session mode is on.
- **C-R** (purple): the control-room mic is open (talkback).
- **P-B** (yellow): playback of recorded material to the talkback output.
Hover a square for a reminder. Take Display screens keep their own optional talkback glow.

## Update of 6 October (take / edit / repair / export)
**Take window:** stereo tracks show two meters (L and R). Enter after typing the piece name confirms and leaves the box. Takes cannot be dragged; they stay in time order. Waveforms are drawn as one filled shape, so no vertical stripes at any track height. Key **5** removes the 1/2 flags; with no flags, a clicked take plus Pitch / Pitch curve / Repair loads the whole take. Small **In n / Out n** flags show which edit number each take's start and end went to.

**C-R talkback:** any number of mic inputs can be the C-R mic (the C-R button toggles each input; they are summed at unity).

**Edit window:** numbered edit points: 1 = start of the first piece, 2 = join of piece 1 and 2 ... and the final fade-out. Where fades do not overlap the flag sits midway between the in-start and out-finish. Double-click a flag to open the Trim window, which shows the number and can also load a fade at the very start or end (a one-sided fade). **Ctrl+click** selects a whole piece, which can then be dragged sideways (release Ctrl once selected); a plain drag marks a portion. Key **5** clears the 1/2 flags; with no flags a selected piece is processed whole.

**Noise repair:** crossfades are linear. The whole loaded area is processed. **Accept** keeps the change and leaves the window open (several changes can be made); **Finish: write it back** renders everything between In and Out at once. **Space** plays/stops here and in both pitch-curve windows; playback covers only the loaded part.

**Pitch curve:** Preview button, a **Hear corrected** switch (on = changed audio, off = original, carried on from the same place), new points start at 0 cents, whole cents, within 1 cent snaps to 0.

**Windows and menu:** a new window always opens on top, even over a full-screen one. The menu bar is always shown. Order: ... Take display, Organiser, Media, Mastering (right). The session button is called Pre-Rec.

**Virtual Master export:** FLAC **Compression** 0-8 (lossless, only size and speed change); MP3 quality box as before (CBR / VBR). A **File name** box for each file (separate from the edit name). **Also export every file as** WAV / AIFF / FLAC / Ogg / MP3: tick any combination and one Export makes them all. MP3 still needs lame.exe beside Fermata.exe.

**Mixer:** a **Mono** button under Phase scope in the METERS column. Lit amber = the mixer's outputs carry (L+R)/2 on both sides, for checking mono compatibility. Listening only, never in files, off when the project opens.

Not done yet: Shift+drag of a clip to another track (placeholder only).

## Tracks of an edit (Edit window, "Tracks..." button)
* Every edit can have **its own list of tracks**. By default it shows every project track; the first time you use **Tracks...** it gets a list of its own (saved with the project).
* **Tracks...**: add a new mono / stereo track, add a track the project already has, remove a track (its audio leaves this edit; **Ctrl+Z** brings it back), or show every project track again. A new track has a mixer strip in every mixer at once, and no microphone input.
* **Drag a track's name up or down** to change the order (a yellow line shows where it will land). The order belongs to that edit only.
* **Strips and tracks that do not match:** when an edit is opened (and before Play), if its audio belongs to tracks that are no longer in the project, a window asks "There are more audio tracks than mixer strips. Would you like to add the missing strips?" Yes puts the tracks back (same identity, no input) with a strip in every mixer; No leaves that audio silent. A mixer with more strips than the edit has tracks is fine: the extra strips just stay quiet.
* Not yet: the mixer windows still list strips in the project's order (they will follow an edit's own order when each edit gets its own mixer), and the Take window has no track list of its own.

## Move a clip to another track (Shift + drag, Edit window)
Hold **Shift**, press on one track's clip in a piece (or overdub) and drag up or down: a yellow frame shows the track it will land on. Let go and the clip is on that track, at exactly the same time. If the target track already has a clip in the same piece, the two swap tracks. Both tracks must have the same number of channels (mono to mono, stereo to stereo); otherwise a notice appears and nothing changes. The audio files are not touched, volume changes and fades stay with the clip, and Ctrl+Z undoes it. This is also how audio gets onto a track you added with "Tracks...".
* **Audio always goes to its own track.** A piece sent from a take window lands on each track by the track's identity, not by the row it had in the take: if "vln1" is the 3rd track in the take window and the 10th in this edit, vln1's audio appears in the 10th row (and plays through its own mixer strip). A track the edit does not list yet is added at the bottom of the edit.
* **Shift + drag** a clip (one track's part of a piece) up or down to another track: it keeps its place on the timeline. If the other track already has a clip in that piece the two swap. Both tracks must have the same number of channels.

## Import into the Edit window
The **Import...** button in the Edit window (or just drag audio files / a folder onto the window) brings a multitrack recording straight into that edit: a multichannel / polyphonic WAV, or one mono / stereo file per track (WAV, AIFF, FLAC). The same file-reading as the Take window's import is used: tracks are matched to your existing tracks by name (new ones are created), every track goes on its own row, and each take found is placed in the edit one after the other, ready for mixing. The files are copied into the project's audio folder (in a folder named after the edit) as 24-bit WAV; the originals are not touched. The edit's sample rate must match the audio.
(Importing a whole recording day into a **Take window** has been there already: its Import button, or drag the files onto it.)

## Control port (Stream Deck)
Fermata listens on 127.0.0.1 port 7788 (this computer only) so a Stream Deck can press its main buttons and show their state. One line of JSON per message. Commands: `record`, `playpause`, `talk` (with `down` true / false: a tap under 1 second latches the C-R mic, a longer hold is momentary), `playback` (the P-B speaker), `mixer` (with `index`: 0 = processing mixer, 1 = second mixer), `prerec`. Fermata sends back a `state` message whenever something changes (recording, next / this take number, playing, C-R open, P-B on, Pre-Rec, mixers open, the time of day and the session time left). Put `remotePort` in the settings file to use another port (0 = off). The plugin is in the separate Fermata Stream Deck package.

## Transport everywhere, zoomable repair windows, bar search in the take window (latest)
* **Full transport in the Take and Edit windows:** `|<` `<<` Play `>>` `>|` (Home / End also go to the start / end). You can play with no take or region at all, and past the end of what is there (silence); bouncing and mastering still use the real length. The timelines follow the playhead and grow as it moves on.
* **Noise repair and pitch-curve windows:** left / right arrows zoom in time (right = in), up / down zoom in pitch (repair) or cents (pitch curve). A time ruler sits above the picture: drag it to move left / right, double-click it to see everything. **Play (and Space) plays only what you can see**: zoom in to hear a short piece, out to hear more. The existing transport buttons are used; there is no extra Play button.
* **Pitch scale box (repair window):** linear (even Hz) through three blends to musical (every octave the same height). Drag the pitch axis up / down to move the pitch view.
* **Round colour control (bottom left of the picture):** drag it up / down, click it, or use the wheel to step through eight colour maps and level ranges (standard, strong sounds only, quiet sounds, very quiet noise, greys, rainbow) so faint noises show. Double-click returns to standard. (Modelled on the colour roll of spectral editors such as CEDAR Retouch; its exact behaviour is not publicly documented, so this is our own interpretation.) Zoomed-in detail is limited by the resolution the area was scanned at.
* **Pitch curve points:** the first point anchors at 0 cents; later points go where you click, snapping to 0 near the middle line. Whole cents only.
* **Search for bar** moved from the Edit window to the Take window: type a bar and every take that contains it is framed in blue-green; the button shows how many.

## Windows remember their size and place; the mixer opens tall enough (latest)
* **Every window** (take, edit, mixer, mastering, scope, bridge, media, ...) reopens where and how big you left it. This is kept **in the project file** (so each project has its own), not in the program. It is saved with the project; it does not count as a change to the project.
* **The mixer** opens tall and wide enough to show every strip completely (names at the top and bottom, no scroll bars), even if that is taller than the screen: the title bar stays on screen and the rest hangs off the bottom. It can be made smaller (scroll bars appear) but never needs to be bigger than that. Its size and place are remembered like the other windows.

## Window fighting and the menu bar (latest)
* **Take and Edit windows no longer fight for the front.** Cause: a scroll bar that moved by itself (a timeline following the playhead while something plays) gave its window the keyboard, which pulled that window forward; with two windows scrolling, each pulled the other in turn. A scroll bar now takes the keyboard only in the window you are working in, and no window is pulled forward just to receive the keyboard.
* **The menu bar strip (Project, Audio settings, Pre-amps ...)** is no longer on top of every program. It belongs to the Fermata main window: it stays in front of the other Fermata windows, goes behind a program you put on top of Fermata, can be covered by one, and minimises and restores with the main window.

## Any outputs can be talkback (latest)
* **TB on the Driver Outputs list:** click TB on any output and that output and the next become a stereo talkback pair (to the studio). You can make as many pairs as you like. On the last output the pair is mono (the missing right side is simply ignored, no warning). Click again (on either output of a pair) to turn it off. Like the CR mic, which can be any input(s), the talkback outputs can be any output(s).
* The safety is unchanged: a talkback output carries only the CR mic and recorded playback, whatever the mixers send there. Older projects with one pair open as before.

## Alt Mixer for the Stream Deck's second Mixer key (latest)
* Every mixer except the processing mixer has a small key-pad button in its top bar (right of Copy mix). Lit (glowing) = **this is the Alt Mixer**: the one the second Mixer key opens and closes. Only one mixer can be the Alt Mixer at a time. If none is chosen, the first cue mixer is used; with no cue mixer the key reads "Alt-Mixer" and does nothing. The key shows the Alt Mixer's own name, and follows it when it is renamed.
* The Stream Deck keys have new pictures: Record (red dot with the take number on green; all red "TAKE" while recording), Play / Pause (white, with the fermata sign), and a little mixing-desk drawing with the name for both Mixer keys.

* **Mixer window size, second fix:** the window's limits were worked out from the window's own size at the moment it was first fitted to the strips, when that size was still a placeholder, so it could shrink itself to a tiny size and open that way. The limits now use the window's frame, and a mixer window that has never been fitted opens showing every strip completely (a size you set yourself afterwards is still remembered).

## Project folders, Save as, Create copy, crash recovery (latest)
**Folder layout.** A new project asks where to keep it and what to call it, and makes a folder with that name:
`Projects 101/` holds the project file, `Recorded Media/` (all recorded audio), `Bounced Media/` (the default place for exports from the Take and Edit windows) and `Bounced Media/Mastered Audio/` (the default for the Mastering tab). Paths inside the project are stored relative to the project folder as well as absolute, so copy-pasting the whole folder by hand (to a drive, another computer) is a working copy.
**Project menu.** *Save as...* asks "Copy everything" or "Project file only", then a name and place, then carries on in the new project. *Create copy of entire project...* copies every file (also audio kept elsewhere, into `Recorded Media/From elsewhere`), reads each back and compares it, and stays in the original project.
**Crash recovery.** The take is saved the moment recording starts, and WAV headers only get their length when a file is finished, so after a crash the files look empty. When a project is opened, every take file is checked and its header put right from the real size of the audio; the take's length is restored and a "Recording recovered" message lists what was kept. Limits: WAV files over 4 GB; what survives a power cut depends on what Windows had written to the disk.

## The program is now called Fermata everywhere (latest)
The program, its .exe, its settings folder, its log (`Documents\\Fermata\\fermata-log.txt`), its default project folder and the project files (`.fermata`) all use the new name. Projects made under the old name (`.takedaw`) still open, and the old settings are carried over once.

## Zoom follows the playhead everywhere (latest)
As in SADiE: the arrow-key (and +/-) zoom is centred on the playhead. Zooming in moves the playhead toward the middle of the window and keeps it there; near the start of the timeline it can't go further left than time 0, so it stays toward the left and slides back there as you zoom out. This now applies to the Take window, the Edit window, the Mastering timeline, and the spectrogram / pitch-curve windows (while the playhead is running; otherwise they zoom around the middle). The Trim window always zooms around the edit point.
The Take and Edit timelines always have empty room after the end of the material (as much again as the material, and at least 10 minutes; 24 hours at most) for dragging more audio onto, and zooming out goes as far as that whole length fits on screen, so a 2.5 hour concert can be seen end to end.

## Mixer window: opens at the full size, Fit button, log (latest)
A mixer window now always opens at exactly the size that shows every strip (only its position is remembered per project, not a size that may have been saved while it was too small), and there is a **Fit** button in its top bar that does the same at any time. The numbers (strip size, window frame, screen area, resulting window size) are written to `Documents\Fermata\fermata-log.txt` each time, so a window that still comes out wrong can be diagnosed from the log.

## Start-up asks where the project goes (latest)
Fermata no longer creates a project by itself. When it starts, a screen asks "Where do you want to save your project?": **New project...** (pick the place, type a name; a folder with that name is created holding the project file, Recorded Media and Bounced Media/Mastered Audio), **Open an existing project...**, **Open the last project: ...**, or **Quit**. Until a project is chosen nothing is saved and Record does nothing. Double-clicking a project file in Explorer still opens it directly.

Mixer window size, found from the log: Windows applied the window's biggest-size limit to the whole window (frame and title bar included), so a mixer set to exactly the strips' size was cut short by about 14 x 37 points, which made scroll bars appear. The limit now has some slack. The start screen shows only its buttons.

## Export for Processing (Oct 2026)
- `WaitingPiece` (core/Edit.h) lives on `EditRegion::waiting` and `TakeGroup::waiting`; persisted by Project.cpp (file + rel paths; "channels" key so project copy includes the files).
- `audioops::exportRange / checkReplacement / spliceFile` (core/AudioOps.cpp), tested in tests/core_test.cpp.
- Edit window: the region keeps pointing at the ORIGINAL files while waiting; re-link swaps in the processing files (fileStart = srcIn). Take window: re-link splices into new full-length files (5 ms crossfade).
- UI: fixtools::exportForProcessingEdit/Take, relinkEdit/Take (FixTools.cpp); `drawWaitingBlock` in WaveDraw.h.

* **Repair / De-Click workflow (latest):** **Fix** does the repair at once (no auto-play) and keeps it as a temporary step; **Play** (Listen to: Fixed) auditions it; **Undo** takes the last fix off (stackable); more boxes can be drawn and Fixed in the same window, earlier fixes stay; **Write back to file** renders all fixes between the marks. The round colour control now turns smoothly (the colour maps and level ranges blend continuously). De-Click: multi-pass detection with hysteresis, longer gaps (to 1500 samples, predictor order up to 128), and a box of 30 ms or less is mended as a whole.

* **Low-end detail (Spectral Repair / De-Click window):** a "Low-end detail" box (normal 2048, fine 8192, very fine 16384, finest 32768 point analysis) redraws the picture with a longer window: low notes separate, sudden sounds smear. **Waveforms:** solid body in a slightly darker shade with a thin bright line round the outer limit (SADiE / Pyramix style), drawn in WaveDraw.h fillEnvelope.

* **WaveColour (new):** rainbow button next to the playhead button in the Take and Edit windows (same setting in both; default off; lit = on). Each file is analysed once in the background (ui/WaveColor.h: 4096-sample blocks, FFT): hue = spectral centroid (100 Hz red, 1 kHz green, 8 kHz+ blue), saturation = tonality (spectral flatness), BLACK = more than half the energy below 100 Hz (a kicked mic stand), grey = silence. A pixel column shows the colour of the loudest block under it. Drawn as a horizontal gradient through fillEnvelope.

* **Info buttons:** explanatory sentences that took up room (Design window pages, Ext Bus note in the mixer strip, Media window, Organiser hint, Take Display hint, Preamp setup, the pitch / pitch-curve / repair / de-click dialogs) now sit behind a small blue "i" (`InfoNote` in Uikit.h; a drop-in for the Label that showed them). Clicking it opens the text in a call-out box. The File menu's "Save now" is now "Save".

* **Automation playback position (bug fix):** the engine read the lanes at "samples played since Play" instead of the timeline position, so playing from the middle or looping gave wrong fader values. `PlaybackSession::getTimelinePosition()` (start + played, wrapping with the loop) is now used.
* **Automation value box:** while a point is set or dragged a box beside it shows its value (dB / L-C-R / plug-in value) and time. Values snap to 0.1 dB; hold **Alt** while dragging for ten times finer movement.
* **Spectrogram:** two analysis layers (2048-point window at full rate, and the same size after 8x decimation = 8x longer window, 3 Hz rows) blended between 250 Hz and 1.5 kHz (`MergedSpectrogram::level`). The "Bass detail" box: blended / off / extra fine (16x, 150-800 Hz).
* **Spectral Repair / De-Click in the Edit window across edit points:** the marks may now cover several pieces. The picture is the whole marked area, piece after piece; each piece is repaired from its own source file (the part of the box that falls in it) and put back in its own place, so edit points and fades are kept.

* **Quit asks first:** closing Fermata (window close or Quit) always asks "Save and quit / Quit without saving / Cancel" (Main.cpp systemRequestedQuit); Cancel keeps working.

* **Fix joins are not edits:** the joins made by Pitch, Pitch curve, Spectral Repair, De-Click and Export for Processing carry `fixIn` / `fixOut` flags on the pieces (EditDef::isFixJoin). They are drawn as a small grey "fx" flag with no number, are left out of the edit count (also in the Trim window caption), and "Accept, next / previous fade" skips them; on a fix join those two buttons are hidden. They can still be opened and trimmed (click + T, double-click, or the **Trim Window** button, formerly "Trim join").

## Per-Edit mixers
- Every Edit owns a MixerState (MixerState::editId). Made in Project::addEdit / syncEditMixers (also on load for old projects) as a copy of the processing mixer (levels, inserts, Ext-bus outputs, dither). Named after the Edit (renaming either renames both). Order in `mixers`: processing, cue mixers, then Edit mixers; `cueEnd()` = first Edit mixer. addMixer inserts before the Edit mixers.
- Engine: an Edit mixer only runs while PlaybackSession::editId matches; while an Edit with a mixer plays, the processing mixer rests (same outputs would double). Cue mixers are unaffected. TB playback tap uses the playing Edit's mixer. Only ONE playback session exists at a time, so two Edits cannot play together yet.
- Automation lanes with mixerId null drive the Edit's own mixer (automationPlanFor(..., onProcessingMixer) is used by the CD preview, which still plays through the processing mixer).
- Bounce: BounceItem::mixerId; Mastering "Each piece through its own mixer" is the default (MasteringDef::mixerId null).
- UI: Mixers menu lists Edit mixers separately; Edit window "Edit Mixer" button; Design page lists them (cannot be deleted alone); Mixer window hides Audition/Delete/Alt for Edit mixers.

## Mastering render cache
- Each Edit in the Mastering window gets a stereo render (native rate, 32-bit float) in `<project>/Mastering renders/<editId>-A.wav` / `-B.wav`, listed in `renders.json`. Temp file `mrtmp-<editId>.wav` is swapped in when finished.
- Two-slot rule: at most two renders per Edit. A new render overwrites the OLDER slot; the newest becomes the backup. A render only happens when the fingerprint changed (edit content, its mixer, source bus, tail, sample rate). "Use previous version" pins the older slot.
- Fingerprint (MasterRender.cpp) uses FNV-1a hashes; controls driven by automation (fader/pan/gain/sends, plug-in state of automated slots) are masked so playing an edit doesn't cause re-renders. "Render again" forces.
- Mastering Play (DDP view) uses `AppContext::playRenders` -> PlaybackSession with `directOut`: stereo render goes to the output pair of the chosen source Ext bus at unity, no mixer. Previews are native resolution.
- Export reuses the renders. DDP always 44.1 kHz / 16-bit: SRC + TPDF dither only happen at DDP render (makeDiscSpec + MasterExportJob stages 2-3); cache renders stay native.

## Stage speaker mixer (main window, top right)
- `src/ui/StageMixer.h` (StageSpeakerPanel): one strip per CR input (fader -60..+10 dB, pan knob), a Playback strip and an Output strip. Shown only when a CR input or a TB pair exists; sideways scroll only if the strips do not fit.
- Settings live on `Project` (`stageGainDb`, `stagePan` indexed by driver input, `stagePlaybackDb`, `stageOutputDb`) and are saved in the project file.
- Engine: `AudioEngine::setStageMix` fills lock-free gains; the TB section of the audio callback applies them with a per-block ramp. Centre pan is unity on both sides, so 0 dB / centre is exactly the old behaviour. Several TB pairs share the one stage mix.

## Take window flags, spectral windows, overlaps, ISRCs
- Take window: the separate Bounce I / O flags are gone. P sets Edit IN / OUT (keys 1 / 2) round the whole selected take; the Edit IN / OUT flags can be dragged (snap to the take's start / end within 5 px). Bounce uses the 1 / 2 flags. Old projects' take-window I / O marks are not loaded.
- Spectral Repair / De-click / Pitch curve windows: a click on the time ruler places a playhead (`cueT`); Play starts from it and the left / right zoom keeps it still on screen; down arrow zooms in vertically, up zooms out; R = Fix (Repair) / Audition (Pitch curve).
- Mastering: a track may overlap the one before it (negative `gapSectors`, at most 20 s, never before the previous track's start). `computePq` keeps `pos` as the furthest end; `ddp::streamProgram` mixes the overlap (tail of the earlier track kept in a pending buffer). INDEX 00 is not set for an overlapped track. Dragging no longer swaps tracks.
- Mastering waveform: 40 slices per second, drawn as one smooth filled outline. ISRC tool (button "ISRCs..." in the DDP view) fills country / registrant / year / first number, each track one higher than the one before.

## Reserved CPU cores (MassCore-style, user mode)
- `src/core/CoreReservation.{h,cpp}`. Windows cannot give a normal program a core that nothing else may use (Pyramix MassCore does that with its own kernel driver). What Fermata does instead, with Windows 10 "CPU sets": (1) `SetProcessDefaultCpuSets` puts everything in Fermata on the GENERAL cores; (2) the audio threads - the driver's callback (`AudioEngine::audioDeviceIOCallbackWithContext` -> `CoreReservation::audioThreadCheck()`, a thread_local generation compare per block), the disk writer and each `PlaybackSession` reader (`registerAudioThread`) - get `SetThreadSelectedCpuSets` = the RESERVED cores; (3) the callback joins MMCSS "Pro Audio"; (4) the process is set to High priority class and opted out of EcoQoS / timer-resolution throttling; (5) optionally ("keep others off", the Process Lasso part) a low-priority thread gives every other process that has NO default CPU set of its own the general cores, every 5 s, and releases exactly those (same ids) on switch-off / `shutdown()`. Processes that Process Lasso etc. have configured are never touched; protected / other-user processes fail to open and are skipped.
- The Windows calls are looked up with GetProcAddress (kernel32, ntdll, avrt) and the structs written out, so the build does not depend on the SDK version. `tools/check-windows-code.sh` compiles that part on Linux against `tools/win-stubs/` (syntax only - the stubs repeat the SDK signatures from memory, so the real MSVC build is the real test).
- What it cannot do: DPCs / interrupts run on the core their device interrupt was routed to, regardless of CPU sets. So the UI (`src/ui/CpuCoresPanel.h`, button "CPU cores..." in Audio settings) measures per-logical-processor DPC + interrupt time (`NtQuerySystemInformation(SystemProcessorPerformanceInformation)`, 3 s) and ticks the quietest cores (`pickQuietCores`: performance cores first on hybrid CPUs, then quietest, then higher number). Steering the interrupts themselves (Interrupt-Affinity Policy in the registry) is deliberately not done.
- Planning (`makeCorePlan`, `pickQuietCores`, `suggestedReserveCount`) is plain code with unit tests. Settings are per PC, in the user settings file (`cpuReserveOn`, `cpuReserveCores`, `cpuReserveOthers`), not in the project. Off by default. Switched on at start-up (`Main.cpp` initialise) and released in `shutdown()`.

## Parallel mixers, idle mixers, plan carry-over, second drive (2.60)
- `src/core/MixerWorkers.h`: up to 6 helper threads (`physical cores - 2`), started in the AudioEngine constructor, registered as audio threads (reserved cores, denormals off). `run(count, fn, ctx)` on the audio thread: one 64-bit atomic ticket (gen 24 bits | total 20 | next 20); the audio thread claims jobs too and then spins until `done == count`, so a late helper never delays the block. No locks, no allocation.
- `AudioEngine::process`: the mixer loop first builds `Plan::jobs` (MixerJob per live mixer). A mixer whose outputs go nowhere (`mixerHasOutput`) is skipped unless it is the talkback-playback tap (strict) or audited. With >= 2 jobs, helpers, one chunk and `numOut <= outChannels`, each job writes into its own `MixerPlan::outScratch` (touched channels listed) and the audio thread then sums the touched channels into the device outputs in mixer order: bit-exact with the serial path (tested). Otherwise `processMixer` runs serially as before.
- Plan carry-over: `project.changed()` -> `rebuildPlan()` used to start new, zeroed PathDelay lines (a gap at Record / Stop). The audio thread now notices a new plan (`lastPlanSeen`) and `carryOver(prev, new)` swaps the delay vectors across where node kind / strip / bus (and sends by `SendState*`) match and length / channels match, and copies `wasPlaying`. Tested with a 100-sample LatencyInsert rebuilt mid-stream (difference < 1e-6).
- `src/core/BackupCopier.h`: one thread at `THREAD_MODE_BACKGROUND_BEGIN` (`CoreReservation::makeThisThreadBackground`). Per file: tails the primary (FileInputStream, share read / write) 4 MB a step and appends to `<backup>/<project folder name>/<relative path>`. Lag = elapsed x bytes-per-second - copied; above `maxLagSeconds` (20) or on any write error the copy is binned (closed, deleted, reason queued; MainWindow logs it and shows a notice). `finish()` after the writer has closed: copy the tail, then the first 64 KB again (the finished header). The audio thread and the main writer never see it. Setting `backupFolder` (user settings, per PC); folders inside the project folder are refused.

## Window lights, header boxes, Listening copy tab, dark look (2.60)
- MainWindow: `WindowLamp` after the Take / Edit / Mixers menu entries, state from `AppContext::windowLight(kind)` (Main.cpp: 2 = active kind, 1 = shown, 3 = all minimised / docked, 0 = none). `InfoBox` project / rate / depth / disk free; disk free checked every 2 s. `headerLineY` line painted under the header. Menu: all entries left to right, Media and Mastering last.
- Mastering: view 2 = Listening copy. `makeFormsSpec` (MasterExport.cpp) = a Files spec with `formsOnly` (no audio formats written) and `feedbackForm`; `MasterExportSettings::formRate / formBits / formFolder`. The Virtual Master export sets `feedbackForm = false`. Job kind 0 / 1 / 2 replaces `lastWasDisc` for the status line and buttons.
- Theme: `theme::setDark(true)` = the desk palette multiplied by `deskShade` (field, text, accent, warn, playhead kept). Setting `deskDark` (old `darkTheme` ignored, so everyone starts light). `MixTheme::ground` replaces the old `dark` test for the blue mixer ground; the mixer rebuilds when `theme::generation()` changes.

## Listening copy: one file with all the pieces, Save / Export, .fmnotes (2.61)
- Page template `tools/review_page.html` -> `src/core/ReviewPageTemplate.inc` (run `tools/make_review_template.py`; the page must stay pure ASCII). META now = `{id, title, project, name, tracks:[{piece, edit, offset, duration, mime, peaks}]}`; audio chunks are `<script type="text/plain" data-k="N" class="ch">`. The page finds the chunks in `start()` (DOMContentLoaded: the chunks come after the script), decodes piece 0 first and the others in the background (a piece asked for jumps the queue), plays straight on at `ended`.
- `reviewpage::writeMany (html, title, project, pieces)`; `write()` = one piece. `MasterExportSettings::formSingle` -> `MasterJobSpec::formSingle / formTitle` (album title, else project name); in the export loop the pieces' audio waits in `_fermata_work` until the one page is written.
- Comments: localStorage key `fermata-review-<id>` = `{who, notes:[{id,k,t,bar,text,at}], exported}`; no beforeunload handler. Export = `<META.name with "listening copy" -> "listening notes">.fmnotes`, JSON `{fermata:"feedback", v:2, id, name, title, who, ...}`: one piece = the v1 shape (`edit, piece, offset, notes`), several = `pieces:[{k, edit, piece, offset, duration, notes}]`. "Open exported comments" maps by edit id, then piece name, then index (same id).
- Import: `feedback::Parsed::parts` (one per piece with notes); EditWindow::importFeedbackFiles puts each part on its edit (by id, else by edit name; skipped and reported if neither exists). The chooser lists *.fmnotes first.
- Main window: `setFullScreen (true)` after it is shown (native title bar = maximised).

## Host transport for plug-ins (2.62)
- `src/core/HostTransport.h`: `HostTransport {playing, recording, samplePos, sampleRate}` and a thread_local `currentHostTransport()`. AudioEngine fills `transport` per chunk in process() (playback timeline position, or the recording's sample count; stays put when stopped) and per step in renderOffline() (`offlinePos`); processMixer() points the running thread at it, so it works on the helper threads too. Vst3Insert is the plug-in's AudioPlayHead (120 bpm, 4/4, 25 fps placeholders).
- Vst3Insert::prepare sets the main buses to stereo with setBusesLayout (other buses as the plug-in has them), hands over as many channels as the plug-in uses (extra ones silent), and copies a mono-only output to both sides.
- Mixer: `valueentry::ReadoutClick` makes a read-out label open the value box (fader level: double-click; pan: click). Fader caps and pan sliders reset on double-click (TypedSlider without onDoubleClick).
- Tails: MixerPlan `ranLive / tailNow / tailLeft / quietFor / outPeak`. A mixer that stops being live (mixerIsLive) keeps running as a job with `tailNow` (no disk audio, no live inputs) until its Ext Bus peak has stayed under -90 dB for 0.5 s, at most 30 s. processMixer clears the delay lines only when playing STARTS (not at stop). carryOver copies the tail state.
- Mixer strips: `kStripWidths[6]`, `stripWidthLevel()` (setting `mixerStripWidth`), `stripWidthGen()` makes every open MixerComponent rebuild; FaderBlock squeezes its gap / scale / slot in narrow strips. StripBase::paint rounds the corners (6 px). `MixTheme::ringTrack` = the dark-look groove for dial rings in both looks.
- ToolWindow::keepTitleBarClear: a maximised window that overlaps the floating menu bar at all is un-maximised and fitted below it.
- (2.62 - 2.89) Edit window bus view with a reference waveform box: removed in 2.90 (the buses and VCAs are rows under the tracks; see "Lane rows" below).
- Main window header: logo | displays | status (as wide as its text, `statusWidthFor`) | DRIVER INPUTS | stage speaker boxes in all the rest (right-aligned; they scroll only when they need more).

## Re-HarmoniSer 2.63: lone voice, ring-out, wobble trend, box handles, joint voice fit
- `Params::lone` (default true): in retuneSegment, when `nb` is empty, each overtone's band is `lowpassFlat` (Dsp) with half-width = clip (loneSpreadCents + loneMargin, 35, loneMaxHalf) cents converted to Hz at that overtone (so it scales with h; the old Gaussian scaled with sqrt(h) and its FWHM was only bw/2). `loneSpreadCents` = 95th percentile of how far the line moved over the last 0.6 s (the first 0.2 s, the scoop, left out). `loneBandLimit` looks at the demodulated profile (overtones 1-4, 0.25 s windows, cents from the line): where it turns up again, or is louder than -15 dB beyond the note's own hump, the band stays near the hump (down to 15 c). `ringOutEnd` first looks for a legato departure (within 0.3 s of the box end: the broadband level still within 10 dB, but the band's share of it down to a fifth): then the fade-out runs from the box end to that moment (fadeOut comes back shorter); otherwise it extends the fade-out after the box while the band's level (overtones 1-3) decays at >= 8 dB/s, up to `tailMax` (1.5 s); the editor cuts `tailMax` more audio for a lone note (renderCorrection / hit).
- `Params::trendFit` (default true): planCurve's base is a Gaussian-weighted local LINE fit (sigma = smooth), with the ease-in region weighted 0.05, so `wob` is zero-mean on the steady part. The golden tests set lone = false, trendFit = false (the prototype's algorithm).
- `retuneVoices (..., jointFit = true)`: refineVoice for every voice, then separateAndMove with the moved voices' new lines (others nullptr). The editor uses the default.
- Editor View: `edgeAt` / Mode::edge: the box's sides are draggable once a note is measured (handles drawn on the box); mouseMove sets the cursor.
- Lab (scratchpad/tune, not in the repo): fix2 harness, bench of 8 semi-synthetic scenarios, the solo clip analysis scripts (resid.py = residual at the old pitch with the note moved an octave, pitchof.py = independent pitch check).

## Version numbers
- MAJOR.MINOR.PATCH, defined once as `FERMATA_VERSION` at the top of `CMakeLists.txt`; history in `CHANGELOG.md`. Every delivery bumps it (fix = PATCH, feature = MINOR, big change = MAJOR) and adds a CHANGELOG entry. 1.0.0 is the stable base before the CPU-core work (kept as the GitHub branch `stable-1.0` and as a full source zip); 2.0.0 is the first build with the CPU cores window.

## 2.64: time readout, plug-in reload format, follow mute, Space in plug-in windows
- `src/core/TimeFormat.h` (pure, tested in `tests/timeformat_test.cpp`, run by tools/sandbox-check.sh): `timefmt::format()` writes a position in four modes (hours:min:sec.ms, min:sec.ms, timecode at any LTC frame rate incl. drop-frame with a start timecode, samples) and says which leading zeros are padding; `timefmt::parse()` reads what the person types (90, 1:30, 1:02:03.5, 2m10s, 01:02:03:04 in Timecode, a number in Samples).
- `src/ui/TimeReadout.h`: the component. `setPosition()` / `setState()` (stopped = white, playing = light blue, recording = red), or `follow (positionFn, stateFn)`; `onJump` makes double-click open the typed-time box; `getIdealWidth (height)` for layout; `onFormatChanged` fires when the shared mode changes. Under 40 px high it shows only digits. The mode, frame rate and start timecode are shared by every readout (`timereadout::shared()`), kept in the user settings as `timeReadout` by AppContext. Digits sit in fixed cells (the widest digit of the font); a text longer than the mode was made for (100+ minutes, ten-digit samples) shrinks its digits.
- Placed in the take and edit windows as an ordinary FlowItem (58 px high, as tall as the Next Take box): take window after timeLabel, edit window after the name box (2.64.0 had it centred with a flowAroundCentre layout; the user preferred it on the left, so that was removed in 2.64.1). `grid::flow()` can measure only (`place = false`) and, with `keepGroups`, moves a group (items up to one with gapAfter >= 10) to the next line rather than splitting it; the take and edit windows use keepGroups. Take window: `TakeTimeline::clockSeconds()` (live position while recording / playing here, smooth; otherwise the playhead). Edit window: the smooth playhead while this edit plays, otherwise `transportNow()`. State is global (engine recording -> red, app playing -> blue). onJump = goTo().
- Plug-ins on reload: `Project::restoreSampleRate / restoreBlock` were never set from the device (stayed 48000 / 512), so `slotsFromVar` prepared every plug-in of a project opened while the device ran at those values. `AudioEngine::prepare()` now copies its rate and block into them. `Vst3Insert::process` runs a block longer than its work buffer in pieces (it used to return, i.e. bypass the plug-in).
- `SendState::followMute` (default true, saved only when false as "followMute"). Engine: `soloOk` = not silenced by another channel's solo; a send with followMute off builds its own feed in sendTmp from `pre` with a ramp (`lastFeed`) to 1 (pre) or faderGain (post), times soloOk. The send dial label gets a "*" when it is off.
- Plug-in windows: `PluginHost::EditorWindow` polls the space bar (40 Hz, `KeyPress::isKeyCurrentlyDown`, no modifiers) and, on the press edge while it is the foreground window (`isForegroundWindow()` in WinOwner: GetForegroundWindow on Windows, so it works while the plug-in's own child window has the focus), sends Space to `ToolWindow::lastTransportWindow()`. Its keyPressed swallows Space so it never acts twice.
- Take window In / Out flags are collected in the take loop and drawn after it, Out first, In over them; number 0 ("fx") draws a grey tag sized to "I fx" / "O fx" at 8.5 pt.
- StripBase::paint: the name plate is a path with the bottom corners rounded, its dark bottom line inset, and the strip outline re-stroked over it.
- `ClickAwayEndsTyping` (Uikit.h), used by MainComponent: a mouse-down anywhere outside the focused TextEditor gives its focus away. Input / output name boxes: Enter commits and gives the focus away, Escape restores the name.
- Re-HarmoniSer `Params::oneVoice` ("One voice only" tick box, `oneVoiceBox`, remembered in a static for the next note, part of params so undo / NoteEdit keep it): `measureNote (..., lookForOthers = false)` skips findNeighbours; retuneSegment treats the note as lone whatever nb holds, skips `loneBandLimit`, and ignores nb lines. Lone path: overtones up to `loneTopHz` (8 kHz) with oneVoice; without it up to the overtone where (flat1 + roll1) * h reaches 0.45 * f0min (min nharm). Each overtone's band (`loneBand`) is capped at 0.45 * f0min so bands never overlap (the cap used to be applied only at h = 1, and scaled with h). Test: reharmoniser_voices_test "One voice only" (no neighbours looked for; a handed-in line ignored; a 24-overtone voice: overtones 12-22 at the old pitch -45 dB vs 0 dB with ten).
- 2.64.2: `StackedField` (Uikit.h) puts a caption above its box as one 58 px FlowItem (the take / edit windows' name: "Piece / Take Window Name:" / "Piece / Edit Window Name:"); `IconButton::setBoxLook (true)` draws a button like NextTakeBox (corners 5, white 55 % outline, its text in the top third, the icon below) - the take window's record button, 84 x 58.
- 2.64.3: MixerStripsHolder::clear() now clears `widths` too (rebuilt strips kept the old widths, so STRIP WIDTH only showed after reopening); the width change also calls fitWindowToStrips(). Take / edit window top bars: the head (StackedField column - in the take window two pairs, name and "To edit goes into", a line between, `addPair` / `idealHeight`; Next Take, REC, take length, readout) is placed by hand; the picture buttons flow (keepGroups) in the area right of the readout, minus `grid::cornerReserve` (58 px for the pin and sun / moon buttons); under 300 px there they flow under the head across the whole width.
- 2.64.4: `StackedField::setTopAligned (true)` (take window): captions from the very top, 18 px caption + box 2 px under it per pair (44), 9 px with the line between pairs. The take window's timeLabel is as wide as "00:00:00.000" in its font (+12) and right-justified, 6 px before the readout.

## 2.65: several main outputs, cue mixers that follow the processing mixer
- `BusState::isMain` (Ext Bus, per mixer; saved as "main" with outFirst, the mixer gets "mainFlags": true). `MixerState::mainBus` is only read from older projects (no "mainFlags"): that bus, else the first Ext Bus, gets isMain. `Project::syncMixers` ticks a mixer's FIRST Ext Bus when it is created (a new mixer, or the first Ext Bus of a mixer that had none); later ones start unticked. makeEditMixer copies isMain with outFirst.
- Plan: a Track / Int Bus output with outDest null (or a bus that is gone) gets an edge to EVERY isMain Ext Bus (all with the same outSend state; it is always unity, so the shared ramp is harmless). None ticked: no edge. `Node::isMainOut` = the first main (talkback playback tap, follow capture).
- `MixerState::followProcessing` (cue mixers only; saved). process(): the following cue mixers are listed first (live, with outputs or auditioned) and NOT run (`resumeClear` empties their delay lines when they run again); the source (`editPlan` if an edit plays, else the processing mixer) gets `MixerJob::capMain` (forced to run) and processMixer copies its isMainOut bus's output into `mainCap` (passed as mainL / mainR, also through BlockJobs for the helper threads). After the jobs, each follower adds mainCap to every one of its Ext buses' driver pairs (and the engineer's pair when auditioned) and sets their meters.
- UI: StripOutputRow's "Main out" ticks the bus's isMain; MixerComponent `followBox` (cue mixers) and `FollowCover` (an overlay that takes the mouse) over the strips while it follows.
- Tests (core_test): "main outputs: ..." (first Ext Bus ticked on a new mixer, later ones not; two mains both carry the mix; none = silent; a main plus a send; a follower carries the processing main mix exactly and its own mix is untouched; save / load; a 2.64-style var gets its mainBus ticked); the old "strip outputs" test uses isMain.
- StripOutputRow::fill() returned at once for an Ext Bus, so refresh() (the Main out tick) never ran: the box always looked unticked. It now refreshes. In the legacy fix-up note that a default-constructed juce::Uuid is a new random id: start from `juce::Uuid::null()`.

## 2.66: the recording writer, and the audio-thread review

A full review of the audio thread and the recording path (read line by line; the engine and the test suite run under ThreadSanitizer, AddressSanitizer and UBSan; a new stress test, `tests/stress_test.cpp`, runs a pretend audio thread flat out while a pretend user records, plays, rebuilds the plan and swaps plug-ins). The sanitizers found no memory errors and no races in Fermata's code; the review found the problems below, all fixed here.

- **`TakeWriter` (src/core/TakeWriter.h) replaces `juce::AudioFormatWriter::ThreadedWriter`.** The JUCE one ignored every disk write's result (a full drive went unnoticed), dropped a block that did not fit (the rest of the take moved earlier), woke the disk thread once per track per block, buffered a fixed number of samples (1.4 s at 192 kHz), and converted float to 24-bit with `INT_MAX * x`, which writes positive words above -6 dBFS one LSB low (4,194,302 of the 16,777,216 words; verified numerically). The new writer: a lock-free fifo per file sized in seconds (`AudioEngine::kRecordBufferSeconds` = 8, 1.5x in session mode); `failed()` when a write is refused (the engine's `recordingProblem()` tells the UI); a dropped block is marked (`markAt` / `markLost`: after how many accepted frames, how many lost) and written as silence at that point by the disk thread, so the file keeps its length and its timing - a run of drops with no acceptance between is one marker, and if 256 markers are pending the rest goes at the end (`tailLost`); exact words with `std::lrintf (x * 2^23)`; `writer->flush()` every 10 s rewrites the header. The engine calls `writerThread.notify()` once per block for all the files. The file stream is a `FileOutputStream` with a 1 KB buffer: the audio goes in 32768-frame pieces anyway, and a 16 KB buffer would hold the last moments back from the OS.
- **The engine** keeps `recBytesPerSecond` and `recordingFolder()` for the DISK FREE box; `TakeGroup` gained `overruns`, `lostSamples` and `writeFailed` (saved as "overruns", "lost", "writeFailed"); `stopRecording` leaves the session in memory (never frees it) when `waitForAudioThread()` says the callback has hung, as `stopPlayback` and `rebuildPlan` always did.
- **WavRepair** writes an RF64 header when the mended audio is 4 GB or more: "RF64", size -1, a `ds64` chunk (riff size, data size, sample count, no table) in the JUNK placeholder JUCE's writer leaves right after "WAVE" (28 bytes, or 52 with the short fmt chunk; what is left stays a JUNK chunk), and a data size of -1. `inspect (file, repair, extendToFileEnd)`: the third argument, used only for takes Fermata was recording when the program stopped (group length 0, file inside the project), also mends a header that says LESS than the file holds (the header refresh leaves it up to 10 s stale). Never for a file from elsewhere: a WAV with chunks after its audio would be read as audio.
- **Helper threads:** `CoreReservation::audioThreadCheck` joins "Pro Audio" always (it used to only when the reservation was active); `reservedLogicalProcessors()` gives the engine the cap for `MixerWorkers::setLimit` (set at the top of every callback: reserved - 1, or no limit); `AudioEngine::setMixerThreads` returns false and does nothing while the device runs (the workers cannot be replaced under a block).
- **`InsertSlot::isLoaded()` / `getName()`** read `proc` without the lock: only the message thread ever changes it (as `refreshLatency()` already assumed). The mixer window asked every slot's name at 3 Hz; each collision with the audio thread's try-lock skipped the plug-in for a block (dry pass-through).
- **Playback ring** is `std::vector<float>` per channel (a `juce::AudioBuffer` shared between the reader thread and the audio thread raced on its `isClear` flag - harmless, but TSan was right to say so).
- **Latency hash** in `uint64` (the signed multiply overflowed: UB).
- Still open from the review, for a later build with timing on the real PC: buffered playback reads (JUCE reads WAVs in 5.7 KB system calls), the disk writer and playback reader pinned to the reserved cores, and `getStateInformation` of every plug-in at every autosave.

## 2.67: load meter, timing log, buffered playback reads, disk threads off the reserved cores, quick saves

- **Load meter:** `AudioEngine::audioDeviceIOCallbackWithContext` times `process()` with the high-resolution clock; busy / span (block length at the sample rate) is summed, the peak kept and late blocks (busy > span) counted. `takeLoadStats()` (message thread) exchanges them. `AppContext::tickLoad` (1 Hz) keeps `lastLoad` for the status line and writes a "Timing" line to the log every 60 s and when a take ends. The recording-buffer fill is `TakeWriter::fillPercent()`, the highest over the files, sampled after every recorded block.
- **Buffered playback readers:** `PlaybackSession::open` builds each reader on a `BufferedInputStream` (owning a `FileInputStream`); size `bufferBytesFor (segments)` = 96 MB / segments, between 32 KB and 1 MB. JUCE's `BufferedInputStream::setPosition` only moves the position, so the WAV reader's seek before every read stays inside the buffer when reading on.
- **Disk threads:** the writer thread and the playback reader are no longer `registerAudioThread`-ed, so with a reservation active they follow the process default (the general cores). Only the callback and the `MixerWorkers` live on the reserved cores.
- **Quick saves:** `InsertProcessor::getStateBase64Quick()` (default: the full one). `Vst3Insert` caches the last state; an `AudioProcessorListener` on the plug-in (parameter changed / processor changed, any thread: one atomic flag), `setParamNorm` (automation; `setValue` tells no listener) and `stateWasSet()` after a restore mark it stale; an open editor or an age over 60 s also forces a fresh one. `Project::save (error, quick)` sets a thread-local flag that `slotsToVar` reads, so only a quick save ever uses the cache; `toVar()` for a render copy, copying a mix or a full save asks afresh. Quick: the 4 s autosave, the save after playback stops, a new window's first save, and Record start / stop.

## 2.67.1: big writes, background finishing

- **`TakeWriter` minWriteSamples** (the engine passes 0.5 s, at most `kChunk` = 65536, at most a quarter of the buffer): `drain()` returns 20 ms without writing while less than that is waiting, unless closing or an overrun's silence marker is due. The first rig test (40 mono files, 96 kHz, internal NVMe) fell about 20% behind with one write of a few KB per file every 10-15 ms: the cost is per write call (NTFS extending the file, Defender's filter), not per byte. The 1 KB `FileOutputStream` buffer from 2.66 stays (the writes are now far bigger than any buffer).
- **Background finishing:** `stopRecording` computes the take's length and overrun figures, builds the log line, and hands the session to the `Finisher` thread (`AudioEngine::Finishing`: the session, the backup jobs, the files, the report, `done`). The finisher resets every writer (drain + header), then calls `BackupCopier::finish` (the copier must see the closed file with its final header), logs, sets `done`. The message thread owns the list: `isFinishing (gid)`, `waitForFinishedTakes (ms)` (used by `startPlayback`, 15 s cap, and the engine's destructor), `reapFinishedTakes()` (AppContext's 100 ms watcher; returns the files, which `AppContext::forgetWaveforms` drops from the in-memory waveform caches through redrawLog so a thumbnail read from a half-written file is not kept). `getLivePeaks (..., groupId)` also finds a finishing take's peaks (the finisher never touches them); the take window draws a finishing take with them.
- **DirectWaveCache::draw (..., rateHint)**: with the rate given, it returns before opening the file when the view is not zoomed in past 128 samples per pixel (take and edit windows pass it).

## 2.68: Space / R everywhere, Enter in name boxes, mixer layout

- **Space / R:** `AppContext::pollTransportKeys` (in the 60 Hz talkback poller) reads the keys with `KeyPress::isKeyCurrentlyDown`, edge-triggered, only when `Process::isForegroundProcess()`, a JUCE top-level window is active (not a Windows file dialog), no modifier is held and no editable `TextEditor` has the keyboard. Space while recording -> `toggleRecord`; R while not recording -> `toggleRecord` into `AppContext::lastTakeWindowId()` (Main: the open take window with the highest `lastActive`, else `recentTake`; then `recordTarget`, then the first). A component carrying `ownsRKeyProperty()` (the edit window, `JobDialogBase`, the plug-in `EditorWindow`) keeps R. So that the same press is not acted on twice, the windows' own Space handlers do nothing while `spaceIsForRecording()` (a take is recording, or the Space that stopped one is still held). The take window's own R still starts a recording into itself (whichever of the two comes first wins; the other sees the recording and does nothing).
- **Enter leaves the box** in the VCA name, the mixer name, the gap boxes and the take display timer (`giveAwayKeyboardFocus`), so Space / R work again at once.
- **VCA strip:** its body is trimmed by `kLatH` like the strips that show a plug-in delay line, so M / S, the fader, its meter and level number line up.
- **Strip width:** a change keeps the meter strip in place: the window keeps its right edge (and is never made wider than the screen), and a scrolled view keeps its distance from the right end. `applyHeight` leaves room for the horizontal scroll bar when the strips are wider than the view (they used to run on under it, hiding their rounded bottoms).
- **VCA ghost line:** `FaderBlock::setVcaGhost (active, addDb, muted, colour)`, set from the strips' `tick()` with `vcaGhostFor` (the VCAs in the strip's `vcaMask`: dB added up, any muted, the first one's colour); `paintOverChildren` draws a 2 px line in the gap between the slot and the meters from the cap to fader + VCA dB (bottom when muted) and a faint cap there.
- **Pre-Rec memory in the background:** `ensurePreRoll` no longer allocates the ring; it records what is wanted (`wantedRingInputs` / `wantedRingCap`) and asks `RingBuilder` (a thread) to make it. `servicePreRoll` (AppContext's 100 ms watcher, and `ensurePreRoll` itself) puts a finished ring in place if it is still what is wanted and nothing is recording (the old one is freed after `waitForAudioThread`). `waitForPreRoll (ms)` for tests and anything that must have it. A take started before the ring is in place simply has no pre-roll.
- **Take window name column:** `nameBlock` starts at the window's top (y 1) instead of with the boxes (y 10), captions top-left with no top border.
- **Play marked** button removed from the take window. Play never stops at mark 2; only Loop (L) uses the marks, going back to mark 1.

## 2.68.1: saving that survives being interrupted

- `src/core/FileSync.h`: `syncToDisk` (FlushFileBuffers / fsync), `replaceTextSafely (target, text, keepBackup)` (temporary file beside the target, synced, the old file moved to `<name>.bak`, then the rename; a rename that fails puts the old file back), `readJsonWithBackup (file, var, accept)` (0 = unreadable, 1 = read, 2 = the `.bak` was used: the broken file is set aside as `<name>.damaged` and the `.bak` copied into its place, so the folder is whole again at once). The Windows branch is compiled by `tools/check-windows-code.sh` against `tools/win-stubs/windows.h`.
- `Project::save` order (2.102: split into `prepareSave` / `writeSave`, see there): clear `dirty` (so a change made during the snapshot is kept for the next save), `toVar`, `splitWindowFiles` (writes the window files that changed, returns `next` and the `OldWindowFile` list), `windowFiles = next` and the old files appended to `oldWindowFiles`, THEN the project file, THEN `oldWindowFiles` tidied away (renamed -> delete, gone -> `_removed`, their `.bak` deleted). A failed project-file write sets `dirty` back and keeps `oldWindowFiles` for the save that gets through. `createDefaultDesign` clears `windowFiles` / `missingWindowRefs` / `loadNotes` / `oldWindowFiles` (a new project used to inherit them).
- `Project::load` and `mergeWindowFiles` read through `readJsonWithBackup`; a recovery adds a `loadNotes` line naming the `.bak` and `.damaged` files. `AppContext::lastProjectFile` accepts a project whose file is gone but whose `.bak` is there.
- Tests: core_test "2.68.1 saving that survives being interrupted" (the `.bak` contents, a damaged and a deleted project file, a damaged window file, a save that fails at the project file and the tidy-up on the next one, `createDefaultDesign` clearing the lists).
- Left as decisions (not changed): the automatic save is held while recording (markers made during a long take are lost if the program stops before Stop); `undoState` serialises the whole editorial project 500 ms after each change (tens of ms on a project with thousands of takes).

## 2.69: per-window undo snapshots and saves

- Measured first (tests "2.69 undo per window"): 50 pieces x 60 takes x 20 stereo tracks plus 50 edits x 40 pieces. The old `undoState()` (whole project as JSON) took ~300 ms per settled change (1.4 s with big edits), the stack held 30 x 8-38 MB; a quick save ~300 ms. Now: a settled change ~20 ms, undo ~25 ms, a save with nothing changed ~20 ms, with one take changed ~30 ms (sandbox, -O1).
- `Project::undoHash (TakeWindowDef / EditDef)`: an FNV-style hash over exactly the fields `undoText` serialises (`takeWindowToVar` / `editToVar` with the view keys stripped: `kTakeViewKeys` / `kEditViewKeys`). `undoHash (MasteringDef)` hashes its text (small). `saveHash` = undoHash + the view state that is saved. The test "2.69 undo hash" changes every field one at a time and checks hash and text move together (76 fields) and that view fields move neither (14) - ADD A LINE THERE FOR ANY FIELD ADDED TO THE WINDOW / EDIT VARS, and the field to the hash.
- Snapshots: `undoTakes` / `undoEdits` (id -> {hash, text}) and `undoMastering`, filled by `undoReset` (the one full serialisation, at open) and kept up to date by `undoTick`: a window whose hash moved contributes its previous text to the `UndoStep` and gets a new snapshot; a new window just gets a snapshot (not undoable, as before); a removed one drops out. `undo` / `redo` restore each piece of a step with `restorePiece` (the old per-kind merge logic: the take being recorded and the view state are kept) and push the current texts of those same windows onto the other stack.
- Saving: `toVar (false, false)` builds the root without the windows; `splitWindowFiles` walks the structs, and `consider` writes a window file only when `takeFileHash` / `editFileHash` (saveHash + the tracks' fingerprint + the edit mixer's var fingerprint, the mixer var taken from root so plug-ins are asked once) differs from `windowFiles[id].hash` or the file is missing. Only then is the window's var built. If a window file cannot be written, `addWindowsToVar` puts the windows into root and the project is written whole, as before. `refreshWindowFileHashes` after `load` (with `quickPluginState`) sets the hashes to what was read, so the first save after opening writes nothing.

## 2.70: screen speed and opening on a big project

- Measured with the Xvfb render harness at -O2 (`scratchpad/rend`: `mkbig` makes a 3000-take project with 1200 real 8 s files; `bench.sh take|edit|mixer|main` runs the render build with `FERMATA_BENCH=1`, which times `createComponentSnapshot` of each open window and of its children, zooms with the arrow keys, and times `project.changed()` + `sendSynchronousChangeMessage()`; `FERMATA_PAINT_PROFILE=1` adds the `paintprof` breakdown). Software renderer numbers: Windows with Direct2D will be lower for the fills, the same for the CPU-side work.
- Take window (`TakeTimeline::paintAll`): `g.getClipBounds()` culls takes by x (60 px margin for flags and fade corners) and rows by y; a per-paint `rowOf` map and `rowColour` table replace `trackRow()` (which built `shownTracks()` for every clip) and `chan::of` per clip; the name column is skipped when outside the clip; while recording `timerCallback` repaints only the live take's columns. Edit window (`EditTimeline::paintAll`): the same culling (fade reach + 60 px), `rowColour` per paint. Before: a 150 px meter repaint cost 30-45 ms (all 1200 clips worked through), a full paint 64 ms; after: 2 ms and 18 ms.
- `LevelMeter::setLevel` repaints only when level / rms / hold / latched moved (shownLevel ...). `fillEnvelope (..., bandHeight)` skips the two outline strokes under 10 px. `drawThumbnailEnvelope` returns on a clip under 2 px wide.
- `MainComponent::changeListenerCallback`: `resized()` (12 ms, 40 strips, then a full repaint) only when `StageSpeakerPanel::rebuildIfNeeded()` returns true or the panel's wanted width changed (`laidOutStageW`); `rebuildRows` still on structure changes (~120 ms: creating 40 InputRows). `Project::undo / redo` raise `structureChanged()` only when an edit came back (restorePiece returns true; `syncEditMixers` for it), otherwise `changed()` - a structure change rebuilds the main window's rows, every mixer window and the engine's plan.
- `ThumbSaver` keeps a `pending` set (thumbnails being read) so `saveFinished` no longer walks every thumbnail at every change (2 ms with 1200).
- Opening: `undoReset` stores a copy of each window (`UndoSnap::takeCopy / editCopy`, `undoTextOf` makes the text on first need) - 600 ms -> 18 ms. `TakeGroup::verified` (saved "verified") is set by `repairInterruptedRecordings` for a take found whole (reset when mended; relinkAudio clears all), and verified takes are skipped unless `Project::filesMoved` (set by `getFilePath` when a file was found by its project-relative path, not where it was saved). Pre-made `juce::Identifier`s (`key::k_...`) for the take / piece keys in `fromVar` / `toVar`: var lookups by text go through JUCE's locked `StringPool` (62 ns vs 24 ns each; a million of them here). `Project::load` logs "Project read in N ms: project file, window files, set up, file hashes"; `AppContext::openProject` logs "Opened <name> in N ms: files read, recordings checked, audio and undo ready".
- Tried and dropped: reading / parsing the window files and building the windows from their vars on several threads (`parallelFor`): JUCE's JSON parser and every `var["key"]` go through one locked string pool, and two threads took three times as long as one. `parallelFor` is left as a plain loop with that note. What remains of opening the test project (sandbox, 2 cores): window files 510 ms (juce::JSON on ~20 MB), set up 190-230 ms, hashes 40 ms.

## 2.71: plug-in hosting review; playback, bounce and mastering review

- **`InsertSlot` (Mixer.h):** `set` / `clear` return bool, `take` returns null, `reprepare` does nothing, when the slot's `SpinLock` cannot be taken within `kLockWaitMs` (2000 ms: `TimedLock`, tryEnter + sleep (1)); `stuck()` counts it in `stuckCount()` and logs. The audio thread's `process` / `setParamNorm` try-lock as before (never wait). `set` prepares the new processor OUTSIDE the lock, and the old one is destroyed outside it too. A bypassed slot runs the plug-in on `scratch` (kMaxTrackChannels x maxBlock, sized in `set` / `reprepare` on the message thread) and throws the copy away; `latencySamples()` still says 0 while bypassed, so the delay compensation is unchanged. Tests: "a bypassed insert still hears the music", "a plug-in stuck in its process call cannot hang the program" (a Sleeper processor held in `process` on another thread: a 300 ms one is waited for, a 2.6 s one makes `set` return false after ~2 s with the slot unchanged; `take` afterwards gets the plug-in out).
- **Retired mixers freed:** `AudioEngine::waitForAudioThread()` clears the sticky `neverStalled` flag when it gives up (`audioThreadNeverStalled()`): from then on nothing retired is ever freed (the leaked plan / session may point at it). `AppContext::freeRetiredMixersSoon()` (called right after `engine.rebuildPlan()` in the structure-change handler and in `afterProjectReplaced`, and from `stopPlayback`) takes `count = retiredMixers.size()` and posts a `callAsync` that, if the flag still holds and nothing is playing (an edit's `AutomationPlan` holds `Param*` / `InsertSlot*` into whatever mixers existed when Play was pressed), closes the editors of every slot of the first `count` retired mixers (strips, sends, outSend, buses) and calls `Project::freeRetiredMixers (count)`. The ordering rests on JUCE's message queue being FIFO: every retirement (`removeMixer`, `syncEditMixers`, `retireAllMixers` in `fromVar` / `createDefaultDesign`) posts its `structureChanged()` before `freeRetiredMixersSoon` can be called, so the windows have rebuilt (dropping their `MixerState&` strips) and the plan has been rebuilt before the posted free runs. The engine's `auditionMixer` and `scopeSource` pointers are only ever compared, never dereferenced. Anything new that keeps a raw pointer into a mixer across messages must be rebuilt in a structure listener or look the mixer up by id. Test: "retired mixers are only freed once the audio thread has certainly moved on from them" (also the sticky flag: `setRunningForTests (true)` + `rebuildPlan()` with no blocks).
- **Plug-in scan:** `PluginDirectoryScanner` with the dead-man's-pedal file `plugin-scan-in-progress.txt` beside `plugins.xml` (JUCE blacklists the file named in it at the next scan); `getFailedFiles()` and newly blacklisted files are logged. `EditorWindow::closeButtonPressed` captures host and insert by value for its `callAsync` (the window may be gone when it runs). `newProject` / `openProject` call `plugins.closeAllEditors()` right after `stopPlayback()`.
- **Lazy playback readers (`Playback.h/.cpp`):** `PlaySegment::rate` (the file's rate as the project knows it, set by all three builders). `PlaybackSession::open()` opens nothing: it checks every segment's `rate` against the device rate and that each distinct file exists, and makes the `opener` (`makeOpener (formats, bufferBytesFor (tracks * 3), rate, problem*)`: `BufferedInputStream` over a `FileInputStream`, a reader whose rate differs is refused with a log line and `*problem`). `renderSegments (..., const Opener*)` opens a null reader the first time its segment overlaps the render; `releaseReadersBefore (readers, segs, pos)` closes the readers of segments ending at or before `pos` (`run()` calls it after every chunk unless looping). `renderNow` / `openReaderCount` / `getReaders` / `getSegments` for the tests. `Bounce::render` uses the same opener (problem -> `res.error`) and releases behind itself. Tests: "playback opens each file as it reaches it and closes it behind it" (counts, a missing file, a wrong stated rate, a wrong real rate, no opener), `bufferBytesFor`.
- **Audible span (`EditDef::audibleStart / audibleEnd`):** min of `fadeInBegin` / `startSample` and max of `fadeOutFinish` / `endSample` over regions and overdubs (may be negative: a fade-in dragged before a piece at 0). Used by `MasterRenderBatch` (`bi.start / bi.end`), `masterLengthSamples`, `MasterExport` (`editOffsetSeconds`, the listening copy's time scale) and the Bounce window's whole-edit item. The Edit's segments already began at `fadeInBegin` and ended at `fadeOutFinish`, so playing was right; only the whole-edit renders cut at the piece edges. Tests: "a fade that reaches outside its piece is part of what the edit plays, bounces and masters" and the whole-edit Bouncer item in the bounce section (the file starts with the fade).
- **Left as decisions (not changed):** a plug-in crash still takes the program with it (in-process hosting; a bridge process is a project of its own); a quick save's plug-in state may be up to 60 s old when a plug-in changes itself with its window closed (`getStateBase64Quick`); automation values are applied at input time, not per strip's plug-in delay (a lane on a strip behind a 50 ms look-ahead lands 50 ms early); surround tracks pass only channels 0-1 through plug-ins (`Vst3Insert` is stereo); the DDP field layout (DDPID, DDPMS map packets, VVVS PQ records) was checked field by field against the open-source DDP 2.00 reader ddplib (github.com/NonStaticEu/ddplib, Oct 2026) and agrees, but no image has yet been through a plant's checker or a DDP player; JUCE's JSON parse remains the floor of the open time.

## 2.72: automation delay compensation

- `AudioEngine::buildPlan`, in the delay-compensation block, publishes how late the sound is where each automatable control acts: `InsertSlot::autoLat` (node inLat + the slots before it; send slots: the node's outLat + the send slots before), `StripState / BusState::autoLatIn` (= inLat: input gain) and `autoLatOut` (= outLat: fader, pan), `SendState::autoLat` (= the destination's inLat for a send in the plan: its level is applied after its own inserts AND its `sp.delay`; a send not in the plan gets outLat + its slots), `VcaState::autoLat` (the largest outLat of the channels in its mask: what the VCA's own fader shows while automated). Atomics: written on the message thread, read by the audio thread. `latencySignature` includes every strip's / bus's `vcaMask`, so a membership change rebuilds the plan at the next `checkLatency`.
- VCA lanes are exact per channel: `AutomationPlan::Item::members` (built in `automationPlanFor` from the mixer's pools by vcaMask at the time Play is pressed) each get the lane's value at their own `autoLatOut` in `StripState / BusState::vcaAutoDb[v]` with bit v set in `vcaAutoMask`; `MixerState::vcaGain (mask, own, ownMask)` uses those instead of `vcas[v].gainDb`. `AutomationPlan::end()` (from `AudioEngine::stopPlayback`, after `waitForAudioThread`) clears the bits. A channel added to the VCA during the play uses the VCA's fader (the latest channel's timing) until the next play.
- Sends moved by a lane: `SendState::autoHeld` (set by `AutomationPlan::begin()` in `AudioEngine::startPlayback` and per item in `Bouncer::render`, cleared by `end()`), and `buildPlan` connects a send when `inPlan()` (= active or held). Before, a send off at plan time had no edge, so a lane fading it up was never heard. Solo propagation through a send needs it to be audible (`active() || lastGain > 0`), so a held-but-silent send keeps nothing open. A held send is connected only if it closes no loop with the real edges (a DFS from its destination): `Project::sendAllowed` only checks sends that are on, and the topological sort breaks a loop by cutting whichever node comes first, which could silence an audible path (test "a send held for automation never closes a loop"). `Bouncer::render` ends the last piece's plan.
- Reviewed by a second agent twice (findings fixed above). Left: VCA members are taken when Play is pressed (a channel added mid-play follows the VCA's own fader until the next play); a stalled audio thread could set member bits again after `end()` (they clear at the next play's end); starting a play with held sends rebuilds the plan twice (stop, then begin: up to ~2 blocks each); a plug-in parameter lane is timed to the sound going INTO the plug-in (right for most; a plug-in applying a parameter at its output, after its own look-ahead, hears it that much early).
- `AutomationPlan::Item::lat` points at the right one (`automationPlanFor`); `AutomationPlan::apply (t, ps)` reads each lane at `t - lat`, or `ps->getTimelinePositionAgo (lat)` during playback (back round a loop to its end; before the play's start it just counts back). Live: `AudioEngine::process`; offline: `Bouncer::render` (`apply (first + done)`: the position of the sound going in, like live). Block granularity as before (one value per block, the fader ramps across it).
- Test: "2.72: automation lands on the music under it" (the published figures through two strip slots, a bus, sends and a VCA, bypass; `apply` with and without `lat`; the loop wrap; a bounce through a 100 ms look-ahead whose fader step lands at 1 s, not 0.9 s). A mutation run with the compensation switched off fails it.

## 2.73: Loop let go of plays on past mark 2

- `PlaybackSession::releaseLoop()` (engine: `releasePlaybackLoop()`; the take / edit windows' Loop switched off) vs the old `setLooping (false)` (FixTools: the pass finishes, the session ends). `setContinueTo (end)` (via `beginSession`'s continueTo) says where a released loop plays on to: the open end, like Play.
- The disk thread fades the last ~1 ms before the loop's end itself (`tailLen`, rendered as its own chunk; `transportFades (..., fadeEnd = false)` for loops that can be released) and remembers each faded pass end's place in the ring (`tails`, in `written` = ring samples). On release (handled at the top of `run()`, woken by `notify()`): if a pass end the audio has not finished hearing is in the ring, it sets `dropAt / dropLen / dropTimeline` and re-reads from `loopEnd - tailLen` unfaded; the audio thread (`pull`, now around `readRing`) skips `dropLen` (less what it already consumed) when its `position` reaches `dropAt`, then publishes `linearFrom = position, linearTimeline = loopEnd - tailLen`. Otherwise it reads straight on and publishes `linearFrom = wrapStream, linearTimeline = startPos`. `timelineAt (played)` maps consumed samples to the timeline (round the loop before, straight on after `linearFrom`) for `getTimelinePosition`, `getTimelinePositionAgo` (automation) and `timelineSecondsFromStart` (the playhead: `AppContext::playheadSeconds*` / `playedSeconds` use `engine.playbackTimelineSeconds`). `position` excludes dropped samples; after a release nothing compares it with `written` again.
- A release when the audio is within one block of a faded pass end (or already in it) lets that end play and goes on from the loop's end itself (`dropAt = tail + tailLen`, `renderPos = loopEnd`): a 1 ms dip instead of a moment heard twice. A skip that finds the audio already past the dropped region adds the overshoot to the timeline.
- Reviewed by a separate agent (no blocking findings). Left as they are: a release a few ms before mark 2 in an edit where a new file starts right after mark 2 can underrun once (the reader's lead is shorter there); every play's final 1 ms is now faded by the session too (inaudible); loops of 2 ms or less get no end fade; the take window's loop now hears every take on the window timeline under the marks (like Play), and the window's marks follow the take under the playhead while it loops.
- The take window's loop plays on the window timeline (`playTakeWindow (window, from, loopTo, rateOfTake)`, at the marked take's rate) so a released loop carries on through the window like Play.
- Test: "2.72: Loop switched off while it plays goes straight on past mark 2" (a ramp file whose samples say where they are: released mid-pass with the reader several passes ahead, released after the faded end was read, a long loop released early, and the old switch-off ending at mark 2; the timeline is checked against the sound on every block).

## 2.74: the Neumann MT48

- Captured with `tools/watch_device.py` (firmware 2.0.0b202606052): the MT48's dump has `identity.product = "MT 48"` (vendor Neumann). Modules: 154 "Mic/Line" (2 ch: mic/line, 48V, pad, lift, phase, cut, low cut with `lowCut_info: [40, 60, 80]`), 155 "Inst/Line" (2 ch: line/instrument, `inputMode` 1/2, phase, cut, the same low cut), 156/157 the hidden "Split" copies, 158 "Built-in Mic" (both ignored as before). Gains 0..660 in tenths for every mode.
- The MT48's Pad is three positions sent as two switches: OFF = pad off + lift on (the default), -12 = both off, -24 = pad on + lift off; the raw gain does not change, the page's scale moves 12 dB per step (`PreampSettings::steppedPad / padStep / setPadStep / shownGainOffset`). `RavennaDevice::isMT48 (dump)` sets `mt48`; `readChannel (module, index, isMT48)` sets `HwPreamp::steppedPad` for a module with pad and lift, `lowCutChoice` when `lowCut_info` lists more than one frequency, and `lowCutFreq`. `apply` sends `lowCut_freq` (with `lowCut` when it comes on), as the device's own page reports them.
- Input strip (MainWindow.cpp InputRow): on a steppedPad input the PAD key steps (`padStepNow`) and shows PAD / -12 / -24, BOOST and Z HI are hidden, the Pad is hidden in Line mode; on a lowCutChoice input the LC key steps (`lcHzNow`) and shows LC / 40 Hz / 60 Hz / 80 Hz. The readout and the typed gain add `shownGainOffset()`. Everything else (Anubis, Hapi) goes through exactly the old path. "Cut" is read but never shown or sent.
- `lowCutHz` saved in the project's inputs (only when not 80) and in the preamp memory; `PreampMemory::same` compares it only while the low cut is on.
- Checked against a stand-in device serving the captured dump (scratchpad harness: every step sends exactly the fields the MT48 reports for the same click) and core_test "2.74: the Neumann MT48".
- Reviewed by a separate agent: fixed that `Project::setPreamp` (a restored preamp memory) wiped what the input can do (`lineOnly`, `hasInstrument`, `lowCutChoice`, `steppedPad` now kept from the hardware) and that the PAD / LC steps started from what the key last showed (now from the input's current state). The gain knob's double-click gives 20 dB on the scale shown. Left: the preamp memory compares pad / lift on an MT48 input in Line mode (where the Pad is hidden); hidden keys leave their places empty on the strip.

## 2.74.1: a new project's Main bus

- `Project::addDefaultMainBus()`: an Ext Bus "Main", `isMain` and `outFirst = 0` in every mixer (just the processing mixer at that point; Edit mixers made later copy it). Called by `AppContext::newProject` and `startBlankProject` after `createDefaultDesign()`, NOT by the Project constructor (the tests build their own routing on a bare project). Test in "a new project starts completely empty".

## 2.75: David's list after testing 2.74

- **Take window head (TakeWindow.cpp `resized`):** `markLabel` and `statusLabel` share one rectangle on the "To edit goes into" row (`sendBox` bounds + `nameBlock` position, from `takeBox.getX()` to `clock.getRight()`); the timer shows `statusLabel` only while it has text and hides `markLabel` then. The 22 px info row is gone. TAKE LENGTH is a `TimeReadout` with `setLengthStyle (label)`: format forced to time-from-zero (`currentFormat`), no badge menu / cycling / typing, grey box, plain softer digits, dashes while `State::stopped`; it follows `recordedSeconds()` at 30 Hz while `hereRecording()`.
- **`formatTimeShort (seconds, decimals)` (Uikit.h):** MM:SS[.d] under an hour, HH:MM:SS[.d] from 3600 s; replaces every `formatTime (x).substring (3, ...)` (rulers and marks in take / edit / trim windows, FixTools, VolumeDialog).
- **Space from anywhere (Uikit.h `ToolWindow`):** `lastTransportWindow()` is now the newest live entry of `recentTransportWindows()` (SafePointers; `noteTransportWindow` on activation of a take / edit window), so a closed one falls back to the one before. `ToolWindow::keyPressed` forwards a plain Space that the content did not use from every non-transport window (forwardKeys windows forward everything, as before), also to a window that is down in the bar; `ToolWindowMain` (Main.cpp), the plug-in editor windows (PluginHost.cpp timer) and `TakeDisplayWindow` do the same. The Mastering and Trim windows and the FixTools windows consume Space themselves, so they never reach the forwarding. `AppContext::pollTransportKeys` still owns Space while recording (`spaceHeldAfterStop`).
- **Combo boxes that ate the arrows:** `refBox` (edit window) and `sendBox` (take window) `setWantsKeyboardFocus (false)` (JUCE's ComboBox nudges its selection with the arrow keys when it has the focus).
- **Main header (MainWindow.cpp):** logo + DRIVER INPUTS caption in a 148 px column; project / rate / depth on the top row and the disk box under them (`resized`); `headerSeps` (x positions) painted as cream + black hairlines; the status label is two lines (`s` alerts + "Type: device", `nums` buffer / io / 2nd drive / xruns / DSP) and `statusWidthFor` measures the widest line. Take Display (`TakeDisplay.cpp`): the project line is centred on the area and clamped to start after the logo plus padding.
- **Mixer window (MixerWindow.cpp / .h):** `roomOnScreen (win)` = display width minus the OS frame, and the height from the window's top to the display's bottom minus the frame; `updateWindowLimits` clamps the "full" size (`lastMaxW / lastMaxH`) to it - before, the full width was the strips' natural width, so the window grew off the screen, lost its horizontal bar, and a strip-width change (which called `fitWindowToStrips` and then saw the clamped width as "full" and re-expanded it) left the view at x = 0 with the METERS strip off screen. `setMixerLimits`: max width = the strips' whole width + slack (a drag may take the window past the display; `updateWindowLimits` keeps a hand-set width), height free; only the fitted / remembered sizes are clamped to the display. `fitWindowToStrips (remembered)`: Main.cpp `showWindow` passes the saved bounds for a mixer (place AND size now kept), clamped to the same room; a new window moves below the menu bar before the room is measured. The strip-width change no longer refits: `rebuild()`'s `updateWindowLimits` gives a full window the new width; the window keeps its old right edge and the view its `rightGap`. `MixerViewport::mouseWheelMove`: wheel -> x (deltaX or deltaY, ~400 px per unit, about a strip per notch), Shift -> y when the vertical bar shows. `rebuild()` walks the holder and `setScrollWheelEnabled (false)` on every `juce::Slider` (they then pass the wheel up to the viewport); the send panel's fader too.
- **Send MUTE:** `SendState::mute` (Flag). Engine: the send's target gain is 0 while muted (`st1`), ramped like a level change; it stays in the plan; the solo pass does not count a muted send as keeping its source open. Saved as `"gainDb": -100, "mute": true, "mutedGainDb": <level>` (an older build reads it as off, never as live); `sendsFromVar` takes `mutedGainDb` when muted; a send the incoming var lacks is switched off AND unmuted. `MasterRender`'s cache description drops `mutedGainDb` with `gainDb` for an automated send. UI: `SendMuteButton` in `SendPanel` (8 Hz timer for the channel's mute: `channelMuteOn` = the source strip's / Int Bus's own mute flag, not solo or VCA mute) - filled red = muted, red outline = Follow mute and the channel muted. `SendDial`: knob properties `pre` (MixerLook draws a yellow rounded square round the dial and shrinks the ring 5 px) and `muted` (grey ring); the value label says MUTE (own) or gets a red outline (by the channel). Test: in "sends, pre / post, send inserts, loops" (silences only the send, any pre / follow-mute setting, level kept; saved; copied).
- **Plug-in copy:** `SlotCell::isCopyDrag` = Alt or Shift (the copy path `dropTo (..., copy)` = description + `getStateBase64` -> factory, bypass copied).
- **Plug-ins window (PluginManager.h `PluginManagerPanel`, `showPluginManager`):** `PluginHost::listFiles()` groups `known.getTypes()` by `fileOrIdentifier` and adds JUCE's blacklist (crashed = blacklisted and not in `ignored`). `setIgnored (file, true)`: added to `ignored` (+ the names, for the list), to JUCE's blacklist (so `scanAndAddFile` never opens it) and its types removed; false: taken off both and the file scanned again. `tryAgain (file)`: off the blacklist, scanned again. Single-file scans: `onlyFiles` set on the message thread before `startScan`; `run()` builds the scanner with an empty search path and `setFilesOrIdentifiersToScan` (which also applies the dead-man's pedal). `scanning` is now cleared (and `onlyFiles` emptied) in the message-thread callback; `startScan` waits for a thread still on its way out and handles `startThread()` failing. Ignored plug-ins are refused only by the factory wrapper `AppContext::openProject` installs while a project is read (desc XML "file" attribute; the slot keeps its saved description / state, like a missing plug-in; "Plug-in not loaded: ..." in the log) and skipped by `buildPluginMenu`; `create()` itself does not refuse them, so a copy already loaded still bounces, pastes and Alt-copies. Matching is case-insensitive. The dead-man's pedal: applied at start-up (`applyBlacklistingsFromDeadMansPedal` + save, so a crash shows as CRASHED at once), plugins.xml written right after each scanner is built (a second crash keeps the first blacklisting), and `forgetCrashNote (file)` before `tryAgain` / un-ignore (the scanner's constructor would otherwise blacklist the file again before trying it). `startScan` returns false when nothing started. `ignored` lives in `plugins-ignored.txt` (file TAB names, one per line), re-applied to the blacklist at start. Not while scanning: JUCE's `addToBlacklist` takes no lock. The panel rebuilds its rows after a row's button with `callAsync` (the button's row is deleted by the rebuild).
- **Opening a .bak (AppContext::openBackupProject):** `filesync::projectFileForBackup (bak)` gives the project file ("Name.fermata.bak" and "Name (replaced <stamp>[ n]).fermata.bak" -> "Name.fermata"). Reads the .bak's text first, then `project.save (err)` of the open project (abort on failure; if it is that project its .bak is rewritten - the text is already in memory), `Project::backupWithCurrentWindows (bakText, currentText)` (the backup with the current `mixerFiles` / `takeWindowFiles` / `editFiles` / `mixers` / `takeWindows` / `edits`: those files are not put back, so a window created or renamed in the last save is neither lost nor "missing"), then `filesync::restoreBackupText (target, text, stamp, aside, err)`: copies the target aside as "Name (replaced <stamp>).fermata.bak" (a .bak, so it is not opened as a second project sharing the window files; a counter if taken), writes the text with `replaceTextSafely` (the target becomes the new .bak). `project.projectFile` is cleared before `openProject` so it does not save the old state over the restored file. The Open dialog's filter adds `*.fermata.bak;*.takedaw.bak`, confirms, then calls it; the window-file import accepts `*.fmedit.bak / *.fmtake.bak` (`importWindowFile` reads by content). Test: in "2.68.1 saving that survives being interrupted".
- **Lasso (TakeTimeline / EditTimeline):** Alt (no Ctrl) at mouse-down in the tracks area starts `lassoActive`; after 4 px `lassoMoved` and every drag recomputes the selection (`takesIn` / `piecesIn`: a take's banner or the rows of the tracks it has files on; a piece's or overdub's rows over its sample span) - a full repaint only when the selection changed, else the old + new rectangle. Mouse-up without a move: the 1 x 1 box (the take / piece under the mouse, or none). The edit window takes the lasso after the automation lanes (lanes on: a drag is automation, Alt fine steps unchanged) and does not move the cursor; one piece lassoed becomes `selectedRegion` too, several only `selectedRegions` (Bounce Out "selected", R butt), `moveRegion` cleared. Track-only marks: `markAlt` = Alt AND Ctrl; the Ctrl-click selection toggle / Ctrl-click whole-piece select are skipped when Alt is also held (`trackMark` also blocks moving an armed piece).
- **Modifier hints (Uikit.h `modhint`):** the timelines' 30 Hz timers compare `modhint::heldFor (*this)` (real-time modifiers, only while the window's peer has the focus and Fermata is in front) with `shownMods` and repaint the corner; `paintAll` draws `modhint::paint` in the name column above the first track (in the sticky header pass when scrolled; the edit window under the Markers / Feedback lines). `modifierHints (bits)` in each timeline lists what that mix does there (the edit window's depends on `lanesOn`) - KEEP THEM IN STEP WITH THE MOUSE / KEY HANDLERS. The take window's Shift + Left / Right compares `getKeyCode()` (`KeyPress == keyCode` is false while a modifier is held, so the nudge had never worked).
- Reviewed by a separate agent: fixed the solo pass counting a muted send, ignored plug-ins being refused inside `create()` (a loaded copy silently missing from bounces), Try again blacklisted again by the crash note, the Shift + arrow nudge, the .bak restore (current window / mixer lists, checked save, the kept copy as a .bak), the mixer wheel with no horizontal bar, the lasso taking the take being recorded, the edit window's hints with automation showing, Alt on a fade corner, the send panel reading a deleted mixer. Left: the mixer's height clamp follows the window's position only when it next lays out. (The user asked for it afterwards: a drag may make the mixer wider than the display; only opening / fitting is clamped.)

## 2.77.1: Pro Tools imports

`buildImportPlan` (`src/core/ImportPlan.cpp`) grouped mono / stereo files into takes by length first and then looked for the take word at one fixed word position from the left or the right. A Pro Tools session ("Ave Maria 8 AB 01.L", "Ave Maria 8 SPOT1 01") broke both: the files of a take differ in length, and the take number is word -2 in a split-stereo name but word -1 in a mono one, so no position fitted and every name kept its number (one track set per take). New: `numGroups` - the k-th (1 or 2) whole-number word from the end, a last L / R / Left / Right not counted, groups the files whatever their lengths; accepted when no take holds the same track twice and the takes share their tracks (all track names <= 1.5 x the biggest take); the k whose takes have equal-length files most often wins, a tie goes to the last number. It replaces the length grouping only when that grouping leaves the takes with tracks of their own (distinct keys > 1.5 x the biggest take), so every earlier import case is untouched (core tests). The words every name starts with (the piece) and a last word every name has before its L / R (Pro Tools' "-01") come off the track names when the names stay distinct and there are at least 4 of them. The Pyramix pattern ("<piece> <take>_<inst>") no longer takes a name whose "instrument" is only a number ("AB 01_01.L"). Test: "2.77: a Pro Tools session" in the import section (split stereo, "ROOM 414", "SPOT10", ragged lengths, with and without "-01"). Simulation harness: `scratchpad/imp/sim.cpp` (six naming styles, 44 takes x 20 tracks).

**2.77.2 (the real names).** The session's names are "<piece> <track>.<take>_<clip>" ("Ave Maria 8 ORTF.32_31"): the length grouping worked, the take word (-2) was found, but word -1 (Pro Tools' clip number) also goes with the take and was not a take word only because two takes share clip number 31 - so it stayed in the key: 43 x 20 = 860 tracks. `numGroups` now tries the k-th word (1..6) with a digit in it from the start and from the end (L / R not counted) as the take word, and after taking it out removes a second whole-number word (1st or 2nd from the end) that is constant within every take and not constant over all takes; the candidates are ranked by takes with equal-length files, then by more takes. The common-prefix tidy also runs for every import now (>= 2 words, >= 4 tracks, no bare-number result). `scratchpad/imp/sim.cpp` has 16 naming styles incl. this one ("REAL"), the old planner reproduced the 860-track result exactly; core test style 2 is this naming.

**2.77.3 (Pyramix without a piece name).** "001_1-C" / "001_OMNI-L" with one file of one take of another length: the length grouping makes an extra cluster that shares its take word with another, `values.size() < clusters.size()`, so `haveTok` fails for the whole import and every key keeps its take number. `numGroups` now counts C as a side word (Pyramix's mono marker; it comes off the track names) and ranks the candidate groupings by coverage first (mean of take size / all track names, in 5 % steps): reading "001_1-C" from the end made 'takes' of mic numbers for the mono files and take numbers for the stereo ones, which passed the duplicate and sharing checks. The general tidy also drops a last word C when every name ends in C, L or R. `scratchpad/imp/sim2.cpp`: this naming, equal / ragged lengths, two takes of one length, one take's Room pair longer (the old planner: 13 takes, 144 tracks).

## 2.89: pitch pipe, click, spot-mic time alignment, take sheet, BWF metadata

**Tunings (core, pure, tested):** `src/core/Tuning.h` (namespace `tuning`). `Temperament` (Equal, Just, Pythagorean, QuarterCommaMeantone, WerckmeisterIII, Vallotti, KirnbergerIII) as cents above the key for each scale degree (`offsetAboveKey`: just = 5-limit ratios 1, 16/15, 9/8, 6/5, 5/4, 4/3, 45/32, 3/2, 8/5, 5/3, 16/9 (9/5 in a minor chord: `justRatio`), 15/8; Pythagorean = pure fifths from the key; meantone = a chain of quarter-comma fifths from three below the key to eight above (Eb .. G# on C); Werckmeister III, Vallotti and Kirnberger III as the usual cents tables, moved to the key); `offsetCents (t, key, pc)` = raw (pc) - raw (A), so A is always exactly the reference; `frequency (midi, refA, t, key)`; `chordFrequencies (notes, refA, t, key, chordTuning)` -> `Chord { freqs, root, triadFound }`: As the tuning, or Pure major / Pure minor (5-limit ratios on the root, which is found from a major / minor third plus a fifth in any inversion, else the lowest note; the root keeps the tuning's pitch). Names and one-line descriptions for the UI.

**Sounds (core):** `src/core/PitchPipe.h`. `PitchPipeVoices`: 48 slots (one per key of the keyboard, `slotFor (midi)`), each a phase into band-limited wavetables (`kBands` = 10, one per octave of fundamental from C1, `kTableSize` 4096, built in `prepare` - a free reed: every harmonic, the odd ones a little stronger, rolled off above about 4 kHz, spread phases, each table holding only the harmonics under 0.45 x the rate for the highest note it serves), a 45 ms swell, a little breath noise, a chiff as the reed speaks, a 150 ms release; `setSlot (slot, hz, on)` from the message thread (atomics), `render` on the audio thread. `ClickTrack`: `Sound` Click / Woodblock / Soft / LowPulse, hits rendered once per rate (`makeHit (sound, accent, rate)`: Click a 4 - 5 ms ping, Woodblock three decaying modes and a knock, Soft a rounded G3 (C4 accented) of four harmonics with a 22 ms swell and a breath of dark air at its front - the "f" of "fum" -, Low pulse a 70 Hz (90) thud; the accent is the higher one), mixed in sample-exactly at the beat positions (`samplesPerBeat` from bpm and the beat unit), `beatCount` / `beatInBar` for the lights. `AuxSounds` { pipe, click } with output lists (`setPipeOutputs` / `setClickOutputs`: driver outputs, 0-based) and `process (out, numOut, n)`: renders each once into a scratch buffer and adds it to its outputs. `AudioEngine::aux` is prepared with the engine and processed at the very end of the callback, after the mixers and the talkback, so it is never recorded, never metered as a track and never reaches a bounce (the Bouncer's engine has its own, silent). `AppContext::applyAux()` pushes `Project::pitchPipe` / `click` (PitchPipeDef / ClickDef, serialised as "pitchPipe" / "click") into it; it is called from `applyTalkback`, and opening / closing a project stops both.

**Windows:** `src/ui/PitchPipeWindow.*` (`PianoKeys`: three octaves from `lowest`, normal mode = `onPress` / `onRelease` with a minimum 0.4 s note (`pressedAt`, `pressGen` so a late off does not stop a key pressed again), chord mode = `onToggle`; `HoldButton` for Play; `PitchPipeComponent`: the reference combo (presets, or typed 380 - 500), tuning, key, Chord + chord tuning, Clear, octave buttons, `OutputPicker`, level, the read-out and the cents line) and `src/ui/ClickWindow.h` (`ClickComponent`: Start / Stop, tempo slider + Tap (3 - 8 taps, a pause of 2.5 s starts again), beats / unit, Accent, four sound buttons, `OutputPicker`, level, beat lights from `beatCount`). `src/ui/OutputPicker.h`: one line - Choose... (a CallOutBox with a scrolling list of every driver output with a tick box, and None; the list holds a SafePointer to the picker) and the chosen outputs written out (three and "n more" past four); a timer follows renamed / removed outputs. One line whatever the output count (MADI / RAVENNA: 64 and more). Menu bar: `MainComponent::pitchPipeButton`, `clickButton` (lit while the click runs) before Media; window keys "pitchpipe" / "click" (CornerStripHost, forwardKeys).

**Alignment model (core, pure, tested):** `src/core/TimeAlign.h` (namespace `timealign`). Roles (`TrackDef::alignRole`, saved as "alignRole"; `guessRole (name)`). `analyse (tracks, length, sr, Reader, progress, maxLagMs = 80, maxFrames = 360)`: Hann frames of N = the power of two >= max (8192, 4 x max lag), hop N / 2, at most 360 spread over the take. Pass 1: the energy of every *unit* (a spot / distance track, or each channel of a stereo one) and of the main tracks per frame; a unit uses its louder half of the frames, and a spot unit of those the half with the highest unit / main energy ratio (its own instrument standing out). Pass 2: per frame, the FFT of every main channel and of every used unit; cross-spectra summed per (unit, main channel); each main track's mono sum against the first main track on the same side (`sideClass`) for the in-line check. Results: `detail::gccCurve` (PHAT, 100 Hz - 8 kHz with cosine edges) and `peakIn (curve, lagMin, lagMax, maxLag)` (sharpness = peak over the RMS of the whole +/- maxLag correlation without the peak). A Spot is searched on its own side only (lag >= -0.5 ms: an instrument nearer the main pair than the spot peaks on the other side and can be the bigger peak - the "bleed" test); per side group (left / right / centre main channels) the peak most channels agree on within +/- 4 ms (by summed sharpness) wins and the others are searched again within +/- 4 ms of it; the track is called farther than the main pair only when, on most main channels, the whole correlation's peak is before -0.5 ms and at least twice the spot-side peak. `leftMs` / `rightMs` = sharpness-weighted mean lags of the left / right (centre counts for both) main channels; `delayForPan` weights them by the track's constant-power pan (stereo: the mean of its two channels'). Suggested pan: 12 dB per ms of left-right time difference plus the main channels' level difference (the summed cross-spectrum magnitude), through the constant-power law; a stereo pair gets one per channel (`suggestedPanR`, `suggestedStereo`). Distance: the arrival (all main channels), the spread of a pair's two channels, the flags. A pair with one dead channel is flagged. `MainCheck::drastic()` = more than 2 ms out or inverted.

**Per-piece sets:** `AlignResult` / `AlignTrack { ms, on, result }` / `AlignSet { tracks (track id -> AlignTrack), measuredOn, hasDelays(), measured(), find() }` in Project.h; `Project::alignSets` (owner -> set; the owner is a take window id or an edit id), `alignActive` (the set the mixers use now), `alignEnabled` (the A / B switch); serialised as "alignSets" (orphans skipped), "alignActive", "alignEnabled"; reset in `createDefaultDesign`. `alignOwnerFor (isEdit, id)`: a take window is its own owner; an edit is its own only once it has a measured set or delays, else the take window most of its regions' length comes from. `alignSamplesFor (track, sr)` (0 unless enabled and the active set has the track on). `alignSignature (owner)` for MasterRender's fingerprint.

**Engine:** `TrackPlan::alignSamples` (from `alignSamplesFor`, clamped to 0.25 s) -> `Node::alignDelay` (a `PathDelay`, carried over between plans like the other delays and cleared when playback starts), run in `processMixer` on the track's channels after the disk / live input and before `srcDelay`. It is NOT part of the latency compensation (the point is to make the spot later). `AppContext::setAlignOwner` (rebuild the plan + change message) is called before every playback start (playTimeline / playTake / playEdit / playRegionOriginal / playDisc), when recording starts, and every third watcher tick while idle with `currentAlignOwner()` (recording: the record target; playing: what plays; else the last take / edit window used), so live monitoring follows the piece in hand; while a disc plays through the mixers (`playDisc`), `discAlign` (each item's end on the disc and its owner) lets the watcher switch to the next piece's set once the playhead passes the end of the one before (in the pause). Bounces: `BounceItem::alignOwner` / `BounceSettings::alignOwner`; the Bouncer sets its shadow project's `alignActive` before its engine exists and switches it (re-planning) per item. MasterRender sets each item's owner from its edit and adds the signature to the render fingerprint.

**Align window:** `src/ui/AlignWindow.*` (key "align", opened by the Align buttons of the take and edit windows; an open one is pointed at the new source by `setSource`). A `Job` thread reads the take files (between marks 1 / 2, or `cursorTake`, or the latest take) or the edit's regions (the topmost piece at each point, ignoring fades: timing is all that matters) and runs `analyse`; `applyResults` replaces the source's own set (`ownOwner()`), keeping the main checks as messages on the main tracks. `shownSet()` = what the source plays with now; `editableSet()` copies it the first time something is changed by hand (so an edit measured in post starts from its piece's). Rows: name, role combo, result text, delay editor (ms), On, suggested pan + Use (a pair: both channels). Copy from another piece, Guess the roles, Alignment ON / OFF, Delays for the present pans, Use all suggested pans, Clear this piece's delays. The mixer strip's latency line shows "Δ x.xx ms" (`StripBase::alignText`).

**Take sheet:** `src/core/Pdf.h` (a minimal PDF 1.4 writer: the standard Helvetica fonts with their AFM widths, WinAnsi encoding, text / lines / rectangles, `fit` and `wrap`, an xref table; tested for a valid structure) and `src/core/TakeSheet.h` (`collect (project, onlyWindow)` -> sections of rows: number, name, time, length, bars, dud, room tone, markers within the take with their offsets, problems - lost audio, cut short, waiting for corrected audio; `csv` (CRLF, quoted where needed); `makePdf` - A4 portrait, a heading on every page, a section per piece with its tracks and a summary, a date line whenever the day changes, page numbers). `src/ui/TakeSheetExport.h`: `exportTakeSheet (app, windowId)` writes "<piece or project> - take sheet <date>.pdf / .csv" (UTF-8 with a BOM) into "Take Sheets" in the project folder. Take window: `takeSheetButton`; Project menu item 15 for every take window.

**BWF:** `AudioEngine::startRecording` writes bext (description "<window> / <take> / <track>" - re-linking reads it, so keep that form -, originator "Fermata", date / time, time reference = the first sample's time of day in samples (Record pressed minus the pre-roll, wrapped past midnight), originator reference "FERMATA" + take + HHMMSS, coding history) and RIFF INFO (title, product = project, comment, date, software) through JUCE's WavAudioFormat metadata. iXML is not written (JUCE's writer has no iXML chunk; replacing the recorder's proven writer for it was not worth the risk).

**Tests ("2.89 ..." sections):** the tunings (seven temperaments on a key, A at the reference, pure major / minor chords), the pipe's voice (exact pitch, the swell, released notes die away, a chord never clips), the click (sample-exact beats, the accent, four sounds), the engine adding both to their outputs only, after the mixers, the analysis (spaced main pair, a centre and a left spot, pan-aware delays, a hall pair, the flags, two main pairs in line / not, bleed from an instrument near the main pair, a stereo spot pair's two suggestions, what cannot be measured), per-piece sets in the engine and their save / load, BWF chunks read back and crash repair keeping them, a bounce with its piece's alignment, the PDF writer, the take sheet. Render hooks FERMATA_RENDER=pitchpipe (FERMATA_PP_CHORD=1: chord mode with C E G lit) / click / align (roles set, Analyse pressed). The demo project (scratchpad rend/mkproj.cpp) has a fourth take, a run-through made from a simulated hall - eight sources, the mics at their places with cardioid / omni patterns, a Schroeder reverb per mic - on which the window measures the organ, harp, choir and soloists' spots within 0.1 ms of the geometry.

## 3.12.1: the ALL line at the top

`TakeChartComponent::View`: ALL at 0 .. kAllH, the ruler kAllH .. kRowsTop (= kAllH + kRulerH), the takes from kRowsTop; the lit bands and the ruler's lines run from kRowsTop to the bottom; `rowAt` -2 for the ALL line, -1 the ruler. Take sheet: `allTop = y`, the ruler 2 pt under it, the takes 12 pt under that, a rule under the ALL line and under the last take, the key 12 pt under the takes. Core "3.12.1": in the PDF the ALL label is above the bar numbers, they above the first take, it above the key.

## 3.12.0: the no-take pinstripe

`drawNoTakeStripes (g, rect, colour, spacing 7, thickness 0.8)` in BarCoverage.h: clipped to the rectangle (a Path clip: the cells sit at fractional x), lines going up to the right at 45 degrees, placed at multiples of the spacing in the component's own coordinates so that cells and runs join into one pattern. Used on runs of no-take bars in the strip (after the cells, under the searched bar's frame and the playhead take's outline) and in the chart's ALL line, on each no-take bar of the lit "No take" bands (fainter, wider), on the No take button (its swatch, and the whole button when it is on: `CoverageButton::striped`) and on the list's swatch of a no-take run. TakeChart.h now includes BarCoverage.h.
PDF: `Document::beginClip (x, y, w, h)` / `endClip()` (q ... re W n ... Q; a pair never crosses a page) in Pdf.h; `takesheet::noTakeStripes (doc, x, y, w, h, colour, spacing 4.5, thickness 0.35)` places its lines by the page's coordinates; used on the no-take runs of the shading behind the takes (crimson at 45 %, spacing 6) and of the ALL line (crimson at 35 %, i.e. light lines on the full crimson), and on the key's "none" swatch.
**Tests.** Core "3.12.0": a chart with no-take bars has three clips (shading, ALL line, key), every clip ended; with every bar covered only the key's.

## 3.11.0: the coverage colours

The user's scale, in one place: `coverage::colourFor (takes)` in Takes.h (0 crimson 165/14/42, 1 bright red 255/45/35, 2 red-orange 255/112/28, 3 darkish yellow 204/158/18, 4 - 10 a straight line from very light green 200/240/194 to dark green 18/98/40, 10 or more dark; `coverage::isLight` for the text on it). Used by `BarCoverageStrip` (every bar cell; the numbers dark on light cells), `TakeChartComponent` (five buttons - 0, 1, 2, 3, 4+ - lit in their colour, 4+ in the colour of 7; the bands use each bar's own colour at 50 %; the ALL line; the list's swatches; the summary now counts three and four-or-more apart; the takes' lines blue 2f74b5 instead of teal) and the take sheet (`takesheet::coverageColour (takes, share)`: the shading behind the takes at 22 %, the ALL line full, the key "Takes per bar: none 1 2 3 4 7 10 or more", the list's no-take lines crimson and one-take lines bright red; `chartSummary` counts three and four-or-more apart; the takes' lines blue).

**Tests.** Core "3.11.0": the scale (crimson deeper than the bright red, red-orange, yellow, greens getting darker from 4 to 10, the same beyond, light / dark text), a printed chart with the key and the crimson in it, and the summary.

## 3.10.0: the take chart in the take sheet; the Guide picture

**Take sheet (`src/core/TakeSheet.h`).** `Section` gains `pieceBars`, `lastBar`, `counts`, `runs` (from `coverage::` in Takes.h, filled by `collect`). `chartCsv (sections)`: "Piece,From bar,To bar,Takes,Which takes" per run (takes joined with "; "), empty when no piece has bars. `chartSummary (sec)`: the on-screen chart's summary sentence. `makePdf`: after each piece's table, the chart - title + summary, a ruler (number step from the cell width, faint lines), the shading of runs with 0 (red) / 1 (amber) takes behind the take lines, one line per take in number order (label "Take N", a teal bar from IN to OUT with its bars written inside when it fits, duds grey, "no bars entered" for a take without), the ALL line drawn run by run (red for none, white-to-teal by count), a key, then "Bars with no take or only one:" (the runs with 0 or 1, the no-take ones in red; "Every bar has at least two takes." when there are none). Row height 10 pt, shrinking to 4 so the chart fits under the page heading when a piece has many takes; the chart starts a page of its own (with "(continued)") when it does not fit where it is; the list continues onto further pages. A piece with no bars: one italic line. `src/ui/TakeSheetExport.h` writes "<what> - take chart <date>.csv" (UTF-8 with BOM, checked write) next to the sheet when `chartCsv` is not empty, and names it in the result box.

**Guide picture.** `Icon::Guide` in Uikit.h (a figure, an arm raised to a pole with a red pennant, a yellow badge; drawn on the 24-unit grid as in the mock-up the user picked, idea 1 of 6). `EditWindowComponent::guideToggle` is an `IconButton` now (34 x 26 in the flow, was a 58-wide text button); its tooltip is the old one with "Visual guide (show / hide)" in front.

**Tests.** Core "3.10.0": the section's counts and summary, the chart CSV's rows (a run of two takes, one, none), the PDF's chart title, list lines and a dud's bar, "Every bar has at least two takes.", a piece with no bars (a line, no chart, no CSV), 90 takes over 400 bars (thinner lines, several pages, no filled box below the footer line).

## 3.9.0: the project's own sample rate

**Why.** Until now a project's rate was whatever the device happened to run at when the first take was recorded (3.8.0 then took the engine's rate from the takes). A project meant for 96 kHz could be started at 48 kHz without anyone noticing.

**Data.** `Project::sampleRate` (0 = not chosen), saved in the project file's root as "sampleRate" whenever `audioRate()` is known (so a pre-3.9.0 project gets its takes' rate written at its next save); reset by `createDefaultDesign` and read in `fromVar`; not an undo step. `audioRate()` = `sampleRate` when set, else the 3.8.0 vote of the takes and edits. `Project::kProjectRates` (44.1 / 48 / 96 / 192 k), `hasTakes()`.

**Engine.** `startRecording` also refuses when the project's rate is set and the engine runs at another without the converter (a device rate the converter cannot bridge); the converting case is refused as in 3.8.0. `AppContext::matchEngineRate` already follows `audioRate()`, so a new project runs at its chosen rate from the start.

**UI.** `src/ui/ProjectRateDialog.h`: `ProjectRateChooser` (four radio toggle buttons, none picked unless a preselect is given, OK greyed until one is; the device's present rate under them; Enter = OK when enabled, Esc / Cancel / the title bar = nothing; `onDone (rate or 0)` called once, from the message loop) and `chooseProjectRate (app, heading, text, preselect, okText, cancellable, chosen, around)` (a modal DialogWindow, always on top). `MainComponent::newProjectDialog`: the file chooser, then the rate box, then `AppContext::newProject (file, rate)` (Cancel: nothing made). `AppContext::askProjectRate (why, fromMenu)`: refused with a message once the project has takes; sets `sampleRate`, saves, `matchEngineRate`, `offerDeviceRate`. Asked (async, Later allowed) when a project with `audioRate() == 0` opens, and by `toggleRecord` before recording into one (no take is started). Project menu item 18 "Project sample rate: 96 kHz..." (no "..." once there are takes: the box then only says why it cannot change).
`AppContext::offerDeviceRate()` (async after newProject / openProject, and after a rate is chosen): nothing when the device matches; when the device's `getAvailableSampleRates()` lacks the rate, an info box once per session per device and rate ("plays through Fermata's rate converter ... cannot record on this device"); otherwise "Switch the device to 96 kHz?" -> `switchDeviceRate (rate)`: `setAudioDeviceSetup` with the new rate (the device restarts; the engine follows in `audioDeviceAboutToStart`), checked against the rate the device then reports; on success `applyAudioSettings` (channels, the setup remembered) and a notice; on failure an error naming what happened and that it plays through the converter.

**To watch for.** Switching a Merging (ASIO) device's rate from Fermata changes it the way MADPanel would; the question is always asked first. A project whose takes are at a rate other than its chosen one (an import, say) plays those takes as before: refused, with the project's rate in the message.

**Tests.** Core "3.9.0": the four rates; chosen rate saved and read back, winning over a take at 48 kHz; a pre-3.9.0 project (key removed) goes by its takes and writes the rate when saved again; an empty one has none; `createDefaultDesign` forgets it; Record refused with the project at 96 kHz and the engine at 48 kHz (no converter), not refused for the rate once the two agree or no rate is set.

## 3.8.0: live sample-rate conversion; the take chart

**Why.** A laptop's Realtek driver runs at 48 kHz only; a 96 kHz project was refused ("X is 96000 Hz but the audio device is running at 48000 Hz"). One device = one clock, so the converter bridges the whole device (Pyramix-style per-output conversion would need a second device).

**The converter (`src/core/RateBridge.h`, header-only).** `RateConverter`: a fixed rational ratio (`ratio()`: both rates whole Hz, reduced up/down, at most 2048 phases), polyphase windowed sinc with a Kaiser window: pass band to 0.86 of the lower rate's half, stop band from that half, 120 dB (beta 12.27), taps = the Kaiser estimate in lower-rate samples (112) scaled to the input rate, rounded up to a multiple of 8; each phase normalised to pass DC exactly; coefficients stored oldest-input-first so the inner loop is a contiguous dot product (four accumulators). State: per channel the last taps-1 inputs (linear buffer, memmove after each block) and a count of trailing digital-silence samples (silence in + silent memory = zeros out, no work); `acc` = the next output's place in 1/up input samples after the next block's start (0 <= acc < down). `inputFor (n)` = inputs needed for n more outputs; `outputsFor (n)`; `clearChannel`; an `active` mask skips channels entirely. Delay = (taps*up - 1) / (2*up) input samples (about 1.16 ms on a 48 kHz side).
`RateBridge`: per device block (done in pieces of at most the device block it was prepared for): (1) the device inputs -> project rate onto the end of an input queue (only the inputs `wantIn` says the engine uses: the plan's track inputs, the CR mic inputs, the LTC input; an input that comes back into use starts from silence); (2) K = `fromEngine.inputFor (piece - outCount)` engine samples are rendered from the front of the input queue; (3) converted onto the output queue; (4) the piece handed to the device. The queue never runs short with a prime of 1 sample (cumulative inputs ceil(T*P/D) against consumption floor((T-1)*P/D)+1); 2 are primed; a test counts shortfalls (0). Engine blocks: at most `engineBlock()` = ((devBlock*down - 1) / up) + 1 (960 for 480 at 96 -> 48: exactly 2x every block after the first).
Measured (bench `scratchpad/srcb/bench.cpp`, every pair of 44.1/48/88.2/96/176.4/192 k): pass-band error -123 to -142 dB, a 30 kHz tone into 96 -> 48 k at -136 dB, images at -140 dB, input-to-output through both converters an exact fixed delay (2.34 ms at 96 k on 48 k), stereo 96 -> 48 k 0.45% of one core.

**The engine.** `AudioEngine::setProjectRate` (AppContext: `Project::audioRate()` = the rate most takes and edits are at, 0 = none); `rateFor (deviceRate)` = the project's rate when set, different and convertible both ways, else the device's. `audioDeviceAboutToStart` -> `startForDevice (rate, block, ins, outs)`: with a different rate a `RateBridge` is made and the engine prepared at the project rate with `engineBlock()` (so `project.restoreSampleRate/restoreBlock` and every plug-in are the project's); `converting`, `deviceRateNow`, `converterMs` are atomics for the UI. The callback calls `deviceBlock`, which runs `process` directly or through the bridge (input meters then from the raw device inputs, `bridgedBlock` skips process()'s own). `prepare()` called from anywhere else (tests, offline renders) drops the bridge. The load meter's block length is at the device rate. `startRecording` refuses while converting (message names both rates and the fix); `startPlayback`'s rate refusal says "this project plays at" while converting.
`AppContext::matchEngineRate()` (from `afterProjectReplaced` and the 1 s load tick): sets the project rate; when `rateFor (device)` differs from the engine's rate and nothing records, plays or is being finished, removes and re-adds the engine as the device's callback (JUCE: stopped, then about-to-start -> prepared for the new rate). Says once when the converter comes on (notice + log). `converterNote()` for the main window's RATE box tooltip, the DEVICE lamp (level 1, CONVERTING) and the Audio settings window's amber line. `pasteMix` / `undoCopyMix` prepare plug-ins at the engine's rate and block, not the device's.

**The take chart.** `TakeWindowDef::pieceBars` (saved as "pieceBars" when > 0, in the undo hash). `coverage::` in Takes.h: `lastBar` (pieceBars, or further when a take's bars go beyond), `counts` (duds never count), `runs` (a new run wherever the set of takes changes; every bar in exactly one run), `describe`. `BarCoverageStrip` is always shown: red with "Right-click here..." until pieceBars is set, then bars 1..lastBar; BARS (the left 56 px) is a button (`onLabelClicked`), right-click anywhere `onAskBars` (an AlertWindow, Enter saves). `TakeChartComponent` (src/ui/TakeChart.h) is a child of the take window placed at the strip's bottom-left (`placeTakeChart`, also from resized; size kept in a static, set by its resizable corner, at least 520 x 300 where there is room); it never takes the keyboard except its own bars box, so the take window's keys work while it is down. Ctrl+B: `TakeTimeline::lastClickedTake` (set by any mouse-down on a take, the recording one included - it cannot be selected) -> `openBarsForClicked` -> `showBars`. Shift+B toggles the chart, Esc closes it.

**Tests.** Core "3.8.0 live sample-rate conversion": tones through five pairs (error < -110 dB, delay < 1.5 ms), aliasing of 30 kHz into 96 -> 48 k, odd pairs refused; the bridge with random device blocks (some larger than prepared) on three pairs - no shortfall, engine blocks within the limit, a loop input -> output exact at the reported delay, queues small; `Project::audioRate`; the engine with a 96 kHz take on a pretend 48 kHz device (`startForDevice` + `deviceBlock`): converting, 96 kHz / 960, plug-ins prepared so, recording refused with both rates named, the same level as a 96 kHz device to 0.05 dB and 1 kHz still 2000 crossings a second, the input meter of an unpatched input still read, a 48 kHz take refused with the project's rate, live monitoring through both converters at 2.34 ms and the pan law's gain, a plain prepare() drops the converter. Core "3.8.0 the take chart": counts, runs, wording, a dud not counted, a one-bar take, pieceBars shorter than a take, the undo hash and the save round trip.

## 3.7.0: take matching tuned on real takes

**The material.** MHP (one piece, 20 tracks at 96 kHz): T1 bars 1-14, T3 / T4 bars 1-5, T45 bars 1-5 with 2 false starts, T6 / T7 / T8 bars 6-10, T49 bars 101-104 with 3+ false starts, T50 bars 101-104, T51 bars 103-104 with a false start. Bars 1-5 and 6-10 (and 11-14) share speed, dynamics and some notes; the inner voices differ. Salve takes 16 and 17 (another piece, same session, 16 tracks): the "different music" test and a second same-piece pair. The benches are in the scratchpad (`mhpb/ev2.cpp` the old search with its limits as parameters, `ev3.cpp` feature experiments, `ev4.cpp` the product's own code, `tune.py` the tuning every 5 s); the regions of each bar range in each take were set by ear from loudness timelines, and passages of 2, 3, 5 and 10 s every second were searched for in every other take.

**What 3.5 / 3.6 did** (12 note classes, A440, maxCost 0.25, tempo 0.7 - 1.45): 10 s passages separated (threshold 0.15, no errors); 3 s passages about 54 % right decisions, 2 s worse; bars 1-2 found in bars 6 and 11 at 0.013 - 0.025 (0 = identical) - the 12 classes and 0.25 s smoothing hear melody, bass, rhythm and loudness, hardly the inner parts. Microphones (12-class): MAIN PRINTS best, AB / ORTF close, the rooms worse, the spots worst (each hears part of the choir; many right versions missed).

**The tuning.** `tune.py` (energy-weighted circular mean of the cents deviation of 100 Hz - 1.2 kHz, 5 s blocks): T1 +1, -26, -34, -39, -43, -39, -45, +45 (= -55), -46, +50 (= -50); the others 10 - 40 flat and drifting; Salve 16 / 17 around -40 to -50 all the way, read +50 in some blocks and -45 in others. Notes read against A440 sat on the edges of their bins: T1's bars 6-10 were almost never found.

**What was tried** (MAIN PRINTS, 3 s passages; "right" = right versions kept and wrong ones kept out at the best single threshold): 12 classes 54 %; 60 notes (MIDI 36 - 95) 73 %; + the tuning followed (circular mean over +-2.5 s, wrapped to +-0.5 semitone) 94 %; + context 8 s, 94 - 95 % with no wrong ones at all (the openings of bars 6 / 11 at 0.20 + against right ones at about 0.11). Unwrapping the tuning continuously from the take's start fixed T1 but slipped a semitone in the Salve's quiet gaps (silence: noise in the angle); reading it leaning flat (-0.75 .. +0.25 semitone: a choir sinks, it rarely goes sharp by more than a quarter) fixed both with no running total: Salve 16 in 17 at 0.08 - 0.12 everywhere, next candidate 0.47 +. Without context, short passages found silence in silence at cost 0 (the quiet before T1 against the end of Salve 17): silence is now no evidence.

**The product (`src/core/TakeMatch.h/.cpp`).** `Fingerprint::bins` = 60 (`kNotes`, `kLowNote` 36); `NoteBuilder` runs the 8 kHz stream twice (`analyseSamples`: the resampled samples twice; `analyseFiles`: the files read twice, nothing large kept): pass 1 the loudness and a tuning phasor per frame, `tuneUp` the circular mean over +-50 frames read in (-0.75, +0.25], held through silence; pass 2 each FFT bin's note against that frame's tuning (within 0.4 semitone of a note, 60 Hz - 4 kHz). Silence below `kSilenceDb` -72 dBFS of the mix (was -55: the MHP takes peak at -35 to -45 on a mic pair). Onset emphasis and 5-frame smoothing as before, for any number of bins. `rotated` moves notes linearly (what moves off the end is dropped), `bestRotation` -11 .. +11 (the 3.4.0 test's transposed take is now -3, was 9 in 12 classes). Save format 2 (bins written); a version 1 file is refused, so the service makes it again.
`findVersions (passage, take, maxCost = kVersionCost 0.15, innerFrom, innerTo)`: tempo 0.8 - 1.25; versions in one take may not overlap (10 % slack); a passage whose inner part is mostly silence finds nothing, and a place whose inner part is mostly silence is not a version; the start / end returned are the inner part's, read off the path. `placePart`: the part alone searched within 1 s of where the context put it, in a slice of the take - the context's path wanders inside (part placement by context vs by the part alone: median 0.10 s, 90 % 0.4 - 0.5 s, worst 0.95 s), and `refineJoin` only searches +-0.15 s. `kPossibleCost` 0.12 for the bare search (T45's false starts 0.08 - 0.12; at 0.15 too many look-alikes on 2 - 3 s parts: 43 / 16 wrong, at 0.12 20 / 3).
Through the product code with the files Fermata chooses for these tracks (AB + ORTF + MAIN PRINTS): with context, 93 - 94 % of right versions listed, none wrong; lowest wrong 0.162 (bars 101's opening in T45's bars 1-5: the bar filter leaves T45 out anyway); AB alone with Salve in: 95 - 96 %, none wrong, Salve never under 0.23. Without context (the possible list), L = 3: 78 % at 0.12 with 3 wrong.

**The service (`TakeMatchService::filesOf`).** Main tracks: `TrackDef::alignRole == 1`; else names with a word MAIN / MAINS / AB / ORTF / DECCA / TREE / XY / MS / NOS / OMNI / OMNIS / PAIR (or containing MAINPRINT, DECCATREE); else the first four. Never LTC. The source key is of these files, so a role change makes the fingerprint again.

**The window (`VersionsWindow.h`).** `makePassage`: the source take's own fingerprint (`fingerprintFor (piece.takeId)`, waited for while it is made), sliced to the marked part (`part`) and to 8 s round it (`passage`, `innerFrom` / `innerTo`); the take gone from the window: `analyseRange` of the piece's files, no context (said in the status line). Mostly silence: refused. `passageBars`: the part's bars spread evenly over its take's bar range; a take with bars entered whose range is more than 2 bars away is left out (counted in the status line). Each take: sure versions (context) placed by `placePart`, then possible ones (the part alone, `kPossibleCost`) where no sure one is (0.5 s slack), the part's own place dropped; rows sorted by time within the take, possible ones dimmed and labelled.

**To watch for.** 2 s parts are ambiguous on their own (the possible list); the 8 s context means a part within 4 s of a take's start or end has less context on that side (it is taken from the other side); the bar filter trusts the bars entered (2 bars of slack). J's confidence threshold (0.55) was not retuned: the new fingerprint's costs for right places are about 0.10, so it is not near.

## 3.6.0: consonants copied, kept and pasted

**The data (`src/core/ConsonantData.h`).** `ConsonantAudio`: per track (`trackIds`, `trackNames`) its channels (`tracks[track][channel][sample]`), the rate, and `lead` = samples of it before the start of the box it was copied from. `ConsonantDef`: a kept one - id, name, from ("003 - Requiem, 0:12.40"), rate, length, lead, time made, and per track `ConsonantTrack { trackId, trackName, file, numChannels }`. `Project::consonants` (saved in the root of the project file as "consonants"; each track object has "file" + "rel" + "channels", so `projectcopy` copies and remaps it and a moved project finds its own files; not in undo), `Project::consonantsFolder()` = `<project>/Consonants`.

**Copy (`consonants::copySpec`, `consonants::extract`, `src/core/Consonants.h`).** The window's ReTouch settings forced to Attenuate + consonant mode + LeftRight + strength 1, lost samples off (`lostKnown` with an empty list: a copy must not squeeze time). For each track, its channels over the box with `1 s + 2 x box` either side (the ReTouch measures 150 ms+ round the box), the fix applied, and the difference kept; trimmed to where any track has more than 1e-5 (with 2 ms more), `lead = s0 - first`. The ReTouch's STFT reaches a frame before the box, very quietly: on the made-up 't' the copy starts 26 ms before the box, 84 ms long. The window does it on a job thread (`RepairDialog::copyConsonant`): `RepairContext::readChannels (a, b)` reads every track's channels from the files (take: the take's files; edit: the area's parts, whoever owns each moment, every channel), the window's fixes so far are applied where they reach the stretch (a denoise is skipped: it needs each file's room tone and only touches the background), and the side of the main consonant is found on the picture as the ReTouch finds it (`consonantSides`, when the slider is at 50). *Copy and remove* then makes the removal as an ordinary fix with the same spec (full strength), so what is kept is exactly what was removed.

**Paste (`FixSpec::Kind::Paste`, `audioops::addClip`, `audioops::resolvePaste`).** A FixSpec carries the whole `ConsonantAudio` (`paste`) and the level (`pasteGainDb`); the caller sets `pasteChannels` - the track's own channels - before each call, as it sets the denoise profiles: `fixTakeFiles` takes the track of each source (`trackOf`), `RegionJob::trackOf` (filled by `prepRegionJob`) does it for an edit's pieces, the window's preview does it per picture row (one channel: `addClip` averages a stereo track's channels for the picture only). `addClip`: channel c gets clip channel min(c, n - 1), 2 ms raised-cosine fades at the clip's ends, anywhere from a negative start to past the end (only the overlap is added). A paste op is never clamped to the file / piece (`fixWholeFile`, `fixPiece`) and the edit window's prepare gives every piece the paste's UNclipped start in its own take samples, so a consonant pasted across an edit point is split between the two pieces at the right sample (the edit point's own crossfade then blends the two copies: with a linear one they add back to exactly one; an equal-power curve gives up to +3 dB in the middle of it). The paste must lie 40 ms inside the loaded area (clear of the joins where the corrected piece is crossfaded in).

**The window.** A fifth tab (`kTabConsonant`; the tabs are in two columns, so the panel is now three rows high: `kTabRows`). Row 1: Copy consonant, Copy and remove, a hint. Row 2: the list (`ComboBox` of `ConsonantDef::listText`, made again only when the project's list changes), Rename (an AlertWindow), Delete (confirm; deletes the files). Row 3: Level (-24..+12 dB, remembered), Nudge `<<` `<` readout `>` `>>`, and *Paste here [P]* in the action corner. Keys: C (the tab), P (paste), `,` `.` (1 ms), `<` `>` (5 ms). A paste is a `RepairOp` with `placed` (where it was first put); a nudge or a level change takes the last op off (`revert`) and runs it again (`startFix`, the old `preview` body), so Undo still takes it off in one go and the readout counts from `placed`. Nudges and a level change made while a paste is still being made add up (`pendingNudge`, `pendingLevel`) and follow in one go when it is done (`followPending`): the nudge buttons stay live during a paste's job (each paste redraws the whole picture, a second or two for eight tracks). Kept audio is cached in the window by id; `RepairContext::loadConsonant` reads it back (`consonants::load`), `keepConsonant` writes it (`consonants::save`: one float WAV per track, every channel) and adds it to the list, `project.changed()` (logged).

**Tests.** Core "3.6.0": a made-up vowel (200 Hz harmonics to 5 kHz) with a 40 ms high-passed noise 't' on a stereo track (left 1, right 0.6) and a mono distant track (0.3): the copy holds most of the t (0.4 - 1.3 of its energy, correlation > 0.8 with it: the vowel stayed behind), its channels and tracks in the right proportions, nothing copied from a box with no consonant; `addClip` placement, gain, fades, a negative start, mono averaging; save / load exact (float), the project round trip and a moved project; `fixTakeFiles` puts each track's own part on its own file at the right sample and nothing anywhere else, a track the consonant was not copied from is left alone; `fixPiece` of a piece that starts in the middle of the paste adds only its own half, at the right sample.

## 3.5.0: Find other versions

**The search (`takematch::findVersions`).** Subsequence dynamic time warping of the marked part's fingerprint (`analyseRange`: the piece's files over the take's sample range, a `Source` carrying each file's `fileStart`, so a repaired or corrected piece is read from where its short file starts) against a whole take's: the passage must be matched whole, its first frame free to sit on any frame of the take (row 0 costs the local distance alone). The steps keep the tempo within a factor of two: diagonal (weight 2), two take frames to one passage frame (3), two passage frames to one take frame (3). Without that constraint the first draft squeezed the passage onto a single chord of the take and called it a 0.3 match - a dozen spurious hits per query. The cost of ending on each take frame is the path's weighted sum over its weight; the dips (lowest within +-3 frames, at most `maxCost` 0.25) are the candidates, best first; each is backtracked to its start, kept when its stretch is 0.7 - 1.45 and it does not overlap an accepted one by more than half the passage. `Version { start, end, cost, stretch }`. A 6 s passage against a 100 s take: 2 ms.

**The joins (`takematch::refineJoin`).** Two performances never share a waveform, so sample correlation alone fails (it found the nearest cycle of a different note, 548 samples out). Two stages: the loudness envelope - a millisecond at a time, `log1p (1000 rms)`, mean removed - of 0.2 s of the marked part's audio around the join slid over the version's within +-0.15 s (a frame and a half of fingerprint), best normalised correlation wins; then the waveforms themselves over 20 ms within +-2 ms of that, taken only when they correlate above 0.5, so the crossfade joins like with like. The same track (microphone) of the two takes is compared when the other take has it. On the made-up piece: exact against the same audio, 145 samples (3 ms) between two performances with random phases and 20 ms attacks - real onsets are sharper.

**The ripple (`EditDef::rippleAfter`, `replaceRangeRippled`).** `rippleAfter (from, delta)` moves everything on the timeline from `from` on: overdubs, markers, feedback flags, the guide's start, marks 1 / 2, the playhead, automation points with no region (fixed in time); anchored points are left to `resolveAllAutomation`, which follows their audio. `insertRegion` and `insertRegionAt` call it; `replaceRangeRippled (r, from, to)` splits at `to` then `from`, drops what lies inside, moves everything from `to` by `r.length() - (to - from)`, puts `r` at `from`, default crossfades both sides, `hardenEnds`, `settleGapFades`, `clampAll`. The ReTimEr keeps its own plan (it ripples pieces by its own arithmetic) - not changed.

**A 3.4.0 bug found by the render check.** `analyseFiles` set `sourceKey = sourceKeyFor (used)` - the up-to-four files it mixed - while `TakeMatchService` compares a fingerprint against `sourceKeyFor (filesOf (take))`, every non-LTC file of the take. With eight files per take the two never agreed: `fingerprintFor` reset the entry and queued it again, the saved `.fpr` failed the same comparison on load, and the log filled with "analysed in 230 ms" lines for the same four takes until the program closed (the demo project showed 30 of them in a minute). Now the key is of all the files given. The core test used one file per take and could not see it; the 3.5.0 test section analyses a take of eight files and checks the key is the one the service computes.

**The window (`src/ui/VersionsWindow.h`, `AppContext::showVersions`, item 9 of the edit window's piece menu).** Opens only when marks 1 and 2 lie within one main-track piece (0.5 s - 2 min). The passage fingerprint is made at once (a few hundred milliseconds of disk reading); each take's fingerprint comes from `TakeMatchService::fingerprintFor` (queued when not ready: the row says "analysing", a timer re-searches every two seconds while any is pending). The marked part's own place in its own take is dropped (within 1 s of it). Audition: the ReTimEr pattern - a `Base` snapshot (regions, overdubs, lanes, marks, insert index, and now markers, feedback, guide, playhead too), `PreviewApplied` hold, `replaceRangeRippled`, `playEdit (from - roll, from + newLength + roll)`, taken out when playback stops (timer), on Stop, or on close. Use this one: `armFixUndo` restoring the snapshot, `project.changed()` (so Ctrl+Z also works), a notice saying how far the rest moved.

**Tests.** Core "3.5.0" section: insert at 4 s moves markers / flags / overdubs / guide / marks / playhead / fixed points after 4 s by 2 s and leaves what is before; replace 5 - 7 s by 3 s (A | B 4-5 | V 5-8 | B 8-9 | C 9-13, crossfades at both new joins, the rest +1 s); a same-length replacement moves nothing; a 1 s for 4 s replacement brings the rest 3 s earlier; the version search on the 3.4.0 piece with a messy take (false start bars 0-2, bars 0-19, 10-19 again, 20-29): bars 12-14 found twice, bars 1-2 twice (the false start and the real one), bars 25-26 once, and none in a take of other music; `analyseRange` through a file starting at 20 s; `refineJoin` exact on the same audio, within 240 samples between performances. The bench is `scratchpad/tm/vbench.cpp`. To tune on real takes: `maxCost` (0.25), the stretch limits, the join search width, and whether the envelope stage wants a longer window for sustained music.

## 3.4.0: the same place in another take, and the bar-coverage map

**The matching (`src/core/TakeMatch.h/.cpp`, `td::takematch`).** A take's FINGERPRINT: its files (up to four, all channels, mixed) brought down to 8 kHz (`SincResampler`, 16 zero crossings), a 4096-point Hann STFT every 400 samples (20 frames a second), each FFT bin between 80 Hz and 4 kHz within 0.4 semitone of a note assigned to that note's class, the twelve classes log-compressed (`log1p (20 v / sum)`) and scaled to length 1; a frame under -55 dBFS is "silence" and gets the flat vector, so silence matches silence. Then onset emphasis - the rise of each class against the previous half second, weighted 2, added before smoothing over 5 frames - because in a long reverb what sustains is smeared with what came before, while a note's start is still sharp. Loudness in dB is kept per frame. `analyseSamples` for one channel (the tests), `analyseFiles` for a take; `save` / `load` a small binary ("FMFP", version 1, with the files' names, sizes and times as the key that says whether it is still the right one).

The ALIGNMENT: open-begin / open-end dynamic time warping, coarse then fine. Coarse: both fingerprints averaged down so the longer has at most about 1500 frames (never finer than 2 a second), the whole of A against the whole of B (at most about 2.25 million cells, a few milliseconds); steps diagonal (weight 2), along A, along B (weight 1), cost = the weighted sum of local distances (1 - dot product of the chroma vectors), a path may start on either take's first frame and end on either's last; among the possible ends, the heaviest path whose cost per unit weight is within 1.3x + 0.01 of the best is taken (the best alone was sometimes a short stretch that happened to match very well, cutting the path short - the transposed take showed it). Fine: the full-rate frames of A along the coarse path's stretch, B tried within 2.5 s either side of where the coarse path put it (a band, `centre` per row), the same rules; the ends of the fine path are trimmed while a second of it costs more than 0.4 (one take going on after the other stopped). A take in another key is turned into A's key first (the rotation of B's average chroma that best matches A's, taken when clearly better than none). `locate` reads the time in B off the path (interpolated) and gives a confidence of 1 minus the mean local cost within 4 s; found when at least `kMinConfidence` (0.55) and inside the aligned stretch.

**What the made-up piece shows** (the core test's "3.4.0 take matching" section, and `scratchpad/tm/bench.cpp`): 60 bars of chords, a bass note and a four-note arpeggio each, 48 kHz. Against the straight take: 5 % slower, worst error 38 ms; from bar 16, 0; bars 10 - 40 only at a faster tempo, 38 ms (and bars it has not got are "not found", as they must be); rubato (+-12 %), 27 ms; up 3 semitones, 0 (rotation 9); a wet room (2 s tail, noise at -35 dB) against the dry take, 0.65 - 0.75 s late - the tail drags the match - and two wet takes of the same room against each other, 70 - 130 ms, which is the case a session is. Hiss against music: not found. The tolerance in the test is 0.25 s, 0.8 s for dry against wet. The questions only real takes answer: the confidence threshold (everything synthetic scores 0.95 +), how a choir in a church with spot mics fingerprints, and whether the onset weight suits sustained music. `FERMATA_TM_DEBUG=1` in the environment prints a line per DTW pass on stderr for that tuning.

**The service (`src/ui/TakeMatchService.h`, `AppContext::takeMatch`).** A low-priority thread with a queue of takes; a 3-second sweep queues every take the service does not know (so a take recorded, imported or relinked is found without anyone telling it); `takeFinished` (from `tickRecordMonitor`'s stop event) puts a take at the front; `recordingStarted` (from the start event) and the sweep cancel work under way while a take records, and the thread does not start a job while recording (the queue waits): the recording's disk writes come first. Fingerprints are kept in memory as `shared_ptr<const Fingerprint>` and in `<project folder>/Analysis/<take id>.fpr`; one loaded from there is used only when its key matches the files now. `alignmentFor (a, b)` makes the alignment on the message thread when asked (a few tens of milliseconds for ten-minute takes; up to about 150 ms for an hour) and keeps up to 400. `projectReplaced` (from `afterProjectReplaced`) clears everything. The sweep and all lookups read the project on the message thread; the thread only touches files.

**J / Shift + J (`TakeWindowComponent::jumpToSamePlace`).** The playhead's take (the live playhead while playing, as `transportNow`), its fingerprint (else a notice saying analysing / waiting / cannot), then the other takes in timeline order outwards, duds passed over: the first whose alignment places the point goes to `goTo` (which carries playback on from there); takes that do not reach the place are named in the notice. **The bar map (`src/ui/BarCoverage.h`, `BarCoverageStrip`):** drawn from `TakeGroup::barIn/barOut`, a count per bar of non-dud takes, the playhead's take outlined, `searchBar` marked; a click calls `TakeWindowComponent::goToBar`, which sets the bar search and estimates the bar's place in the take evenly over its bar range. It sits under the head only while `hasBars()`; `changeListenerCallback` re-lays out when that changes. The next step is `TakeGroup` bar positions derived through the alignments from one take whose bars were marked, which will replace the even spread and fill the map for takes with no bars entered.

## 3.3.0: Export AAF

**Shape of the file.** Modelled on the Pyramix AAF the person supplied (`AAF_Living_Voices.aaf`, read with pyaaf2 - an independent open-source AAF library, cloned into the sandbox for the purpose - to see exactly how it is built), because Pyramix's files are known to open in Pro Tools and Resolve: a CompositionMob (UsageCode Usage_TopLevel) with a 'TimeCode Track' slot and one TimelineMobSlot per track and channel (PhysicalTrackNumber 1..n) holding a Sequence of Filler / SourceClip / Transition (an OperationGroup of the Mono Audio Dissolve, CutPoint = where the join is inside the overlap; the clips either side also carry FadeOutLength / FadeInLength of the overlap, as Pyramix writes them, for readers that skip transitions); one MasterMob and one file SourceMob per audio file, named "<take> (<n>) <track>" (the form Fermata's own reader splits into take and track), the SourceMob with a WAVEDescriptor (Summary = a plain WAV header built from the file's length, channels and bits; ContainerFormat RIFFWAVE; a NetworkLocator with a file:// URL) and one slot per channel; and one "Generic Source Mob" - a 24-hour tape with a Timecode slot starting at 0 and a SoundDescriptor - that every file SourceMob's clip points at, its StartTime being the file's time of day in samples. That last part is how Pyramix carries source timecode and what Pro Tools and Resolve read it from. Timecode segments use DataDef_LegacyTimecode, as Pyramix does. Mob ids are SMPTE UMIDs with a random material number (pyaaf2's prefix). Fades from and to silence are plain FadeInLength / FadeOutLength (no transition from filler: simpler, and the reader side treats both the same).

**The dictionary.** An AAF carries definitions of every class and type it uses (its MetaDictionary) and a Dictionary of the data / container / operation definitions the mobs refer to. The meta dictionary is generated: `tools/gen-aaf-model.py <pyaaf2 src>` walks pyaaf2's transcription of the SMPTE object model from a seed list of the classes the export writes and emits `src/core/AafModel.h` - the closure (61 classes, 101 types: the Pyramix file has 64 and 101, the differences being its VaryingValue / ControlPoint for the dissolve's level curve, which the export does not write, and PCMDescriptor on our side). Properties without a fixed pid (SMPTE extensions, dynamic ids) are left out. The Root class and its two types stay in the table for the writer's use but are not written into the file (Pyramix leaves them out too). The Dictionary gets DataDef_Sound, DataDef_LegacyTimecode, ContainerDef_RIFFWAVE, ContainerDef_AAF and the Mono Audio Dissolve OperationDefinition.

**The writer (`src/core/AafWrite.h/.cpp`, `td::aafwrite`).** Input is a `Document` (files, tracks of clips in timeline order - consecutive clips may overlap, which is the crossfade - the frame rate, the timeline's start timecode). Three layers: `Builder` makes objects and sets properties by name (the model supplies pid and stored form: 0x82 data, 0x22 strong reference, 0x32 / 0x3a strong vector / set with an "<name> index" stream and children "<name>{i}", 0x02 weak reference (tag into the root's "referenced properties" table, key pid, key), 0x12 weak vector); `buildMetaDictionary` writes the model; `Cfb` lays out the compound file (version 4, 4096-byte sectors, 64-byte mini sectors for streams under 4096 bytes, a balanced directory tree ordered the compound-file way - shorter names first, then upper-case comparison - the FAT appended last and sized to cover itself, DIFAT in the header only, so files over about 440 MB are refused rather than written wrong; the header carries the AAF 4K signature, the root entry the Root class id). The names of storages are mangled as pyaaf2 does ("Properties-9", "Containe-nitions-2608"): "<name>-<pid hex>", squeezed from the middle to fit 31 characters. Checked against pyaaf2 (opens the file, decodes every mob, slot, segment, descriptor and the dictionary), against olefile (an independent compound-file parser: no issues beyond the AAF signature in the header, which it flags on Pyramix's file too) and against Fermata's own `readAaf`, which reads the pieces, crossfades, files and track names back exactly (the test).

**The export (`src/core/AafExport.h/.cpp`, `td::aafexport`).** `planFor` (message thread) turns an `EditDef` into the document: the timeline's origin is `audibleStart()` (as the reader counts), one lane per project track and channel in the project's order (the LTC track left out unless asked for), the overdub layer on lanes of its own ("<track> OD" - its pieces overlap the main layer's, and a sequence is linear), each file once with the take sample of its first sample located in the take's LTC file (`fileStart` of both files). A clip is a region's fade-in start to its fade-out end; `cutPoint = -inStart` samples. `exportAaf` (worker) reads every file's header (length, channels, bits; notes a rate that differs from the edit's and a non-WAV file), decodes the code at its first sample with `videoexport::timecodeAt` (the +-2 s window clipped at the file's start), takes the majority frame rate, and writes. UI: `src/ui/AafExportWindow.h` (file, start timecode "01:00:00:00" parsed hours:minutes:seconds:frames, overdub tick; the folder remembered as `aafExportFolder|<project file>`; the same BounceJob-style thread as the video export), the `aafButton` in `EditWindow` through `AppContext::showAafExport`, `Icon::AafExport`. No new key.

**Small fix to 3.2.0's lamps.** The render harness showed the take window's DEVICE lamp cut to "NO DEV...": in a marks row the three `RecordLamps` boxes are 22 px high, and name plus word did not fit one line. `RecordLamps::paint` now draws two lines when the box is at least 19 px high (the name at 7.5 px, the word fitted under it), and `lampWidth` is at least 72 px. Checked in the harness (`scratchpad/rend/lamps.png`).

**Not verified here, by nature:** opening the file in Pro Tools, Resolve or Pyramix - the test log asks for it. If one of them refuses the file, the first things to compare with the Pyramix sample are the Identification (Pro Tools is known to look at CompanyName / ProductName), the WAVEDescriptor's Summary, and whether a Filler-to-clip fade-in should be a Transition as Pyramix writes it.

## 3.2.0: the recording monitor and Export for video edit

**Recording monitor.** The state lives in `AppContext::recordMonitor` (`RecordMonitor` in `AppContext.h`) and is kept up to date by `AppContext::tickRecordMonitor`, called first from the watcher timer: it resets the record at the start of a take (found through `engine.currentTakeId()`), logs an event (clock time, text, good/bad) for every overrun, write failure, device stop / silence / return, consumes `engine.takeBackupProblems()` and `engine.takeDeviceNote()` (both moved here from `MainWindow.cpp` - they are destructive reads, so they can only live in one place; the notices and the device alert are still raised from here) and writes a stop event with the take's length. The three lamps are `mainDriveLamp()` / `secondDriveLamp()` / `deviceLamp()`: a level (-1 grey, 0 green, 1 amber, 2 red), a word for the box and a detail line for the tooltip and the window. The second drive's "FINISHING / lag" comes from a new `BackupCopier::worstLagSeconds()` (the worst `(file length - copied) / bytes per second` over the open copy jobs, an atomic the copy thread updates). UI: `src/ui/RecordMonitor.h` - `RecordLamps` (a component of three boxes at 5 Hz, repainting only when a level or word changes; `idealWidth (height)` for the layout; click opens the window) and `RecordMonitorComponent` (lamps big, three lines, the take line with elapsed time, a read-only monospaced event list, Copy events). The take window adds `RecordLamps` to the right end of its marks row (`TakeWindow::resized`); `Main.cpp` opens the window with `showWindow ("recmon", ...)`, `setAlwaysOnTop` and a `StayOnTop` timer like the meter bridge; the Project menu item is 17. No new key.

**Export for video edit.** Core in `src/core/VideoExport.h/.cpp` (`td::videoexport`), UI in `src/ui/VideoExportWindow.h` (`VideoExportComponent`, `showVideoExport`), the button in `EditWindow` (`videoButton`, `Icon::VideoExport`) through `AppContext::showVideoExport` set in `Main.cpp`. The pieces of work:
- `gateLtcRuns (pieces, gapSamples)`: the person's rule. Pieces in timeline order as `PieceEdge { fadeInStart, end }`; a run goes from a piece's fade-in start to `min (its end, next fade-in start - gap)`, the last to its end. A piece whose next fade-in starts within the gap of its own would make a run of no length: it is dropped and the run before it goes on to the gap before the piece after it (iterated, so a string of short pieces is dropped together). Tested with the four-piece case (a 0.1 s piece C between B and D) and the degenerate ones.
- `LtcGenerator::startExact (formatCode, seconds)` (`Ltc.cpp`): starts the generator inside a frame, at the exact bit and half-bit for a fractional time, so a run's first sample carries the code that belongs to that sample (the old `start` began at a frame boundary). Round-tripped by the test through `LtcDecoder` + `analyseLtc` at 36001.0, 36009.9, 3600.123456 and 82800.25 s (a start a frame before midnight counts as a "jump" in the analysis, so it is not a test case).
- `timecodeAt (formats, file, channel, fileSample)`: decodes the +-2 s around the sample (not extrapolated from the file's start, so a box whose clock drifts against the recorder is read right at that moment) and returns the time at that sample, the frame rate and drop flag.
- `planFor (project, edit, settings)` (message thread) walks the edit's pieces (`segmentsForEdit`, the overdub layer too unless turned off), finds each one's take and its LTC track's file (`TrackDef::isLtc`; `noLtcTrack` when the project has none) and the file sample of the piece's fade-in start (`srcIn + samples (inStart) - fileStart`), and the export range (`audibleStart` .. `audibleEnd`). `mixSettingsFor` builds the `BounceSettings` the Bounce window would: the edit's own mixer (`project.mixerOfEdit`) or the first, its main Ext bus (`BusState::isMain`) or the first Ext bus, one item for the whole range, the edit's automation.
- `exportForVideo (plan, bouncer, settings, formats, progress, cancel)` (worker thread; the `Bouncer` is made on the message thread by the window, as `BounceJob` does, because the plug-ins are re-created there): decodes every piece's code, takes the majority frame rate, gates the runs, renders the mix (and checks its length is the export's), then writes `<name> - LTC.wav` (the generator restarted with `startExact` at every run, the same code on both channels, silence elsewhere) and `<name> - check.wav` (L = half the sum of the mix's channels, R = the code), all 24-bit WAV through `audioops::makeWavWriter` with `checkWrittenLength`. The result carries the counts for the message: pieces, runs, pieces without code (silence there, named in the notes), very short pieces dropped.
- The window: folder (remembered per project as `videoExportFolder|<project file>`, default `<bounced folder>/For video`), name (the edit's), gap (remembered as `videoExportGap`, 0 - 5 s), an overdub-layer tick, Export / Cancel, a progress bar, the result in the window and in the log, Show files. It refuses to start while a take records.
- Tests: `tests/core_test.cpp`, section "3.2.0 export for video edit": the gating cases, the `startExact` round trips, `timecodeAt` on a generated file, and a whole export of a project with a music track and an LTC track and an edit of four pieces (A 1 - 6 s at 0, B 10 - 15 s at 5 with a 0.1 s fade-in, C 7 - 7.1 s at 10, D 16 - 19 s at 10.1): three runs, one dropped, all three files 13.1 s, silence in the LTC file at [4.65, 4.9) and [9.85, 10.1), the codes read back at 36001.0, 36009.9 and 36016.0, and the check file's channels as specified. Things the test had to get right that a caller must too: `EditDef::sampleRate` must be set when pieces are pushed in directly (the plan reads it); the edit's own mixer exists after `syncEditMixers()`; `juce::Uuid()` is a random id, never null - use `juce::Uuid::null()`.

## 3.1.7: the code sweep, area F - clean-up (`docs/CODE-SWEEP-BRIEF.md`)

No behaviour change in this build, as the brief asks. Checked by the core tests, the stress and long-session tests, the Windows-code check and a syntax check of every UI source.

**Dead code.** A pass over every function defined in `src` and referenced nowhere else in `src`, `tests` or `tools` (a line-based scan, then each one checked by hand: JUCE overrides and the tests' own hooks - `setRunningForTests`, `setMixerThreads`, `renderNow`... - were left). Taken out: `LtcDecoder::bitLength`; `PitchPipe`'s `pipeToTalkback` / `clickToTalkback` (the 3.1.0 feature kept its state in the flags the engine reads; these accessors were never called); `AafPiece::fileCount`; `Spectrogram::frameTime`; `retime::Plan::framesToMake`; `BackgroundSaver::lastWriteMs`; `StallWatch::stallCount` / `longestMs`; `LoudnessMeter::truePeakLinear`; `SpectralRepair::rmsRange`; `ReHarmoniserEditor::hasPending` / `notePitchHz`; in the UI `navyKey`, `paintGiltRule` and `themeName` (the Manuscript look's leftovers), `DockBar::wantedHeight`, the mixer's `getNaturalHeight` and `slotLoaded`, the spectral window's `panFreq`, and the stream windows' `lockForPair` - which uncovered that `pairLocked` was never set, so its two "lock the window while the other stream is written" branches could never run: gone with it. `scanFileForLtc` (`Ltc.h`) has no caller in `src` but the tests use it: kept as test support.

**Duplicated plumbing.** `RetimerWindow.cpp` had its own copies of the fix tools' message box (`say`), the dialog launch (`juce::DialogWindow::LaunchOptions` with the same settings as `launchDialog`'s plain branch) and the save hold (`Hold`, the same thing as `PreviewApplied`). `say`, `launchDialog` and `PreviewApplied` are now in `FixTools.h` (`fixtools::`) and the ReTimEr uses them (`PreviewApplied` gained a constructor that takes the `AppContext`; the ReTimEr's `hold.set (b)` / `hold.get()` became `hold = b` / `hold`). The ReTimEr's `deleteTried` (files that could not be deleted because the edit window still reads them - Windows - are tried again every half second for half a minute) has no twin in `FixTools.cpp`, which deletes a tried file once and leaves it if that fails: making the fix tools retry too would be a behaviour change, so it is left for a later build (a small one: the 2.77 note's "`uniqueFile` keeps handing out higher numbers for preview names" is this). Also: the statement that GCC's `-Wmisleading-indentation` flagged in `findLostTogether` is on its own line now.

**Not done: splitting `FixTools.cpp` (6,200 lines) into a file per tool.** The map, so it can be done later. The file's sections are: helpers (24 - 174: `say`, `uiPrefs`, `gMakeProcessPage` / `gProcessPage`, `withPitchTabs`, `launchDialog`); OUT STREAM / IN STREAM (175 - 447: `StreamPair`, `StreamSel`, `JobDialogBase`, `launchStream`, `detectStreams`, `makePair`, `closeDialogOf`, `formats`); the pitch dialog; the pitch-curve dialog; the repair dialog (971 - 2455: `RepairDialog`, `SpectrogramView`, `SpectroSettings`, `PictureRequest`, `RepairOp`, `gSpecPrefs`); the imported tracks' order and the import dialog; applying a fix to the model (3026 - 3350: `takeFixDest`, `setUndoForTake` / `setUndoForEdit`, `prepRegionJob`, `runRegionJob`, `TrackParts` / `buildTrackParts` / `readTrackMix`, `learnFileProfiles`, `attachRoomTone`, `runRepairJobs`, `findLostTogether`, `prepareRepairJobs`, `ReaderCache`); take window: pitch / repair (`takeWin`, `markedTake`, `takeSel`); edit window: pitch / repair (`editOf`, `openRepair`); Noise mode; Re-HarmoniSer and the pitch curve; copy the project; Export for Processing (`SimpleJobDialog`); the room tone; Import AAF. Every tool section uses the helpers, the stream classes and `closeDialogOf`; the take and edit entry points use the three dialog classes; Noise mode uses the repair dialog's spectrogram classes and the fix-applying helpers; the room tone uses the take and edit helpers. A split therefore needs an internal header (`FixToolsInternal.h`, say) that declares the helpers and defines the dialog classes, with the four file-scope statics made non-static - a mechanical move of a few thousand lines with nothing to show for it until a Windows build and a run of every tool prove it. This run was unattended (the person could not test anything for a couple of days), so it was not the moment: do it in a session where the person can build and run the tools the same day.

## 3.1.6: the code sweep, area E - snappiness (`docs/CODE-SWEEP-BRIEF.md`)

Measured before anything was changed, as the brief asks, on two projects: `scratchpad/savebench` (the brief's size: 14 take windows of 26 - 27 takes x 40 tracks, two edits of 400 pieces x 40 files, 5 mixers, a room tone - the 2.102 bench) and the render harness's Big Session (`scratchpad/rend`, `/tmp/fermata_big`: 51 take windows, 3000 takes of 20 stereo tracks with 1200 real 8 s files, 50 edits of 40 pieces), the latter also loaded into `scratchpad/savebench/bigbench` for the core-only timings. Core timings at -O1 (the sandbox build), the harness at -O2 with the Xvfb software renderer; on a 2-core machine, so take them as proportions.

**Found and fixed: the undo and save snapshots' string hash.** `undoTick` (every change, 500 ms after it settles) and `prepareSave` (every automatic save, on the message thread) compare every window with its last snapshot by `undoHash` / `saveHash`. `UndoHasher::add (const String&)` used `hashCode64()` + `length()`: two walks of the text, each decoding UTF-8 character by character - and a big project hashes 47,000 file entries of four strings each (`trackName`, the path, `sourceName`, `sourceName2`), so the file names were most of the hashing. Now the string's stored bytes (UTF-8) are folded eight at a time in one pass (multiply and fold; the byte count mixed in). Measured: the brief's project - `undoHash` of every window 33 -> 9 ms, `saveHash` 25 -> 9 ms, the undo step after one edit changed 44 -> 14 ms, after one take changed 26 -> 10 ms, `prepareSave` 32 -> 16 ms (nothing changed) and 52 -> 17 ms (one edit changed); the 3000-take project - `undoHash` 29 -> 16 ms, `saveHash` 25 -> 16, the undo tick 25 -> 17, `prepareSave` 39 -> 33 - 37 ms (there the hashing is a smaller share: see below), the "file hashes" stage of opening 39 -> 26 ms, and the harness's "Automatic save: the snapshot took N ms" line (logged over 60 ms) no longer appears. The 2.69 tests (`expectW` / `expectE`: every field that must change the hash does) pass unchanged; the window-file hashes are kept in memory only (`refreshWindowFileHashes` at open), so nothing on the disk depends on the function.

**Measured and left as it is**
- *`prepareSave` on the 3000-take project, 33 - 37 ms*: `toVar (false)` 8 - 10 ms (55 mixers - one per edit - of 20 strips, serialised so the edits' files can carry their mixer and be hashed with it), the window hashes 16 ms, and `planWindowFiles` with two `exists()` calls per window (204 stats: "never overwrite a file that is not this window's" and "the file is still there") - the stats are the safety, and the whole runs once per idle automatic save (30 s), held off around Play / Stop by `noteTransport`. The writing has been off the message thread since 2.102.
- *Change handlers*: `project.changed()` + every open window's handler 0.4 - 1.0 ms on the Big Session (take window, edit window or mixer open). *Opening*: 0.9 - 1.0 s (102 window files read in parallel 400 - 560 ms, set-up 160 - 210 ms, undo copies 160 - 190 ms). *`undoReset`* (every window copied at open) 33 - 39 ms. *`segmentsForEdit`* on 400 pieces x 40 files: 7 ms, 16,000 segments; the opener's file checks run on the playback thread (2.102).
- *Redraws (software renderer)*: a whole take window 21 - 24 ms (20 tracks: the "takes" part 14 - 20 ms, of which 96 thumbnail waveforms 8 - 13 ms), zoomed out x8 17 - 27 ms, small rows 13 - 16 ms; a whole edit window 16 - 24 ms ("pieces" 13 - 14 ms), zoomed in 6 - 7 ms; the mixer 26 - 32 ms; the main window 22 - 40 ms (the 40 driver-input rows 1.2 - 2.9 ms each). The one slow case: a take window zoomed in x8, 96 - 110 ms, "waveforms 100 ms (8 calls)" - 12 ms per track. `scratchpad/wavebench` (`DirectWaveCache::draw` on an off-screen image, 1470 px wide, 2 channels) shows where: the min / max scan of the samples is 0.02 - 0.6 ms at any zoom (spp 0.1 to 200), `fillEnvelope`'s shape (a path of 4 segments per pixel column, filled, plus the outline rectangles) 10 - 14 ms for two channels in the software renderer, and the whole draw 3 - 37 ms depending only on how jagged the shape is. That is raster work: on Windows JUCE 8 draws with Direct2D, where a filled path costs the CPU a fraction of this (the 2.70 note: "Windows with Direct2D will be lower for the fills, the same for the CPU-side work"), so it was not changed - a change for the test renderer's sake would be a change the person cannot see.
- *The lead "text handled a character at a time"*: every remaining `String::operator[]` loop in `src` walks a short string (a name, a time read-out, a Ravenna path, a CD-TEXT field); the long texts (comments import, iXML, the Stream Deck lines) go through `textFromBytes` and `getCharPointer()` walks since 3.0.0. Nothing of size left.

**Noted, not changed:** `juce::Logger::writeToLog` for every preamp change sent to a Merging device (`Ravenna.cpp`) opens the log file each time (JUCE's `FileLogger`), at dial speed tens of times a second - well under a millisecond each, so a few per cent of the message thread while a dial turns, and the line is the record of what was sent; `TakeSheetExport` builds its CSV by appending to one `String` (quadratic over thousands of takes, an export-time cost in the tens of milliseconds).

## 3.1.5: the code sweep, area D - the hardware-facing code (`docs/CODE-SWEEP-BRIEF.md`)

Read across (none of it can be run here: reasoning about the code, with tests where the core can be tested): the audio device starting, stopping and changing under the engine (`audioDeviceAboutToStart` / `audioDeviceStopped` / `prepare`, `syncWithDevice`, `Project::syncWithDevice`, the device settings window), sample-rate changes, channel counts of 64 and more, the Merging device (`Ravenna.cpp`: the WebSocket client, `handleSettings` / `applyPath` / `rebuildChannelList`, the ANEMAN fetch), the MCU parser and the surface controller (`Mcu.h`, `Surface.cpp`), the control port (`RemoteProtocol.h`, `RemoteHub.h`, `RemoteControl.cpp`, `RemoteDesk.h`), the preamp remote and its memory (`Preamp.h`, `PreampMemory.h`), the LTC reader. Fixes-only build (PATCH). Two agents surveyed independently (the device side; the protocols); every item below was checked against the code before it was changed. The fuzz test (`tools/tool-checks.sh <JUCE> fuzz`) already covered `mcu::decode`, `remote::parseCommand`, `PreampMemory::fromVar`, the Ravenna parsing helpers and the LTC decoder for crashes; what follows is what fuzzing does not find - hangs, growth, wrong values that are valid numbers.

**Found and fixed**
- **A device stopping and starting again under a take.** `audioDeviceStopped` only cleared `running`; `audioDeviceAboutToStart` called `prepare()`, which stops playback but knew nothing of a `RecordingSession`. So a take went on across a device restart (the driver's control panel, a clock change, the interface unplugged and back, or the Audio settings window, whose `AudioDeviceSelectorComponent` applies a change the moment it is made and was not disabled while recording): at the same rate the sound while the device was down was simply missing and nothing said so (the take clock stood still; `recordingProblem()` knows only the writers); at a new rate the writers, made at Record with the old rate, took the new rate's audio into files whose headers said the old one - the rest of the take unplayable at the right speed. Now `audioDeviceAboutToStart` with a session alive calls `deviceRestartedWhileRecording` (on the message thread, where JUCE restarts a device; a `callAsync` otherwise): a changed rate ends the take there and then (`stopRecording` before `prepare`: `waitForAudioThread` returns at once because `running` is false, the finisher writes the headers for what the files hold), a same-rate restart lets it go on; either way `deviceNote` says what happened (`takeDeviceNote`: the main window shows it as a notice and a message box, and logs it). While the device is stopped, or "running" but delivering no block (`AppContext::quietSeconds`, counted in `tickLoad` from the load meter's block count: a hung driver), the main window's status line flashes AUDIO DEVICE STOPPED / NOT DELIVERING in the style of the disk alarms, and the take window's status says the same. The Audio settings panel disables its chooser and Apply while a take records (a 500 ms timer). `stopRecording`'s stalled-audio-thread branch, which leaked the session on purpose but left no trace, now sets the note and logs. Test: "3.1.5 the audio device stops and starts again during a take" (a fake `juce::AudioIODevice`: a same-rate restart keeps the take, 200 blocks long, with the note; a restart at 96 kHz ends a 48 kHz take with the file at exactly the recorded length and 48 kHz in its header; the next take is at 96 kHz).
- **MIDI to the surface from its own thread (`SurfaceController::Sender`).** `send()` called `MidiOutput::sendMessageNow` on the message thread: two LCD SysEx per 30 Hz tick while levels move, `blank()` at close. JUCE's Windows output waits for the driver to take a SysEx with no time limit (`while (! MHDR_DONE) Sleep (1)`) and up to 50 ms per short message; a virtual port whose owner has hung (EuControl closing) would have held the whole program. Now the message thread queues (capped at 4096) and a thread writes; the `MidiOutput` is a `shared_ptr` the thread holds while writing, so a port let go of under it is deleted only when the write is over; the destructor flushes the blanking for up to 1.5 s and the thread is stopped with `stopThreadOrLog` (2 s: only a driver hung inside a write keeps it, at the program's close).
- **The control port's numbers.** `(double) v["delta"]` of the JSON string "nan" is a real NaN (JUCE's string-to-number reads nan and inf), and `jlimit` returns NaN unchanged (no comparison with it is true): `faderStep` / `gainStep` / `stageStep` then put NaN into a mixer's main fader or an input's preamp gain (`Param::set` has no guard). `(int) v["index"]` of a number beyond int's range is undefined. Now `parseCommand` clamps as doubles and treats a non-finite number as nothing (delta 0, index 0, input -1); `PreampMemory::fromVar` does the same for a damaged memory file (gain 0..66, lowCutHz 0..1000, input >= 0). `monitorCycle` with no input (-1) matched every track that has no input (`inputOf == -1`) and cycled them all: refused now. Tests in the "remote control: the Talk key" section; the fuzz test checks that every delta, index, input and memory gain that comes through is finite and in range.
- **A flood on the control port.** `handleLine` posted one `callAsync` per line; the hub reads 4 KB per pass (~256 commands). A program sending faster than the message thread drains fills Windows' posted-message queue (10,000), after which `PostMessage` fails and JUCE drops messages - its timers and async updates among them. Now the commands go into a capped vector (256) drained in order by one `AsyncUpdater`, the surplus dropped with one log line.
- **Merging devices (`Ravenna.cpp`).** The brief's two leads, confirmed: (a) `handleSettings` skipped `rebuildChannelList` for a path containing "channels", and `applyPath` replaces a channel list whole when its size differs, so a module that gained or lost channels left `refs` stale (channels missing or reading as nothing) until the next full settings message - now the module's channel count is compared with the entries the list has for it after every applied path, and the list (and the status text) is rebuilt when they differ; (b) `WebSocketClient::receive` capped one frame at 64 MB but appended continuation frames to `message` without a total - the same 64 MB now bounds the whole message; and a ping or pong between the pieces of a fragmented message cleared `inMessage`, so the pieces so far were dropped and the rest read as a new message (a failed JSON parse: harmless, now right). The ANEMAN fetch read its reply with `readEntireStreamAsString` (the UTF-8 rule: `textFromBytes` now, with a 16 MB cap).
- **Smaller.** `syncWithDevice` returned on a failed `setAudioDeviceSetup` without `project.syncWithDevice`, so the project kept the previous device's channel names and count (no restart was coming to put it right): it falls through now. Playback stopped by `prepare()` (a device restart) bypassed `AppContext::stopPlayback`, leaving `playInfo` and the disc's alignment list set until the next transport press: the 100 ms watcher now tidies when the engine is not playing but `playInfo` says it is.

**Checked and found sound (no change)**
- *The audio thread's indexing by channel*: every read of an input or write of an output is checked against the block's counts and `kMaxInputs` (256); a project channel map pointing past a smaller device gives silence; `outScratch` / `outPtrs` sized for the device; no 32- or 64-bit channel bitmasks (`juce::BigInteger` is unbounded) - 128-channel Ravenna / MADI fits; blocks larger than prepared for are handled.
- *Buffers on a restart*: `prepare()` remakes `zeros`, the talkback tap, the main capture, the aux sounds, the LTC reader and generator, the plug-ins (`reprepare`), the low cuts and the plan; the pre-roll ring is rebuilt when not recording.
- *Sample rates*: `startPlayback` refuses a mismatched take or edit with a message; `PlaybackSession::open` refuses a mismatched segment; takes are written with the engine's rate at Record; `restoreSampleRate` / `restoreBlock` follow the device.
- *`Mcu.h`*: every index from the wire range-checked (pitch-wheel channel, touch notes, dial CCs, buttons); SysEx needs 5 bytes, the challenge reply 16; unknown ids are `Other`; V-Pot deltas bounded. *`Surface.cpp`*: strip indices limited before the `sent` / `touched` arrays; LCD lines exactly 56 characters; the MIDI-in queue capped at 4096, the monitor at 400 lines; a port that vanishes is dropped and looked for again every ~2 s.
- *`RemoteHub`*: a line without a newline is bounded (64 KB), the outbox at 4 MB; non-blocking sockets with 10 ms waits (`stopThread (4000)` never kills); the accept loop sleeps 200 ms on error; a port in use fails `start()` with a log line. *`RemoteControl`*: every index from the wire guarded; windows close through `closeFromKey` with the modal and recording guards; `armtoggle` and `playpause` refuse while recording; `record` toggles the take as the key does; destruction order (hub first) right.
- *The preamp remote*: gains clamped 0..660 and the low cut to 40/60/80 before sending; sends queued to the device's thread; a damaged memory file that fails to parse is ignored and rewritten. *ANEMAN*: both callers on their own threads, `this` touched only on the message thread behind an `alive` flag or a `SafePointer`; 1.5 s timeouts. *Ravenna sockets*: every call has a timeout; a fixed 4 s pause between reconnects; every `juce::var` numeric index checked; threads stopped on a helper thread with the `Link` mutex against a destroyed driver.
- *LTC*: the live reader allocates nothing on the audio thread; no division by zero (`lenEma`, `rate`, `fps` guards); a signal that stops clears `present` after 0.5 s; a rate change rebuilds the decoder.
- *`CoreReservation` on the callback*: try-lock only, one system call per generation. *`Main.cpp`*: the callback is added after the project opens and removed in `shutdown`.

**Noted, not changed:** the control port that cannot be opened (another Fermata running, usually) is logged, not shown - a message at every start for people without a Stream Deck would be noise; the ANEMAN fetch thread is not joined at exit (the process ends it; area A's class of detached threads); a device that reports "started" but never delivers a block when not recording is only visible in the load meter (recording shows it on the status line); `scanFileForLtc` has no caller in `src` (a dead-code candidate for area F); `juce::Logger::writeToLog` per preamp change (`Ravenna.cpp`) opens the log file for every dial step - for area E.

## 3.1.4: the code sweep, area C - error paths and undo (`docs/CODE-SWEEP-BRIEF.md`)

Read across: every place a result is dropped or an error only logged (`saveNow` and its callers, `newProject` / `openProject` / `closeProject` / Save and quit / `shutdown`, `Project::load`, the plug-in factory and `InsertSlot`, the mixer's insert menu and drag-move, the import, the fix completions, the plug-in lists, the device setup, the control port), and the undo model end to end (`undoHash` / `saveHash` / `undoText`, `undoTick`, `restorePiece`, `restoreWindow`, the fix tools' own "Undo fix", every `markDirty` against every `changed`). Fixes-only build (PATCH). Two agents surveyed independently (one the error paths, one undo); every item below was checked against the code before it was changed.

**Found and fixed - undo**
- **A take was an undo step.** Record added the take to the window and marked the window changed, so the next Ctrl+Z after a recording took the take out of the window (its files stayed on the disk, in no take) and set `nextNumber` back, so the next take got the same number as the one just recorded. Now (`AudioEngine::startRecording`) what the person changed before Record is closed as a step of its own first (`undoTick (false, true)`), the take is added, and the window as it now stands becomes the state the history goes on from (`Project::undoAbsorb`): Ctrl+Z after a recording undoes the edit before it and leaves the take and its counter alone. The recorder's own fields (`lengthSamples`, `overruns`, `lostSamples`, `writeFailed`, `verified`) are out of the undo hash and the undo text (`kTakeMachineKeys`) and in the save hash instead: a take ending, or `repairInterruptedRecordings` marking a take verified, is saved but is not a step. `restorePiece` never sets `nextNumber` back. Test: "3.1.4 a take is not an undo step".
- **"Undo fix" could take later work with it.** The fix tools' single-level undo (pitch correction, repair, denoise, ReTimEr, Export for Processing) put the edit's pieces (or the take's files) back as they were before the fix, whatever had been done to the edit since - edits made after the fix went with it, without a word. Now `AppContext::armFixUndo` remembers the hash of what the fix changed as it finished (`fixUndoHashNow`), `fixUndoStillValid` compares, and `undoLastFix` refuses when the edit (or that take) has changed since: "Ctrl+Z steps back through the changes, the <fix> among them". `afterProjectReplaced` drops it with the project.
- **Mastering edits were not undo steps.** The mastering window only marked the project dirty, so Ctrl+Z there undid whatever came before. `Project::markDirtyForUndo` marks a change the undo ticker looks at; the mastering window's `touched()` uses it. Test: the mastering part of "3.1.4 undo puts a take's files back...".
- **Undo did not put a take's files back.** `restorePiece` restored only `trackId` / `trackName` of files still in the take, so a track's clips taken out of a take window (`removeTrackClips`) or a fix that pointed a take at a new file could not be undone. Now the saved file list is put back whole when every file in it is still on the disk and its track still exists (the track's row is unhidden with it); when a file has gone (a relink after a move), the old behaviour stays - undo never points a take at a missing file. The take's `waiting` marks (Export for Processing), the window's `showLtc` row and `targetEdit` are restored too. Test: "3.1.4 undo puts a take's files back...".
- **Renaming a take left the overdub pieces' names.** `TakeWindow`'s rename updated `takeName` in the edits' `regions` but not their `overdubs`.

**Found and fixed - error paths**
- **"Save and quit" quit even when the save failed, and said nothing.** The message box was queued after `quit()`, so it was never shown, and no log line was written; the same for the final save in `shutdown`. `saveNow` now returns whether the project was written (or handed to the saver) and logs every save that fails; with an `errorOut` the reason is handed back instead of shown, for callers that say it in their own words. Save and quit (`Main.cpp`: `saveAndQuit`) keeps the program open when the save fails and asks: Stay, or Quit without saving (what was saved automatically before is untouched either way). `shutdown`'s save failure is in the log (no box can be shown while the program closes).
- **New / Open / Close went ahead over a project that could not be saved.** `newProject` and `openProject` saved the open project and replaced it whatever the result; "Save and close" closed it. Its unsaved changes were gone with it. Now all three refuse, as `openBackupProject` already did: "The project that is open could not be saved first, so nothing was changed" / "...so it stays open".
- **A failed Open could make the next save of the still-open project write every window file again under a "(2)" name.** `Project::load` cleared `windowFiles`, `missingWindowRefs`, `loadNotes` and `oldWindowFiles` as soon as the file read as JSON, before `fromVar` could find it is not a Fermata project; the open project then saved with another file's lists in place (its windows found their files "taken", took "(2)" names, and the other file's missing windows were listed as its own). Now the format is checked before anything is cleared, and the lists are kept and put back if the load fails after that; the error names the file ("Not a valid Fermata project file: X", or "The file does not exist: ..."). Test: "3.1.4 an Open that fails...".
- **A plug-in that could not be started went without a word.** `PluginHost::makeFactory` dropped the reason; the slot stayed empty-looking (it kept the plug-in with its settings for the next save, which is right) and the mix played without it. Now the factory logs each one with the reason and keeps it in `lastFactoryError`; `openProject` collects them and says once, after the windows are up, which plug-ins could not be started and that the mix plays without them for now; the slot shows "<name> (not loaded)" (`InsertSlot::savedName`), and its menu says so and offers "Remove it" or "Replace it with...". Ignored plug-ins (chosen in the plug-in manager) are not in that message, as before.
- **A bounce could leave a plug-in out.** The render copy (`cloneForRender`) re-creates every plug-in from its state; one that would not start in the copy was simply not there, and the bounce came out without it - a file that does not sound like the mix. `Project::pluginsMissingFrom` compares the live mixer with the copy (loaded and not bypassed there, not loaded here); `Bouncer` now refuses with the list ("Bypass or remove them to bounce without; the log says why they would not start").
- **The mixer dropped a plug-in when a slot would not take it.** In `insertOnLinked` (the same plug-in put on linked strips) a slot that could not be locked within 2 s dropped the new instance quietly; in `dropTo` (drag-move) the results of `set` were not looked at, so a refused move would have destroyed the plug-in being moved. `InsertSlot::setKeeping` hands a refused plug-in back to the caller; the move puts it back where it came from and says what happened; linked inserts count the stuck strips and say so.
- **An import into one window per piece that failed part-way left files and empty windows.** The take windows for the pieces were made before the copy, and the copies of the pieces already done stayed in their folders in no take (a second try copied them again as "(2)"). Now the job takes away the copies it made for the pieces before the one that failed (never a linked original), the completion removes the windows it made that are still empty, and the status says "Nothing was added; the files copied so far were taken away again". And if the window (or edit) an import was going into is deleted while the files copy, it is said, with the folder the copies are in (it used to end silently).
- **Smaller:** the plug-in host's two list files log a write that fails (settings: a scan again next time, or an ignored plug-in offered again); a driver whose channels cannot all be switched on (`syncWithDevice`) is said once per device, not only logged.

**Checked and found sound (no change)**
- *Undo*: `undoTick`'s hash-and-text model (a window is serialised only when its hash changed), the deferral while recording, `kUndoLevels`, the redo stack cleared by a new step, `restoreWindow` keeping view state (zoom, scroll, selection, playhead), windows removed from the project not undone (their files went with them), `afterProjectReplaced` resetting the history. Every `changed()` / `changedLive()` / `structureChanged()` reaches the ticker; the `markDirty()` calls that do not are view state and the mixers (below).
- *Saves*: the background saver's failure path (`savedInBackground` -> `saveFailed` -> written again; `retryFailedSave` when closing without saving), `showSaveError`'s two-minute throttle for the automatic save, `waitForSaves` before closing.
- *Error reporting*: the recording's problems (3.1.3), Create copy, Relink, the exports and DDP all report; `busy::` windows are closed on every return path (`Closer`).

**Noted, not changed:** the mixers, the tracks and buses (the design), the alignment sets and the layouts are not undoable (they never were; a pasted mix has its own one-step undo (`mixUndo`) - a mixer undo history is a feature, not a fix); a click that places an edit's playhead does not mark the project dirty (view state, saved with the next change); the control port that cannot be opened (another Fermata running, usually) is logged only - a message at every start for people without a Stream Deck would be noise; a plug-in's own crash inside its process call is the plug-in's (the 2 s slot lock and `stuck()` log are the program's protection).

## 3.1.3: the code sweep, area B - data safety (`docs/CODE-SWEEP-BRIEF.md`)

Read across: `FileSync.h` and the save path (`prepareSave` / `writeSave` / `tidyWindowFiles` / `saveFailed` / `load` / `mergeWindowFiles`), `WavRepair`, `TakeWriter`, `startRecording` / `stopRecording` / the `Finisher`, `BackupCopier`, `ProjectCopy`, `AudioRelink`, the import copies (`ImportPlan`), Export for Processing (`AudioOps`), `Bounce`, `MasterRender`, `MasterExport`, `Ddp`, `ReviewPage`, the small writers (`Pdf`, `TakeSheetExport`, `PreampMemory`, `PluginHost`'s lists), and how each failure reaches the person. Fixes-only build (PATCH). The foundations hold: the project and window files are written to a temporary file and swapped in whole with the save before kept as `.bak` and used when a file is unreadable; the recordings' headers are mended on reopening; the second drive copies every take as it records; Create copy reads every file back; the recording problems reach the status line, the take window and the take's CUT SHORT mark.

**Found and fixed**
- **A full drive could quietly ruin the project file.** `replaceTextSafely` wrote its temporary file with `juce::File::replaceWithText`, which does not look at the result of `appendText` at all (JUCE 8.0.x: `tempFile.getFile().appendText (...)` then `overwriteTargetFileWithTemporary()`), and a `FileOutputStream` writes its last buffer (up to 16 KB) in its destructor, where nothing can report a failure. On a full drive the project file was replaced by a short, unparseable file and the save reported success; the next "successful" save moved that file to `.bak` and replaced it with another short one - both copies of the project text gone (the audio untouched, and the window files the same way). Now `filesync::detail::writeChecked` (FileSync.h) opens the stream itself, checks every write, flushes and checks the stream's status, and compares the file's size on the disk with the bytes written; `writeTextChecked` (UTF-8, the same "\r\n" line breaks JUCE's writer made, so the files come out byte for byte as before) and `writeDataChecked` sit on it, and `replaceTextSafely` uses it for its temporary file, so a failed save keeps the old file and its `.bak` and reports. The same two helpers replaced `replaceWithText` / `replaceWithData` in `ProjectCopy` (the copy's project file), `PreampMemory::save`, `MasterRenders::save` (renders.json), `Pdf::save`, the take sheet's CSV, the DDP's DDPID / DDPMS / PQDESCR / CDTEXT / CHECKSUM.MD5 and the CUE sheet, the FLAC tag rewrite, and the comments copy. `PluginHost`'s two list files stay as they were (settings, remade at the next scan). Tests: "3.1.3 checked file writes" (bytes and line breaks exact; a refused write is false with the file named; `/dev/full`, where there is one). `syncToDisk` now goes through JUCE's stream (`FileOutputStream::flush` is `FlushFileBuffers` on Windows and `fsync` elsewhere), so FileSync.h no longer includes windows.h; `check-windows-code.sh` compiles it plainly.
- **Exports could come out short with no warning.** `Bounce` (both passes) and `MasterExport::finalise` ignored `DitherOut::write`'s result, the resampling step ignored `writeFromFloatArrays`', and nothing looked at the finished files; the import copies and Export for Processing checked their writes but not the file at the end (the closing header write is the destructor's). On a full drive a shorter file and "done". Now a refused write stops the export with a plain error, and `audioops::checkWrittenLength (fm, file, frames)` looks at every finished file: a WAV through `wavrepair::inspect` (header against bytes - a reader's `lengthInSamples` is the header's claim, so a file cut short after its header still "reads" whole), any other format by its reader's length and, for AIFF, its size against the audio. Used after every writer in `Bounce`, `MasterExport` (resampling, every format but MP3 - LAME's output - and the FLAC again after its tags), `ImportPlan::copyImportFiles`, `audioops::writeWav / exportRange / spliceFile / fixWholeFile`. A compressed file (FLAC) cut short by the stream's final flush keeps a header that says it is whole and its reader zero-fills the end, so `MasterExport::finalise` writes through `audioops::CheckedAudioFile`: the `FileOutputStream` is kept by Fermata and the writer gets a forwarding stream; once the writer is closed, `finish()` flushes, checks the stream's status and the file's size against the furthest byte written, then closes it (Windows cannot rename over or delete an open file: the FLAC tag rewrite and LAME's input follow). The checks pass for every format the exports write (WAV, AIFF, FLAC, Ogg: the test writes a second of each; Ogg's reader gives the exact count from its last page). `MasterExport` now treats a DDP fileset that fails `ddp::verifyFileset` as an error ("do not send it") rather than a line in the report; `ddp::writeFileset` / `writeWavAndCue` check their stream's status and the WAV's size. `addFlacTags` writes through a temporary and is checked; its failure fails the export (it was ignored).
- **The second drive's copy was thrown away when the main recording failed - the one time it is needed.** `BackupCopier::service` judged "behind" by the time elapsed since the take began (`elapsed x bytesPerSecond - copied`), so when the main file stopped growing (the main drive full or gone) the copy was binned 20 s later as "could not keep up"; and at Stop, when the finished header could not be re-read from a main file that had gone, the copy was binned as "the finished header could not be copied". Now (with the person's agreement, as the brief asked): lag is `source size - copied` (a copy that has everything the main file has is not behind; a slow second drive that really falls behind by `maxLagSeconds` of audio is still binned), and a main file that cannot be read back at the end leaves the copy as it stands, closed with a header made from its own size (`wavrepair::inspect (dst, true, true)`), with a notice "THE MAIN RECORDING COULD NOT BE READ BACK ... the second drive's copy was kept as it is, N min of audio". The bin notices now carry "; the main recording is not affected" themselves (MainWindow no longer appends it: it would have been wrong for the kept copy). `stopRecording`'s stalled-audio-thread branch now calls `BackupCopier::finish` for the session's copies (they used to stay open for ever). Test: "3.1.3 the second drive's copy is kept..." (a source that stops growing is not binned after the lag has long passed; a source whose contents vanish before Stop leaves a whole, readable copy of 24000 frames and the notice; a drive really behind is still binned; a cancelled job goes quietly).
- **A take that failed to start part-way left files behind.** `startRecording` returned "Cannot create ..." from inside its loop over the armed tracks; the `RecordingSession` it dropped closed the writers already made (header-only files in Recorded Media, not in the project), their `BackupCopier` jobs then copied the headers and were binned 20 s later as "could not keep up" (a notice and a log line for a take that never existed), and the next take's files got "(2)" names. Now `giveUp` closes and deletes the files made in that call (just made, empty, unknown to the project) and cancels their copies (`BackupCopier::cancel`: the copy is removed without a notice). Test: "3.1.3 a take whose later track's file cannot be made..." (a 300-character track name: no .wav left, no copy, no notice, and the next take is number 1 with two files).

**What happens when the recording drive fails during a take (the brief's question):** the take is not lost and does not stop silently. A write the drive refuses sets the writer's `failed()` flag; `recordingProblem()` is polled by the main window (the status line flashes "RECORDING HAS STOPPED REACHING THE DISK", a notice, a log line) and the take window; the file is left with a good header for what reached the disk (JUCE's writer writes the header when a write fails) and the take is marked `writeFailed` (CUT SHORT in red). A drive that stalls is covered by the 8 s buffer (`kRecordBufferSeconds`), beyond which the lost time is written as silence in its place and shown as DISK OVERRUN. The second-drive copy is now kept (above). Not changed: the automatic save is held while recording (markers made during a long take are lost if the program dies before Stop - a 2.68.1 decision), and `LivePeaks` keeps three hours of overview per take (`kMaxSeconds`): a longer take records on, the overview stops drawing.

**The long-session test** (`tests/long_session_test.cpp`; `tools/tool-checks.sh <JUCE> longsession`, 2 simulated hours, about 3 minutes per simulated hour; `sandbox-check.sh` runs 0.75 h). A pretend audio thread is time itself (48 kHz / 512; flat out between takes, held to ~20x real time while recording so the disk writer keeps up); the "message thread" runs what the program runs alongside: the take window's 20 Hz polling (meters, the live overview, the readouts, `reapFinishedTakes`), MainWindow's once-a-second `takeLoadStats` and `takeBackupProblems`, `undoTick` every 500 ms, the automatic-save policy (dirty, idle, 30 s) through a `BackgroundSaver`, the save at Record and Stop, a marker or two per take, a playback of the last take between takes (stopped as a transport press: the Retirer frees it), the second drive on. Finished takes' files are deleted (the sandbox has no room for hours of 24-bit audio). First the session as it really runs, then a steady state (the window keeps only its newest take) in which any growth is a leak. Checks: every take's length is what the audio thread delivered between Record and Stop (to within the two blocks those fall in), every file holds exactly the take's length, no save failed, no file failed, nothing waits to be freed, no copy still going, the undo depth within `kUndoLevels`, the files list no longer than the windows, and the steady state's heap (glibc `mallinfo2`, first quarter to last quarter, medians) grows under 4 MB - a leak of 16 bytes a block would be 5 MB an hour. Result at 2 h: 40 takes, 105 saves, 13,374 undo ticks, 115,663 polls; heap 5.7 -> 5.8 MB, steady-state growth 0.02 MB; no drift. Nothing to fix.

**Checked and found sound (no change)**
- *Saves*: order (window files, project file, tidy), `saveFailed`, `planWindowFiles` never taking another window's path, `readJsonWithBackup` setting the damaged file aside, `settleBackgroundSaves` before `save` / `load` / `forgetWindowFile`, the whole-project fallback when a window file fails. A rename that OneDrive's client blocks (it holds a just-written file) is retried five times by JUCE's `TemporaryFile`, then reported, with the old file put back.
- *Crash recovery*: `WavRepair` never touches audio bytes; `extendToFileEnd` only for Fermata's own takes that were recording; the RF64 cases; `repairInterruptedRecordings`'s `verified` skip.
- *Create copy*: every file read back and compared by CRC; the copy's project file now checked too. *Relink*: reads only. *The fix tools*: delete only the files they made.
- *DDP*: `verifyFileset` checks every size against the map and the MD5 of every file, now as an error. *Review pages*: checked status. *Import*: writes checked, now read back.
- *Paths*: JUCE's wide-character calls take non-ASCII names; a path over 260 characters fails at Record with "Cannot create <file>" (Windows refuses it unless long paths are enabled; the program has no long-path manifest) - reported, nothing lost. *Full disk at Record*: refused under 500 MB free; the DISK box and DISK NEARLY FULL warn. *A drive gone between takes*: "Cannot create folder".

**Noted, not changed:** `tidyWindowFiles` moves removed windows' files to `_removed`, which only grows (a window renamed many times leaves a file each time); the plug-in host's two list files use the unchecked `replaceWithText` (settings, remade by the next scan); a crash in the middle of an export leaves a half file with a 0-length header that players refuse (visible, not misleading); `saveCommentsCopy` empties the edit's comments folder before copying the imported files in again, so a drive that fills up half way loses that copy (the comments themselves are in the edit; the copy is a convenience).

## 3.1.2: the code sweep, area A - threads and lifetimes (`docs/CODE-SWEEP-BRIEF.md`)

Read across: `AudioEngine` (plan swap, `Retirer`, `Finisher`, `RingBuilder`, the record path), `Playback` (reader and ring), `MixerWorkers`, `TakeWriter`, `SaveWriter`, `BackupCopier`, `RemoteHub` / `RemoteControl`, `Ravenna`, `CoreReservation`, `StallWatch`, `AudioJob` and every window that runs one, `WaveDraw`'s read-ahead, `AlignWindow`'s job, the detached threads, the 40 SafePointer places and the 42 `this`-capturing asynchronous callbacks from the 3.0.0 notes. Fixes-only build (PATCH).

**Found and fixed**
- **`AudioJob::start()` killed a running job.** It called `stopThread (5000)` before starting the new work; but a job's work loop looks only at `cancelled()` (the `cancel` flag), never at `threadShouldExit()`, so a job that was still running never stopped in those 5 s and was killed by force every time - in the middle of writing its files, or inside the allocator (on Windows a thread killed while it holds the heap lock hangs every later allocation: the program freezes for good). Reachable: in the ReHarmoniSer, `renderAndSet` / `toggleOriginal` check `busy` (the analysis) but not `hostBusy`, and the Isolate toggle is never disabled, so Isolate (or Original) pressed twice while the corrected audio is still being made called `setCorrected` -> `self->start()` again. Every other `runJob` caller is guarded by `running`. Now `start()` sets `cancel`, waits (`waitForThreadToExit`, up to 30 s; a wait over 2 s is logged) and never kills; if the old job really has not stopped after 30 s the new one is not started and its onDone gets an error text instead. A per-job `generation` (a shared counter captured by the posted onDone) drops the replaced job's onDone, so the window only hears about the job it asked for last: without that, the old job's "Cancelled." would have hidden the progress bar and cleared `running` under the new job. core_test "3.1.2 AudioJob" has a job that ignores the thread's stop request; start() during it must cancel it, wait for it, and deliver only the new onDone (it fails on 3.1.1: the old job is killed after 5 s). The test pumps JUCE's message queue through `juce::detail::dispatchNextMessageOnSystemQueue` (declared in the test: the public loop needs `JUCE_MODAL_LOOPS_PERMITTED`).
- **Short wait-then-kill timeouts on threads that touch the disk.** `PlaybackSession::~PlaybackSession` gave its reader 3 s, `WaveDraw`'s read-ahead `Reader::stop` 5 s, `BackupCopier` and `AlignWindow`'s `Job` 4 s. An external drive that has spun down takes several seconds to answer a read, a network share longer, and a thread killed inside a read risks the hang above. Now `stopThreadOrLog` (Common.h) with 30 s for the playback reader (it runs on the Retirer thread: nothing visible waits) and 20 s for the others, and a log line naming the thread if one ever has to be killed (JUCE's own "!! killing thread by force !!" does not say which). The engine's destructor, which already waits up to 60 s for the take finisher, then gives the finisher and the disk writer 20 s each instead of 5 and 3 (a kill mid-flush would cost a take's last moments). Left as they were, with the reason: `SaveWriter` waits for ever (a save handed over is always finished); `Ravenna` uses -1; `RemoteHub`, `StallWatch`, `MixerWorkers`, `Fence` and `RingBuilder` wait on events or flags and stop at once; `PluginHost`'s scan thread (5 s, at exit) may be inside a plug-in that hangs, and killing it at exit is the right thing; `Bounce` / `MasterRender` / `MasterExport` (10 - 15 s) check the exit flag per block.
- **ReHarmoniSer Space while recording.** `ReHarmoniserEditor::keyPressed` started playback on Space with no `spaceIsForRecording()` check (the editor is a standalone module and cannot see Uikit.h). `EditorHost::spaceIsTaken` (a hook, set by `openReharmoniser` to `spaceIsForRecording`) is asked first. No key changes: the HotKeys list already says Space stops a recording from every window.
- **Drop-downs taking the keyboard.** Mastering set `setWantsKeyboardFocus (false)` on its direct Button children only; its ComboBoxes, and buttons inside the panels, took the focus on a click, so the arrows changed the drop-down instead of zooming. The ReHarmoniSer's buttons and sliders had it, its four ComboBoxes did not (Enter then opened the drop-down instead of applying the correction). `keysStayWithWindow (root)` (Uikit.h) walks every descendant and switches it off for Buttons and ComboBoxes (TextEditors and Sliders keep it: they need the keys); Mastering calls it at the end of its constructor; the editor does its four boxes itself.
- **One capture.** The project-copy dialog's "open the copy" callAsync captured `this` where it only reads `app`; it now captures the AppContext reference.

**Proof the audio path never waits: `tools/tool-checks.sh <JUCE> rtcheck`.** Clang's RealtimeSanitizer needs Clang 20 (the sandbox has 18), so `tests/realtime_guard.cpp` does the same by hand: it defines malloc / free / calloc / realloc / memalign / posix_memalign (forwarding to glibc's `__libc_*`) and interposes the locks, waits, sleeps and file / socket calls (`dlsym (RTLD_NEXT)`), and the link line wraps `td::AudioEngine::process` (`-Wl,--wrap=` on its mangled name) so a thread-local flag says when a thread is on the audio path. Each such call made while the flag is up is reported once per place (a hash of the stack), with `backtrace_symbols_fd`; the script turns the offsets into source lines with addr2line. `juce::WaitableEvent::signal` and `juce::Thread::notify` are wrapped too and allowed: that is how the audio thread wakes the disk writer and the mixer helpers (SetEvent on Windows, where it is not a lock; a mutex and a condition variable on Linux). Before the real tests the mode runs `tests/realtime_guard_control.cpp`, a pretend engine that allocates, frees, locks and sleeps (compiled twice: `--wrap` only catches calls that cross between objects), and checks all four kinds were reported - a clean run would otherwise prove nothing. Result on core_test and stress_test (5 s, two mixer helpers): **0 reports**. The helpers run the same job function as the audio thread, for other mixer indices, so the job code is covered through the audio thread's share. Not covered: plug-in processing (no plug-ins in the core tests) and the device callback itself (`audioDeviceIOCallbackWithContext` calls `process` inside AudioEngine.cpp, which `--wrap` does not see; the tests call `process` directly).

**Checked and found sound (no change)**
- *The 40 SafePointer-after-a-call places* (list in the 3.0.0 notes). Every window deletion is a posted message: `closeWindowLater` / `closeAllWindows` / the dead list in `refreshWindows` all `callAsync` the erase, and `JUCE_MODAL_LOOPS_PERMITTED=0` means no call in the program runs a nested message loop - so no call made from inside a callback can delete the window before the next `safe->`. The one exception is plug-in instantiation (a VST3 may pump messages while loading), already guarded where plug-ins load (MixerWindow, 2.98.0); a licence dialog shown by a plug-in during project load remains a residual risk, noted.
- *The 42 `this`-capturing async callbacks.* The AppContext / Application / PluginHost / RemoteControl ones belong to objects that live for the program (with `alive` flags for shutdown). The window ones are `showMenuAsync` / `confirmAsync` / `enterModalState` callbacks: while the menu or alert is up, `closeFromKey` (keys, Stream Deck, surface) refuses with a notice, the Stream Deck's other commands only open windows or act in them, mouse clicks on other windows are blocked by the modal, and project close / open / delete need the menu; so the window cannot be deleted while the callback waits. At shutdown the modal items are dismissed with result 0 before the windows go, and every such callback returns on 0. The remaining ones carry a SafePointer or `alive` flag beside `this`.
- *Detached threads.* The two detail-picture threads (FixTools) hold shared data, a cancel flag the window's destructor sets, and hand the result back through a SafePointer; `Ravenna`'s destructor thread only stops devices it owns. At program exit one may still be running; it touches nothing that is being destroyed.
- *`SaveWriter`* always finishes a save that was handed over before it stops; the engine waits 60 s for the take finisher. *Static initialisation*: the shared hooks are function-local statics.

**Noted for area B (data safety), not changed here:** `startRecording` adds each track's file and its `backupCopier` job before all tracks have succeeded, so a failure on a later track leaves header-only files behind and their second-drive copies are binned 20 s later as "could not keep up"; nothing waits for the second-drive copier at exit (`backupCopiesGoing()` is unused), so a copy still catching up is cut off when the program closes; and whether `recordingProblem()` is actually shown to the person.

## 3.1.1: a Click box and a Pitch pipe box instead of one Click / Pipe box

- **Engine.** The two levels now live in `AuxSounds`: `setTalkbackLevels (pipeGain, clickGain)`, set from `AudioEngine::setStageMix (..., clickDb, pipeDb)`, whose 5th and 6th parameters replace 3.1.0's one `auxDb`. `talkbackMix` glides each source from last block's gain (button x level) to this block's, so a button and a level are smoothed the same way. The engine adds the result unscaled, before the Output fader. `stageAux` and `stagePrevAux` are gone.
- **Project.** `stageClickDb` and `stagePipeDb` (saved under those names) replace `stageAuxDb`. A 3.1.0 project's `stageAuxDb` is loaded into both.
- **Main window.** `StageSpeakerPanel` has the strip kinds 3 (Click) and 4 (Pitch pipe), made by one `addSound` helper. `reservedWidth` is five boxes.
- **Windows.** `setUpTalkbackButton (button, sound, box)` names the right box in the tooltip, and both help texts name their own box.
- **Tests.** core_test checks that each box sets only its own sound, and that a 3.1.0 project's single level reaches both boxes.

## 3.1.0: the click and the pitch pipe To talkback, with a Click / Pipe box in the stage speaker mixer

**Engine.** `AuxSounds` (`PitchPipe.h`) is split into `render (n)` (the pitch pipe and click make this block once, before the outputs are filled), `talkbackMix (n)` and `addToOutputs (out, numOut, n)` (Plays to, as before). `process()` stays as render + addToOutputs.
- `talkbackMix` returns the mono sum of the sources whose To talkback flag is on (`setPipeToTalkback` / `setClickToTalkback`, atomics), or nullptr when that is silence. A flag switched on or off ramps 0 to 1 (or back) over the block, so it never clicks.
- `AudioEngine::process` calls `aux.render` just before the talkback block. In it, `auxMix` is added to each TB output, once per side it serves (mask & 1, mask & 2, like Playback's tapL / tapR), through the new `stageAux` gain (ramped with `stagePrevAux`) and before the Output fader and the peak light. `aux.addToOutputs` stays where `aux.process` was, after LTC.
- The talkback rule still holds: the TB outputs carry only the talkback mic, recorded playback, and now these two synthesised sounds; never a live microphone.

**Gains and project.** `setStageMix (..., float auxDb = 0.0f)` gives the Click / Pipe box its gain (-60 dB = off). `Project::stageAuxDb` holds it (saved as `stageAuxDb`, clamped -60 .. +10 on load). `PitchPipeDef::toTalkback` and `ClickDef::toTalkback` hold the buttons (saved in `pitchPipe` / `click`). Older projects load with both off at 0 dB. `AppContext::applyAux` passes the flags on; `applyStageMix`, the start-up call and `StageSpeakerPanel::push` pass `stageAuxDb`.

**UI.**
- `StageSpeakerPanel` has a fourth kind of strip (`kind` 3, "Click/Pipe", level only) between Playback and Output; `syncFromProject` follows it. The air before Playback is now found by `kind == 1`, not by position. `reservedWidth` is four boxes.
- The Click and Pitch pipe windows have `tbButton`, set up by `setUpTalkbackButton` (`OutputPicker.h`: toggling, TB orange `0xffff8a2e`, tooltip). It sits at the right of the Plays to row (the picker gets 140 px less), follows the project in each window's timer, and puts a notice up when it is switched on with no TB output chosen. Both windows' help texts, the Click/Pipe tooltip and the main window's TB tooltip say what it does.

**Tests.** core_test "3.1.0 To talkback":
- **Button off:** the TB pair is silent while Plays to sounds.
- **Button on:** both sides carry the click at the level its window gives it, and Plays to is unchanged.
- **Click / Pipe box:** -6 dB halves it in TB only; -60 dB is off there and nowhere else.
- **Output fader:** it comes after the Click / Pipe box.
- **Pitch pipe:** with Plays to empty, it is heard only in the TB pair, and only with its button on.
- **Fade-in:** exact, sample by sample, against the pipe's own signal.
- **Project:** the settings round-trip, and older projects come back off at 0 dB.

## 3.0.2: R records only into an open take window; the ReHarmoniSer asks before closing

**R.** `pollTransportKeys` and `remoteRecord` (the Stream Deck's Record and the controller's RecordTake) now take their take window from `counterTakeWindow()`: the take window used last if it is open, then the one recorded into last if open, then the first open one. That is the window the take counters show, so the counter and R can never disagree. With none open they do nothing but `noTakeWindowNotice()`: "Make a take window first ..." when the project has none, "No take window is open: open one ..." otherwise. Before, R went `lastTakeWindowId` (which falls back to `recentTake`, possibly closed), then `recordTarget`, then the first take window, and Record went `recordTarget`, then the first, all regardless of whether the window was open. A minimised take window counts as open, as it does for the counter. The windows that own R (`ownsRKeyProperty`: `JobDialogBase` - ReTouch / Noise mode / Pitch / ReHarmoniSer and the other tool dialogs - the edit window, ReTimEr, plug-in windows) are unchanged: their own R jobs never reached this path. The take window's own REC / R records into itself, as before.

**ReHarmoniSer close.** `ReHarmoniserDialog::mayClose()`, as `RepairDialog::mayClose`: if the corrections were not written back (`isWrittenBack`, which is `st->accepted`) and Corrected Notes has entries (`editor.editList()`, the switched-off ones too), it asks with `confirmAsync` ("Throw it/them away") and returns false; the answer closes it with `closeMe()` (`closeDialogOf`, which does not ask again). Wired three ways:
- the editor's Cancel (`host.cancel`) asks first;
- the plain window is now launched with `launchDialog (..., mayClose)`, so Escape and the X go through `GuardedDialogWindow` (it used to be JUCE's dialog, Escape = close at once);
- the OUT / IN stream windows keep their own rules (Escape does nothing, the X closes the pair, as for every tool), but their Cancel asks too.

HotKeys: the R line and the ReHarmoniSer Esc line are changed with it.

## 3.0.1: the take counters speak only of an open take window; U and I; Project > HotKeys

`AppContext::counterTakeWindow()` (new) names the take window the counters outside the take windows are about. In order: `lastTakeWindowId()` (the take window R records into) if that window is open, then `recordTarget` if open, then the first open take window in project order. When none is open it returns null. "Open" means `windowIsOpen ("take:" + id)`, where `Main.cpp` looks in `windows`, so a take window minimised to the dock still counts. With no `windowIsOpen` set (a harness) every take window counts as open.

`takeBoxInfo` uses it when not recording, and gives `number = 0` when no take window is open. Before, it used `recordTarget` and fell back to the first take window, whether or not that window was open, so a closed window's number stayed up. While recording it is unchanged: the take being recorded.

The callers:
- `NextTakeBox` (`Uikit.h`): `number <= 0` while idle draws an en dash (`charToString (0x2013)`; the sources stay ASCII) on grey `0xff5c636e`. The main window's menu-bar box shows it; the take window's own box still shows `d->nextNumber` (1 or more in any project Fermata wrote: it starts at 1 and only grows).
- `TakeDisplayView::paint`: the piece comes from `counterTakeWindow()`, so no take window open means no piece. The panel is drawn in `kNone` (`0xff3a4049`) with a dash, and the captions are dimmed.
- `RemoteControlServer::collect`: `takeNumber` is 0 (`RemoteProtocol.h` says so). Stream Deck plug-in 1.8.1 (`render.js` `noTake`, `takelabel`, `takenumber`; `renderdial.js` `take`) draws grey `#3a4049` with a dash for `!recording && !(takeNumber > 0)`. An older Fermata never sends 0, so 1.8.1 changes nothing there; the 1.8.0 plug-in draws "0". `test/run.js` checks the keys and the dial in both scenarios, plus unit checks on the three renderers.

Not changed: the R key and the Stream Deck's Record still record into the last take window when none is open (`pollTransportKeys`, `remoteRecord`), and the counter then turns red with the take's number.

**U and I** (`Main.cpp`, `globalKeyHook`): `deskWindowToggle ("pitchpipe")` and `deskWindowToggle ("click")`, the path the Stream Deck + XL's keys already used. It opens the window, or closes it with `closeFromKey` (never while a menu or message is up), or brings a docked one back. No window used U or I before: the letters in use are R, P, D, L, C, T, H, A, Z, G, N, O, plus M and B everywhere. FixTools' "In stream" button has I as its key, but only inside its own modal message, which has the keyboard. The keys go up through `ToolWindow::keyPressed`, so a text box with the keyboard still types them. The Pitch pipe and Click tooltips in the menu bar name the keys. Checked in the render harness by pressing U, U, I, I with XTest: each window opened and then left the desktop. (Xvfb has no compositor, so the screenshots keep the closed window's pixels until something repaints there.)

**Project > HotKeys** (`src/ui/HotKeysWindow.h`, header-only, included by `Main.cpp`; window key `hotkeys`; `AppContext::showHotKeys`; `forwardKeys` like the mixer):
- **The list.** `hotkeys::lines()` holds `{ section, keys, what }`. In `keys`, `+` joins a combination, `or` separates keys that do the same job, and `/` separates a pair whose jobs the description gives in the same order. `click`, `drag`, `double-click`, `right-click` and `wheel` are the mouse. An empty `keys` is a note written across the line.
- **The drawing.** `HotKeysList` draws headings with an accent line and alternate row shades. Keycaps are filled with `theme::button` and lettered with `theme::inkOn (theme::button)`, so they read in both UI styles. Descriptions are wrapped with `TextLayout`. A combination is never split across lines.
- **Find.** Every typed word must appear in the section, the keys or the description.
- **The window.** `HotKeysComponent` adds the Find box (Esc clears it, Enter goes back to the list), a count, and **Copy the list** (`hotkeys::asText`, plain text with a padded key column). The list keeps the keyboard, so Up, Down, Page Up, Page Down, Home and End scroll it and every other key goes on as from the mixer. The top row leaves room for the window's pin and sun / moon buttons.

**How the list was made.** It is an inventory of every key handler, each line checked against the code:
- **Keys:** every `keyPressed` (TakeWindow, EditWindow, TrimWindow, FixTools x5, RetimerWindow, MasteringWindow, OrganiserWindow, TakeDisplay, PluginHost, ReHarmoniserEditor, the `ToolWindow` frame and the main window in `Main.cpp`), plus `AppContext::pollTransportKeys` (Space and R) and the global hook (M, B, U, I, Ctrl+Z, Ctrl+Y).
- **Boxes and dialogs:** the text boxes with their own Enter / Esc / Tab (the main window's names, a take's bars and notes box, the value boxes, the marker names), the AlertWindow button keys, including JUCE's automatic first-letter keys from `LookAndFeel_V2::createAlertWindow` (Fermata does not override it), and the dialogs that close on Esc.
- **The mouse:** every gesture with a modifier, double-click, right-click, wheel or press-and-hold, from `e.mods` / `getNumberOfClicks` / `setDoubleClickReturnValue` (Alt + click is JUCE's default single-click reset, so it is listed with the double-clicks).

Two helpers read the tool windows and the mouse code. Their findings were spot-checked line by line, and the full list was rendered in both UI styles from a snapshot of the whole `HotKeysList`.

**Fixed while at it:**
- `OrganiserComponent::returnKeyPressed` arranges. Its four buttons no longer take the keyboard, so Enter = Arrange holds after a click.
- Ten FixTools messages (and the comments around them) said "Alt + drag" for choosing tracks: it is Ctrl + Alt + drag (`markAlt` = Alt with Ctrl); Alt + drag alone is the lasso.
- `LowCutPanel` tip: the value takes a double-click (`ReadoutClick` with `needsDoubleClick`).
- The automation tip adds Ctrl + click and Alt fine moves (`draggedValue`, `fine`).
- The insert slot tip adds Shift (`isCopyDrag`).
- The pitch curve texts say Preview for the R button.

**Found and left as they are** (the person was asked about the first):
- **ReHarmoniSer Esc** closes the window at once and throws away corrections not written back, with no question (Spectral Repair and Noise mode ask first: `GuardedDialogWindow` / `mayClose`).
- **ReHarmoniSer Space** has no `spaceIsForRecording()` check: the press that stops a recording also starts its playback.
- **Drop-downs keep the keyboard** after a click in Mastering and ReHarmoniSer, so the arrows change them instead of zooming.
- **M, B, U, I and Ctrl+Z / Ctrl+Y do not reach** the modal tool dialogs (`launchDialog`), the stream windows, plug-in windows or the Take Display screens. Only `ToolWindow` and the main window run the global hook. The list's first note says so.

**Rule from now on:** a key added, changed or removed anywhere is changed in `hotkeys::lines()` in the same build. The sweep brief's ground rules say so too.

## 3.0.0: the tool pass (sanitizers, the readers' fuzz test, static analysis) and what it found; the mixer's fader caps

Every check where the tools, not reasoning, find the problems, run over the whole program before the deep sweep (`docs/CODE-SWEEP-BRIEF.md`, for a Fable 5.1 session). `tools/tool-checks.sh <JUCE> <mode> [out]` runs each again (asan, tsan, fuzz, warnings, tidy, query, valgrind, coverage, all). Two core test programs must never run at the same time: both use the temp folder `fermata_test` (a first ThreadSanitizer run showed four "failures" that were exactly that).

**Results**
- **AddressSanitizer + UBSan + leak check** (core_test, stress_test 3 s, timeformat_test, remote_hub_test, and the Re-HarmoniSer's voices and golden tests): nothing in Fermata's code. Elsewhere: undefined behaviour inside JUCE's bundled FLAC and Ogg Vorbis codecs (left shifts of negative numbers, one read past a 17-entry table in libvorbis psy.c), in JUCE's JSON reader (a whole number of 20 digits overflows its sum) and in `BigInteger (int)` (a WAV channel mask with its top bit set, met by the fuzz test) - JUCE's own, harmless in practice: `tools/ubsan-suppressions.txt` silences JUCE's arithmetic and shift reports (never memory errors) so the checks stop only for Fermata's own. Leaks: (1) the plan `AudioEngine::rebuildPlan` leaves in memory when the audio thread does not come round in 500 ms - deliberate (it may still be in use), and the tests pretend such stalls (suppressed in the script); (2) a test that move-assigned an `mcu::Event` holding a long SysEx reply: JUCE's `MidiMessage` move-assignment does not free the heap data the target already holds. The program never re-assigns an Event (Surface.cpp hands each one straight on, the incoming queue is push_back / swap only); the test now uses a fresh variable. Never move-assign into a `MidiMessage` that may hold a long SysEx.
- **ThreadSanitizer** (stress_test 5 s, remote_hub_test, core_test): no report.
- **GCC warnings** (-Wall -Wextra -Wshadow -Wnull-dereference -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wimplicit-fallthrough -Wmisleading-indentation -Wformat=2 -Wdangling-reference ... at -O2; all 45 sources): no error. 176 -Wnull-dereference, 49 -Wshadow (deliberate: lambdas re-binding `app` and friends), 17 -Wmissing-field-initializers (aggregates whose other members are meant to be zero), 17 -Wduplicated-branches (cosmetic: MediaWindow 100, FermataLookAndFeel 210, ReHarmoniserEditor 1078 / 1240), 1 -Wmisleading-indentation (FixTools 3269: `done +=` after a one-line `if`, correct), 1 unused function (Main.cpp `writeProjectIcon`).
  - The null warnings come down to 40 places in Fermata's code, all one pattern: a `SafePointer` (or `def()` / `findTakeWindow`) checked at the top of a callback and used again after calls; GCC cannot know the second read gives the same pointer. Every one is checked first. Whether a call in between can close the window is left to the deep sweep (area A). The places (3.0.0 line numbers): EditWindow.cpp 418-420, 2390, 2406, 2447; FixTools.cpp 437, 553, 890, 1046, 1406, 1667, 2137, 2139; MainWindow.cpp 1452-1453; MasteringWindow.h 67, 73 (callbacks in MasteringWindow.cpp: requestEnvelope, chooseFolder, openIsrcTool); MixerWindow.cpp 1216, 2691 (after `app.copyMix`); OrganiserWindow.cpp 400, 445-450; PluginHost.cpp 107; PluginManager.h 37, 134; PreampSetup.h 174-177; TakeWindow.cpp 1004 (via applyNotes / showBars), 1275, 1306-1309, 1538-1541, 2058, 2294; TimeReadout.h 339, 380; Takes.h 42 (via AlignWindow.cpp makeSource).
- **clang-tidy** (bugprone-*, clang-analyzer-*, without the style-only checks; all sources): 30 distinct findings, none a bug. Deliberate `unique_ptr::release()` where the audio thread stalled (AudioEngine.cpp 55, 1440, 1566); float loop counters in drawing code; `(seen[...] |= bit) == 3` (ImportPlan.cpp); `--depth == 0` in a condition (BusyWindow.h); a dead store (MasteringWindow.cpp 1690); TakeWriter.h 76 "garbage pointer": the analyzer assumes the writer's channel count differs from the route's - both are the track's `channelCount()` (AudioEngine::startRecording); MainWindow.cpp 62 (a null `this` inside a lambda); Mcu.h `assignmentDigit` (a char conversion); the Re-HarmoniSer's `(int) (x + 0.5)` rounding (Dsp.cpp 76), empty catch blocks and an excessive-padding note.
- **clang-query** (`juce::var` indexed with a number - `var::operator[] (int)` is unchecked in a release build: on a value that is not a list, or past its end, it reads through a null or stray pointer): 5 places in the whole program. Project.cpp 150-152 check first; `RavennaDevice::applyPath`, `RavennaDevice::readChannel` and `MasterRenders::load` did not (fixed, below). Now 0 unchecked.
- **The readers' fuzz test** (new, `tests/fuzz_readers.cpp`; `tools/tool-checks.sh fuzz`): damaged and random input for every reader of outside data, under ASan + UBSan with a 30 s per-input watchdog: WAV headers (mended, then read by JUCE), iXML, AAF (bytes of a real 44 MB Pyramix AAF changed in place, the header, sector index and directory most often), listeners' comments, control-surface MIDI, Stream Deck lines, saved projects with their windows and the settings files (changed as parsed values - kinds, numbers, lists, missing fields - not as text, which the JSON reader would refuse first), the Merging devices' and ANEMAN's JSON, typed times, the names of files being imported (piece / take / track, stereo pairs), LTC audio. Rules checked besides crashes: looking at a WAV never changes it; mending never changes its length or a byte of its audio; a mended file is whole when looked at again; a mended file with an ordinary format chunk reads (through the checked reader) as long as the mending said; a project read from damaged input saves into a file that reads back. Before the fixes it found four crashes within seconds - JUCE's WAV reader on a nonsense RF64 size, the iXML and comments readers on text that is not UTF-8, the Merging path - and JUCE's JSON overflow (a fifth, in time parsing, came from feeding the parser bytes the program never gives it: typed text is always valid UTF-8, and the harness now makes its text the way the program does). Its later runs found five more, all undefined behaviour from numbers at the very edge (no crash in practice, but undefined): the stale-header sum in `wavrepair::inspect`, the iXML track index, ANEMAN's channel numbers, `timefmt::detail::grouped` (negating the lowest number) and a timecode's hours figure (fixed, below), and two WAVs that read as empty after mending - JUCE's reader stops at an unknown chunk of size 0, before mending as well, so the rule now leaves such files out. The final run after the fixes: about 4.25 million inputs over the 11 targets (60 s each; wav 341,102, ixml 289,297, aaf 55, feedback 211,566, mcu 2,757,102, remote 283,781, json 6,289, devices 5,910, text 291,692, import 11,386, ltc 52,063), nothing found.
- **valgrind memcheck** (stress_test 5 s, timeformat_test, remote_hub_test): 0 errors in all three (memory read before it was written, invalid reads / writes); the stress test passes under it.
- **Coverage** (gcov on an -O1 build, so the figures are rough; the core tests and the stress test): about 91 % of src/core's lines run. Least covered: Ravenna.cpp 24 % (the conversation with a device needs a device or the Python mock - only the parsing helpers are in the tests), AudioFormats.h 42 % (the over-4 GB check), Mcu.h 76 %, MasterRender.cpp 79 %, Pdf.h 85 %, CoreReservation.cpp 86 % (its Windows half is not built here), Bounce.cpp 88 %, MasterExport.cpp 89 %. TimeFormat.h and RemoteHub.h are run by their own test programs; src/ui has no automated test at all (only the render harness).
- **Windows-header check**: OK.
- Not available here: cppcheck (no package source reachable), MSVC's /analyze, clang's MemorySanitizer and libFuzzer (no clang runtime libraries).

**The fixes**
- **`src/core/AudioFormats.h`** (new): `CheckedWavAudioFormat` - JUCE's WAV reader with `lengthInSamples` kept within what the file's bytes can hold (`keepWithin`). An RF64 file whose ds64 data size has its top bit set gave JUCE a negative length, and `AudioFormatReader::read` then cleared "the samples beyond the end" at a negative offset: a heap overflow. A whole file is never affected (its data cannot be larger than the file), and the file's size is only asked for when the header claims more than 4 GB (no extra stat for the thousands of files an edit opens). `registerAudioFormats (fm)` - JUCE's basic formats with it in place of the plain WAV reader - is now used by all 19 places that called `registerBasicFormats()`.
- **`wavrepair::inspect`**: an RF64 file with its ds64 chunk at 12 is mended in its 64-bit sizes (RIFF size, data size, sample count at 20 / 28 / 36) with 0xFFFFFFFF in the 32-bit ones; the chunk keeps its size and table. Until now a small RF64 file (a recorder that always writes RF64) got 32-bit sizes only, which JUCE ignores for RF64: it still read as empty. An RF64 file whose real 32-bit data size disagrees with ds64 (what that old mending left) is mended to agree, the 32-bit size standing (`ds64Disagrees`; only sizes that were wrong are extended to the end of the file). 'RF64' without a ds64 chunk under 4 GB becomes 'RIFF'. JUCE-written RF64 files (recordings past 4 GB): byte for byte as before. And the stale-header test (`good + blockAlign <= avail`) is written `good <= avail - blockAlign`: with the largest 64-bit size it overflowed (undefined behaviour, caught by the fuzz test's UBSan on its second run).
- **`src/core/TextBytes.h`** (new): `textFromBytes (data, size)` - UTF-16 with a BOM, UTF-8 with or without one (checked strictly by `isStrictUtf8`: continuation bytes, overlong forms, surrogates, > U+10FFFF), otherwise Windows-1252. `juce::String::fromUTF8` trusts its bytes: with a character cut off at the end, JUCE's UTF-8 reader runs past the end of the text (the fuzz test caught `XmlDocument::readChildElements`, `String::operator[]` and `StringArray::addTokens` doing it). `juce::String::createStringFromData` checks first, but its check (JUCE 8.0.9 `CharPointer_UTF8::isValidString`) does not test continuation bytes, so "Caf\xe9 </NAME>" passes as UTF-8 and the 'é' swallows "</". Now used by `feedback::parse`, `readIXmlTrackNames`, `WebSocketClient::receive`, `remote::Hub` (control-port lines), the edit window's comments-file sniff and Ddp.cpp's DDPMS / PQ records. Typed text is always valid (TextEditor), so the time readout and the name boxes need nothing.
- **`feedback::splitTable`**: the characters are taken out once and each field is gathered as characters. `String::operator[]` walks a UTF-8 string from its start (O(n²) over a file: 40,000 comments took hours) and `String +=` one character copies the string each time (JUCE's String does not grow geometrically: one stray quote made the rest of a big file one field). `feedback::merge` looks each key up in a hash map instead of searching the list for every note (seconds for a long list). 40,000 comments read and merged: about 100 ms (it was hours). `feedback::parse` refuses files over 256 MB.
- **`timefmt`**: `grouped` takes the magnitude as an unsigned number (negating the lowest `long long` overflowed - a typed "-1e30" in Samples mode gets there); `parse` refuses a timecode whose hours are 100000 or more, checking the figures while they are still doubles (a huge hours figure overflowed the frame count). Tests in timeformat_test.
- **`readIXmlTrackNames`**: a track's INTERLEAVE_INDEX is used only when it is a positive number ("-2147483648" minus one overflowed: UBSan, on the fuzz test's iXML target).
- **`planFromAneman`**: a connection whose input or output number is outside 0 - 9999 (no device has such a channel: a damaged answer) is left out (adding the group's first channel to 2147483647 overflowed: UBSan, on the fuzz test's devices target).
- **Ravenna**: `applyPath` checks `[n]` against the list (a device message with a path to a channel the module does not have crashed); `readChannel` checks the list (a message can replace it with a shorter one, and `handleMessage` does not call `rebuildChannelList` for a change under "channels" - left for the sweep, area D). **`MasterRenders::load`**: renders.json's "slots" checked.
- **`Aaf.cpp` `Cfb::open`**: the extra sector index (DIFAT) is followed for at most as many parts as the file has sectors - a chain that loops back on itself with a huge count added 127 entries per turn until memory ran out - and the sector index (FAT) stops once it covers every sector of the file.
- **The mixer's fader caps** (not from the tools: the person's photo - a fader at 0.0 dB with its cap at about -4 on the scale, one at +4.9 near 0). `MixerLookAndFeel::getSliderLayout` took `kCapHalf` (22 px) off each end of the travel on top of the bounds JUCE's `LookAndFeel_V2::getSliderLayout` returns, which have already lost `getSliderThumbRadius` (12 px, `LookAndFeel_V4`: min (12, width / 2)) at each end; the scale, the meters and the VCA ghost (`FaderBlock`: `travelTop` / `travelBottom`) take only `kCapHalf`. So the cap's travel was 24 px shorter than the scale's: drawn `24 f - 12` px below its mark (f = `faderFrac`, 0 at -inf, 1 at +12) - 7 px low at 0 dB, 12 px low at +12, 12 px high at -inf. Now the fader's slider bounds are its local bounds less `kCapHalf` at each end (the faders have no text box), the same as the scale; the drag sensitivity (`syncDrag`) follows from the layout. Checked with the render harness: faders at +12, +6, 4.9, 0, -6, -20, -60 and -inf have the cap's line on their marks to the pixel.
- **Tests** ("3.0.0 tool pass" section): `textFromBytes` (UTF-8, Windows-1252 that JUCE's check let through, cut off, overlong, surrogate, 4-byte, BOMs, UTF-16); comments in Windows-1252 / cut off / UTF-16; 40,000 comments read and merged (and a stray quote) under a generous time bound; iXML that is not UTF-8, cut off, or with the lowest index number; `applyPath` / `readChannel` out of range; ANEMAN's answer with impossible channel numbers; RF64 mending (stale ds64, nonsense ds64, the largest 64-bit size on a recording, sizes that disagree, RF64 without ds64) read back through `registerAudioFormats`, and a whole file read as before; an AAF whose index loops. remote_hub_test: a line that is not UTF-8. timeformat_test: the lowest number grouped, an impossible timecode hours figure. The MCU test no longer re-assigns an Event (the leak check).

## 2.103.0: the ReTimEr (Signalsmith Stretch, linked multitrack, ramps, ripple)

Time-stretch of a whole piece or of marks 1 - 2 of an edit, pitch kept, every track through ONE stretcher, everything after it rippled. The engine choice came from a listening test on a solo piano and a 20-channel choir multitrack (Rubber Band R3 vs Signalsmith, 5 % and 20 % slower): no audible difference to the user; Signalsmith keeps the inter-mic timing exactly (it phase-locks every channel to the loudest channel of each band) and was about 15 times faster, and it is MIT.

- **Library:** `third_party/signalsmith-stretch/` - Signalsmith Stretch 1.3.2 (`signalsmith-stretch.h`) with the two Signalsmith Linear 0.6.6 headers it needs (`signalsmith-linear/stft.h`, `fft.h`) next to it, so its own `#include "signalsmith-linear/stft.h"` resolves without an include path; licences and provenance in `README-FERMATA.md`. Only `src/core/Retime.cpp` includes it, inside a warnings-off block (MSVC `warning (push, 0)`, GCC / Clang pragmas), behind a plain `Stretcher` class whose members instantiate the templates there. Fixed seed (`SignalsmithStretch (20261024L)`): the same input always gives the same output (the library only randomises phases above 2x stretch, which the ReTimEr never asks for). `presetDefault` (120 ms blocks, 30 ms interval).
- **`retime::TimeMap`** (Retime.h, header-only, pure): original timeline -> new timeline. Speed 1 before `t0`; from `t0` the stretch `S` (new / old, > 1 slower) with raised-cosine glides `rIn` after `t0` and `rOut` before `t1` (rate `1 + (S-1) w(x)`, `w = (1 - cos pi x) / 2`, integral in closed form via `F(x) = x/2 - sin(pi x) / 2 pi`); speed 1 after `t1`, everything later by `delta()`. `uniform`: `S` everywhere (a whole piece's own audio, handles included). `unmap` inverts it (linear parts exactly, glides by bracketed Newton). `solve (L, target, rIn, rOut) = 1 + (target - L) / (L - (rIn + rOut) / 2)`: the window solves S for the wanted length with the glides it has (they are shortened proportionally when longer than the part, in the window and again in the plan).
- **`retime::stretch`** (Retime.cpp): drives the library in 512-sample output steps whose ends lie on the map: step k makes `n` new samples from `round (unmap (q0 + e + n)) - previous` original samples (the library maps linearly within a call), so the speed follows the glides and the content lands where the map says (measured: transient energy centres within ~0.5 ms; the two-channel delay of 37 samples is still exactly 37 in the middle of a 12 % stretch; before `t0` the output equals the input, correlation 1.0000). Latency: input is handed over `inputLatency()` ahead of the library's processing time and comes out `outputLatency()` behind it, so emitted sample e is at new time `q0 + e - outLat`; it starts `margin` (2 blocks or 0.25 s) before the first wanted sample, `seek()`ing a full `seekLength()` of real audio first, so that sample has a full history. Input through `InputWindow` (forward-only, a second at a time from the `ReadFn`), output through a `WriteFn` in order. Measured after `t1` (speed 1 again): correlation with the shifted original -0.4 / 0.7 - the phase vocoder's partial phases have drifted (inaudible on its own, but a crossfade with the original would comb). Hence the hand-back rule below.
- **`retime::makePlan (edit, request, folder, fileLength, isLtc, quietest)`** (any thread, pure apart from the callbacks): works on a copy of the edit.
  - The part: whole piece = `wholeRun` (the clicked piece extended over unnumbered joins - fix joins and snips - to the real edit points); marks = `snapSpan` (a mark within `kGuard` (60 ms) of a real join's fade zone, or of a free piece edge's fade, goes onto that join / edge - the IN piece's start; fix joins are NOT snapped to). Too short, waiting pieces (Export for Processing), S outside 0.5 - 2, an unreadable file: `error`.
  - Mark 1 inside a take: the take-over point `s0` is moved in front of any fix join's zone it touches (both sides of a fix join must be retimed together), then the piece straddling `s0` is split there (`splitRegion`) with a LINEAR crossfade `[s0 - 30 ms, s0]` (both sides identical there), marked as a fix join; when no clean split fits (the mark in the piece's own crossfade out), `min (s0, fadeOutBegin - guard)`, else the piece is retimed whole.
  - Mark 2 inside a take: the straddling piece's fix-joined run is followed; if its end is within `kRunOn` (30 s) of `t1` the whole run is retimed (`forced`) - it runs on at speed 1 to its own edit point; otherwise it is split at `quietest (piece, lo, hi)` (`quietestMoment`: the quietest 60 ms of all its tracks summed, in 10 ms steps, in the 3 s after the mark) with an EQUAL-POWER 60 ms crossfade `[s, s + 60 ms]` after the split (speed 1 there; the two are no longer phase-coherent, so equal power).
  - Classification: whole piece by index (`run`); marks: `forced` -> retimed, `end <= t0` -> stays, `start >= t1` -> moves by `delta`, else retimed. Retimed pieces joined by fix joins form ONE stream (read through their own linear crossfade: `Source::fadeIn / fadeOut` only at joins inside the stream; the stream's two ends are left open so its handles are the take itself); real edit points and snips separate streams. Each overlapping overdub is a stream of its own. LTC tracks (`isLtc`) are left out of streams and of the new pieces.
  - A stream reads `[a, b)`: its pieces with their fades, plus handles - 20 s (whole piece) / 5 s (marks) at real edges, 0.1 s at the joins made here -, clipped to what every file of its first / last piece holds (`fileLength`); it makes `[outA, outB) = [floor (map (a)), ceil (map (b)))` (tiny epsilons against rounding noise) through `audio` (= the timeline map, or the uniform one for a whole piece's own streams). New files named `<take> - <track> (retimed 105.0%).wav` in Recorded Media/Fixes (named only: `audioops::uniqueFile`).
  - The new piece: the first piece's id and metadata, a NEW take id per original take (its samples are no longer the take's, so the take window's In / Out flags and the automation's take matching must not use them; the edit window's take count now counts window + base name), name "<take> (retimed 105.0%)", its files at `fileStart` 0, `startSample = newSampleAt (first.start)`, `srcIn / srcOut` in the new file's samples, `sourceLength = outB - outA`; fades keep their lengths (the first piece's in-fade, the last one's out-fade, fix / snip marks with them); volume changes mapped through the map (ramps too), their dB lined up with the new file list, and where a fix-joined piece took over a change to its starting level over the join's crossfade.
  - `newSampleAt (t)`: t <= t0 -> t; t >= t1 -> t + delta; else `round (map (t))`. `delta = round (map (t1)) - t1`.
- **`applyPlan`**: regions and overdubs from the plan, every automation point `newSampleAt` and re-anchored when it was pinned, `insertIndex` by the piece it was before, `clampAll`. Marks 1 - 2 are NOT moved (they are view state, not in undo: moved, an Undo would leave them round the retimed length, and "try 103 % instead" would retime a different part). Markers, feedback flags and the guide are not moved either (no ripple operation moves them; the guide is the reference).
- **Waiting pieces** (Export for Processing): `splitRegion` clears `waiting`, and a new piece has none, so a waiting piece must be neither cut nor re-rendered - checked before every cut (the take-over in front of a fix join before mark 1, the hand-back after mark 2) and for every piece classified as retimed (the run-on), not just for the pieces between the marks (review of 2.103).
- **Levels across a fix join inside a stream:** the later piece's own start level (a gain change at <= 1 ms) is NOT copied - played, that level holds from the start of its fade-in (Playback `GainStep`), so the merged piece glides to it over the join's crossfade instead (the copied change was a 3.8 dB step mid-glide).
- **`renderStreams`**: streams on up to `threads` threads (each with its own format manager, readers, writers: 24-bit, dithered, `DitherOut`), progress weighted by frames x channels, cancel checked every step; on failure or cancel every new file is deleted.
- **Files only tried:** `deleteTried` (RetimerWindow.cpp) deletes a result's files on Cancel / Make again / closing / ANY job error (a stop after the last file was finished made "Cancelled." with every file written), with their saved waveforms (`wavestore::fileFor`, named after size and date, so first), retrying every 0.5 s for 30 s what Windows will not delete yet (the edit window's `WaveThumb::readFile` holds a file open while it reads its outline, and a preview puts long files in front of it).
- **Undo and the insert point** (Project.cpp `restorePiece`): `insertIndex` is view state taken from the live edit; when the live edit's index points at a piece, the restored edit's index is that piece's index there (a retime's cuts, or a snip, change how many pieces come before it). The window's `Base` keeps `insertIndex` too and puts it back in `takeOut` and in the Undo fix lambda.
- **Window:** `src/ui/RetimerWindow.*` `openRetimer (app, edit, selectedPiece, parent)`: marks set -> marks mode (snapped span shown, with the note), else the clicked main-track piece -> whole piece, else a message. `RetimerPanel` (modal, `LaunchOptions::launchAsync`): selected length, length / percentage boxes (typing in one makes it the master; the other shows the result), ramps (marks only), `SpeedCurve` (speed % along the part), Make (an `AudioJob`: `makePlan` + `renderStreams` on the job thread, from a snapshot of the edit), Play original / Play retimed (`putIn` = base + `applyPlan`, played, taken out when playback ends - `Hold` raises `saveHold` meanwhile, like FixTools' `PreviewApplied`), Write back (keeps it, `fixUndo` puts back regions / overdubs / lanes / marks / insert point through `Base::restore`; `undoHold` held while the window is open, so Write back is one Ctrl + Z step). A result is "fresh" when its key (S, ramps) matches the fields; Play / Write make it first when not. Settings remembered in the user settings (`retimer.asPercent / rampIn / rampOut / preroll`). Edit window: `retimerButton` (`Icon::ReTimEr`: option A - a spring, in the ReTimEr red, between two bars, `|<- ->|` under it).
- Tests (also): "2.103 undo keeps the insert point ..."; the plan test has the level glide over a fix join, waiting pieces met by the run-on, the hand-back and the take-over, and the insert point; the "2.102 background saver" test waits (up to 2 s) for the saver's wake call, which comes just after it is idle (it failed on a loaded machine).
- Tests: "2.103 ReTimEr: the time map ..." (four maps: exact new length, inverse, monotonic, no jumps, glides start / end at speed 1; the stretch: in order and exact length, deterministic, identical before t0, the 37-sample mic delay kept, clicks within 1.5 ms of the map), "2.103 ReTimEr: the plan ..." (run on, hand back at the quietest point, mark on an edit point, whole piece faster with handles clipped to the take, a fix join inside read as one stream, a whole piece across a fix join, gains / automation / overdubs, refusals), "2.103 ReTimEr: end to end ..." (real files: cancel leaves nothing, file lengths and channels, a click where the map puts it, the file before mark 1 equal to the take, the hand-back found in a breath).

## 2.102.0: Space and Stop answer at once (saves on a thread, undo by copies, a Play that does not wait, the stall watch); the edit window's visual guide

The 2.101 log showed the screen held up for 220 - 430 ms at play starts (`refreshes late` lines). What blocked the message thread, measured with `scratchpad/savebench` (14 take windows of 26 takes x 40 tracks, 2 edits of 400 pieces x 40 files): the undo step after a change to a big edit (`undoText` twice: 1090 ms), a save after one edit changed (331 ms, every 4 s while idle and straight after Stop), `PlaybackSession::start` waiting for kRing / 2 samples and `open()` asking the disk about every file (16 000 `existsAsFile`: 73 ms on Linux, several times that on Windows).

- **Undo by copies:** `UndoPiece` holds `std::shared_ptr<const TakeWindowDef / EditDef>` (`take` / `edit`) besides `text` (now only the mastering list, or a window without a copy); `snapOf` (hash + copy, about a millisecond for a 400-piece edit), `pieceOf`, `masteringSnap`; `undoTextOf` is gone. `restorePiece` copies from the piece's copy (text fallback through a private Project as before). `undoTick` for the bench edit: 1090 -> 25 ms. The view fields kept from the live window now include `showGuide`.
- **Saves in two halves:** `Project::SaveJob` (root var, the root's tracks and their ids, the windows to write as copies - `Window { file, id, take | edit + mixer var | mixerFile var }` -, the `OldWindowFile`s to tidy, timings). `prepareSave (quick)` (message thread: `dirty = false`, `toVar (false)`, `planWindowFiles` - the old `splitWindowFiles` without the writing: it decides the files, copies the changed windows into the job and fills root's file lists -, `windowFiles = next` at once, `oldWindowFiles` moved into the job). `writeSave (job, error, windowsFailed)` (static, any thread: `takeWindowToVarIn` / `editToVarIn` - static forms of the window vars with the base folder passed in - and `windowFileVar`, the window files, the project file, `tidyWindowFiles`). `saveFailed (job)`: `dirty` again, the job's windows' hashes cleared so the next save writes them, the tidy list back into `oldWindowFiles`. `save()` = prepare + write on the calling thread; when a window file cannot be written it writes the project whole (`toVar (true)`), as before. Bench: prepare 19 - 28 ms, write 48 - 294 ms (the part that moved off the message thread).
- `src/core/SaveWriter.h` `BackgroundSaver`: a thread with a FIFO of jobs; `write`, `isBusy`, `waitUntilIdle (ms)`, `drain()` (message thread: runs the callbacks of finished jobs; the thread calls `wake` after each, which in the program posts a `drain`). Anything that saves again or touches the files first waits and drains (`AppContext::waitForSaves`, also as `Project::settleBackgroundSaves`, called at the top of `save`, `load` and `forgetWindowFile`), so a failed save is always known before the next one is prepared. `AppContext::saveGeneration` (bumped in `afterProjectReplaced`) drops a result of another project.
- **Automatic save policy** (`AppContext::autosaveIfIdle`, the 4 s timer): dirty, not recording, saver idle, >= 30 s since the last save, no mouse button down; while playing only once 5 minutes have passed; otherwise (unless 5 minutes have passed) >= 3 s since Play / Stop (`noteTransport`) and >= 2 s since the last change (`project.undoDirtyAt`). `savesHeldWhilePlaying` and the save after Stop are gone. `saveNow (true)` (record start / stop, a new window, the policy) = `prepareSave` here + `saver->write`, ONE AT A TIME: while a job is being written a request only sets `quickSaveWanted`, and `savedInBackground` posts that save once the job is done - so a failed job's `saveFailed` (hashes cleared, `dirty`) always runs before the next snapshot, and no project file can list a window file a failed job never wrote. `saveNow()` = wait for the saver, `project.save`. `waitForSaves` waits as long as it takes (logging every 10 s): writing the same files from two threads is never safe. A failed background save: `saveFailed`, `wholeSaveNext` when a window file failed (the next save is a full one), `lastFailedJob` kept until a save gets through, its error posted and shown only if the project is still unsaved then (`showSaveError`: an automatic save's error at most every 2 minutes). `closeProject (false)` and "Quit without saving" (`shutdown` with `skipFinalSave`) wait for the saver and then `retryFailedSave` (write that job again: the state as of the failed snapshot, never the later changes the person chose not to save).
- `planWindowFiles`: `place` never takes a path another window has in `windowFiles` (`ownerOf`), on the disk yet or not; `prepareSave` drops from the tidy list any file this save lists (review of 2.102: a window taking over a path another had just left could have had its new file tidied away by the same save).
- **Play that does not wait:** `PlaybackSession::start (waitForPreload = true)`; with false (`beginSession`, `playRenders`: `engine.startPlayback (session, false)`) `primed` is false and `pull()` gives silence and leaves `position` (and the clock: `clockBlock` 0) alone until `fifo.getNumReady() >= primeTarget` (0.35 s, 16384 for a loop; or the render is done; or 3 s of waiting), then plays; `msToFirstSound()`. `open()` checks existence only for segments that overlap the first 3 s; the session's opener logs a file it cannot open once ("Playback: cannot read ..., that part plays silent"). The ring is `juce::HeapBlock<float>` (not zero-filled: only what the reader wrote is read). The tests' and the old default stay `start()` = wait.
- **Stop that does not wait:** `AudioEngine::stopPlayback (handOff)`; `AppContext::stopPlayback (true)` only from the transport presses (the take and edit windows' `togglePlay`, `remotePlayPause`, the watcher when a play reaches its end). A session without automation then goes to `AudioEngine::Retirer` (defined at the top of AudioEngine.cpp, before the destructor resets it), a thread that frees it once `blockCounter` has moved on by 2 (or the device stopped), or leaks it and clears `neverStalled` after 5 s with no block (a hung driver), as `waitForAudioThread` did. Every other stop (with automation, the process windows' Stop / Revert / close, project changes) does everything itself as before AND `waitForStoppedPlays (3000)`, so when it returns no file that played is open (on Windows an open file cannot be deleted: the process windows delete their preview files straight after). `sessionsWaitingToBeFreed()`. The saver's `writeSave` call is wrapped in try / catch (a throw must not leave `pending` stuck).
- **The stall watch:** `src/ui/StallWatch.h`: a 20 Hz timer stamps a beat; a thread logs "Stall watch: main thread busy for N ms at HH:MM:SS (labels)" when the beats stopped for 150 ms or more (at most 30 lines a minute; a gap over 30 s is "the computer may have been asleep"). `BusyLabel` (RAII, a string literal in `messageThreadLabel()`) marks: starting / stopping playback, making an undo step, the save snapshot, saving, waiting for the saver, drawing the edit / take / trim window, the edit / take windows' change handlers. `AppContext::stopPlayback` logs "Play / Stop timing" when Play took over 60 ms, the first sound over 600 ms or Stop over 40 ms (`PlayStartTimer` in every play function, `AudioEngine::playbackFirstSoundMs`).
- **The visual guide:** `EditGuide` (Edit.h: window, take and track ids, names, file, channels, rate, `srcIn / srcOut`, `start` on the edit's timeline in samples at the guide's rate, may be negative) and `EditDef::guide` / `showGuide`. Saved as "guide" (the file through `putFilePath` + "channels", so the project copy and the window file's track list see it) and "showGuide"; the guide is in `undoHash`, `showGuide` only in `saveHash` (and `kEditViewKeys`); `forEachAudioFile` includes it (relink). `segmentsForGuide` (one segment, `begin = max (0, start)`, 5 ms fades) and `guideTrackIndex` (its track, else one with its name and channels, else the first with as many channels) in Playback; `AppContext::playEditGuide` (as `playEdit`, through the edit's mixer; a notice when that strip is muted or another is soloed) and `setEditGuide` (the take window's clip menu, item 6: marks 1 - 2 of that take if set, else the whole take; into `targetEdit`, made if needed; start 0). `EditTimeline`: `bandH()` = ruler band + `guideH()` (40 - 90 px, from `rowH`) - every old `kRulerH + kLaneH` that meant "where the tracks start" is `bandH()`, so the lane is part of the sticky band (it stays in view when the tracks are scrolled); `paintGuideLane` / `paintGuideName`, `guideMouseDown / Drag` (one undo step per drag; at least half a second of it stays at or after 0:00), `showGuideMenu`, `guideSolo` and `setGuideSolo` (restarts a play of this edit through `EditWindowComponent::replayFromPlayhead`). `EditWindowComponent::play` routes every play of the window (Space, the play button, loop, go-to, the ruler while playing) to `playEditGuide` while the guide's S is on; the `guideToggle` button (dim without a guide). The timeline's length takes the guide's end into account; the edit's own length does not.
- Development aids (environment variables, off by default): `FERMATA_LOG_PLAYSTOP=1` logs the Play / Stop timing line at every Stop; `FERMATA_LOG_AUTOSAVE=1` logs why the automatic save waits at each tick. The render harness (`scratchpad/rend/Main_r.cpp`) has `FERMATA_FAKEAUDIO=1`: a thread that drives `AudioEngine::process` at 48 kHz / 256 as a driver would (Xvfb has no audio device), so Play / Stop can be measured there - Play took 0 ms of the message thread, first sound after 1 - 7 ms, Stop 0 ms, no late refreshes.
- Tests: "2.102 undo keeps copies...", "2.102 a save in two halves..." (the job holds the window as it was at the snapshot; written on another thread; a window file that cannot be written; written again after `saveFailed`), "2.102 the background saver..." (order, `drain`, renamed window files tidied in turn), "2.102 a play that does not wait..." (silence and position 0 until primed; the 3 s file check), "2.102 Stop hands the play...", "2.102 the edit's visual guide..."; the "2.69 undo hash" test has the guide's fields (86 fields move it, 17 view fields do not).

## 2.101.0: snips (F1); the pitch pipe's keyboard sound

- `EditRegion::snipIn / snipOut` (saved as `snipIn` / `snipOut`, in `undoHash`). `EditDef::isSnipJoin (k)` needs BOTH `regions[k].snipIn` and `regions[k-1].snipOut` (a piece moved or removed from its partner leaves a plain edit); `isUnnumberedJoin (k)` = fix or snip, used by `editNumberAt`, the edit window's flags ("sn" / "fx"), the take window's use flags ("I sn"), the Trim window's numbering, next / previous and `step`. `isFixJoin` stays the offline fixes' own joins only: `detectStreams` treats a snip as a real join (two streams) and `commitAllJoins` commits it like an edit (the boundary goes to the middle of the rolled crossfade).
- `EditDef::snipAt (at, why)`: `regionAt`, refuses a waiting piece and a cut whose ±15 ms would reach the piece's fades (`fadeInFinish` / `fadeOutBegin`) or ends; `splitRegion` (which now clears `snipIn` of the right half and `snipOut` of the left, so the right half keeps the original end's mark), then marks the join and sets both sides to `FadeCurve::EqualPower` (the fix joins are linear). `shuffleRegion` clears the snip marks of the joins it remakes; `applyRegionJobs` gives the first / last piece of an original region its `snipIn` / `snipOut` back, like `fixIn` / `fixOut`.
- `EditWindowComponent::snipAtPlayhead` (F1, no modifiers; F1 was unused): the playhead as `setFixMark` takes it (the moving one while this edit plays, else `cursorSeconds`), selects the new join, `project.changed()`, notice. Playback is not stopped (the session plays a snapshot of the segments).
- `TrimComponent::describe`: "Snip (F1, not an edit point)".
- `PitchPipeVoices::setKeyboard`: two band-limited wavetables per band (`kbBright`: 1, then 0.62 h^-1.25, soft above 5 kHz; `kbMellow`: 1, 0.14, 0.04), same phases, crossfaded by `kBright` (time constant 0.18 s); level `env (2 ms attack) * (0.35 + 0.65 kDecay (0.9 s)) * kRel` (`kbLevel`), release to -80 dB in 0.3 s; a re-struck key starts from the level it is at; the breath noise becomes a 4 ms hammer knock. Pitch, glides, slots, tone filter and soft ceiling are the reed's. `PitchPipeDef::keyboard` (saved), `AppContext::applyAux`, the window's `keyboardToggle` (bottom row, followed by its timer).
- Tests: "2.101 a snip (F1)..." (cut, flags, numbers, curves, fade times, refusals, undo / redo round trip, a split next to it, a removal next to it); "2.101 the pitch pipe's keyboard sound" (pitch within 0.05 Hz, held level 20 - 55 % of the strike, release 0.2 - 0.35 s).

## 2.100.0: name matching through end numbers; import "one set of tracks"; closing a playing / recording window

- `Project::trackNamedIn (name, channels, except)`: the longest track name that `name` ends with (lower case, trimmed; at the first step strictly shorter), else the same after `dropEndNumber` (digits with at least one of space / `_` / `-` / `.` before them - "SPOT4" keeps its 4) taken off the end, up to 8 times; `channels` > 0 limits it to tracks with that many channels. `trackNamedAtEndOf (id)` is now `trackNamedIn (its name, 0, itself)`, so the take window's name menu and *Move the clips of all...* handle "Rorate Caeli SPOT4 19 18" / "Rorate CaeliROOM 414 01". `Project::withoutEndNumbers` (static).
- `TakeWindow::moveAllByName`: the question has one line per target track in project order ("AB  <  first source and N more") instead of one per source; `askToDeleteTracks` lists at most 14 names, then "and N more".
- `FixTools.cpp` `ImportDialog`: `byNameBox` (setting `importByTrackName`); `refreshSummary` shows target  <  sources when ticked; `finishImport / finishImportToEdit (..., byName)` -> `mapTracks (..., byName)`: an import track with no exact name + channel match goes on `trackNamedIn (name, channels)`, else on a new track named `withoutEndNumbers` (one per lower-case base + channels, `made`); afterwards a take with two parts on one track gives the second a new track of its own. `makeTrack` (the free-input search) factored out.
- `Main.cpp` `transportWindowMayClose (key)` (the ToolWindow close callback and `closeFromKey`): "take:" / "edit:" keys only; a take window that is `recordTarget` or holds `engine.currentTakeId()` while recording is not closed (`showError`); `stopPlayback()` when `playInfo` is that take window's (Kind::Take, `windowId`) or that edit's (Kind::Edit, `id` - an edit's `windowId` is its take window's, so closing the take window does not stop the edit). `closeWindowLater` (project changes, deletions) is unchanged.
- Tests: "2.99 the track named in a name, with the numbers at its end taken off" (`trackNamedIn` incl. SPOT1 / SPOT11, glued names, channels; `withoutEndNumbers`).

## 2.99.0: clips moved from one track to another in a take window; deleting emptied tracks

An import named its tracks "VivienneSPOT1" ... (a prefix stuck to the names the project already had), so every clip went onto a new track. A take's files are `TakeFile`s with a `trackId`; moving clips is changing that id (and `trackName`) - the files and the take's place in time are untouched.

**`Project` (core).**
- `canMoveTakeClips (window, from, to, takes)`: "" or why not - no window / track ("Nothing to move."), the same track, either one the LTC track, none of the takes with a clip on `from`, a take that already has a file on `to` (up to four named: "take 002, ..."), a clip whose `numChannels` is not `to->channelCount()`. Every case that refuses moves nothing.
- `moveTakeClips (window, from, to, takes, why, editPieces*)`: the check, then per take the file(s) on `from` -> `to` (`trackName` = the track's name), the take's `waiting[].tracks` mapped; `editTracks` mapped when `editTake` was one of them; `unhide (to)`. Every edit's `regions` and `overdubs` with a `takeId` that moved and no file on `to` already have their `RegionFile`s on `from` moved too (and `waiting.tracks`); an edit with its own `trackIds` gets `to` inserted after `from`. One `changed()`, so one undo step; returns the number of takes moved.
- `trackNamedAtEndOf (track)`: the longest other non-LTC track name that the (trimmed, lower-case) name ends with and is shorter than it.
- `trackHasAudio (track)`: a `TakeFile` in any take window, or a `RegionFile` in any edit's `regions` / `overdubs`.
- `removeEmptyTracks (ids)`: erases those tracks that are not LTC and have no audio (`trackHasAudio`), one `structureChanged()` (what `removeTrack` does, for many). Dangling ids left in `hiddenTracks`, `editTracks` and edits' `trackIds` are as after the Project Designer's delete (`editTracks()` skips them).
- `restorePiece`: a take window's files get their `trackId` / `trackName` back from the snapshot, matched by file (undo of a move; before 2.99 the track of a file was never restored) - but not onto a track that no longer exists. For an edit, a `RegionFile` whose snapshot track is gone keeps the track it has now (looked up by region id + file path in the live edit), with its `waiting.tracks`, and the edit's own `trackIds` gets that track in place of the gone one: otherwise move -> delete the old track -> Ctrl + Z left the clips invisible and the edit asking to bring the deleted track back (`checkMixerStrips`).

**`TakeWindow.cpp`.**
- Ctrl + Shift + mouse down on a track row inside a take (`mouseDownImpl`, before the Ctrl + click branch): `moveFrom` = the row's track, `moveTakes` = the selected takes with a clip on that track if the take under the mouse is selected, else that take; `beginDragAutoRepeat (40)`. `dragMove`: the row under the mouse (clamped to the visible rows), `moveMoved` after 3 px or a row change, repaints only the old / new row strip and the label, scrolls when within 10 px of the top or bottom of the rows (4 px to a row per tick). `paintMoveGhost`: a rectangle per take on the target row and the target's name plate, blue (`0xff29b6ff`) or red (`0xffff5a52`, `moveWhy` = `canMoveTakeClips`), clipped below the sticky top band; the label says "Move N clips: FROM  >  TO" or the reason. `mouseUpImpl`: no movement = Ctrl + click (toggle the take); a new row = `moveClips` (refuses while recording, stops playback, the notice line says what moved and how many edit pieces went along).
- `selectAllTakes()` (Ctrl + A in `TakeWindowComponent::keyPressed`; not the take being recorded). Modifier hints for Ctrl and Ctrl + Shift.
- Track name menu: *Move its N clips onto <trackNamedAtEndOf>*, a submenu of every shown non-LTC track, and (more than one pair) *Move the clips of all N tracks named like this...* (`nameMatchPairs` / `moveAllByName`: asks with the list, checks each pair, moves; what could not move is listed). The tracks emptied by it (`trackHasAudio` false) are offered to `askToDeleteTracks` ("Keep them" leaves them). *Delete track 'X' from the project...* when `deletable (X)` (not LTC, no audio anywhere), *Delete the N selected tracks...* when every selected track is. `askToDeleteTracks` asks (names listed), refuses while recording, `removeEmptyTracks`, takes the ids out of `selectedTracks`.
- `Uikit.h`: `confirmAsync` takes the second button's text (`noText`, default "Cancel").

**Tests (core_test).** "2.99 clips moved from one track to another in a take window (an import that put them on tracks of their own)": `trackNamedAtEndOf` (longest match, spaces / capitals, LTC never), a three-take move with an edit piece and the edit's own track list, one undo step back and redo, the refusals (busy take, stereo, LTC, same track, no clips), a move of only some takes, a hidden track shown again, `trackHasAudio` (clips, an overdub), `removeEmptyTracks` (only the empty ones, never LTC). "2.99 undoing a move after the old track was deleted": the take's clip and the edit's piece stay on the new track, the edit's track list has it, `missingTracksOf` is empty; with the old track still there, undo puts the clip back on it.

## 2.98.0: the sweep of 2.72 - 2.97 (the control-port crash), Stream Deck dials

Seven read-only reviews (control port and Stream Deck; windows x 2; engine; project / save; FixTools / Noise mode; DSP cores), each finding checked in the code (many with a probe program) before it was fixed.

**The crash.** `RemoteControlServer::broadcast` did `const auto data = (json.replaceCharacters (...) + "\n").toUTF8();` and wrote `data` to every socket: the `CharPointer_UTF8` pointed into the temporary `String`, destroyed at the end of the statement (`strlen` on freed memory, every 50 ms for a desk client). Reproduced under ASan. The rest of the old server had more: `StreamingSocket::write` (a single blocking `send`) on the message thread, so a controller that stopped reading could freeze Fermata; a partial write was never finished (the next message was written into the middle of a line); `lastSent` cleared from the accept thread while the message thread read it; a dead client was never closed; `~Client` closed the socket under its own thread.

**`core/RemoteHub.h` (new, header only, juce_core only).** `remote::Hub` (the accept thread: `waitUntilReady (true, 200)`, `sweep()` deletes finished connections outside the lock, at most `kMaxConnections` = 8) and `Hub::Connection` (one thread per controller doing ALL its reads and writes). The socket is made non-blocking by `sock->read (&probe, 0, false)` (JUCE's `readSocket` calls `setSocketBlockingState (false)` for a non-blocking read); the loop swaps `outbox` into `out` under `outLock`, `waitUntilReady (false, 5)` then `write` (a part is fine: `sent` advances, the rest goes on the next pass), then `waitUntilReady (true, 0 or 10)` and `read`, lines split on `\n` and handed to `onLine` (on the connection's thread; longer than `kMaxLine` = 64 KB thrown away). `broadcast (line, onlyDesk)` makes its own `std::string` (newlines inside a message replaced), appends `\n` and `queue`s it to each connection; over `kMaxQueued` = 4 MB waiting sets `overflow` and the connection ends ("it stopped reading"); the plug-in reconnects and is sent everything afresh (`takeNewConnection()` -> `lastSent.clear()`). SIGPIPE ignored on POSIX. `fermata-log.txt`: "Control port: a controller connected (n connections)" / "... disconnected (why)", and when the port cannot be opened.
**`ui/RemoteControl.h/.cpp` (rewritten round it).** `remote::Hub hub` is the LAST member (built with a lambda that calls `handleLine`); the destructor sets `*alive = false`, stops the timer, removes the peak listener, then `hub.stop()` (every connection thread has ended before anything else goes). `handleLine` answers `desk` itself (`Connection::setWantsDesk`, `deskWanted` atomic) and posts every other command to the message thread (`callAsync` checking `alive`). The pan command (`panStep`, `findStrip`, `panMixer`, `Command::side / centre`) is removed: no plug-in key has sent it since 1.5. `RemoteDesk.h`: `stageStep`'s mic index check is `isPositiveAndBelow`.
**Tests.** `tests/remote_hub_test.cpp` (own `main`, juce_core only; added to `tools/sandbox-check.sh`): lines in (split across reads, CR LF, blank lines, the desk flag), lines out (to all / desk only, newlines inside a message), a long message whole and in order, a controller that closes, a controller that stops reading (never holds `broadcast` up, let go after 4 MB), the connection limit and stop / restart, three threads broadcasting while controllers come and go. Clean under ASan, UBSan and TSan.

**Stream Deck plug-in 1.8.0** (`com.fermata.streamdeck.sdPlugin`). Three Encoder actions with drawn icons: `dtime` (`renderdial.clock`: `state.timeOfDay`, the computer's clock greyed without Fermata), `dsession` (`renderdial.session`: `hasSession`, `sessionLeft`, `sessionText`; amber under 600 s, red with "OVER TIME" below 0), `dtake` (`renderdial.take`: green "The next take is:" / red "This take is:" with `takeNumber`, from `recording`). They come from the classic `state` message (`RemoteProtocol.h` `State`: `timeOfDay`, `hasSession`, `sessionLeft`, `sessionText`), so they work without the desk messages; turning and pressing do nothing. Plug-in tidying: the old pressed-input and pan code gone (`plugin.js` rewritten round `colOf`, `STRIP_PER`, the `ENCODER` set; `renderdesk.inputTile (st, desk, index, meter)`, `gainKey`), `link.js` drops a line buffer over 8 MB, `config.json` without `panPerTick`, `test/run.js`'s XL part rewritten (passes in both scenarios, three runs).

**Fixes from the reviews** (file: what):
- `Main.cpp`: `closeFromKey (key)` - a window closed from the Stream Deck (mixers, Meterbridge, the `deskWindowToggle` close branches) is not closed while `ModalComponentManager` has a modal component up (a popup menu or alert of that window would run its callback on a deleted window: the edit window's menus were reachable); a notice says why. `deskKeyFor ("editwin")` = the edit window with the highest `lastActive`. The unused `deskMinimisedSome` hook removed. `shutdown()` deletes any modal `DialogWindow` (the fix tools) before `ctx->shutdown()`: JUCE deleted them after the app object, and their destructors called into the dead `AppContext` (stopPlayback, discardUncommitted, undoHold) - and deleted the files of fixes the final save had just written.
- `AppContext`: `afterProjectReplaced` closes "pitchpipe" and "click" (their controls showed the old project's settings); doc comments of `deskWindowState`, `frontMixerId`, `saveHold`, `openBackupProject` corrected. `missingAudioList (atMost, quick)`: with `quick` (as `missingAudioCount (true)`, used at open) the files of verified takes in a folder that is still there are not asked about (`QuickAudioCheck` in Project.cpp, shared by both).
- `Surface.cpp/.h`, `SurfacePanel.h` (S1): the timer's early returns (Off, no project, no mixer) `chans.clear()` - the raw `StripState*` / `BusState*` were kept and written after the mixer was freed; `handleAsyncUpdate` looks the mixer up again before acting; `handle (const mcu::Event&)` no longer takes the MIDI message; `touched` is reset when the ports close; `resetMap` keeps `meters`; `kNoPort` ("none") makes the explicit "(none)" choice stick (`statusText` knows it); the LCD is sent as one SysEx per line (`tops` / `bottoms`; the bottom line uses `lcdCell (dbText)`) instead of one per strip.
- `TrimWindow`: `slipLeft / slipRight` read `app.trimSlip*`, the timer syncs the boxes, a click uses the other box's current value; a redundant `extraZoom` line removed. `EditWindow`: `clickTrackName (juce::Uuid id, ...)` takes the id by value and works on copies (the rows can be rebuilt under it); a Shift range takes only audio-track rows; SafePointer guards in the Colour, Select none, lane-label, Tracks and feedback `getEdit` callbacks. `TakeWindow`: `BarsPopup` keeps `origIn / origOut / origNotes` (`typed()`), `keepAndCloseBarsPopup()` (a click elsewhere keeps what was typed), `barsGroup`; double-click on the audio uses `groupAt (pos, true)` (works on the take being recorded); `showSettings` lost its unused `fromBars`.
- `Project.cpp/.h`: the room-tone fields (`roomTone`, `roomToneIn`, `roomToneOut`) are in `undoHash (TakeWindowDef)` and the `restorePiece` merge (the `.fmtake` was not rewritten when only the mark changed); `restorePiece` keeps `te->laneRows = live->laneRows` (stripped from the undo text with the other view keys, so Undo reset every node to one row); `load()` clears `windowFiles`, `missingWindowRefs`, `loadNotes`, `oldWindowFiles` only once the file has been read (a failed Open left the open project to write "(2)" copies; another project's leftovers could be deleted by this one's save); `lowCutFromVar` clamps `hz`; `createDefaultDesign` clears `layouts`; the duplicate `preampDevices.clear()` removed; `driverNamePrefix()` cached (`findDriverNamePrefix`, keyed on a hash of the driver names: `inputShortName` / `inputTag` recomputed it per input, O(n^2), 51 ms a pass at 256 inputs, every 50 ms with a desk client); `toVar` lost its unused `editorialOnly` parameter.
- `AudioEngine.cpp/.h`: `buildPlan` re-prepares a Track's or Int Bus's low cut when `preparedRate() != sampleRate` (strips made after the device started kept the 48 kHz design); a cue mixer that follows the processing mixer writes `remotePkL / R` next to `meterL / R` (its Stream Deck meter was dead); `setCrMicInputs()` writes each `crSel` flag once (it cleared all 256 then set the mic ones, 60 times a second: a block could read a flag in between and drop the talkback mic); the automation lateness of a Track includes its spot-mic `alignSamples` (slots, `autoLatIn / Out`, sends, VCA lateness - NOT `inLat / outLat`, which line the paths up: the alignment is on purpose). `Bounce.cpp`, `MasterRender.cpp`: `juce::ScopedNoDenormals` (the double-precision IIR low cut ran into denormals in silent tails).
- Low cut: `Mixer.h` comment (zero latency by default since 2.86), `LowCut.h` unused `partition()` removed, `LowCutPanel.h` tooltip, and `swallow = false` at the start of each press (opening the panel sends mouseExit, so the click that was to be swallowed never came and the next real click was).
- `MixerWindow`: `insertOnLinked` and the `handle()` load branch copy what they need and check a SafePointer after each `make()` (a plug-in with a licence window runs a modal loop in which the 3 Hz timer can rebuild the strips); `refreshCounter` is a member (a `static int` was shared by every mixer window); a duplicate pan line removed.
- `AlignWindow`: `setSource` stops a running job (its results were saved into whatever piece the window showed when it finished); `Job::run` catches `bad_alloc` and other exceptions into `result.error`. `MasteringWindow`: `LevelBox::write` only reads the box when the mode has not changed; `summary()` and unused members removed.
- Waveforms: `WaveThumb` `failed` flag (a file that could not be read was marked fully loaded and saved flat); `WaveStore` saves only `if (t.saveTo (out))`; `WaveDraw`'s synchronous read on a jump is `kBehind / 4` and `kAhead / 4` (the read-ahead thread does the rest).
- `AudioOps.h`: `AudioJob::release()` (`endLock`, `released`): the window lets go of its job; `run()` posts `done`, marks finished and `deleteSoon()`s itself if released. `FixTools.cpp`: `JobDialogBase` holds `AudioJob& job = *new AudioJob()` and releases it (closing a window no longer waits up to 20 s for the worker, which JUCE then killed); `runJob (..., ifGone)` deletes the files of a job that ends after its window; try/catch round the detail-picture threads, the Noise mode detail picture capped at 512 MB and read track by track; the room-tone box minimum is 0.5 s everywhere (what `learnDenoise` needs); the leftover multi-box code of 2.80 removed (Shift / Alt kept extra boxes that every tool then ignored); `struct PreviewApplied` raises `app.saveHold` while a preview is applied (five state structs; `saveHold` had been raised by nothing since 2.81.1, so the autosave wrote preview files into the project).
- **De-click** (`SpectralRepair.h`): the short-area path (the area is the click, mended whole by `interpolate`) is taken only when `t1 - t0 <= min (0.03 * sr, 1500)` - `interpolate` refuses gaps over 1500 samples, so at 96 / 192 kHz an area of 15.6 / 7.8 - 30 ms did nothing at all; the per-thread report is reset at the top of `declick` and counts the short mend; the last stage's put-back (`repair()` with Attenuate, 512-sample window, rewrites up to +-512 samples round the gap) snapshots `[g0 - 1024, g1 + 1024)` instead of +-64 (putting back only the middle left a step of 9.5 x the normal third difference at the edge). A no-op loop removed.
- **Denoise** (`AudioOps.cpp` `denoiseBox`): through `denoise::processChannel` on the in-memory stretch (15 s pieces, 6 s before, 2 s after; a box of up to 15 s is one piece, bit for bit as before) instead of one `processSegment` over the whole box: ~100 bytes a sample, 5.8 GB a channel for a 10-minute box at 96 kHz. Test: a 22 s box against one go, 66.5 dB apart.
- **Noise mode** (`FixTools.cpp`): `prepareRepairJobs` widens the piece put in by 0.25 s each side of the changes, within the region (`applyRegionJob` leaves out a piece under 60 ms - a click box - and centres its 30 ms joins on the piece's ends); the changes that look for lost samples get `lostLo / lostHi` 35 ms inside the piece; `findLostTogether` finds them on all the tracks of the piece together (10 s at a time, every track, 0.25 s context; `LostSamples::find`'s vote needs a fifth of the tracks), and sets `lostKnown`, as the ReTouch window does - per file, each file voted alone. `rtParts`: the `TrackParts` as they were when L was pressed (the room-tone box is learned from that sound; `revert` falls back to `cur` if it took that step's files). The ReTouch window's picture learns the box from `history.front().tracks` (the loaded sound), as the written files do. `learnFileProfiles` reads only the boxes (+-0.25 s) of each piece, not the whole piece. `attachRoomTone` refuses (with the track's name) when the project's room tone has no usable profile for a track of the piece or another sample rate (the file was left as it was). `GuardedDialogWindow` / `launchDialog (..., mayClose)`: Noise mode's and the ReTouch window's X / Escape / Finished / Cancel ask first when fixes are not written back (Noise mode: keep (`keepSteps`, as Write back) / throw away / cancel; ReTouch: throw away / cancel); a `StreamWindow` ignores Escape (JUCE's `escapeKeyPressed` hid it, leaving it open and out of reach). `sliceForView` tests `pyramidBusy` before `pyramid->valid()` (the job's `setUp()` refills `levels`).
- ReHarmoniSer `Engine.cpp`: "Nothing to measure: the box is silent on every track, or every channel is turned down in the visual mixer." (both throws); a leftover `hopS` removed.
- **Removed (only the tests used it since 2.81.1):** the whole-file denoiser of 2.77 - 2.80 - `audioops::denoiseFiles`, `denoiseTracks`, `denoiseFile`, `denoisePiece`, `DenoiseRun`, `denoiseFeather`, `parallelFor`, `learnDenoise`'s `decaySeconds` and `DenoiseProfiles::decay`; in `Denoise.h` the cross-track parts (`presenceSegment`, `dilate`, `othersMax`, `othersAgreed`, `finish`'s `others`, `Params::groupMode / groupSureLo / groupSureHi`) and `hfProtectHz / Db`. `processChannel` and `measureDecay` stay (the reference tests; `denoiseBox` uses `processChannel`). The 2.77 / 2.79 tests that went through them now test `learnDenoise` + `applyFix` (per-channel profiles of a file) and `processSegment` with a limited profile. `DenoiseFft.cpp` no longer defines `POCKETFFT_NO_MULTITHREADING` (Dsp.cpp includes the same header without it: two bodies for the same inline templates; every call is single-threaded, no pool is made). Also: the old De-click window mode (`repairTake / repairEdit`'s `declick`, `RepairDialog::declickOnly`, `modeBox`), `RepairDialog::previewValid / pvTracks / repSpec / sensCapRow / patchMode`, `SpectrogramView`'s marks 1 / 2 and `cueTime / totalSeconds / detailWanted`, `TrackParts::from / to`, `refreshPicture`'s `then`, `SpectralDenoiseSettings`' unused fields, `SpectralPyramid::frameTime`, `MeterSettings::riseCoef / fallCoef`, `Icon::Buses`, `WindowRef::kindName`, `pdf::Document::textCentred`; stale comments in `Denoise.h`, `Playback.h`, `MainWindow`, `AudioOps.h`, `FixTools.h`; a handful of -Wshadow / -Wreorder warnings in the files touched.

**Tests added (core_test).** "2.98 the sweep: lane rows survive Undo, the room-tone mark is an undo step, a failed Open keeps the open project, a new project forgets the layouts"; "2.98 the sweep: De-click on a short area at 96 and 192 kHz, and its report; a long Denoise box in pieces"; the room-tone undo-hash line; the LC damaged-hz line; the spot-mic automation lateness lines (in the 2.89 alignment section).

**Looked at and left as they are (decisions):**
- S1: its MIDI is still sent from the message thread (only batched per line now). A stalled MIDI driver could still hold the UI; moving it to a thread is a bigger change.
- Linear-phase low cut: all of a partition's FFT work falls in one block (bursts of 0.8 - 1.3 ms at 64-sample blocks with the lowest settings and slopes); spreading it over a helper thread is a design change for a later version. Zero latency is the default.
- `mixSnapshot / applyMix` do not carry the hidden strips (a new edit mixer, Copy mix, an imported `.fmmix` show a hidden track again) - may be what is wanted; ask.
- `inputDeviceLabel`'s "not connected" fallback also runs when the driver is connected and the input belongs to no device, and the last device set up claims every later input (a MADI / Dante input can read "Hapi-29"). Not changed without a real driver to check against.
- A manual full Save while a ReTouch preview is applied still writes the preview into the project (only the autosave waits).
- Noise mode's pieces next to an edit point: the cross-fade of the edit point uses the take's audio past the piece, which the fix does not reach (as before; the ReTouch window's two streams are the tool for fixes across an edit point).

## 2.97.0: Stream Deck open / close keys

`{"cmd":"openwin","what":"editwin|takedisplay|organiser|media|mastering"}` (`remote::isOpenWin`) -> `deskWindowToggle`: not open: open it (`openEdit` of `recentEdit`, `showTakeDisplay`, `showOrganiser`, `openMedia`, `showMastering`); shown: `closeWindowLater`; minimised: back. `deskKeyFor ("editwin")` = `edit:<recentEdit or the first edit>`. The `desk.win` object has the five new names (0 / 1 / 3).

## 2.96.0: Tab in the name boxes; Stream Deck window keys

**Tab.** The alias editors' `onFocusLost` -> `Project::setInputName` -> `structureChanged()` -> `MainComponent::changeListenerCallback` -> `rebuildRows()` destroyed every row, so the focus Tab had just handed to the next editor was lost (unchanged names return early, which is why Tab worked when nothing was typed). `rebuildRows()` now notes which input / output alias editor has the keyboard focus (at that moment the NEW one, JUCE sets the focus before it tells the old one) and gives it back to the same row index after the rebuild (`InputRow::aliasHasFocus / focusAlias`, same on `OutputRow`).

**Window keys.** `{"cmd":"wact","what":"mark1|mark2|align|search|trim|markers|feedback|slipl|slipr"}` (`remote::kWindowActs`, `isWindowAct`); the desk message carries `wact: {what: -1|0|1}` (-1 not available now, 1 lit). `AppContext::deskActState / deskAct` (Main.cpp: slip keys act on `trimSlipLeft/Right` directly, the rest go to the `TakeWindowComponent` / `EditWindowComponent` of `lastWorkWindowKey()`, `deskActState / deskAct` in each: mark1/2 = `editMarkIn/Out` or `setFixMark`, markers / feedback = `triggerClick()` of the toggle, trim = `trimSelected`, search = `toggleBarSearch`, align = `app.showAlign`). Plug-in 1.6.0 draws the program's own icons: a small JUCE program (`drawIcon` into a 160 px image, light and dark ink) made PNGs that are embedded in `bin/icons.js`.

## 2.95.0: double-click a take, `Project::inputTag`

`TakeTimeline::mouseDownImpl`: `getNumberOfClicks() > 1` on the audio area (right of the name column, below the banner) calls `showBars`; the banner's double-click (`showSettings`) is unchanged. `Project::inputTag (i)` = `NN-DEVICE-CC` from `inputDeviceLabel` (the last `-` splits the device and the channel), else `NN-` + `inputShortName`; used by `MainWindow` (input rows) and the desk message's `d`. `inputShortName` / `inputLabel` are unchanged (pickers). Test: "inputTag" lines next to the `inputDeviceLabel` checks. Plug-in 1.5.0 (strip layout) is plug-in only: it uses the 2.94 commands.

## 2.94.0: notes on a take; the Stream Deck + XL (control port)

**Take notes.** `TakeGroup::notes` (`Takes.h`). `Project.cpp`: property `notes` on the take group (saved only when not empty), loaded back, part of `undoHash (TakeWindowDef)` (so Undo / Redo of an edit made in the box works), copied in the live-take merge (`lg->notes = tg.notes`). `TakeWindow.cpp`: `BarsPopup` is now `(title, bin, bout, notes, ok (int, int, const String&), more, close)` with a `NotesEditor` (a `TextEditor`, multi-line, Ctrl + Enter = OK); `showBars` passes `g->notes` and calls `applyNotes (gid, notes)` (the undo step is the same as for the bars); `TakeTimeline` is a `TooltipClient` and gives the note as the tooltip of the take under the pointer; the banner draws a small note icon. `TakeSheet.h`: `Row::notes`, CSV column "Notes", PDF line "Note: ...".

**The control port, Stream Deck + XL part.** Files: `core/RemoteProtocol.h` (messages, `Command` with `input`, `what`, `delta` (clamped +-100), `side`, `centre`; `State` now carries `deskProtocol: 1`), `core/RemoteDesk.h` (new: plain functions on the `Project`, all tested in `core_test.cpp`), `ui/RemoteControl.cpp/.h` (the server), `AppContext` hooks, `Main.cpp` (windows).
- *Protocol.* `{"cmd":"desk","on":true}` makes this connection a desk client (handled on the client's thread, sets `Client::desk` and `deskWanted`). Desk clients get (a) `desk` whenever it changes (compared as text, checked every 50 ms): `inputs` (`inputsToVar`), `mixers` (`mixersToVar`: slot 0 processing, 1 - 5 cue), `stage` (`stageToVar`), `win` (bridge, pitchpipe, click, take, minimiseall: 0 not open, 1 shown, 3 minimised / "there are windows to bring back"), `arm` (0 none, 1 some, 2 all), `wc`, `loop` (-1 no take / edit window), `rec`, `panMixer`; (b) `meters` every 50 ms: `in` (every input's peak, whole dB, -100 = silence) and `mx` (6 x [L, R], the main output of each mixer slot). Classic `state` is unchanged (the Neo). Commands run on the message thread (`execute`), and only when a project is open.
- *Meters.* `PeakListener::peaksUpdated` (30 Hz) accumulates `max` of `AppContext::inPeaks` and of the new `BusState::remotePkL/R` (written by `AudioEngine` in the meter loop for Ext Bus nodes, read with `take()`), so a short peak between two messages is not lost. `collectMeters` sends and clears the accumulators.
- *The dials' mixers.* `mixerIndexForSlot` (slot < `cueEnd()`), `mainBusOf` (the first Ext Bus ticked MAIN, else the first Ext Bus), `faderStep` (-80 .. +12 dB; all the way down = -100 = off; from off the first step lands on -79.5), `mixOutStep`, `mixOutMute`. The pan follows `panMixer()` = the mixer in front (`frontMixerId`), else the first one.
- *Stage panel.* `stageStep` (playback / mic1 / mic2 / out, -60 .. +10 dB, reset to 0) then `AppContext::applyStageMix()` (hands the project's levels to `engine.setStageMix`); `StageMixer::syncFromProject()` runs from its timer so an open panel follows. Strips have `kind` (0 mic, 1 playback, 2 output) and `input`.
- *Inputs.* `preampKey` (mic | v48 | pol | lc | pad: the same steps as the main window, via `Project::setPreamp`; the Pad / Z-HI rules of `PreampSettings` are respected), `gainStep` (0 .. 66 in half dB), `monitorCycle` (all tracks fed by the input, E > S > S-L > T), `panStep` (mono: `pan`; stereo: `panL` / `panR` with `side`; surround: none), `tracksOnInput`. `talkin` toggles `setCrMicInput`.
- *Windows (Main.cpp).* `deskWindowState / deskWindowToggle` for "bridge", "pitchpipe", "click", "take", "minimiseall"; the take window used last is found by `deskTakeWindowId`, minimised with `minimiseToDock` (the DockHub's way, so it can be brought back from the dock). `minimiseAllButMain` hides every window in `windows`, the audio settings and the plug-in editor windows and remembers them in `deskMinimised` (most recent last), the next call restores them and brings the main window to the front. `AppContext::remoteArmToggle` (not while recording; all armed > disarm all, else arm all), `remoteWaveColour`, `deskLoopState / deskLoopToggle` (`TakeWindowComponent::toggleLoop`, same in `EditWindow`).
- *Tests.* "2.94 notes on a take..." and "2.94 the Stream Deck + XL: the commands, the preamp keys, gain, pan, monitor, the mixers' main outputs, the stage levels" in `core_test.cpp` (a fake preamp driver). The plug-in side is tested with `test/run.js` in the plug-in folder (pretend Stream Deck and pretend Fermata). Not tested against a real Stream Deck + XL.

## 2.93.2: playhead stutter when zoomed in (DirectWaveCache)

**Cause (from the user's two `fermata-log.txt`s, 2.92.0 build).** The playhead line of the log (`Edit window playhead: ...`) showed, around 237-238 s, a run of about 65 redraws of 12-20 ms and one 33 ms late refresh in each of four plays; at 305-315 s (zoomed in further: "more obvious the more you zoom in") 103 of 454 redraws over 12 ms, worst 128 ms, 102 late refreshes, and in a longer play 378 slow redraws, worst 340 ms; playback underruns 0 throughout (so the audio was fine, only the screen). The playhead redraws a thin strip (48 px or so) of the timeline 60 times a second, and that strip's waveforms are drawn by `DirectWaveCache::draw` when fewer than 256 samples share a pixel (`spp < 256`). `ensure` held, per file, only the strip's samples plus a third of the strip on each side, and re-read (open the file, read, close: `createReaderFor` per read) whenever the strip left that. The playhead moves 1600 samples (96 kHz) per frame and the margin is `strip px x spp / 3`, so zoomed in (spp 10-100) that is EVERY frame for EVERY track (reads per second per file = 288000 / (strip px x spp)), and in a crossfade (two pieces, twice the files) twice as many. Measured in the render harness (Big Session, 300 strips of 48 px, zoomed in: `rend/Main_r.cpp` FERMATA_STRIPBENCH=2): 2000 file reads for 300 strips. On Linux with a warm cache that is 3.3 ms a strip; on Windows each open can wait on the virus scan, which fits the 12-130 ms redraws.

**Fix (`WaveDraw.h`).** `Entry` holds a `Chunk` (`cur`: samples, start, file length, file date) of `kBehind` (65536) + the strip + `kAhead` (524288) samples (about 5.5 s ahead at 96 kHz; at most `kMaxSamples`). `prepare()`: from memory if `cur` covers the strip; else (a jump, a new zoom, the first draw) read at once, as before but big; and when less than `kAhead / 2` is left ahead of the strip and nothing is on its way, `Reader` (one `juce::Thread`, low priority, a queue of `Job`s) reads the next chunk into an `Ahead` (`done` atomic, release / acquire) which `prepare()` adopts when it covers the strip. A read nobody waits for any more (`out.use_count () == 1`: the cache let it go) is skipped. `trim()` lets go of the least recently used chunks beyond 256 MB in all. The file's date is looked at once a second and a changed file drops its chunk (a fix or bounce rewrote it); `forget()` as before. `~DirectWaveCache` stops the thread first. Same cache class in the Take window.

Measured after (same bench, paced at 60 strips a second like a real play): 20 file reads for 300 strips (the first strips of each file), 1 strip over 12 ms (the first one, 50 ms: the synchronous first reads at the start of a play or a jump; before the change 2000 reads), median / p95 unchanged (the drawing itself was never the cost). `DirectWaveCache::readCounter ()` (a static atomic) counts the reads; the playhead line of the log now ends with "waveform file reads: N" for the play.

What this does not touch: the zoomed-OUT case (`spp >= 256`, thumbnails, never reads the disk) and the first synchronous reads. The slow redraws near 237 s in the first log may have been this (the zoom is not in the log); if redraws over 12 ms remain when zoomed out, the new line tells them apart (reads 0 = not this).

## 2.93.1: tab lighting, fixed action button, keys on the tabs

**Why ReTouch stayed lit (`FixTools.cpp`, the spectral window, Noise mode, `PitchTabs`).** The tabs were `setClickingTogglesState (true)` + `setRadioGroupId`, with `onClick = selectTab (i)` and `selectTab` doing `tabBtn[t].setToggleState (true, dontSendNotification)`. JUCE's `Button::setToggleState (on, sendNotification)` (what a click does) calls `turnOffOtherButtonsInGroup (sendNotification, ...)`, and that turns the other tabs off WITH their click action: the tab that was on (ReTouch, the first one, on by default and again whenever ReTouch was last chosen) ran `selectTab (0)` from inside the turn-off, and that lit it again after the group loop had passed it. Result: the clicked tab and ReTouch both lit, `tab` right (the clicked tab's own onClick runs last). Reproduced in the Xvfb render harness with real `triggerClick()` clicks (the key path, `selectTab` from `keyPressed`, never showed it: no notification, so earlier renders looked right). Fix: no radio group, `setClickingTogglesState (false)`, and `lightTab()` sets every tab's state by hand (`i == tab`, `dontSendNotification`); `setupTabs`, `loadControls` and `selectTab` all go through it. `PitchTabs::show` the same. (The other radio groups - `ClickWindow`, `MixerWindow`, the monitor keys, `BounceWindow` - end with the clicked key's own action, which runs last, so the final state is right there even though the turned-off key's action runs first.)

**One place for the button that does the work.** `placeTab (in, rows)` takes the panel's row count and cuts `action` (`kActionW` = 116 wide) off the right of the last row once; the four tools' buttons take `action` whichever tab shows (ReTouch's narrow layouts already ended on the last row). `kClickW` / `clickRows` are gone (De-click's slider is on the first row now, so it never needs a second row), `kAmountsW` uses `kActionW`. The panel height is `max (2, reTouchRows)` as before. Gain: the button in the corner, its hint on the first row (shortened to one line). `kTabW` 84 -> 104 for the keys on the tab names.

## 2.93.0: de-click joins, key 4 overwrite / Shift + 4 insert, Noise mode tabs

**De-click joins (`SpectralRepair.h`).** Symptom (the user's 15 stems of a 96 kHz edit with an acoustic room click, de-clicked at a high sensitivity): the output had click-like spikes the input did not have. Mechanism: `mendGapOnce` (the old `mendGap`: 160-pole LS interpolation, then the level corrections) replaces the gap with a prediction. The gap is cut where the finder's residual stops being above normal, but a smooth click's tail (low in pitch, invisible in a residual of a 32-pole model) goes on past it, so the correction (new minus old) is not zero at the gap's edges: a step of 3e-5 .. 1.7e-4 in sound with hiss around 2e-6, i.e. 15 - 27 times the normal third difference. `declickPass` pass 2 / 3 and the picture check then saw the step as a click: a chain of adjoining mends, each with its own step. (The white-noise correction `wnc` of `interpolate` makes no difference to this: tried 1e-6 .. 1e-12, identical output.)
- `thirdDiff / thirdDiffNormal / thirdDiffPeak`: the measure. The third difference is used because the curve of the music hardly shows in it (the second difference of a 700 Hz tone at -35 dBFS is bigger than the hiss; the third is not). Normal = the median of |third difference| over 600 samples each side of the gap (the gap and 6 samples beyond left out). A join that is only grain scores 1.5 - 4 (90 % <= 3.6, a few up to 8 - 11 in 240 random gaps cut into clean audio: `dcp/itest`); the steps in the report scored 15 - 27. `cleanJoin = 8`.
- `mendGap` (new) = `mendGapOnce` + a check. Per try: restore the stretch from `orig`, mend `[a,b)`, score the sharpest feature in the first / last 4 samples. Over `cleanJoin`: widen that side by `max (6, (b-a)/5)` (at most `room = max (24, m0)` beyond the original gap, never over 1500 samples in all) and try again, up to 6 tries (1 for a gap over 400 samples: the Cholesky is O(m^3)). The best try is kept. If it is still over `cleanJoin`: `spreadCorrection` lets the correction die away outside the gap as a raised cosine from its edge value (length `clamp (2 * gap, 32, maxTail = 160)`); kept only if it scores better. If the result is still over `cleanJoin` AND not smaller than the sharpest feature in the gap before the mend (`clickBefore`), everything is put back and `mendGap` returns false (the callers already treat that as 'not mended').
- Also guarded: the last stage of `declick` (the `repair (... Attenuate ...)` on the lines still found: snapshot, put back if the result is over `cleanJoin` and sharper than before), and the area of 30 ms or less chosen by hand (mended whole as asked, `spreadCorrection` on its join if over `cleanJoin`; never reverted, never widened). `mendDropouts` and `mendLines` go through `mendGap` and so are covered.
- Measured (`dcp/` scratch harness, not in the repo): the user's 15 stems run through `fixWholeFile` at sensitivity 6 / 8 / 9 / 10: new click-like spikes (third difference > 12x the median of its 4001 window and > 1.8x the sharpest feature of the input within 40 samples) 0 / 3 / 5 / 11 before, 0 / 0 / 0 / 0 after; samples changed 12127 -> 11836 (sens 9), 282022 -> 281867 (sens 10, 195 k of them on the Room track). The user's own 'MORE clicks' output could not be reproduced exactly (the app fixes the take files with their whole context and all tracks together; the harness ran the exported stems one by one), but showed the same steps (PnL at 1.2753 s: a step of 1.6e-4 at the first mended sample, a second step 8 samples on).
- Test (`core_test.cpp`, 'declick, room-click bursts'): 2.5 s of quiet piano-like sound at 96 kHz (two notes, 10 partials with slow amplitude and phase drift, hiss 2e-6) with 12 bursts (a damped ring that rises in 0.1 ms, 300 - 2000 Hz, 1 - 4 ms long, 2e-3 - 8e-3), de-clicked at 9 and 10: at least 6 repairs, more than 200 samples changed, and no sharp feature that was not there before (old code: 4 of them).

**Key 4 (`Edit.h`, `AppContext.cpp`, `TakeWindow`, `EditWindow`).** `EditDef::overwriteRegionAt (r, sample, &replaced)`: the piece goes at `sample`, over `[sample, sample + length)`; regions are split at the end and at the start of that span (`splitRegion`), the ones fully inside are erased, the piece is put in (`insertIndex = -1`), `setDefaultJoinFades` at both joins, `hardenEnds`, `settleGapFades`, `clampAll`. Overdubs are not touched. Past the end of the edit it leaves a gap with gap fades, as `insertRegionAt` does. `AppContext::copyMarkedToEdit (..., insert)`: `atEditPlayhead && insert` = `insertRegionAt` (the old ripple), `atEditPlayhead` alone = overwrite, no playhead = append as before. `isShiftFour (k)` in `Uikit.h` (the key carries '$' with Shift on US / UK keyboards). `TakeWindow::toEdit (atPlayhead, insert)`, `sendToEdit (atPlayhead, insert)`, the **To playhead** button reads Shift on click. `EditWindow::placeFromTakeWindow (insert)`. Tests: six cases in `core_test.cpp` (inside a piece, across a join, overdub untouched, longer than everything after it, past the end, empty edit) and Shift + 4 still rippling.

**Noise mode tabs (`FixTools.cpp`, `NoiseModeDialog`).** Same structure as `RepairDialog` (see 2.92.0): `tabBtn[]` (radio group 7313), `tab` (user setting `noisemode.tab`), `tabControls`, `selectTab`, `setupTabs`, `placeTab`, `layoutBottom (area, place)`, `paint()`, `refreshControls()` (chosen tab visible, method rules, other tabs hidden, `resized()`). The old `Chunk` / `packRows` / `bottomChunks` packing is gone. Transport row: `listenW()` (five 34 px buttons, Loop, the clock) and `transportRows (w)`: Undo / Write back to clip / Finished go on the same row when `listenW + 8 + 90 + 16 + 406` fits, else on a second one. Under the picture: the info line with the scale box and Spectrogram settings, the status line (one line, 22 px), then the panel. `dirCap` is gone (the Direction box says it), the Denoise tab's room tone caption is `roomShortText()` with `roomText()` as its tooltip. Keys R, G, D, L, N call `selectTab` first.

## 2.92.0: Trim waveform size, the spectral window's tool tabs, the playhead record

**Trim waveform size.** `AppContext::trimWaveZoom` (declared at the END of the struct, so objects built before it still agree on the offsets of the other members) + `trimWaveZoomChanged` (a `juce::ChangeBroadcaster`) + `changeTrimWaveZoom (factor)` (clamps 0.25 - 40, saves `trimWaveZoom` in the user settings, sends only that broadcaster's message). `TrimComponent` listens to it (added / removed beside the project listener), takes `waves->extraZoom` from it when it is made and in `changeListenerCallback`, and handles the broadcaster's message by setting the zoom and invalidating the waves only. The Take and Edit windows still use `waveZoom` (and send the project change message, which the Trim windows no longer read it from).

**Spectral window (RepairDialog, `FixTools.cpp`).** The tools are four tabs (`tabBtn[]`, radio group 7312, the chosen one in `tab`, remembered as the user setting `repair.tab`): `tabControls (t)` lists the controls of each tab; `refreshControls()` first makes the chosen tab's controls visible, then applies the old visibility rules (the method hides Direction / Surrounding length / Bands / the ticks / Before <-> After as before), then hides every other tab's controls; it ends with `resized()`. `selectTab (t)` is also what the keys R, G, D, N, L call before they act (so the Gain pop-up opens from its button's place). `layoutBottom (area, place)` returns the height of the bottom part at a width (place = false only measures) and places it: the tool panel (`panelR`: the tabs in two columns of two on the left, `contentR` beside them, a thin divider between), whose height is the same for every tab (rows = max (2, `reTouchRows`, `clickRows`) at this width; ReTouch is one row of choices + one of sliders and the button when `contentR` is at least 736 / 844 pixels wide, otherwise two rows each), then the transport row (Play, Stop, Loop, Listen to, the progress bar `bar`, and the Fixes buttons at the right when they fit: `transportRows`; else the Fixes get a row of their own). `placeTab (in)` puts the chosen tab's controls in rows of 24 pixels; `putLeft` / `putSlider` skip hidden controls so the rest move up. `paint()` draws the panel's frame (black at 16 % over whatever the window's colour is, so the tinted stream windows work as well; gilt in the Manuscript UI). Sliders are 140 pixels with their number. `dirCap` was dropped (the Direction box names itself), `gainHint` is the Gain tab's sentence. The Noise mode dialog (`NoiseModeDialog`) keeps its own chunk flow.

**Playhead record (`EditWindow.cpp`).** `HitchLog` + `noteHitches (ph)` (called first in `movePlayhead()`, i.e. at every screen refresh) + `endHitchLog()` (when the edit stops playing: one `juce::Logger` line if the play had at least 120 refreshes). Late refresh: gap > 25 ms. Stood still: the position moved less than a quarter of the time passed over 2 refreshes in a row. Jumped: it moved more than 2.5 times the time passed plus 4 ms (a loop going round or a new play are not counted). `paint()` is always timed now (one `getMillisecondCounterHiRes()` pair) and reports redraws over 12 ms; whole-window redraws are counted in the two places that make them while playing (`changeListenerCallback` for the project, the throttled thumbnail refresh).

## 2.91.0: Re-HarmoniSer, a note struck under the voice (time-limited neighbours, capture recovery)

Symptom (soprano high + alto low + piano G3 struck at ~3.47 s inside the soprano's G4 box): the retuned soprano dipped at the strike. Cause: the piano's even overtones sit on the soprano's overtones (G3 x2h = G4 x h), so at the onset the boxed line (`trackNoteLocked` / `refineTrackLocked`) jumped ~25 cents onto the steady piano line ("capture") for as long as the piano stood out, and a piano struck mid-box was never kept apart (`findNeighbours` wanted share >= 0.7 of the note), so the retune moved its even overtones with the voice (the odd ones stayed: the piano note was bent against itself).

**Measuring (`Engine.cpp`):**
- `LineRefs` / `lineRefs()` / `persistence (const LineRefs&, ...)` / `standingRun()`: the sound of a line against the four lines beside it (+-45, +-75 cents), sample by sample; `standingRun` is the longest run of 0.15 s windows (every 0.05 s) where it is > 4x the lines beside it (>= 0.3 s).
- `findNeighbours (..., std::vector<Vec>* active)`: a line that fails the whole-note test (ratio > 4, share >= 0.7, energy > 1 %, not close to the boxed line) but has a standing run (ratio > 6, apart from the boxed line, not a rest of it) is kept as a time-limited neighbour: held flat on either side of the run, with an activity mask (0..1 on `ts`: `smoothstep` ramps of 0.1 s round the run). `active[i]` is empty for a whole-note neighbour. `peelLine (..., const Vec* act)` peels a line weighted by its activity. `combViterbi / combPath` take a jump penalty `lambda` (default 0.25; larger values were tried and did not stop the candidate lines hopping between steady lines).
- `resolveCapture (f0, part, act, ts)`: when the time-limited neighbour keeps to where the voice was just before / after its stretch and the boxed line does not (dL + 10 < dF, dL < 30 c, a jump >= 12 c at either end), the two are swapped over the stretch.
- `recoverCaptured (...)` (new, called by `measureNote` when `lookForOthers`): scans `f0` for a jump >= 15 c in 3 hops followed by a stretch that stays within 12 c for >= 0.25 s and is not where the voice was before and after (a held note is left); the stretch starts where the line starts to leave the voice. The steady sound is `steady`: `f0` on the plateau, held flat at the plateau's pitch BEFORE it (the line on its way to the sound is on the voice's own pitch and must not be peeled; this was the dip left at ~3.46 s in the first version) and flat at the last value after. It is peeled from the channels with its onset activity (`smoothstep` from `ta - 0.05`, no end) and the voice is looked for again (`combPath` +-45 cents round the expected pitch, `refineTrackLocked`). Accepted when the line's own sound at least doubles at the jump (`lvlJump >= 2`; a voice swinging in a vibrato does not; measured on the line as it was, `Sf`), the new line stands > 4x clear of its sidelines, is >= 10 c apart from the captured line, and is no farther from the expected pitch than the captured line + 4 c (<= 30 c). `f0` over the stretch becomes the voice's line (log blend, 2 hops each side); the steady line is pushed to `part` with an activity that lasts as long as it still stands above its sidelines with the voice taken out (a struck note dies away slowly), so the retune protects it until ~4.2 s in the example.
- `measureNote`: `part / partAct` (time-limited) next to `nbs` (whole-note); the two peeled sweeps use the activity; `NoteMeasure::nbActive` carries the masks (empty entry = there all through the note) in step with `nb`.

**Retune:** `retuneSegment (..., nb, nbActive)` / `separateAndMove (..., active)`: the joint sliding least squares keeps a neighbour only where its activity weights it (basis weighted by the mask, Gram diagonal fixed for the zero-activity samples). `ReHarmoniserEditor` carries `nbActive` in `NoteEdit`, `Cand`, `Res` wherever `nb` is copied and hands it to `retuneSegment` in `renderCorrection`. Every correction is still applied identically to every channel.

**Tests:** `tests/reharmoniser_voices_test.cpp`, "a note struck under the voice (2.91)": soprano blend (3 channels) 20 c above a piano G3 struck at 2.0 s (overtone gain x2) in a 4.4 s clip. Checks the boxed line against the truth before / during / after the strike (< 3 c; before the strike it stops 0.6 s ahead: a measuring window reaches the strike), that the piano is found, that a +35 c retune lands the voice at the new pitch (< 2 dB) with the old pitch gone (< -10 dB) and the piano's overtones left (< 2 dB), and that with the piano not found its overtones are moved with the voice (< -3 dB: the failure the engine used to have). On the 2.90 engine 8 of these fail (line 17 / 22 / 19 c off, no sound kept apart); with the piano 10 c or less from the voice's mean pitch both engines fail (vibrato of 10 c: not separable), which is why the test has the piano at 20 c. All other engine tests unchanged (golden comparison with the Python reference passes).

**Checked on the real example** (`Pitch_Example_Original`, long box 0.3 - 7.1 s, 395 Hz +-70 c, snap 100 %; scratch harness `measureNote` + `retuneSegment`, compared with a harness built from the 2.90.0 engine): one sound kept apart (nb0, sounds 3.46 - 4.22 s), `recoverCaptured` accepts exactly one stretch (3.44 - 3.94 s: level jump 4.5, ratio 22, dF 12 c, dL 10 c), `f0` is continuous through 3.4 - 3.6 s (a trough of 12 c in a vibrato that swings 12 - 39 c); the piano microphone's 392 / 784 / 1176 Hz partials move by a few cents at most. Against 2.90.0 the line also differs by up to 44 c at ~1.45 - 1.95 s and ~4.9 - 5.7 s (piano strikes at ~1.6 and ~5.3 s): there 2.90.0's line sits 20 - 40 c under the bright partial ridge of the choir microphones in the spectrogram and the new one on it. That is not `recoverCaptured` (it fires once) but a side effect: when `part` is not empty `measureNote` now runs the "each voice followed again with the others taken out" sweeps (seed = the line's slow drift, wide windows) over the whole note, where 2.90.0 skipped them with a single line. Everywhere else the new line is within 1.5 c rms (max 7 c) of 2.90.0's. Not checked: the user's own boxes and settings, and by ear.

## 2.90.1: RMS meters of a stopped mixer

`processMixer` works the RMS (`StripState/BusState::msL/msR`, atomics `rmsL/rmsR`) only while the mixer runs; `process()` skips a mixer that is not in `pl->jobs` (its tail has ended, or it rests / has no outputs). Before 2.90.1 the last RMS stayed in `rmsL/rmsR` (the peak is read with `take()` so it always fell). Now, right after the jobs are chosen, a mixer that is not running and whose `MixerPlan::rmsIdle` is false gets `msL/msR` and `rmsL/rmsR` of every node set to 0 once (`rmsIdle = true`; cleared when it runs again). The `LevelMeter` then falls at its own rate. Core test section "meters: the RMS of a mixer that stops running falls to nothing (2.90.1)" (a mixer whose Ext Bus goes nowhere is not run).

## 2.90: a pitch pipe that never bends, softer; click sounds; icons; Noise mode as a program of its own

**The bend (fixed in the engine, not the window):** `PitchPipeVoices::render` takes a slot's frequency only while its gate is open (`v.active && v.gate`), latches it in `Voice::targetHz` when the note starts, and never reads it again after the gate closes; `setSlot (slot, hz, false)` does not even store the hz. 2.89's window passed `hzFor (midi)` (the plain tuning) with every "off", and for a pure chord that is 14 - 16 cents away from the chord's own pitch. A retune while gated (`hz != targetHz`) glides: `incFrom` -> `incTo` over `glideSamples` (50 ms, smoothstep), ending exactly on the target; a new note on a dead slot starts at once (`targetHz` reset to 0). `debugHz (slot)` (tests only). Test: the fade-out of a 436.5 Hz note released with 440 Hz handed along stays on 436.5 (the sound's phase advance within 1 cent; `debugHz` exact), a whole pure chord, the glide (monotonic, ends exact in 45 - 56 ms), a new note after the old one died.

**Tone:** `setTone (0..1)`, saved as `pitchPipe.tone`. Per voice two equal one-pole low-passes whose corner is `toneCorner (tone)` = 2^(1 + 3.5 tone) times the note's own pitch (so the same softness at every pitch; clamped 500 Hz .. 0.45 sr); `toneKeep (k)` is the RMS the filter leaves of the reed's 1/h^1.05 series, and the render divides by it (cap x3), so the loudness is the same at every setting. Breath 0.035 -> 0.014 (chiff 0.22 -> 0.09), its band 900 - 6000 -> 700 - 3500 Hz, swell 45 -> 70 ms. Default 0.35. Measured (scratchpad b290/meas.cpp, old vs new voice side by side): A4 above 3 kHz -15.6 -> -22.4 dB of the total, C3 -20.8 -> -41.5, level within 0.1 dB. Tone 1.0 = about the old sound.

**Window:** `PianoKeys::kOctaves = 2` (`whiteCount()` 15); `lowestC` 24 .. 84 (the old top of the range, 108, is still reachable); window 980 x 620; `resized()` rewritten with 14 - 16 px between rows. Tone slider beside Level.

**Click (core):** `ClickTrack::DrumKit` (5th sound). `makeHit (Soft)`: body `sin` of a phase integrated from a frequency falling 150 -> 60 Hz (tau 40 ms) plus 5x (accent 7x) lowpassed noise, the 22 ms raised-cosine swell, dark air at the front; the accent only differs in length and fullness. Normalised autocorrelation of the hit (a pure tone = 1.0): 2.89's Soft 1.00, now 0.38 (accent 0.36). `makeDrum (Kick / Snare / Hat)` and `makeTick()` (second difference of noise: only the top of the band); `drumFor (beat, perBar)`; `quaversPerBeat (unit)` = 8 / unit for unit <= 8 (a beat of a half note holds four quavers), 1 above (a sixteenth beat ticks every second beat). `render`: the scheduler now steps at the quaver (`subInBeat` set at each beat from the CURRENT setting, `subIdx`), so toggling the ticks never moves a beat; `Playing` has its own gain (the tick plays at `tickGain`), 8 slots. `ClickDef::quaverTicks`, `tickDb` (-20 default), saved. `AppContext::applyAux` pushes the unit, ticks and tick level too.

**Icons:** `Icon::RoomTone` (house: red roof, chimney, light walls, door, two windows), `Icon::NoiseMode` (dot, two arcs, red ring with a slash). `roomToneButton` / `noiseModeButton` are `IconButton`s (34 x 26 like their neighbours; tooltips unchanged).

**Noise mode (`NoiseModeDialog` in FixTools.cpp):**
- *Layout:* `bottomChunks()` lists every control as a `Chunk { put, minW, grow }`; `packRows` fills rows greedily in order; `resized()` gives each row's spare width to the chunks that can grow (sliders, the writing, the progress bar) and puts the picture above. At 1980 px: three rows (transport + fixes; settings; settings + writing); at 1100 px it wraps to more. The writing is 12.5 pt with a minimum horizontal scale of 0.7 / 0.75.
- *Transport:* `startBtn`, `rewBtn`, `playBtn` (Play / Stop: the button text "Stop" makes `IconButton` draw the square), `fwdBtn`, `endBtn`, `loopBtn`, `TimeReadout clock` (follows `transportPos()`, `onJump` -> `goTo`). `goTo (t)` places the cue (`SpectrogramView::placeCue`), turns the page to show it (`showTime`) and, if playing, restarts from there. Play: `playRange (from)` plays `from` -> the END of the edit (`seconds()`); with Loop on it loops `[from in page, visT1)` as before. From the very end it starts again from 0. `startPoint()` = the placed playhead if it is on the page (the end counts), else the left edge.
- *Page turning (`tick`, 30 Hz):* when the playhead was on the page at the last tick and has now reached the last 1 % of it, and the page does not already end at the end of the edit, `view.showFrom (view.visT1())` - the next page, contiguous (the user's own panning away is not fought: it only turns when the playhead was on the page). Loop on: no turning. `restartIfPlaying` (after a fix or Undo) carries on from the playhead to `lastPlayTo`.
- The view's `note()` no longer says "Play plays this part" (the ReTouch window's own text says it where it is true).

**Selection box (all spectrogram windows share `SpectrogramView`):** the hit test is `boxedit::edgesAt` in `src/core/BoxEdges.h` (pure, tested). A side belongs to one axis: left / right change `a.x` / `b.x` only, top / bottom `b.y` / `a.y` only; both only at a real corner (`cr = min (6, min (w, h) / 3) >= 2`, point within `cr` of the corner in both directions). Where a vertical and a horizontal edge are both within 6 px but it is no corner, the short edge wins (the end of a flat box = its width). The old test took "left else right" and "top else bottom" independently, so a box under 12 px thin had every point of its end in two edges' reach (a corner mask: the pointer's y became the new top or bottom). `mouseDown` stores `grabDx / grabDy` (pointer minus the grabbed edge, px) so the edge does not jump; `mouseDrag` moves only the masked axes, keeps the moving edge at least `max (0.002 s, 3 px)` / 3 px from the fixed one (clamping, not freezing, and flipping to the other side at the limits of the picture), and the edge may cross the fixed one.

**Playhead trail:** `theme::paintPlayhead` draws a glow (`x - 5 .. x + 7`) in the Manuscript look, but `movePlayhead` of the edit and take windows redrew only `x - 1 .. x + 3` around the old and new x. A repaint is one pass clipped to the union of the dirty strips, so the new glow was painted over the freshly cleaned old strip and nothing after that cleaned it: a translucent band from the start of playing to the line. `theme::playheadReach = 6` / `theme::playheadStrip (x, top, height)` now give the area to redraw; use it for any window that moves the line by redrawing a strip.

**Edit window buttons:** `AppContext::windowIsOpen (key)` (Main.cpp: `windows.find (key) != end`); `timerCallback` sets `trimButton` / `bounceButton` toggle states from `"trim:<edit id>"` / `"bounce:<edit id>"`, and their gold is `buttonOnColourId` (it had been `buttonColourId`, i.e. always on). Closing the Bounce Out window destroys its job, so "open" also means "bouncing".

**Mixer faders:** `NudgeButton` beside `FaderBlock::label` (same 15 px height; 14 px wide, 12 in a narrow strip): `nudge (+-1)` = `slider.setValue (v + delta, sendNotificationSync)`, the same path as typing a level (so `linkFader` and the engine follow); -inf stays; one step up from -inf is -79. `setTriggeredOnMouseDown` + `setRepeatSpeed (450, 90)`.

**Stepped plug-in automation lanes:** `InsertProcessor::paramSteps (i)` (Mixer.h; 0 = smooth, the default) and `paramText (i, norm)`; the VST3 host (PluginHost.cpp) returns 2 for `isBoolean()`, `getNumSteps()` when it is 2..100, else 0 (a smooth VST3 parameter reports a huge default step count, so more than 100 counts as smooth). `AutoLane::snapToSteps (v, steps)` (steps < 2: the value untouched, so a fader / pan / smooth lane is never altered) and `valueAt (t, whenEmpty, steps)`: with steps >= 2 the earlier point's value holds until the next point (no line between points) and every result is snapped; `AutomationPlan::Item::steps` carries the count to the audio thread (`apply` calls `valueAt (at, 0.5f, it.steps)`), so a lane saved with in-between values (an older project) plays on the nearest position and nothing in between is ever sent. In `EditWindow.cpp`: `stepsOfLane` asks the live processor of the lane's slot, `draggedValue` snaps (no fine mode), `automationMouseDown` hit-tests at the snapped value, `paintLane` draws the staircase and a guide per position (up to 16), `autoValueText` shows the plug-in's own word and "(n of N)". A plug-in that is not loaded (no processor) gives 0 steps: the lane stays smooth rather than guessing.

**Manuscript keys:** `FermataLookAndFeel::drawManuscriptKey` draws a lit key gold whatever on-colour it has, so `drawButtonText` must use the dark ink for every lit key (`giltInk`), not only when the on-colour is light; the mixer's OUT key (on-colour green, brightness < 0.6) had gold writing on gold. The render hook `FERMATA_PRESSKEYS="OUT;M;S"` (gen_r.py, with `FERMATA_RENDER=mixer`, `FERMATA_STYLE=1`) lights keys by their text.

**Lane rows (2.90):** `EditDef::laneRows` (node id text -> vector of `LaneShown {param, mixer}`; no entry = one row showing the fader) says what each row of a track / Int Bus / Ext Bus / VCA shows; it is a VIEW (saved as `laneRows`, in `saveHash`, stripped from the undo text by `kEditViewKeys`, not in `undoHash`) - the lanes themselves are `EditDef::lanes` and `automationPlanFor` builds the plan from every lane with points (`hasAutomation()`), whether `automationOn` (the Automation button = "show the rows") is set or not. Read with `laneShown (node, k)` (never adds an entry), change with `setLaneShown`, `addLaneRow (node, hasPan, hasGain)` (`freeLaneParam`: first of fader / pan / gain no row shows), `removeLaneRow`; `isDefaultRows` entries are erased so the hash does not move. `kMaxLaneRows` = 12. `automationPlanFor`: a lane whose trackId is an Ext Bus handles `fader` and plug-in parameters only (gain / pan / sends do not exist there). `EditTimeline::rowTracks()` builds the rows AND a parallel `rowInfo` (`RowInfo {node, lane, kind, block}`): audio tracks (each followed by its extra lane rows when automationOn), then Int Buses, Ext Buses, VCAs (each with its extra rows; a VCA has none). An extra row's TrackDef has a NULL id (so no audio code can match it); every handler that works on tracks goes through `isTrackRow (row)` (lane 0 of a Track) - name picking, drag-reorder (rows are mapped to track ids by node, not by row number: `trackRowAtY`, `rowAfterBlock`), Shift + drag clip moves (`finishClipMove` also checks `kindOf == Track`), Alt-drag track marks (`fixTracks`), the lasso. `infoOf (row)` rebuilds the cache on every call: copy what you need (`const TrackDef t = rowTracks()[row]`), do not hold references across calls. The row's name cell: `plusRect` / `minusRect` (+ on a node's own row; - on every row but a VCA's own row), `drawLaneBox`. Menus: `showLaneMenu (ed, t, inf)` per kind (`slotsOf (mixer, node)` for the insert slots of a track, Int Bus or Ext Bus).

**Starting inside a plug-in lane (2.90):** `startPlayback` primes the plan (`automation->apply (position)`) before the first block and logs one line per plug-in lane; `AutomationPlan::Item::refresh` (24) re-sends the value in the first blocks; `InsertSlot::setParamNorm` returns whether the processor really took the value (`lastSent` is set only then). Not reproduced with a stand-in plug-in in the engine-level test (the lane chase already worked); the log line is there to catch the real Pro-C 2 case.

**A note for later edits of FixTools.cpp:** RepairDialog's and NoiseModeDialog's constructors share several identical lines (`hearBox`, `playBtn.setColour`, ...); an edit anchored on such a line must say which class, or it lands in the first.

**Tests ("2.90 ..." sections):** the locked pitch, the glide, the tone (softer in order, the same loudness, the usual setting below -20 dB above 3 kHz on A4), the Soft's periodicity < 0.6, the drum kit's pattern (`drumFor`, a rendered 4/4 bar: kick 1.0, hats 0.5, snare 0.8, onsets 0.5 s apart, the Accent box's effect), quavers (8 to a bar of 4/4 0.25 s apart and quiet between the beats, 16 per 2 s in half-note beats, none extra in 6/8, beats unmoved by switching on mid-bar), the new settings in a project round trip. Render hooks as 2.89 (`FERMATA_RENDER=pitchpipe / click / edit / noisemode`).

## 2.88: the Display Organiser rebuilt

**Model (core, pure, tested):** `src/core/ScreenLayout.h`. `ScreenDivision` = a tree of splits (`Node { split: None | SideBySide | AboveBelow, frac, a, b, cell, linked }`); `shape (s)` builds one of the eight shapes (`One, AboveBelow2, SideBySide2, Rows3, Columns3, OneAboveTwo, OneBesideTwo, Grid4`); `cells()` the leaves as `FracRect`s (fractions of the area; the core has no juce_graphics); `lines()` the dividers (node, vertical, where); `setFrac (node, f)` moves one (12 .. 88 %, a `linked` node follows: the grid's two inner verticals); `fracs()` / `setFracs()` outermost line first (saved with a layout). `fitTo (wanted)` puts every line at the mean of the window edges that meet it (three passes), `matchScore` = mean IoU; `layoutFromWindows (key, rect ...)` tries every shape with the right number of cells (cells take the unplaced window with the nearest centre), keeps the best score - "Save how the windows are now" (the four biggest windows when there are more). `WindowRef { kind, key }`: a named window or "any of this kind" (key empty). `LayoutDef { name, shape, fracs, refs, builtIn }`, `toVar`/`fromVar`, `builtIns()`. `Project::layouts` (serialised as "layouts" next to "windowBounds", not an undo step; saving one calls `markDirty`).

**Window:** `src/ui/OrganiserWindow.*`. `ShapeButton` (picture + name), `ScreenPicture` (the cells, drawn with the kind colour band, title and sub-line from `describe()`; line hit zone +/- 5 px, drag = `setFrac` from the node's `areaOf`; a cell drag beyond 6 px becomes a swap with a floating tag; a click calls `onChooseCell`), `OrganiserComponent` (ListBoxModel for the layouts: header rows, built-ins, project ones; a Timer every 700 ms re-reads `app.listWindows()` and the display's aspect; `resolve (ref)` = the named key if it exists, else the kind's highest `recency`; `arrange()` refuses the same window twice and reports cells with nothing to put in them).

**Hooks (Main.cpp):** `AppContext::listWindows` now returns `WindowEntry { key, title, kind ("main" / "take" / "edit" / "mixer" / "other"), open, recency }` (recency = `ToolWindow::lastActive`, -1 closed; the main window 0; the Organiser itself and Take Displays are left out). `windowPlacesNow()` = the main window's display `userArea` and the bounds of every visible, non-minimised, non-docked window at least half on that screen. `placeWindows` unchanged.

## 2.87: inputs named by device; loudness (EBU R 128 / a target) on the Virtual Master and Listening copy tabs

**Input names.** `PreampDriver::deviceOf (input, name, channel)` (RavennaPreampDriver: `locate`, so only a connected device that reports its channels); `Project::inputDeviceLabel (i)` = "<device>-<channel + 1>" from the driver, else from `preampDevices` (a map: its entries; `firstInput` only: the device whose firstInput is the highest <= i, unless another device's firstInput lies between); `driverNamePrefix()` = the longest run of whole words every driver name starts with that leaves a letter in every name ("MAD Input 12" -> "Input 12"; "Hapi 1" is kept whole); `inputShortName (i)` = the device label, else the driver name without that prefix; `inputLabel` = "n - short - your name". The main window's InputRow shows "nn · short" across the display (min scale 0.6, mouse clicks on so the tooltip shows).

**Loudness.** `src/core/Loudness.h`: `LoudnessMeter` - K-weighting (the shelf and the high-pass derived for any sample rate from the standard's analogue prototypes: the 48 k coefficients come out of them), 100 ms hops summed per channel, a 400 ms block = 4 hops (weights 1 / 1 / 1 / 0 / 1.41 / 1.41 for 5.1), `integratedLufs()` with the -70 LUFS absolute gate and the -10 LU relative gate, `truePeakDb()` from a 4 x polyphase windowed-sinc (12 taps a phase, unity DC gain per phase). Tests "2.87 loudness": EBU Tech 3341 cases (-23 / -33 dBFS tones at 44.1 / 48 / 96 k within 0.1 LU, mono -3, the gates: silence around, -43 gated out, -29 kept), the K-curve (10 kHz up, 50 Hz down), true peak of a quarter-rate tone phased between the samples, block-size independence, 5.1 weights.

`MasterExportSettings`: `loudnessMode` (0 peak / 1 R 128 / 2 target), `targetLufs`, `ceilingDbTp`, `wantedLufs() / wantedCeiling()`, and the Listening copy tab's own `formNormalise / formLoudnessMode / formPeakDb / formTargetLufs / formCeilingDbTp / formTogether` (makeFormsSpec copies them into the spec); saved in "vm". Export (`MasterExport.cpp`, stage 2b): with a loudness mode, every converted piece is read again through a meter (and all of them through one album meter when they share a rate; otherwise the album is the length-weighted power mean), `planLoudness` (pure: per piece the gain to the target - per album the album's one gain - the true peak at that gain and the excess over the ceiling) -> `LoudnessCheck`; with problems and `spec.loudnessDecision` 0 the job calls `onLoudnessCheck` on the message thread and waits on a WaitableEvent (`decide (1 retain / 2 ignore / 3 cancel)`; cancel() and the destructor decide 3); `loudnessGains` (retain: minus the excess, per album minus the worst); the gains replace the peak normalisation in stage 3; `R.report` = `LoudnessCheck::text()` + the decision, `R.loudness` the check. `MasterExportJob::start()` so the window can set the callback before the thread runs. UI: `LevelBox` (MasteringWindow.h / .cpp) used twice (vm / forms fields chosen by a flag): on, the mode combo, the value with its unit, the ceiling row and Presets (mode 2 only), per album / per track; `askLoudness` (an AlertWindow with the list and the three buttons -> `job->decide`), `showReport` (the Loudness report button, both tabs), the old normToggle / peakEditor / overall / individual controls are no longer placed. Tests: the planner (per track and per album, retain / ignore), an export of a spiky piece (clicks over a quiet tone) and a plain one with retain (the plain one on the target, the spiky one at the ceiling, short by the excess), ignore (clipped), per album (the distance kept), R 128 overrides the boxes, the forms' own levels, save / load.

## 2.86: the zero-latency low cut

`lowcut::Biquads` (LowCut.h): a Butterworth high-pass of order `iirOrder (slope)` (slopeDb / 6; the wall 24) as cascaded second-order sections (RBJ high-pass, Q = 1 / (2 cos angle) for the Butterworth pole pairs - angles (2k+1) pi / 2N for even N, k pi / N for odd N plus one first-order section), bilinear with the cut pre-warped, transposed direct form II in double (a 10 Hz cut at 96 k: poles within 1e-6 of the circle), `setCoefficients` keeps the state across a sweep of the same order, `magnitudeAt` for the tests. `LowCut::linear` (Flag, false by default): `rebuild` makes the cascade (one per channel, swapped in under the lock, `usingFir` = false, latency 0, the FIR freed outside the lock) or, when linear, the FIR as before (the cascade freed); `process` runs whichever `usingFir` says, on a scratch copy while the key is off as before; `setDesign (hz, slope, linear)`; `linearLatencyFor` (static) for the panel's DELAY while unticked. `targetMagnitude / targetDb (f, fc, slope, linear)`: the same Butterworth curve for every slope but the wall (24th order when not linear). Project file: "linear": true in "lowCut" (written only when set; the default-skip includes it). Panel: the Linear phase tick (applies with the other settings 60 ms after the change), DELAY "none" / "17408 = 181 ms", the slope tooltip by kind; the key's caption gets " lin". Tests: "2.86 the zero-latency low cut" (every slope's curve by `magnitudeAt` and by running a sine, the impulse at once, 10 Hz wall on noise stays bounded, LowCut default / linear latency, switching both ways while it runs, save / load); the 2.84 FIR test sets linear.

## 2.85: the control surface (Avid S1 / Mackie Control)

`src/core/Mcu.h` (pure, tested): the Mackie Control protocol - `decode` (pitch bend = fader 14-bit on channel strip + 1, notes = buttons / fader touch 0x68+, CC 0x10+ = dials with the direction in bit 6, the handshake SysEx 00 00 66 14 xx with the Logic Control challenge / response formula in `handshakeResponse`), the encoders (`fader`, `lamp`, `ring` mode / position / centre, `lcd` at a position 0 .. 111 with 7-bit text, `lcdCell` 7 characters per strip, `assignmentDigit` 7-segment, `meter` channel pressure), the fader law shared with the screen (`faderFrac` = LevelMeter::faderFrac: -80 at the bottom, 0 dB four-fifths up, +12 at the top; the bottom = -100 off) and `describe` / `hexOf` for the monitor. Test "2.85 the Mackie Control protocol".

`src/ui/Surface.h / .cpp`: `SurfaceController` (owned by the application, `ctx->surface`): MIDI in on its own thread -> a locked vector -> `AsyncUpdater` -> the message thread, where everything happens; `openPorts` every 2 s (a port that appears is taken, one that goes is noticed; nothing chosen = a port whose name looks like the S1 / a Mackie: `looksLikeSurface`), the chosen ports matched by identifier then by name. `controlledMixer()`: Off / Pinned (if that mixer still exists) / Follow = `ctx->frontMixerId` (Main.cpp: the visible mixer window with the highest lastActive, else recentMixer, else the first mixer). `channels (MixerState&)`: the strips in the mixer window's order (tracks not hidden, Int Buses, Ext Buses, the VCAs in vcaShown) as `Chan` (the pool pointers: StripState / BusState / VcaState); `bank` per mixer in `banks`. The surface writes the same Params the mouse does (`gainDb`, `pan` / `panL` + `panR` (both move by the same delta, clamped so the outer one stops at the edge: `setPanMid`), `mute`, `solo`, `lowCut.setOn` + `engine.checkLatency`) and `project.markDirty()`; the preamp gain through `project.setPreamp` on the track's inputs (`preampGainOf` raw / shown: the MT48's Pad offset), 0.5 dB steps, home = 20 dB; the mixer strips follow in their 30 Hz `tick` (`ChannelStrip::followModel`, the M / S / pan follows in BusStrip and VcaStrip), and the Select key goes to `MixerComponent::selectStrip` (the gang's click) through `ctx->selectStripInMixer`. The zoom keys send a KeyPress to the take / edit window last in front (`ctx->keyToWorkWindow` -> `keyPressed` of its content), so they are exactly the arrow and , . keys. Feedback 30 times a second in `show`: per strip the fader (not while touched; a fader that was moved is remembered as sent, so there is no echo), the ring (pan mode 1 with the centre lamp, gain / flipped level mode 2), the four lamps by `stripMap`, the two LCD cells (name; level or the value just turned for 1.5 s - `valueText / valueUntil`; the mixer's name across the bottom line for 1.5 s on a change of mixer: `banner`), the meter every other tick from rmsL / rmsR; the Flip / Plug-in / Pan lamps and the two digits; everything diffed against `sent[]`, `force` resends all. `blank()` when the target goes away and at shutdown. Key map `map` note -> `surface::Action` (defaults in `defaults()`; Learn: `learnAction`, the next button press is bound, a strip key is refused with a notice), `stripMap` (Rec / Solo / Mute / Select / Push -> `StripAction`), saved in the settings file (surfaceIn / Out (+ Name), surfaceTarget, surfaceMap, surfaceStrip, surfaceMeters). Monitor: the last 400 lines, `ChangeBroadcaster` for the panel.

`src/ui/SurfacePanel.h`: `SurfaceTargetBox` (the CONTROLLER combo, in the main window's top row - `MainComponent::controllerBox` beside DISK FREE with the `controllerLamp` (an LtcLamp "S1", green while connected and not Off, click = the panel; `controllerRowWidth` in the column width) - and in the panel; it listens to the project's structure and the surface), `SurfacePanel` (window key "surface", Project > Mixer controller...): the port combos (by identifier; a port taken on its own is shown too: `openInId / openOutId`), Refresh, the status line, CONTROLLER, the meters tick, the five strip-key combos, an `ActionRow` per Action (name, the keys bound, Learn / Cancel, x), "Back to the standard keys", and the monitor ListBox (time / in-out / hex / meaning, follows the end unless paused). Render hook FERMATA_RENDER=surface. Not testable here beyond the protocol: there is no MIDI on this Linux build (JUCE_ALSA=0), so the first run against the S1 is the user's.

**2.85.1.** From the user's Anubis Bridge (`AnubisBridge/anubis_bridge/defaults.json`, the same S1 through EuControl's Mackie emulation on "Euphonix EuMidi1"): the eight bottom keys left to right send 0x46 0x48 0x47 0x49 0x60 0x61 0x62 0x63 (Shift, Control, Option, Alt, Up, Down, Left, Right) - now the defaults in `SurfaceController::defaults()` for the eight jobs, F1 - F8 alongside; the Mackie cursor codes are therefore no longer zoom keys. `mcu::FaderLaw` (EuControl / Screen): `euControlScale()` is the bridge's fader calibration (position 0..1 -> dB as EuControl shows it: 0.846 = 0, 0.596 = -10, 0.364 = -20, 0.155 = -40, 1.0 = +6.14; the bottom = off), piecewise linear both ways (`euFracOf / euDbOf`); `dbToFader / faderToDb` take the law, EuControl by default; `SurfaceController::law` (surfaceLaw in the settings; the panel's "Fader scale" combo). EuControl needs the program chosen in MIDI Setup > Select Mackie Application (ports 1 - 4): the panel's intro says so and "Copy Fermata's path" puts `File::currentExecutableFile` on the clipboard. Tests extended (both laws, monotonic, the calibration points).

## 2.84: the low cut

`src/core/LowCut.h`. `lowcut::design (fc, slope, sr)`: the target magnitude (`targetMagnitude`: Butterworth-shaped of order slope / 6, -3 dB at fc; the wall a raised cosine over a sixth of an octave round fc, -6 dB at fc) sampled on `tapsFor` bins (a power of two: 6 / 12 / 24 cycles of fc for slopes 6-12 / 18-36 / 48 and up and the wall, 1024 .. 262144 taps - the steep slopes need the resolution or the window rounds them off: at 10 cycles 48 dB / octave measured 44.6 an octave below), inverse FFT (dsp::FFT, double), centred on tap n / 2, Kaiser beta 8 centred on the same tap (so the response is exactly symmetric), normalised to unity at Nyquist. Measured at 80 Hz / 96 k: -3.1 at fc for every slope, an octave below within 2 dB of the slope's figure up to 72, within 4 at 84 / 96, the wall -136 an octave below. `Convolver`: uniform partitioned convolution, partition `partitionFor (taps)` (taps / 32 clamped 256 .. 4096), overlap-save with a 2B complex FFT (`FftF`, single precision, tables made in `init`), the upper half of each product left to Hermitian symmetry; the input gathered B samples at a time through a ring (`process` takes any block size), so the latency is taps / 2 + B; `setKernel` swaps new spectra under the same layout (the sound in the filter carries on - a sweep within the same length is seamless; a change of length starts afresh, with a dropout of the latency's worth, and the plan is rebuilt anyway). `LowCut` (in StripState and BusState): `on / hz / slope`, `prepare (sr, block, channels)`, `setDesign`, `setOn`, `ensureChannels` (buildPlan widens a surround track's filter), `latencySamples()` (0 while off), `process` under a try-lock (off: run on a scratch copy so switching on is clean, as with a bypassed plug-in); `rebuild` makes the new filters outside the lock. Engine: before the inserts in the strip and Int Bus paths; `chainLat` and the slots' `autoLat` include it; `latencySignature` includes it (so `checkLatency` from the panel / key rebuilds the plan); `prepare` re-prepares every low cut. Project file: "lowCut" { on, hz, slope } on strips and buses, written only when not the default. UI (`src/ui/LowCutPanel.h`): `LowCutKey` (the LC key, a 450 ms press or a right-click opens the panel, the click that ends the press is swallowed; the caption "80.0  48dB" / "120  wall") in a `kLcRowH` = 18 row between the inputs and INSERTS on ChannelStrip and (Int) BusStrip, `heightFor` grown by it; `LowCutPanel` in a CallOutBox: on, frequency slider (log by `setSkewFactorFromMidPoint`, typed value through `valueentry::ask`), slope combo, DELAY, the curve from `targetDb`; `setDesign` 60 ms after the last change. Render hook FERMATA_LC=1 (two strips switched on, the first strip's panel opened and moved into view). Test "2.84 the low cut" (symmetry, the slopes' figures, the convolver against direct convolution at odd block sizes, the impulse through LowCut at the reported latency, off = bit for bit, save / load).

## 2.83: waveforms the Pyramix way, the mixer's meters

**The outline store.** `WaveThumb` (src/ui/WaveThumb.h) replaces `juce::AudioThumbnail` (8-bit min / max per 128 samples) in the take and edit windows: int16 min / max per 256 samples per channel, plus a coarse level per 16 points (`getApproximateMinMax` uses it for the middle of a wide span), read in the background by a shared `WaveThumb::Pool` (2 threads; the part read so far is drawable, `done` is atomic; progress and completion come back on the message thread through a `WeakReference`, every 200 ms); `load (file)` opens the header on the calling thread so the length is known at once. Saved as `.fwave2` ('FWT2') in the Waveforms folder (`wavestore::loadThumbnail / saveThumbnail`, the same ThumbSaver); `redrawWaveforms` deletes both the old and the new file. `DirectWaveCache::kThumbSamplesPerPixelLimit` follows (256) and `kMaxSamples` is 1.5 M (a wide window at 255 samples a pixel). Measured against Pyramix on the user's Cantate Domino file: the whole-file view correlates 0.90 with a plain peak envelope (mean-of-block-peaks, RMS, decimation all worse), so the shape was already right; the two real differences were the 8-bit floor and the drawing.

**The drawing.** `fillEnvelope`: tops / bottoms rounded to whole pixels; the body is a stair-step path (column i from x to x + 1 at its own top, vertical edges between columns) filled with the body colour or the WaveColour gradient; the line is a `RectangleList<int>` of the top and bottom pixel of every column and the step to the next column, drawn in whichever of the two columns reaches further out (so a one-pixel spike is one black column, as in Pyramix); `fillRectList` takes the gradient fill too. Harness `scratchpad/wv/wt.cpp` renders the file at the screenshots' zooms (written as PPM: this JUCE build's PNG writer blurs 1-pixel lines). Note: in the solid look the line's brightness is 12 % of the body's (Pyramix's is black).

**The mixer's meters.** `LevelMeter` with `useFaderScale()` (the mixers; the main window's meters are as before) is Pyramix-style: `paintPyramix` (well 0x101010, edge 0x232323, clip light `kClipH` = 5 px at the top lit by `clipped` (a sample at full scale, until Reset peaks), bar 0x83a84e below 0 dB and 0xdc5467 above, the cap 0xa2d25e from the bar up to `capDb`, the hold line 2 px in the cap's colour or red above 0 / latched). Ballistics in dB by the clock (`lastTick`): the bar climbs over `MeterSettings::riseMs` (0 / 10 / 50 / 300) and falls at `fallDbPerSec` (90 / 25 / 12 / 6); the cap is the sample peak falling at `kCapFallDbPerSec` 60; the RMS bar follows the same rule; defaults rise 1, fall 1, hold 2 (3 s). `FaderBlock::resized` puts the well's edge and clip light above the fader travel (`travelTop - 7`) so the bar runs exactly over the travel and the one scale serves both. From the user's video (`scratchpad/pmx/va`, frames at 30 fps): the bar (131,168,78) with ~25 dB/s fall, the lighter cap (162,210,94) on transients falling ~60 dB/s, the hold ~3 s, red (220,84,103) above 0, scale +12 at 19 %, -18 at 47 %, -60 at 94 % = Fermata's faderFrac to within a percent. Offscreen check `scratchpad/wv/mt.cpp`.

## 2.82: the Manuscript UI

`theme::styleIndex()` (0 Fermata style, 1 Manuscript; `theme::manuscript()`), saved as "uiStyle", set at start before the desk colour and the dark look; `applyStyle (int)` in ThemeSwitch.h (Project > UI style, `MainComponent::pickStyle`). The style is mostly the palette: `theme::setDark` fills the 18 colours from the Manuscript table (`m[]`, no desk tint) when it is on; `desk::makePalette` and `makeMixTheme` have a Manuscript branch; `theme::gilt / giltBright / giltDim / giltInk / parchment / navyKey`, `isNavyKey (c)` (the Manuscript keys in any shade: their writing is gilt - `inkOn` and `drawButtonText` use it; a key with a colour of its own keeps white writing). Drawing: `FermataLookAndFeel::drawManuscriptKey` (navy gradient, gilt rule, gilt gradient when on; also used by the mixer's look), `theme::paintPanel` (a glow from the top; the main window), `paintStaff` (five faint lines per row of the take and edit timelines), `paintPlayhead` (gold with a glow), `wood::paintManuscriptMargin` (the cheeks, the bottoms and the rail), the take / edit banners write with `inkOn (plate)`. Type: TeX Gyre Pagella regular / bold / italic in `src/ui/FontData2.cpp` (from assets/, GUST licence); `getTypefaceForFont` maps the Manuscript default name to them and `<Manuscript-Italic>` (`theme::wordmarkFontName`) to the italic. **Why the Manuscript face has a name of its own** (`theme::sansName`, `applyDefaultFontName` sets JUCE's default sans-serif name by reference): JUCE's glyph cache is keyed by the font's name / style / height, not its typeface, so a live switch that only changed what "<Sans-Serif>" resolves to drew letters of one face with the glyph ids of the other (wrong letters in every text box). With separate names the old fonts stay consistent until `themeswitch::refont` gives every Label and TextEditor a fresh font (set past the equality check, which ignores the typeface). Render hook: FERMATA_STYLE=1 (a live switch 2.5 s in).

## 2.81: Denoise inside ReTouch, the project's room tone, the de-clicker's picture check

2.81.1: `DenoiseDialog` / `DenoiseContext` / `denoiseTake` (the take window's whole-take denoiser, 2.77) removed with its button; `audioops::learnDenoise / denoiseFiles / denoiseTracks` and `AppContext::saveHold` stay (the file paths of ReTouch's Denoise, and the tests).

**Denoise as a fix.** `FixSpec::Kind::Denoise`: f0 / f1 = the box's pitch range, `dn` (Params: amount, residual, groupMode 0), `dnBoxT0 / T1` (a room-tone box of the loaded area, seconds from its start; < 0 = the project's), `dnProfiles` (shared_ptr to one Profile per channel of the audio handed over - set by the CALLER before each applyFix / fixPiece call, never saved: the profile differs per track and channel while a FixSpec is shared by all). `applyFix` → `denoiseBox`: the profile copied with `weight` = 1 inside f0 .. f1 (else 0), `featherProfile` a quarter octave, `processSegment` over the box with 6 s / 2 s context, the result faded in / out over 100 ms (a quarter of a short box) - so outside the box in time and pitch nothing changes (`marginFor` gives Denoise the Patch margin of 8 s). `RegionJob::profilesFor (fileIndex, spec)` (std::function) and `fixTakeFiles (..., profilesFor)` give each file its own profiles as it is made (`runRegionJob` copies the ops and sets `dnProfiles`). `attachRoomTone (jobs, ops, regions, tp, projectRT, rate, j)`: for ops with a box, `learnFileProfiles` over the area's TrackParts with a full-spectrum box (weights cleared), once per box; else the project's `perChannel` for the file's track; `openRepair` builds `tpArea` from its (stream-aware) parts and snapshots the project's room tone; `repairTake` learns a box with `learnDenoise` on each file and looks the project's up by track. The RepairDialog: Amount / Residual sliders, "This box is the room tone [L]" (`rtBoxT0 / T1`), "Denoise [N]" = `preview (2)` - the picture tracks use the project's mono profile for that track (`RepairContext::roomMono`) or a profile learned from the track's own box; `buildSpec (t0, t1, what)` with what 0 / 1 / 2. Group mode is off in this path (one file at a time). Noise mode: `denoise()` = a Denoise RepairOp through `prepareRepairJobs (..., makeFiles = false)` → `attachRoomTone` → `runRepairJobs`; the marks, feathers, Tail and Other tracks are gone. The 2.79 SpectralDenoiseDialog / `denoiseEdit` / `prepareDenoiseJobs` are removed (`SpectralDenoiseSettings` stays as the carrier for `learnFileProfiles`).

**The project's room tone.** `Project::RoomTone { source; tracks: RoomToneTrack { trackId, trackName, perChannel, mono } }`, saved as "roomTone" (each profile's psd as base64 float32: ~16 KB per channel) and loaded with it; `TakeGroup::roomTone / roomToneIn / roomToneOut` (k_roomTone...). Take window: the take's right-click menu (items 3 / 4 / 5: whole take / marks 1 - 2 / not), `markRoomTone` (one per window), the ROOM TONE badge in the take's bottom strip. Edit window: `roomToneEdit` → `RoomToneDialog` (candidates from `roomToneCandidates`, a combo when several; marks of this edit; "Not now"), `confirmAsync`, then `SimpleJobDialog` running `learnRoomTone (tracks, readTrack, len, rate, source, out, j)` (per track: a profile per channel + the mono mix, `learnProfile` over up to 60 s) → `project.roomTone`. Test: "2.81 denoise as a fix" (a box with a profile: outside in time bit for bit, hiss -12 dB inside, above and below the pitch range as it was, the tone kept; the room tone and a marked take through save / load).

**De-click.** `SpectralRepair::findLines (x, sr, t0, t1)`: N = 256 (512 at 96 k), hop N/4, bins from 2 kHz to min (20 kHz, 0.45 sr); a frame is a line when > 45 % of those bins stand > 10 dB above their median over +-40 ms (the frame's +-2 neighbours left out), its HF energy is at least -50 dB against the whole-spectrum median around it and above -80 dBFS (the trace of a mend in digital silence is not a click), the run is <= 8 ms, and the whole-spectrum energy after it is not 4 x that before (an attack). `tightenLine`: the smoothed |x[n] - x[n-1]| inside the line's (window-smeared) span, 6 dB above its median outside. `declick`: dropouts → lines mended whole (`mendGap` on the tightened span + 0.3 ms) → the finder's passes → lines again → `mendGap` → still there → `repair` Attenuate LeftRight, 256 bands, 400 % context, 20 Hz .. Nyquist; `lastDeclickReport()` (thread-local: mended / lines / linesMended / linesLeft) shown by the RepairDialog after De-click. `mendGap`: `interpolate` always with 160 poles (the probe `scratchpad/dck/t4.cpp`: with 55 + 82 Hz under the music a 216-sample gap came out 20 % wrong at 72 poles, 0.1 % at 160), and the hole / level checks on `rmsDiffRange` over >= 1024 samples (they doubled a gap in the trough of a bass note: `t6.cpp`). Tests: "2.81 declick: the picture check" (four spikes 0.7 ms apart the finder takes for an attack, a 3 ms top-heavy burst with no jump in it, a note's start is not a line); the older declick tests pass unchanged.

## 2.80: Noise mode

`SpectralPyramid` (core): the merged picture of the whole edit as bytes (0.5 dB steps), the finest level's hop = seconds / 60000 clamped 10 - 60 ms, then x4 until a level has < 400 frames; `addTrack (f0, f1, read, replace)` takes the louder of what a frame has and this track (FFT 2048, the same dBFS scale as `SpectralRepair::spectrogram`), reading in runs of 256 frames through the callback; `settle` remakes the coarser levels over a range; `slice (t0, t1, maxFrames)` gives the best level's frames over a view as a MergedSpectrogram with the exact times of its first and last frame. `SpectrogramView::setDataRange (spec, dataT0, dataT1, seconds)` draws a quick picture that covers only part of the time (the frame mapping uses dT0 / dT1; `setData` sets them 0 / duration), `onView` fires on every zoom / pan so the dialog can slice again, `setMarks` / `markPoint` for marks 1 / 2, `invalidateDetail`. Shared with Spectral Denoise (refactored out of `denoiseEdit`): `TrackParts` / `buildTrackParts` (which piece owns each moment of each track, built from the regions as they are now), `readTrackMix` (the mono mix of a track over a range, with an optional `ReaderCache`), `learnFileProfiles` (per track per channel from the parts under the boxes), `prepareDenoiseJobs`, and `prepareRepairJobs` (the repair window's prepare without the OUT / IN streams; the piece made again is just the span of the changes inside each region). `NoiseModeDialog` holds the app and the edit id directly: `base` (the regions at open / last Write back), `steps` (each fix's regions-before + RegionJobs + time span); `runFix (label, suffix, t0, t1, prepare)` runs the prepare on the job thread against a copy of the regions as they are now, then `applyRegionJobs` on the message thread, pushes the step, rebuilds `cur` (TrackParts), restarts a running playback from where it was (`playEdit` snapshots the segments at start), and remakes the pyramid's frames over the span (`refreshPicture`, which also invalidates the sharp picture); Undo pops a step (regions back, files deleted); Write back = `setUndoForEdit (base)`, base = now, steps cleared with the files kept; Finished / close = regions back to base, uncommitted files deleted. The sharp picture of a view under 180 s is rendered from the files (`readTrackMix` of the view plus 0.5 s, `picture::render` on that, `pic.t0 / t1` put back to the view's times); further out the pyramid alone is shown. The denoiser's decay in Noise mode is measured per piece by `denoiseFiles` (no picture tracks in memory). Render hook `scratchpad/rend` "noisemode" with FERMATA_NM_STEPS (drag / key / button names): ReTouch on a box, room tone from a box, Denoise of the whole edit, Write back - the edit came out with the repaired span cut as its own pieces and every piece denoised in its own place. Test: "2.79 Noise mode: the whole edit's spectral pyramid".

## 2.79: Spectral Denoise (edit window)

**Engine (Denoise.h).** `Profile::weight` (empty = all bins; else 0..1 per bin): `finish()` scales each bin's gain in dB by it (`pow (g, w)`), a bin at 0 keeps gain 1. The output is now `x + istft ((G - 1) X)` (the change added to the input; `Analysis::x` set by `analyse`) so anything with gain 1 passes bit-exactly - the golden case still matches the reference to > 60 dB. `SelRect` (t0, t1, f0, f1, add) and `selected (rects, t, f)` (later boxes override earlier ones); `selectedSpans` (the add boxes' time ranges, merged). `ProfileLearner::add (x, len, sr, p, sel, tOffset)` takes the STFT of each selected time span (a window either side) and, for frames that lie whole inside a span, accumulates |X|^2 per bin where `selected (tc, f_k)`; `finish()`: the mean per covered bin, uncovered bins filled from the nearest covered one (so the smoothing / presence next to the edge behaves as with a full profile) and weight 0 there, 1 where covered, then the 3-tap smoothing as `learnProfile`. `featherProfile (prof, octaves)`: inside each covered run the weight rises as a smoothstep of the distance in octaves to the run's edge (half a bin past the last covered bin); the covered set is never widened. **AudioOps.** `DenoiseRun::writeFrom / writeTo` (the part of the source the destination holds: an edit piece plus handles rather than the whole take), `featherIn / featherOut` (samples; `denoiseFeather` = a raised cosine from 0 at the stretch's edge to 1 that far in, applied as `x + w (y - x)` at the output). The per-piece group step is `denoisePiece` (presence maps -> `othersAgreed` -> gains), shared by `denoiseFiles` and the new `denoiseTracks` (channels in memory, each its own source - the window's picture). Tests: "2.79 spectral denoise" (a profile from boxes with a subtracted click, the fill of uncovered bins, the feather's shape, the rest of the spectrum and everything outside the stretch bit-identical, hiss -12 dB, the time feather's first sample, weight 0 everywhere = identity, a file piece with handles).

**Window (FixTools.cpp).** `SpectrogramView::setMultiSelect`: `extra` (earlier boxes) + the current box with `curAdd`; plain drag clears, Shift / Alt push the current box into `extra` first (Alt makes the new one a subtract); drawn as a mask (adds fill, subtracts clear) plus each outline (subtracts red); `selectionRects()` is the list in order. `SpectralDenoiseDialog` (modelled on RepairDialog; `SpectralDenoiseContext` has `prepare (SpectralDenoiseSettings, job)` instead of ops): `learn()` runs `ProfileLearner` per picture track + `measureDecay` over the whole area, keeps `learned` (unfeathered) / `decays` / `learnedSel`, then `denoise()`: `featherProfile` copies + `denoiseTracks` on the mono tracks (feathers from the sliders, threads = cores) -> `dnTracks` / `dnSpec`; `settingsStale` after a control change; `dnSerial` tags the prepared files; Undo drops `dnTracks`. `denoiseEdit`: the same per-track `Part` list as `openRepair` (no OUT / IN streams); `c.prepare` learns the FILES' profiles per track per channel from the parts under the boxes (once per `learnSerial`), then one `RegionJob` per piece: outFrom / outTo = the piece with 1 s handles, the denoised stretch = the piece, extended into a handle where the piece's edge is the region's own edge (an edit point: the crossfade there is then between two denoised sounds), the feather at the mark where the mark cuts the region; `denoiseFiles` with `writeFrom / writeTo` = the handles span, all the piece's files as one group (profiles per channel from the track's learners, the picture's decay for every channel); `PieceResult.from = max (fileStart, outFrom)`, `to` from the written length; apply / unapply / accept as the repair window (`applyRegionJobs`, suffix "denoise", `setUndoForEdit` "spectral denoise"). Pieces whose files start at different places (one track corrected earlier) are refused with a message. Render check: `scratchpad/rend` hook "sdenoise" (three synthetic drags - add, Shift add, Alt subtract - then L, then optionally a button): the window, the learned profile ("82 - 6656 Hz"), the denoised picture, and Write back producing "001 - Take (denoise)" / "002 - Take (denoise)" pieces between the marks. Not yet: the take-window version, and "Noise mode" (the whole edit in one multi-resolution window with gain / ReTouch / denoise / DeClick together).

## 2.78.2: SADiE imports, "Recorded with"

`buildImportPlan (sources, ImportNaming)`: Auto (everything, in order SADiE -> Pyramix -> take-number grouping -> lengths), or one way only (Pyramix / ProTools / Sadie / ByLength), from the import window's "Recorded with" box (`ImportDialog::setBase` rebuilds the plan from the kept `sources` and re-does the piece box and the stereo pairs). A forced choice relaxes that way's own guards (half the files instead of 80 %; SADiE without the equal-length requirement; the take-number grouping used even when the length grouping shared its tracks). `ImportPlan::readAs` says which way was used (shown at the top of the summary). The SADiE block: last '-', then the mic name and a two-digit take (three digits tried too; the better share of equal-length takes wins), mic names without spaces / '.' / '_'; accepted when >= 80 % of the files fit, >= 2 takes, no mic twice in a take, each piece's mics shared (all <= 1.5 x its biggest take) and >= 80 % of the takes exactly one length on every file. It cannot match Pyramix ("-L", "-C": no digits after the dash) or the Pro Tools forms ("01-01" leaves an empty mic name). Test: "2.78.2: SADiE" (two pieces, a piece without one mic, numbered room-tone clips); `scratchpad/imp/sim3.cpp` (the user's session layout: 57 takes, 7 pieces, 14 mics + 19 numbered tracks of the "rt" clips). The ImportDialog now takes the sources and the unreadable-file notes, not a plan.

## 2.78.1: the denoiser on the user's own recordings

**The cathedral sets (2026-10-21, no code change).** Three takes ending in a tail (12 - 13 tracks: spots Harp / Organ / Theorb / VO1 - 3, nearer pairs Vox / Inst, Omni, High, Rear, Wide; two tracks of one set digital silence - harmless: profile -105 dB, output silence). Measured rooms 2.5 - 4 s (the broadband tails fall ~40 dB in 2 s; 355 - 710 Hz ~17 dB/s). `tails2.py`: every band of every mic follows the original within 1 dB while the tail is above the band's noise, then eases to the residual over 0.5 - 1 s at the room's own rate - no step; at 3 - 7 dB above the noise the output sits 2 - 3 dB under the original, which is the Wiener answer (the tail alone is that much lower than tail + noise). Music 63 Hz - 16 kHz unchanged (<= 0.05 dB) except on the quiet spot VO2 (10 dB below VO1) above 4 kHz (0.5 - 1 dB; 3 - 6 dB above 16 kHz) and Rear / High above 16 kHz (0.5 - 0.8): bands where the music is within a few dB of the noise - the noise share goes, as it should, but this is where any dulling would be heard. Room tone down the full 12 dB in every band (`rtcourse.py`; a band-energy figure over a region that holds an event is misleading - the kept event dominates it). The silent "removed" check holds (-50 .. -80 dB vs the music). Group on costs 0.3 - 0.5 dB in room tone, as before. Listening set 3 sent (mix + Omni alone per set).

Five sets (96 kHz, spots 1 - 8 left to right, OMNI / ORTF / Room / Wides): two pure room tones, one room tone with a car / rustle / piano note, two takes that end in a tail with 3 - 4 s of room tone after it. Harnesses: `scratchpad/dnan/run.cpp` (learnDenoise + denoiseFiles, the dialog's path, all files as a group), `inventory.py`, `noisechar.py` (band levels, steadiness, coherence), `measure.py` (reduction in room tone, music kept per band), `cmp.py` (group 0 vs 1), `tails.py` (band envelopes through the tails), `decaystudy.py` (decay estimators against the final decays), `probe_pres.cpp` (presence maps), `mixes.py` (listening mixes). Findings: the noise is rumble-heavy (falls ~3 dB / octave to 1 kHz), steady below 2 kHz, with intermittent 2.5 - 10 kHz activity in the room tones; mostly uncorrelated between mics (a pair's two sides share the rumble below 160 Hz). Music above 125 Hz untouched (<= 0.01 dB to 16 kHz); room tone down 9 - 11 dB. Changes: (1) `decayRates` takes the 80th percentile of the run slopes (the mean of two) instead of the median - the median included diminuendos and measured 7 - 9 s where the final decays showed 30 - 60 dB/s; the 80th percentile matched the final decays within a few dB/s on both tails and was rarely faster (faster is the dangerous side); engine.py the same; the golden case is unchanged (its bands have two slopes each); a unit test with five decays. (2) `othersAgreed` replaces `othersMax` in `denoiseFiles`: a channel's others value is the second largest over the OTHER sources (a source = a file, counted by its loudest channel), so one spot's local rustle no longer raises every other track's presence (stage-1 maps: 1 - 11 % of cells confident in pure room tone on the spots, concentrated 1 - 16 kHz); with fewer than three sources it is the plain maximum (the pair test). Group cost in room tone: ~0.9 dB -> ~0.3 - 0.4 dB. (A threshold on the others value, smoothstep 0.4 - 0.85, made no difference - the cells were confident, just local - and is kept as `Params::groupSureLo / Hi`.) Not changed: the profile is still fixed for the whole take; the tails of these takes are short, so the long-tail behaviour wants a church recording.

## 2.78: strip selection and linking, hidden strips, track multi-remove, multi-series Pyramix imports

**Mixer.** `StripGang` (MixerWindow.cpp, owned by `MixerStripsHolder`, so the selection survives a rebuild; `pruneGang` drops strips no longer there): selected track ids, the anchor for Shift ranges, the `GangMember`s (each `ChannelStrip`) in order. `linked (id)` = two or more selected, id one of them, and Shift + Alt held right now (`ModifierKeys::getCurrentModifiersRealtime`, so a menu callback sees the keys held as the item was clicked). `forOthers` runs a function on the other selected strips only when linked. Fader: `linkFader` adds the dB difference to each (-inf counts as -80, clamped -80..+12; their faders follow on the meter tick). Pan: `linkPan` (same side of a stereo strip, both sides when a mono pan moved, one pan on a mono strip). M / S: same state. Sends: `nudgeSend` (the same dB difference, off counts as kSendOffDb, a send to an audio track only where the other strip already has one; `AppContext::setSendLevel`, so routing changes rebuild the plan); `SendDial::forOthers` / `SendPanel::ForOthers` (level fader, MUTE, PRE / POST, Follow mute). Inserts: `SlotButtons::gangSlots` gives the other strips' slot arrays; insert / remove / bypass and an Alt-drag copy go to the same slot (or the first free one; nothing is replaced: `slotFor`), each strip its own instance (`plugins.create` / `makeFactory` with the state for a copy), then `structure.sendChangeMessage()` (a strip may need another slot row). `noModeSwap`: JUCE switches a slider to velocity mode while Ctrl / Alt / Cmd is held (ctrlAltCommandModifiers): off on the strip faders, pans, send dials and the send panel's fader. The outline: `StripBase::paintOverChildren` with `gangLook()` (1 blue = selected, 2 orange = linked; `StripGang::tick` repaints when the keys go down / up). The name plate's left click selects (`plateClicked`), right click is the plate menu (Colour..., Hide, Select every audio strip, Select none); the coloured bar keeps the colour picker. **Hidden strips**: `MixerState::hiddenStrips` (message thread), saved as "hiddenStrips" in `mixerToVar` (so the master-render hash sees it), `AudioEngine::buildPlan` leaves a hidden track's sends (also held ones) and output out of that mixer's plan; `ChannelStrip::hideStrips` lets go of its solo first; the meters strip's + Strip menu shows them again. Test: "2.78: a track hidden from one mixer reaches nothing in it".

**Take / edit windows.** `selectedTracks` + `trackAnchor` + `clickTrackName` in `TakeTimeline` and `EditTimeline` (plain click: only this one, or none if it was the only one; Shift: range from the anchor over the shown rows; Ctrl: toggle). Take window: a left click in the name column (after the M / S / ARM switches; not with Alt) selects; the right-click menu gets "Remove the N selected tracks..." (`TakeWindowDef::removeTrackClips` each, one confirmation). Edit window: Shift / Ctrl + click selects at once; a plain click selects on mouse-up when the name was not dragged (dragging still reorders); right-click = menu (Colour..., remove selected / this track via the new public `EditWindowComponent::removeTracks`); the bus view keeps the plain colour picker. Both windows' modifier hints mention it.

**Import.** The Pyramix pattern is now `^(?:(.*?)[ ]+)?(\d{1,4})_(.+)$` (piece optional, 1 - 4 digits) and is only used when its takes share their tracks (two or more takes, biggest >= 2, all instrument names <= 1.5 x the biggest take) - or, for a single take, only in the old strict form (a piece name and 2 - 4 digits: "Song 001_gtr-L"). The user's NYCGB folder (0 ... 106 with no piece, plus "CT TEST", "CT TEST 002", "CT TEST 003"): 110 takes x 12 tracks; 2.77.3 gave 1320 tracks because `numGroups` needs one word position that fits every file. Core test "2.78: the user's folder"; `scratchpad/imp/sim.cpp` + `sim2.cpp` (25 naming cases) all give one set of tracks.

## 2.77: the background denoiser

**Where it lives.** `src/core/Denoise.h` (namespace `td::denoise`; its FFT is pocketfft in `src/core/DenoiseFft.cpp`, the only place that header is compiled): `Params`, `Profile` (mean power per bin of the room tone), `learnProfile`, `measureDecay` (a pass over a channel: the room's decay per 1/3-octave band), `analyse` (one stretch in memory up to the presence map, into an `Analysis`), `finish` (the gain from an `Analysis`, with or without an "others" map, and the output), `processSegment` = analyse + finish on its own (the single-track algorithm = the reference), `presenceSegment` (the presence map as bytes), `dilate` / `othersMax` (the cross-track maps), `processChannel` (a long channel in 15 s pieces with 6 s of context before and 2 s after). `src/core/AudioOps.cpp`: `learnDenoise` (a `DenoiseProfiles`: one profile per channel of a file, the first 60 s of the marked region at most, and with `decaySeconds` the decay per channel over up to that much of the file after (or before) the room tone), `denoiseFiles` (all the files of a take together: pass 1 the decay where the profiles did not bring it, then the untouched parts copied bit for bit (no dither) - or written as silence for a preview, which also ends with the stretch - then the stretch in 10 s pieces with context, every channel of every file: stage 1 `presenceSegment` + `dilate (+-2 frames, +-1 bin)` per channel, `othersMax`, stage 2 `analyse` + `finish` with each channel's others map; `parallelFor` over channels with try/catch per thread; 24-bit with dither; optionally the 'removed' files = input - output; a progress callback that can stop it; `readRange` failures are errors, never silence), `denoiseFile` (one file on its own = groupMode 0). `src/ui/FixTools.cpp`: `DenoiseContext` / `DenoiseDialog` / `denoiseTake` (the take-window tool, same pattern as `repairTake`: learn (profiles + decay over 3 minutes, tracks in parallel) -> preview files applied to the take's file list so the ordinary `app.playTake` plays them -> unapply -> whole-take files -> swap with `app.fixUndo`). While preview files are applied `app.saveHold` is up: `saveNow (quick)` and the automatic save skip, so a crash can never leave the project file pointing at preview files; `app.undoHold` as for the other tools. `tools/denoise_reference/`: the Python reference (`engine.py`: the per-track algorithm; the cross-track part exists only in C++, in `finish`) and the bench; `tests/data/denoise/`: a golden case made by `make_golden.py`; `tests/core_test.cpp` "2.77 background denoiser" checks the C++ against it (SNR > 60 dB, same profile level, decay rates and noise reduction), the piecewise path against the one-piece path, a pure tone (within 0.2 dB), the file functions (bit-exact copy outside the stretch, per-channel profiles, removed = in - out, digital silence outside the stretch in 'removed'), and the group: a tone 80 dB down on the right channel of a pair, clear on the left, comes out at least 2 dB stronger on the right with the group on than off, the left unchanged, and a preview run (silence before, ends with the stretch) has the same gains.

**The algorithm** (see the comment at the top of Denoise.h). STFT: periodic Hann, N = the power of two nearest 85 ms (8192 at 96 kHz, 11.7 Hz per bin), hop N/4, N zeros before the signal and 2N after, real-to-complex transforms by pocketfft (`DenoiseFft.cpp`; an earlier version packed two real frames into one complex `juce::dsp::FFT`, pocketfft is about twice as fast on the 8192 window). a posteriori SNR gamma = |X|^2 / (profile x 10^(amount/10)). a priori SNR by the decision-directed rule (alpha 0.85 per hop), forwards AND backwards, the larger of the two (backwards sees onsets coming; forwards follows tails). The tail rule on the estimate: the forward xi may not fall faster than the band's decay rate + 6 dB/s (per hop a power ratio), seeded only from an SNR >= 3 dB smoothed over +-2 bins x +-2 frames (a lone noise spike never starts a 'tail'). Smoothing of xi over +-2 bins in dB. Harmonic protection: a local maximum (+-2 bins) standing >= 8 dB above the mean of bins 4-7 away on both sides (a broadband burst never does), gamma > 3, persisting 3 frames; the run and +-1 bin keep xi >= gamma - 1 and count as present; not in the bottom / top 8 bins. Gain: Ephraim-Malah MMSE-LSA (E1 by series / continued fraction). Presence (OM-LSA style): the a posteriori SNR (or 1 + the tail prior, whichever is larger) in dB, averaged over +-2 frames and +-2 bins (local) / +-16 bins (global), through smoothsteps (1.5..6 dB, 0.8..3 dB) -> a priori absence q <= 0.98 -> p = 1 / (1 + q/(1-q) (1+xi) e^-v) -> G = G_lsa^p x G_min^(1-p). Floor G_min = residual. The tail rule on the gain too (per bin, dB per hop), the high-frequency ceiling, +-1 frame smoothing, clip to [G_min, 1], ISTFT. Decay measurement: 1/3-octave bands from 50 Hz; per band the dB energy per frame (3-frame mean), decays = monotone runs (<= 1.5 dB rise allowed) from a local maximum >= 15 dB above the band's noise down to 6 dB above it, >= 0.4 s long, least-squares slope; the median of >= 2 slopes; unmeasured bands take the nearest measured one; a 3-band median; clipped to 6..60 dB/s (10 s .. 1 s).

**What the bench showed** (`bench.py`, ours vs a conventional short-window threshold denoiser with a 350 ms release vs an oracle Wiener mask that knows the truth): on a quiet chord (-36 dBFS, 80 partials with vibrato) in real room tone at -40 dB the partials above 3 kHz keep -0.15 dB (oracle -0.21, baseline leaves +0.6 dB of noise in them); at a hopeless -15 dB SNR -2.3 dB (oracle -2.4); on a synthetic church tail into real room tone the output follows the oracle to within 0.1 dB in every band at a -12 dB floor, with no step anywhere, while the baseline sits 3-10 dB above it; noise-only stretches come out at the residual exactly (12.0 of 12 dB); residual-noise flicker 0.04-0.2 dB above the input's (baseline 0.4-0.6). The real omni of the Ice Shining session: 12.0 dB off the floor, 0.00 dB change in the 1-16 kHz bands of the singing. Lessons that cost time: (1) a protection rule that trusts persistent peaks in the RAW spectrum protects rumble bursts (broadband, above the profile for many frames, full of chance local maxima) - prominence against the bins 4-7 away fixed it; (2) `np.roll` wraps: bin 0's "neighbours" are the top bins, so DC looked like a huge peak and sub-20 Hz rumble passed, collapsing the RMS-based reduction figure - the end bins are excluded and the level metrics are 30 Hz high-passed; (3) "clean + residual x noise" is not a fair tail reference once the tail is under the noise - the oracle mask is; (4) a tail prior seeded by noise flicker smears every burst into a slow decay - hence the 3 dB seed threshold; (5) window names change after recording and imported files carry other projects' numbers, so header 'No' verdicts are only trusted for Fermata's own recordings (that one is in the relink, same day). Speed: about 18x real time per core at 48 kHz (330 ms for 6 s), ~10x at 96 kHz with the 8192 window; the smoothing passes cost more than the FFTs. The dialog was exercised end to end in the Linux render harness (`scratchpad/rend`, hook `denoise2`: opens the take, marks 0.3-2.3 s, opens the dialog, presses Play): the eight demo tracks learn in about a second, the preview files are made and swapped into the take (the take display shows the denoised waveforms), and only the playback itself fails there, for want of an audio device. **The cross-track part** (`finish`, `Params::groupMode`): each channel's presence map (0..255) is dilated by +-2 frames (+-42 ms at 96 kHz: the delay to a mic 10-14 m away) and +-1 bin; a channel's "others" map is the largest value any other channel has in the cell (the two largest per cell, found once). Mode 1: presence = max (own, 0.85 x others) - what another track hears as music is kept here at (nearly) its own Wiener gain instead of being floored. Mode 2 adds presence = min (presence, 0.25 + 0.75 x others) for cells no track protects as a harmonic: what no other track hears is pushed towards the residual. The tails still come from each channel's own decay rule. Costs a second analysis per piece (about 1.7x); the reviewer's probe harness of the 2.77 review (`scratchpad/probe`) showed the single-track port at 150 dB against the reference, so the golden test's 109 dB is the 24-bit golden file. Not done yet: a second, short analysis window for consonants (the 85 ms window lets the noise through for ~40 ms around a transient), a second pass re-estimating the profile from frames the first pass found empty (noise that drifts during a take), linked gains for the two channels of a stereo file (the image could wobble in very quiet passages), time alignment of the others map by measured mic delays instead of the +-2-frame spread, the edit-window version (whole pieces), and listening on the user's own material. Known small things: a project opened or closed from a menu while a preview is applied saves the preview paths (`saveNow (false)` is not held; the dialog normally unapplies first); `uniqueFile` keeps handing out higher numbers for preview names within a session; the Waveforms folder keeps thumbnails of deleted preview files.

## 2.76.1: the Windows build

`<windows.h>` without `WIN32_LEAN_AND_MEAN` includes the RPC headers, and `rpcndr.h` has `#define small char` (and `hyper`); `windef.h` has `#define near` / `far` / `pascal` as nothing. JUCE's own native headers include it lean and `#undef` min / max, so nothing in Fermata ever saw these - until `src/core/FileSync.h` (which includes `<windows.h>` itself for `FlushFileBuffers`) was included by `MainWindow.cpp` in 2.75, whose `sizesFor` had a lambda parameter called `small`: MSVC reads `int small` as `int char`. Rules now: `FileSync.h` includes it lean and undefines the lot; no identifier in `src/` may be called `small`, `near`, `far`, `hyper` or `pascal` (renamed: `TakeWindow.cpp` rowMin / rowMax, `FixTools.cpp` smallCaption, `TrimWindow.cpp` nearTo, `SpectralRepair.h` nearLevel / nearCount); `tools/check-windows-code.sh` scans for them (outside comments, strings and preprocessor lines) and fails on a hit, and `tools/win-stubs/windows.h` defines the same macros as the real header so the stub compile of `FileSync.h` exercises the undefs. The Linux syntax checks cannot catch this (g++ never sees windows.h), which is why it reached the Windows build.

## 2.76: missing audio files found again; the keys never dead

- **Relink (`src/core/AudioRelink.h`, `Project::relinkAudio`):** `relink::Wanted` (the missing file + what the project knows: rate, channels, length in samples, bits (24 when `TakeFile::sourceName` is empty = recorded by Fermata), track name, take tag `pad3 (number)`, window name, `recordedAt`, `ownRecording`) is built once per path from the take windows (the take says the most) and the edits' pieces / overdubs (`sourceLength` only when `fileStart == 0` AND the piece's `takeName` has no " (" suffix - a processed piece ("003 - Kyrie (declick)") may have a short file that starts at the take's start, so its length is not known and it is found by name only, like a waiting piece's files; a path several pieces describe keeps the doubt). The files that exist go into `inUse`, every track name (TrackDefs, TakeFiles, RegionFiles) into `trackNames`. `relink::find`: pass 1 by lower-case name - only candidates that `sizeCouldFit`, and when the fingerprint is known only those whose header matches it (`Candidate::fingerprintMatches`: the same name in another project's folder is NOT the file); several left: the one whose folder is named like the old one, else nothing is taken and they are `alternatives` (relative paths). Pass 2 for the rest in rounds - the WAV/AIFF candidates are indexed by byte size (`bySize`, binary search on `Candidate::sizeRange`) so a wanted file only touches the files that can hold it (compressed files are always looked at), `readHeader` (once per file, via `AudioFormatManager`, progress every 8 reads) gives rate / channels / bits / length / BWF description, originator, date, time; a file fits when rate (±0.5), channels and length are equal and bits equal when known. Several fits are narrowed by three-state tests (`Says::Yes / No / Nothing`): a Yes wins (only the Yes files stay); with no Yes the No files drop out and the files that say Nothing (no header: exported by another program) stay - a silence never rules a file out. In order: the BWF description (`<window> / <takeTag> - <label> / <trackName>` as `AudioEngine::startRecording` writes it; `looksLikeFermataHeader` or it says Nothing; another take number or track = No, except a description of 250+ chars, which was cut when written; the same take and track from this window = Yes, from another window or from this window under an older name = Nothing, as windows get renamed), then `secondsFrom (date, time, recordedAt)`: < 180 s Yes, farther No, no date Nothing - both only for `ownRecording` files (an imported file's header is another program's or another project's take numbers / times) - then `fileNameNamesTrack` (the sanitised track name as whole words in the file name, and no longer track name of the project that contains it: "Violin 2" is not "Violin"), then the take tag at the start of the file name; exactly one left = taken (claimed: never given to a second missing file; `inUse` files are claimed from the start), else `alternatives`. Rounds repeat while something was taken (a file picked for one missing file leaves its sibling with one fewer). After relinking `Project::relinkAudio` calls `structureChanged()` and `undoReset()` (the hash covers file paths, so it would have been an undo step that silently puts the missing paths back). `Project::missingAudioCount (quick)` dedupes paths and (quick, unless `filesMoved`) skips the files of verified takes whose folder exists (a missing folder counts all its files); `missingAudioList` names where each is used. `importWindowFile` clears `verified` on imported groups (verified by the other project) and reports the imported window's own missing count (`int* missingFiles`). UI: `MainComponent::relinkAudioDialog` (a full `missingAudioCount()` first under the busy window - "Nothing is missing" without a chooser; the menu items are always enabled, as the quick count trusts verified takes; the recording check is repeated in the chooser callback; busy window during the search; the report with 12 / 8 / 8 lines and "every one is in the log" - still-missing files are logged too, 100 at most), `AppContext::openProject` asks ("Find the audio files..." -> `findMissingAudio` set by the main window) when the quick count is > 0. Test: "2.76 finding missing audio files".
- **Keys (Uikit.h `ToolWindow`):** JUCE hands a key to the peer's component when `currentlyFocusedComponent` is null (`ComponentPeer::getTargetForKeyPress`); `ToolWindow::keyPressed` now gives such a key to its own contents first (`! focusIsInside()`, not for forwardKeys windows) and re-grabs. `ToolWindow::FocusKeeper` (a `Desktop` focus-change listener, one instance in `FermataApplication`): focus gone null -> `callAsync` -> if still null, Fermata in front, no modal component: the active `ToolWindow`'s `grabContentFocus()`, or the main window's contents (`ResizableWindow::getContentComponent`, when it wants focus). `showWindow` sets `setWantsKeyboardFocus (true)` on every window's contents (and the inner of a `CornerStripHost`), `MainComponent` too, so Fermata's own `grabContentFocus` -> `Component::grabKeyboardFocus` on the contents stops there instead of descending to the first focusable child (`FocusTraverser::getDefaultComponent`: sorted by y then x - the mixer's name box, the first input's name in the main window; JUCE's `handleFocusGain` itself gives the keyboard to the `TopLevelWindow`, which wants it). **Foreground guard:** JUCE's `TopLevelWindowManager::findCurrentlyActiveWindow` keeps the previous active window while the keyboard is in a non-JUCE HWND of this process (a plug-in editor's child window: its JUCE peer only takes focus on a mouse-down on its own HWND; an ASIO panel), so `getActiveTopLevelWindow()` is stale then. `grabContentFocus`, `FocusKeeper` and `KeepKeysOnTimeline::scrollBarMoved` therefore also require `td::isForegroundWindow` (`WinOwner.h`: `GetForegroundWindow() == hwnd`) - without it, a click into a plug-in's value box pulled the take window over the plug-in and typed into it. **Tab** with the keyboard on the window / its contents as a whole is swallowed by `ToolWindow::keyPressed` and `ToolWindowMain::keyPressed` (JUCE's `ComponentPeer::handleKeyPress` fallback would move it into the first text box); inside a box Tab still moves on. The take / edit name boxes' Enter gives the timeline the keyboard directly (was `unfocusAllComponents`). `globalKeyHook` returns false without a project (the StartPanel's own key block only worked while it had the focus).

## 3.13.0: hybrid CPUs, crash leftovers, the stall watch's numbers
- `makeCorePlan (cores, settings, limitFastCores = true)`: on a hybrid CPU (`isHybridCpu`: the efficiency classes differ) at most `maxFastCoresForAudio` = half the big cores (rounded down) go to the audio, the higher-numbered first; the others ticked are moved to the general set with a `warning`. `limitFastCores = false` reproduces the older plan, only for `releaseLeftovers`. `pickQuietCores` on a hybrid CPU takes big cores only, within the same limit (it may return fewer than asked, or none).
- The fence writes the CPU-set ids it gives other programs to `%APPDATA%/Fermata/cpu-fence.txt` before it starts and deletes it after it has set them free. `CoreReservation::releaseLeftovers (saved)` (Main, before `configure`) frees every program whose default CPU set is exactly those ids, or exactly the general ids of the saved settings (limited and unlimited plans: covers versions before 3.13.0).
- Laptop (`GetSystemPowerStatus`: a system battery): once, marked by the user setting `cpuLaptopChecked`, `keepOthersOff` is switched off and saved. While cores are reserved the process gets ABOVE_NORMAL_PRIORITY_CLASS on a laptop, HIGH elsewhere.
- `SystemLoad` (CoreReservation.cpp, Windows only): snapshots of `GetSystemTimes`, `GetProcessTimes` and the message thread's `GetThreadTimes` (handle opened by `rememberMessageThread`, called from StallWatch's constructor). The stall watch snapshots when it notices a stall and when it ends; `describe` gives the main thread's share of the wall time, Fermata's processor use in cores, the PC's busy percentage and the power state, and a verdict (PC >= 85% busy: flat out; main thread >= 70%: Fermata's own work; < 30%: waiting). `tickLoad` adds the same (without the main thread) to the minute's timing line.
- The Windows stubs (`tools/win-stubs/windows.h`) gained FILETIME, the three times calls, SYSTEM_POWER_STATUS, GetSystemPowerStatus, SetPriorityClass, GetCurrentThreadId.

## 3.14.0: the Help menu, crash and freeze reports
- `src/core/CrashReport.{h,cpp}` (new, in CMakeLists, the sandbox check and the Windows-code check): `crashreport::install (folder, version)` at start-up (Main, before the crash handler) copies the folder and version into static wide-char buffers and loads dbghelp.dll's `MiniDumpWriteDump`, so `write (exceptionPointers, why)` needs no heap during a crash. Dump type: indirectly referenced memory, unloaded modules, thread info, process thread data (a few MB). Names: "Fermata <version> <crash|abort|freeze> <date> <hhmmss>.dmp". `describeException` decodes the EXCEPTION_RECORD (code, access-violation address, module + offset via GetModuleHandleExW FROM_ADDRESS). `allReports` (newest first), `reportsSince`, `prune (5)`.
- Main: `crashHandler` writes the report, then the log line (kind and place) and JUCE's stack trace. On Windows `std::signal (SIGABRT, abortHandler)` covers abort() (uncaught C++ exceptions, pure virtual calls). `announceCrashReports` (after the main window shows): reports newer than the user setting `crashCheck` (the previous start) are logged and announced once, 4 s after start; the setting is moved to now; `prune (5)`.
- StallWatch: once the main thread has been stuck for 15 s (`kFreezeReportMs`), measured by the watch thread's own clock and reset when that thread itself did not run for 2 s (sleep), `crashreport::write (nullptr, "freeze")`, at most two a session, with a log line.
- The .pdb: CMakeLists already builds with /Zi and copies Fermata.pdb into dist/Fermata, which the workflow's artefact uploads with the exe - keep it with each build to read its reports.
- `src/ui/HelpMenu.h` (new): `showHelpMenu (app, target)` (the main window's new last menu-bar entry, `helpButton`); `findFermataManual` (user setting `manualPdf`, else a *manual*.pdf next to the exe or in Documents\Fermata), `openFermataManual` (asks with a file chooser when none is found), `aboutFermataText`.

## 3.15.0: ignored missing files, the closing log, the busiest threads
- `Project::ignoredMissing` (full paths, saved as "ignoredMissingAudio" only when not empty, read in fromVar), `ignoreMissingAudio()` (adds every missing file of `forEachAudioFile`, full check). `missingAudioCount` / `missingAudioList` leave the ignored paths out (quick or not), so the open-time message and Find missing audio files skip them; the relink itself is unchanged. AppContext: `askIgnoreMissingAudio` (the open-time message's middle button: lists up to 12, Confirm / Cancel, then saveNow) and `askUnignoreMissingAudio` (Project menu item 19, shown only when the set is not empty).
- `src/ui/ClosingLog.h` (new): `closing::begin / step / end` and `closing::Mark` (a member whose destructor logs a step; placed before a member, it times that member's destruction). Main::shutdown begins it and ends it just before the logger goes; AppContext::shutdown and ~AppContext log their steps; Marks before project, devices, engine, plugins, takeMatch and after takeMatch time the member destruction. Nothing is logged unless begin() was called.
- `SystemLoad::take (true)` also lists the process's threads (Toolhelp TH32CS_SNAPTHREAD, GetThreadTimes); `describe` then names each thread using >= 10% of a core (at most four) by GetThreadDescription (JUCE names its threads; the message thread by its remembered id; others "unnamed thread <id>"), and gives the main thread's share. Used by tickLoad's minute line only.

## 3.16.0: the edit's start anchored, bars in Find other versions, window names, no nudges
- `EditDef::leadIn / leadInSeconds` (saved as "leadIn" / "leadInSeconds" when set, in the undo hash), `startTarget()`, `anchorStart()` (Edit.h, after the struct: every region += delta, then `rippleAfter (min / 4, delta)` for the overdubs, markers, flags, guide, marks, playhead and time-fixed automation). `AppContext::anchorEdits (always)`: run from the undo ticker before `undoTick` (so the move and the change it follows are one undo step), skipped while a mouse button is down, the engine plays, a modal component is up, or that edit's Trim window is open (`windowIsOpen ("trim:<id>")`: the Trim window's view and its Revert snapshot are in timeline samples); `afterProjectReplaced` runs it with always = true before `undoReset`, so the history starts anchored. Edit window: `leadInBox` / `leadInSecs` / `leadInUnit` at the right of the info row; `commitLeadIn` rounds to 0.01 s; the timer keeps them in step with undo.
- `takematch::BarMap` / `barMapOf (fp, barIn, barOut, length)` (TakeMatch.h): bars spread over the music span (first / last frame within 35 dB of the loudest, never below kSilenceDb). VersionsComponent: `passageBars` from the source take's BarMap; when known, takes without bars (counted `noBars`) and takes without any of the bars (`leftOut`, now strict: no 2-bar slack) are skipped; each searched take's fingerprint is sliced to `searchSpan` (the bars +- 2 bars + 5 % of the music + 2 s, at least the passage + 4 s) and the versions shifted back by the slice's offset; `placePart` uses the full fingerprint.
- Main: edit windows are titled "Edit - <name>", trim windows "Trim - <edit name>"; `refreshWindows` renames both; the edit window's name box calls `refreshTitles` on commit.
- TrimWindow: the Slide A / Slide B / Move the join groups (and slideA / slideB / moveJoinBy) are gone; the controls area is 171 px instead of 264.

## 3.17.0: why a closed Fermata stayed behind, and the tools to see inside a stuck one
- The 29 Oct log: closing stopped right after "the rest of the program's helpers freed", i.e. inside ~TakeMatchService (the next Mark), while the process went on reading audio at ~240 MB/s and grew to 22 GB; the next start, competing with it, looked stuck at "Checking the recordings (353 of 367)". Exactly what the take matching thread was doing is still to be seen (the thread listing below will show it); the fixes make it harmless either way.
- `TakeMatchService::stop()`: stops the timer, sets cancel, signals, waits up to 3 s (`waitForThreadToExit`), then leaves a still-busy thread alone (logged once) - never `killThread` from here; the destructor calls stop() and, only if the thread still runs, `stopThread (2000)` (JUCE's ~Thread would otherwise wait for ever with stopThread (-1)). AppContext::shutdown calls it first (closing step "take matching stopped").
- `ClosingWatchdog` (ClosingLog.h): started in Main::shutdown right after ctx->shutdown() (saved, device closed); a detached thread on a never-deleted object; 15 s for `finished()` (called after closing::end, before the logger goes), else append "Closing: not finished ..." + `describeThreads` to fermata-log.txt directly and `crashreport::endThisProcess()` (TerminateProcess); after finished(), another 20 s, then the same (the process normally ends long before and takes the thread with it).
- `StepTimer` (ClosingLog.h) and `AppContext::openSteps / openStep`: openProject logs each stage; afterProjectReplaced logs its sub-steps (only while opening: openSteps is null otherwise). `Project::slowTakeLog`: repairInterruptedRecordings names any take that took >= 0.5 s.
- `crashreport::describeThreads (mainId, mainFrames, otherFrames, onlyThread)` (Windows x64): Toolhelp thread list first; then each thread SuspendThread -> GetThreadContext -> `unwindStack` (RtlLookupFunctionEntry / RtlVirtualUnwind into a fixed array, leaf frames from [Rsp]; under MSVC inside __try) -> ResumeThread; only after all are running again: names (GetThreadDescription) and symbols (SymInitialize once with the exe's folder, SymRefreshModuleList, SymFromAddr + SymGetLineFromAddr64, else module+offset). StallWatch: at 5 s stuck (`kStacksMs`, five a session); tickLoad: a thread >= 90 % of a core three timing minutes running (`SystemLoad::busiestThread`), twice a session.
- Freeze report: `crashreport::writeViaHelper` runs `Fermata.exe --fermata-dump <pid> "<file>"` (CREATE_NO_WINDOW) and waits up to 60 s; `FermataApplication::moreThanOneInstanceAllowed` is true for that command line and `initialise` hands it to `crashreport::helperMain` (OpenProcess + MiniDumpWriteDump of the other process) and quits; `shutdown` does nothing in that mode. The in-process write stays as the fallback, and for crashes.
- Thread names: JUCE 8 names threads only in debug MSVC builds (RaiseException 0x406D1388), so release builds showed every thread as unnamed. `crashreport::nameThisThread / nameThisThreadW` (SetThreadDescription) is called at the top of every run() of ours, in the WaveThumb / WaveColor pool jobs, and once in the audio callback (a wide literal: nothing allocated).
- Stubs: tools/win-stubs/windows.h gained CONTEXT (Rip, Rsp), the unwind calls, Suspend/ResumeThread, GetThreadContext, CreateProcessW, WaitForSingleObject, GetExitCodeProcess, TerminateProcess; tools/win-stubs/dbghelp.h is new.

## 3.17.2: the take matching's busy loop (the real cause of 3.17.0's lingering process)
- `TakeMatchService::run` took "is there a job?" from `job.takeId.isNull()` - but a default-constructed `juce::Uuid` is a NEW RANDOM id, never null. With the queue empty it "analysed" a made-up take with no files, stored it in `known` under that id and went round again at once: one core at 100 %, `known` growing without end (22 GB), and minutes of freeing on closing. Now the queue says whether there is a job (`have`), and `Job::takeId` starts as `juce::Uuid::null()`. Other `juce::Uuid` members made without a value are listed for the second sweep (docs/CODE-SWEEP-BRIEF.md).

## 3.18.0: Rec mode, the Rec | Pre button, the Pre length, take matching under the person's control
- AppContext: `recModeOn` (never saved: off at every start), `setRecMode` (refused off while recording; tells `takeMatch.setRecHeld`), `recModeHolds (what, startAgain)` (Rec off: false; recording: a notice, true; otherwise the "Rec mode is on" AlertWindow - "Switch Rec off and start" runs startAgain after setRecMode (false) - and true). `toggleRecord`: without Rec mode, the "Rec mode is off" warning ("Switch Rec on and record" / Cancel; `recModeAsking` stops a second one stacking while it is up), after the project and rate checks.
- Heavy work that asks first in Rec mode (each re-runs itself through startAgain, with SafePointers): BounceComponent::start, MasteringComponent::startJob (VM, DDP, listening copies), AafExportComponent::run, VideoExportComponent::run, RetimerPanel::make, the three plug-in scans (Audio settings, the mixer slot menu, the Plug-ins window), and in FixTools (`heldByRec`) importTakes, importAaf, copyProject, exportForProcessingEdit/Take, the pitch / pitch curve / pitch tabs, Spectral Repair, Noise mode and ReHarmoniSer entry points (pitchTake / pitchEdit skip it while `gMakeProcessPage`: the tabs entry points asked already). The take matching was the only heavy work Fermata did by itself (no other timer scans folders or analyses audio); the waveform readers go on (a take just recorded needs its picture).
- `RecPreFlag` (MainWindow.h) replaces the Pre-Rec StatusFlag (same place, `flagPre`; `StatusFlag::wantedWidth` is virtual now): halves at width / 2, Rec fill red + right half outline red, Pre fill green; per-half tooltip from `getMouseXYRelative`; right-click -> `showPrePopup` (RecPrePanel.h: Pre on / off and `PreLengthControl`). The never-shown `sessionButton` is gone. Stream Deck: command "recmode", state field "recMode" (remote::State).
- Pre length: `AudioEngine::setPreRollSeconds` (1..10, atomic `preRollSeconds`, default `kPreRollDefault` 5; `kPreRollSeconds` 6 is gone) -> ensurePreRoll (a new cap means a new ring from the RingBuilder; not while recording - stopRecording's ensurePreRoll picks it up). `AppContext::setPreRollSeconds` saves the user setting "preRollSeconds"; MainComponent applies it before switching Pre on at start-up. `PreLengthControl` also in Audio settings (window 594 px tall).
- TakeMatchService: `TakeWindowDef::takeMatching` (Takes.h; saved as "takeMatching" only when on; in saveHash, stripped from undoText, not copied by restorePiece: a setting, not an undo step). `enqueue` and the sweep take only windows switched on (`isOnFor`); `windowsChanged()` prunes the queue and cancels the take under way if its window went off, then sweeps. `setPaused` / `setRecHeld` -> `holdChanged`: held, the take under way is cancelled with `requeueOnCancel` (it goes back to the front as Queued, not away); run() idles (`wake.wait (500)`) while `isHeld()`, and re-checks after clearing `cancel` so a hold between pop and start still stops it. `progress (onlyWindow)`: windowsOn / total / ready / cannot / nowTake / nowProgress / secondsLeft (remaining audio - Queued takes with a cache file count as nothing - over `audioPerSecond`, an average of fresh analyses; -1 until one is done). `Entry::hasCache` is noted at enqueue.
- UI: `MatchBox` (MATCHING, top row after DEPTH; text from `takematchui::boxText`) opens `TakeMatchPanel` (CallOutBox: status line, Pause / Resume, a row per take window with ON / OFF and "n / m analysed"); `TakeMatchLamp` in each take window's marks row left of the recording lamps (click on / off, right-click the panel). J and `showVersions` call `takeMatchingUnavailable` (window off: offers to switch it on); their "not analysed yet" texts say when it is held or paused.
- Tests: core (Pre length 1..10, clamped, kept by a take started before a change and used after Stop; takeMatching saved / absent in older files / not undo; recmode command and state; TakeMatchService off / paused / Rec-held / done / switched off); stress test changes the Pre length while the audio thread runs.

## 3.18.1: the take window's marks row
- TakeWindowComponent::resized: matchLamp and recordLamps now start at Next Take's x; markLabel / statusLabel follow them, to the window's right edge when the flowed buttons end above the row (`buttonsBottom`, from grid::flow's height in the wide layout), else to the readout's right edge. markLabel never squeezes (minimum horizontal scale 1.0; statusLabel 0.9).
- `paintLampWords` (RecordMonitor.h): the shared two-line lamp writing for RecordLamps (marks-row size) and TakeMatchLamp - name about 0.28 of the height (6.5-7.5), word about 0.36 (7.5-9.5), equal gaps; a long word squeezes to 0.7 at most.

## 3.18.2: the second code sweep - area A (starting, closing, threads) and area B (recording, saving)

### Area A: what changed
- Default-constructed `juce::Uuid`s that meant "none" (the 3.17.2 fault class): EditTimeline `shuffleId`, `clipMoveRegion`, `clipMoveTrack`, `trackAnchor` (a fresh edit window's first drag went into the shuffle branch of mouseDrag - `settleGapFades`, `markDirty`, cursor, `stopPlayback` - and its second into the clip-move branch; the piece / mark / fade / row drag that was meant never ran, and the shuffle branch of mouseUp returned before the others); TakeTimeline `markDragTake` (mouseDown and mouseUp set it to `{}`, a random id, so `if (! markDragTake.isNull()) { ...; return; }` returned before the `scrubbing` branch: ruler-drag scrubbing, documented since 2.x, was dead), `dragId`, `lastClickedTake`; `AppContext::auditionId` and `setAudition ({})` (a random id, which `findMixer` then failed to find - it worked by the fallback), `RecordMonitor::windowId / takeId`, `PlayInfo::windowId / id`, `DiscPlayItem::editId`; `automationPlanFor (..., unassignedMixer = juce::Uuid())` -> `null()` (a random default made every call walk the mixers for nothing); `BounceSettings::automationEdit / mixerId`; `AudioEngine::offlineMixerId`; `EditRegion::windowId / takeId` and `EditGuide` ids (`active()` goes by the file, so null changes nothing there); the AAF import's `addEdit (name, juce::Uuid())` -> `null()`. A core-test section checks the defaults and that `Uuid (String())` is null while a garbled id is a junk id (not found, never "none").
- `WaveColorCache`: the pool job used `folder()` -> `project.projectFolder()` from the pool thread (a race with openProject; and a queued job could run after `Project` was destroyed - `waveColors` is declared before `project` in AppContext, so it is destroyed after). The folder is now taken in `get()` on the message thread and captured; `stop()` (alive = false, removeAllJobs 20 s) is the first line of `~AppContext`; `clear()` in `afterProjectReplaced` drops `done` (the `shared_ptr`s drawing code holds stay valid); `alive` is an atomic.
- MainComponent: the MATCHING box text (`takematchui::boxText` -> `TakeMatchService::progress`, a walk of every take of the windows switched on under the lock) every 15th tick of the 30 Hz timer.
- HotKeys: the take window's ruler click / drag.

### Area A: checked and found sound (no change)
- Closing order (Main::shutdown -> AppContext::shutdown -> ~AppContext -> members): the final save (`saveNow (false)` -> `Project::save`, which settles the background saver first) and the "quit without saving" path (`waitForSaves` + `retryFailedSave`) are synchronous and finish before `ClosingWatchdog::start`; the net can never fire mid-save. `devices.removeAudioCallback` + `closeAudioDevice` happen in shutdown(), so `~AudioEngine` before `~AudioDeviceManager` is safe. Member order: everything declared after `props` dies before `takeMatch` -> `plugins` -> `engine` -> `devices` -> `project`; the body stops the saver, the stall watch and the timers first.
- Every service's stop is bounded: TakeMatchService 3 s wait then (destructor only) stopThread 2 s; PluginHost stopThread 5 s; StallWatch 2 s; AafExport / VideoExport / Bounce jobs 20 s; WaveThumb pools and WaveColorCache 20 s with alive flags checked per chunk; DirectWaveCache reader `wait (-1)` idle and stop 20 s; the BackgroundSaver as long as the drive needs (logged every 10 s); RingBuilder 4 s; Surface sender flush 1.5 s + 2 s; RemoteHub 4 s. Every run() rests when idle (`wait (-1)` / `wait (1000)` / `wait (500)` / event waits); 3.18.0's take-matching holds re-check after `cancel` is cleared so a hold between pop and start still stops the work.
- `crashreport::describeThreads`: between SuspendThread and ResumeThread only `GetThreadContext`, `RtlLookupFunctionEntry` / `RtlVirtualUnwind` into a fixed array - no heap, no JUCE, no logging; names and symbols come after every thread runs again. Residual: a thread held inside ntdll's own unwind-table lock (the same exposure every sampling profiler has); accepted - the listing is only taken once things are already stuck, and the closing net ends the process anyway. dbghelp is not thread-safe: the stall watch's listing (5 s) and the watchdog's (15 s after the save) cannot overlap; a crash during a listing is the one unguarded case.
- `writeViaHelper` / `helperMain`: snapshot, thread, process and file handles all closed; `WaitForSingleObject` bounded by `waitMs` (60 s) then `TerminateProcess`; the helper never installs the crash handler, never shows the splash, `shutdown()` returns at once.
- The rate converter (`RateBridge::run`) in the callback: memmove / memcpy over buffers made in `prepare`, no allocation, no lock; `bridge` is only replaced in `startForDevice` (from `audioDeviceAboutToStart`, or `prepare` without a device) while the engine is not in the device manager's callback list.
- Async captures in the windows added since 3.1.1: VersionsWindow (timer, `hold`, the dialog deleted in Main::shutdown before `ctx->shutdown`), AafExport / VideoExport (`SafePointer` into the job's onDone, the FileChooser a member so a closed window drops its callback), CpuCoresPanel (`SafePointer` through `Thread::launch` + callAsync), ProjectRateDialog, HelpMenu / PluginManager / TakeMatchPanel / RecPrePanel (`[&app]` / button clicks only), the ignore-missing dialogs (`this` = AppContext, program-long, `hasProject()` re-checked), 3.18.0's `recModeHolds` callers (SafePointers; FixTools `ap = &app`). FixTools' detached picture threads (RepairDialog, NoiseModeDialog) are cancelled in the destructors and hold their data by shared_ptr.
- Containers: `TakeMatchService::known` (cleared per project, pruned by `windowsChanged`), `alignments` (400), `DirectWaveCache` (80 files / 256 MB), `RecordMonitor::events` (kMaxEvents), the undo stack (30), `Project::retiredMixers` (freed), `PluginHost::editors` (closed ones removed), `FileLogger` (512 KB). `WaveColorCache::done` was the one that only grew (now cleared per project).
- Idle cost: the 30 Hz timers repaint only on change; the 250 ms undo tick and 100 ms watcher do small work on a big project (measured in 3.1.6); the 3 s take-matching sweep is a map lookup per take; nothing opens files or analyses audio by itself (take matching only when switched on and not held).

### Area B: what changed
- `Project::createDefaultDesign` now clears `ignoredMissing`, `windowBounds` and `filesMoved` as `fromVar` does (New project / Close project kept the previous project's: the ignored paths and stale window places were then written into the new project's file; `filesMoved` made its first missing-file check a full one).
- Readers of the three cache formats check the count in the header against what the stream can still deliver before resizing: `takematch::load` (.fpr: frames x (bins + 1) x 4), `WaveThumb::loadFrom` (.fwave2: points x 4 x channels), `WaveColorCache::loadSaved` (.fwcol: n x 8). The old upper bounds (100 M frames, 2^32 points, 50 M blocks) let a garbled header ask for gigabytes -> `std::bad_alloc` on the take-matching thread or the message thread -> terminate. Core test: a 60-byte .fpr claiming 50 M frames and a thumbnail header claiming 2^31 points are refused. (Area D will fuzz them properly; this was moved up because it ends the program.)
- `AudioEngine::Finisher::run` waits 1000 ms when idle (was 200; `add()` notifies). `BackupCopier::run` waits 1000 ms when it has no jobs (was 40 ms, 25 wakes a second from start-up on, second drive or none; `add()` notifies; with jobs the 40 ms poll stays, since `finish` / `cancel` only set atomics).

### Area B: checked and found sound (no change)
- Recording: `startRecording` refuses while converting (3.8.0) and when the project's rate is not the device's (3.9.0), and AppContext asks for a rate first (3.9.0) and for Rec mode (3.18.0, re-entering `toggleRecord` once it is on). The free-space check (500 MB) is at the start; from then on a full or vanished drive shows as `TakeWriter::failed()` -> "RECORDING HAS STOPPED REACHING THE DISK" and the recording monitor, the file keeping a good header for what reached the disk (refreshed every 10 s). Overruns are kept as silence in place (`markAt / markLost`, `tailLost`), counted, and in the take's line in the log. `giveUp` cleans up the files and second-drive copies of the tracks before the one that could not be made. `stopRecording` with a stalled audio thread leaks the session on purpose and says so; otherwise the pre-roll remainder is handed over, the take's fields set, and the files finished on the Finisher thread (Stop never waits for the disk). The device stopping under a take: the take goes on (silence for the gap) or, at a new rate, ends with what it has (`deviceRestartedWhileRecording`, 3.1.5). The second drive: `BackupCopier` with the lag limit, "COPY LOST" / kept in the monitor (3.1.3 / 3.2.0). Pre's hand-over: `deliverFromRing` in blocks of 4096 at most; a length change during a take applies after Stop (3.18.0, tested).
- Saving: `replaceTextSafely` (temporary file, checked write, fsync, old file -> .bak, rename; the old file put back if the rename fails) for every project and window file; `writeSave` orders window files -> project file -> tidying, so a stop at any point leaves an openable folder; `readJsonWithBackup` falls back to the .bak and puts the damaged file aside. The background saver finishes every job handed to it before it is destroyed (`waitUntilIdle (-1)`), one job at a time, and `drain()` reports a failure before the next snapshot. `openProject` and `closeProject (save)` refuse to go on when the save fails; `closeProject (false)` and "quit without saving" still wait for the automatic save and rewrite one that failed. Autosave never runs while recording (Record and Stop save at once), never while a mouse button is down, at most every 30 s, and at least every 5 minutes while playing.
- New fields saved and loaded with defaults for older files: `sampleRate` (0 = none; older projects take their takes' rate), `ignoredMissingAudio`, `leadIn` / `leadInSeconds` (clamped 0..3600), `takeMatching` (3.18.0), `pieceBars`, `barIn / barOut`, `notes`, `roomTone` (take and project), `waiting` (3.1.4), `consonants` (through `putFilePath` + "channels", so a project copy takes their files along and remaps them). Undo / save hashes: checked by the 3.1.4 / 3.16.0 / 3.18.0 tests (every field saved; the recorder's fields and the view fields not undone).
- `projectcopy::run`: everything under the project folder except the project files (so Analysis, Consonants, Waveforms, Fixes and Bounced Media go too), audio from elsewhere into "From elsewhere", CRC-checked, the copy's project file written safely; a full destination refused up front.
- Missing files (3.15.0): `missingAudioCount / List` leave ignored paths out; `ignoreMissingAudio` adds every missing path after a full check; "Look for them again" clears the set and marks dirty; relinking unchanged.
- Other writers since 3.1.1 are checked or atomic: the take sheet (`writeDataChecked`, PDF save result, a message naming the file that failed), consonants (`consonants::save` returns the error), the preamp memory (`replaceTextSafely`), the fingerprint / thumbnail / colour caches (TemporaryFile), AAF (TemporaryFile + status check), video export (its own result), crash reports (the helper deletes a failed file). `cpu-fence.txt` is a plain note (unchecked, harmless).

## 3.18.3: the second code sweep - areas C (edit operations, undo), D (files and messages from outside) and E (snappiness)

### Area C: what changed
- `VersionsComponent`: `app.undoHold += 1` in the constructor, `-= 1` at the end of the destructor (after `takeOut`). `fixtools::PreviewApplied hold` only held the automatic save; every `putIn` / `takeOut` called `project.changed()` and the undo tick (not held) recorded each as a step. With the hold, the tick (and `anchorEdits`, in the same lambda) wait; `use()`'s `changed()` becomes one step from the snapshot taken before the window opened once the hold is let go; a Close that took the audition out leaves the hash as it was, so no step.

### Area C: checked and found sound (no change)
- `EditDef::anchorStart`: the first-region extension (`srcIn -= d`, `startSample -= d`, `inStart / inEnd += d / rate`, clamped to `srcIn - max fileStart`, not while waiting for corrected audio) and the shift of everything by `startTarget() - audibleStart()` through `rippleAfter (min / 4)`. `AppContext::anchorEdits` runs in the undo ticker before `undoTick`, skipped while a mouse button is down (drags use `markDirty`; `changed()` comes at mouse-up, so the anchor and the drag share the step), while playing, while a modal is up (the fix tools, menus, Find other versions) and while that edit's Trim window is open; `afterProjectReplaced` runs it with `always` before `undoReset`. `slideRegion` clamps to time zero and to the neighbours' starts and `shuffleRegion` swaps by index, so `regions` stays in timeline order and `regions.front()` is the first piece.
- The lead-in (3.16.0): `leadIn` / `leadInSeconds` in `undoHash` and the undo text; `commitLeadIn` -> `changed()` -> one step with the move.
- J: `goTo` -> `setPlayhead` (view state: `saveHash` only).
- Trim window: a step per adjustment (`changed()` after each), `changedLive` while dragging, Revert to the state at opening - by design since 2.x.
- Consonants (copy / remove / paste / nudge), Noise mode's write back and the ReTimEr: `undoHold` while the dialog is open (`c.hold`, RetimerPanel), then `armFixUndo` for one step; `fixUndoStillValid` refuses once the edit (or the take) has changed since.
- `replaceRangeRippled` + "Use this one": `restore (base)` before each `putIn`, so a second audition never stacks on the first; `armFixUndo (old = base)`.

### Area D: what changed
- `tests/fuzz_readers.cpp`: `richProjectSeed` (a project with every field since 3.1.1: consonants, room tone, lead-in, ignored missing files, take matching on, bars / notes, a waiting mark, a guide, lanes with a pinned point, markers, comments, the rate, window places) joins the json target's seeds; a `caches` target mutates saved .fpr fingerprints (`takematch::save` of analysed tones / silence) and .fwave2 thumbnails (`WaveThumb::saveTo`) and checks what `load` / `loadFrom` accept adds up (frames x bins = chroma size, every point readable); the aaf target starts from an AAF made by `aafexport::exportAaf` when FUZZ_AAF is not given (192 KB: two pieces, a crossfade, an LTC track).
- Findings fixed: `Project.cpp` `samplesFromVar` clamps every sample count / position read from a file to +-2^50 (regions' srcIn / srcOut / startSample / sourceLength / fileStart / gain `at`, the guide's, automation `time` / `srcPos`, waiting marks' from / to, takes' length / lost, consonants' length / lead) - a value near INT64_MIN overflowed `EditRegion::length()` (UB); `WaveThumb::loadFrom` refuses a header whose `samples` is negative, above points x 256, or does not give `points` (a negative length was accepted and drawn). Then four runs (60 / 120 / 90 / 60 s per target, twelve targets): nothing found.
- `consonants::load`: a track file over a minute long is refused (it is read whole into memory; a consonant is milliseconds).
- `tools/tool-checks.sh query` (juce::var indexed with a number): the three hits in `Project.cpp:152-154` are guarded by `isArray()` and `i < v.size()` (as in 3.0.0).

### Area D: checked and found sound (no change)
- Writers since 3.1.1 (area B listed them) are checked or atomic. `crashreport::reportsSince / prune` go by file dates, never by parsing names; `CoreReservation::idsFromText` (cpu-fence.txt) takes digits only; `helperMain` reads its two arguments with `getLargeIntValue` / `unquoted`. Fermata's own JSON files (comments.json, the plug-in ignore list, PreampMemory, the mixer / window files) are read through `juce::JSON::parse` with `readJsonWithBackup` where a .bak exists; bytes from other programs still go through `textFromBytes` (Feedback, iXML, the import's file names, the control port, the Merging devices).

### Area E: what changed
- `VersionsComponent::search`: results per take are kept (`searched`: take id -> the fingerprint they were made from + rows) and reused while the fingerprint is the same, so the 2 s re-search while takes are still being analysed only runs `findVersions` for the newly ready takes (it ran for every ready take every time: tens of ms each, times 26, every two seconds, on the message thread).
- `TakeMatchService`: at most `kResident` (48) fingerprints in memory (`trimResident`, least recently used first, only those saved in the Analysis folder - `Entry::hasCache` is now set from the save's result); `fingerprintFor` reads one let go of back with `takematch::load` and stamps `used`. The alignment cache keys its two fingerprints by `sourceKey` instead of holding their `shared_ptr`s (which pinned them). Core test: 60 takes analysed -> at most 48 resident, every one comes back whole, nothing analysed again, the alignment cache still hits. (A 20 fps x 61 floats fingerprint is ~4.9 KB per second of take: 367 takes of 5 minutes were ~540 MB.)

## 3.18.4: the words along the bottom of a take, on a narrow take
- TakeWindow.cpp: `takeStripWords` makes the words (DUD, CUT SHORT or LOST, ROOM TONE, the bars) each with its forms, longest first; `layoutTakeStrip` lays them out once per take per paint (every track of a take is as wide, so the rows share it; laid out only when a row's strip is inside the area being painted). Everything fits: the old look (5 px either side, 10 px between words). Otherwise: 2 px in, 1 px at the end, 6 px between words, each word the first of its forms that fits in what is left (`fitLabel`, Takes.h), the line stopping at the first word that cannot show anything; the strip is drawn under a clip, so nothing reaches past the take. The old rule (no line at all on a take 50 px wide or less, "..." on one a little wider) is gone; rows under 22 px still have no line.
- Takes.h: `barsLabelForms` ("Bar 17 - 39", "17 - 39", "17-39", "17-", "17"; one bar "Bar 17", "17") - never a cut number, so a narrow take never shows a wrong bar. `fitLabel` takes the measuring as a function, so core_test checks it with a pretend font (7 px a letter); the other words (DUD, LOST, CUT SHORT, ROOM TONE) may lose letters off their end (halving search, a few measurements), LOST and ROOM TONE first drop their numbers.

## 3.18.5: the second code sweep - area F (clean-up)
Found with a scan of every function and member against every use in src and tests (names defined and never named again), a scan for names that appear once, the settings keys read against those written, the project keys written against those read, headers nothing includes, and static helpers defined in more than one file.
- Removed: `EditDef::moveJoin` (the Trim window's "Move the join" buttons went in 3.16.0; only its test was left), `fermataCrashFolder` (HelpMenu.h), `interp1` (reharmoniser/Dsp), `RateBridge::isReady` / `numTaps` and `ProjectRateBridge::isReady`, `TimeReadout::stopFollowing`, `MasterExport`'s `editRates`; `tests/ravenna_mock_test.cpp` (pointed at an old path, run by no script, its checks are in core_test).
- Unused values dropped instead of `juce::ignoreUnused`: `menuNeed`'s `lineH`, the menu layout's `rx`, the Trim window's `s1` / `e0`, the edit window's `mouseDown` event, the import dialog's `self` capture, `runRegionJob`'s and `runRepairJobs`' spec (the work is in `RegionJob::ops`), `markedTake`'s `app`, the Export for Processing box's `haveOut` / `haveIn` capture, ReHarmoniSer's `place`.
- Looked at and left: the minutes:seconds text written five ways (FixTools consonants, Find other versions, feedback flags, Align, the recording monitor) - one helper would be tidier, later; `bessel0` (RateBridge / Resampler) and `levinson` (LostSamples / SpectralRepair) exist twice with small numerical differences, so merging them could change results; the marker and comment flags' drawing (MarkerStrip.h / FeedbackUi.h) is alike but over two types. The settings and project keys all match; no `#if 0`, TODO or header nothing includes. FixTools.cpp (6,600 lines) not split: only on a day the tools can all be opened after the build.
- **Volume changes opened again (asked for with this build).** `GainChange::group` / `role` (saved as "group" / "role" only when there is a group; in saveHash): the points one `RangeVolumePanel` makes share a random group id - role 1 the start of the area (ramp = fade in), 2 the area carrying on into a piece from its start, 3 the glide back (ramp = fade out, arriving at the end of the area). `EditRegion::setGainChange` now returns the point (a replaced point keeps its group). `RangeVolumePanel` has two constructors (new area / existing group); it applies live (a 10 Hz timer takes what the faders and the fade boxes say), holds the undo history (`undoHold`) while open so Done is one step, keeps the edit's regions from when it opened and puts them back unless Done or Remove was pressed (Cancel, Escape, the close button), and plays -2 s .. +2 s round the area. Editing: per piece, the start point's dB = the level before it (`levelAfter` over the earlier points) + the fader; both ramps at most half the area in that piece; the end point is moved so it still arrives at the end of the area. The tracks shown when opened again are those the change actually moves (a track left at 0 dB is not shown). `EditTimeline::gainMarkAt` (line +-4 px or inside the shaded glide; normalise points and a piece's starting level left out) serves the double-click and the right-click item; a point with no group opens `VolumeChangePanel` at its place as before. Test: the group and role through save and load, and a replaced point keeping its group.

## 3.18.6: FixTools.cpp split; one minutes:seconds helper
- **The split** (left from both sweeps; done when the person could build and open every tool the same day). Moved, not changed: `FixToolsCommon.h` = the helpers, the OUT / IN stream picker, `JobDialogBase`, `RepairOp` / `RepairContext`, "applying a fix to the model", `takeWin` / `markedTake` / `editOf`; `FixToolsDialogs.h` = the pitch, pitch-curve and repair windows (with the spectrogram and its settings); `FixToolsEdits.cpp` = the take-window and edit-window pitch / repair entries, Noise mode and the pitch curve; `FixTools.cpp` = `say` / `launchDialog` / `isAudioFile` (declared in FixTools.h, so they stay out of the header), the import dialogs, ReHarmoniSer, copy project / Save as, Export for Processing, the room tone, AAF import. What was file-`static` in the moved parts is `inline` in the headers, so there is one of each for both files (`gMakeProcessPage` / `gProcessPage` above all: the curve page and the pitch process page are made across the two). Checked: every line of the old file is in exactly one of the new ones; compiled to objects, no `fixtools` symbol is defined in both or needed and missing, and every symbol the old object defined is still defined. Only those two .cpp files include the two headers.
- **`timefmt::minSecText (seconds, decimals)`** (TimeFormat.h) replaces the repair window's consonant `clockText` (2 decimals), Find other versions' `timeText` (1), `feedback::timeText`, the Align window's `mins` and the recording monitor's own text. core_test compares it with each old formula over chosen edge values (9.95, 59.96, 3600 ...) and 20,000 values in between.

## 3.19.0: Save this window
The person wanted to save the work in one window and NOT the work in another. Every save (automatic ones included) still writes every window: that is the crash safety they chose to keep. Over it, `Project::kept` (not saved with the project) holds what the person last asked to keep of each window: for a take window or an edit an `UndoSnap` (the undo hash - no view state - and a copy), for a mixer `mixSnapshot (m, true)` and its `fingerprintOf`. `keepAllWindows` on a project just opened (`afterProjectReplaced`) and after every save the person asked for (`saveNow (quick = false)` with `keepAll`, the default); `keepWindow (id)` from Project > Save this window (`AppContext::saveWindow`: keep one, then `saveNow (false, &err, false)` so the others are not marked). `windowsChangedSinceKept` compares the hashes; a window with no entry (made since) is never listed. `revertWindowToKept`: a take window or edit is put back through `restorePiece` with the kept snapshot - the same code as undo, so takes recorded since stay, the view (marks, playhead, markers) stays, and the revert is an undo step of its own (pending changes ticked into a step first, the revert ticked into the next); a mixer through `applyMix` (not in the undo history). The window as it then is (kept + recordings since) becomes its kept state. The quit box (Main.cpp `systemRequestedQuit`) lists the changed windows as ticks, all ticked; Save and quit reverts the unticked ones (`AppContext::revertWindow`, which uses the engine's rate and block for a mixer and tells the strips) and then saves as before. The Project menu item names the open take window, edit window or mixer that was in front last (`listWindows` recency). Test: kept / changed / keep one / revert with a take recorded since / undo of the revert / a new window never listed.

## 3.20.0: the Trim window's scroll bar; Match edit in an edit's mixer
- **Trim window.** `TrimComponent::viewLimits()`: from where the earlier of A's and B's takes begins on the edit timeline (`startSample - srcIn`) to where the later ends (`+ sourceLength`, or the piece's end when that is unknown) - the centre is kept inside it (`clampCentre`) by the bar, the pan band, a ruler click and the wheel (it used to be kept inside [0, edit length]). `hScroll` (always shown, under the waveforms): limits = viewLimits widened by half a view at each end, thumb = the view; `syncScrollBar` (no notification) on zoom, pan, recentre, resize, a drag and any project change; `scrollBarMoved` sets the centre. Wheel: Shift + wheel or a mostly-sideways event moves the centre by 0.6 x the delta x the view; the plain wheel zooms as before, and an event with no vertical part no longer zooms out. Drags of a fade, the crossfade or a take are measured on the timeline (`downT` at mouse down, `timeAtX` now) instead of in pixels, so the view can move during a drag: within 28 px of either edge (or past it) the centre moves by up to 4 % of the view per drag event, and `beginDragAutoRepeat (30)` keeps the events coming while the mouse is held still there. With the view unmoved the result is the same as before. The reference line still stays inside the edit.
- **Mixer.** `Project::matchMixerToEdit (m, e)`: strips of project tracks not among `editTracks (e)` are hidden (their solo let go of, as `ChannelStrip::hideStrips` does), those among them that were hidden are shown; returns { hidden, shown }. The METERS strip (`MeterSettingsStrip`) shows Match edit and Show all strips only when its mixer has an `editId`; both rebuild the plan and raise `structureChanged` (which rebuilds the window - nothing of the strip is used after it). Test: B hidden and its solo off, C back, D stays hidden; a second press does nothing; an edit with no rows of its own shows all.

## 3.21.0: N and V in the take window
- `TakeTimeline::currentOrLastTake`: the take being recorded in this window (`engine.currentTakeId()` found among its groups), else the one with the highest number. **N** (`openOutBar`): `showBars (id, where, outOnly = barIn > 0)`; `BarsPopup`'s outOnly mode hides the IN box, the notes and More..., sizes 260 x 70 and focuses OUT (selected). The IN box keeps the take's IN Bar unseen, so OK, Enter and a click elsewhere (keepAndCloseBarsPopup) go through the same paths as the full box: empty OUT = the IN bar, OUT before IN refused. **V** (`copyPreviousBars`): only while this window records; the take before = the highest number below the live take's; its IN / OUT (OUT = IN when missing) through `applyBars`, which also updates edit pieces of that take (none yet for a live take). No notice on success, at the person's request; a status-line notice when there is nothing to copy. Both keys are plain (no modifier) and free everywhere the take window is: N is otherwise only the ReTouch window's and Noise mode's Denoise; V was unused. Checked by compiling (UI-only: the bars of a take being recorded are already set this way by B).

## 3.21.1: "changed since kept" for mixers
3.19.0 compared `fingerprintOf (mixSnapshot (m, true))`, which holds every plug-in's `getStateBase64()` - not stable for many plug-ins - and the faders / plug-in parameters an edit's automation writes into the mixer during playback; the person saw every edit mixer listed at quit. `Project::mixKeepHash (m, only, partsOut)`: a fingerprint of the mixer as the person sets it - strips, buses, VCAs: fader, input gain, pan (and stereo pans), mute, solo, output, VCA mask, sends (active ones, as saving has them), low cut; each insert's description, bypass and automatable parameters (`getParamNorm`, to 1 / 10000) instead of its state; a slot whose plug-in could not be loaded keeps its saved description and state. Every (owner, param) an automation lane of any edit writes into this mixer (the lane's mixer found as `automationPlanFor` finds it) is written as "automated". Only the parts ('t:' / 'b:' / 'v:' + id) present when it was kept are looked at, so a track or bus added later (a strip in every mixer) is not a change. `keptMixer` takes the snapshot with `quickPluginState` (the states the save just fetched, not asked for again) and the hash with its parts; a revert sets the hash to the mixer as it is after `applyMix`. Test (core_test 3.21.1): a plug-in whose state differs at every call and a meter parameter that moves, a fader moved by a lane, a new track - none listed; a fader, a plug-in setting, a bypass - listed; back to the kept values - not listed; revert.

## 3.22.0: Edit in RX
`src/ui/RxLink.h/.cpp` (namespace `rxlink`), `src/core/RxWatch.h`. The edit window's `rxButton` (`Icon::EditInRx`, next to Export for Processing) calls `rxlink::editInRx`: held by Rec mode like the other fixes, marks checked, then the RX program (user setting `rxProgram`; `askWhereRxIs` if it is not there), then `fixtools::planSendOut` - Export for Processing's planning, taken out of `exportForProcessingEdit` (FixTools.cpp) so both share it: the piece mark 1 is in, or with one edit point between the marks the OUT / IN / Both question; `SendPart` per set (piece id, take range [a,b), files, names, fileStarts, tracks, channels). Export for Processing itself is unchanged (same messages; its `markWaiting` now builds a `WaitingPiece` and calls `EditDef::markWaiting`, moved from FixTools.cpp into Edit.h).
- Sending (`send`): job "RX n" = the first free folder name in Processing Media; one WAV per track named "<track> - RX n" (streams: "RX n OUT" / "RX n IN"; the track first because RX's tabs show the start of the name). On the job thread each set's handle range is [a - 2 s, b + 2 s] cut to what every file of the set holds (`fstart` .. `fstart + length`), so all tabs of a set are the same length; `audioops::exportRange` gained `keepFloat` (a float source stays 32-bit float, else 24-bit as before) and `WavMetadata` (`cueMarkers`: a "cue " chunk with "labl" names at the part's start and end - JUCE's WAV metadata keys). On success each set becomes a waiting piece (`WaitingPiece::rx`, `fileFrom` / `fileLen` = the files' take range; `filesFrom()` / `filesLength()` fall back to from / to for Export for Processing pieces), `Project::rxSeen[path]` = each file's modification time as written, `setUndoForEdit`, then RX is started with all the files (`startProgram`, WinOwner.cpp: CreateProcessW with bInheritHandles FALSE, so RX never inherits Fermata's sockets or file handles; JUCE's ChildProcess inherits them; ShellExecute would cap the command line at ~2048 characters; a list over 30000 characters goes in several calls; on a Mac `open -a`). A one-off explanation box (setting `rxExplained`).
- Coming back: `rxlink::makeWatcher` - a 1 s timer owned by the application (Main.cpp `rxWatcher`, freed before the AppContext). It skips while recording, while `undoHold` / `saveHold` are up, or while any modal component is up (the fix windows and dialogs that put an edit back as they found it). It looks at every rx waiting piece's files and every `RegionFile::rxFrom`, and `RxSaveWatch::ready` (pure, tested) returns those whose time differs from `rxSeen` and has been the same (time and size) for two looks; a file met for the first time is taken as it is. `bringIn`: per file, the places it goes (rx waiting pieces naming it; tracks whose `rxFrom` is it); the expected length / channels / rate from the waiting piece (`filesLength`) or from the copy now playing; the file is checked, copied to "<name> (saved n).wav" (each save its own file: Ctrl+Z can go back to the previous one, and RX keeps owning its file), the copy checked, then `EditDef::takeInWaitingFile` (that track of the piece plays the copy from `fileFrom`, `rxFrom` set, the waiting list loses it - tracks named from then on - the piece stops waiting when none are left, takeName "(RX n)", `keepFixFadesInside` over the files' range) and `EditDef::useRxSave` (every track with that `rxFrom`, pieces and overdubs, plays the new copy). One `undoTick (false, true)` before the first change and one after `changed()`: one undo step per look. `RxSaveWatch::done` writes `rxSeen` whether it was brought in or turned down (a message), so Ctrl+Z after a file came back does not bring it straight back in; a file that cannot be read or copied yet is tried again (`later`, 10 tries). `rxSeen` is saved in the project file (only files that still exist, with relative paths inside the project), not part of undo, cleared with the project.
- The piece menu of an rx piece: "Bring in what RX has saved now" (`bringInNow`: every file of the piece, saved or not; `relinkEdit` routes rx pieces there too), "Open it in RX again" (`openAgain`), "Cancel: keep the original audio" (the old Stop waiting). `drawWaitingBlock (..., rx)` says "In RX (RX n) - save it there to bring it back".
- RX's location: `askAtFirstStart` (Main.cpp, 6 s after the start, once: setting `rxAsked`) and Project > iZotope RX location (item 23) call `askWhereRxIs`: "Use this one" for `findRx()` (Program Files / ProgramW6432 / x86 "iZotope" folders, folders "*RX*", any "*RX*Audio Editor*.exe" - the highest version number in the name - else any RX exe that is not a helper), "Choose..." (a FileChooser for *.exe), Skip / Cancel.
- Saved: `WaitingPiece` writes "rx", "fileFrom", "fileLen" only for rx pieces (and only then are they in the undo hash, so other hashes stay as they were); `RegionFile::rxFrom` as {file, rel} under "rxFrom". Tests (core_test 3.22.0): the export (float kept, 24-bit kept, the two markers read back), markWaiting with handles, one track then the other coming back, the crossfades staying, a later save reaching both halves of a cut piece, files that do not cover the piece, save / load of all of it with `rxSeen` (a missing file dropped), and `RxSaveWatch` (first sight, being written, ready, done, missing for a moment, retries, forgotten files); the undo hash test has the three new fields.

## 3.23.0: a stereo track from two mono tracks; send dials that do not double the fader; the ruler far out
- **Ruler.** `timefmt::rulerSteps (pixelsPerSecond)` (TimeFormat.h) gives the take and edit windows' ruler its labelled step and its fainter step. At 3 px/s and more, the old ladder (0.5 s .. 60 s; minor 0.1 .. 10 s). Below it, the first of 5, 10, 15, 30 min, 1, 2, 3, 6, 12 h, 1 day that keeps labels at least 90 px apart (it used to stay at 5 minutes, and at 0.01 px/s - the furthest out - the labels overlapped), and a minor step of a fifth / quarter / third / half of it that is at least 10 px (else none). Test (core_test 3.23.0): a sweep from 0.005 to 3 px/s.
- **Send dials.** `Project::faderFeeds (m, node, bus)`: the output of a track / Int Bus strip is on and goes to 'bus' - its `outDest`, or, with `outDest` null ('Main'), every Ext Bus ticked `isMain` in that mixer. `Project::sendDialBuses (m, node)`: the buses its strip shows a dial for - the Ext Buses ticked Main out that the fader does not feed first, then the Int Buses, then the other Ext Buses, leaving out the node itself and what the fader feeds unless that send is already active (an old project: it stays visible, its tooltip says it doubles the fader). `ChannelStrip::buildSends` and the Int Bus `BusStrip` build their dials from it. The strips are rebuilt when it can change: `MixerComponent::routeSignature()` (each strip's outOn / outDest, each Int Bus's, each Ext Bus's Main out tick), checked on the 3 Hz pass beside `slotsNeeded`; a send's level is not in it, so automation never rebuilds the mixer. Test (core_test 3.23.0): Main by default, an Int Bus, another Ext Bus, output off, an active send to Main, two Mains, an Int Bus strip, an Ext Bus.
- **Stereo track.** `src/core/StereoJoin.h` (header only, namespace `stereojoin`) and `audioops::joinToStereo` (left / right mono files with their take-sample offsets over a take range; silence where a file has none; 32-bit float if either is float, else 24-bit, dithered only from more than 24 bits; length checked). `plan()` (message thread): both tracks mono, not LTC, a name; nothing on them waiting (take windows' and edits' `WaitingPiece`s, `waitingTouches`); per take with a file on either: a `FileJob` (whole take: 0 .. the longer file) named `TakeWindowDef::fileNameFor (number, labelFor, name)` in the take window's folder; per edit piece / overdub with a file on either: the take's job when both are the take's whole files (fileStart 0, the same pair), else a piece job over [srcIn + fade-in start - 2 s, srcOut + fade-out end + 2 s] in Recorded Media/Fixes, cut by `writeFiles` (job thread) to the union of where each file has audio. `apply()` (message thread) checks that every take and piece still has the planned files (else false, the caller deletes the new files), inserts the stereo `TrackDef` at the left track's index (its colour, alignRole, inputs[0] of each), replaces the two `TakeFile`s / `RegionFile`s by one in the left one's place (sourceName / sourceName2 kept; a `GainChange`'s two levels become their average), renames the lists (take windows' editTracks and hiddenTracks - hidden only if both were - edits' trackIds, fixTracks, the visual guide), keeps the left track's fader / gain / send automation lanes on the stereo track and drops its pan / plug-in lanes and the right track's, gives every mixer's new strip the left strip's fader, input gain, output, sends and VCA mask (hidden only if both were), removes the two tracks, `structureChanged`, `undoReset` (the undo steps would put clips back on tracks that are gone) and `keepWindow` for each window it changed (so the quit list cannot revert them to the mono tracks either). UI: the take window's track-name menu with exactly two selected tracks (`askStereoFromSelected`: an AlertWindow with the left channel and the name; `makeStereo`: `fixtools::runSimpleJob` writes the files, then `apply`). Test (core_test 3.23.0): the writer (exact 24-bit, float kept, padding, a rate mismatch refused), the name suggestion, refusals (waiting part, not mono, same track, no name), two takes (one with only the left), an edit with a whole-take piece and a repaired piece, gains, lists, lanes, the mixer strip, no undo, save / load, and a take changed between plan and apply.

## 3.24.0: Export for Processing's menu and linked programs
3.22.0's Edit in RX, for any program. The code kept its names (`rxlink`, RxLink.*, `WaitingPiece::rx`, `RegionFile::rxFrom`, `Project::rxSeen`, `RxSaveWatch`): they now mean "sent to a linked program". `rxlink::Program { name, file }`; `programs()` / `setPrograms()` keep the list in the user settings as XML ("externalPrograms": `<PROGRAMS><PROGRAM name path/>`); with no list yet, 3.22.0's single "rxProgram" setting becomes "iZotope RX". `WaitingPiece::program` (saved as "program", in the undo hash with the other rx fields, only for rx pieces): the list name it went to - for "Open it in X again" (found by name in the list) and the menu's words. The edit window's `exportProcButton` calls `rxlink::showExportMenu`: 1 = `fixtools::exportForProcessingEdit` (unchanged: no rx, re-linked by hand), 100 + i = `sendToProgram (list[i])`, 2 = `addProgram` (a FileChooser, then an AlertWindow for the name, `suggestedName`: "iZotope RX", "Melodyne", the file's name), 200 + i = remove. `sendToProgram` = 3.22.0's `editInRx` with the program: held by Rec mode, marks, the program's file must exist, `planSendOut`, then `send` - job "<jobLabel> n" (`jobLabel` drops a vendor word: "RX", "Melodyne"), files "<track> - <job>", `w.program`, the program started with the files on its command line (`openInProgram`; `open -a` on a Mac), and a first-time box per program (setting "extExplained_<name>") that says how files come back (`howToSave`: RX Save, Melodyne File > Replace Audio, others save over the same file) with "Show the files". `drawWaitingBlock` says "In <job> - save it there to bring it back" (Melodyne: "File > Replace Audio there brings it back"). The rx piece's menu: 5 bring in now (`relinkEdit` -> `bringInNow`), 11 open again, 12 Show the files (`showFiles`: the first file's place in Explorer), 6 Cancel. `askAtFirstStart` (setting "extProgramsAsked"): `findRx()` and `findMelodyne()` (Program Files / ProgramW6432 / x86 "Celemony": any "*Melodyne*.exe" that is not a helper, the version from its folder and name) not yet linked -> "Link them" / "Choose a program..." / "Skip"; nothing found and something linked -> no question. Project menu item 23 = `showProgramsMenu`. The Edit in RX button and `Icon::EditInRx` are gone; Uikit.h is back as it was in 3.21.1. The take window's Export for Processing is unchanged (Create files only: a program there would need its take files spliced at every save). Tests: the undo hash test has `program`; the 3.22.0 save / load test checks it.

## 3.25.0: marks 1 / 2 dragged with the mouse; Ctrl + Alt + click picks the marked tracks
- **Helpers** (Takes.h, before `namespace coverage`): `moveMark (in, out, first, t, gap = 0.001)` puts mark 1 (`first`) or mark 2 at t seconds, never below 0 and never past the other mark (kept `gap` apart; a mark not set is -1 and does not hold the other back). `toggleMarkedTrack (list, track)`: the track in, or out again - on an empty list (= every track) it gives just that track, and taking the last one out leaves the list empty (= every track again). Both work on an edit's `fixTracks` and a take window's `editTracks`.
- **Edit window** (`EditTimeline`): `markLineAt (ed, p)` = 1 / 2 when p is within 5 px of the line of mark 1 / 2 (the nearer), at or under `kRulerH` and right of the name column (+ 4 px for its widening edge); 0 on the ruler (a click there still moves the playhead / sets where the next take goes / plays from there), in the visual guide, on a row that is not an audio track's own row (`isTrackRow`), on any row while `automationOn` (a click there is a point), and within 8 px of a numbered edit flag in the flag zone (select the join / double-click to trim). `mouseDownImpl`: with no modifier and no popup, `markLineAt` != 0, not a fade corner, and not a double-click on a volume change (`gainMarkAt`) -> `markLineDrag` = 1 / 2 and return (before the playhead moves). `mouseDragImpl`: t from the mouse, snapped to a piece's `startSample` / `endSample()` within 4 px, then `moveMark (fixIn, fixOut, ...)`, `markDirty`; `fixTracks` is left alone. `mouseUpImpl`: `changed()`. `mouseMoveImpl`: the left-right cursor over a mark line (no modifier). Ctrl + Alt + click: the existing Ctrl + Alt mark drag (`markDrag`, `markAlt`, `markAnchor`, `markRow0`) ending with no drag (`! markActive`), marks set, the click between them, `isTrackRow (markRow0)` -> `toggleMarkedTrack (fixTracks, ...)` and a notice. `markWholeSelected` (P) now clears `fixTracks`.
- **Take window** (`TakeTimeline`): `editFlagAt (w, p, lines)` - the Mark 1 / Mark 2 flags in `kRulerH .. kRulerH + 14` as before, and with `lines` (no modifier held) within 5 px of a mark's line over the take's tracks (`kRulerH + kLaneH` down to the bottom of its last shown track with a file; not the banner, where the fade corners are). The flag drag that follows is the old one (snaps to the take's ends, clamps, keeps `editTracks`). Ctrl + Alt + click in `mouseUpImpl`: the mark drag (`markDragTake`, `markAlt`) ended with no drag on the marked take, the click between the marks, the row's track has a file in that take -> `toggleMarkedTrack (editTracks, ...)` and a notice.
- Hot keys: Ctrl+Alt+click and drag for both windows; P's text says every track. The Ctrl + Alt modifier hint has the click line. Test (core_test 3.25.0): `moveMark` (shorter, clamped at the other mark and at 0, mark 2 after mark 1, mark 1 not set), `toggleMarkedTrack` (first click, add, remove, last out), `fixUses` with a one-track list.

## 3.26.0: automation heard while it plays; Virtual Master stems; Export complete; the mixer's fader line and tighter strips
- **Live automation.** `PlaybackSession::autoNow` (atomic `const AutomationPlan*`) is what the audio thread applies each block (AudioEngine.cpp: `ps->autoNow.load (acquire)`); `startPlayback` sets it from `automation`, which only the message thread touches afterwards. `PlaybackSession::followsAutomation`: set by `beginSession (..., followAutomation)` from `AppContext::playEdit` only (not the guide, takes, the disc preview). `AudioEngine::replaceAutomation (editId, next)` (message thread, `followsAutomationOf` must hold): `next->begin()` holds its sends - a send not held before means `rebuildPlan()` first; then `automation = next`, `autoNow = next.get()`, the old plan into `replacedPlans` with the block count. `serviceReplacedAutomation()` (also from AppContext's 20 Hz `liveAutoTicker`): a replaced plan the audio thread has left (`blockCounter >= block0 + 2`, or not running) goes through `handBackAutomation (old, now)` - its sends that `now` does not move with points lose `autoHeld` (rebuild the plan), its VCA members that `now` does not cover lose the VCA bit in `vcaAutoMask` - and is freed. `stopPlayback`: `end()` on the current plan and on every replaced one still waiting; no hand-off to the tidy-up thread while any is waiting; if the audio thread stalled, they are leaked with the session. `AppContext::followAutomation()` (20 Hz): while `playInfo` is an edit that `followsAutomationOf`, `lanesSignature (e->lanes)` (Automation.h, FNV-1a over each lane's id / track / mixer / param and each point's region, srcPos, value - and time only for a point fixed in time, so pieces moving under the points do not count: the sound that plays is the one the play started with) is compared with `liveAutoSig` (set by `playEdit`); a change -> `replaceAutomation (automationPlanFor (project, *e))`. Test (core_test 3.26.0): the line moved while playing (next block), another edit refused, a send lane drawn (held, plan rebuilt) and removed (let go of), all lanes deleted (nothing applied) and drawn again, a non-following session refused, the signature.
- **Stems.** `MasteringDef::stemIds` (saved as "stems", only when not empty). `makeFilesSpec`: `spec.stemIds` / `stemNames` = the project's buses (in its order) that are in the list, without the render source; `makeFormsSpec` clears them; the DDP never has them. `MasterRenderBatch (..., stems, stemFolder)`: groups by rate and kind of pass - `master` (its render is stale: sources = { master } + stems) or stems only (an up-to-date master render and its backup slot are not touched: committing again would have overwritten the backup); the Bouncer writes item by item, each item's outputs in order; the stem files are moved out of the renders folder (where `removeTemps` deletes every "mrtmp-*") to `stemFolder` = the job's `_fermata_work/stems` as "stem-<piece>-<stem>.wav" (`stemFileFor`). `MasterExportJob::executeInner`: stage 2 converts each stem to the target rate (`resampleFile`, a helper like the masters' converter; only the masters' peaks set the level); stage 3 writes each piece's stems after its master with the same `gain`, in `o.format` (not the extra formats), into destFolder/Stems as "<baseName> - <bus>" (title tag "<title> (<bus>)"), into `finals` (so Cancel and errors delete them) and `R.files`; `R.stemFiles` counts them, `R.clippedStems` names those whose peak at that gain is over 0 dBFS (not for 32-bit float WAV). The work folder (and with it the stem renders) is deleted at the end as before. A piece set to its previous master render still gets stems of the edit as it is now. UI (MasteringWindow): the Stems row under "Also export every file as" (`stemsButton`: "None - ..." or "N buses: names"; `refreshStemsButton` from `refreshSource` and when the render source changes), `chooseStems` -> a CallOutBox `StemsPanel` (Ext buses, then Int buses, without the render source; All / None; every tick changes `def().stemIds` at once, an undo step). Test (core_test 3.26.0): two tracks, the master on an Ext Bus, a stem on an Ext Bus going nowhere and one on an Int Bus; the list leaves out the master's bus and a gone id; no stems with the listening copies; files, names, folder, lengths, the master's gain on both (balance kept); a second export reuses the master render (no B slot); a hot stem named; save / load.
- **Export complete.** `MasteringComponent::showDoneBox (result, kind)` after `jobFinished` (also for an error; not after Cancel): an AlertWindow over the window - files (and stems) written, the folder (the parent of the first file), the loudest peak, clipped stems (up to eight); "Show the files" reveals `lastOutput`.
- **Mixer layout.** `StripLayout` (MixerWindow.cpp): `inputRows` / `panRows` (2 only when a shown audio strip is stereo), `sendRows` (the busiest strip's rows, 0 without dials), `slots`, `faderH`, `topShown`, `topScroll`; `topContentH()` = the section above the line (inputs, 3, LC, 2, INSERTS, slots, 2, 4, the SENDS line `kSendHdrH` with the + at its right, the send rows, 2); `fixedH()` = band margin, the line `kDividerH`, pans, M / S, the gaps, the out row and the bottom margin. `fitStrip (L, h, pref)`: the fader `pref` (0 = the normal kFaderH), kept within kFaderMin .. room - kMinTopShown, the top section covered (scrolling) when the room is short, and room the top section does not need going to the fader. `StripBase`: `topPane` (clips) holding `topContent` (all the controls above the line - input boxes, LC key, slots, send dials, +, an Ext Bus's output box and note, a VCA's controls - are its children; it paints `topWells` / `topSections`), `layoutFrame (alignLat)` lays out the frame and returns the parts (content coordinates: x as in the strip, y from 0), `setTopScroll` only moves `topContent`, the groove painted in `dividerRect`; both containers pass empty-space clicks to the strip (its tooltip, the wheel; `SlotButtons::cellAt` still finds the cells). `MixerStripsHolder::layout (h, L)` lays every strip out and puts `FaderLine` (drag: `onDrag (dy)` -> `mixerFaderPref() = dragFader - dy`; double-click -> 0, the normal size; saved as "mixerFaderHeight"; `faderPrefGen` makes the other open mixers follow) over the fader strips (not METERS). `MixerComponent::layoutView()`: the view, `applyHeight()` (strips never shorter than `kMetersMinH`, the METERS strip's own layout; empty extra slots held while the line is dragged), and when the top section does not fit, the view without `kTopBarW` and `applyHeight()` again; `topBar` (vertical ScrollBar at the right, beside the shown section, following the view's vertical position) -> `scrollBarMoved` -> `holder->setTopScroll`; `MixerViewport::onWheel` -> `wheelOverTop`. `naturalContentH()` is everything in sight with a kFaderH fader (the line's setting never makes windows grow); `minContentH()` = the least top section and the smallest fader. Ext Bus outputs read "Out 1-2"; the "DRIVER OUTPUTS" title only with two input rows. `ChannelStrip::heightFor`, `setSendRows`, `setSlotCount` and `kToTrackH` are gone.

## 3.27.0: automated controls go back when their lane loses its points; compressed waveforms
- **Back where it was.** `EditDef::autoBefore` (laneKey "trackId|param|mixerId" -> the control's value: dB, pan, 0..1): saved with the edit ("autoBefore"), in `saveHash`, left out of `undoText` (kEditViewKeys) and carried over by an undo / redo like `laneRows`, so an undo never forgets where a control was. `holdAutomatedControls (Project&, EditDef&, canRestore)` (Playback.cpp): a lane with points whose key is not remembered -> the value its control has now (found by `planForOneLane`: `automationPlanFor` on an EditDef with the same id and that one lane with one point - the same mixer, track, bus, VCA or plug-in; read through the item's `target` or, for a plug-in parameter, `getProcessorUnsafe()->getParamNorm`); a remembered key with no lane with points -> the value put back (`target->set`, or `InsertSlot::setParamNorm`, retried while the slot is busy) and forgotten, only when `canRestore` (else `pending`); a control that no longer exists is just forgotten. `AppContext::followAutomation` (20 Hz) runs it for every edit with lanes or remembered values BEFORE the live plan is replaced (so a new lane's control is read before the plan drives it); for the edit playing as such, `canRestore` waits until the plan without the lane plays (`liveAutoSig` equals the lanes' signature) and no replaced plan is left (`AudioEngine::hasReplacedAutomation`); then `markDirty`. Test (core_test 3.27.0): fader, pan and plug-in values remembered, a lane without points ignored, the automation applied, the points gone (pending while driven, then each control back), Ctrl+Y (the same value remembered), one lane cleared (only its control back), a lane whose control is gone forgotten, save / load, not in the undo hash or text.
- **Compressed waveforms.** `src/core/WaveShape.h` (`waveshape::exponentFor`: 1 / 0.6 / 0.4 / 0.25 for off / light / medium / strong; `apply (v, e)` = sign(v) |v|^e). `drawThumbnailEnvelope (..., shape)` and `DirectWaveCache::draw (..., shape)` (WaveDraw.h) draw every peak and, zoomed right in, every sample through it; the take window's waveform of a take being recorded too; the Trim window's `TrimWaves::shape` after its own peak scaling (and for the faded picture, after the fade gain). `AppContext::waveCompress` / `waveCompressLast` (user settings "waveCompress", "waveCompressLast"), `setWaveCompress` (sends the project's change message and `trimWaveZoomChanged`), `waveShape()`, `showWaveCompressMenu`. `CompressWaveButton` (Uikit.h): a loud then a quiet burst, evened out and in the accent colour when on; click = onClick, right-click = onMenu. In the take and edit windows after `waveColourBtn` (kept in step in the timer like it), in the Trim window right of Reset crossfade. Test (core_test 3.27.0): off unchanged, full level and silence kept, symmetric, rising and never below plain, the three strengths in order, 40 dB down at about 1/6 and 1/3.

## 3.27.1: the third code sweep - the whole program

Nine reviewers (one per area of docs/CODE-SWEEP-BRIEF.md: A, B, C, D core, D UI, E, F, G, H) each read every file of their area in full and reported findings with the code quoted; every finding was then verified against the code before it was fixed, and the ones below were. Builds: core_test (par.sh), the three gates, the GCC warnings run (nothing new), asan (clean), fuzz (12 targets, 60 s each, nothing found after the channel-count fix below), rtcheck (clean), tsan (the stress and control-port tests clean; core_test under tsan was stopped after an hour inside the 3.4.0 take-matching test's convolution - an old, CPU-bound test that tsan slows by a factor of tens - so the newer thread paths are covered by the stress test and asan, not by tsan on core_test). Fixes only, no redesign; where a fix changes something the person could notice it is in CHANGELOG.md.

### A. The audio thread, playback and threads; starting and closing
- `Main.cpp shutdown()`: `audioSettings.reset()` after `windows.clear()`. The window was a member declared before `ctx`, so it was destroyed after the AppContext, and JUCE's `~AudioDeviceSelectorComponent` then called `deviceManager.removeChangeListener()` on the freed manager.
- `WinOwner.cpp startProgram`: the wide command line is sized with `wcslen`, not `String::length()` (code points; a character outside the BMP is two `wchar_t`s).
- `AudioEngine::automationDrives (const AutomationPlan::Item&)`: true while the playing session's plan (`autoNow`) or a replaced plan still waiting drives the same `Param*` (or a VCA member of it) or the same plug-in parameter. `holdAutomatedControls (..., stillDriven)` leaves such a key pending; `AppContext::followAutomation` passes `engine.automationDrives`. (Two edits automating one control of one mixer, or a disc preview: the restore used to write under the driving plan for a one-block blip.)
- `AudioEngine::startPlayback` waits for finishing takes only when a segment of the new session names one of their files.
- `AppContext::followAutomation`: `autoHoldSeen` (per edit: the lanes' signature, looked, pending) - `holdAutomatedControls` runs for an edit only when its signature changed, a restore was pending, or it is the edit playing (it used to build every lane's key 20 times a second for every edit). `mainDriveLamp` asks the volume for its free space at most once a second (`freeSpaceFolder / Gb / AtMs`).
- Checked and found sound (the reviewer's list, re-read): the whole callback path (no lock / allocation / wait), the 3.26.0 / 3.27.0 live-automation state machine in every order (publication before `block0`, hand-back of what the new plan does not drive, chains of replacements, stop with plans waiting, a stalled thread), the Bouncer's engine, start-up and closing order of every service, Uuid defaults, containers, async captures. Left: the offline engine starts helper threads and builds the pitch pipe's tables it never uses (a short hold at render start).

### B. Recording and saving
- **`TakeWriter::drain`** (serious): the fifo's count is read BEFORE the markers. The audio thread places an overrun marker (at `accepted`) and then pushes the block; the disk thread read the markers first and the fifo second, so a marker placed between the two reads was passed by (`drained > markAt`), `want` went negative, `prepareToRead` gave nothing, and the writer stood still for the rest of the take (every later block counted lost; the buffer's audio thrown away at Stop). Now: `avail` first, `want = min (kChunk, avail)`, a marker at or behind `drained` is paid at once, `want <= 0` reads nothing. Test: a producer thread pushing 512-frame blocks at a 1 ms-per-write "disk" with a 4096-frame buffer for 1.5 s - overruns happen, and `written == offered` at the end.
- `TakeWriter::finish()` (the destructor's work, returning `! failed()`); `Finishing::failedAtEnd`; `reapFinishedTakes` collects `finishFailures` (window, take) which `AppContext`'s 100 ms tick turns into `g->writeFailed`, a monitor event, a notice and a box. (A drive that filled during the final flush left a short file with no word.)
- `~AudioEngine` waits up to 10 s for `backupCopier.busy() == 0` after the finisher (logged if it times out).
- `MixParts::clearEmptySlots` (a thread_local flag read by `slotsFromVar`): `revertWindowToKept` and `undoCopyMix` set it, so a plug-in added into a slot the snapshot has empty is cleared; Paste mix keeps adding to what is there.
- `relinkAudio`: `keepAllWindows()` after `undoReset()`. `restorePiece` puts `pieceBars` back (in the undo hash since 3.8.0, never restored).
- `MainWindow.cpp` `saveWindowId = juce::Uuid::null()`; `Project::undo / redo` default `liveTake = null()` (both the 3.17.2 class; harmless in effect, wrong in kind).
- Checked and found sound: every saved field against undo hash / save hash / undo text / restore (the 3.19 - 3.27 fields included), damaged and partial files, the save path, kept state and the quit list, the record path, crash recovery, project copy, relink.

### C. Files that leave Fermata and come back
- **Stream pairs** (serious): the ReTouch pair's `apply` sets `st->applied` (the `PreviewApplied` that raises `saveHold`), as the pitch-curve pair did; `AppContext::saveNow (quick = false)` refuses while `saveHold > 0` with the message in `errorOut`; `systemRequestedQuit`, Project > Save, `newProject`, `openProject` and `closeProject` refuse with "write it back or close it first". (An automatic save wrote the preview into the project file; "Save and quit" and Project > Save did so for every pair.) `StreamPair::wrote` arms Undo fix through `armFixUndo` (it set `fixUndo` directly, so the validity check looked at the previous fix's edit).
- `rxlink::bringIn`: a save turned down because the piece no longer fits is said in `problems`; the copy is checked with `audioops::checkWrittenLength` (header against bytes). `EditWindowComponent::removeTrackFromEdit` names the waiting list's tracks first and drops the removed track's entry (the wait goes on for the others; it used to be left with a files list the piece no longer matched, so every save was refused silently).
- The watcher: skips while a mouse button is down and leaves out edits whose Trim window is open (both put the regions back from a copy); with nothing out in a program only every fifth tick looks at the files that came back (`rxFrom`), which are followed for the life of the piece. `Project::toVar` keeps an `rxSeen` entry while a waiting piece or an `rxFrom` names the file (not by whether the file exists this moment: a program that writes and renames leaves the path missing for an instant, and a save made while Fermata was closed must still be noticed).
- `stereojoin::apply`: refuses when any take file or piece on the two tracks is not in the plan (a take recorded, or a piece placed, while the files were written would have been left on tracks about to go); `keepAllWindows()` after `undoReset()` (every mixer's kept state had the two mono strips, so each was listed at quit and a revert undid the carried-over strip settings).
- `copyImportFiles`: writers closed before the new files are deleted (Windows refuses to delete a file open for writing); a part's channel checked against the reopened reader's count.
- `waitingFromVar` clears a `tracks` list of another length than `files`; `waitingFileIndex` never indexes past `tracks`. `FixTools.cpp` AAF import `firstEdit = null()`.
- Checked and found sound: the send / watcher / bring-in logic otherwise, Export for Processing and both re-links, the stereo join's plan / write / apply ordering, the imports, the AAF reader, `AudioJob` / `JobDialogBase` lifetimes.

### D. Edit operations and undo
- **`Project::renameTake`** (serious): `TakeWindowDef::setLabel (gid, label, &moved)` reports each (old, new) path it moved and names a second same-named track " (2)" as the recorder does; `renameTake` then points every `RegionFile::file`, `WaitingPiece::files` entry and `EditGuide::file` of every edit at the new names (the edits kept the old paths: Play refused or played silence until a relink). `restorePiece`'s edit branch: a snapshot `RegionFile` (or guide) whose file is gone keeps the file the same piece plays now when that exists - an undo never points a piece at a path nothing can read.
- `EditDef::splitRegion` keeps `waiting` on both halves (each still lies inside the files' take range, which is all `takeInWaitingFile` / `bringIn` / the hand re-link need); `left.outCurveOwn = false` and `right.setInCurve (EqualPower)` (an insert into a piece whose end was a fix join got a linear fade against an equal-power one).
- `applyRegionJob` refuses a waiting piece (a save coming back would have landed on the fixed file); it and the hand re-link clear `rxFrom` on the files they replace (a later save replaced the fixed part at the wrong `fileStart`, shifting its audio by seconds).
- `TakeTimeline`: the take being recorded cannot be renamed or removed (`isLiveTake`: the items disabled, `handleMenu` refuses) - removing it erased the group while the engine wrote into its files; at Stop `stopRecording` found no group and the files belonged to no take.
- Checked and found sound: every `EditDef` operation, gains, automation resolution, the hashes and view keys, Retime, the DSP helpers' bounds, TakeMatch, Feedback, Consonants, the 3.25.0 drags and Ctrl + Alt + click, the 3.20.0 Trim scrolling, N / V, the compress button wiring. Left: slips and fades of a piece on a short fix file can reach past its audio (silence only; `sourceLength` is the take's).

### E. The mixers and routing
- **Plug-in windows** (serious): `Project::beforeInsertsReplaced` (a hook `AppContext` sets to `closeEditorsOfMixer`) is called by `applyMix` before it replaces inserts - Paste mix / "send it straight to", Undo last copy, the quit box's revert and the mixer / window imports all come through it. `InsertSlot::set` destroyed the instance while `PluginHost::editors` still held its window and `AudioProcessorEditor` (a use-after-free).
- `SafePointer`s / ids instead of raw `this` across asynchronous menus and value boxes: `ChannelStrip::chooseTrack`, `BusStrip::chooseTrack` (the mixer looked up by id), `MeterSettingsStrip::chooseNewStrip`, the BusStrip pan read-out, `SlotCell::hovered` (a cell in another window during a drag), the Delete-mixer confirm, the stage mixer's boxes.
- `StripBase::lookKey / repaintIfLookChanged` (colour, band and plate text, VCA mask, latency, align text, gang look, theme) and `SlotCell::setLook` only on change: the 3 Hz pass used to repaint every strip twice and every slot cell whatever the model said.
- Checked and found sound: the whole 3.26.0 layout (`fitStrip` at every size, `layoutFrame`, `topPane` / `topContent` lifetime, the fader line drag with rebuilds mid-drag, the top scroll bar and wheel, `kMetersMinH`), `sendDialBuses` / `faderFeeds` / `routeSignature`, plan building and `carryOver`, nothing in the window on the audio path, PluginHost, kept-state hashing, VCAs, gangs, sends, LowCut, the control surface, the remote desk. Noted: Paste mix leaves a target slot's plug-in when the source slot is empty (a merge; unchanged).

### F. Mastering, bouncing and exports
- **`MasterRenders::commit` returns bool** (serious): the older slot's file is moved aside (`mrtmp-aside-...`, swept by `removeTemps`) rather than deleted first, so a failed move keeps the backup; `MasterRenderBatch::run` returns an error when a render could not be stored or `check` does not say Ready (it set `stale = false` on the OLD slot's file and the export went on with it, reporting success).
- **`Bouncer`** (serious): `pluginsMissingFrom` for the settings mixer AND every distinct `BounceItem::mixerId` (with "each piece through its own mixer" only the processing mixer was checked, which the master render does not use).
- **`renderFinished`** (serious): `renderingParts` (the flat description per edit, `masterRenderParts`, captured as the render starts - the files' and the design parts included) is compared with the description now by `masterRenderOnlyVolatileDiffers` after `learnMasterRenderVolatile`: the render is restamped only when every differing part is a known-volatile one; otherwise it stays Stale and the light says "out of date" (an edit made while the render ran used to be stamped up to date).
- `ddp::writeFileset` builds in "<folder> (writing)" beside the destination and moves the six files in only when whole (the old set deleted then); the zip through a `TemporaryFile`; a cancel check before zipping. `reviewpage::writeMany` closes its stream before deleting a half-written page on every failure path (an open `FileOutputStream` cannot be deleted on Windows). `Bouncer::render`'s normalise pass checks `cancel`.
- `MasterRenders::prune (liveEditIds)` from the batch constructor (renders of deleted edits and their entries go; the folder only grew). `check` saves the list only when it changed (`changed (before, after)`; it rewrote and fsync'd it on every look). The Bouncers (`MasterRenderBatch::run`) and the batch (`MasterExportJob::executeInner`) are no longer destroyed on the worker thread: plug-ins expect to be released on the thread that made them.
- `MasteringDef::fromVar` clamps `sampleRate` / `formRate` (`saneRate`), `mp3Kbps`, `oggQuality`, `languageCode`, the peaks, `endPadSectors`, `gapSectors`' upper bound, `index01Shift` / `index00Shift` (`kIndexAuto` kept).
- Checked and found sound: the stems passes and clean-up, the two-slot cache logic, loudness and levels, DDP streaming and verification, listening copies, AAF / video exports, the take sheet. Left: two Bouncers (two project clones) with stems when some renders are stale and others not; `Pdf.h` "%.2f" under a comma-decimal C locale; the take chart's rows past ~170 takes; the video export's mix file on a later failure; `findLame` per MP3.

### G. Files and messages from outside
- **`AudioFormats.h`** (serious): `checkedformat::sane` refuses a reader whose header says more than 256 channels, fewer than 8 or more than 32 bits, or no rate - JUCE's WAV reader reads through a 5760-byte buffer in whole frames, so a header saying more channels than fit asked for 0 frames and looped for ever (the import thread, the waveform pool or the playback reader hung); JUCE's AIFF reader divides by a frame size a header with 0..7 bits makes 0. `CheckedAiffAudioFormat` (the same three overrides plus `keepWithin`) replaces the plain AIFF format in `registerAudioFormats`; the fuzz test's `numChannels <= 64` guard, which hid the hang, is gone and the target reports a header shape the reader should have refused. The fuzz run then found the worse case at once: JUCE reads the WAV channel count as a signed 16-bit number into an `unsigned int`, so a header saying 0xFFFF channels arrives as about four billion - a signed comparison let it through, and an `AudioBuffer` of that many channels ended the program (`bad_alloc`); the check compares unsigned.
- `Project.cpp`: `finiteFloat / dbFromVar / panFromVar` (faders, input gains, sends, pans, stage levels, gain changes, preamp gains), `rateFromVar` (takes, edits, pieces, the guide, consonants: 0 unless finite and 1..1e6), `secondsFromVar` (fades, ramps: +-3600), automation point and `autoBefore` values (+-1e6); `samplesFromVar` clamps a double before the cast. `takematch::load` refuses a non-finite fps or one outside 1..1000 (a NaN passed "<= 0"). `Aaf::i64` clamped to +-2^50.
- The fuzz seed `richProjectSeed` now has an rx waiting piece, an `rxFrom`, `rxSeen`, `autoBefore` and `stems`.
- Checked and found sound: `TextBytes.h`, every `juce::var` indexed with a number guarded, `textFromBytes` on every outside text path, the caches' bounds, the remote protocol, MCU, Ravenna, LTC, crash reports, DDP check, `MasterRenders::load`. Not fuzzed (listed for next time): `.fwcol`, consonant files, the file-level project load and `.bak` fallback, window / mixer import, the WebSocket frame parser, `Hub`'s line splitting, `verifyFileset`, `renders.json`, `analyseLtc`, the memory-mapped reader, `inspectFiles` / `copyImportFiles`.

### H. Snappiness and a long session
- `TakeTimeline::drawLiveWave` walks only the clip's columns (the whole growing take - 14,400 columns of a 20-minute take per track - was walked 30 times a second); the recording repaint covers only the growing end (`liveEndX`, half a second back) once the take is over 400 px wide (narrower, the words along its bottom are fitted to its width, so the whole take is repainted).
- `DirectWaveCache::find` evicts the least recently drawn entry at 200 instead of clearing everything at 81 (with more than 80 files in view every repaint re-opened and re-read every file).
- The edit window skips edit-point flags outside the clip (every flag was drawn on every playhead frame; `fillPath` is not clip-tested by Direct2D); `TakeTimeline::updateMeters` repaints the name column only when a bar or hold mark moved (levels snapped to 0 below 1e-4) or while recording; the edit window's on a meter move, an M / S change (a small signature) or recording.
- (The mixer's 3 Hz repaints, the automation look's string work, the watcher's stats and the free-space queries are under E, A and C.)
- Checked and found sound: `WaveDraw.h` / `WaveThumb.h` / `WaveStore.h` / `WaveColor.h` (the 3.18.2 fixes hold), `TimeReadout`, `Uikit.h`, the look-and-feel and theme switch, `ValueEntry`, `BusyWindow`, `HotKeysWindow`, `BoxEdges`, every idle timer. `waveshape::apply` costs nothing when off. Left: `WaveColorCache::get` stats the file per paint with WaveColour on; the thumbnail / colour caches' ceilings (bounded by the project, not time); the thumbnail pool re-reads a finished take and does not pause for the next one; the Trim cache re-reads whole lanes at the widest zoom.

### I. Clean-up
- A scan of every function named only once in src and tests found nothing dead (the hits were locals and comments); the GCC warnings run found `aafwrite::auidBytes (const String&)` unused (left: one line, harmless) and nothing else new.

### Not changed on purpose
- The person's decisions in the brief, all kept. The ReTouch / Pitch previews now block Save / Quit / Close / Open with a message rather than being closed silently (nothing is thrown away by the program).

## 3.28.0: rename a bus from its mixer strip; the spectrogram box (sides, moving) and the frequency read-out
- **The name plate.** `StripBase::plateRenamable()` (virtual, false; `BusStrip` true - Int and Ext buses; `nodeId` is the bus). `StripBase::mouseDown` on the plate of such a strip (not a right-click): the first click starts `plateTimer` (`MouseEvent::getDoubleClickTimeout() + 20` ms), which then opens the colour picker as a plain click did; a second click (`getNumberOfClicks() >= 2`) stops it and calls `startPlateRename()`. The coloured bar at the top and a right-click on the plate open the picker at once, as before; an audio track's plate (`plateClicked`, the gang) is untouched. `startPlateRename` (refused while `engine.isRecording()`, as `DesignComponent::locked()`): `plateEd` (a TextEditor child of the strip, hidden otherwise) over `plateRect().reduced (3, 3)` with the bus's name, all selected, keyboard focus. `endPlateRename (keep)`: `plateEditing` cleared first (giving the focus away calls onFocusLost, which comes back), the focus given away, the box hidden; keep (Enter, focus lost) -> a trimmed, non-empty, different name goes into `BusDef::name` and `structureChanged()` (undo step; the mixers rebuild from the change message, never inside the call); Esc keeps the old one. `~StripBase` cuts the box's and the timer's callbacks before the members go. The strip's tooltip says so for a bus (`setLatencyTip`). HotKeysWindow: Mixer, double-click. No core test (UI only: the name is the same `BusDef::name` the Project Designer writes).
- **The spectrogram box (FixToolsDialogs.h `SpectrogramView`).** The flattening: `mouseDown` on an edge did `a = { selT0(), selF0() }; b = { selT1(), selF1() };` - `selT1()` / `selF1()` read the NEW `a`, so a box drawn top-down (a.y high, b.y low) got b.y = a.y = the bottom. Now `boxedit::order (a.x, a.y, b.x, b.y)` (BoxEdges.h: all four read first). The 2.90 `edgesAt` rules were right; this was the rest of the bug. `Mode::move`: `mouseDown` in `insideBox` (on the plot, inside `selRect`, `edgesAt == 0`) keeps the ordered corners in `moveA` / `moveB`; `mouseDrag` shifts time by the drag in seconds and pitch in the scale's units (Mel, Hz below warp 0.5, else log: the same units as `dragPitchWindow`) through `boxedit::moveSpan` (length kept, stopped at 0 .. duration and kMinHz .. maxHz); `mouseUp` calls `onSelection` only when the box moved. Cursor `UpDownLeftRightResizeCursor` inside. The read-out: `hover` (set by `setHover` from mouseMove / the select, resize and move drags; -1 on mouseExit or off the plot; repaints only the pitch-scale column and the strip under the plot), `paintHover` at the end of `paint`: a tag on the scale (whole Hz) and "<Hz> Hz   <note>   <t> s" right-aligned under the plot; `tuning::nearestNote (hz, refA)` (Tuning.h: equal temperament, cents rounded, nothing for a non-pitch); `refA` from `RepairContext::refA` / `NoiseModeDialog` (`project.pitchPipe.refA`) via `setReferenceA`. Test (core_test 3.28.0): the top-down box from the video, all four drawing directions, a point box; `moveSpan` both ends, no move, longer than the range, length kept over many moves; `nearestNote` at 440 and 415, cents both ways, sharps / flats, non-pitches.

## 3.28.1: the Display Organiser and windows' smallest sizes; Save as from the screen; import onto the existing tracks
- **Placing by smallest size.** `PlacedWindow` / `placeInCells (wins, area)` (ScreenLayout.h, pure): each window's cell and its constrainer's minimum (`DocumentWindow::getConstrainer()->getMinimumWidth / Height`; the mixer's is `topBarMinWidth() + frame`, 1080 + 16 for a cue / edit mixer); a window whose minimum exceeds its cell is given its minimum, with its far edge kept at the cell's (then kept within the area), and every window whose cell ends where this one starts (sharing rows / columns) is shortened to its left / top edge, never below its own minimum (then they overlap rather than leave the screen). Why it went off the screen: on Windows a native, resizable, constrained window runs every `setBounds` through the constrainer (`handlePositionChanging`), which keeps the top-left and grows the window right / down - Linux does not, so the sandbox could never show it. `placeWindows` (Main.cpp) builds the cells, calls `placeInCells`, sets the bounds and then, whatever the OS made of it, moves any window whose right / bottom is past the area back in; it returns the names of the windows that grew, which `arrange()` puts in the status line. `arrangeArea (mainIncluded)` is the one place the area is worked out (the main window's screen, below its menu bar when the main window is not being arranged) and `windowPlacesNow` uses the same, so "Read from screen" and Arrange agree. Test (core_test 3.28.1): the case from the screen (2560 wide, 60 %, mixer min 1096 -> mixer 1464 .. 2560, edit 0 .. 1464), both fit, the neighbour's own minimum, wider than the area, above / below, 1 above 2 (the top window untouched).
- **Read from screen / Save as.** `coveredFraction (rect, others)` (24 x 24 grid): `windowPlacesNow` leaves the main window out when the other windows cover 60 % or more of it. `OrganiserComponent::screenMatchesPicture()`: each of the picture's windows that is on the screen is compared with its cell (overlap / union >= 0.9); `saveAs` calls `saveFromScreen()` first when they do not match and the status line says so.
- **Import onto the existing tracks.** `existingTrackIndices (projectChannels, plan)` (ImportPlan.h, pure): import track key -> project track index, mono and stereo counted separately, in order. `ImportDialog::existingBox` ("importOntoExisting" in the settings; shown only with project tracks; it and `byNameBox` untick each other); `mapTracks (..., mode)` with mode 2 = the existing tracks (`existingTrackMap` wraps the indices as ids), 1 = by name as before, 0 = plain; `finishImport` / `finishImportToEdit` take the int. The summary lists "<project track>   <   <file's track>". The track's name comes from the project track as before (`f.trackName = tr->name`). Test (core_test 3.28.1): mono / stereo in order, more files than tracks, no tracks, a stereo file never on a mono track.

## 3.29.0: a selection of several boxes (Shift / Alt); the room tone from the quietest moments; Gain in the panel
- **The selection.** `SpectrogramView` keeps `boxes` (`denoise::SelRect`, add / take out, in drawing order) and `rects = denoise::selectionRects (boxes)` (Denoise.h, pure: the grid of every box's edges tested cell by cell with `selected()`, the cells joined up each column of time and then across columns with the same pitch span; the boxes are put in order first, whichever way they were drawn). `selT0..F1()` are the extent of every rectangle; `selectionRects()`, `selectionSpans()` (`selectedSpans (rects)`), `boxCount()`. `mouseDown`: Shift / Alt -> a new box (`drawing`, `drawAdd`; Alt with nothing selected does nothing; a plain drag clears the boxes first); else `edgesAt (p, &which)` / `boxAt (p)` find the box (added ones, the last drawn first) to resize / move (`editing`, `writeBack (i)` after every drag step so the shape follows, `selectionChanged (tell)` recomputes the rectangles; an empty result clears the boxes). `mouseUp` of a draw adds the box when at least 2 ms wide. Paint: `RectangleList` fill of the rectangles, the border = each rectangle expanded by 2 less the fill (so holes show), the surround per rectangle, the live box yellow (add) or red (take out). Test (core_test 3.29.0): one box, two apart, two touching, a corner cut out (area and every point agree with `selected()`, no overlaps), a take-out over nothing, the whole box taken out, a hole, a backwards box, none.
- **The tools over the selection.** RepairDialog: `preview (what)` builds one op per rectangle (f0 / f1 and t0 / t1 from it) and `startFixes (group)` runs them in turn on the same track copy (one job; `Out::ops`); `history` `Step::ops` = the group's size and `revert()` pops that many from `committed`, so the group is one Undo. `startFix` (paste, nudge) wraps a single op. NoiseModeDialog: `opsForSelection (spec)`, `opsStart / opsEnd`; `retouch`, `applyGain`, `denoise` pass the whole list to `prepareRepairJobs` (which already took a list). Room tone: `rtSpans = view.selectionSpans()`, the length check over their total; `FixSpec::dnSpans` (empty = the one range dnBoxT0 .. T1), `roomSpans()`, `roomKey()` (the cache key in `attachRoomTone` and the take path: the spans and the way of learning - `std::map<juce::String, ...>`); `audioops::learnDenoise (fm, file, spans, p, err)` (the old one calls it with one span); the picture's own learner takes the regions of every span; `learnFileProfiles` gets one SelRect per span.
- **Quietest moments.** `denoise::Params::quietest` / `quietestPercentile` (0.2); `quietestMean (vals, p)` = the p-th percentile of a bin's frame powers / -ln(1-p) (the percentile of an exponential in terms of its mean; under 8 frames: the mean); `smoothPsd (m, wide)` (3 taps, 5 for a percentile estimate). `learnProfile` and `ProfileLearner` (per-bin `vals` kept when quietest; `finish()`) both use them, so the picture, the take files and the edit files learn alike. UI: `quietBox` on the Denoise tab of both dialogs (a third row: RepairDialog had three, NoiseMode's panel now has at least three), read at L time into `rtQuietest`, carried in `spec.dn.quietest`; `roomShortText` / `roomText` say so. Test (core_test 3.29.0): white noise with a 1 kHz note on for half the time: the plain profile has the note +57 dB above the floor, the quietest one within 8 dB of it (a few dB up: the percentile falls inside the noise's own distribution when the note is there half the time), the floors agree within 1.5 dB (the bias correction), the per-cell learner the same; a 0.15 s region does not crash. A note that never stops cannot be told from the noise (no quiet moments): said in the CHANGELOG.
- **Gain.** `gainSlider` / `gainCap` in the Gain tab of both dialogs (row 0; the hint on row 1), the value in uiPrefs "gainDb" as the pop-up's was; `applyGain()` replaces `openGain()`: G and the button apply at once (0 dB refused with a note). HotKeysWindow: G, Shift+drag, Alt+drag.
- **Seen, not changed:** the core test's 3.8.0 rate-converter level check (line ~10768, "0.1250 against 0.1202") failed once in this build's first run and passed on the next two: timing-dependent (the engine test runs threads); worth a look if it recurs.

## 3.30.0: the offline fixes on every core
- `src/core/Workers.h`: `workers::threadsFor (items, hardwareThreads, reservedForAudio)` = min (items, clamp (hw - reserved - 1, 1, 16)); the one-argument form reads `std::thread::hardware_concurrency()` and `CoreReservation::get().reservedLogicalProcessors()`, so it follows the CPU-cores setting on whatever machine. `workers::forEach (items, fn, cancelled, threads)`: a pool of std::threads (the caller one of them) pulling item indices from an atomic; fn returns an error text; the first error (or a cancel) stops further items being started and is returned ("Cancelled." for a cancel). Used in: `RepairDialog::startFixes` (the picture's tracks: each track's audio is moved out, fixed and moved back; the declick report summed under a mutex - `lastDeclickReport()` is thread_local, so each worker reads its own), `runRegionJob` (FixToolsCommon.h: a piece's files, results collected in order into `j.results`; on an error every file made is deleted), `audioops::fixTakeFiles` (the take's files, `newFiles` in source order). `runRepairJobs` still runs the pieces one after another (the files inside each in parallel). What is shared between the workers: `formats()` (JUCE's AudioFormatManager makes a new reader per call; nothing is written to it), the profile maps (read-only once built), `uniqueFile`'s set (already under a mutex), pocketfft (no plan cache). ThreadSanitizer: a small program of its own (the pool, then an Attenuate and a De-click over 8 tracks at once through applyFix, as the dialog does) ran clean - 0 reports. Test (core_test 3.30.0): the thread counts (studio PC, laptop, a reservation that leaves nothing, no items), every item once at 1 / 2 / 5 threads, an error stops the rest, a cancel stops the rest, no items.

## 3.30.1: the fourth code sweep - the code since 3.27.1 (3.28.0 to 3.30.0)

Three reviewers, each given the whole diff since 3.27.1 and the current files of an area (A: the spectrogram selection and the tools over it; B: the parallel offline fixes and what they share; C: the Organiser, the import onto existing tracks, the bus rename), reports in the scratchpad's sweep/S4-A.md, S4-B.md, S4-C.md. Every finding verified against the code before the fix. Fixes only.

### Fixed
- **A-F1 De-click per rectangle (serious).** `selectionRects` splits a shape by pitch too; De-click ignores pitch, so rectangles stacked over one stretch de-clicked it once each (the later passes on the mended sound, the report added up), and in Noise mode `findLostTogether` gave the stacked ops the same lost-sample list, so `fixPiece` put the dropout back twice. Now `RepairDialog::opsOverSelection` and `NoiseModeDialog::opsForSelection` give a Declick one op per `selectionSpans()` stretch; and `findLostTogether` clears the list of any later op with the very same s0 / s1 as an earlier one that found some.
- **A-F5 the click-box decision per rectangle.** `boxRoundClick` (Patch, f0 <= 1 kHz, <= 250 ms) was decided per op: a sliver left by a cut-out searched for lost samples and skipped its ReTouch when it found a jump. Both dialogs now decide it from the selection's extent (`selT0..T1`) and, when the shape is no click box, give every op `lostKnown = true` with an empty list (no search, nothing to mend; `findLostTogether` skips them).
- **A-F7 the seam in time.** `selectionRects` now joins runs along TIME first (per strip of pitch) and then strips with the same time span up the pitch axis, so each rectangle is as long in time as it can be: a corner cut out gives [t0, t1] x [f0, c) + [t0, c') x [c, f1] - the seam is in pitch, where Denoise is feathered a quarter octave and ReTouch has its band edges, not in time where every tool fades over ~100 ms at a rectangle's ends. Test: the corner case has the full-length strip; a hole has two full-length strips of four rectangles.
- **A-F2 a move that empties the selection.** `mouseUp`'s move branch told nobody when the moved box slid wholly under a later take-out (`selectionChanged (false)` in the drag had cleared `boxes`, so `editing` was stale and the guarded `selectionChanged (true)` never ran): the info line and buttons stayed stale and R ran a ReTouch over the whole area with f0 = f1 = 0. Now the move branch calls `selectionChanged (true)` whenever the drag moved anything, and `preview` refuses every kind but De-click without a selection.
- **A-F3 ghost boxes.** `selectionChanged (tell = true)` drops any box that overlaps none of the rectangles (wholly under a take-out, or a take-out that took nothing) and resets `editing`; never in the middle of a drag (`tell = false`), where `editing` points into the list. A box with f1 <= f0 is not pushed at mouseUp.
- **A-F4** a plain drag now clears `rects` / `hasSel` with `boxes` at mouseDown (the old shape stayed drawn until mouseUp). **A-F6** `boxCount()` counts the added boxes, not the rectangles. The fix counts in `mayClose`, "fix(es) made", "Fix N done", "N fix(es) left" use `history.size()` (steps), not `committed.size()` (ops).
- **B-F1 an exception in a worker ended the program (serious).** `workers::forEach` runs fn on std::threads; an exception escaping one (std::bad_alloc above all - the memory is multiplied by the workers: a 10-minute stereo denoise at 96 kHz on 11 workers is ~7.5 GB) called std::terminate, and one on the calling thread unwound past the joinable pool with the same end; the AudioJob's own catch used to cover this work. Now each item runs inside try / catch (bad_alloc -> "Not enough memory for this fix: try a shorter area, or fewer tracks at once."; anything else -> "Something went wrong while processing."), the first error stops the rest as any error does, and a thread that cannot be made (std::system_error) just means fewer workers. `workers::threadsForMemory (items, bytesPerItem)` caps the count so that threads x bytes stays under half of `SystemStats::getMemorySizeInMegabytes()`; the callers pass an estimate (fixTakeFiles: 2 channels x (the ops' longest span + 16 s at 96 kHz) x 40 bytes; runRegionJob: the piece likewise; startFixes: (t1 - t0 + 20 s) x 40 per track). Test: a worker that throws bad_alloc / runtime_error returns the error text (both ended the test program before); the cap with a petabyte an item is 1.
- **B-F2 `formats()`** registered its formats behind an unsynchronised `static bool done`, and is now first reachable from several workers at once (the edit window's Pitch path): the registration now runs inside the static's own initialiser (once, under the language's lock).
- **C-F1** the import window's summary was built by `setBase()` before `byNameBox` / `existingBox` had their remembered states: `refreshSummary()` once more at the end of the constructor. **C-F2** `saveAs` read the default name after `saveFromScreen()` had cleared `loadedName`: read first. **C-F3** `screenMatchesPicture` returned false when none of the picture's windows was on the screen and two or more windows were open (a layout composed for closed windows was replaced by the screen's): now true when there is nothing to compare. **C-F4** `placeInCells` floored a neighbour without a constrainer (take / edit windows have none) at 1 px: `kLeastW / kLeastH` = 160 x 100 for a window with no limit of its own. Test: three columns on 1600 with a 1096 mixer on the right - the middle keeps >= 160, the mixer ends at 1600, the left one ends where the mixer starts.
- CHANGELOG 3.30.0 wording: the studio PC with 6 cores (12 logical processors) reserved gets 11 workers, not 12.

### Checked and found sound (by the reviewers)
- The three room-tone learners and `roomKey` agree on both sides; `history` / `committed` stay consistent for groups, pastes, nudges, failed and cancelled jobs and Undo (`preparedOps` counts ops on both sides); `editing` is guarded everywhere; `selectionRects` agrees with `selected()` and never overlaps; the per-rectangle processing context matches the drawn surround; Noise mode's Step holds whole jobs.
- Nothing is written by two workers at once (per-index slots, the declick report under a mutex and `thread_local`, read-only profile maps, `uniqueFile` never inside a pool and under its own mutex); error / cancel paths delete every file and leave `newFiles` / `j.results` empty or complete in source order; JUCE's `AudioFormatManager::createReaderFor` is read-only and the Checked* formats add no state; pocketfft has no plan cache (`POCKETFFT_CACHE_SIZE 0`) and runs single-threaded; `reservedLogicalProcessors()` is a relaxed atomic; the pool threads inherit the process CPU set (off the audio cores); the AudioJob thread is never killed, so the pool is always joined.
- The bus-rename box: all three TextEditor callbacks are posted (`postCommandMessage` with a WeakReference), so `endPlateRename` cannot re-enter through `giveAwayKeyboardFocus`, a deleted strip is safe, and `structureChanged()` is asynchronous so the mixer never rebuilds inside the strip's callback. `placeInCells` geometry (the corner-only case, the order of processing); the Windows constrainer applies to the frame-included rectangle, so the OS never enlarges a placed client; `windowPlacesNow` / `arrangeArea` agree; `existingTrackIndices` (unique keys, channels matched, the per-piece path shares the tracks, `trackName` from the project track).

### Noted, not changed
- After one worker's error the others finish their current item before the error shows (a delay, not a loss). Two fix windows each running a job make two pools (overcommit only). The seam of a cut-out shape is now in pitch, not gone: the proper cure (one time span with a pitch mask varying along it) is a bigger change. `learnDenoise` caps each span at 60 s while the picture and `learnFileProfiles` do not (pre-existing).
- Tool checks: core_test under asan was first run while the reviewers had the machine (the compile was killed for memory and a stale test binary ran against the new core: a false heap overflow in `FixOp` from the grown `FixSpec`); rerun with the machine free: core_test, the stress test, the time-format, remote-hub and ReHarmoniSer tests all clean (0 reports). Fuzz (12 targets, 60 s each): nothing found. ThreadSanitizer: the stress test and the parallel-path program (the pool, an Attenuate and a De-click over 8 tracks at once) clean, 0 reports; core_test under tsan not run again (the hour-long 3.4.0 section, as noted at 3.27.1).

## 3.30.2: one repository for fermata and fermata:Live, built from one shared core

fermata moved to a new repository that also holds its companion, fermata:Live (the live-concert recorder: `docs/LIVE-BRIEF.md`). The old repository is kept as it was. The rules both programs follow are in `COMPATIBILITY.md`, and the working rules for each conversation are in `CLAUDE.md`. **fermata's code is unchanged from 3.30.1**: only the build lines, the checks and the documents changed.

### The layout
- **Shared** (built into both programs): `src/core` (unchanged, plus `Compat.h` and a `README.md` saying it is shared), `cmake/SharedCore.cmake`, `src/reharmoniser/third_party/pocketfft_hdronly.h` (used by `DenoiseFft.cpp`) and `third_party/signalsmith-stretch` (used by `Retime.cpp`).
- **fermata's own**: `src/Main.cpp`, `src/ui`, `src/reharmoniser`, `CMakeLists.txt` (its version), `CHANGELOG.md`, this file.
- **fermata:Live's own**: `src/live` (`Main.cpp`, `Live.cmake` with its version, `README.md` shipped with the program), `LIVE-CHANGELOG.md`, `docs/LIVE-BRIEF.md`, `docs/LIVE-DEVELOPMENT-NOTES.md`, `assets/fermata-live-icon.png`.
- `src/live` sits beside `src/ui`, so both programs include the core the same way (`"../core/X.h"`), and nothing in fermata had to move.

### The build
- `cmake/SharedCore.cmake` holds the one list of the shared `.cpp` files (`FERMATA_SHARED_SOURCES`), the JUCE settings (`FERMATA_SHARED_DEFINITIONS`), the JUCE modules (`FERMATA_SHARED_JUCE_MODULES`), and two helpers both targets call: `fermata_use_windows_audio` (ASIO and WASAPI) and `fermata_keep_debug_symbols` (/Zi, /DEBUG and the .pdb copied to `dist/<folder>`). These are the lines fermata's target had, moved so both programs use them.
- `CMakeLists.txt` keeps fermata's target and its version line and includes `src/live/Live.cmake`, so work on one program never edits the other program's build lines.
- **fermata is built exactly as before.** Both CMake setups (3.30.1 and 3.30.2) were configured on Linux against a stand-in for JUCE that records what is asked for, with `WIN32` and an ASIO folder given, so the Windows lines ran too. Comparing the compile commands: the same 52 files, and every file's definitions, include paths, standard and optimisation are identical except `FERMATA_VERSION_STRING`. fermata:Live's target has 23 files (its `Main.cpp` and the same 22 core files), with the same shared definitions and drivers.
- fermata:Live sets `FERMATA_VERSION_STRING` to "Live 0.1.0", because the shared audio engine writes "Fermata " plus that text into recorded WAVs. Its window shows `FERMATA_LIVE_VERSION_STRING`.
- The GitHub workflow builds both programs (two downloads: "Fermata-windows" as before, and "fermata-Live-windows"). A second job on Linux runs the compatibility check. It runs beside the Windows build, so it adds no waiting, and a failure turns the run red.

### The compatibility check (`tests/compat_test.cpp`, `tools/compat-check.sh`)
- It compares the numbers in `COMPATIBILITY.md`'s box (contract version, the project file's format and version, the sample rates) with `src/core/Compat.h` and `Project::kProjectRates`.
- It checks that every file listed as shared exists.
- It checks that nothing in `src/core` includes a program's own code. Only `../reharmoniser/third_party/` and `../../third_party/` are allowed.
- It saves a project through the shared code and reads it back: the file must say `"format": "Fermata project", "version": 1`, and the sample rate, tracks and take window must come back.
- `tools/compat-check.sh` builds it with the WHOLE core and nothing else, which also shows that the shared core links without either program's code. It took about a minute here with JUCE already built. `tools/sandbox-check.sh` runs it too, after the core tests.
- Checked that it fails when it should: a copy of the repository with the contract version, the sample rates and a shared path changed in `COMPATIBILITY.md`, and a core header including `../ui/Uikit.h`, gave 4 FAIL lines and exit 1.
- `tools/syntax-check-ui.sh` and `tools/tool-checks.sh` now include `src/live`. fermata:Live's log files in `tool-checks` start with `live_`, so its `Main.cpp` does not share `src/Main.cpp`'s log.

### Left out of the new repository
- Stray copies that had collected at the old repository's top level, none of them used by the build: the folder `Fermata-3.5.0-everything-changed-since-Sunday-25-Oct-1pm` (92 files from a 3.5.0 upload); loose copies of `HotKeysWindow.h`, `TakeMatch.cpp`, `TakeMatch.h`, `TakeMatchService.h` and `VersionsWindow.h`; an old `core_test.cpp`; an old `DEVELOPMENT-NOTES.md` (the current one is `docs/DEVELOPMENT-NOTES.md`); and `tools/__pycache__`.

### The first upload to the new repository
- GitHub's web upload takes fewer than 100 files at a time, so the first upload is in numbered parts of whole files with their folders. Upload them in any order, and each part adds to what is there.
- The workflow is made by hand last (Add file > Create new file, named `.github/workflows/build-windows.yml`, with the text pasted in). That first build therefore has every file in place.

## 3.31.0: never change the audio device without asking; the polarity key; the S1 (built in the fermata:Live conversation, 5 November 2026)
- **`src/ui/DeviceGuard.h` (new).** `GuardedDeviceManager` (AppContext::devices) is JUCE's AudioDeviceManager with `createAudioDeviceTypes` overridden:
  every real driver type is wrapped in a `GuardedType` that forwards everything to it, but takes the real type's "list changed" itself (posted to the
  message thread) and hands it to `DeviceGuard::listChanged`. JUCE's manager hears of it (`passOn`, i.e. `callDeviceChangeListeners`) only while the
  device in use is still in its type's list - its private `audioDeviceListChanged` then changes nothing. (Without the wrapper: device gone -> JUCE
  closes it and runs `initialiseFromXML (..., true)` / `initialiseDefault`. Shown by the test below, step 0.) JUCE makes its own type list only while
  the manager's list is empty (`createDeviceTypesIfNeeded`), so the types must be made by the override, not added beside JUCE's.
- **`DeviceGuard`**: `known` lists per type; added devices go into `pending` (asked 1.5 s after the last addition, never while
  `engine.isRecording()`: the 500 ms timer asks once the take has stopped); the question (AppContext's `ask` hook: an AlertWindow whose first button
  - Keep - is also Escape and the window's close) offers Keep using X / Change to Y / Choose in Audio settings. Y: an ASIO driver first, then one of
  the type in use, then Windows Audio; for types with separate inputs and outputs the new input of the same type goes with it. Change: stopPlayback,
  then `closeAudioDevice` + `initialise (256, 256, xml, false)` with the type, the names and the project's rate (the start-up path); if it fails, the
  previous setup is opened again the same way and the user told. **The device in use gone**: not passed on, said once, `closeAudioDevice` (not while
  recording: the dead device is left under the take), and opened again from the saved setup (the settings' "audioDevice", which JUCE's
  lastExplicitSettings keeps) when its name is back - after the take if one records. **Start-up** (Main.cpp): `initialise (..., saved == nullptr)`, so
  a saved device that fails opens nothing else; `deviceGuard->start (err)` says so 2.5 s later (once the main window is up) and waits for it.
  Hooks: recording, playing, stopPlayback, afterChange (`applyAudioSettings` + `matchEngineRate`), openAudioSettings, savedSetup, projectRate, notice.
- Checked on Linux with fermata:Live's "Test signal" type (it can plug and pull a second device; its masks are now clipped to its own channels, as
  fermata asks for 256): JUCE alone switched to the other device by itself; with the guard: pulled out -> nothing opened, told, opened again when
  back; plugged in -> asked after 1.5 s, Keep changes nothing, Change stops playback and opens it (and it is remembered); during a recording the
  question waits; the device gone during a take is not closed under it and is opened again once the take stops; a start-up with the saved device
  missing opens nothing and opens it when it appears. (A scratch program, not in the repository: it needs a message loop with timers.) Not checked:
  real ASIO / WASAPI drivers on Windows.
- **The review (one reader) and what changed in the guard.** (1) Serious: a list change of ANOTHER type was passed on without looking at the open
  device - one endpoint is listed by Windows Audio's three modes and DirectSound, so after "Windows Audio" said the device in use had gone (left open
  under a take), the next type's news reached JUCE, which closed it, opened the default endpoint, ended the take and saved the default as the device.
  Now every list change first applies JUCE's own test to the open device (`listed`: its name in its own type's lists, exactly as JUCE compares) and
  passes nothing on while it fails; once the take stops, a device still gone is closed. (2) A stale "gone" could reopen the old device over one chosen
  meanwhile in Audio settings: the wait ends as soon as another device is open, and a device is opened again only when nothing is open (or it is the
  dead one left under a take). (3) A driver that is listed but cannot open (an ASIO interface switched off: its driver is listed from the registry)
  was never opened by itself, and plugging its interface in asked to change to its Windows Audio endpoints instead: with nothing open, any list change
  of any type first tries the saved device again (only if its type lists it), and the devices that came with it are not asked about (3 s).
  Also: one message at start-up, not two (Main's own error box only when nothing was saved); the startup message only if nothing is open by then.
  The test program gained a second driver type (one that lists Test signal's devices too, and one that is always listed but opens only while Test
  signal's second device is plugged in) for these three cases.
- **The polarity key.** `StripState::polarity` / `BusState::polarity` (Int Buses) - `Flag`, with `lastPol` (audio thread) so that a change is a gain
  ramp from +1 to -1 over one block; applied in `AudioEngine` right after `lowCut.process`. Saved by `stripToVar` / `busToVar` as `"polarity": true`
  only when on (rule 4 of COMPATIBILITY.md: a key added; missing = off), read under `faders` like the low cut, and in `mixKeepHash` only when on (so
  the mixers kept by older versions still match). `LowCutKey` (LowCutPanel.h) takes an optional `Flag*`: given, a Ø key (the preamps' orange) takes
  the place of the read-out, whose text moves into LC's tooltip. Tests: compat_test (one strip on, one off, round trip), core_test (a bounce with the
  key on is the same bounce inverted, sample for sample after the first block).
- **The S1.** `surface::GainFaders` (new last action, so saved maps keep their numbers): `dialGain` and `flipped` together, and both off again; a
  banner on the S1's bottom line and a notice. Default on `mcu::Left` (the S1's User 3, which EuControl sends as cursor left), moved from MeterBridge
  (F7 keeps it); `surfaceMapVersion` 2 moves it in a saved map too when User 3 still had its old standard job. `openPorts`: with nothing chosen, the
  surface-looking port with the lowest number at the end of its name ("Euphonix EuMidi1", not "...EuMidi11"). SurfacePanel: the intro's height from
  its text (`TextLayout`), the action rows in two columns when the keys area is at least 964 px wide (the window now opens 1300 wide); the new setup text (MIDI Setup, then
  Applications +, then EuMidi1). The user found that the S1's top-row keys other than Bank and Nudge never arrive (EuControl keeps them).

## 3.32.0: the automation lanes told apart (built in the fermata:Live conversation, 5 November 2026)
- The user's screenshot (a 2000 px wide edit window, LV08): with Automation on, every lane looked the same - the rows' two shades (`theme::wave`
  0xff0f1821 / `waveAlt` 0xff131e2a) differ by four steps, `paintLane` then darkened every row by 18 %, and only the name column had a line under
  each row. All in `EditTimeline` (src/ui/EditWindow.cpp); nothing shared, nothing saved.
- **`paintLane (g, ed, inf, rowTop, name, lastOfTrack, lit)`**: after the 18 % black, the node's channel colour (`chan::of`) at 11 % over the whole
  row; a line at the row's foot - under a track's last lane (`lastOfTrack`: the next row is another `RowInfo::block`) the colour at 80 % and a
  black 55 % line below it, between a node's own lanes the colour at 40 %; `lit` adds white 9 %. The dashed default line of an empty lane: 35 % -> 62 %.
- **The lane's name at the right-hand edge**: "<name> · <paramLabel>" in a black 45 % rounded box at the right of `viewport.getViewArea()`, at the
  foot of the row (the low end of the range, where the line seldom is), drawn BEFORE the automation line so points and line stay on top; only for
  rows of 22 px or more, and not when it would come within 40 px of the names. Width at most `kLaneNameMaxW` (360, then "..."), and only worked
  out when the clip reaches that far right (the playhead's and the meters' narrow repaints skip it). EditViewport repaints the whole timeline on
  every scroll (`visibleAreaChanged`), so the fixed-on-screen name is never left behind.
- **The lit lane**: `hoverRow`, set by `updateHoverRow` from `mouseMove` (the sticky band's shift applied first: over the band, no row) and cleared
  by `mouseExit`; `setHoverRow` repaints only the old and the new row (the visible width). Scrolling and the row-height keys move the timeline
  component, and JUCE then sends a fake mouse move (`Component::sendFakeMouseMove` in setBounds), so the lit row follows. The name column of the lit
  row: white 8 % and a 2 px yellow frame (drawn after the blue frame of a selected track). Lane rows off the clip are no longer painted at all.
- Checked on Linux (the Linux build of fermata, a 2000 x 800 screen, a project like LV08: four tracks with audio, Room with a Fader and a Pan
  lane, three Int Buses, an Ext Bus, a VCA; points on Solo, Room's pan and Spots): the colours, lines and names as above; the lit lane follows
  the mouse and leaves nothing behind; the names are hidden at the shortest rows and move with the window's edge when the vertical scroll
  bar comes and goes. Not checked: the look on the user's own screen and colours.

## 3.33.0: Noise mode and the ReTouch window - the picture as heard, harmonics, the selection tools (built in the fermata:Live conversation, 5 November 2026)
- **The picture as heard** (src/ui/FixToolsCommon.h). `TrackPart` was "the piece on top" over each stretch between the pieces' starts and ends, switched hard at every
  edit point: Noise mode's pyramid, the ReTouch window's picture (`openRepair`, which built its own such parts), its consonant copy and the room tone learned from
  marks 1 - 2 all read two takes butted together - a full-height line at every join (the user's LV08: "a click that does not sound"). Now a part is one piece's
  file over its whole audible span (`heardPart`: fadeInBegin .. fadeOutFinish) with its fades and volume changes (`TrackPart::gainAt`, the rules of Playback.cpp's
  addRegionSegments - the piece's own level holds from its first sound), and the readers add them: `readTrackMix` (mono), `readTrackChannels` (every channel, the
  widest file's count). A stream window's parts stay plain (`shaped` false). `learnFileProfiles` reads each part's file as it is (the noise of the files the fixes
  work on). Checked with a scratch program against PlaybackSession::renderSegments over two cross-fades (equal power, linear), a piece level and a volume change on
  a mono and a stereo track: equal to the sample (4.5e-8); the old reader's step at the edit point 24 x the music's own, now gone.
- **Alt take-outs** (SpectrogramView::selectionChanged): the 3.30.1 tidy-up kept a box only if it overlapped what was left - a take-out never does (that is what it
  took out), so every take-out was dropped at mouse-up and the hole closed (shown by a scratch program on denoise::selectionRects). A take-out is now kept when it
  overlaps the selection made by the boxes before it; the box carrying harmonics is always kept; with brush strokes no box is tidied away.
- **Harmonics** (SpectrogramView::setHarmonics / makeHarmonics; HarmonicsBar): `harmBase` (a box index) carries `harmBoxes`, put straight after it in
  `allBoxes()` so a later take-out takes from them. The box's pitch (`measurePitch`): the analysis bins across the box averaged in power over its time (the long
  bass layer below the cross-over), the peak 6 dB above the median or the box's middle, placed by a parabola on the dB of three bins. Harmonic n of fundamental
  F = pitch / (k stretch(k)), k = "my box is", at F n sqrt (1 + B (n^2 - 1)), B from Spread (the 10th harmonic that many cents off). Widths in Hz: the box's
  reach below / above its pitch times (n / k)^(1 - thinning) (thinning 1, the standard: the same Hz), clamped to 45 % of the distance to harmonics n - 1 and n + 1
  (the user: "at 10,000 Hz it should only have 30 Hz either side as well"); up to the top of the picture (maxHz). Not on a Time box. The count, spread and
  thinning are in the settings ("harmCount", "harmSpread", "harmThin").
- **The selection tools** (SpectrogramView::Tool, SelectToolsBar): Rectangle; Time (f0 0, f1 = half the rate: `isTimeBox` - moved and resized in time only);
  Brush - strokes of `Dab`s (seconds and log2 Hz, the reach up and down from the pitch scale at that height, so the circle on the screen is what is painted; gaps
  filled at a third of the radius), ordered with the boxes by `seq` (`boxSeq` beside `boxes`). A plain drag (or stroke) starts afresh only once it has moved 3 px;
  a click places the playhead (`placeCueAt`, as the ruler's click). "selTool" and "brushPx" in the settings.
- **A brush shape to the fixes** (shared: src/core/AudioOps.h / .cpp): `SoftMask` (a grid over time and log2 Hz, 0 .. 1, edges held beyond its ends) and
  FixSpec::mask / maskLead. applyFix for a Patch or a Denoise with a mask (and no lost samples to put back): the fix on its box as before, then `keepMasked` -
  after = before + mask x (after - before), in a Hann STFT three-quarters overlapped (N 2048, 4096 above 64 kHz; exact where the mask is 1). The picture's
  `buildSoft` paints the boxes and strokes in order on a grid (4 ms x 1/64 octave, at most 1600 x 720 cells, two empty cells round it); each 4-connected piece is
  one `Part` (its box and its mask; f0 0 / f1 half the rate when it reaches the picture's bottom / top). Both windows build their fixes from
  `selectionParts()` (rectangles exactly as before when there is no stroke); Noise mode's prepareRepairJobs adds to maskLead where a fix is cut at a piece's start.
  core_test: a mask of 1 = no mask (1e-4), 0 = nothing changes, a low band only, half = -6 dB, a mask begun part of the way in. Checked in Noise mode on Linux
  (a piano-like test edit): the masked fix changed only under the stroke.
- **L C R** (both windows' `setupSides` / `sideKey`): the Before <-> After (Side) slider to 0 / 50 / 100, keys Z \ X, shown with the slider in every method.
- **specTime**: seconds under a minute, then m:ss(.d), then h:mm:ss - the ruler, the read-out, "Showing", the selection and room-tone texts.
- Not done: feathering (the user: ship the rest first). The plan: a feather in time (ms) and pitch (semitones) for every tool, as a soft edge on the same grid
  (a distance transform from the shape, a raised-cosine fall over the feather) - the masks and keepMasked are already made for it.

## 3.34.0: feathering (built in the fermata:Live conversation, 5 November 2026)
- SpectrogramView: `featherT` (s) and `featherP` (octaves); `softSelection()` is now strokes OR feathering, so every tool's selection goes through the soft
  shape (3.33.0: `buildSoft`, `SoftMask`, core `keepMasked` - nothing new in the core). The grid is widened by the feather; after the boxes and strokes are
  painted, the squared distance to the selection in feathers (time and pitch each in their own: (dt / featherT)^2 and (dl / featherP)^2 per cell) by two 1-D
  lower-envelope passes (`distance1d`, Felzenszwalb - Huttenlocher; an axis without feather is not passed, so the distance across it stays "none"); the value
  is 1 inside, 0.5 + 0.5 cos (pi d) out to one feather. The parts are the 4-connected pieces of the soft values (so feathered boxes that meet are one part),
  their masks the soft values. While a box is dragged (selectionChanged without 'tell') the shape is not made again (`softStale`): it shows as a plain box until
  let go. paintSoft: the selection's edge solid, the feathering shaded by its value, its outer edge dotted; drawn at full opacity (a JUCE image takes the
  opacity of the colour last set - the shape had been drawn faint after a translucent grid line).
- A box round a click (findsLost on the hard shape, no brush strokes) is mended from `hardParts()`, exactly as before, feathering or not.
- FeatherPanel (a CallOutBox from SelectToolsBar's Feather button): in time 0 - 1000 ms, in pitch 0 - 24 semitones, Off. "featherMs" / "featherSt" in the settings.
- Checked on Linux in Noise mode (the drawing; the fixes through the same masks as the brush, checked in 3.33.0 and by core_test).

## 3.35.0: Glue (the snip taken away), a click clears the ReTouch selection, the ReTouch window shared with fermata:Live's Fixer (built in the fermata:Live conversation, 5 November 2026)
- Asked for (glue): "a way to sort of 'reverse' the cut. Like glue it back together as though it was never there ... the 'Trim Window' button changes to a T
  and a needle & thread and then we get a button for cut or split ... (the f1 action) and then glue ... another f key ... like a superglue pot ... the user
  clicks on the glue button to go into glue mode and then they can click on an sn ... point and it glues it back together. They then click the glue to come
  out of glue mode. Or they can right click on an sn fade and click 'remove snip'." F3 was chosen for it: F1 is the snip, F2 the Trim window's Audition, and
  F3 means nothing anywhere else in fermata.
- Shared: `EditDef::glueSnip (k, why)` (src/core/Edit.h). Refused unless join k is a snip (both flags), neither side waits for processed audio, and both
  play the same files (file, track, fileStart per file; rate; take length). The glued piece is the first one with srcOut = the second's srcOut: it plays the
  take on from its own start, so a moment rolled out at the snip (the Trim window's slipIn / slipOut move one side's audio under the join) comes back, or one
  doubled goes. It keeps the first one's start, fade-in, snipIn / fixIn, normaliseIn, and the second one's fade-out (its curve), snipOut / fixOut, barOut.
  Volume changes: the first one's before `cut` (= where the second one's audio began, b.srcIn - a.srcIn), then the second one's moved by `cut`; the step
  that splitRegion put at the second one's start (the level the first had reached, no ramp, no group) is dropped when it changes nothing; the autoNorm
  glide is left to refreshNormalised. The pieces after it move by (glued end - old second end), so the next join keeps its crossfade exactly; overdubs,
  markers and fixed automation stay put (the Trim window's slips do not move them either: this undoes a slip). Points pinned to the second piece are
  pinned to the glued one (resolveAutomation puts them on their audio). insertIndex follows. Undo: Project::changed() records it like any edit change.
  core_test: glued at once = the edit before the snip (ids, source range, fades, volume change); with a quarter of a second rolled out = the moment back,
  the next piece and its crossfade back in place, the volume change on its audio, a pinned point on its audio; refused across an offline fix; undo.
- The edit window (EditWindow.cpp): `snipButton` (Icon::Snip, = F1) and `glueButton` (Icon::Glue, lit in glue mode) beside the Trim button;
  `EditWindowComponent::setGlueMode` (F3, Esc leaves) and `glueSnip` (stops this edit's playback only when the timing changes; the notice says how much moved).
  EditTimeline: `glueMode`, `snipNear` (the flag or line within 10 px, or a point inside the snip's crossfade), the glue pot as the cursor (drawIcon into an
  image, hot spot on the nozzle), the snips drawn in yellow in glue mode (the one under the pointer brighter, its crossfade shaded); `showSnipMenu` on a
  right-click on a snip's flag (Remove snip, Open in the Trim window) and "Remove snip" first in the piece menu on a snip's crossfade. HotKeysWindow: F3, Esc.
- Shared: Uikit.h - Icon::Snip (scissors), Icon::Glue (a pot tilted 45 degrees, nozzle to the top left), added at the end of the enum; Icon::Trim is a T with
  a needle and thread (its scissors went to Snip). fermata:Live draws none of the three.
- Asked for (the click): "when I click and it moves the playhead, I ALSO want it to remove the selection box". SpectrogramView::clickPlaces: a click without a
  drag in the picture - on nothing, inside a box (move), on an edge (resize), with the brush - places the playhead and, when plain (no Shift / Alt), clears
  the selection (boxes, strokes, harmonics: clearSelection). Shift / Alt clicks keep it; the time ruler's click only places the playhead, as before.

### The ReTouch window shared with fermata:Live's Fixer
- Asked for: "it absolutely should be the same tool as fermata ... it will only be available to the fixer and not the primary". So the window was moved, not
  copied (COMPATIBILITY.md rule 1): `src/ui/SpectralWindow.h` holds everything from SpectroSettings to RepairDialog and SpectroSettingsDialog, with RepairOp,
  RepairContext, JobDialogBase, `uiPrefs()` and `formats()`. It includes only the shared look (`Uikit.h`, `TimeReadout.h`) and src/core, so the check lets
  fermata:Live include it. FixToolsDialogs.h keeps the pitch dialogs; FixToolsCommon.h includes the new file.
- The two things the window needed from fermata's windows are hooks now: `openDialogHook()` (how it opens its spectrogram settings; fermata sets it to
  launchDialog, else a plain DialogWindow) and `closeDialogHook()` (how it closes itself; fermata's closes an OUT / IN STREAM window's pair). fermata sets both
  in FixToolsCommon.h (`fermataDialogsHooked`, `streamCloseHooked`), so fermata behaves exactly as in 3.34.0.
- RepairContext gained `acceptText` / `acceptTip` (the keep button's name; "" = "Write back to file") and `mayAccept` (asked before keeping; "" = go ahead,
  else the reason is shown and the window stays open). The window's texts name the keep button as it is named. Rename / Forget of a consonant are greyed
  out when the program gives no way to do them, and the Consonants tab says copying is done in fermata when it gives no `keepConsonant`.
- Shared: `FixVersion` (src/core/Takes.h): `takeId`, `flagId` and `replaces` start as `juce::Uuid::null()`. A juce::Uuid made with no value is a new random id,
  so a version that answered no flag (or was a first attempt) was put into the project by fermata:Live's Primary - and made again on every load - pointing at
  a flag and an earlier attempt that never existed. Found while trying the Fixer's ReTouch window. compat_test: a version with no flag and no earlier attempt
  keeps none through a save and a load.
- Checked: the window-code compile of both programs; fermata:Live linked on Linux with the shared core and the shared look only (nothing else from src/ui).

## 3.35.1: RX's Overwrite Original File; files coming back together; more times on the ruler (built in the fermata:Live conversation, 5 November 2026)
- Asked for: "when RX does a 'Save' it actually saves the project files and NOT the audio file ... 'Overwrite Original File' (CTRL+ALT+S) ... Can you make
  it so THAT is the trigger". It already was: the Watcher (RxLink.cpp) looks at the sent WAVs themselves (RxSaveWatch: a newer time, the same twice
  running); RX's Save writes an RX document and leaves them alone. What was wrong was `howToSave` ("press Save (Ctrl+S) on each tab"): now File > Overwrite
  Original File (Ctrl+Alt+S), and `howToSaveShort` for the status line. explainOnce's key for RX gained "_overwrite", so the corrected explanation shows once.
- Shared: RxSaveWatch::ready - when ready files meet another watched file that is still being written (it changed since the last look, or is missing for a
  moment while it is replaced), they wait (at most maxHold = 30 looks), so Composite View's overwrite of every file in a row comes in as one bringIn - one
  undo step, one notice. core_test: three files written in turn come back together; one that keeps changing holds the rest for maxHold looks only.
- Asked for: "the timeline timecode has become so spaced out ... please bring back some of the numbers. Unless it's acting differently on different
  resolution screens?" Not the screen: timefmt::rulerSteps went from 10 s to 60 s labels at 8 px/s (at 4.65 px/s - the user's 6 1/2 minutes across a
  wide window - 280 px apart), and from 5 to 10 minutes. Now (shared, src/core/TimeFormat.h) below 60 px/s the shortest of 2, 5, 10, 15, 30 s, 1, 2, 5, 10,
  15, 30 min, 1, 2, 3, 6, 12 h, a day with labels >= 64 px apart (kRulerLabelGap); the faint lines the finest step (or 1 s) dividing it, >= 10 px apart.
  Logical pixels, which the desktop scale factor scales with the text. core_test: >= 64 px, never wider than needed, the minors divide.
- And in the edit window the times were under the edit points' number badges (the same rows of the ruler: labels kRulerTop + 2 .. 18, badges kRulerH - 16
  .. - 1), so with edits close together most were hidden: the ruler is 10 px taller (kRulerH = 32 + kRulerTop) and the times sit on its top line.

## 3.36.0: Consonants in Noise mode (built in the fermata:Live conversation, 5 November 2026)
- Asked for: "the 'Consonants' functionality in the 'Noise' mode from ReTouch ... I usually do my consonant tidying on the final pass which is when I use the noise
  mode". NoiseModeDialog's fifth tab (kTabConsonant), the ReTouch window's controls and wording (SpectralWindow.h's RepairDialog: copy, copy and remove, the kept
  list, Level, Nudge, Paste), worked against Noise mode's own fixes: each paste is a Step (Step::paste, Step::pasteOp) put into the edit by runFix like the
  others; a nudge or a level change takes it off (takeOffLast - Undo without the picture's refresh) and puts it in again, so it stays one step; nudges pressed
  while it is being made (pasteRunning, or the picture after it) wait in pendingNudge / pendingLevel and follow when the picture is done (followPending, from
  refreshPicture's end). The list and its files: a RepairContext of its own given wireConsonants, so it is the ReTouch windows' list.
- Copy reads every track's channels round the box (1 s plus twice the box each side) as the edit plays them (readTrackChannels through the pieces' fades -
  this window's fixes are in the edit already), finds the background side on the tracks' mixes (SpectralRepair::consonantSides) and keeps
  consonants::extract's difference. The spec is consonants::copySpec of the ReTouch tab's settings (the box's extent and pitch range).
- prepareRepairJobs (FixToolsCommon.h; only Noise mode uses it): a Paste op is given to each piece it reaches with its own start (unclipped - the core adds
  only the overlap), as the edit's ReTouch window always did. It used to be clipped to the piece like a box, which would have started the consonant again at
  an edit point.
- Keys: C (the tab), P (paste), comma / full stop and < / > (nudge 1 / 5 ms, only while the last fix is a paste). HotKeysWindow: the Noise mode line.
- Checked on Linux (the Prelude test edit, a note's attack as the "consonant"): Copy (kept "t", 349 ms), Paste at the ruler's playhead, five 1 ms nudges, a
  5 ms one and one back (+10 ms, still one fix), Undo; a paste across the edit point at 20 s (each piece its part, the lines unbroken); Copy and remove
  (kept "moved", taken out, two fixes).

### 3.36.0: the next page's sharp picture, made ahead while Noise mode plays
- Asked for: "it seems that fermata waits until the page turns to generate the next view - for about 1.5 seconds I get the low resolution view before the high
  resolution one appears ... as soon as it has rendered the page it is looking at, to get it to begin rendering the next chunk".
- SpectrogramView (shared, SpectralWindow.h): requestForPage (t0) - the PictureRequest that requestDetailNow would make for the page from t0 at this page's
  width (clamped as setWindow clamps it), size, axis and settings; takeReadyDetail (picture, options) - shows a picture made ahead if it fits the view exactly
  (bumps detailSeq so a render still running for the page is ignored, stops the debounce); detailShowing() - the paint's own test of whether the sharp picture
  covers the view. Nothing else uses them; the Fixer's ReTouch window is unchanged.
- NoiseModeDialog: renderSharp (the old startDetail thread body) and sharpAllowed (kDetailMaxSeconds and the 512 MB guard) are shared by startDetail and
  prepareNextPage. prepareNextPage runs on every tick while playing (not Loop, not the last page) and starts one render at a time (aheadBusy, aheadCancel) of
  requestForPage (visT1) once the page in view has its own (view.detailShowing()) and no fix or picture refresh is running; one for a page no longer next is
  called off. aheadDone (message thread, with aheadBusy cleared there so the next is not begun before this one is in place) keeps it if it is still for aheadRq
  and the same audio (audioGen, bumped with audioChanged() by runFix's completion and takeOffLast - Undo and the paste's take-off). At the turn, tick() calls
  useNextPage: takeReadyDetail, or, if the one for this page is still being made, adoptAhead - startDetail then waits for it instead of starting the same
  picture again, and aheadDone shows it (or asks the view for its own if it came to nothing).
- Checked on Linux with a test-only copy of the dialog that pretends to play (there is no audio device on the test display) and logs each step: the turn
  used the picture made ahead every time (19 s pages, about 8 s per sharp picture in the test machine's unoptimised build); a fix while playing threw the one being made away and
  made it again after the page's own; a zoom while playing called it off; a turn onto a page whose picture was half made showed it 3 s later instead of
  starting again.

## 3.37.0: the fast FFT, every core, and the sharp picture in the ReHarmoniSer (built in the fermata:Live conversation, 5 November 2026)
- Asked for: "do 1 and 3 - and try to improve the reharmonizer window" (1 = a faster FFT, 3 = lift the 8-core limit), with "I am usually loading about 10
  seconds (or less) of music into the reharmonizer window ... it would VERY rarely be above 30 seconds". And, for the next zip, option 2 of the warnings:
  turn the "missing initializer" warning off in the Linux check rather than edit the program lines.
- `dsp::FastFFT` (Dsp.h; body in DenoiseFft.cpp, the one translation unit that compiles pocketfft in the shared core - so no build list changed): the same
  interface as `dsp::FFT` (forward, inverse scaled by 1/n), a `pocketfft::detail::pocketfft_c<double>` plan made once in the constructor (the high-level
  `pocketfft::c2c` with POCKETFFT_CACHE_SIZE 0 makes a new plan at every call), `exec` on the vector's data as `cmplx<double>` (the same layout as
  std::complex). exec is const and takes its own scratch, so one FastFFT is shared by the picture's threads. Used by SpectralPicture (render), SpectralPyramid
  (addTrack) and SpectralRepair (its eight FFTs). `dsp::FFT` stays for the rest (PitchShift, TakeMatch, LowCut's design, WaveColor) - unchanged results there.
- Measured with an optimised bench (-O2, one thread; fftbench in the scratchpad): sharp picture 19.2 s at 1042 x 535, 96 kHz, one track 4.12 -> 0.90 s, four
  tracks (two threads) 8.36 -> 1.97 s, 2 s page 0.91 -> 0.22 s; quick picture 22 s 0.32 -> 0.18 s; Attenuate 1 s box 1-6 kHz 0.17 -> 0.05 s; boxGain 0.05 ->
  0.01 s. Compared: Attenuate and boxGain outputs identical sample for sample (float32), the pyramid's bytes identical, the sharp pictures within 1.5e-5 dB.
- Threads: picture::render uses `workers::threadsFor (tracks)` (was min (8, hardware)); the ReHarmoniSer's `parallelFor` takes its most from
  `rehar::setParallelThreads`, which the editor sets to `workers::threadsFor (1 << 20)` at every job (startJob) - the engine stays JUCE-free and its own
  tests keep the old "up to 8". No parallelFor runs inside another (checked by scanning the bodies and the functions they call). tools/reharmoniser-check.sh
  (golden made with make_golden.py): output identical to the old code's, line for line.
- PictureOptions: `sumTracks` (the tracks' energy added, as the ReHarmoniSer's own picture adds its channels by power) and `trackGain` (linear per track,
  squared onto its power; 0 = skipped). `picture::windowSigma` / `picture::fftSize` split out of render (same values) so the ReHarmoniSer knows the bin spacing.
- ReHarmoniSer: Spectrogram keeps its decimated channels (`low`, at sr2, at most 24 kHz - the view tops at MIDI 108, 4.2 kHz) and decimates them with
  parallelFor. The View: `keyNow` (view times and MIDI range, A4, size, Detail, the See gains, the Spectrogram) - when the sharp picture's key is the view's,
  rebuildImage draws it on the quick picture's scale (0..255 over refDb-70 .. refDb, then contrast / gain / LUT as before); otherwise the quick picture, and
  the request after 180 ms (sharpTimer; startSharp: a thread, picture::render of `spec->low` with an even-semitone axis (warp 1) from midiHz (vm0) to
  midiHz (vm1), sumTracks and the See gains, detail 0 / 0.5 / 1 for Pitch / Balanced / Time, cancelled by the next). softenSharp then spreads the power
  over sigma 1 row (0.6 px across), and low down over 0.4 x the rows between two bins (up to 3.5): a reassigned impulse keeps each bin's own pitch, so with
  8-9 rows between bins near A2 an attack was a column of dots. The level: the quick picture's p99.5 was about 4 dB under the sharp one's before the
  softening (a test of four voices on four mics), which the softening takes back, so no offset is added (kSharpLift 0).
- Checked on Linux (the Prelude test edit, marks at 30 and 40 s, H: the two stream windows): the sharp picture replaces the quick one; lines crisp, attacks
  continuous after softenSharp; the ReHarmoniSer window code compiles clean under -Wall -Wextra (it is not in syntax-check-ui's list).

## 3.38.0: the sharp picture made in parts - round a fix, and the strip a drag uncovers (built in the fermata:Live conversation, 5 November 2026)
- Asked for: "if I make a 1/4 second adjustment to some audio, in a window that spans 10 seconds, it seems like the spectral viewer rewrites the entire
  window ... if I drag forwards 10 seconds ... surely it could just move what was the second 10 seconds to become the new first 10 seconds and just redraw
  the new second half?" - then "just start looking into and building those resource and loading time ideas".
- picture::render (SpectralPicture.h) takes a `RenderPart` (firstSample: the timeline sample of the tracks' first sample; c0, c1: the columns to make).
  The columns are whole pixels of the timeline from time 0 (`firstColumn (t0, t1, W)` = llround (t0 * W / span); a point's column is
  floor (timeline sample * W / (span * sr)) - firstColumn) and the frames every `hop` samples from time 0 (they were from t0). A part takes the frames
  whose points can land in its columns (a point moves at most half a window: frame centres within the columns' samples +- N/2 + 2), in the same order
  as the whole picture, so its columns are bit-identical; a shifted view (same span and size) shares every common column exactly. Checked (fftbench/
  parts.cpp): a part round a 0.25 s change = the whole picture's columns, 0 differing pixels; views 300 and 300.4 pixels apart: shared columns 0 differing;
  the uncovered strip alone = the moved whole picture; tracks read from a stretch with its firstSample = from sample 0. Times there (-O2, 3 tracks,
  1042 x 535, 10 s at 96 kHz): whole 2.95 s, the fix's part (103 columns) 0.40 s, a 301-column strip 0.94 s. `picture::reach` = N / sr + 2 px: how far
  a change reaches (a frame within N/2 hears it and moves its points up to N/2 further). DetailPicture has c0 / c1 (`whole()`); the rest is -200 dB.
  Asked for whole, a picture now also takes the frames just outside the view whose points land inside it (they were missing at its edges).
- SpectrogramView (SpectralWindow.h): `staleC0 / staleC1` (columns of the sharp picture out of date: the quick picture paints there), `detailGen` /
  `reqGen` (bumped by every change of the sound or the view; setDetail takes a picture only if nothing changed since it was asked for - this also mends an
  old race: a picture finishing after a fix went in, with the sound before it, was taken and its stale flag cleared). `invalidateDetailRange (a, b)`:
  the columns a change reaches become stale (outside the view: nothing); `setData (..., changedFrom, changedTo)` does that instead of throwing the picture
  away when the duration is the same. requestDetailNow asks for only the stale columns (PictureRequest c0 / c1) when the picture is the view's;
  setDetail copies a part in (and clears the stale range if it covered it). setWindow -> `carryDetail`: the same span, |k| < W columns
  (firstColumn new - old): the picture moves k columns (std::copy / copy_backward per row), the uncovered strip is stale, detail.t0/t1 are the view's.
  detailShowing() also needs no stale columns (Noise mode's next page waits for the page in view to be complete); takeReadyDetail clears them.
- The ReTouch window (RepairDialog): Step keeps from / to (the ops' span +- 0.05 s, seconds); a fix, Undo and Compare pass it to setData. If the picture
  showed Compare's "before" when a fix went in, it changed where the last fix was too: anywhere. (The first fix of a window used to be treated as that:
  `showingAfter` starts false - "shown is current" is showingAfter || history.empty().) Its startDetail passes c0 / c1 to render.
- Noise mode: renderSharp reads only the stretch the columns need (the columns' time +- reach + 4 px + 0.05 s) and passes firstSample; refreshPicture's
  end calls invalidateDetailRange (t0 - 0.05, t1 + 0.05) (the fix's span: its boxes with their feathering, and the cross-fades). audioChanged (t0, t1):
  a next page the change does not reach (t1 + reach < its t0, or t0 - reach > its t1) is kept - aheadGen / aheadMaking move to the new audioGen, so one being
  made carries on; aheadDone now checks aheadMaking == audioGen (the thread's own count went).
- ReHarmoniSer View: `sharpRaw` (before softenSharp) beside `sharp`; rebuildImage carries both along a move (`sameButTime`: everything but the time;
  carrySharp shifts by firstColumn's difference, the strip stale) and paints the quick picture in the stale columns; startSharp then makes only the strip
  (RenderPart c0 / c1) on a copy of sharpRaw, and softens the whole of it again.
- Checked on Linux (temporary logging, taken out): Noise mode - a ReTouch box re-made [359, 531) of 1042 columns in 1.4 s (the whole page 7.7 s, the
  unoptimised build), a ruler drag of 270 columns made [772, 1042) in 2.1 s, the join invisible; the ReTouch window - the first fix, Undo, Show before and
  Show fixed each re-made only [282, 499) of 926; drags both ways [656, 926) and [0, 400); the ReHarmoniSer - Shift+wheel made [518, 574) of 574 in 0.3 s
  (whole 1.9 s); Noise mode playing (the pretend-play copy) - a fix at 32-34 s kept the next page (49.6-68.8 s), which the turn then used.

## 3.38.1: Noise mode asks before a whole-piece De-click (built in the fermata:Live conversation, 5 November 2026)
- Asked for: "In noise mode with the whole track loaded, if I click d by accident it does a declick of the whole piece on every channel and takes absolutely
  ages to process ... (b, better I think) if the user makes a process over the entire piece (ie with no selection made) then there is a splash screen to ask
  the user 'do you really want to make this process on the whole piece on all (insert number of audio tracks) audio tracks?'". Then: "can we have the
  positive action option on the left and the cancel option on the right. In general, that is how I prefer the options to exist."
- Only De-click runs with no selection in Noise mode (ReTouch, Gain and Denoise need one). retouch (declick, wholeAsked): no selection and not yet asked ->
  askDeclickWhole(): a juce::AlertWindow (warning icon) with the length (m:ss, or h:mm:ss) and cur->parts.size() tracks; buttons "De-click the whole piece"
  (1, left) and "Cancel" (0, right, with Escape AND Return as its keys); every child's setWantsKeyboardFocus (false), so a Space pressed by habit presses
  nothing; the callback runs retouch (true, true). Checked on Linux: D -> the box; Return, Escape, Space then Escape: nothing done ("0 fix(es)"); the
  button: the whole piece de-clicked (1 fix); a box then D: no question, de-clicked (2 fixes).
- The other question boxes were checked for the order asked for: all already have the action first and Cancel last (confirmAsync, the AlertWindows,
  fermata:Live's close box).
- Also found while looking at the user's pictures (no change made, at the user's wish): Click sensitivity 10 sets the time-domain finder's threshold to 3
  sigma of the prediction residual, which plain noise crosses a few hundred times a second; on a test of church-like rumble with two soft high clicks
  (fftbench/declick.cpp) 10 mended 211 bits of noise as well as the two clicks, and 3 of those mends left a click of their own; 9 mended 8 and added none;
  2 to 8 mended just the two (found by the picture check, findLines). The user will keep it lower. (If it is ever wanted: undo any mend that leaves more
  sound above about 2 kHz round it than there was, and map 10 to about 5 sigma.)


## 3.39.0: surround Int and Ext buses (built in the fermata:Live conversation, 5-6 November 2026; in two parts)
- The whole plan, what the manuals require, what is built and the work list still to do are in `docs/SURROUND.md`. Part 1 (the formats, the panner,
  the fold-downs, the project model) and Part 3 (the mixer, the Project Designer, the bounce window) were built first, on the `surround` branch;
  Part 2 (the engine, the plug-in host's layouts and play positions, multichannel bounce files, the Atmos pass) is to be written with Claude Fable,
  as the user asked ("PLEASE tell me when there is a bit that would really benefit from using Fable").
- The 3.39.0 history entry describes the finished feature: Part 2 must make it true (or change it).
- `InsertProcessor::prepare`'s third argument was always 2 at every call; it now means the layout (1 mono, 2 stereo, else a BusFormat value), and
  InsertSlot owns it (`setLayout`), so none of the 48 call sites had to change.
- Checked on Linux (the test build under Xvfb): a 9.1.6 and a 7.1.4 Int Bus and a 9.1.6 + headphones Ext Bus in the processing mixer - the format
  in the coloured bar, the wider strips with a meter per channel (front / surrounds / wides / tops), Main out greyed on the surround Ext Bus, the send
  panel's panner (follow pan on: the piano's centre pan lights C; unticked and dragged back with 77 % height: Ltr lit, a little Lrs), the output list
  showing each bus's format, the round panner button on the piano's output once it goes to the 9.1.6 bus. 
- Part 2 (the engine, the plug-in host, the bounce, the Atmos pass) was built the same night with Claude Fable: `docs/SURROUND.md` lists what, and
  the decisions. The engine's stereo path is untouched line for line where it could be (the whole test suite pins it); the surround path branches off
  it at the node's width. The channel order the Dolby Atmos Composer gets on its 18-channel bus (by position) is the one thing only the user's
  machine can confirm: `docs/SURROUND.md`, "Decisions worth knowing", says what to change if it is wrong.

## 3.40.0: the post-fader slot, and fermata's own Dolby Atmos export (built in the fermata:Live conversation, 6 November 2026)
- The user asked for a post-fader slot "that literally goes under the fader but before the fader on button", switched on and off in the Project
  menu: one more insert slot on every audio track and bus, in a row between the fader and the OUT / Main out row, shown while
  `Project::postFaderSlots` is on. Built with Claude Opus.
- The slot is the last element of the strip's and the bus's `slots[]` (`kPostSlot`; the arrays are `kChanSlots` long), so preparing, re-preparing,
  closing windows, the bounce's offline flag, the render copies, the slot layouts and the Atmos-link checks all reach it with the others. Code that
  means the slots before the fader counts to `kNumSlots`: the processing loops, the visible slot rows, the "slots" list in the project file. Every
  `kNumSlots` left in the code was looked at.
- In the engine the slot runs on `post` right after the fader and the mute (a Dolby Atmos Beam there goes quiet with its channel, and follows the
  fader's automation). `Node::takesPost` is the one rule for what takes its output: a post-fader send that follows mute, and of a track only a stereo
  one (Direct) - a track's send into a surround bus leaves from its own channels before the stereo fold the post slot works on. A bus's Pan / Fold /
  Across post-fader sends read `post` (with the switch off they are exactly as before). The delay compensation follows the same rule (`sendLat`,
  `postOutLat` for an Ext Bus's outputs and the post-fader taps); the slot's posLat is the lateness after the inserts.
- The project file: "postFaderSlots" (only when on) and "postSlot" (a list of one, only when the slot holds something). An older fermata reads the
  "slots" list it always read; a project that does not use the slot is written byte for byte as before (core_test checks).
- fermata's own Dolby Atmos (ADM BWF) export (built with Claude Fable the same morning): the user's decisions, the rules from Dolby's two documents
  and the design are in `docs/ATMOS-EXPORT.md`. The writer (`src/core/AdmWriter`) was checked line for line against a master that Dolby's tools
  wrote and Apple Music released (the user's own, trimmed): the bed part of fermata's axml is identical to it apart from the names; the chunk order
  (JUNK, fmt, data, axml, chna, dbmd), the 40-byte chna entries and the ID numbering follow it. Two things the brief had guessed wrong, found by
  the reference and the profile: a 9.1.6 bus is a **7.1.2** bed + 6 objects (the largest bed set it holds whole, as the Composer builds a
  Composite), and a stream's / track's ID carries its channel's number per type (AS_00011003 for AC_00011003), not one counter across both.
  The export reuses the Mastering window's render batch (`MasterRenderBatch`, with `keepSurroundStems`): each piece's ticked buses are rendered at
  full width into 32-bit float files over the Virtual Master's stretch, then interleaved into the ADM file with the LFE low-pass and dither.
- Fable's look at the post-fader slot: the one change made is that which sends take the channel's signal after the post slot (`Node::takesPost`) is
  now decided when the plan is built and kept in `SendPlan::viaPost`, so the audio thread and the delay lines always agree (it was read from the
  live PRE / follow-mute flags on every block, which could disagree with the delay line for up to a third of a second after a flip). Everything else
  - the model, the saving, the engine path, the window - reads right; the tests pin it.

## 3.40.1: the Fable sweep of 3.40.0 (built in the fermata:Live conversation, 6 November 2026)
- Read in full: the engine diff (the post-fader slot's plan building, the audio-thread path, the latency signature, carryOver), the project file
  and snapshot code, the mixer window's post row and slot columns, the automation's slot range, the render fingerprint, the AdmWriter, the Atmos
  job and the Mastering window's tab. Three fixes (CHANGELOG 3.40.1). Confirmed sound, with reasons noted in the code where it was not obvious: a bus's
  Pan / Fold / Across post-fader send reading `post` with unity ramps equals reading `pre` with the fader ramp (`post` = `pre` * the same ramp); the
  wells are cleared before the post row's well is added; a VCA strip keeps the post row's room empty so the faders line up; drag and drop between a
  POST cell and an insert cell goes through each column's own base pointer; the Atmos job's readers are closed before its work folder is deleted;
  the Composer in a post-fader slot counts for keepRunning only while the slot runs.

## 3.41.0: the heard-take layer in the ReHarmoniSer's stream windows, and the guide that stays put (6 November 2026)
- **The green layer.** `EditorHost::heardGain (sec)` - set by `openReharmoniser` (FixTools.cpp) for a stream window only (`ss.on`) - is the sum of
  `EditRegion::gainAt` over that stream's own pieces (pieces of one take joined inside the stream add up as their crossfade does; capped at 1). The
  stream's audio is loaded without fades (its take as it is in the files, the OUT stream carried on past the edit point, the IN stream started
  before it), so the layer is what tells the two apart. `View::paint` draws it after the picture and before the note lines and frames: one value
  per pixel column (at the column's centre), quantised to 48 steps and filled in runs, green 0xff2fd36a at 0.24 x the gain. It does not follow the
  playhead (the user's choice: it shows what will be heard, before you press play too). Checked on the Linux build with the Prelude test edit (the
  marks 0:45 - 0:54 over one join): the OUT window green up to the join and none after it, the IN window none before and green after, the edge where
  the crossfade is (30 ms there, so a short soft edge when zoomed in). Not added to the ReTouch stream windows (not asked; they could have it).
- **The guide.** `EditDef::rippleAfter` moved the guide when its start was at or after the place of the change. `insertRegion` adds at the end with
  `rippleAfter (end, length)`, so a guide at the end - and, from the first piece added to an empty edit, one at 0:00 (the first piece goes at 0 too) -
  was pushed along by every piece added after it. The user: it should stay where it is until moved by hand. So `rippleAfter` never moves it (and so
  `anchorStart`, which ripples everything, does not either); only the drag and the menu do. Test: core_test "3.41.0 the visual guide stays where it
  was put".

## 3.42.0: edit versions - Edit 1, Edit 2, Edit 3 (7-8 November 2026, with Claude Fable)

**The model** (`src/core/Edit.h`): `EditVersion { number, subheading, frozenAt, laneShown, shared_ptr<EditDef> edit, var mixer }` and, in `EditDef`,
`versions` (Edit 1 first), `subheading` and `compareWith` (the changes lane's choice; 0 = closed). The live edit is version `versions.size() + 1`;
`versionTitle (n)` is "Name - Edit n - subheading". A frozen version's edit is a whole `EditDef` copied at the freeze, with its own `versions` empty:
that keeps the reader simple (`editFromVarIn` in `Project.cpp` reads a version's edit with the same code as any edit) and means everything that
walks an edit's files - `forEachAudioFile`, `renameTake`, `relinkAudio` - visits the frozen pieces too (`withVersions (e, f)`), so a relink or a
take renamed keeps them playable. `shared_ptr` because `EditDef` is copied by value all over (undo snapshots, the trim window) and the frozen
copies are big; a copy of the live edit shares them, which is right: versions are not undone.

**Not undone, but saved.** Like the comments and markers: `versions`, `subheading` and `compareWith` are in `kEditViewKeys` (stripped from the undo
text), the undo restore copies them from the live edit, and `undoText` leaves them out (`editToVar (e, false)`) so the history is not six edits
long per step. `saveHash` covers them (a frozen version's comments change: ticked, deleted, imported), so a freeze or an import gets the edit's
file written.

**Freezing** (`editversions::freeze`): the copy goes into `versions` with the comments that were on the live edit (they were about that version),
the live edit's comments and subheading are cleared, and the edit window sets `compareWith` to the new number so the changes lane shows what has
been done since. Mastering offers it in the done box of the listening copies (`MasteringComponent::showDoneBox` / `freezeExported`), only for
pieces whose live number is still the one exported (frozen already from the edit window: left alone). `unfreezeLast` undoes the last freeze and
brings its comments back, mapped.

**Where a comment is now** (`mapSeconds (from, to, seconds)`): the piece of `from` at that time, its take and the place in it; the same piece in
`to` (by id), else any piece of that take holding that place (the nearest to where it was); in a gap or past the end, the distance from the piece
before is kept. Not found: the time is kept and the flag is drawn hollow (`floating`). The lanes map at paint time (cheap; the lists are short).

**What changed** (`editversions::compare (a, b)` - a the older): the main track is matched piece by piece - by id, then the same take's audio
re-cut (the biggest overlap of take positions) - and the longest run of matches in the same order is the alignment; a matched piece outside it
"moved here from ...", one change (`Kind::Moved`, with `bWas`: where it used to be on B's timeline), whose put-back takes it out where it is
and puts it in where it was. An aligned pair is compared (`pieceDifferences`: take in / out points, files, both fades, the volume changes, normalise) and the
gap before it against the previous aligned pair; the unaligned are "taken out" / "new", placed on the other timeline next to their aligned
neighbours. Changes whose spans touch on both timelines become one (a replaced passage is one flag, "cut new"). Overdubs by id; automation lanes
by track / parameter / mixer. Nothing is reported for pieces that only moved along because something before them changed length.

**Putting back** (`putBack (b, a, change)`): A's pieces inside the A span, trimmed to it (gains rebased), shifted to the B span's start; B cut at
the span's edges and what is inside taken out; everything from the span's end moved by the difference in length (`rippleAfter`, so overdubs,
markers, flags and fixed automation points follow, as they do when a piece is inserted - though not when one is deleted, an older asymmetry);
the pieces put in; at the two edge joins A's own fades when the neighbours are the same pieces
as in A, else the default crossfade; the joins inside came with the pieces. A piece whose id is still in B elsewhere comes in as a new piece.
Overdubs: put back as A had it (or taken away again); a lane: its points as A had them. The whole-version put-back (a tab's menu) copies the
pieces, overdubs, lanes and guide outright.

**The mixer** (`compareMix (a, b, nameOf)` / `patchedMix`): the two `Project::mixSnapshot` vars (with inserts) walked by strip (track id), bus (bus
id) and VCA (number), then every key the writers may leave out at its default (`valueOr` / `defaultFor`: inGainDb 0, pan 0, polarity off, low cut
off at 80 Hz ...), the sends by their bus, the plug-in slots by number (a slot is one change: added, taken out, another plug-in, bypass, or the
state blob differs - "settings changed"). A change knows its `path`; `patchedMix` clones the newer snapshot and sets that one leaf from the older,
and `applyMix` with the matching parts (inserts only for a plug-in change) puts it on the mixer. `MixChangesPanel.h` is the list; the METERS strip's
Changes... key opens it (an edit's own mixer only).

**The windows**: `VersionStrip` (`EditWindow.cpp`, between the buttons and the timeline, sharing its `xOf` and scroll) - the tabs, the subheading
box, Freeze, Changes..., the comments lanes and the changes lane with the compared version's pieces drawn above the flags. `AppContext::playEditVersion`
plays a frozen version (`segmentsForEdit` takes any `EditDef`; its automation plan is not followed, so the live lanes do not take over). The playhead
then runs on the frozen version's timeline, which may differ from the live one: said in the notice. Mastering always renders the live version;
to master an earlier one, put everything back from it first (or keep that for a later build: a version pick in the Mastering list).

**Checked**: the six checks; `tests/core_test.cpp` (freeze / save / load / undo, mapping, compare and put back on pieces, overdubs, lanes and a
shuffle, the mixer list and patch), `tests/compat_test.cpp` (the keys, contract 3). The Windows build and the windows themselves could not be
run here beyond the Linux build; see the delivery message.

## 3.43.0: mixer transport, send dials typed, +36 dB volume changes, the guide's own size, the ReHarmoniSer ruler (9 November 2026)

**Mixer transport** (`MixerComponent::transportKey`, `updateTransport`; `kTransportW`). Five `IconButton`s right of the name box. A key is pressed in
the window whose audio the mixer plays - an edit's mixer (`MixerState::editId`): the `EditWindowComponent` of that edit; the processing and cue mixers:
`takeWindowOfMixers` (the take window playing now, else the most recent take window - `ToolWindow::lastTransportWindowWhere` - else
`AppContext::recordTarget`, else the first) - found among the desktop's windows (`findWindowContent`, so a window down in the bar counts) and sent
`transportKey (AppContext::TransportKey)`, which both windows now have (it calls their own goTo / jumpBy / togglePlay / goToEnd, so loop, the guide
solo and the strip check behave as their own keys). With the window closed, `editTransportClosed` / `takeTransportClosed` do the same on the
project's playhead (EditDef::playheadSeconds, TakeWindowDef::setPlayhead) and AppContext::playEdit / playTakeWindow. Disabled while recording.
Play shows Stop while anything plays (as in the windows: pressing it stops whatever plays). The tooltips name the window (3 times a second).

**Send dials** (`SendDial`): `knob.onDoubleClick` opens `valueentry::ask` (the small box) - it was 0 dB, with Ctrl + double-click typing;
`setDoubleClickReturnValue (true, 0, ctrlModifier)` makes Ctrl + click 0 dB, and `Knob::mouseUp` no longer starts the panel's timer for a Ctrl + click.
The read-out under the dial opens the same box on a double-click (`ReadoutClick`, declared after the label).

**+36 dB** (`kVolumeChangeMaxDb` in `src/core/Edit.h`, shared): `VolumeChangePanel`'s faders use `volumeFaderRange()` - a `NormalisableRange` with its own
mapping: proportion 0..0.8 is the old -100..+12 dB curve (skew from -20 dB at the middle), 0.8..1 is +12..+36 linear - so the usual range keeps its
feel; its All fader is +/-36. `RangeVolumePanel`'s faders -100..+36 (0 in the middle), and a level after the change may reach `kMostKept` (+60 dB,
what `dbFromVar` keeps) so +36 is added in full on top of a raised level. `EditRegion::setPieceLevelDb` clamps at +36 (was +24). The trim window
widens a Level fader's range to show a piece beyond +/-12 (for the same piece it only widens; `levelRangeFor`), never during a drag.

**The guide's own size** (`EditTimeline`: `guideSelected`, `guideOwnH`, `guideWaveZoom`): a click in the guide's name column (`guideMouseDown`) selects it
(`selectGuide`), which adds a global mouse listener (`GuideDeselect`) that ends the selection on any click outside `guideNameBox()` (in screen
terms, so it also works with the sticky band scrolled down and from other windows). `EditWindowComponent::keyPressed` sends Up / Down and , / . to
`guideHeightBy` / `guideWaveBy` while `guideIsSelected()`. `guideH()` is `guideOwnH` once set (40 px up to half the visible height), else 40 - 90 px
from the rows as before; the guide's waveform uses `guideZoomNow()`. Per window, not saved (the tracks' heights are not saved either).

**ReHarmoniSer ruler** (`View`, `Mode::rulerPan`): zoomed in (`canPanTime`), a press on the ruler away from the playhead's triangle (`onCueHandle`,
7 px) pans: `vt0/vt1 = panT0/1 - dx / width * span`, clamped by `clampView` (the sharp picture carries its columns, as when following); a press
without moving puts the playhead on the mouse-up. The triangle, or the whole ruler with everything in view, is `Mode::ruler` as before. Cursors:
the hand / left-right arrows.


## 3.44.0: the De-Hummer (9 November 2026, with Claude Fable)

**What the user's hum is.** 17 example files (room pairs and twelve spot mics, 96 kHz, 5.2 s: a chord that dies away in 1.3 s, then room). Measured
with long frames (1.46 Hz per bin): the "hum around 1400 Hz, give or take 100" is two things - a *whistle* at 1439 - 1440 Hz, 25 - 30 dB above the room
in the room mics, wandering about a hertz; and a *buzz*: a 30.18 Hz fundamental (a motor at 1811 rpm, or a pump) with harmonics 7, 11, 12, 15, 16,
20, 21, 28 and 42 - 49 (211, 332, 362, 453, 483, 604, 634, 845 and 1268 - 1479 Hz), dead steady, each 15 - 28 dB above the room. The 211 Hz "hum" the
user heard is the buzz's 7th harmonic, 5 Hz under the chord's bass A (216 Hz, A = 432). Besides those: the mains' 100 Hz in every mic, a 48 Hz
tone, a 55.56 Hz-spaced comb at 15.0 - 15.8 kHz (-117 dBFS: a switching supply) and quieter rows at 5.4 - 7.1 kHz 50 Hz apart. The Python used to
measure all this (`analyse.py`, `steady.py`, `region.py`, `comb.py`, `proto2.py` - the finder's prototype, which the C++ follows) lived in the
conversation's scratch folder; the conclusions are here and in `src/core/DeHum.h`'s head comment, which is the reference.

**The finder** (`dehum::find`, `DeHum.h`). Frames of 2^n samples near 1.5 Hz per bin (65536 at 96 kHz), hop a quarter, at most 64 frames spread
evenly over the box, per channel (the picture's tracks' mono mixes in Spectral Repair; `readTrackMix` of each track's pieces in Noise mode). The
*steady spectrum* is each bin's 10th-percentile level over the frames (the lowest with fewer than 10): what is there all the time; the floor is the
running median over +-40 Hz; a tone is a local maximum 12 dB or more above the floor and at least -126 dBFS, *narrow* (falls 6 dB each side and is at
most 10 bins wide where it has fallen 20 dB or 70 % of its height: a very loud tone's Hann skirt runs on for many bins; a hum right beside a note
stops at the saddle between them and still counts), and *steady* when followed frame to frame (the peak within 2 bins: its pitch's 15th - 85th
percentile range, the *wander*, is 6 Hz or less, and its 80th-percentile level is within 12 dB of its 10th - a note sounding through some frames
lifts those only). Tones heard by several channels are merged (the channel that hears it best speaks for it). Grouping: *series* first - whole-number
multiples (up to the 60th) of one fundamental 20 Hz - 2 kHz, the fundamental fitted by least squares through the origin, members within
min(1.5, 0.5 + 0.001 f) Hz; a series needs 4 + the number chance would give (n x 2.4 / f0) members, three of them up to the 30th harmonic; the gaps
inside a run of heard harmonics, and one step beyond a run of three, are filled by faint peaks (7 dB) - then *rows* of 4 or more evenly spaced tones
(10 - 400 Hz) with one gap allowed, extended at the ends by faint peaks; the rest are single tones. Loudest first, the twelve loudest, those 15 dB or
more above the room ticked. Under 2048 samples: refused with a note. `measureAt` is the same analysis near a typed pitch (+-2 %, at least 5 Hz),
picking the nearest strong tone (a dB of height counts as a hertz of distance); nothing there: the typed pitch, `aboveDb` 0.

**The remover** (`dehum::remove`; `FixSpec::Kind::Dehum`, `spec.hum`): for each pitch of the ticked hums (each once, `Params::notches`), the sound over the
box plus 2 s each side is multiplied by a rotating phasor (renormalised every 1024 samples) that shifts the tone to 0 Hz; a 4th-order Butterworth
low-pass (two biquads, Q 0.541 and 1.307, transposed direct form II started at the steady state) is run forwards and backwards at half the notch
width (zero phase, flat to the cutoff, steep beyond it - a 2nd-order one left a wandering whistle 20 dB down at the notch's edges); shifted back up
and summed, that is the hums as they are in the sound; `fraction` x (a 50 ms fade at each edge of the box) of it is subtracted inside the box, and
nothing outside it. The notch is 1.5 - 4 Hz (Soft .. Hard, `Params::hardness` 0..1) plus twice the tone's wander (so a wandering whistle sits in the
flat middle); the fraction is 6 - 40 dB (`reductionDb`). The box is worked in one-minute stretches, each with 2 s of context either side, the
context BEFORE a stretch taken from a copy of the original (`keep`) - the stretch before has already been written over in place, and reading the
de-hummed sound there put a step into the filter and a 1e-3 click at every join (found by comparing against a single stretch: now 1.6e-8). In `applyFix` it is a per-channel case like Denoise, a Patch-length margin (8 s) is read
round it, and a soft (brush) mask applies to it as to a Patch. One op per stretch of time of the selection (`selectionSpans`, as De-click) unless the
selection is brushed (then `selectionParts` with masks). Cost: samples x tones x a few dozen flops - a 5 s box with 18 tones in 0.1 s; an hour's
Noise mode edit with a dozen tones is minutes, which is why it asks first with no box.

**The windows** (`HumPanel` in `src/ui/SpectralWindow.h`, used by `RepairDialog` and `NoiseModeDialog`): Find hums [H] and a summary line; the list
(tick, words - `Hum::text()` and `loudness()` - and a `TextEditor` with the pitch: Enter or leaving it calls `onRetune`, which the window answers with
`measureAt` on a job thread and `set`; a series' fundamental is taken as typed, since it is seldom there to measure); Add with its box; Soft <-> Hard
(the number shown is the dB that come off; remembered as `repair.humHard` / `noisemode.humHard`). The De-hum button sits in the action corner as
the other tabs' buttons do. The tab needs five rows, so the tool panel grows by two rows while it shows (the picture is that much shorter on that tab
only). The scan and the measurement run through `runJob` like a fix (the window's buttons are off meanwhile). Keys: H = the tab and Find; Harmonics
moved to Shift+H. fermata:Live's Fixer opens the same window, so it has the tab; and its fix list (0.7.0, `FixerWindow.cpp`) has a one-step De-hum:
`runFix` builds a `dehum::Source` over the chosen part from `readTakeSpan` (each track's channels averaged), runs `dehum::find` on the job thread, keeps
the hums it ticks (15 dB or more above the room), writes what it found into `what` (said when the try plays) and refuses with the finder's note when
nothing stands out; the slider is the hardness as dB off (6 - 40). Two seconds of context are read round the part for its filters.

**Tests.** `tests/core_test.cpp` "3.44.0 De-Hummer": a synthetic room (hiss at -100 dBFS, the 30.18 Hz buzz's 16 harmonics at -88 / -94 dBFS, a whistle
at 1439 Hz wandering +-1.5 Hz, the mains at 100 Hz, a chord at 216 + 432 Hz for 1.3 s). The finder lists exactly the series (all 16 harmonics), the
whistle (wander 1.5 - 5 Hz) and the mains, never the chord; a 0.1 s box is refused with a note; 1440 typed finds 1439, 5000 typed finds nothing and
is kept. Hard takes 25 dB or more off every harmonic (measured from 1.4 s, past the chord's tail), 30 off the mains, 25 off the whistle at its own
pitch in every quarter second; the chord's partials change under 0.3 dB; what comes out has the hums' own rms (9.7e-5), not the hiss's; nothing
outside the box changes; Soft takes 4.5 - 7.5 dB; an unticked hum stays; `applyFix` gives the same samples. On the real files (not in the repo): the
finder's lists are in the conversation; after De-hum nothing stands out where the hums were, and the music before the box is bit-identical.

## 3.47.0: the fourth sweep, areas D to I; Delete takes what lies over the music with it (10 November 2026, with Claude Fable)

`docs/SWEEP-4-FINDINGS.md` has every finding. D was read at the agreed scope; E to H "the shorter way" (what changed since 3.27.1 / 3.30.1 /
3.40.1 / 3.46.0); I is a scan, nothing removed. What a later change must not undo:
- **`EditDef::rippleAfter (from, delta)` is the one way the timeline closes or opens up** (`removeRegion` with ripple, `closeGap`, `glueSnip`):
  it moves the overdubs, the markers, the feedback flags, fixIn / fixOut, the playhead and the lanes' points fixed in time that lie at or
  after `from` (the callers move the regions); what lies before `from` - over a deleted piece itself, say - stays; fixIn / fixOut left the
  wrong way round are cleared. A new operation that shortens the timeline calls it with the OLD end of what went (`removeRegion` takes
  `end` before erasing; `glueSnip` takes `oldEnd` before the piece shrinks).
- **Volume changes are counted from their piece's start**: whatever moves a piece's start without moving its music (`anchorStart`,
  `trimEdgeToFade`'s reach-back) calls `rebaseGains (old, shift)`; never `g.at += ...` by hand.
- **Undo and the things undo does not own.** Markers, feedback, fixIn / fixOut, the playhead and the versions are not in an undo step
  (`UndoSnap` hashes the pieces only). So `restorePiece` copies the live ones into the restored edit - and since 3.47.0 PLACES each one:
  `UndoSnap::made` / `fresh` (`Project::UndoTimes`, from `timesOf`) remember where they stood when the state was made and at the last tick
  the edit was unchanged (`UndoPiece::before`: just before the change). One the change did not move, and that has not been moved since,
  stays exactly where it is; every other is carried through `editversions::mapSeconds (live, restored, t)` - it stays on its music.
  `undoTick` refreshes `fresh` only when the hash is unchanged. A test
  that calls `undoReset()` by hand must call `mastering.sync (edits)` first, or the first undo shows a phantom step.
- **`Project::changed()` commits the joins** (`commitAllJoins`): piece edges settle a few milliseconds. Tests compare places with a tolerance.
- **`trackHasAudio` counts the frozen versions** (`withVersions`): "is this track empty" must keep doing so.
- **The version strip asks `undoHash` of the two edits compared**, not `saveHash` (which writes every frozen mixer out as text).
- Bars from a project file are clamped to `kMaxBar` (99999) in `Project.cpp`'s reader.
- **Pieces that are slid, shuffled or re-ordered carry what lies over them: `editversions::Riders`** (decided 10 November 2026). The core
  operations (`slideRegion`, `shuffleRegion`, `moveRegion`, `slipIn`, `slipOut`, `trimEdgeToFade`) move pieces only; the WINDOW makes a
  `Riders` before and calls `carry` after. A drag makes one when it begins and carries from it at every mouse move (`dragRiders` in the edit
  window - for the slide and for the shuffle - `riders0` in the Trim window): carrying from the previous move would let a piece pick up a
  marker lying in a silence it slides across. The rule is `mapSeconds`' (`detail::placeOf`): same place in the same piece; in a silence or
  past the end, with the piece before; music gone, stay. A new way of moving pieces about must do the same - or use `rippleAfter`, if it
  puts music in or takes it out. Not carried: a piece's own volume changes when `slipIn` slides its audio (D3 in the findings).
- Decided, nothing to do: Move clips leaves the frozen versions' pieces on their track (D8); `trackHasAudio` keeps that track.
- Area I's unused functions are out (the list is in the findings): `AppContext::playDisc`, `discAlign` and `discToken` among them - the
  Mastering window plays its renders with `playRenders`; `discItems` / `DiscPlayItem` are still used (by the render list).

## 3.48.0: the Edit window's one line, the first automation point, typed values, rows round the clicked track (10 November 2026, with Claude Fable)

What a later change must not undo:
- **`g.setFont (14.0f)` sets a height, not a font**: JUCE keeps the style of whatever font the Graphics last had. In a paint that is not
  always run in full (the strip under the playhead, a clipped redraw) the "last font" differs from one redraw to the next, and text
  flickers between bold and ordinary. The track names of the Edit and take windows name their weight now (`FontOptions (h, bold)`). The
  other height-only calls left (`grep -n "setFont ([0-9]"`: the rulers, the Trim window's lane titles, `MeterScale`) are each the first
  text of their paint or follow the same call every time; a new one should name its font.
- **The Edit window's line**: `VersionStrip` still owns the tabs, the subheading, Freeze and Changes and its lanes below them, and is as
  wide as the window. `EditWindowComponent::resized` lays `infoLabel` and the lead-in box OVER the strip's top line (they are added after
  the strip, so they are in front) and tells the strip how much of the line's right end to leave (`rightReserve`); `keysRight()` is where
  the text starts, `keysNeed()` decides between one line and the old two. `resized()` is called on the strip by hand because its keys' room
  can change while its own size does not.
- **`EditTimeline::controlNow (lane, value)`** reads the control a lane drives. It finds it exactly as the player does
  (`automationPlanFor`, Playback.cpp) - the same mixer for a lane that names none, a VCA's fader only, an Ext Bus's fader and plug-ins only,
  a send that must exist, pan for mono tracks only. It is a second copy of those rules on purpose (the core has `planForOneLane` /
  `readControl`, private to Playback.cpp): using them would have changed shared code for a window's convenience. **If the player's rules
  change, change this with them.**
- **A lane's first point** (`automationMouseDown`, `l.pts.empty()`): value = `controlNow`, `autoDragHold` keeps the height for the rest of
  that click (`automationDrag`). `holdAutomatedControls` then records that same value as the control's "before", so deleting the point
  puts the control back where it was - nothing changed there.
- **`askPointValue`**: the answer comes later (an AlertWindow, or a menu for a stepped parameter): the point is found again by lane id and
  point id, and the edit by id, never by a pointer kept from the click. `parseDbText` holds what the lane can hold (below -59.5 dB is
  'off', as under the mouse). A plug-in's text goes through `PluginHost::valueOfText` (JUCE `getValueForText`: the plug-in's own
  `getParamValueByString`, else the number as 0 .. 1).
- **Rows round the clicked track**: `clickedRow` is a row number noted at every mouse-down below the pinned band (the event as it came,
  before `shifted`); `rowToKeepInTheMiddle()` must be read BEFORE the row height changes, the view moved after `updateSize()`. The guide
  lane's height follows the row height (40 - 90 px), so `bandH()` is read again after the change.

Asked for the same day and NOT built yet (the next build): the volume changes shown clearly. What was found:
- There are two kinds. "Change volume from here to next edit" is one `GainChange` in one piece: it ends at that piece's end and does not
  carry over the join. "Between the 1/2 marks" is a group (roles 1 / 2 / 3) with a fade in, a fade out, and it does cross joins.
- A piece that STARTS at a level (a change at 0 with no glide) is drawn only as a 7 x 3 pixel mark under its banner: nothing says which
  tracks or how much. That is why a level carried over a join cannot be seen.
- The box is drawn round the tracks a change moves by 0.05 dB or more from the level just before it: a box round every track means every
  track's level changes there.
- Wanted: whether a change carries over the join to be plain; a fade back shown only on the tracks it is on; a click on the teal triangle
  to say what the change does and to change its length, its fade in and its fade out. (Today: a double-click opens it; a "from here"
  change has one glide and no end of its own; neither kind's start or end can be changed.)

## 3.49.0: the levels drawn; tab keys open their tab first (10 November 2026, with Claude Fable)

- **`EditTimeline::paintLevelLines`** draws what `EditRegion::levelDbAt` says, nothing else: it does not look at what made a level (group,
  role, autoNorm), so it stays true whatever the volume changes become. It is called before the boxes and triangles. The figures: at a
  piece's start only when the level differs from the end of the piece before (pieces of the edit in order; an overdub: when it is not
  0 dB); after a change only when its glide arrives (a later change that starts first says where the level goes instead); alike on every
  row of the piece: once, on the top row.
- **Tab keys** (`NoiseModeDialog::keyPressed` in FixToolsEdits.cpp; `SpectralWindow.h`, shared, `tabWasOpen`): a tab's key does the work
  only when `tab` is already that tab. L and P are not tab keys and still act at once.

**The edit that was asked about** (A New Song - EDIT, sent on 10 November) - what its file holds, because it decides the next build:
- Piece 010 (1:32 - 1:58): one "from here" change at 1:33.27, a 1 s glide, Spot2 +4 dB.
- Piece 007 (1:58 - 2:07): starts at Spot2 +4 dB (a level of its own that happens to match: nothing ties it to the change).
- Piece 010 again (2:07 - 2:12): starts at +2 dB on every track and +6 on Spot2 (the Trim window's +2, "normalise" ticked, on top of the
  carried +4); the normalise glide then takes EVERY track to 0 dB - Spot2's +4 with it; at 2:09.63 a "from here" change takes every track
  to +0.6 dB over 1 s, where they stay to the end of the piece (the normalise glide never arrives).
- So: the raise was carried over both joins, but only as loose copies; the fade back was written as a level for all eleven tracks; and
  the raise had already been melting away under the normalise glide. Every level is stored as an absolute figure per track, in the
  piece: a raise, the Trim window's level and the normalise glide overwrite one another instead of adding up.

**Proposed for the next build (not built: it is shared code and changes what plays, so it was put to the user first):** a volume change
as an AREA of the edit, kept beside the pieces and not inside them - start, end, fade in, fade out, and so many dB for each track it is
on - added ON TOP of each piece's own level (the Trim window's, with its normalise glide). It crosses joins by itself, is not undone by
a normalise glide, and is opened from its triangle to change its start, end and both fades. The pieces' `gains` stay as they are (the
Trim window, old edits); the player (`addRegionSegments`, Playback.cpp, the one place that reads them for sound) would play the two
combined. The project file gains a key (rule 4: it only grows); an older fermata would drop the areas when it saves. "From here to next
edit" and "between the 1/2 marks" would both make an area; an old change would become one when it is opened and kept.

## 3.50.0: volume areas; Inter throughout in the Fermata style UI; the send dials (10 November 2026, with Claude Fable)

**Volume areas.** What a later change must not undo:
- **Two layers, never merged.** `EditRegion::gains` is the piece's OWN level (the Trim window's level and its `autoNorm` glide, "between
  fades", an overdub's little mixer, and every volume change made before 3.50.0). `EditDef::areas` (`VolumeArea`, Edit.h) is the edit's
  volume changes. What plays is the product. Nothing writes an area's level into a piece, and nothing reads `gains` to learn what an area
  does. That is the whole point: the absolute levels in the pieces were why a raise, the Trim level and the normalise glide overwrote
  one another.
- **The one place the product is made for sound** is `segmentsForEdit` (Playback.cpp): after the pieces' segments are built, each gets
  `areaGains` (`EditDef::areaGainsFor`), and `PlaySegment::gainAt` multiplies them in. Everything that plays, bounces, masters or exports
  an edit goes through `segmentsForEdit`. The noise window's own reading "as it is heard" (`heardPart`, FixToolsCommon.h) does the same
  with `TrackPart::areas`. The LTC track gets none. A player that builds segments any other way must add the areas itself.
- **An area is positioned on the timeline and kept over its music**: `EditDef::rippleAfter` moves its ends (an end over music taken out
  comes to where that music was; an area left empty is erased) and `editversions::Riders` carries them when pieces are slid, shuffled or
  re-ordered. An undo brings the areas back with the edit's copy (`EditDef` is copied whole; `Project::undoHash` counts the areas only
  when there are some, so an edit without areas hashes as it always did). Nothing else moves an area: a join moved in the Trim window
  leaves it where it is, as it leaves a marker.
- **Fades are straight lines in gain** (`AreaGain::at`), as a `GainChange`'s glide is; when the two fades are longer than the area they
  share it in proportion (`VolumeArea::fades`).
- **Old changes** (`src/core/VolumeAreas.h`, tested in core_test.cpp on a copy of the edit that was asked about): `takeOutGroup` and
  `takeOutLoneChange` read an old change as an area and take its points out of the pieces. The amounts of a group are read at its role 1
  point (where the level before is the piece's own); a role 2 point holds the piece's own level and the area in one figure, so it only
  speaks for a track the first piece did not have. A lone change is followed through the pieces that START at the same raise, and ends
  at the change that brings its tracks back level with the others - both are read while the raise is still in the pieces. The level a
  piece starts with is never an old "change"; an overdub's change stays in its piece.
- **The box** (`VolumeAreaPanel`, VolumeDialog.h): the area is in the edit while the box is open (so Play plays it); Cancel puts the
  pieces AND the areas back from the copies made when it opened. For an old change the pieces are changed the moment the box opens
  (its points come out, the area goes in) - which is why Cancel must restore the pieces too.
- **Drawing** (`gainUnits` gives an area a unit with `hit.areaId`; `paintLevelLines` draws piece level x areas): the level lines know
  nothing of areas except through `EditDef::areaGainAt`.
- Not done, on purpose, and worth doing next: the versions' changes lane (`editversions::changes`) does not compare `areas`; an area's
  ends cannot be dragged; `Retime` leaves an area's ends where they are on the timeline when a piece under it is stretched.

**Type.** The Fermata style UI's ordinary face is Inter Medium (`fontdata::interMedium`, appended to FontData.cpp from
`assets/Inter-Medium.otf`, the same Inter 4.0 build as Regular and Semibold; `FermataLookAndFeel::getTypefaceForFont`). Regular is still
bundled and no longer used. `theme::figureFont` (Uikit.h) is the face of the displays: Inter with the `tnum` feature in the Fermata
style, the system's console face in the Manuscript UI. A display's font is asked for when it is drawn (or when its strip is rebuilt on a
change of style), so a switch of UI style changes it. Lists in columns keep `Font::getDefaultMonospacedFontName()`.

**Send dials.** `MixerLook::drawRotarySlider` reads the slider's properties: "arc" (the band's colour - the send dials set green; it was
the destination channel's colour, invisible when that was dark), "muted" (the whole ring pulses red, timed from the clock so every
muted dial pulses together; `SendDial::pulse` repaints the dial at 15 Hz only while its send is muted), "pre" (the yellow square).

(The answering of listeners' comments, asked for on 10 November and noted here as not built, is 3.51.0.)

## 3.51.0: replies to comments and the conversation in the listening copy; the score; pins; old volume changes as sections (10 November 2026, with Claude Fable)

**The conversation.** What a later change must not undo:
- **One list, three new fields.** The editor's replies and notes are `FeedbackNote`s in the same list as the listeners' comments of that
  version (`EditDef::feedback`, the live edit's or a frozen version's): `editor` (gold ring), `replyTo` (the key of the comment it
  answers) and `madeIn` (the live version's number when the editor wrote it). A reply has the SAME `seconds` as its comment, so it is
  in that comment's thread (`feedback::threads`: notes within 0.05 s) and travels with it through every operation that moves comments
  (rippleAfter, `editversions::Riders`, the undo's carry, freeze / unfreeze). `feedback::sortNotes` keeps a reply after its comment
  whichever was put in first; `feedback::merge` ends with it.
- *(Changed in 3.52.0: a note is now shown in the lane it is kept in - see 3.52.0.)* **Where a note is KEPT and where it is SHOWN are different on purpose.** Kept: under the comment (so fermata's lanes show comment and
  reply together). Shown in the listening copy: in the lane of the round it was made in - a listener's comment in the lane of the
  version it was made on, an editor's note in the lane of `madeIn` (`reviewpage::historyFor`, ReviewHistory.h). So the live version's
  lane is "the editor's replies on this version", which is what was asked for (10 November: "They should be able to 'see replies on
  current version'"). A note with `madeIn` 0 counts as the version it is kept with.
- **The page only shows it.** `reviewpage::Spec::history` -> the piece's `"history"` in the page's meta JSON (written only when not
  empty: a first copy's page is byte-for-byte what it was apart from the new script). Times are in the copy's own time
  (`mapSeconds` to the live edit, less `editOffsetSeconds`); the page clamps them to the audio. What the listener adds goes back in the
  .fmnotes file as before, a reply with `"re"` (read into `replyTo`; `"editor"` is never read from a file that comes back - nobody
  outside can claim the ring).
- **One colour a person**: `feedback::personArgb` (Feedback.h) and `personColour` in `tools/review_page.html` are the same function,
  written out by hand in both (multiply by 31, add the character, modulo 8 into the same palette). The core test holds the values the
  page's script gives for six names; change one and the test fails.
- *(Changed in 3.52.0: the import only adds - see 3.52.0.)* **The import replaces the listeners' comments of a version, never the editor's notes** (`importFeedbackFiles`: the notes with `editor`
  are put back before the merge). "Mark every comment as done" and "Take away the comments marked done" leave the editor's notes alone;
  `feedbackui::allDone` does not count them.
- **`tools/review_page.html` is the page; `src/core/ReviewPageTemplate.inc` is made from it** (`tools/make_review_template.py`). In 3.42.0
  three lines (the version number that comes back with the comments) were changed in the template only; a regeneration would have lost
  them. They are in the source now. Always change the .html and regenerate. The page must stay pure ASCII.
- The page was driven in a browser here (Chromium, by script): the lanes, the ticks (and that they are remembered), a reply, a new
  comment, a click on a lane's flag, the exported notes file, light and dark, a phone's width.

**The score.**
- `EditDef::score` is a NAME in `Project::pdfsFolder()` (read through `createLegalFileName`: never a path). `scoreTop` / `scoreZoom` are
  the window's view. All three are view keys: saved, in `saveHash`, not in `undoHash`, carried over by an undo (restorePiece), the same
  for every version. `Project::addScore` copies (the same file again is not copied twice; another file of that name gets "(2)").
- `ScoreWindow.h`: one worker thread owns the open PDF and makes pictures of the pages in view at the size they are shown (the display's
  scale included, 3600 pixels at most), one at a time (`busyPage`); the message thread keeps the pictures of the pages within three of
  the view. The window is a ToolWindow from `showWindow` (key `score:<edit id>`, `forwardKeys`, closed with its edit in
  `refreshWindows`).
- **`PdfPages.h` is plain C++ with two bodies.** `PdfPagesWin.cpp` (Windows.Data.Pdf through C++/WinRT, C++20, `#pragma comment(lib,
  "windowsapp")`) is tried by `try_compile` in CMakeLists.txt with `cmake/PdfCheckMain.cpp`; when the trial fails the build uses
  `PdfPagesNone.cpp` and prints why. **It has never been compiled here** (no Windows SDK on Linux); `tools/syntax-check-ui.sh` skips it.
  It was written to the documented API: `StorageFile::GetFileFromPathAsync`, `PdfDocument::LoadFromFileAsync`, `GetPage`, `Size`,
  `RenderToStreamAsync` with `PdfPageRenderOptions` into an `InMemoryRandomAccessStream`, read back with a `DataReader` (the picture is a
  PNG, decoded by JUCE). Every call is on the worker thread (`.get()` must not run on the message thread). If the first Windows build
  reports that the reader did not build, the trial's output in the configure step is what to read.
- Looked at here with a stand-in body that uses the Linux PDF tools (not in the repository).

**Pins.** `Pins` (Uikit.h, shared): the list of pinned windows; one timer brings them all forward, in the order they are in now, while
an unpinned window of the program's own is the one in front (the single-pin rule's test, kept: the program is in front, no modal box
is up, the active window is a native-title-bar DocumentWindow). `ToolWindow::setPinned` and a plug-in's window both go through it. A
plug-in's window (`PluginHost::EditorWindow`) now holds a `Holder` - a 26-pixel bar with the pin, and the plug-in's editor under it: the
pin cannot be laid over the editor, which is a native window of its own. The holder follows the editor's size (plug-ins resize
themselves) and gives a resizable editor the room under the bar. **Not seen with a real plug-in here** (none on this machine).

**Old volume changes as sections.** `EditTimeline::oldChangesAsAreas` runs `volumeareas::takeOutLoneChange` on a copy of the edit for
each lone change (cached on `undoHash`) and `gainUnits` draws the area it gives; a later change that the reading consumed (the way
back) is not drawn on its own. Drawing only: the pieces are not touched, and a click still opens `showOldChangeAsArea`, which makes the
same reading.

**The subheading box.** A click on a component that does not take the keyboard hands it to the first child that does
(`Component::grabKeyboardFocusInternal`): for the version strip that was the subheading box. The strip has
`setMouseClickGrabsKeyboardFocus (false)` and gives the keyboard to the timeline in `mouseDown`.

*(Built in 3.52.0.)* **Asked for on 10 November and not built**: a listening copy of an older, frozen version, chosen per piece on the Listening copy tab
(default: the newest). The export renders an edit by its id through `MasterRenderBatch` / `Bouncer`, with the live edit's automation
(`bi.automationEdit`) and the edit's mixer as it is now; a frozen version needs its own pieces (`segmentsForEdit (live, *v.edit)` is
enough for those), its own automation lanes and the mixer snapshot it was frozen with (`EditVersion::mixer`, with plug-ins' states),
and the render store keeps one render per edit id. "Hear Edit N" (`AppContext::playEditVersion`) plays the pieces through the mixer as
it is now and says so; an export must not do that silently.

## 3.52.0: the comments workflow; a listening copy of any version; removing a frozen version; held pictures behind a process window (10 November 2026, with Claude Fable)

**The workflow, as the user set it out on 10 November 2026** (it replaces parts of 3.42.0 and 3.51.0 - do not bring those back):
1. Edit 1 is made; listening copies go out. **An export freezes nothing** and offers nothing (the "Freeze now?" in the Export complete
   box, 3.42.0, is gone with `MasteringComponent::freezeExported` and `lastFormVersions`).
2. A .fmnotes file comes back. **The import is where the next edit starts** (`EditWindowComponent::importFeedbackFiles`): comments on
   the live version ask once - "Start Edit N+1 and show them above it?" (freeze through `freezeLiveVersion`, the Freeze key's own code;
   the comments then go to the frozen version's list, its lane shown) or "Keep on Edit N". Comments on an already frozen version go to
   its list with no question: that is how a second listener's Edit 1 file joins the first when Edit 2 exists. A file that names no
   version counts as the live one. Several pieces in one file: one question for all of them.
3. **The import only adds** (`feedback::merge`: a known key is updated, its tick kept). The old "the first file of a batch replaces the
   version's comments" lost one listener's comments to the next one's file - it must not come back in any form.
4. The editor answers by clicking the comments in the lane (3.51.0's Write Reply). A reply is kept in the list of the comment it
   answers, with the same `seconds`.
5. **The listening copy shows a round as two lines**: `reviewpage::historyFor` now puts every note in the lane of the list it is kept in
   (the `madeIn` lane rule of 3.51.0 is gone; `madeIn` is still written, and still decides what is left out of a copy of an OLDER
   version: a note written while a later one was being made). The page (`tools/review_page.html`) splits a lane into rows - "Edit k
   comments" and "Edit k editor" (gold) - and a moment's notes into two flags (`HT[k].marks`, `rows`); the list at the side keeps the
   thread together under a tab for the version. One open / closed choice per version (`hid['v'+n]`: 1 closed, 0 open, absent = the
   default) drives the tick, the tab and the rows. Default: the newest lane that is not the copy's own version, and the copy's own
   version, are open; older ones closed. The listener's row is "Edit N: yours".
Tried in a browser by script (Chromium): the default for a copy of Edit 3 and of Edit 2, opening and closing by tab and by tick, the
choice kept over a reload, a click on a flag of each line, a reply and a new comment and the notes file they make, dark, a phone's width.
The import was driven in the Linux build under Xvfb with the test project (Edit 1 frozen, working in Edit 2): a file on Edit 2 asked the
question, "Start Edit 3" froze Edit 2 and put its three comments in the Edit 2 lane; then two files together, one on Edit 1 and one on
Edit 2, went into those lanes with no question and nothing replaced; a reply written with Enter (and Shift + Enter for a second line) was
saved under its comment with `madeIn` 3; listening copies of Edit 3 and of Edit 2 (chosen on the key) were made and opened in a browser.
No sound here: the renders were silent.

**A listening copy of a frozen version** (`MasterItem::formVersion`, 0 = the newest; saved as `"formVersion"` only when set).
- `makeFilesSpec` marks the item (`MasterJobItem::frozenVersion`), takes the offset and the conversation from the version
  (`historyFor (live, version, offset)`), and the names say "Edit N".
- **Rendered in the Bouncer's private copy of the project, never in the project** (`BounceSettings::overrides`, `BounceEditOverride`):
  after `cloneForRender` the frozen `EditDef` is put in place of the edit in the shadow, and - with "each piece through its own mixer" -
  the edit's mixer in the shadow is given the snapshot the version was frozen with (`applyMix` with `clearEmptySlots`; its plug-ins are
  checked against the snapshot, `Project::pluginsMissingFromSnapshot`). With "all pieces through one mixer" the mixer is as it is now
  and `MasterExportResult::versionNotes` says so in the Export complete box.
- A frozen item is **not** rendered through `MasterRenderBatch` (the store keeps one render per edit id, the live edit's): the job
  has a Bouncer of its own per frozen item (`MasterExportJob::versionRenders`), written into the work folder's `versions`.
- The core test renders both and compares length and level; it measures the level INSIDE the first piece, because two pieces of a
  steady made-up sound are louder where they cross.

**Removing a frozen version** (`editversions::removeVersion`): the newest is `unfreezeLast`; any other is erased, the later ones and
every `madeIn` renumbered, its comments merged into the version before it by their music (`mapSeconds`), `compareWith` moved. The
window resets `formVersion` of the edit's Mastering piece.

**The comments' safety copy and a freeze** (the doubled comments of 10 November). `Project::saveCommentsCopy` writes `comments.json` at
an import; `restoreSavedComments` gives it back to an edit that has lost its comments. A freeze empties the live edit's list on
purpose, so the copy came back as a second set. Now: every freeze / unfreeze / thread-box delete calls `refreshCommentsCopy` (which
rewrites or removes an existing copy and never starts one); `restoreSavedComments` never restores to an edit that has frozen versions,
and takes off the live edit any listener's note whose key is on one of its frozen versions (the repair for projects saved by 3.51.0).

**Held pictures** (`HeldPicture.h`, fermata only; `ProcessWindows` in Uikit.h, shared). The log of 10 November: 12 cores at 5 to 15%,
the message thread held ~290 times for 150 to 470 ms by "drawing the edit window / a take window" while the noise picture played.
- `JobDialogBase` (SpectralWindow.h) registers every process window with `ProcessWindows`; `inUse()` = one of them is, or was within
  2 s, the active window.
- While `inUse()` and the transport plays (or did within 1.5 s) `EditTimeline` and the take window show a kept image
  (`HeldPicture::show`) instead of painting, skip their timers' repaints and `movePlayhead`; `keep` makes the image at the end of a
  normal paint whenever a process window is in use, so the first held frame costs nothing. `stale` brings one real repaint when the
  hold ends. **Never while recording** (the take window's picture is the recording).
- `SpectrogramView`: `setPlayhead` repaints two 8-pixel strips; `livePlayhead` + a `VBlankAttachment` move the line at the screen's
  rate (Noise mode sets it; a window that does not still calls `setPlayhead` itself); `BusyLabel ("drawing the noise / ReTouch
  picture")` for the stall watch; `PlayLog` writes one line per play. `BusyLabel` moved to its own shared header (`BusyLabel.h`).
- `FERMATA_TEST_HOLD=1`: holds whenever a process window is in use and makes the Edit window ask for a redraw every tick - measured
  here with `FERMATA_PAINT_PROFILE=1`: 0.3 ms a held redraw against 16 to 33 ms a real one. **Not heard or seen with sound** (no audio
  device here): the user's next log is the check ("Noise / ReTouch picture playhead: ..." and "redraws held back ...").

**The reply box.** `feedbackui::ReplyEditor`: Return = Add (`onEnter`), Shift + Return inserts a line break.

## 3.53.0: recording reliability (awake, dropouts and device stops marked, take notes kept), the plug-in window's keys, channel names to plug-ins, the report at closing (11 November 2026, with Claude Fable)

**Where the review of 11 November left the recording path** (asked: "any improvements for recording reliability and low latency?"). Built
here: items 1 to 4 below. NOT built, and still true: the second-drive backup is a copy that TRAILS the main file (`BackupCopier`), so it
stops when the main drive does - a second writer fed straight from the audio thread would survive that; a plug-in crash still takes the
program down (in-process hosting); a plug-in with delay in a cue mixer delays the whole of that mixer (delay compensation) - a
low-latency monitoring switch for live inputs, and two buffer sizes (live inputs small, playback large on the helper threads), are the
two latency jobs. All four touch the heart of the engine.

1. **Awake.** `CoreReservation::keepAwake (bool)` - `SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)`,
   looked up by name. Windows keeps the request with the THREAD that made it: always the message thread (fermata:
   `AppContext::tickRecordMonitor`, every tick, a no-op unless it changed; fermata:Live: `RecorderService::timerCallback`, while a session
   exists or a merge runs). JUCE's `Desktop::setScreenSaverEnabled (false)` is not this: it fakes a mouse move every few seconds and only
   while the program is in front.
2. **Dropouts located.** `AudioEngine::RecEvent` {kind, at (samples from the take's start), amount} in a 64-slot `AbstractFifo` - one
   writer (the audio thread), one reader (`takeRecEvents()`, message thread). `noteLateBlock()` is called by the callback when a block
   took longer than it lasts (`lastRecAt` = where the block began, -1 when the block was not a take's; a run of late blocks within half
   a second is one event). The driver's own count (`getXRunCount`: JUCE's ASIO counts `kAsioOverload`) is polled by the watcher at
   10 Hz, so its place is right to a tenth of a second. The watcher (`tickRecordMonitor`) turns each into a Recording-monitor line and an
   ordinary marker on the take window (`RecordMonitor::dropouts`, `takeMarks`, at most `kMaxTakeMarks`). **Not done on purpose:** judging
   a dropout from the time between callbacks - drivers that deliver in bursts would mark every block.
3. **A device that stops in a take** (`fillDeviceGap`). `audioDeviceStopped` with a session sets `recGapPending`; the take's next block
   measures the time since the take's last block (`lastRecTicks`, the high-resolution clock, stamped in `process()` so the tests'
   direct calls work), less the block that was due, and - when it is no more than `RecordingSession::gapFillMax` - gives every file that
   much silence with `TakeWriter::addSilence` (the overrun's "silence owed" path, without counting an overrun) and every live waveform
   the same (`LivePeaks::addSilence`, by bins: no samples are looked at). Ring (session) mode first delivers everything up to the stop,
   and its length is `delivered - first + gapSamples` (three places). **Why a cap:** the silence is written by the disk thread BEFORE the
   sound that follows it, which waits in the files' 8-second buffers meanwhile. `gapFillMax` is what a 100 MB/s drive writes in half that
   time, between 2 s and 10 min (set in `startRecording` from the take's bytes per second). Longer: `gapNotFilled`, nothing added, the
   marker says how much is missing. Never end the take for it. A different rate at the restart still ends the take, as since 3.1.5.
4. **A take's notes** (`Project::saveTakeNotes` / `recoverTakeNotes` / `forgetTakeNotes`; `<project>/Recovery/take-<id>.json`). Written
   by the watcher every 3 s when the text changed (`lastTakeNotes`), through `replaceTextSafely`: the window's markers from the take's
   start on, the take's bars, label, note, Dud. Given back in `Project::load` (before the application mends the files and gives the take
   its length) ONLY to a take saved with length 0 - still recording when the program stopped; markers by id, the rest where the take has
   none. The file of a take that has its length is removed without being read into anything: a marker the person deleted since must not
   come back. A merged file stays until the take is whole (a second opening without a save merges the same again: nothing doubles).
   `readOnlyLoads` (a render's copy) touches nothing. The files of earlier takes go when the next take starts.

Tests (core_test "3.53.0 recording reliability"): `addSilence` (more than the buffer, and at the very end); a device away for a third of
a second in both modes - the event, the silence exactly in place, the length, no overrun counted; a stop before the take's first block;
the late-block event; the notes file through save / stop / load / load again / mended / read-only / next take; `keepAwake`'s answers.
**Not tried with a real device**: a real driver's stop and start, and whether `getXRunCount` ever moves on the Ravenna driver.

**The plug-in window's keys** (`PluginHost::EditorWindow::Holder`, `BarKey`). Bypass is the SLOT's `bypass` flag (found through
`PluginHost::slotOf`, which the application sets: `AppContext::forEachInsertSlot`), not the plug-in's own bypass parameter - so it is
what the mixer shows, what is saved, and the plug-in still hears the music (see `InsertSlot::process`). The window polls 5 times a second
(`syncBar`) so it follows the mixer. Copy / Paste: `copySettings` keeps the description, the identifier
(`PluginDescription::createIdentifierString`) and the base64 state; `pasteSettings` calls `setStateInformation` on the RUNNING instance
(message thread, as a preset change in any host; the slot is not locked for it - locking would pass the sound round the plug-in dry for
that moment); `createFromCopied` is the project's factory with the copied state, for an empty slot. The keys never take the keyboard.
A plug-in narrower than the keys keeps the pin and Bypass.

**Tried here with a plug-in built into fermata** (`src/ui/TestPlugin.h`, only with `FERMATA_TEST_PLUGIN=1`: a `juce::AudioPluginFormat`
of its own with two kinds, "Test plug-in A" and "B" - a gain, a second control, and a window that shows the channel name it was told;
`PluginHost` takes its types out of the known list at every start without the variable). In the Linux build under Xvfb: inserted from a
slot's menu; the top row and its keys; Bypass from the window lights the key and colours the mixer's slot, and from the mixer's menu
puts the key out; Copy, then "Paste Test plug-in A with the copied settings" on an empty slot made a second one already set (1.74 /
0.62); its controls changed and its Paste key put them back; Paste is dim in a window of plug-in B with the tip that says why; both
were told "PnC - LV08 - Tender" / "PnR - LV08 - Tender"; the project closed cleanly. **Not tried: a real VST3**, and Pro-Q's list.

**Channel names.** `Vst3Insert::tellTrack` -> `AudioProcessor::updateTrackProperties` (JUCE passes it to a VST3 as
`IInfoListener::setChannelContextInfos`), only when name or colour changed; `AppContext::tellPluginsTheirChannels` once a second from
the load ticker. The name is "channel - mixer name" exactly as the mixer is called. Not checked against Pro-Q 4 here (no plug-ins on
Linux): the user's own look at its instance list is the test.

**`madeIn` after a freeze is undone** (the user's four edits of 11 November: Edit 1 frozen, the edit in Edit 2, every reply in Edit 1's
list stamped `madeIn` 3). `historyFor` leaves out an editor's note with `madeIn` above the copy's version; `unfreezeLast` did not bring
the stamps down, so after "Edit 3 -> take the extra freeze away -> Edit 2" every copy of Edit 2 dropped the replies.
`editversions::settleMadeIn` (no stamp above the live version's number) is called by `unfreezeLast` and, for every edit, when a
project is read (`Project::restoreSavedComments`); `historyFor` also takes a stamp above the live number as the live number.
`removeVersion`'s own renumbering (3.52.0) was right. **The page** no longer draws a done comment's flag at 40%: a tick and full
strength; a nameless listener's flag says "Note" (it was "?").

**The report at closing** (the dump of 10 November 2026, 22:59:33). One thread left in the dump; at the bottom of its stack the C
runtime's exit, then Windows' loader unloading libraries, then `Windows.Data.Pdf.dll` -> `d3d11.dll` -> `dxgi.dll` -> RaiseException
0x87A with 0x887A0001 (DXGI_ERROR_INVALID_CALL): Windows' PDF reader letting go of its Direct3D objects during process exit, after
fermata's `shutdown()` and its static destructors. Our unhandled-exception filter was still installed and wrote a report.
- `crashreport::closingFinished()` at the end of `shutdown()`; the crash handler asks `closingHasFinished()` first, writes one log line
  and `endThisProcess()`.
- `pdfpages::everUsed()` (set when a document is first opened): `shutdown()` then ends the process itself (`TerminateProcess`, code 0)
  right after closing the log, so the PDF reader's unload code never runs. Everything is saved and closed by then.
(The symbols of Windows' own libraries were not available here: the reading rests on the single thread and the order of the modules.)

## 3.74.0: the Spectrogram Editing window, part 1 - the figures and the meters (28 November 2026, Claude Fable; fermata only)

**Asked** (27 November): RX's Waveform Statistics for the Mastering window's piece, whole and for a selection; live meters "similar to the TC
Electronic Clarity M Stereo device and the Dorrough Loudness meter". Agreed in the conversation: it works on the multitracks' result, stays
editable to the export, in two builds - A (this: the measuring) and B (the spectrogram, the top true peaks marked, feathered gain boxes with
ReTouch's gain, a mastering plug-in on chosen tracks just before the mix).

**`src/core/WaveStats.h` (new; plain C++, no JUCE; `tests/wavestats_test.cpp`):**
- `TruePeak`: the points between two samples from the 12 either side (Kaiser beta 6 windowed sinc, each phase at unity gain; 3 phases at 4 x,
  1 at 2 x). `cell (xi)` = the peak of sample i and the points up to the next. Flat to 0.45 fs within 0.01 dB; what is left is the spacing of
  the points (cos (pi f / 4 fs)). `oversampleFor (rate)`: 4 below 80 kHz, 2 below 160 kHz, else 1. `kClipOver` = 1 + 2e-6: "over 0 dBTP".
- `KFilter` (the BS.1770 shelf and high-pass, designed for the rate - the same numbers as Loudness.h), `channelWeight`.
- `Analyzer` -> `Profile`: 10 ms ticks (`tickLen` = rate / 100) per channel: lo / hi sample, sample peak, true peak and its sample, clipped
  count, sum, sum of squares, sum of K-weighted squares; `cross` (L x R per tick); running sums `sqUpTo` / `kUpTo` made in `finish()`. The true
  peak of sample i needs 12 later samples: a tail of 23 samples is kept between blocks, and `finish()` feeds 12 zeros (silence follows the
  file). Block sizes do not change the result (tested bit for bit).
- `measure (profile, from, to, options, reader)`: whole ticks from the Profile; the two ends that are not on a tick are read through the
  `Reader` and measured sample by sample (`detail::measurePiece`, with 12 samples of context either side), so peaks, clipped samples, DC and
  total RMS are exact; without a Reader the part is rounded out to ticks (`exactEnds` false). RMS windows: W ticks, a tick apart, inside the
  part (a part shorter than a window is its own window); AES17 adds 3.01 dB. Loudness: 400 ms blocks / 3 s windows every 100 ms from the
  part's first whole tick; integrated = BS.1770 gates; LRA = Tech 3342 (gates -70 LUFS and -20 LU, percentiles 10 and 95 by
  floor ((n - 1) q + 0.5), as libebur128). `Profile::momentaryAt / shortTermAt / power / correlation` serve the meters.
- Speed: about 1.6 s for 10 minutes of 48 kHz stereo (-O2, one thread); `measure` about 1 ms.
- NOT changed: `Loudness.h` (the export's LoudnessMeter: its own 12-tap Hann interpolator, always 4 x). The two true peaks can differ by a few
  hundredths of a dB (more near the top of the spectrum). One meter for both would be the tidy end state; it changes the export's levels by
  that much, so it was left for a decision.

**`src/ui/SpecEditWindow.h / .cpp` (new, fermata only; window key `specedit`, one window; `AppContext::showSpecEdit (editId)`):**
- The piece's render: `MasterRenders::check` with the piece's fingerprint; Ready / Previous -> `FileAnalysis` (a thread: the file through the
  Analyzer, `done` on the message thread, guarded by a shared `alive` flag). None / Stale -> its own `MasterRenderJob` for that one piece
  (`startRender`), then the same restamp rule as MasteringComponent::renderFinished (`renderDone`: only when what differs is known to change
  by itself; otherwise "changed while it was rendered" and no automatic second try until the project changes again).
- `MasterBusy.h`: a message-thread count of who is rendering previews. MasteringComponent holds one while `job`, `renderJob` or `atmosJob`
  exist (`syncBusy()`, in its timer and where they are made) and waits (`othersRendering()`) in checkRenders / startJob / startAtmosExport;
  this window holds one for its job and does not start while the count is over 0.
- A project change marks it dirty; 1.2 s later the fingerprint is compared and the render looked at; every 2.5 s the store is looked at in case
  the Mastering window rendered the piece meanwhile.
- `WaveView`: columns from the ticks' lo / hi, or (under 0.75 ticks a pixel) from the samples, read when the view changes (`fillFine`); the
  loudness lane from `momentaryAt / shortTermAt`; selection and playhead; wheel zoom about the mouse (no closer than 4 pixels a sample).
- `StatsPanel`: the two blocks; tooltips per row; a click on a peak's figure -> `onGoTo (frame)`; `asText` for Copy.
- `MetersPanel`: reads the Profile at the playhead's tick - no audio-thread tap. Playing: bars fall 20 dB / s, hold 2 s, the integrated loudness
  and LRA of [the tick Play started at, now] by `measure` four times a second. Standing still: the values at the playhead. The vectorscope and
  the spectrum (8192-point Hann FFT, both channels' power, 31 third-octave bands, a full-scale sine = 0 dB) read the file (`FileSamples`) -
  the stretch before the playhead while playing, around it standing still.
- Playing: `AppContext::playRenders` (the render at unity to the Ext bus's outputs, as the disc audition), its own token.
- Settings (user): `specEditRmsMs`, `specEditAes17`, `specEditAuto`.
- The small-screen check: scenes `specedit` (the window renders and measures the piece) and `specedit-measured` (the render filled with a test
  sound first - `soundInRender` - so the figures and meters have something to show whatever the project's mixer lets through).

## 3.73.0: the video report, the head's buttons on two lines, softer corners (27 November 2026, Claude Opus; fermata:Live 0.10.19)

**Asked:** the video reader was missing from the user's build ("this build of fermata cannot read video on this computer's system" - the text
of `VideoReaderNone.cpp`) and `video-reader-did-not-build.txt` was not in the download; the edit window's buttons on two lines on the smaller
screen; rounded corners everywhere, as the time readout's.

**The video report (CMakeLists.txt, the workflow, `VideoSource.cpp`, `HelpMenu.h`):** after the video trials, CMake always writes
`build/video-reader.txt` - the source built in, the trials' outcome (`FERMATA_VIDEO_TRIALS`: "not tried (the compiler is not MSVC)" too, so a
skipped block shows), CMake, generator, compiler, `CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION`, each trial's output (the last 60000 characters)
- and the same bytes as `build/generated/VideoBuildReport.h` (`kVideoBuildReport`, a byte array; on Fermata's include path). `VideoSource.cpp`
includes it with `__has_include` (the Linux checks' builds have none: `buildReport()` says so). Help > Save the video report writes
`Documents/Fermata/fermata-video-report.txt` (version, `video::systemName()`, Windows, the report) and reveals it. The workflow copies
video-reader.txt beside Fermata.exe always (a line saying it was not made, if Configure did not make it). Video.cpp's NOT ADDED message points
to the report when the system is "none in this build". The old `video-reader-did-not-build.txt` is still written and copied as before.

**The head's buttons (`src/ui/HeadButtons.h`, new, fermata only):** `headbuttons::choose (items, widthAt)` tries way 0 (as they are), 1
(`compact`: 34 -> 28, other widths -6 / -12, gaps 6 / 8 -> 4, 14 -> 10 - still 10 or more, so grid::flow's groups still end there) and 2 (1 with
`keepGroups` off) and takes the first that needs no more than two lines; when none does, the one with the fewest lines (the first of
equals). `widthAt (level)` is the buttons' room at that level: the edit window's name box is 240 / 170 / 150, the take window's column 200 /
170 / 160. The zoom keys stay in the corner (LaneFold.h's placeZoomKeys). The take window at 1280 wide still needs three lines (within its
head's height).

**Corners (`Uikit.h`, shared):** `theme::kCorner` = 4 (TimeReadout's radius), `cornerFor` (no more than a third of the shorter side),
`fillBox` / `drawBox` (int and float rectangles; drawBox draws inside the rectangle, as drawRect does). The look-and-feel (text editor - not
opaque in JUCE 8, so its corners show the parent -, combo box, toggle, sliders, scroll bar thumb, tabs: top corners only, the front tab's
accent line clipped to them), MixerLook (keys, combo, fader slot, pan, scroll thumb), the LevelMeter's well (its bars clipped to the rounded
well, the edge drawn over them), DeskLook's and MainWindow.h's displays, the input strips (MainWindow.cpp, LiveMainWindow.cpp: clipped to the
rounded strip, so the colour bars follow it), MarkerStrip's flags (a plain fillRoundedRectangle: it does not include Uikit.h), and fermata's
windows: the timelines' clips, banners, overdubs, guide and video clips, volume boxes, flags, selections and lasso; BarCoverage (its cells
clipped to the rounded strip); TakeChart; Mastering's DDP tracks; the colour picker; the mixer's wells; graph frames. The Manuscript keys use
the same radius (was 2.5; the mixer's 1.5 -> 3). Not rounded: rows, lanes, name plates and panels that tile; lines and grids; windows, menus,
tooltips and alert boxes (Windows' own windows); the spectral selection and the score's marks (they show exact areas).

## 3.72.0: lanes that fold, the take window's PDF key, the zoom keys on line 2 (27 November 2026, Claude Opus; fermata only)

**Asked** (with a picture of the edit window's lanes): the PDF key in the take window; - and + on the buttons' second line; fold and open the
lanes above the tracks one by one or all together, to give the tracks more room.

**`src/ui/LaneFold.h` (new, fermata only):**
- `lanefold::Folds`: one window's folded lanes - a set of ids, kept in the user settings as `laneFolds_<window id>` (a comma list: the edit's id
  for an edit window, the take window's id for a take window). `set`, `toggle`, `setAll (lanes, fold)`, `countFolded (lanes)`; `onChange` (the
  window lays itself out again). A fold of a lane that is not there just now (no guide, the tuning line off) is kept for when it comes back.
- Lane ids: `markers`, `feedback`, `guide`, `tuning`, `video` (the timelines' band); `comments:N`, `pieces`, `changes` (the edit window's
  VersionStrip). The take window has `markers` and `tuning`.
- Drawing: `triangleBox (nameX, nameW, laneTop)` (18 x 14 at the top right of the name box), `paintTriangle`, `paintFoldedBand` and
  `paintFoldedName` (a folded lane: `kFoldedH` = 16), `keyBox` / `paintKey` / `keyTip` (the LANES key), `showMenu` (Fold all / Open all / each
  lane, ticked when open).
- `placeZoomKeys (r, minus, plus, iconH)`: the zoom keys in the corner reserve (`grid::cornerReserve`), centred on the flow's second line
  (`max (rowH, iconH + 8)` below the top). The flow no longer has them (`zoomItems`), except in the narrow layout, where the buttons go under the
  head and the zoom keys are appended to them as before.

**Edit window (`EditWindow.cpp`):**
- The band's heights were constants (`markerui::kStripH`, `kFbH`, `kInsY`, `kRulerTop`, `kRulerH`, EditTimeline's `kGuideTop`); they are EditTimeline members
  now (`markH`, `fbH`, `insY`, `rulerTop`, `rulerH`, `guideTop`), worked out by `refreshBand()` (in the constructor, in `setViewing`, and on a
  fold change). `markH` / `fbH` are 0 when folded - except that the feedback line stays while a frozen version is on show (its notice is
  there). `guideH()`, `tuneH()`, `videoH()` give `kFoldedH` when folded; their paint functions draw the folded band / name and return early.
- `foldClick (e, shift)` runs in `mouseDown` before anything else (a frozen version on show too): the LANES key (`lanesKeyBox`: in the names
  column, 7 below the ruler's top), the triangles (a right-click: the LANES menu), and any click on a folded lane (on its name: open it; along it:
  nothing). `foldGesture` keeps the drag and the release of that click away from the rest of the mouse code. Folded markers: no flags, no strip
  clicks (`markers.mouseDown` only while `markH > 0`); their faint lines are drawn by the timeline itself (MarkerStrip.h is shared and
  unchanged).
- VersionStrip: `folds` (the window's), `foldLanes()` (for the menu), `idealHeight()` leaves folded lanes out, `paint` skips them and notes
  where it drew each triangle (`triRects`), `mouseDown` folds on a triangle; a TooltipClient now (the triangles' tip).
- EditWindowComponent owns `laneFolds` (declared before the timeline and the strip, so destroyed after them), gives it to both, and builds the
  menu's list (`foldableLanes()`: the strip's lanes, then the band's, top down; only the lanes there are just now).
- `scoreKey` / `scoreMenu` call `score::toggle` / `score::showMenu` (moved to `ScoreWindow.h`, fermata only, for the take window to share).

**Take window (`TakeWindow.cpp`):** the file-level `kRulerH` (34 + the marker strip) is the TakeTimeline member `rulerH` (with `markH`); `laneH`
comes from `wantLaneH()` (the tuning lane folded: `kFoldedH`); `refreshBand()` lays the rows out again (`applyHeightLevel`, so the rows take the
room). The marker strip has a name now ("Markers", with its triangle), as in the edit window. `scoreEdit (make)`: the window's target edit
(made, as `openEditWindow` makes it, when there is none and `make`); the PDF key is lit while that edit's score window is open (the timer).

**The small-screen check:** `FIT_ONLY` (names, with commas) runs only those scenes. New scenes: `takewindow-folded`, `editwindow-folded` (every
lane folded through the settings, then cleared), and last `editwindow-versions` / `editwindow-versions-folded` (the edit frozen as Edit 1 with a
comment, compared with: the comments, pieces and changes lanes).

## 3.71.0: windows that fit the screen (26 November 2026, Claude Opus; fermata:Live 0.10.18)

**Asked** (with a picture of the Listening copy page on a smaller screen, "Your name" a sliver): detect that a box will not be properly visible,
and either change the relative sizes or make a scroll bar - program wide. The cause there: `layoutForms` took its rows with
`r.removeFromTop (h)` one after another; when the page ran out JUCE gave the last row what was left.

**`src/ui/FitLayout.h` (shared, JUCE only):**
- **Squashed** (`fit::squashed`, with a `Roomy` snapshot): the contents are laid out once on a roomy page (as wide, much taller - or much wider,
  as tall), each component's bounds noted, then at the real size. A piece (a control, a list, a self-fitting component, or a component with no
  children) is squashed when it is shorter (narrower) than on the roomy page AND shorter than it needs (`leastHeight`: a label's font, a text
  box's font, 14 for a button or list box ...; `leastWidth`: a label's or button's text squeezed as far as JUCE squeezes it - at most 100 / 120
  pixels, so a long hint line is cut on purpose - 48 for a list box ...). Also squashed: a component that was all inside its parent on the roomy
  page and now shows less than it needs. Comparing with the roomy page is what keeps rows that are 0 high on purpose, or always narrow, from
  counting. `kMayShrink` (hint and status lines) never counts across; `kNoText` (IconButton) has no text to measure.
- **`WindowFit`** (a member of each window: `ToolWindow`, `FittedDialog`, `GuardedDialogWindow`, `StreamWindow`, fermata:Live's LiveMainWindow,
  RecorderWindow, FixerWindow, ReTouchWindow): after the window's `resized()`, `refit()` lays the contents out on the two roomy pages and at the
  window's size - three layouts when it fits - and only when something is squashed searches (in steps down to 8 pixels) for the width they need,
  then the height at that width; a scroll bar each way that does not fit (across, up to `kSlackW` = 24 pixels short is let go: a text squeezed a
  little more, rather than a bar). The contents stay the window's content component - only taller / wider and moved up / left by the scroll - so
  every `getContentComponent()` cast still works. Covers (drawn as the window draws itself there) hide the contents where they reach under the
  window's own edges (the wooden bottom and sides). `childBoundsChanged` is ignored while it lays out at trial sizes (a window that follows its
  contents' size - `setContentOwned (c, true)` - would otherwise grow with each trial). A timer (3 Hz): when WHICH components are visible changed
  (a tab, a row that appears) it fits again; when only WHERE they are changed (something moved or grew - a playhead), it checks the layout as it is
  against the last roomy pages, without laying anything out, and fits again only if that squashed something. Contents marked `kSelfFitting` are
  left alone (Main.cpp: take, edit, mixer, video, score, meter bridge; HotKeysComponent itself - its list scrolls; fermata:Live: its main window,
  the Fixer, take window, mixer).
- **Speed** (every trial size is a whole layout of the contents): text widths are remembered (`fit::textWidth`; the Listening copy page counts its
  paragraphs' lines with it; the Mixer controller measures its intro only when it or the width changes). On the Linux -O0 build: the Listening
  copy page 1.7 s -> 55 ms to fit when it scrolls, the ReTouch window 390 -> 25 ms; the HotKeys list (it lays out hundreds of lines at each width,
  0.3 s) is self-fitting, and it no longer lays its lines out again when only its height changes.
- **`fit::stack` / `Rows`**: a page of rows laid out twice - once to measure, then with the gaps cut (down to the least gap) when they do not fit.
  Used by the Listening copy and Atmos pages.
- **`keepOnScreen`**: no bigger than the screen's usable area less the window frame, and on it. Called by showWindow (Main.cpp), launchDialog
  (FixTools.cpp) and `FittedDialog`; a window whose contents grow it past the screen is shrunk back (WindowFit, async).
- **`launchAsync`** (`FittedDialog`): JUCE's DefaultDialogWindow, with the fitting. Every `o.launchAsync()` in the code now goes through it.
- **`audit`**: for the check - squashed leftovers, text wider than its control, controls over one another (a label by where its text is), controls
  off the window where nothing scrolls.

**The small-screen check** (`tools/fit-check.sh`; `tools/fit-check/Checker.h` does the looking, `FitCheckDriver.h` has fermata's scenes,
`LiveFitCheck.h` fermata:Live's; hooked into `src/Main.cpp` and `src/live/Main.cpp` under `FERMATA_FIT_CHECK`, which only the check defines):
each window as it opens, its tabs, then filling the screen and its tabs again; report.txt and a picture each. The Manuscript UI (wider serif
type: the user's) is the harder case - run it too (`FIT_STYLE=1`; `FIT_PROGRAM=live` for fermata:Live, which opens the project it had last - the
script sets that). The script was run end to end here (fermata: 1536 x 816, Manuscript UI, the same 2 windows scrolling as below; fermata:Live:
1366 x 728, nothing scrolling); it builds JUCE once (-O1, shared by both programs) and each program's own files when they change. On the rig
this session used, `$S/fitrig/run.sh` / `run-live.sh` did the same faster with the rig's objects (STYLE=1 for the Manuscript UI).

**What scrolls now on the small screens** (both UI styles; the last round of the check, 64 windows and tabs a screen): 1536 x 816 - only the
ReHarmoniSer (its last rows, about 45 pixels); 1366 x 728 - the Listening copy page and the ReHarmoniSer; 1280 x 680 - those, the Mixer
controller and the Bounce window (about 20 pixels). Nothing at 1920 x 1040. Nothing squashed anywhere. fermata:Live: nothing scrolls on any of
the four. The slowest fitting there: 79 ms on the Linux -O0 build (the Mixer controller).

## 3.70.0: a full-length meter and HEADROOM on every input strip (26 November 2026, Claude Opus; fermata:Live 0.10.17)

**Asked** (with a screenshot of the MT48's four preamp strips beside four plain inputs): every input strip's meter the full height, as the
plain inputs' already were (3.60.0), and a "headroom" reading on each that holds until reset - one reset per channel and one for all - to set
the preamps' gains in a rehearsal.
- **Layout** (`InputRow::resized` in MainWindow.cpp, `InputStrip::resized` in LiveMainWindow.cpp): the meter (10 px) and its scale (19 px,
  `fine`) are a column on the left from the top of the GAIN caption to the bottom of the lower area, on every strip; everything else is a
  column beside it (68 px - the controls were 72), so the strips went from 84 to 112 px (`kStripW`; nothing else measures a strip). The heights
  (`sizesFor`, `heightFor`) are unchanged, so the squeezing for short screens is as before. fermata: the lower area is HEADROOM (28 % of it)
  then the monitor keys two by two; GAIN is centred over the column (`gainCapArea`). fermata:Live: the lower area is the REC / PRE lamp, then
  HEADROOM; "no preamp control" above the lamp.
- **The peaks:** fermata - `AppContext::inHeld`, the max of `inPeaks` (the engine's sample peak per input since the previous read,
  `takeInputPeak`, 30 times a second) kept in the peak poller, so it holds whatever window is open; `resetHeldPeaks (input | -1)`.
  fermata:Live - `StationModel::inHeld`, the max of each "meters" event's peaks (the recorder's sample peak since its previous event, every
  50 ms while a window is connected); `resetHeld`.
- **The display:** `desk::PeakHold` (DeskLook.h, shared): `setPeak` every meter tick, `onReset` (a click), `onMenu` (a right-click; the strip's
  own right-click menu is told to leave it alone). `PeakHold::showPeak()` - headroom ("16.9 dB") or peak ("-16.9") - is one static for the
  program, kept as the user setting "inputPeakShown" ("peak" / "headroom") in each program's settings. Colours follow the meter's: red above
  `LevelMeter::kRedDb` (-2 dBFS), orange above `kOrangeDb` (-9); "AT 0 dBFS" from 0.99999.
- **RESET PEAKS:** fermata - on the controller row of the top row (`resetPeaksKey`, counted in `controllerRowWidth`); fermata:Live - beside
  the DRIVER INPUTS caption.
- **Tried on the rig** with trial-only copies of MainWindow.cpp / LiveMainWindow.cpp whose `hasPreamp()` is `idx < 4` (in the session's scratch
  folder, never in the repository). No audio device there: the readings were drawn offscreen for a range of peaks instead.

## 3.69.0: the keys can be changed (26 November 2026, Claude Opus; fermata:Live 0.10.16)

**What was asked.** "There is a hot keys option in the menu that lists the hot keys. Can we make that editable? And then have a save/export
hotkeys option? So multiple users can set and save their preferences?" On the questions: "fermata will default use the last loaded [hotkey]
file. It will store a copy in the fermata documents folder (near where the crash logs go). If there is no saved [hotkey] file then it reverts
to what is in there now (the default). Yes combinations are OK. It should also look for the same file that fermata uses and just work it in
the same way, importing the same file etc..." (the last about fermata:Live).

**The design: translate at the door, touch no window's key code.** About 150 keys are checked in some 30 `keyPressed` handlers, by key code,
by typed character, with their modifiers, and passed between windows. Rewriting each to ask a key map was the alternative; instead
(`src/ui/KeyMap.h`):
- The standard keys come from the HotKeys list itself (`src/ui/HotKeysLines.h`, moved out of HotKeysWindow.h, fermata's lines unchanged, plus
  fermata:Live's): each key of a line of a window's own jobs is a `Binding` (section, standard key); two lines of one section with the same key
  are one binding (Everywhere's Space plays and stops a recording). `editableLine` / `editableKey` say which: not Esc, Tab, the mouse, a box's
  keys ("In ..." lines), the questions' or the mixer's.
- `Translator` listens (a JUCE KeyListener, asked before the component's own keyPressed) on whatever has the keyboard - moved there by a
  FocusChangeListener - and on every top-level window (for when nothing in it has the keyboard). A key press is looked up in the lists that
  count where the keyboard is (`contextOf`: the nearest component marked with `KeyMap::setSection`, else one marked within two levels of a
  window; a window that forwards its keys - `ToolWindow::forwardKeys` - counts as `ToolWindow::lastTransportWindow()`'s contents; then
  `fallbackSection`, "Everywhere" in fermata). Found as another job's key now: the STANDARD key is made (`pressOf`: Windows' key code, the
  modifiers, the character it types - a British keyboard's for Shift) and handed to the window's peer again from the top, while the listener
  stands aside. Found as a standard key whose job moved: dropped. Not found: as it is. A text box being typed in (TextInputTarget active),
  anything modal (a message, a menu) and a component marked `setRaw` (the key catcher, a plug-in's window) are never touched. So every
  handler still sees only standard keys, and keys the program makes itself - the Stream Deck's, the control surface's (`keyToWorkWindow`),
  one window handing a key to another - never pass through it.
- **Keys watched on the keyboard itself** (not events): `AppContext::pollTransportKeys` (Space stops a recording from any window; R starts one)
  and the plug-in window's Space now watch `KeyMap::current ("Everywhere", ...)` with `KeyMap::isDown` (exact modifiers). R is `setPolled`:
  a key that does R's every-window job is not handed on as R (the edit window's R butts) - the watcher does the job. `rKeyHasOwnJobHere` now
  asks the window's list whether the key pressed is one of its own jobs (the take window's own R records, so it does not count).
- **Marked windows:** TakeWindowComponent, EditWindowComponent, TrimComponent, RepairDialog (shared), NoiseModeDialog, CurveDialog (Pitch),
  ReHarmoniserEditor, RetimerPanel, MasteringComponent, OrganiserComponent, TakeDisplayWindow; fermata:Live's LiveMainComponent and
  PrimaryTakeComponent ("the Primary's take window"), FixerComponent and KeysToFixer ("the Fixer"). A window not marked has only the
  Everywhere list. **Adding a key to a window:** add its line to HotKeysLines.h - that is the standard key and the binding. A new window
  with keys of its own: a section in HotKeysLines.h, `editableSection`, and `setSection` in its constructor.
- **The file:** `Documents\Fermata\Hot keys in use.fermatakeys` (both programs; written through a temporary file), only the keys that are not
  the standard ones, by name (`describe` / `parse`: JUCE's key codes differ between systems). Entries of sections a program does not know
  are kept (`foreign`). Both programs register BOTH lists, so either keeps the other's keys. `reloadIfChanged` every 2 s (the other program).
  Load keys... copies a file in; Save keys as... writes one and names the set after it; Back to the standard keys deletes the copy.
- **The window** (`src/ui/HotKeysWindow.h`, shared, `HotKeysComponent (bool live)`): the list draws each changeable key as it is now
  (orange edge: not the standard one), a click opens the bar (KeyCatcher takes any key; Esc gives up; Tab and Esc refused). Same window:
  swap or leave the other with none. Other windows with the same standard key and the same key now (same program): a list with tick boxes,
  ticked where the first three words of what it does match - `firstWords` - so Loop's L is offered in the take window and ReHarmoniSer but
  not ReTouch's L (room tone).
- **Tests:** `tests/keymap_test.cpp` + `tools/keymap-check.sh <JUCE>` (no screen; not one of the six checks - run it when KeyMap.h or
  HotKeysLines.h changes: it reads back every standard key of both lists). On the rig: see the history entry.
- **Not done / to know:** tooltips and labels that name keys ("Mixer [M]", "Note [N]" in fermata:Live, tooltips everywhere) still name the
  standard keys - only the take window's REC / STOP follow (each is a line of code when wanted). A Mac: "Ctrl" in the file is the command key.
  The race: a focus change is reported to the Translator a moment later (JUCE calls focus listeners asynchronously); a key pressed in that
  moment reaches the window untranslated unless nothing inside handles it first (the window's own listener then catches it).

## 3.68.0: video in the edit window (25 November 2026: part 1 with Claude Opus; part 2 - the decoder - with Claude Fable)

**What was asked.** 24 November: "Is it possible to be able to add video to a project? ... having the video in the project playing alongside,
synced up with the audio may help steer the mix ... I'd just need one video playing at a time (so 1 video track)"; parked while the user
looked into codecs. 25 November: "the next thing to add will be a video clip into the edit window ... .mp4 h264 video files ... the video
engine ... as light weight as possible, to cover that .mp4 h264 files and as many other formats as possible with not having to install
extra codecs but just rely on what a PC may already have installed as default ... I'd like to make a Mac version of this at a later date ...
so it can easily be done on a mac too, without the need to include extra codecs". On the plan: lining up - "First, the program should see if
there is a timecode sync available and if so, use it. If not, it will do it by sound. If it has no sound then just place it at 00:00:00.
The user should be able to move it by 'Unlocking' the sync and clicking and dragging it around"; the camera's sound "Can be switched on";
small pictures along the lane. Model choice (the user's): the lane, the window and the rest on Opus 5.5; the decoding and keeping the
picture in step on Fable High.

**The shape.** Nothing of the video is decoded by fermata's own code: `src/ui/VideoSource.h` is the one door to the computer's video
system for everything else of fermata. (As part 1 left it there was to be one file per system behind it; part 2 made that two layers -
see below.) Its comment is the contract.

**Part 1 (built, Opus):**
- Model (shared): `EditVideo` (`src/core/Edit.h`): file, start (seconds on the edit's timeline of the first frame), length, fps, size,
  `locked`, `synced` (how it was placed), the camera's sound (`sound`, a WAV in the project's "Video Sound" folder, its channels and rate),
  `soundOn` / `soundDb`. `EditDef::videos`, `showVideo`, `videoAt (t)` (the latest-starting clip covering t), `findVideo`. Saved in the edit's
  file as `"video"` (only when there are clips) and `"videoSound"` (on / level: a view key, never undone); `showVideo` a view key.
  Undo: the clips are part of it (the hash, and a missing file / sound kept from the live edit, as for pieces).
- Lining up (`src/core/VideoSync.h`, header only, core-tested): the timecode track of an MP4 / MOV (`tmcd`: read from the file's boxes -
  moov / trak / mdia / hdlr "tmcd" / minf / stbl / stsd + stco|co64 - nothing decoded; checked against ffmpeg's own files, 25 fps and 29.97
  DF); a Sony XML file beside the clip (`LtcChangeTable`, BCD "ffssmmhh" - from the known layout, not tried on a real Sony file); LTC on a
  channel of the camera's sound (`scanFileForLtc`). Placed against the edit's pieces' own timecode (each piece's take file on the LTC track,
  `videoexport::timecodeAt`): the piece whose timecode overlaps the clip's most decides. By sound: the camera's energy (high-passed at 200
  Hz) a thousand times a second, its onsets (log-energy rises) correlated with the edit's - the tracks marked Main for Time alignment, else
  up to six tracks - first at 100 Hz over every placement (FFT), then to the millisecond within 15 ms. Accepted when the best match is 8
  standard deviations above the rest and 1.25 x the next best more than 0.5 s away. On a made-up dull, echoing, noisy camera 40 s into
  an edit cut from two takes: 0.4 ms out; an unrelated sound: rejected (7.6 sd, 1.03 x). The camera hears the hall from where it stands:
  lined up by sound, its PICTURE is late by about 3 ms a metre the camera is further from the stage than the mics (the box says so).
  Else 0:00 - or, for a file added with others, straight after the one before it (a camera's split files run on).
- The lane (`EditTimeline` in `EditWindow.cpp`): under the tuning line, `video::kLaneH` (72 px). Small pictures (`video::Stills`: a thread,
  one picture every max(2 s, length / 1200), kept in Analysis/Video Pictures/*.fvpics as JPEGs, never made while a take records or is being
  finished); the camera's sound faint under them (the edit's own waveform drawing); a padlock - locked, a drag only says it is locked;
  unlocked, a drag moves it (one undo step; then "placed by hand"). A click: the playhead there. Right-click on a clip: lock / unlock, line
  it up again, a frame earlier / later, its start at the playhead, type its start, show / find the file, take it out. The name box:
  Picture, Sound, the menu (add, the picture, bring the picture here, the sound, hide the lane). The edit window's **Video** key: adds a
  video the first time, then opens / closes the picture; right-click: the lane's menu.
- Adding (`video::addVideos` / `AddDialog` in `Video.cpp`): any number of files (sorted by name); for each, on the job's thread: open, read
  its sound into the project (`Source::readSound`), timecode, sound, 0:00; then one undo step. Needs a saved project (the sound's folder).
- The picture (`Picture` in `Video.cpp`, window key "video:<edit id>", made by Main.cpp's `showWindow`): the frame at the edit's playhead
  (`playheadSecondsSmooth()` while this edit plays, else `EditDef::playheadSeconds` - part 2 made that the sound being heard: below),
  60 times a second, never waiting (`Source::frameAt`). Double-click: full screen on the screen it is on (`Desktop::setKioskModeComponent`), Esc leaves it. It goes back to
  where it was left on ANY screen (`place::ownPlace` - meant for a television or projector, as the Take Display's screens); the first time
  over the window clicked in; "Bring the picture here" fetches it (`AppContext::bringWindowOver`). Keys it does not use go on to the edit.
- Tried on the Linux build's screen with a trial-only stand-in decoder (ffmpeg, in the session's scratch folder, never in fermata): a 25 s
  H.264 MOV with timecode 10:00:00:00 and the trial edit's sound from 3 s on - its timecode could not be used (the trial project has no LTC
  track), lined up by sound at 2.9998 s, locked; the pictures along the lane; the picture following the playhead (frame 21 at 0.833 s in);
  a locked drag refused, unlocked and dragged 5 s, Ctrl + Z back; the clip menu; full screen and back; saved and opened again (the lane and
  its pictures came back from the Analysis folder).

**Part 2 (built, Fable, 26 November): the decoder, the picture in step, the camera's sound, a review of part 1.**

*The shape changed from the handover:* not one `VideoSourceWin.cpp` holding everything, but two layers, so that the part with the threads
is the same on every system and could be tested where it was written.
- `src/ui/VideoReader.h` (plain C++, no JUCE): what a system's own decoder must do, and nothing more - `PictureReader` (the next frame; copy
  it out as BGRA rows; seek to the key frame at or before a time) and `SoundReader` (the next block of interleaved floats, with its time),
  both blocking, without threads, used by one thread at a time; `ThreadUse` (COM on Windows) held by each thread that touches one.
  `VideoReaderWin.cpp` (Media Foundation), `VideoReaderNone.cpp`; a Mac needs only a `VideoReaderMac.mm` (AVAssetReader).
- `src/ui/VideoSource.cpp` (every system): `video::Source` over a reader. Its top comment is the design - read it first. In short: one
  decoder thread per Source and the message thread share, under a lock held only for moments, what is wanted (time, playing, size) and
  `shots` - the decoded frames ready, by time, ending where the decoder stands. The thread rests, steps or seeks; a frame on the way to the
  one wanted is not even copied out of the decoder (on a graphics card that is the costly part). `frameAt` only looks in `shots`.
  - Sizes: `wantSize` (the picture's pixels) picks a step of 180 / 270 / 360 / 540 / 720 / 1080 lines; the reader is opened again when the
    step changes and has stayed changed for 0.3 s. Never above 1080 lines (a 4K frame is 33 MB: nothing in a window for checking a mix
    is worth that; say so if the user wants more).
  - Kept: at most 96 MB of frames (6 to 40); standing, the 10 before the playhead and 2 after; playing, 0.35 s ahead.
  - Let go: after 10 s without `frameAt` the reader and the frames are freed (a camera's other files do not each hold a decoder); a Source
    that goes hands its thread to `retire` (it ends by itself when its last decode returns; `finishDecoders` joins them as fermata closes -
    `video::shutdown`). So neither `frameAt` nor closing the picture ever waits for a decoder: measured, 0.14 ms at most.
  - The first frame's time on the file's clock is taken as 0 (`t0`): frames and sound are both counted from it, so a file whose first
    frame is not at 0 (B-frames without an edit list) still lines its sound up with its picture. The stand-in and the test use 0.08 s.
  - Slow computer: while playing, frames behind the playhead are still shown if they come more than 60 ms apart; a second behind, it
    seeks to the key frame before the playhead. (Not done: seeking AHEAD to the next key frame, which would keep a slow machine nearer.)
  - A graphics card's decoder that fails in the middle: the reader is opened again without it, for that Source (`hardwareFailed`).
- `tests/video_source_test.cpp` + `tools/video-check.sh <JUCE> [plain|tsan|asan]`: the Source over a made-up decoder whose frames say which
  they are (a key frame every 12; decode time and failures can be set). Standing, stepping, playing in real time, a jump, a decoder too
  slow, sizes, the card switched off and failing, the small pictures, the sound's start / hole / early start, 180 Sources made and thrown
  away mid-decode. Passes plain, under ThreadSanitizer and under AddressSanitizer with no reports. NOT one of the six checks (nothing else
  touches these files): run it when VideoSource.cpp or VideoReader.h changes.

*The Windows reader (`src/ui/VideoReaderWin.cpp`) - NOT COMPILED ANYWHERE YET.* There are no Windows headers on the machine it was written
on. What stands in for a compiler: `tools/win-stubs/mf/` (stand-ins for the few declarations it uses, written from memory of the SDK - they
prove the file's grammar and that it uses each call the same way throughout, not that the SDK agrees), in `tools/check-windows-code.sh`,
with and without `FERMATA_VIDEO_D3D`; the signatures of `MFCreateDXGIDeviceManager`, `MFGetStrideForBitmapInfoHeader`, what
`MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING` does with an output type's frame size, and the top-row rule for `Lock` / `Lock2D` were
read again on learn.microsoft.com. And CMakeLists.txt tries the file before using it (`try_compile`, as for the PDF reader): with the
graphics card's decoder, then with `FERMATA_VIDEO_D3D=0`, then `VideoReaderNone.cpp` - writing the compiler's output to
`video-reader-did-not-build.txt`, which the workflow copies beside Fermata.exe. **If the user sends that file: fix exactly what it says.**
- IMFSourceReader, synchronous. Output RGB32 at the size asked (so Windows resizes, on the card when it decodes there). Three ways, tried
  in order when a file is opened, each proved by decoding the first frame: 0 = D3D11 device manager + hardware transforms + advanced video
  processing; 1 = advanced video processing only; 2 = the old `ENABLE_VIDEO_PROCESSING` (no resizing: frames at the file's size).
- Nothing of Media Foundation is imported by name: `mfplat.dll`, `mfreadwrite.dll` and `d3d11.dll` are loaded when the first video is
  opened and the five or seven functions found with GetProcAddress (their pointer types are `decltype (&::MFStartup)` ..., so the real
  headers still check the calls). Reason: an "N" edition of Windows has no mfplat.dll, and a program that imports it does not start at
  all. check-windows-code.sh fails if the file ever calls one by name. Media Foundation is started once and never shut down.
- One D3D11 device for all readers (video support, multithread protected), never freed.
- Rows are read through `IMF2DBuffer::Lock2D` (top row and pitch, whichever way up; a frame on the card comes into memory there), else
  `Lock` with the format's default stride (below 0: bottom row first). The display aperture is honoured (1080 of 1088 lines).
- Sound: a second Source Reader, the first sound stream as float (else PCM of 8 / 16 / 24 / 32 bits, converted). Its first block is read
  as it opens, because some sound (HE-AAC) only shows its true rate once decoding has begun.
- Unknowns for the first real try, in the order I would look: (1) does it compile (the txt file); (2) is the picture the right way up
  and the right colours on way 0 (untick "Decode on the graphics card" to compare with way 1); (3) the sound of the videographer's files -
  AAC should be fine; PCM in MOV / XAVC S ("twos", "in24") is whatever Media Foundation's MP4 reader makes of it; (4) a file whose first
  frame is not at 0; (5) `MF_MT_VIDEO_ROTATION`: a phone's upright clip is shown as stored (the log line of the open says "turned by 90");
  (6) kiosk full screen: JUCE gives the window a JUCE title bar for the duration on Windows - `ToolWindow::isKiosk` hides the wooden
  sides and the corner buttons and stops `keepTitleBarClear` / `reportBounds`; tried only on Linux.

*The picture in step (`Picture::timerCallback` in `Video.cpp`).* Standing: the frame at `EditDef::playheadSeconds`. Playing:
`playheadSecondsSmooth() - AudioEngine::heardDelaySeconds (edit) + 25 ms + the user's "videoPictureMs"`. heardDelaySeconds = the edit's
mixer's latency (`MixerPlan::latency`) + the driver's output latency + the live converter's delay: the session's position is the sound
being MADE; the picture is for the sound being HEARD. 25 ms is a guess at a computer screen (paint, the next refresh, the panel); a
television is the user's figure. Nothing measures either: if the user reports the picture early or late by a steady amount, that constant
is where to look. The clip that comes next within 2 s is opened and its first frames decoded while the one before still plays.

*The camera's sound (shared core).* Not a new player: one more track in the edit's own `PlaybackSession`, beyond the project's tracks
(`monitorTrack`), so it loops, jumps and starts exactly as the edit does. `cameraSoundSegments` places each clip's WAV where its picture
shows (`videoAt`), `late` by the edit's mixer's latency; `CameraSoundReader` (Playback.cpp) gives two channels at the session's rate from
any file - a four-point curve, stateless, on the disk thread; the engine adds the track to the first Ext Bus outputs of the mixer the edit
plays through, after the mixers, at `monitorGain` (0 / 1: the Sound key; it glides over a block). It is in every play of an edit with video
(silent until switched on), so the key works while playing. Never in `segmentsForEdit`, so never in a bounce. `EditVideo::soundLtc`: a
channel of it that is timecode - found when the clip is added, whichever way it was lined up - is neither compared nor heard.

*Review of part 1.* Changed: `video::open` no longer runs on the message thread (the picture uses `openForPicture`); LTC on a channel of the
camera's sound is now looked for even when the file has a timecode track of its own (it was then compared as if it were music); the small
pictures' thread is stopped with `stopThreadOrLog (30 s)` (was 6 s, then killed - inside a decoder); no black flash between two clips
(the last frame stays 150 ms; "Reading the video..." only after 0.6 s); a frame's own shape is drawn (non-square pixels). Looked at and
left: `EditTimeline` asks each clip's file `existsAsFile()` every 2 s on the message thread (a sleeping drive would be felt; the same is
true of missing audio); a clip moved while playing keeps its old sound until the next Play.

## 3.67.0: the pass with Fable over what was built on Opus since 3.55.0 (25 November 2026, with Claude Fable)

**What was asked** (25 November 2026): "we have fable usage. Can you please check and build everything/finish anything off that needed
Fable to look at that we have left." The list kept for this (memory: the Fable list): MTC / LTC timing (3.56.0), Int-bus solo (3.58.0),
the recording tap `AudioEngine::LiveFeed` and `LiveWaveColours` (3.59.0), the tuning line's threads (3.63.0), window placement (3.64.0),
then everything else built on Opus since 3.55.0 (fermata 3.56.0 - 3.66.0, fermata:Live 0.10.6 - 0.10.12). The items "to build or tune
with Fable" (take matching, PanNoir-style spot delay, the ReHarmoniser's tuning, direct network input) wait for the user's material or
decision and were not touched.

**What was read, and how.** Line by line: `Mtc.h`, `Ltc.*`, the whole `AudioEngine` diff since 3.55.0 (the tap, the solo, MTC),
`LiveWaveColours.h`, `TuningLine.h` and its wiring in `AppContext`, `WindowPlace.h`, the `Project.cpp` diff, `AudioGather.h`,
`TrackLayout.h`, `StripOrder.h`, the `StereoJoin.h` and `ImportPlan.*` diffs, `WaveColor.h`'s diff, `MeterBridge.h`, `ExportDone.h`,
fermata:Live's diff, and the `FixTools.cpp` diff (the import laid out like a window, Move onto a window's tracks, Import source tracks,
the AAF's one track one row). NOT line by line: the diffs of `TakeWindow.cpp`, `MixerWindow.cpp`, `EditWindow.cpp`, `MainWindow.cpp`,
`ScoreWindow.h` and `AppContext.cpp` beyond what the above touches - those were searched (every line added since 3.55.0) for callbacks
that could outlive their window (`callAsync`, `callAfterDelay`, menu and box callbacks capturing `this` without a guard: only
AppContext's own, which lives as long as the program, and three guarded by an `alive` flag), for `juce::Uuid()` meant as null, for static
JUCE objects, and for the nested-lambda capture that stopped the Windows build (0.10.13); and both programs build with no warnings.

### MIDI timecode: the timing (`src/core/Mtc.h`, `Ltc.*`, `AudioEngine`)

What was wrong or rough, and what is there now.

1. **A missed read was taken for "no timecode"** (a real fault). `MtcReader::ref()` tried its seqlock eight times and then returned an
   empty reference; `MtcChase::fill` then stopped the generator and wrote a block of silence, and `MtcOutput`'s timer set `sending =
   false` (a full-frame locate and a fresh start). Eight tries pass in well under 100 ns, so the read fails whenever the writer is put
   aside by the system in the middle of its five stores - seldom (my reckoning: once in some hours for timecode coming in, the MIDI thread
   being an ordinary one; not measured), but in a take. Now `MtcReader::read (Ref&)` says whether it read, `MtcChase` keeps the last
   reference it read (good for the 0.25 s that timecode counts as arriving anyway), and the timer simply looks again a millisecond
   later. `ref()` stays for the message thread and the tests (it tries until it has it). The test hammers a writer against 400,000
   reads: 1,500 - 64,000 of them cannot be read at once (a writer going flat out is far worse than 100 messages a second), every one
   that is read is whole (`tc - start == at - t0`), and a read that fails leaves the caller's reference untouched.
2. **The seqlocks lacked a release fence** after the writer's first increment (`MtcReader::publish`, `MtcOutput::stamp`). The readers
   had their acquire fence. Without the writer's, the language allows a reader to see new values and still the old count (the stores
   are relaxed and nothing ties them to the increment); on x86 it cannot happen, on ARM it can. Added (free on x86).
3. **23.976 / 29.97 NDF against 24 / 30**: decided from the frames' speed since the run began, from six seconds on, with one threshold
   half way - so one message 3 ms late just after the six seconds flipped the format for a message, and `MtcChase` set the generator
   again at each flip. Now it changes its mind only when eight messages in a row (two frames) are past three quarters of the way to the
   other speed (0.075 % and 0.025 % below nominal). A run that starts 23.976 is still made as 24 for the first six seconds: that cannot
   be known sooner from MTC.
4. **Where the block is in time.** (a) `mtc::BlockClock`: the time read at each block is steadied by the audio's own clock (each block
   expected one block's length after the last; the reading pulls it with a time constant of about a second; a block more than 20 ms +
   half a block from where it was expected starts it again - a dropout, the device away). A first-order loop: a PC clock and an audio
   clock 0.01 % apart sit 0.1 ms apart in it, which is nothing here. With the sample-rate converter the engine's blocks come in bursts
   and it mostly degenerates to the plain reading (recording is not possible like that anyway). (b) The block's first sample is as old
   as the driver's input latency (`AudioIODevice::getInputLatencyInSamples`, passed straight from ASIO's `getLatencies` by JUCE: "the
   age of the first sample of the block when the callback comes", so it includes the block) - never taken as less than the block's own
   length, which is what a driver that says 0 gets (and what 3.56.0 used for all). `AudioEngine::setDeviceLatencies`, from
   `audioDeviceAboutToStart`; 0 when prepared without a device (the tests). Logged at device start. (c) MTC out is stamped with the
   moment the block's first sample is HEARD (the output latency, and the converter's delay when there is one), so MTC out and LTC on an
   output channel agree. At a start from 00:00:00:00 the first few milliseconds of MTC therefore say 23:59:59:xx (where the "heard"
   position is before the generator's first sample is out), then a locate to 00:00:00:00 - harmless, left as it is.
5. **Following, not standing still** (`MtcChase`). `LtcGenerator::setSpeed` (a multiplier on the bit clock; 1 after `start` /
   `startExact`, and then every sample is bit-for-bit as before - tested). The error against the reference is averaged over about half
   a second (`kAverage`; one block counts for 20 ms at most, so one message that came late with a long block cannot look like a jump),
   and the speed is 1 + average / 8 s (`kPull`), held within 0.02 % (`kMaxTrim`). A proportional loop: against a source that drifts by D
   it sits D x 8 s behind (0.4 ms at 0.005 %); an integral term would take that out and was not thought worth its risk. With
   `kPull = 16 x kAverage` it cannot swing past (damping about 2). Set again (`startExact`, a break in the code) when the error is over
   half a second at once, or the average is over 5 ms (`kJump`) - a jump of more than 20 ms is followed after about 0.15 s. 0.02 % is a
   fifth of the way from 24 to 23.976, and `analyseLtc` takes the middle of the frame lengths of the whole take, so a trimmed take still
   reads as its own rate (tested: a minute at the full trim reads 25, no jumps).
6. **The offset** (`AudioEngine::setLtcMidiInOffset`, fermata's user setting `mtcInOffsetMs`, `AppContext::mtcInOffsetMs`). What is left
   once the driver's latency is allowed for: how late the MIDI arrives (the Hapi's own reading of the LTC, the network, rtpMIDI). Kept
   with the computer, not the project: it is a property of the network, and a project file is not touched. **Measuring it**
   (`LtcCompare`, `AudioEngine::setLtcCompareInput` / `ltcCompareResult`, `MainComponent::measureMtcOffset`): the same timecode as MIDI and
   as LTC on an audio input; the audio thread decodes that input (an `LtcDecoder` that keeps no frames) and, for every frame that follows
   the one before it by the right number of samples, compares the frame's own time with where the chase generator was at the sample the
   frame began (the generator's place at the block's first sample, carried back). Mean and spread over 8 s (about 200 frames at 25);
   the box offers "offset now + mean". The LTC on the audio input goes through the same converters as all the audio, so it IS the right
   answer; what the measurement finds is the MIDI's lateness less any error in the driver's stated input latency - which is exactly
   what the offset has to make up. Under 50 frames, or more than 100 ms apart, nothing is offered.
7. **Ports.** `isOpen()` was a read of the `unique_ptr` from the audio thread while the message thread could reset it: now an atomic flag.
   A port open here but gone from the system's list (looked at every 3 s while a MIDI timecode port is chosen) is closed after it was
   missing twice running - and never while timecode still arrives on it - so that the retry there already was opens it afresh.

**Not built, on purpose: an offset for MIDI timecode going OUT.** The generator behind LTC OUT runs free (the time of day, or from a start
time): nothing in fermata ties its sound to that timecode, so a few milliseconds either way at the Hapi's LTC output change nothing fermata
records. What is recorded with a take is the LTC IN side, which has its offset. An OUT offset would matter only if fermata's own LTC
track were fed from its own generator inside the computer while cameras took the Hapi's LTC output; if that is ever the set-up,
`LtcCompare` can measure it the same way (the Hapi's LTC out patched to an audio input, compared with `ltcGen` less the two latencies).

**Still not checked:** any real MIDI port, rtpMIDI, the Hapi. Every figure above is from tests with made-up messages. The first run on
the real thing should look at: the log line with the driver's latencies; whether the lamp stays lit; the measured offset and its spread
(under a millisecond expected on a wired network); and the LTC track's timecode against LTC on an audio input over a long take.

### The solo of an Int Bus (3.58.0) - checked, unchanged

`processMixer`: `soloSelf` (soloed itself, or by a soloed VCA), `soloHeard` (feeds a soloed channel: the backward pass over the order),
`soloAfter` (a soloed channel and what it feeds: the forward pass); a strip is audible when any is set; a path is open out of an 'after'
channel, or into a soloed or 'heard' one. Worked through: a soloed bus with tracks sent to it (heard through it only, not on their own
outputs or other sends); a chain T -> B1 -> B2 with B2 soloed, and with B1 soloed; a soloed track with a send to a reverb bus that other
tracks also feed (the bus carries the soloed track only); a muted send (keeps nothing open); a bus with a generator in it but nothing
soloed feeding it (its way out is shut); VCA solo. All right. Two tests added to the mixer section of the core tests: the chain (the
output is exactly what the chain alone gives, to 1e-6, and the Foldback send is silent), and the soloed track's bus (exactly the output
with the other track muted).

### The recording tap and the live colours (3.59.0) - checked, unchanged

`AudioEngine::LiveFeed`: one writer at a time (the audio thread; at Stop the message thread once the audio thread has left the take),
`begun` before and `count` after each write, the reader checks both sides of its copy: a seqlock over a ring, with its fences in the
right places. Nothing in it allocates or waits; the ring's pages are touched when it is prepared. All three places that write a take's
file feed it. `LiveWaveColours`: reads only; a take's job leaves through `takeReady` after `handOver`; every finished take is reaped
through `takeInFinishedTakes`. Nothing to change. (One thing beside it: `WaveColorCache`'s workers held off only while a take recorded;
they now also hold off until its files are finished - `AppContext::takesBusy`, below.)

### The tuning line (3.63.0) - four faults in `Store` and `Live`

- **The race at Stop.** `tuningOf` asks `Store::get` for a take as soon as it is drawn; for the take that is recording that is usually
  before the take window's timer has handed its feeds to `Live`, so `Store` holds a job for the recording take, going round until
  `holdOff` (was: `engine.isRecording()`) lets go. At Stop that is at once - while the engine's finisher is still writing the last of
  the sound and the headers. `measureFiles` then read files that were too short (or said 0 samples), and when it finished after
  `Live`'s `put`, `finish` put that line over the live one and `saveLine` wrote it to the Analysis folder. Shown with the trial
  (`tune.cpp`, "the live line comes while the take's files are being measured") against 3.66.0's header: the live line was lost; with
  this build it stands. Mended three ways, each enough alone: `holdOff` is `engine.isRecording() || takesBusy` (`AppContext::takesBusy`,
  an atomic the 100 ms watcher keeps: recording, or `engine.anyFinishing()` - which only the message thread may ask); `put` drops every
  waiting job of its take; and `finish` takes a line only for an entry that is still pending with the same key (`wanted`), and the file
  is written only after `finish` took it.
- **A take with files away** (`measureFiles`' `allRead`): not written to the folder, `whole = false`, so it is measured again next time.
- **`held`**: jobs that cannot be measured yet wait in a second queue, put back all together each second; stored lines are read at once.
  A job remembers that the folder was looked in (`looked`), so held jobs do not ask the disk again every second beside a recording.
- **Joining late / catching up** (`Meter::startAt`, `Meter::skip`, `Decimator::skip`): `Live::watch` on a take whose feeds have already
  wrapped starts at `written - capacity / 2` (rounded up to the decimator's step) with the line marked not whole; a reader that fell
  behind counts the lost stretch as silence instead of filtering it. `watchTuning` returns at once when the take is already watched.
- **The edit window's lane** (`EditTimeline::tuningSegs`, found while reading the window code): it ran at every repaint that touched the
  lane, and for every piece found its take (a search of every take window), built the take's source key (`tuning::sourceOf`: the files'
  paths sorted, joined and hashed) and asked `Store::get` - and for an edit with pieces that name no take (an AAF import) first made a
  map of every take file in the project. Now the pieces' places are worked out first and the rest is done only for the pieces the
  clip touches, and from the start of the edit as far as the first piece with a reading (the reference Relative needs - so the values
  shown are the same). The AAF map is kept (path -> take id, not a pointer) until the project next changes. The tooltip still looks
  up all of them (it runs when the mouse rests). Checked on the trial's edit: the lane's picture before and after is the same to the
  pixel, and stays right after narrow redraws (the cursor moved about inside the second piece, whose values hang on the first's).

### Window placement (3.64.0)

`Tracker::check` asked `anchor()` for the window to move a new box over; with `last` empty (no click yet, or the clicked window gone)
that fell to `TopLevelWindow::getActiveTopLevelWindow()`, which by then is the new box itself, and `anchorNow == c` left it where it
was. `anchor (hint, notThis)` now skips the window asked about, so it goes on to the main window. The "please wait" window
(`busy::setAreaProvider` in `Main.cpp`) is given the part of the anchor that is on its screen. Many boxes still pass no window to JUCE
(`AlertWindow::showAsync` without `withAssociatedComponent`): they open in the middle of Windows' main screen and the net moves them when
the anchor is on another - as designed in 3.64.0; with the mend above that now holds in every case.

### Smaller things

- `FixTools.cpp` `finishImport`: a one-sided stereo part made from the RIGHT file (`ImportPart::kOneSide`, `sourceRight`) kept the name in
  `TakeFile::sourceName`; now `sourceName2`, as `stereojoin::applyInto` does. `Project::relinkAudio`: "recorded here" needs both names empty.
- Read and found right, nothing changed: `AudioGather.h` (the copy is checked by CRC before it gets its name; a cancel removes what was
  made), `TrackLayout.h`, `StripOrder.h`, `StereoJoin.h`'s `planInto` / `applyInto`, `ImportPlan.cpp`'s one-sided join, `WaveColor.h`'s
  queue and workers and the knock weighting, `MeterBridge.h`, `ExportDone.h`.

**Checked here:** the core tests (new: "3.67.0 MIDI timecode, the timing" and the two solo cases), the tuning trial (`tune.cpp` in the
session's scratch folder, not in the repository) with its new cases, both programs built and linked with no warnings, on the Linux
build's screen the LTC IN menu, the offset box (a wrong entry refused, 3.4 kept and shown, saved in the settings) and a measurement
with nothing to measure (the 8 s box, then "Nothing could be compared"), and the six pre-delivery checks. Also `tools/tool-checks.sh`
`rtcheck` and `tsan` on the engine (the core tests and the stress test): 0 realtime reports, 0 thread reports. `asan`: the stress, time
format and control-port tests passed with 0 reports, but `tests/core_test.cpp` could not be compiled with `-fsanitize=address,undefined`
on this machine any more (cc1plus was killed at 6 GB - the file is 16,600 lines); instead the two MIDI timecode sections were built on
their own with the same flags, and the tuning trial too: 0 reports. To do when there is a bigger machine, or by splitting the test
file: the whole core tests under the address check. (The realtime check does not reach the MIDI timecode's branch of `process()` - no MIDI port in a test - so that branch
was read for it instead: atomics, the generator, a decoder that keeps no frames; nothing allocates.) One run of the stress test
aborted ("munmap_chunk(): invalid pointer") - a header was edited while the check was building, so half of it was built against the old
layout; the run on the finished tree passed. **Not checked:** see above for MIDI; the box that offers a measured offset (it needs a
real measurement); the window-placement mend on a screen (the Linux rig does not make a new box the active window as Windows does, so
it was checked by its logic: `anchor (nullptr, box)` gives the main window); Windows.

## 3.66.0: an edit's pages of its score (24 November 2026, with Claude Opus)

Asked: one PDF with four movements, each movement its own take and edit window - "tell fermata which pages of the PDF that edit and take
window is referring to". The score belongs to the edit (EditDef::score, 3.51.0), so the pages do too; the take windows tied to the edit get
the brackets and the bars in the piece from it as before.
- *What was there.* Project::addScore copies a PDF once (the same file chosen for four edits is one file in PDFs/), so the four edits share one
  `.bars.json`: the bars are found once and counted through the whole PDF (printed numbers can restart the count; "This bar's number is..."
  sets it). `Map::editStarts` (3.54.0, per edit id, in the bars file) marks where an edit's music begins; `startOrdinal` found it, else bar 0.
- *Shared.* `EditDef::scoreFirstPage` / `scoreLastPage` (1-based, 0 = open end). Saved inside the score's keys only when set; read with
  limits (and swapped if the wrong way round); hashed; view keys (`kEditViewKeys`) and kept from the live edit by an undo (`restorePiece`), as
  scoreTop / scoreZoom. `addScore` sets them to 0 when the edit's PDF name changes (the same PDF chosen again keeps them). Core test: kept
  when the same PDF is chosen, cleared by another, saved and read back.
- *The window* (ScoreWindow.h). `firstPage` / `lastPage` / `pageShown` (0-based, clamped to the PDF). `layoutPages` gives a page outside them an
  EMPTY rect at the current y (no room); every place that walks `rects` skips empty ones (paint, `topNow`, `wantPictures` and its `add`) or can
  only meet shown pages (`pageAt` - an empty rect contains nothing; `setTop` / `goToPage` / Home / End clamp to the range). `scaleNow` (fit
  width) and the width use the shown pages only. `startFrom()`: the marked start (`editStarts`) if there is one, else the first bar whose page is
  >= firstPage - used by `refreshMarks` / `brackets` and `givePieceBars`; the piece's length is `pieceLength (barsToLastPage(), startFrom())`
  (bars after the last page cut off). `askPages` (the Pages key; and once, from `opened`, when another edit has this PDF and this one has no
  pages - suggesting after the highest last page of the others): two text boxes and a tick box (`addCustomComponent`, name cleared - the box
  writes a custom part's name above it), "Use these pages" / "All the pages" / "Cancel". `setPages`: from <= 1 and to >= n is "all"; a marked
  start outside the pages is erased; "Bar 1 ..." (`barOneHere`): `Map::setNumber (.., 1)` on the first bar of the first page, at once if that
  page's bars are known, else when they are found (`barOneOnPage`, checked in `barsFound`). `pagesChanged` (also from the timer when
  `rangeKey()` differs from what was laid out: Ctrl + Z, another window) lays out, clamps the top, and works the marks and piece length out
  again. Right-click on a page (`addPageItems`, ids 20 - 23; also before the page's bars are found). The marked PDF: `ExportJob::first / last`,
  `runExport` writes those pages only (the fills of the others are empty).
- *A crash found in the trial.* `updateLabel` (called by `say` in `opened` before `layoutPages`) read `rects[page]` with the guard changed to
  `page + 1 <= lastPage()` - `rects` was still empty. The guard is `page + 1 < rects.size()` again, as well.
- **Checked** (Linux trial build, Poppler for the pages, `pagestest/Suite.pdf` - four of the generated test scores, 12 pages, given to both
  trial edits): the question on opening (screenshot), pages 4-6 (only those; End / Page Down stop at 6, Home / Page Up at 4; "the first bar
  of page 4 is bar 1 (it was counted 39)"; the take window given 38 bars, was 152), the second edit asked with 7-12 suggested, the keys saved
  (`"scoreFirstPage": 4, "scoreLastPage": 6`) and the window opening on them again, take 1 (bars 2-5) bracketed on page 4, the right-click
  items, the marked PDF with 3 pages. Run again for the shared change: compat-check
  (PASSED), sandbox-check (ALL TESTS PASSED, with the new checks), syntax-check-ui (no messages), check-windows-code (OK); fermata:Live's
  recorder tests were not run again (no recorder code changed). Not checked: Windows.

## 3.65.0: the take window's marks line - all the room, the lengths, a hover box (24 November 2026, with Claude Opus)

Asked with a screenshot: the line "EDIT 002 - Cathedral 01 - W..." cut short with empty space to its right; wanted the take's length and the
Mark 1 to Mark 2 length (which was there, "(37.12 s)", in the part cut off).
- *Why it was cut.* `TakeWindowComponent::resized`: `markLabel` runs to the window's edge only when `buttonsBottom <= y - 2` (the buttons beside
  the readout end above its line), else to `clock.getRight()`. `buttonsBottom` was `r.getY() + flowH`, and `grid::flow` makes each line
  `max (rowH, h + 8)` tall - 4 px below each 26 px button - so with two lines of buttons the line seemed taken by 2 - 4 px. Now `buttonsBottom` is
  the lowest `getBottom()` of the visible flowed buttons themselves.
- *The text* (in the timer that writes it): `(LENGTH long)` after the take's name (`TakeGroup::lengthSeconds`, `formatTimeShort (.., 3)`), and
  `(1 to 2: MM:SS.mmm)` in place of `(N.NN s)`; the playhead form gets the length too.
- *The hover box*: `markLabel.setTooltip` (set only when it changes): the take's name, "Take length", the marks, "Mark 1 to Mark 2", and
  "Selected: NAME - LENGTH long" for up to six other selected takes (`timeline->selectedTakes`, "... and N more").
- **Checked**: Linux trial build (`placetest/`, the stand-in libXinerama with `FAKEXIN_ONE=1` for one 2880 px screen, the take window at
  1900 px): the line in full beyond the readout, the hover box with a second selected take; at 1420 px (three lines of buttons) the line still stops
  at the readout. Not checked: Windows.

## 3.64.0: windows open over the window you clicked in; Move clips asks what happens to the track (24 November 2026, with Claude Opus)

**What was asked** (24 November 2026): a fermata 3.55.0 log sent because "Make a stereo track" seemed to freeze. The log showed no stall (the
stall watch would have written every thread's stack after 5 s, as it did for the MAD ASIO driver opening at start-up; the main thread was at 2 -
4 % of a core). The user then found the box on the second screen: the PC's HDMI output is always plugged into a fibre distribution box, so Windows
sees two screens even when nothing shows the second. Asked: "WITH THE EXCEPTION of the 'Take Display' screen ... all the windows open over the
window you clicked in"; and the move-clips choice (keep / remove from this take window / delete from the project, the last answer the default).

**(1) Window placement** (`src/ui/WindowPlace.h`, fermata only).
- *The cause.* `TopLevelWindow::centreAroundComponent (c, w, h)` (JUCE 8.0.9) centres on `c->localPointToGlobal (c->getLocalBounds().getCentre())`
  and keeps the box within `c->getParentMonitorArea()` - `Displays::getDisplayForRect (c->getScreenBounds())`, the display with most of `c`. The
  stereo box was given the TakeTimeline: the component inside the viewport, as wide as the whole timeline (`updateSize`: the material and as much
  again, at least 10 minutes, times pixels per second). Its middle is far right of the window and most of it lies on a screen to the right. Every
  box given a timeline (or any big inner component) could do the same. Second: `Component::centreWithSize` with no parent uses
  `getPrimaryDisplay()` - new tool windows, Audio settings and plug-in windows opened in the middle of Windows' main screen.
- *Tracker* (a singleton started in `initialise` after the main window, stopped in `shutdown` before it goes): a global mouse listener keeps
  the window last clicked in (`mouseDown`: the event component's top-level component if it is a `TopLevelWindow`; the menu bar - `MenuBarStrip`,
  marked `place::partOfMain` - stands for the main window; a menu, call-out, tooltip or Take Display screen - marked `place::ownPlace` - changes
  nothing). `place::anchor (hint)`: the TopLevelWindow `hint` is in, else the last clicked, else `getActiveTopLevelWindow()`, else the main
  window - each only if showing and not minimised.
- *At the source.* Every `new juce::AlertWindow`, `showMessageBoxAsync`, `withAssociatedComponent`, `componentToCentreAround` in fermata's own
  files now passes `place::anchor (x)` (or `place::anchor()` where none was given), so JUCE centres on the window, not the component.
  `fixtools::launchDialog` (every fix tool's dialog, and the ReTouch window's small dialogs through `openDialogHook`) and `say` likewise; a
  resizable dialog's remembered place (`uiPrefs` "win.<title>") is used only when `place::sameScreen` with the anchor, else its size centred over
  it. `showWindow` (Main.cpp: every ToolWindow) uses the project's remembered bounds only on the anchor's screen, else the remembered size (within
  the screen) and `place::over` (centred on the part of the anchor on its screen, all of it on that screen, room for the native title bar).
  The meter bridge and recording monitor's first places are the anchor screen's top left (they were 20,20 / 40,60 - the main screen's).
  Audio settings and plug-in editor windows: `place::over`. The two talkback NativeMessageBoxes are AlertWindows now (native ones are placed by
  Windows). The shared files (`Uikit.h`'s `showError` / `confirmAsync`, `MarkerStrip.h`, `SpectralWindow.h`) are not changed: their boxes have no
  component, so JUCE centres them on the active window - and the net below covers them.
- *The net.* The Tracker's timer (20 Hz) and a focus-change listener (a new box takes the keyboard: looked at at once) look at every desktop
  component not marked `kSeen` yet: a visible `TopLevelWindow` (boxes, dialogs, document windows - not menus, call-outs, tooltips, the splash,
  the menu bar or the Take Display screens, which are plain Components) other than the main window and the anchor, not full screen or minimised,
  whose screen (`getDisplayForRect`) is not the anchor's, is moved over the anchor, with a log line "Window placement: 'X' opened on another
  screen - moved over 'Y'". One that opened on the anchor's screen is never moved (no jump). The mark is a property on the window itself, so a
  new window at a freed window's address is still new.
- *Not changed.* The main window's own placing; the Display Organiser (it places open windows on the main window's screen on purpose); Take
  Display screens (`TakeDisplayHub::setOpen`, on the display chosen); call-outs (they point at a button); Windows' native file choosers (JUCE
  owns them with a zero-size window on the primary display; Windows places the dialog).

**(2) Move clips** (TakeWindow.cpp). `ThenTrackChoice` (three radio ToggleButtons in the AlertWindow, `addCustomComponent` - not owned by the
box: deleted after it). `askMoveClips (from, to)` (the track menu's "Move its N clips onto X..." and the "onto another track" submenu, which
called `moveClips` at once before): checks `canMoveTakeClips` first, asks, remembers the answer (user settings "moveClipsThen" 0 / 1 / 2), moves
(`moveClips`, which now hands back its status text), then `applyThen`. `moveAllByName` asks the same in its one box (it asked to delete the
emptied tracks afterwards). `applyThen`: a track that still has a clip in this window is skipped (removing it would take the clip out);
erase = `removeEmptyTracks` for those with no audio anywhere (`trackHasAudio`), the others `removeTrackClips` (with no clips it only hides the
track); remove = `removeTrackClips`. One undo step (the move: hidden tracks are view state). Drags (`moveTakes`) are unchanged.
- *Ctrl + Z.* `restorePiece` (Project.cpp) already does the right thing: clips whose track was deleted since stay where they are (so after
  Delete the move cannot be undone - said in the box and on the status line, which no longer says "Ctrl + Z puts them back" then), and a track
  whose clips come back is unhidden (Remove: Ctrl + Z brings the row back - tried). A first version of this build also brought deleted tracks back
  after an undo (`AppContext::mendTracksAfterUndo`); the trial showed `restorePiece` never puts clips on a deleted track, so it could not act,
  and it was taken out. Bringing a deleted track back with Ctrl + Z would be a change to `restorePiece` (shared core): not done.

**Checked** (Linux, JUCE 8.0.9, trial builds only): fermata compiles and links (every `.o.log` clean); fermata:Live unchanged (no shared file
changed). Two screens: the trial JUCE build had `JUCE_USE_XINERAMA=0` and Xvfb's `+xinerama` puts both screens at 0,0, so a scratch
`juce_gui_basics` with Xinerama on (a stand-in `Xinerama.h` - libxinerama-dev is not installed - JUCE loads the library itself) and a stand-in
`libXinerama.so.1` reporting one 2880x900 Xvfb screen as two 1440x900 screens side by side (`placetest/`). `placetest/why.cpp` (JUCE alone): a
window at 100,100 1100x500 on the left screen with a 9000 px wide timeline in a Viewport - an AlertWindow given the timeline opens at
2518,252 (the right screen), given the window at 475,275. `placetest/net.cpp`: the Tracker moves the first over the window within a tick (and logs
it), and leaves a box opened on the window's own screen where it is. fermata itself: the stereo box over the take window (408,261, its middle the
window's); the take window remembered at 1600,200 1100x500 opened at 170,200 1100x500 over the main window; the move box (screenshots), its
remembered answer, Move + Delete, Move + Remove + Ctrl + Z. Not checked: Windows (a real second screen, the fibre box, MSVC); plug-in windows
(no plug-ins on Linux); the shared `showError` / `confirmAsync` boxes on Windows (they rely on the active window, and the net).

## 3.63.0: the tuning line; Import AAF - one Pyramix track, one row (23 November 2026, with Claude Opus; a Fable pass to follow)

**What was asked** (23 November 2026): a tuning line live while a take records, kept for viewing after and while editing; show / hide;
"Absolute" (A = 440 or any Hz) or "Relative" (to the starting pitch). "Build it all now with Opus, and then we will do a pass with Fable later
to stabilise it all." And, after an AAF imported edit showed a single, nearly empty row "instead of like 20" (its `.fmedit` had 16 tracks with
audio in 19 of its 21 pieces but `trackIds` held only one, a track named "Ice Shining 018 20 {01A177F8 FF08 482C A27E 575AA191E6C0}" carrying two
pieces whose file was take 034's "Ch 1" - a Pyramix render of a pitch correction / CEDAR ReTouch with a name of its own, which the media
import had taken in as a take on its own track): fix the import.

**(1) The tuning line** (`src/ui/TuningLine.h`, fermata only, header only - no CMakeLists change).
- *Measurement* (`tuning::measure`): rehar::refPoints step for step (the four loudest channels by energy; Hann ~0.34 s - 2048 points at
  ~8 kHz - every 0.1 s, padded twice; `rehar::findPeaks` 10 dB proud within 45 dB of the loudest, 60 - 1100 Hz, parabolic peak, cents from
  A = 440; folded to -50..50) and `rehar::refStats (D, W, 12)` - except that a peak is kept only when there is one within 25 c in each of the
  TWO frames either side (refPoints: one). On a whole take refPoints' rule let noise through: white noise at -26 and -46 dBFS, and a rumbling
  room, each gave a reading every 2 s (12 - 36 peaks a window, spread 8 - 11 c - the spread does not tell them apart: vibrato spreads music as
  much). With two frames: no readings from any of them; a four-part chord with vibrato keeps 56 - 77 peaks a window, a solo voice 33 - 84. It is done again
  here rather than calling refPoints because refPoints' `decimateAll` uses `parallelFor`, which starts threads (normal priority) on every call:
  not beside a recording. -60 dBFS (the loudest channel's mean square under 1e-6) gives no reading.
- *Windows* (`tuning::Meter`): 4 s windows every 2 s (refSeries' windows); a reading's time is the window's middle. Channels are pushed side by
  side; a window is measured when every channel has reached its end (ring of W + S + 4096 samples each; a channel more than that ahead of the
  slowest loses the window - `lost`).
- *Decimator*: to sr / round (sr / 8000) (8 kHz from 48 / 96 / 192k, 7.35 kHz from 44.1k), a Blackman-windowed sinc whose pass band ends at
  1.5 kHz and whose stop band starts at the new rate - 1.15 kHz (only a fold-back into 60 - 1100 Hz matters), 50 - 200 taps; the history is
  kept twice over so the last L samples are contiguous.
- *The line* (`appendPoint`): each reading moved by whole 100 c to sit within 50 c of the one before, so a drift past a quarter-tone stays one
  line. Shown (`shown`): Relative = cents - the median of the first three readings; Absolute = cents - 1200 log2 (A / 440), the first reading
  brought within 50 c of A by whole semitones and the rest following. Edit window, Relative: from the edit's first reading (the first piece,
  in time, with a reading in the part it plays); each piece's take line moved by whole semitones so its median sits nearest that
  (`EditTimeline::tuningValue`).
- *Live* (`tuning::Live`): `AppContext::watchTuning`, from the take window's timer while it records, finds the engine's feeds of the take's
  files (`AudioEngine::liveFeeds`, matched by `LiveFeed::file`; the tracks chosen in Measure from, else all) and starts a job once all of them
  exist. Its thread (low priority) reads each feed from where it got to every 150 ms; a read that fails (written over: it fell behind the
  1.5 s ring) pushes silence up to half a ring behind the writer and marks the line not whole. When every feed has `ended` and been read, the
  line goes to the message thread (`onTakeDone` -> `Store::put`), which keeps a whole line (and writes it) and drops one that is not (the files
  are then measured). The feeds now run while WaveColour OR the tuning line is shown (`engine.setLiveFeeds (waveColour || tuningShow)`).
- *Store* (`tuning::Store`): `get (take, source)` from the paints (only takes inside the clip); the key is "loudest" or "tracks" with a hash
  of the files' paths, so another choice of tracks, or new files, is measured again. Its thread reads `Analysis/<take id>.ftune` (text: a
  header, the key, whole, then "t cents spread n" lines) or measures the files (16384 samples at a time from each, the first two channels'
  mean, as the feed) - never while `holdOff` (`engine.isRecording()`, as the waveform colours): the job goes back in the queue. `measureAgain`
  deletes the `.ftune` files. `generation` goes up with every new line; the take and edit windows' timers repaint their band then.
- *A bug found in the trial (fixed before delivery)*: the Store's `Job` had `juce::Uuid takeId;` - juce::Uuid's default constructor makes a NEW
  random id, not a null one - so the idle thread never waited: it "measured" an empty job and wrote an empty `<random id>.ftune` on every turn,
  and filled the disk in minutes. `takeId = juce::Uuid::null()` (and the Live job's), and a job with nothing to measure is skipped; the test now
  checks that after one take is measured the thread writes nothing more. Worth the Fable pass looking for the same mistake elsewhere.
- *Settings*: `tuningShow` in the user settings; mode, A and the tracks in `Analysis/Tuning.settings` (JSON), read in `afterProjectReplaced`.
- *Windows*: the take window's band under the ruler is now `laneH` (`kBannerH` 50 + `tuning::kLaneH` 60 when shown; 46 at first, too flat to see a 12 c drift) - every former use of
  `kLaneH` in TakeTimeline is `laneH`, except the In / Out flags and the fade handles, which belong to the banner (`kBannerH`). The lane is
  drawn in `paintAll` (so the sticky header pass draws it too), its name box with the name column; a click on the name box is the menu;
  `getTooltip` gives the reading nearest the mouse both ways. The edit window: `bandH()` adds `tuneH()`, the lane under the guide
  (`tuneTop()`), each piece (`tuningSegs`: its take by `takeId`; a piece imported from an AAF has none, so its take is the one whose file it plays, by
  path, when the file is the whole take - `fileStart` 0) showing its take's readings within the part it plays; EditTimeline is now a TooltipClient for the lane only.

**(2) Import AAF** (`AafImportDialog::build`, FixTools.cpp). `homeOf`: for each AAF track name, the take window track most of its clips' found
files are on (ties: the first met). `trackFor` puts a clip on its track's home when the file is that track's own, or has the home track's
channel count (otherwise as before: its own take's track, else a track of the AAF track's name); `joinedHome` counts those that were not the
home's own files, and the summary says so. The edit's rows: the AAF tracks' in order as before, then every track a piece plays that is not yet
a row. `fixtools::giveEditRows (Project&)`, called in `openProject` after the stashed mixers: every edit (and frozen version) with rows of its
own gets any track a piece or overdub plays that exists in the project (the user's edit: 16 rows come back below the odd one).

**Checked** (Linux, JUCE 8.0.9, trial builds only): fermata compiles and links (every `.o.log` clean - the link script links old objects when a
compile fails, so the logs were read); fermata:Live builds unchanged (no shared file changed; it does not include `TuningLine.h`). The scratch
test `tuning/tune.cpp` (ALL TUNING TESTS PASSED): the decimator's pass and stop bands; a 0 -> -12 c drift read within 1.6 c; the Meter fed in
uneven blocks giving the same line as `measureFiles`; A = 442; an 80 c drift unwrapped; silence, white noise at -26 / -46 dBFS and a rumbling
room giving no readings; `Live` on fake feeds (kept up: whole; written over: not whole, so measured from the files) run through the message
loop; `Store` writing exactly one `.ftune` and then nothing (the Uuid bug). On screen (Xvfb, the scratch `aaftrial/mkaaf.cpp` project: take 1
drifting, take 3 sharp, take 2 a render on the odd track, and `Sonata.aaf`): the take window lane Relative and Absolute (A = 442: -9 -> -22 c,
as made), tooltip, menu, Set A box; the edit window lane both ways, the AAF pieces finding their take by file; the AAF summary line and the
render's piece on Vl1's row; an `.fmedit` with Vl1 taken out of `trackIds` getting it back on opening ("1 track row(s) given back"). Core
tests not re-run (no core file changed). Not checked: a real recording (no audio device on Linux - the live path only in the scratch test),
MSVC (GitHub builds it).

## 3.62.0: each clip's file name; Import source tracks to project folders (23 November 2026, with Claude Opus)

**What was asked** (23 November 2026): "put, in very small writing, the actual file names of each audio region onto the region, perhaps
starting from the top left corner. Just so I can identify which physical file each audio clip refers to" (for the next build); then "an
'import source tracks to project folders' button. Basically if any audio file is being used by fermata that isn't in its own project folder
system, it will copy a version into them and use that copied files, rather than the original. For example, if a user imports files but
doesn't copy them over. The user has the option to copy them over at a later time."

**(1) The file names** (`src/ui/ClipName.h`, fermata only): `clipname::draw (g, clip, file, visibleLeft)` - the file's name in 9.5 px type on a
dark strip (0xb0000000, as the take strip's words) 2 px in from the clip's top-left; x is the larger of the clip's left and the visible
timeline's left (`viewport.getViewPositionX() + kNameW`), so a clip that starts off screen still shows its name; cut with "..." to the clip's
width; nothing when the clip is under 22 px high or leaves under 26 px. It returns before measuring when the strip is outside the paint's clip
(the playhead's narrow repaints measure nothing). Edit window: a pass of its own after the crossfades (which are drawn over the pieces' starts),
every piece on every track but LTC; overdubs after their frame. Take window: after each take track's frame, above the DUD / Bar strip (its 16 px
are taken off the clip's height when the take has one).

**(2) Import source tracks to project folders** (`src/core/AudioGather.h`, header only, shared folder but fermata only; the dialog
`GatherAudioDialog` and `fixtools::gatherAudio` in `FixTools.cpp`; Project menu item 25). `gather::plan (Project&)`: every file played from
outside `projectFolder()` (`isAChildOf`), once each by full path - take files (`TakeFile::file`, first, so a take's file goes to
`takeFolder (window)`), then edit pieces, overdubs and visual guides of every edit and its frozen versions (`withVersions`) - into
`audioFolder()/From elsewhere` when no take plays them (the folder Create copy of entire project uses for such files). A name taken (on disk or
by an earlier item) gets " (2)" ... Missing files are listed, not planned. Waiting files (Export for Processing) are not touched: they are the
other program's. `gather::copy (plan, progress)` (any thread): free space checked (the plan's bytes + 200 MB); each file copied into a
`juce::TemporaryFile` beside its place with a CRC-32 (`projectcopy::detail::crcUpdate`) of what was read, read back and compared, its date set
to the original's, and only then renamed into place; on a cancel or any failure every copy made so far is deleted ("Nothing was changed").
`gather::apply (Project&, plan)` (message thread): only for the project the plan was made for; every reference whose path is a planned original
and whose copy exists is pointed at the copy (take files - `verified` cleared on those takes, so the next open looks at them -, pieces,
overdubs, guides, frozen versions too); then `structureChanged`, `undoReset`, `keepAllWindows` - as `relinkAudio`, not an undo step and not a
change the quit box asks about. The dialog: `app.saveNow()` before; the copy on a job (progress, Cancel); at the end, if the project was closed
(or another opened) or a recording started meanwhile, the copies are deleted and nothing changes; otherwise `stopPlayback`, `apply`, a save
keeping every window, every copy logged. Closing the dialog while it copies stops the job (`JobDialogBase` releases it) and its `ifGone`
deletes copies that were finished. The multitrack import's summary (when linking) and Import AAF's summary now name the menu item.

**Checked** (Linux, JUCE 8.0.9, trial builds only): the core tests (ALL TESTS PASSED, with the new 3.62.0 section); fermata compiles and links
(fermata:Live's build is unchanged: it does not include `AudioGather.h`). On screen (Xvfb, the scratch `gathertrial/mkgather.cpp` project): the
names in both windows, the off-screen start keeping its name at the left edge, the menu item, the box, the copy, the saved `.fmtake` / `.fmedit`
paths, the second try. Not checked: MSVC (GitHub builds it), a copy of many gigabytes, a cancel from the window (the core test cancels).

## 3.61.0: the knock weighting; the same box at the end of every export; the listening copy's instructions, names (23 November 2026, with Claude Opus)

**What was asked** (23 November 2026): (1) "some stand knocks and bangs don't register properly as black/dark as they are a little too quiet,
I want the user to be able to weight the colours so that a quieter level of knock triggers the dark/black colour ... in the same way that the
waveform button can have a menu about how heavy the waveform display compression is" (and fermata:Live to have the coloured waveforms - see
LIVE-DEVELOPMENT-NOTES 0.10.11); (2) "ANY bounce out or mastering function where fermata writes new files ... all bouncing out should show a
dialogue box saying what has been exported, what it's called and where it is. Clicking OK should make the progress bar ... empty and show
'Export Complete' until a new export is started again"; (3) the listening copy: the "What was said about each version ..." text into "a little
popup window that opens the first time ... All the instructions of how to use it are there ... an 'Instructions' button"; "a user HAS to insert
their name ... before they can comment"; "allow the editor in fermata to put their name in ... by their name and then in brackets (Piece
Editor)". Also asked: could an MBIT+ dither be added (answered, nothing built: MBIT+ is iZotope's own; see the delivery message).

**(1) The knock weighting** (`knockweight` in `src/ui/WaveColor.h`, shared). Black at Normal is unchanged (more than half of a block's energy
below 100 Hz, decided when the colours are worked out, so the stored colour is black). Each block now also keeps its share below 100 Hz as a
byte (`WaveColorData::low`, from `BlockAnalyser::colour (..., &lowShare)`, 0 for silence), so a weighting is applied when DRAWING
(`knockweight::shownAt`), at once and without working anything out again. A weighting (Light / Medium / Strong: 30 / 18 / 8 %) makes a block
black when (a) its share is above that, (b) its energy below 100 Hz (level squared times the share) is at least 2.5 times the block before's, and
(c) it is above -60 dBFS. (b) is what keeps a sustained bass note (organ pedals, double basses) in its colour: a share-only threshold blackened
a 55 Hz note at half the melody's level (16 %) as readily as a knock; only a note's sudden start can turn its first block black. (b) also
passes a knock that fell across two blocks: when the block before is itself under the weighting's share, the block is black if it is 2.5 times
the block two before AND the block after has already fallen below half of it (a knock dies away; a bass note fading in, which also rises over
two blocks, does not - so `shownAt` looks one block ahead there, and while recording the newest block waits for the next).

**Where a knock falls in a block.** The colours' FFT window (Hann, 4096) hears a block's middle fully and its edges hardly at all. On screen
(the Knock Trial project: knocks of 0.10 / 0.18 / 0.28 / 0.45 under a four-note chord at 4 / 8 / 12 / 16 s) Strong caught only the 12 and 16 s
knocks: the 8 s one lay three quarters into its block and measured 2 % and 5 % in its two blocks; in the middle of a block the same knock
measures over 20 %. So `BlockAnalyser::colour` takes `before` (the kBlock / 2 samples before the block) and also measures the share over a
Hann window centred on the block's START edge (`edgeShare`: the bins below 100 Hz by Goertzel - 8 or 9 of them - and the total from the
windowed samples by Parseval, so no second FFT), keeping the larger share. The two windows overlap by half, so every moment is heard at half
strength or more by one of them. The block's colour and Normal's black still come from its own window alone (Normal is exactly as before).
The callers give the half block before: `WaveColorCache::analyse` (the block before, kept), `LiveWaveColours` (read again from the feed's ring;
nullptr when the ring no longer holds it - its live colours still match the file's exactly: the 3.59.0 test, shares 0 / 255 apart) and
fermata:Live's `LiveColours` (each batch is read from half a block earlier). The test (`knock.cpp`, scratch): a knock under the chord at 16
places across a block is caught at 0.10 by Strong at 12 places (Medium none), at 0.18 by Medium at 14 and Strong at all 16 (Light none), at
0.28 by Light, Medium and Strong at all 16; Normal at none; never more than two blocks; a 55 Hz organ pedal fading in over half a second
(starting at four places in a block) is never black at Normal, Light or Medium, and one block at most at Strong (its menu line says so). Where it is drawn: `WaveColorData::colourFor` (every window drawing a file's colours), `LiveWaveColours::colours` (fermata, while
recording), fermata:Live's `LiveColours::colours`. The colour file (`.fwcol`) gains the shares at its END, one byte a block: an older program
reads the colours and levels and stops; this one, reading a file without them, returns nothing and the file is worked out again once (so the
weighting works on old takes too - their colours come back over a moment the first time). `AudioEngine::LiveFeed::low` (shared core) carries the
shares while recording, handed over with the colours (3.59.0). In fermata: `AppContext::waveKnock` / `setWaveKnock` / `showWaveColourMenu`
(settings `waveKnock`), `RainbowButton::onMenu` (shared `Uikit.h`) in the take and edit windows, one black dot on the button per step;
choosing a weighting switches WaveColour on.

**(2) The box at the end of an export** (`src/ui/ExportDone.h`, fermata only): `exportdone::describe` (what, each file's name - 15, then how
many more - the folder, or each folder when there are several, e.g. stems; notes), `show` (title, Show the files / OK, a callback when it is
closed whichever way), `barDone` (the value to 0, the bar's text "Export complete", shown) and `barStart` (the percentage again). Used by
Bounce Out (`BounceComponent::finished`), the Mastering window's masters / DDP / listening copies (`showDoneBox`: the success path now goes
through `exportdone::show`; a stopped export keeps its own box) and Atmos (`atmosFinished`), Export AAF and Export for video edit (their bars
now stay shown at the end, at 100 % under the box). The three Mastering exports share one progress value: barDone sets it to 0 for all three
bars, and only the exported one says Export complete. The Mastering window's own renders give no box (they are not exports and start by
themselves).

**(3) The listening copy** (`src/core/ReviewPageTemplate.inc`, shared, with `ReviewPage`): the three explanation paragraphs and the hint beside
Add a comment are gone from the page; a `<dialog id="help">` holds all the instructions (listening, commenting, the conversation so far,
saving and sending back). It opens at start when `fermata-review-help-seen` is not in the browser's storage (then set), and the Instructions
button in the header opens it again. `openComposer` (Add, the C key, Reply, Change) needs a name: `needName` marks the name box red, focuses it,
says why; `nameState` dims Add and writes the hint beside it. The editor's name: `Spec::editor` (from `MasterJobSpec::editorName`, from the
Listening copy tab's new **Your name** box = the setting `editorName` the edit window's note dialog already used) is written into the page as
`META.editor`; at start every editor note's `who` becomes "Name (Piece Editor)" (a note written as "Editor" or with no name takes the
page's name); the "editor" tag is gone; the line names above the picture read "Edit N: Name (Piece Editor)" and the column of line names
widens to its longest name (104 px to 260 px) - each piece of the template stays under MSVC's 16 kB string limit (largest 7.8 kB).

**Checked** (Linux, JUCE 8.0.9, trial builds only): the core tests (every core object rebuilt: ALL TESTS PASSED); `knock.cpp` (scratch: the
weighting, the 16-place sweep, the organ pedal fading in, the colour file - all pass); `livecol.cpp` (3.59.0's, now also comparing the
shares: 0 / 255 apart); both programs compile and link. On screen (Xvfb): the knock trial at each weighting (see CHANGELOG), the menu and
the dots; Bounce Out's box and the bar after it. The page: `review361/page.cpp` writes a page with a history and an editor's name, and
Playwright (`shoot.py`) checked the Instructions box (first visit only, the button), the name rule (marked, the status line, Add enabled
once named) and the editor's name in the history and the gutter (182 px). Not checked: the other exports' boxes on screen, the Listening
copy tab's name box on screen, MSVC (GitHub builds it).

## 3.60.1: ST L / ST R between the two names (23 November 2026, with Claude Opus)

Asked for on 23 November: "make sure it goes in the middle directly between the user insertable name and the ASIO derived name". `InputRow::
resized`: the name box has the display's width again; the tag (30 px wide, 9 - 12 px high as the squeeze allows) is centred across the display
and vertically half way between the bottom of the name's line of text (`alias.getY()` + its font's height) and the top of the driver name's
text. Checked on the Linux trial build (inputs 1 and 3 linked).

## 3.60.0: automation snap, values and boxes; the meter bridge's used I/O; the input strips' long meter; stereo-linked preamps (23 November 2026, with Claude Opus)

**What was asked** (23 November 2026): in the edit window's automation, (1) the area showing a lane's name to show its current value at the
playhead; (2) a per-lane snap button, on by default: "click anywhere in the automation lane and the point automatically places itself at the
value that it was already set at ... keep the mouse clicked and move it or click on it again to move it up and down. It should also snap to the
value of other set points"; (3) each point's value permanently in a small box beside it, the boxes that would overlap left out (only those).
Then, in the same build: (4) a tick box in the meter bridge to show only the used I/O - outputs with routing to them, inputs "linked to an actual
strip in the mixer", following changes by itself; (5) the input strip's meter to run the strip's full length (the user's picture: a strip with
no preamp controls, a short meter under an empty space); (6) stereo-linked preamps: right-click a strip, "Make left channel of stereo input",
then click the right channel, which takes the left's settings; "no other effect on the way fermata handles the channels, just the preamp
controls". And a GitHub question: "Artifact storage quota has been hit" on the build.

**(1) The value at the playhead** (`EditWindow.cpp`, the name column in `paintAll`): `headSeconds` (the moving playhead while this edit
plays, else the cursor) and `laneValueAt` (the lane's `valueAt`, stepped for a switch; no points: `controlNow`, what the lane plays); the
text (`shortValueText`, with " dB") is drawn bold gold at the right-hand end of the lane's label box, the label taking what is left. The name
column is already repainted on every timer tick while the edit plays, so it follows the playhead.

**(2) Snap** (`AutoLane::snap`, shared; saved only when off). The magnet (`snapRect`, `drawSnapButton`) sits between the label and the padlock
(the label box is 19 px narrower). With snap on, `automationMouseDown` puts a new point at `valueAt (time)` - nothing the lane plays changes -
and every drag of a point (new or clicked) is relative (`snapDraggedValue`): `autoVirtY` starts at the point's own height and moves by the
mouse's movement (Alt: a tenth), so a point put on the line by a click far above it does not jump to the mouse. The catch: the value of any
other point of the lane, or the drag's starting value, within a zone of `clamp (1, 3, cap * px per unit)` pixels (cap 1.5 dB / 5 % pan / 0.02
of a plug-in's range): at the default row height (52 px: 0.58 px per dB) that is the one pixel row nearest the target, so a value just beside
another can still be set, and since the virtual height moves in whole pixels from the start, the start value is caught for a wobble of a pixel.
While caught, a dashed white line runs across the lane at the value and the drag box has a white border. A lane's first point (no points yet)
goes where its control stands, as in 3.48.0; with snap on the drag then moves it from there (`autoDragHold` only with snap off). Snap off:
exactly the old code.

**(3) The value boxes** (`paintPointValues`): per point, text from `shortValueText` (no " dB"; a plug-in's own words, else 0.000), a box
13 px high above-right of the point (below the line when it would leave the row's top; on the left near the window's right-hand edge). A box
is left out when it meets another box or another point's dot (10 px); the rest stay. The decision for a point only involves points within
`kReach` (250 px) of it, and every point within that reach of the clip is considered, so a partial redraw (the playhead's strip) draws the
same boxes as a whole one. The dragged point's box is left out (its big box shows). Rows under 26 px: no boxes.

**(4) The meter bridge** (`MeterBridge.h`, fermata only): `usedInputs` - every channel of every track (`TrackDef::inputOf`); `usedOutputs` -
for every mixer, each Ext Bus's `outFirst` + its speaker channels and a surround monitor bus's headphone pair (as `Project::outputFedBy`), the
talkback pairs (an output and the next), the click's and the pitch pipe's outputs, LTC out when on. The cells keep the index they show
(`inIdx` / `outIdx`) for the peaks and their numbers. A 2 Hz timer compares the used lists with the shown ones (the routing changes without
a message: `outFirst` is an atomic). The tick is kept in the user settings (`bridgeUsedOnly`).

**(5) The input strip** (`MainWindow.cpp`, `InputRow::resized`): with no preamp controls (`! preampShown`) the meter and its scale fill the
height the gain caption, knob, readout, keys and meter took, the monitor keys at their usual place at the bottom; no GAIN caption.
`desk::DeskScale::fine` (shared, off unless set): on a scale 180 px or taller, marks at 0, -3, -6, -9, -12, -20, -30, -40, -50. Strips with
preamp controls are unchanged (the knob and keys fill them; a longer meter there would need wider strips).

**(6) Stereo-linked preamps** (shared: `Project::preampLinks`, `preampPartner`, `isPreampLeft`, `linkPreamps`, `unlinkPreamps`). `setPreamp`
applies the settings to the input and, in a pair, to its partner (each keeps what its own hardware can do: `lineOnly`, `steppedPad` ...), and
sends both to the hardware; so the main window's keys and knob, the Stream Deck (`RemoteDesk`) and the control surface all move both.
`adoptPreamp` (what the hardware reports by itself) is not copied, so a change made on the device's own page does not bounce. `linkPreamps`
drops any pair either input was in, adds (left, right) and sets the left's settings again (the right takes them). Saved as `"preampLinks"`;
on loading, a pair with an input out of range, or one that would share an input with another, is dropped; one whose input the driver no longer
has does nothing (`preampPartner` checks the range). In the main window: the strip's right-click menu (`InputRow::mouseDown`, listening to its
children - not the keys, the knob or the readout; the name box's own menu is off), "Make left channel of stereo input" (only on an input with
preamp controls) or "Unlink stereo input"; `MainComponent::startStereoPick` lays `StereoPickOverlay` over the inputs: L on the left strip,
R under the mouse, a banner; a click on a strip with preamp controls links (one without is refused and the pick goes on); Esc, a right-click or
the left strip itself cancels; the wheel still scrolls the strips. A linked strip shows ST L / ST R (with the partner in its tooltip).

**GitHub** (the user's build log): the build compiled; `upload-artifact` failed with "Artifact storage quota has been hit". A free account's
private repositories have 500 MB of artifact (and package) storage, and downloads are kept 90 days by default. Advised: delete old artifacts or
runs, set the repository's artifact retention lower, wait for the recount (GitHub says 6-12 hours; reports vary), re-run the job. The workflow
now keeps each build's downloads 3 days (`retention-days: 3`). The user's log shows `upload-artifact@v4` where this repository's workflow has
`@v6`: the workflow on GitHub may be older than this copy - this delivery's workflow replaces it.

**Checked here:** the core tests, sections "3.60.0 automation snap" and "3.60.0 stereo-linked preamps" (all pass), and the undo hash's list
("lane snap off"). On the Linux build with a trial-only `AppContext` (fermata:Live's test signal device, and a pretend preamp driver that
controls inputs 1 - 8 and logs what it is sent) on a trial project (`mksnap`: four tracks, lanes with points far apart and close together, one
lane with snap off): the values at the cursor (10 s: PnR -5.6 dB on its slope, pan L43, Solo -6.0, Spots -10.0); a click near the top of
PnC's lane at 20 s put a point at -3.0; a new pan point at 117 s (R30) dragged 6 px down was caught at C (dashed line, white box); the
boxes (Solo's four points 1 - 1.5 s apart: none at 12 px per second, all four after zooming in; Spots' points 0.25 - 0.3 s apart: left out at
both); inputs 9 - 16 with the long meter; inputs 1 and 3 linked (the log: input 3 sent 11.0 dB 48V PAD; then 19.0 dB and LC sent to 1 and 3),
ST L / ST R, the unlink item; the bridge's tick (4 of 16 inputs, 2 of 2 outputs). In the trial, the meter bridge's window took clicks over
its area after it was closed (the trial window manager): the lanes were tried outside it. **Not checked:** Windows, real preamps (Hapi, MT48,
Anubis), the bridge following a routing change while open (its 2 Hz check), the Stream Deck and the S1 moving a linked pair.

## 3.59.0: WaveColour while a take records (23 November 2026, with Claude Opus)

**What was asked** (23 November 2026, after 3.58.0): "the coloured waveform does work in the take window once the recording is finished, but
I want to see the colours live as they are being recorded". A take that records is drawn from the engine's live waveform (`LivePeaks`), which
has no colours; WaveColour (`WaveColorCache`) colours finished files from the disk. fermata:Live already colours while it records
(`LiveColours`, from the recorder's chunks on the drive); fermata's files cannot be read while they record (their header is refreshed every
10 s), so the sound is taken in memory instead.

**The feed** (`AudioEngine::LiveFeed`, shared): per recorded track, `RecTrack::feed`, made in `startRecording` when `setLiveFeeds (true)`
(fermata: `AppContext::setWaveColour`). The audio thread adds the mean of the track's first two channels (what `WaveColorCache::analyse`
takes from a file) wherever the file and the live waveform get audio: `deliverFromRing` (session mode, the pre-roll first), the direct path,
`fillDeviceGap` (silence), and the direct path's outsize block (`writer->skip`: silence - the live waveform now gets it too, so it stays in
time with the file). So the feed's sample k is the file's sample k. The ring is about 1.5 s in whole 4096-sample blocks of
`std::atomic<float>` (relaxed); the writer stores `begun`, a release fence, the samples, then `count` (release); `read` loads `count`
(acquire), copies, an acquire fence, then checks `begun` - false when the samples were not all there, or the writer had started over them
(the reader fell behind). At Stop the message thread delivers the rest (the audio thread has left the take) and sets `ended`; a file that
lost sound to an overrun (`samplesLost`) marks `gap` (its file has silence where the feed has sound). The finisher sets `fileDone` once the
writer is finished (a failure there marks `gap` too). The feed also holds the colours worked out (`argb`, `level`, `complete`, `gap`, under
its `lock`). `getLiveFeed (track, take)` and `liveFeeds()` hand out shared pointers (message thread): the session can go while the colours
are still being worked out.

**The colours** (`src/ui/LiveWaveColours.h`, fermata only; `AppContext::liveColours`): one thread at low priority. The take window's timer
calls `watch (engine.liveFeeds())` while a take records or is being finished (WaveColour on); each new feed is a job from sample 0. Every
40 ms (or at once while there is work) each job takes the whole blocks that have come in, up to 32 at a time, and runs
`WaveColorCache::BlockAnalyser` on them (the files' own analysis); a block the ring no longer holds is left unknown (argb 0, `gap`) and it
goes on from the oldest block still there. After `ended`, the last part-block is padded with silence (as the files' are) and the feed is
`complete`. `drawLiveWave` asks `LiveWaveColours::colours` for its pixel columns (the loudest block in each, the plain colour where nothing
is known yet) and hands them to `fillEnvelope`, once per track for all its lanes. **The hand-over:** `AppContext::takeInFinishedTakes` calls
`handOver (files)` after `forgetWaveforms` (which drops the files' old entries); a complete feed with no gap whose `fileDone` is set goes to
`WaveColorCache::put` (kept as the file's colours, saved in the Waveforms folder; a waiting request dropped) at once, or from the thread (via
the message thread) as soon as it is complete. With a gap it is dropped and the file is coloured from the disk as before (`holdOff` keeps
that until no take records).

**Checked here:** the core tests - "3.59.0 the live feed for WaveColour while recording" (the ring: stereo mean, mono, `addSilence` short and
longer than the ring, `read` refusing what has not come or was written over; a take in session mode with a 1 s pre-roll: no feeds while they
are off, two while on, `ended` at Stop, `fileDone` once finished, the engine letting go at the reap, and the last 8192 samples of each file -
the stereo one as the mean of its two channels - equal to the feed's within 24-bit steps). `scratchpad/livecol/livecol.cpp` (not in the
repository: it needs `juce_graphics`, which the core tests do not link): the engine recording a sweep with noise, thumps and silence in real
time, `LiveWaveColours` watching - 82 of 82 blocks coloured while recording, `colours()` giving changing column colours; after the finish
and `handOver`, both files' colours identical to `WaveColorCache`'s own analysis of the files (0 blocks different, levels within 1.5e-8); a
take watched 4 s late: `gap`, its first blocks unknown, not handed over; `setLiveFeeds (false)`: no feeds. **On the Linux build:** a copy of
fermata built for the trial only with fermata:Live's pretend device (`TestSignalDeviceType` through `GuardedDeviceManager::makeRealTypes`;
not in the repository) - WaveColour on, 7 tracks armed, Rec: the take's waveforms coloured as it recorded (input 1's 110 Hz red-orange with
its clicks marked, up to 770 Hz green), and one second after Stop the finished take drawn from its files in the same colours, its `.fwcol`
files written by the hand-over. **Not checked:** Windows, a real interface, and how much the colouring adds with 64 tracks at 192 kHz
(about 3000 blocks a second: on a modern PC a few per cent of one core, at low priority).

## 3.58.0: bus solo, Enter in the take window, a moving progress bar, faster WaveColour, the mixer's link, strip order and layout (23 November 2026, with Claude Opus)

**What was asked** (22 November 2026), eight things: (1) soloing an Int Bus left lots else playing - only what feeds INTO the soloed bus should
be heard (a track sent to the bus and also going to Main: its main output muted, its send kept); (2) Enter for ReTouch did nothing in the take
window; (3) the denoise has no progress bar and looks hung - show progress "even if it's not real"; (4) coloured waveforms "don't show" in the
take window; (5) strips selected with Shift, then Shift + Alt held: a change to one strip - the example, its fader output to another Int / Ext
Bus - changes all of them; (6) move Int Buses, VCAs and non-main Ext Buses sideways (a bus next to the 8 strips that feed it); (7) M / S under
the fader, a stereo track's two pans on one line ("small and fiddly" is fine, values are typed), the fader and its meter nearer the top and
bottom of their area; (8) Reset peaks lower, above Phase scope.

**(1) Solo** (`AudioEngine::processMixer`): three flags per node. `soloSelf`: its SOLO or a soloed VCA's. `soloAfter`: soloed, or fed (along a
live send or output, forwards in the plan's order) by a soloed node. `soloHeard`: feeds a node that is `soloSelf` or `soloHeard` (backwards).
A track plays (`soloOk`) when any of the three is set. Every send and output (`SendPlan`, outputs included) is open only when the node is
`soloAfter` or its destination is `soloSelf` / `soloHeard` (`pathOpen`): a node upstream of a soloed one plays only along the paths that lead
there. A soloed track, and everything after a soloed node, is as before; before 3.58.0 a node feeding a soloed one was opened whole.
Test: the section "audio tracks, Int Buses and Ext Buses" (the Strings bus soloed: its tracks heard through it only, the Foldback send closed;
the bus's output off or the send muted: silence; a soloed violin: everything it feeds).

**(2) Enter** (`TakeWindowComponent::keyPressed`): `returnKey` with no modifier and marks 1 / 2 set on a take (or takes selected) clicks the
ReTouch button, as the edit window's Enter does; otherwise it is passed on. In the hot keys list.

**(3) Progress** (`JobDialogBase`, `src/ui/SpectralWindow.h`, shared): the timer notes when `job.progress()` last moved; after a second
without, `progressValue` is -1 (JUCE's ProgressBar then runs its moving stripes) and the bar's text says "Working... N s" (seconds since
`runJob`); real progress shows again as soon as it moves. Every job dialog (ReTouch, Noise mode's denoise / de-click / de-hum, the Fixer's
ReTouch in fermata:Live) gets it.

**(4) WaveColour** (`WaveColorCache`, `src/ui/WaveColor.h`, shared): not reproduced - the take window colours its takes on the Linux build
(`TakeWindow.cpp` draws with `app.waveColourOf`, as the edit window). The likely cause is time: one worker, oldest request first, for the
hundreds of files of an orchestral take window. Now: requests go into a queue (a file asked for again moves to the back), each worker takes
the newest first (what was drawn last is on the screen), up to `setWorkers` workers (fermata: CPUs - 2, 1 - 4; fermata:Live: 1), each with its
own `AudioFormatManager`, in a low-priority pool made at the first request; `holdOff` (fermata: `engine.isRecording()`) makes them wait;
`clear()` empties the queue too.

**(5) The link** (`StripGang`): `forSelectedOthers` (no key check, for a choice in a list that opened with the keys held) and `othersNow`
(ids, for a menu answered later). `StripOutputRow`: `OpenAwareBox` notes in `showPopup` whether the keys are held (`linkAtOpen`); `choose()`
and the OUT button give the linked strips the same `outDest` / `outOn` (`linkApply`, set by `ChannelStrip`). `LowCutKey::linkedNow` /
`linkedRefresh`: LC on / off and polarity to the linked strips; the panel (`LowCutPanel::setLinked`, captured when it opens) sets their low
cuts too ("+ N linked" in its title). The + menu adds the same send to the linked strips (one that already sends there keeps its level).
`ColourPickerPanel` takes `also` (more channels): the coloured bar with the keys held (`StripBase::colourAlso`), and "Colour of the N selected
strips..." in a selected strip's plate menu. `gangRefresh` refreshes LC and the output row too.

**(6) Strip order** (`src/core/StripOrder.h`, shared; `MixerState::stripPlaces`, saved as `"stripPlaces"`): `itemsOf` lists a mixer's strips
in the usual order - tracks (hidden ones too, so a place after one holds), Int Buses, Ext Buses, VCAs (`shown`, `movable`: not tracks, not an
Ext Bus ticked as main in this mixer). `order` takes out the strips with a valid place (movable, its anchor here or null) and puts each back
right after its anchor (several after one anchor in the order of the list; a circle at the end). `moveAfter` keeps the anchors unique: the
strips that followed the moved one take its old left neighbour, the one that followed the new anchor follows the moved strip - so nothing
else moves on the screen. `backToUsual` and `forget` (strips going: a place after one takes the nearest strip before it that stays, in the
order shown) the same way; `Project::forgetStrips` runs it from `removeTrack`, `removeEmptyTracks`, `removeBus`, `removeVca`; the stereo join
moves places after either mono track onto the stereo track. The window (`MixerComponent::rebuild`) builds the strips in `order`'s order;
movable strips get `StripBase::onMoveDrag` (a press on the bar or plate arms it; 6 px sideways starts the drag, `beginDragAutoRepeat`; a click
without a move opens the colour picker on release - a plate's single-click timer waits for the release while the button is held),
`onMoveCommand` and `isMoved` (the plate menu's two moves). `MixerStripsHolder::moveDrag` draws the gap (`paintOverChildren`), scrolls the
viewport (`autoScroll`) and, let go, hands `MixerComponent::moveStrip` the strip left of the gap; the rebuild comes from the message loop.
"Next to the strips that feed it": after the last shown track or Int Bus whose output or an active send goes to it (a VCA: whose `vcaMask`
has it).

**(7) Layout** (`StripBase::layoutFrame`, `FaderBlock::resized`, `ChannelStrip::resized`): bottom up - OUT, the post-fader slot, M / S, the
fader, the pans. `StripLayout::panRows` is always 1: a stereo track's two `TypedSlider`s are side by side, each half the width less 4 px, the
read-outs over them (24 - 34 px) with "PAN" between when 18 px are left. The fader's slider now covers the whole block: the travel runs from
8 px under the peak read-out to 7 px over the reading (`travelFrom` / `travelTo`), the cap tucking under them at the ends; the meters (on the
same scale) follow; the peak read-out takes clicks (`setInterceptsMouseClicks`) so a click on it does not move the fader. (The cap is 44 px,
`MixerLookAndFeel::kCapHalf` 22, in the shared look: not changed.)

**(8)** `MeterSettingsStrip::resized`: Reset peaks right above Phase scope.

**Tried on the Linux build** (a made-up session: 8 string tracks sent to an Int Bus "Strings" and controlled by VCA 1, two winds, a stereo
Omni, an Int Bus "Reverb", a cue Ext Bus): the layout (M / S under the faders, "L100 PAN R100" over two half-width sliders, +12 just under
the peak read-out, -inf's cap tucked under the reading, Reset peaks over Phase scope); Strings dragged by its bar to between Cb and Fl (the
gold line and the veil while dragging; no colour picker after); VCA 1 "Move next to the strips it controls": after Cb, with Strings
following it; saved (`"stripPlaces"` in the mixer's .fmmix) and the same order after closing and opening; strips 1 - 6 selected, Shift + Alt
held as strip 2's output list opened, let go, Reverb picked: all six to Reverb, the others not; Shift + Alt + double-click on strip 2's plate:
the selection kept. Take window: a take selected and Enter: ReTouch opened; its scan showed the stripes and "Working... 3 s" .. "9 s";
WaveColour on: the takes coloured.

**Checked here:** the core tests - "audio tracks, Int Buses and Ext Buses" (the bus solo) and "3.58.0 buses and VCAs moved sideways" (the
order, `moveAfter` keeping anchors unique, a track moved or hidden, `backToUsual`, saving, `removeTrack` / `removeEmptyTracks` / `removeBus`
keeping the spot, an Ext Bus ticked as main, places that cannot count). **Not checked:** Windows (the Alt key held over a drop-down list, and
the cursor while dragging); the colouring time with a real orchestral session.

## 3.57.0: an import laid out like a take window; a window's takes onto another window's tracks (22 November 2026, with Claude Opus)

**What was asked** (22 November 2026): an orchestral session of just under 50 mono files per take, some made into stereo tracks and renamed
after the first piece's import. Every later piece's import made another set of about 50 tracks (the names no longer matched: "One set of
tracks" failed on the renames, "Use the existing audio tracks" by order failed once pairs had become stereo tracks). Wanted: a choice in the
import to copy a take window's track structure, names and stereo tracks; and (offered, accepted) a way to put a piece already imported on
tracks of its own onto the first piece's tracks.

**How the mics are known** (`src/core/TrackLayout.h`):
- `layoutOf (project, window, naming)`: for every track the window has audio on, the names of the mics whose audio is there. From (1) the
  take files' `sourceName` / `sourceName2` (the original file names, kept by every import and by "Make a stereo track"), read again with
  `buildImportPlan` (the same reading as the import itself, so the names compare like with like), each name tied to the track and side its
  file is on now; (2) the track's own name; (3) the mics remembered on the track (`TrackDef::mics`, `micsLeft`, `micsRight`: what was chosen
  in an earlier list). A source name that appears twice in a take (the channels of one polyphonic file) is not used: it says nothing about
  which channel.
- Names are compared canonical (`canon`: words, lower case, single spaces) and reduced (`reducedNames`: words every name of one import starts
  or ends with taken off - only with 4+ names, while every name keeps a word that is not a number or a side, and only if they stay distinct).
- `match`: in order, the first that gives exactly ONE place wins (two or more: undecided, the person chooses): the same name; a mono file
  whose name is a stereo track's with L / R after it (and a stereo file whose name is the two sides' without L / R); the same name without
  take numbers at the end (two digits or more only - "Mic 1" keeps its 1); the end of the name, longest wins. Compatible places only: mono
  file -> mono track or a side of a stereo track; stereo file -> stereo track.
- `build`: the choices applied to the unjoined import plan - parts on a layout track get the key "@<track id>" (`mapTracks` mode 3 puts
  them there; a key whose track has gone gets a new track of its name), the two sides of a stereo track one part (source + source2, or
  `ImportPart::kOneSide` with `sourceRight` when a take has only one: silence on the other side, written by `copyImportFiles`). Two files of
  a take on one place (or on a whole stereo track and one of its sides) are `clashing` and the import waits.
- After the import the mics' names are remembered on the tracks they went on (`remember`: at most 32 each); every import sets
  `Project::lastImportWindow`, which the layout choice offers first.

**The import window** (`src/ui/FixTools.cpp`): the two tick boxes became the "Tracks:" choice (`importTrackMode` 1-4; the old settings
`importOntoExisting` / `importByTrackName` carried over); mode 4 hides the pair list (the layout decides the pairs) and the track order
button, and shows the window choice and the `PlacementList` (one row per import track: name, a box of places - the layout's tracks first,
then the project's other tracks - and a note). The window is 780 x 760.

**Moving a window's takes** (`MoveOntoDialog`, `moveTakesOntoWindow`; the Import button's menu and the track-name menu): rows for this
window's tracks that the other window does not use; matched with the same `match` (a stereo track made from two files: its whole names
first, then its two sides). A whole place: `Project::moveTakeClips` (as the 2.99.0 moves). A side: `stereojoin::planInto` / `writeJobs` /
`applyInto` - the two mono tracks' files in THIS window joined into the existing stereo track; the pieces of edits made from these takes
follow (whole-take files shared, other pieces their own file in Fixes); frozen versions are not touched (decided for moves, 3.47.0); the
mono tracks stay (deletable once empty: offered afterwards). Refused: audio already on the stereo track in such a take or piece, parts out
for processing, automation on the mono tracks in an edit with such pieces. An ordinary undo step (the tracks are not removed).

**Found while trying the window here:** `joinBox`, `pairView`, `numBox` and `pieceBox` of the import window were never added as children
(in every version in this repository's history): `setVisible` and `isVisible()` on a component without a parent change only its own flag,
so `splitting()` and the pair joining worked with their defaults while nothing was shown. Now `addChildComponent`.

**Tried on the Linux build** (a trial project: piece 1 from Pyramix-style files, renamed, Omni-L / Omni-R made into "Omni"): piece 2
imported "laid out like" piece 1 - every file on its renamed track, the Omni pair one stereo file (left 0.15 / right 0.16 as written), only
the new Celesta made a track, the mics remembered, `lastImportWindow` moved on; piece 3 (imported by name, 9 tracks of its own) moved onto
piece 2's tracks with the move window, the pair joined, and the 9 empty tracks deleted when offered: one set of 18 tracks.

**Checked here:** the core tests' section "3.57.0 an import laid out like a take window": the names, `reducedNames`, the four ways of
matching, a remembered pick, `build` (joined pair, one-sided pair, clashes, left out), `remember`, the one-sided and different-length stereo
files from `copyImportFiles`, saving and loading the new keys, and `planInto` / `applyInto` on a window with an edit (refusals, files,
pieces, the edit's track list, Ctrl + Z). **Not checked:** the two windows on Windows; real orchestral file names (the matching is only as
good as the names: the list is the safety net).

## 3.56.0: timecode as MIDI - the Hapi's LTC_in and LTC_out through rtpMIDI (20 November 2026, with Claude Opus)

**What was asked** (20 November 2026): the user installed rtpMIDI as Merging describe ("TimeCode Setup with Merging RAVENNA / AES67
devices", the Windows - RTP-MIDI part) and has one session, "Hapi LTC I/O", with the Hapi's `LTC_in` and `LTC_out` streams as its two
participants. fermata's LTC IN and LTC OUT only knew audio channels; they are to use those ports. The user plans to have Claude Fable
go over the timing part afterwards.

**How it works** (`src/core/Mtc.h`, header-only):
- `MtcReader` (MIDI thread in, any thread out through a seqlock): quarter frames in order build the timecode of each sequence (the time
  piece 0 was sent); every quarter frame after the first whole sequence publishes a reference - the timecode it stands for
  (`baseFrames + qf / 4` frames) and when it arrived. Out of order (a gap, running backwards) waits for a whole sequence again. A
  full-frame SysEx is a locate (`running = false`). 23.976 / 29.97 NDF (sent with the codes of 24 / 30) are told by the frames' real speed
  measured over 6 s or more of unbroken running (threshold 0.05% below nominal; the two are 0.1% apart).
- `MtcChase` (audio thread): an `LtcGenerator` jammed with `startExact` to the reader's time at the block's first sample, then left to
  run on the audio clock (the Hapi and the PC share the Ravenna / PTP clock, so there is no drift to follow). The error against the
  reader is averaged (one-pole, 0.02 a block) and the generator jammed again only when the average is over one frame; over 0.5 s it is a
  jump and is followed at once. No timecode for 0.25 s: the generator stops and the block is silence (as a stopped LTC source).
- The engine: when `ltcInCh == kLtcFromMidi` (input 255) and the port is open, `process()` makes that LTC into `ltcMidiIn` and hands
  `in` on as a 256-pointer array with it as the last input - so the LTC track (input 255), the pre-roll ring and the lamp's `LtcLive`
  take it as any input, with nothing else changed. The block's first sample is taken as heard one block before `process()` runs.
- `MtcOutput`: the audio thread stamps where `ltcGen` is at each block's start (`positionSeconds`) with `mtc::now()`; a 1 ms
  `HighResolutionTimer` extrapolates and sends each quarter frame as it falls due (sequences start on even frames), and a full frame
  when starting or after a jump. `ltcGen` now runs for a MIDI port alone (into `ltcScratch`), not only for an output channel.
- Project: `ltcInPort`, `ltcOutPort` (port names). `AppContext::applyLtc` opens the ports; the main window's lamp update tries a missing
  port again every 30 updates.

**For the Fable pass (the timing)** (done in 3.67.0 - see "3.67.0", above: the driver's latencies, a steadied clock, the offset and its
measuring): where MTC arrives against the audio. Known approximations: (1) the block's capture time is taken
as `now - block length`, without the driver's input latency or the converter's; (2) with the sample-rate converter (`RateBridge`) the
engine's blocks are not one per device callback, so `now` jitters by up to a device block; (3) MTC out is stamped when the block is
made, not when it is heard (output latency); (4) the rtpMIDI latency itself (2-5 ms in the user's screenshot) is not allowed for. All
four are constant or near-constant offsets: an "offset" setting measured against LTC on an audio input would remove them.

**Checked here:** the core tests' section "3.56.0 MIDI timecode": drop-frame numbering both ways; 25 fps quarter frames read back to the
time they were sent; 30 told from 29.97 NDF by speed; 29.97 DF across a minute; a full-frame locate; LTC made from MTC with 2 ms of
wobble decodes (with `LtcDecoder`) to within 1.5 frames of the MTC's time, every frame after the first 2 s. **Not checked:** any real
MIDI port (none here), the Hapi itself, MTC out as a receiver sees it.

## 3.55.0: the score - scans, printed bar numbers, clear paper, moving the marks, the marked PDF (12 November 2026, with Claude Fable)

**What was asked** (11 and 12 November 2026, after 3.54.0's list of what it did not do): "I'd really like to have the scan or
photographed in feature"; "the program can interpret bar numbers (it should be able to see the numbers and guess that they are
bars, see the context of the bar lines and deduce that they are the size, font and regularity that means they are the bar
numbers)"; "I don't mind the markers going over a bit of the text but it needs to be clear of the stave and notes. Ideally away from
any text though, but maybe not always possible"; "I DO want to be able to print the score to a new PDF AND I want to be able to
adjust the placement and size of the added markings". Agreed before building: the new PDF's pages are 300-dpi pictures with the
marks sharp on top; a scan or photograph always arrives inside a PDF (no JPG / PNG scores).

**The three real scores.** The user sent three scores to try it on, asking that they not be made public (copyright). They were
used from a private folder of the session only and are **not in the repository, the tests or these notes** - do not add them, or
anything cut from them, ever. What they were: (1) a choir score with a solo instrument, 8 pages, one nine-staff system a page, from
MuseScore; (2) a four-movement orchestral score, 52 pages, 59 systems of up to 32 staves, from Sibelius; (3) a five-page scan (a
title page and four of music, four and five staves a system) carrying the user's own pen marks. What they taught is in the
sections below; the figures quoted are from them. The made-up scores of 3.54.0 (`mkscore.js`, thirteen sets of notes) stayed as
the check that nothing drawn got worse, and a second script put them through what a scanner does (turned 0.7, 1.8 and 3 degrees,
blurred, grey and speckled, bent like a book's page, bent and turned, scanned small and blown up, a shadow across the page, black
and white only): 25 of 27 such pages came out right - the two that did not are the same case, an OPEN note in the top space of a
SINGLE staff on a page both bent, turned and blurred, whose stem is taken for a barline.

**A drawn page is left exactly as it comes** (`analysePage`: `trackStaves` on the page as it is, `isDrawnPage`). Its staves are
level, so it is not turned and not flattened and the finder works on the very same dots as in 3.54.0. This matters: the scan code
must never be able to harm a clean export. (The staff-following first read clean pages as slightly bent - a tie or a beam lying
along a staff line makes that "line" fatter in a strip and moves its middle by a dot. The staff's middle is now the middle one of
its five lines' answers, each strip's reading is the median of five strips, and a page where six tenths of the staves are level is
a drawn page whatever the rest say.)

**A scanned page** is one whose staves are not level:
- *Ink* is judged against the paper round about (`Ink`: the page in squares, each square's paper its own light grey), so a shadow
  or grey paper is not ink. On a drawn page that is the one threshold it always was.
- *Turned upright* (`tiltOf`, `levelled`): by the angle at which its UPRIGHT strokes - barlines, stems - fall most sharply into
  single columns. Not by its staff lines: a bent page's staff lines climb without its barlines leaning, and turning it by the
  staff lines' climb made every barline lean (a nine-pixel smear down a system; that was the first version, and it broke bent
  pages). On the scan sent, four of the five pages needed no turn at all - only their staves bend.
- *Each staff laid level* (`trackStaves`, `Warp`, `flattened`): the page is read in upright strips; in a strip a staff is five
  level lines the same distance apart; a staff is followed from strip to strip (`StaffTrack`), and the dots of the page are moved up
  or down so that each staff lies at its own middle height, the paper between two staves moving with them. Only up and down: the
  barlines stay where they are across the page. Traps met: a staff lost for a stretch comes back as a new track (joined if it
  carries on from where the first was going - and only then: five hairpins above one another are a "staff" in a strip, and a
  hairpin's line one staff-space above a staff makes a second five with the staff's top four lines, which is why the staves already
  being followed take their five first); beyond a track's two ends the staff goes on as it was going (else the first centimetre of
  every staff stays crooked and the system's left edge is found late).
- *After that it is the same finder*, with three allowances on a levelled page (`strokesOf (..., flattened)`): each side of a stroke
  is looked at from where the five lines really are just there (they still step a row here and there); a blurred stroke's grey edges
  are not "ink beside it" (its skirt); the line that joins a system's staves is followed down from row to row (`joinedAtLeft`).
- What is found is put back where it is on the page as it came (`Turn::toPage`, `Warp::dyAt`).

**Stems, again.** The stem test of 3.54.0 (ink beside the stroke on ONE side) now also applies in systems of several staves - only
strokes with clear paper beside them, or that run on to the next staff, vote for a barline. That is what removes the **4 over 4**:
on real score (1) the two 4s' uprights, one above the other, made a stroke the whole height of all nine staves at one place, and
every number after it was one too high. A 4 has ink on BOTH sides in every space (`crossed`) and is fat (`fat`); a tie has ink on
both sides in one space. A note's head is told from a tie by how much of the space it fills (`heavy`: a tie's end beside a barline
climbs through half the space - at 55 % real barlines with ties lost their votes; it is 80 %), and an open head on a blurred page
by its two arcs hard against the lines above and below (`arcs`).

**The printed bar numbers** (`readNumbers`, `ScoreDigits.h`, `Map::bars`).
- *Seen*: above a system's top staff, every separate shape of a digit's size (0.8 to 3.4 staff-spaces tall); one to four in a
  row, level, with nothing of a letter's size beside them (a word has letters either side; a boxed number has its box), standing
  over the start of a bar. Each shape is brought to a 12 x 18 grid and laid over 206 drawn digits (`ScoreDigitShapes.inc`, made by
  `tools/make_digit_shapes.py` from 92 free fonts - TeX Gyre Schola for Century Schoolbook, Termes for Times, Pagella, Bonum,
  Heros and others, upright, italic and bold; near-duplicates left out). `Page::reads` keeps what was seen.
- *Believed* (`Map::bars`): a number seen is a bar number when it is what the count says there anyway, or when another number seen
  within 80 bars differs from it by exactly the bars between them. Nothing else is. That is the whole defence against a tempo's 88,
  a tuplet's 3, a page number and a misread digit, and on the real scores it was enough: score (2) gave 54 of its 55 printed
  numbers right and two stray numbers, both ignored; the scan gave ten of eleven, one misread (31 as 11) and one of the user's own
  pen marks read as a 9, both ignored.
- *What a believed number does* where it differs from the count: lower, and counting back reaches 1 at a system's start - a new
  movement (score (2)'s four movements numbered themselves: a movement's first line prints no number); the same but 1 falls on a
  system's second bar - a pick-up bar, numbered 0; otherwise the count takes the print from there and the bar is marked
  (`Bar::jump`, the orange "!" in the Bars view). The user's `NumberFix` wins at its bar; `Map::useReads` switches the reading off.
- The digit table is DATA, generated: change the script and run it, never the .inc by hand. If a score's bar numbers are in a type
  the table does not know, the answer is another font in the script's list.

**The paper above a system** (`Band`, `bandAbove`, `placeAbove`). A grid of cells a third of a staff-space wide, from the system's
top line up to the system above (or 16 spaces): 2 = music - what stands up out of the top staff, followed up each column of dots
until there is clear paper for 1.25 spaces, and what hangs down from the system above; 1 = other ink (words, numbers); 0 = clear.
`placeAbove` puts a mark's block as low as it can go where it covers no music and no block already placed, a cell of other ink
costing as much as six cells of climbing, up to eleven spaces up. Never on music; over words only when clear paper is out of reach.
The band is kept in the bars file (packed with zlib, then base64: about 1 kB a system).

**The marks are laid out in the page's own measure** (`ScoreComponent::layoutOf` -> `MarkDrawn`; x as a part of the page's width, y
of its height; text widths from a 100-pixel font, scaled). That is what makes the window at any zoom and the saved PDF agree.
`laid` holds a page's layout until the brackets, the bars or a look change. The user's changes are `Map::looks` (`MarkLook`: the
bar's anchor and kind; `moved` with `dx`, `dy` from the top of the bar's barline; `scale`) and `Map::markScale`; moved brackets take
their places first and the others are placed clear of them. The mouse: a press on a bracket sets
`pages.setViewportIgnoreDragFlag (true)` so that the drag is the bracket's and not the page's (the viewport looks at that flag on
every drag event); under four pixels of movement it is still a click.

**The marked PDF** (`saveMarkedPdf` -> `startExport` on the message thread, `runExport` on the worker; `src/core/PdfWrite.h`). The
shapes are made first, on the message thread, because they need the fonts: each bracket's rectangles, the pale patches, and each
number as the OUTLINES of its glyphs (`GlyphArrangement::createPath`) - so the PDF needs no font and looks as the window does.
The worker (the one thread that may use the PDF reader) draws each page at 300 dots to the inch (less for a page so big that the
picture would pass 4200 dots), and `pdfwrite::Writer` writes it: PDF 1.4, per page a grey picture packed with Deflate, a drawing
and the page; the file is written beside its place and moved there when whole (`TemporaryFile`). 52 pages: 11 MB, well under a
minute here. `PdfWrite.h` knows nothing of juce's drawing (the core tests link without it): the window turns paths into its
`moveTo / lineTo / curveTo`. Checked here with `pdfinfo` and `qpdf --check`, and by drawing the result again.

**Tried in the Linux build under Xvfb**, the three real scores each put on a trial project's edit: (1) 44 bars, the seven printed
numbers read, 44 given to the take window; the brackets clear of the fermatas, the tempo mark and the running title; a bracket
dragged, the A key pressed twice, a bracket's menu, "This bracket smaller"; the marked PDF saved (8 pages, 0.9 MB), the dragged
bracket and the sizes in it as on the screen. (2) 52 pages in about 40 seconds (with the slow stand-in page drawer), 509 bars, 54
numbers read, the four movements numbered from 1 by themselves, 57 given to the take window; the marked PDF. (3) every page taken
for a scan, 54 bars, ten numbers read, fermata's brackets beside the user's own pen marks.
**Not tried here**: Windows' own PDF drawing feeding any of this (a stand-in draws the pages here - its greys and edges differ); a
photograph; a score that numbers every bar or every fifth; a part with multi-bar rests; sound.

**Known weak places**: a single staff (a part, a solo line) has no other staves to vote, so everything rests on the stem test - the
open-note case above is in it; a close-up photograph at an angle (staff spaces growing down the page) is found only in part; the
count cannot know a repeat's bars counted twice or a multi-bar rest (the print, where read, puts it right with a "!").

## 3.54.0: look and listen - the version keys in two halves (11 November 2026, with Claude Fable)

**What was asked** (11 November 2026): each Edit N key in two areas - the left "reverts the edit back to that edit but keeps the current
mixer settings", the right, "+ Mixer", also "reverts the mixer back to the state of the mixer at that time"; every version's feedback
in its own lane, all visible whatever edit is in focus, switched by the button at the top of the window. Asked whether "reverts" meant
A, look and listen with the working edit untouched, or B, really replace the working edit: **the user chose A.** Do not turn a key's
click into something that changes the working edit; that is "Make the working edit as Edit N was..." on the right-click menu.

**The state** is the application's: `AppContext::versionViews[editId]` = { pieces, mixer, workingMixer }. `pieces` (0 = the working
edit) and `mixer` (0 = the working mixer) are separate: the left half sets `pieces` only, "+ Mixer" sets both
(`AppContext::showVersion`). Not saved with the project: a project opens on the working edit.

**Listening** needed almost nothing: `playEditVersion` (3.42.0) already plays a frozen version through the edit's mixer.
`AppContext::playEdit` now goes there whenever a version is on show, so every play of the edit - the window's, the mixer's transport
row - plays what the window shows. `showVersion` maps the play position to the same music (`editversions::mapSeconds`) and, if the
edit was playing, starts it again there.

**Looking**: `EditTimeline::edit()` returns a COPY of the frozen version while `viewing > 0` (`setViewing`, `viewCopy`), so the whole
window draws it with the code it has. A copy, so that no code path in a 4,900-line window can alter the frozen version itself. And no
path should be reached: the timeline's mouse handlers go to `viewClick` (playhead only), `EditWindowComponent::keyPressed` lets
through Home / End / Space / the zoom keys / C / Esc, `applyViewLock` dims every key that changes an edit, and `deskAct`,
`placeFromTakeWindow` and `filesDropped` refuse. `EditWindowComponent::edit()` is still the LIVE edit (title, comments, import); the
few things that must follow what is on show use `timeline->edit()` (`goToEnd`). The LIVE edit's `playheadSeconds` is the view's
playhead meanwhile (it is what the application moves); the copy's is kept equal to it in the timeline's timer.
`VersionStrip::syncView` (10 Hz and on every click) brings the application's state to the timeline and the keys, and ends the view by
itself when the version is gone or the working edit's pieces changed (`piecesSignature`: a take put in from a take window, Ctrl+Z).
The view also ends when the Edit window closes, a project is opened or made, and the program closes (`endVersionViews`).

**The mixer** (`AppContext::setShownMixer`) is the real one - there is no second mixer to play through: the working mixer's snapshot
(`mixSnapshot (m, true)`) is taken when it is first covered, the version's is applied (`applyMix`, `clearEmptySlots`), and the working
one is applied again on the way back. **Because a save or a crash in between would leave the older settings as the edit's mixer**, the
working snapshot is also written to `<project>/Recovery/mixer-<mixer id>.json` (`Project::stashMixer`), removed when the working mixer
is back, and `AppContext::restoreStashedMixers` applies any that are found when a project opens (with a note for the person). Changes
made in the mixer while it holds an older version's settings are lost on the way back: the notice says so. Freeze, Remove and the
menu's "make the working mixer as..." are closed while it does.

**The lanes**: `VersionStrip::lanes()` - every version with comments, when `EditDef::showFeedback` is on, plus the working edit's own
while a frozen version is on show. `EditVersion::laneShown` is still read and written by the project file and no longer used.

Also: a version frozen (the Freeze key is closed, an import's "Start Edit N+1" is not) while the mixer holds an older version's settings
is frozen with the WORKING mixer (`freezeLiveVersion`); the insert flag is not drawn over a frozen version.

Tried in the Linux build under Xvfb with the test project (Edit 1 and Edit 2 frozen, working in Edit 3): Edit 1's left half - the
pieces of Edit 1 on show, the line that says so, the editing keys dim, the lanes of both versions' comments in place; a click moved
the playhead, Delete did nothing; Edit 1's "+ Mixer" - both halves lit, the file in Recovery written; Edit 3's left half - the working
edit with Edit 1's mixer still lit; Edit 3's "+ Mixer" - everything as before, the file gone; Esc from a view; the right-click menu;
the Feedback key hiding and showing every lane; the program killed while Edit 1's mixer was in use and the project opened again - the
working mixer put back from the file (the log line), the file removed. **No sound here**: playing a version, and the carry-on from the
same music, were not heard; the "Your mixer is back" box was added after that run and not seen.

## 3.54.0: the takes marked on the score (11 November 2026, with Claude Fable)

**What was asked** (11 November 2026, in four messages and a photograph of the user's own pen marks on a choir score): fermata is to
"analyse the score and see where the bars are (I will usually have a PDF export from a proper program) and ... both analyse the recorded
sound AND take note of what bar numbers the operator puts in for each take, and annotate the score". The marks: a bracket with its
bar going LEFT is the end of takes ("take 6,7,8,9 and 10 finished at the beginning of bar 30"), one with its bar going RIGHT is the
start ("11,12 and 13. These start at the beginning of bar 30"), both drawn above the system at the barline, the takes' numbers beside
them. "The markings do not need to be 100% accurate. So long as the takes land within about a bar then it can go where the rest of
them are (e.g. do not do multiple end or multiple start brackets within the same bar)." The numbers need not pile up leftwards as the
pen's do: in order. Then: takes imported from another program are to be placed the same way from their sound once the user has typed
bars (and Dud marks) for some; and the count of the score's bars is to go to the take window as the piece's length, the user's own
number winning. (The bigger idea of the same day - reading notes, dynamics and tempo, an automatic first edit - was put aside by the
user: "Let's leave that idea for a while".)

**Where it is.** `src/core/ScoreBars.h` (new, header only, no drawing and no files of its own; tested in `tests/core_test.cpp`) and
`src/ui/ScoreWindow.h`. fermata:Live does not include the header.

**Finding the bars** (`scorebars::findSystems`, on a grey picture of one page). The score window's one PDF thread draws each page for
it at about 250 dots to the inch (`analysisSize`; always the same size, whatever the zoom), when no page picture is waiting
(`barPages`, the pages in view first). Then:
- *Ink*: anything clearly darker than the paper (the commonest light grey x 0.78), so a thin line that falls between two rows of dots
  still counts.
- *Staff lines* (`levelLines`): rows whose long runs of ink add up to a fifth of the page's width. Runs, not one run: a time
  signature or bar number printed on a patch of white breaks the line. Five of them the same distance apart are a staff (`stavesOf`).
- *Systems* (`joinedAtLeft`): staves joined by an upright line at their left edge. A single line of music is a system of one staff.
- *Barlines* (`strokesOf`): an upright stroke the whole height of a staff that stops at the staff's outer lines, or runs on all the
  way to the next staff of the system. In a system of several staves a barline is a stroke that most of them have at the same place
  (both, of two; six in ten, of more; one fewer if it runs through to the next staff). **Stems are the trap.** A stem that crosses the
  whole staff usually sticks out of it, and that test removes it; but a note in the top space with its stem down ends *exactly* on the
  bottom line, and one in the bottom space with its stem up on the top line. In a single staff nothing but the note's head tells
  such a stem from a barline: ink right beside the stroke in one of the four spaces, on ONE side (ink on both sides is a tie or slur
  crossing a real barline, and says nothing). The stroke's exact columns have to be known for that test (the full-height test also
  passes the column either side): they are the columns with the most ink of their own. Strokes within a staff space of each other
  are one barline (double bar, final bar, repeat).
- *Bars*: the system's start, each barline, its end. A line within 2.5 spaces of the start is the system's own edge (or an alto
  clef). Staff left over after the last barline is a bar carried over the line end - unless it is short (under 8 spaces and under
  0.4 of the usual bar): that is the room for the next line's time or key signature.
Everything is kept as fractions of the page. It takes about 10 ms a page here; the drawing of the page is the slow part. Made-up
scores (`mkscore.js` in the session's scratch folder: a choir with separate barlines, a piano with barlines through, single lines,
nine small staves in groups; stems crossing staves, open heads that break the lines, a repeat, a double bar, a volta, a stub for the
next line's time signature) came out right at fifteen picture sizes from 120 to 400 dots to the inch and with thirteen different
sets of notes. **No real engraved score was to hand** (none on the machine, and no way to fetch one): the first real PDF is the real
test. The user's photograph finds nothing - a photographed page is tilted and shaded, and that is expected.

**What it did not do in 3.54.0: read the printed bar numbers (3.55.0 does - see that build's notes).** The bars
are COUNTED (`Map::bars`), and the user puts the count right with `NumberFix`es ("this bar's number is N", the bars after it count
on): a pick-up bar, a second movement starting at 1 again, a multi-bar rest in a part, a bar carried over a line end (give the second
piece the same number: `lastPiece` then treats the two as one bar). `joinToBefore` takes off a barline that is not one, `splitAt`
adds one. Fixes and the per-edit starting bar are anchored by where they are on the paper (`Anchor`: page, system, x), not by a
bar's index, so they survive a barline being added or taken off. `editStarts[editId]`: when one PDF has several movements with the
same bar numbers, the edit's takes are looked for from that bar on (`findBar (bars, number, from)`).

**The file**: `<project>/PDFs/<pdf name>.bars.json` (`toVar` / `fromVar`): the pages' systems, the fixes, the edits' starts,
`piecesGiven`. It is refused, and the bars found again, if it is of another PDF (its size in bytes), another page count or another
`kAnalysisVersion` - raise that when `findSystems` changes what it finds (the hand corrections are then lost: say so in the history).
Written when all pages are done, on every correction, and - half done - when the window closes (`Page::done`: the rest is found the
next time).

**The brackets** (`scorebars::brackets`; drawn by `ScoreComponent::drawTakes`). The takes are those of the take windows tied to the
edit (`tiedWindows`: `targetEdit`, the edit's `windowId`, its pieces' windows), or of all take windows when none is tied yet
(`windowsOf`); not the room tone, not a take still recording. A take logged "Bar 12 - 29" starts at bar 12's left edge and ends at
bar 29's right edge - which is the barline 29|30, "the beginning of bar 30". A take with `barOut` 0 has no known end (it is NOT taken
to be one bar long). One bracket of each kind per barline; typed bars first, then bars from the sound, which join a bracket of the
same kind one bar either side if there is one. Numbers ascending. Drawn in red (`0xffd3202f`), Dud takes grey, "?" after a number
whose bar came from the sound; sized by the staff space but never smaller than on a normal-sized staff (a full score's staves are
small); many numbers wrap onto rows (the upright grows upward); a bracket whose numbers would land on another's (many short takes of
the same bars) is moved up above it (`placed`). An end and a start at one barline stand side by side, as in the photograph.
`hits` (by page, rebuilt as each page is painted) is where each number is, for the mouse: click plays the take
(`AppContext::playTake`; from 0 at a start bracket, its last 8 s at an end bracket), right-click is its menu ("Set its bars...",
"Keep these bars for the take"). `marksKey` is everything the brackets were made from; they are made again (twice a second at most)
only when it changes, so a take that has just been recorded and has its bars appears by itself.

**Bars from the sound** (`scorebars::guessBars`, called from `ScoreComponent::hearBars`). Only for take windows with take matching
on (the fingerprints are the take matching service's). A take missing a bar is laid over the takes that have both bars typed
(`TakeMatchService::alignmentFor`), the ones with the most of the piece first: six of them, up to sixteen for a take those do not
reach; no more than 25 ms of this per half second, on the message thread. The take's first and last second of music are found in the
reference and read off its bars spread evenly over its music (`takematch::BarMap`) - so "within about a bar", which is what was
asked for and why the guessed ones join neighbouring brackets. The best guess per take is kept in `heard` and thrown away when the
set of reference takes changes (a bar typed on another take). Guessed bars are never written to the take by themselves: "Keep these
bars for the take" (one) and the Takes key's "Write the bars found from the sound onto their takes" (all, only empty bars) do that.
This is the whole of "imported takes": takes brought in from another program are takes like any other.

**The piece's length to the take window** (`givePieceBars`, `scorebars::pieceLength`, `pieceBarsToGive`). The last bar number of the
music that begins at the edit's start (it ends where the numbers start again) goes to `TakeWindowDef::pieceBars` of the TIED windows
(never "all of them") when the window has none and was never given one, or still has exactly what fermata gave it
(`Map::piecesGiven`) and the score's count has changed. A number typed in the take window, or a box emptied there, stays. It is done
when the bars have just been found, when they are put right in the score window, and when the score window opens - **never on a
timer**: it is `Project::changed()` (an undo step, as typing the number is), and a timer would put the number back after every
undo and wipe the redo list.

**Tried in the Linux build under Xvfb** with a made-up three-page score and sixteen takes of a made-up 38-bar piece (their bars typed
in the project file, three with a bar or both missing): the bars found and saved, and read from the file the next time; the Bars
view; the brackets on all three pages - including an end and a start side by side at one barline, a Dud take in grey, a take ending
at a line's end; the two takes with no bars placed from their sound at the right bars (20 - 30 and 19 - 27) and the one with no end
given its end (33); a guessed start one bar off joining the bracket beside it; the tooltip; a number's menu, "Keep these bars",
"Set its bars..."; a click reaching the player ("The audio device has not been started" here); a bar's menu - its number set (the
bars after counting on, the brackets following), counted from 1 again, a barline taken off and added back; twenty-four more takes at
one barline wrapping onto three rows and the brackets stepping clear of each other. Then the same project made to look like an
import - sixteen takes, the bars typed on the one full take only, take matching off, no "bars in the piece": on opening the score the
log said "has 38 bars - given to the take window" and the number was in the saved take window; the Takes key's menu said "1 with both
bars typed, 0 placed from their sound, 15 not placed" and offered "Listen to the takes of ..."; a quarter of a minute after choosing
it fourteen takes were on the score with a ? (the fifteenth, four bars and eight seconds long, was not placed surely: the menu says
to type such a take's bars); "Write the bars found from the sound onto their takes" wrote them, and the saved file had all fourteen
takes' bars exactly as the takes had been made. **Not tried here**: sound (no audio device), a real score from a notation program,
Windows' own PDF drawing feeding the finder (here a stand-in draws the pages), long takes (the layings-over were of takes up to
80 s), a take window's number typed over fermata's and kept (that rule is in the core tests, `pieceBarsToGive`).

**Left out, said to the user**: printing or saving the marked score as a PDF; dragging a bracket to another barline; reading the
printed bar numbers; scanned scores.

## 3.46.1: the fourth sweep, areas A, B and C; the list number in "Number the files" (9 November 2026, with Claude Fable)

**The sweep.** `docs/SWEEP-4-FINDINGS.md` is the record: per area the files read line by line, and every finding as FIXED (with its test), HELD
(the person decides) or NOTED. This build is areas A (the audio engine, playback, threads, starting and closing), B (recording and saving) and C
(files that leave fermata and come back), with fermata:Live's recorder (`docs/LIVE-DEVELOPMENT-NOTES.md`, 0.7.1). Areas D to I follow. What a
later change must not undo:
- **Closing waits for the takes before the safety net starts** (`AppContext::finishTakesBeforeClosing`, called from `shutdown()` before the last
  save): `AudioEngine::waitForFinishedTakes (60000)`, the second drive's copies (10 s), `takeInFinishedTakes (false)` for a take whose file
  refused its end, and `StallWatch::touch()` so the wait is not reported as a freeze. The engine's destructor still waits too (a second net).
- **The pre-roll ring takes every block** (`AudioEngine::process`): a block bigger than `maxBlock` is written in pieces, never skipped - the
  ring's position and its contents must move together.
- **`AppContext::newProjectProblem`** refuses a new project whose file (`.fermata`, the old `.takedaw`, or a `.bak` of either) is there. The file
  chooser cannot ask: it sees `Folder/Name.fermata`, and the project is made at `Folder/Name/Name.fermata`.
- **`BackupCopier` deletes only what it made** (`Job::mine`): a file already at the destination is moved aside as "(earlier copy)", or the new
  copy takes another name. `dst == src` (the second drive's folder is where the project is) is reported, not copied.
- **A window whose file is missing keeps what belongs to it** (`Project::parkedMasterItems` / `parkedDdpClips`, the alignment sets of
  `missingWindowRefs`, and `planWindowFiles` never hands its file name out). `toVar` writes the parked entries back at their old places; nothing
  new in the file format.
- **`OnceSavedBin` (`ui/OnceSavedBin.h`, `AppContext::deleteOnceSaved`)**: files that the project file on the disk may name are deleted only
  when a save whose snapshot was taken after they left the project is on the disk (`saveNow` numbers each snapshot; `savedInBackground` and
  the save written there and then report it; `afterProjectReplaced` forgets the list without deleting). Noise mode uses it for the files of a
  fix taken off again (`NoiseModeDialog::binFiles`): its fixes are in the edit at once and are NOT under `saveHold`, unlike every other try-out
  window (`PreviewApplied`). Anything new that puts files into the project and may delete them again must use one of the two.
- **The stereo join converts the frozen versions** (`StereoJoin.h`; first made to refuse, then converted by the person's decision). `plan`
  walks each edit and then each `EditVersion::edit` (`planEdit`); `PieceUse::version` (0 = the edit itself) says where a piece is, because a
  frozen copy's pieces keep their ids (`editOfUse`). The edit's own pieces get a file each, as before; a version's piece takes the file of the
  edit's piece with the same id and the same source files, else any file planned from the same source files, and widens that job's
  `wantFrom` / `wantTo` - so the same audio is the same `RegionFile` in both and `editversions::compare` sees no difference. `apply` runs
  `convertLists` (trackIds, fixTracks, guide, lanes) on the edit and on every version, and replaces `EditVersion::mixer` by `joinedMix` (a
  changed copy of the snapshot: the stereo strip where the left one was, built as a live mixer's new strip starts - compareMix against the
  live mixer straight after the join is empty; the test checks that). The versions' `EditDef`s are changed in place (they are held by
  `shared_ptr`, and the undo history is reset by the join anyway). A version frozen between plan and apply fails the "piece the plan does
  not know" check. Anything new that is kept inside an `EditVersion` and names a track must be added to `convertLists` / `joinedMix`.
- **Nothing an edit plays lives in Processing Media.** `relinkEdit` copies the corrected files into `Recorded Media/Fixes` on a job
  (`SimpleJobDialog`; `audioops::checkWrittenLength` on each copy; the piece is looked up again and compared before it is changed), and
  `RxLink.cpp copyFor` puts "(saved N)" copies there too (it skips a number already used beside the original by an earlier version). The
  watched file (`RegionFile::rxFrom`) is still the one in Processing Media.
- **`AppContext::toggleRecord` refuses while `saveHold > 0`** (the same sentence as New / Open / Quit). fermata:Live has no such state.
- **`RxSaveWatch::missing` / `kMissingLooks`**: a watched file that is not there counts as "being replaced" for 5 looks, no longer for ever.
- **`Aaf.cpp Cfb::open`** refuses a file over 1 GB before reading it (the reader holds the whole file in memory, on the message thread).
- `Ravenna.cpp`: `outgoing` is emptied with the connection. `Project::importMixerFile` / `importWindowFile`: ids shorter than 8 characters are
  not replaced (`String::replace ("", ...)` never ends). `MediaComponent::signature`: only the project's folder is listed to the bottom.

**"Number the files".** `MasterExport.cpp makeFilesSpec` gives each exported piece the number of its row in `MasteringDef::items` (`listNo`:
1-based, counted over every row, ticked or not, and over rows whose edit is gone or empty), where it used to count the ticked pieces from 1.
`MasterJobItem::trackNo` is that number, so the "NN - " prefix and the track-number tag both follow it. The tick box has a tooltip saying so
(`MasteringWindow.cpp`). The CD image and the DDP are not touched (their track numbers are the disc's). Test: core_test, after "an edit left out".

## 3.46.0: a look for bugs through the whole code; Find other versions for imported edits (9 November 2026)

**What was looked at.** The mastering section read line by line (`MasterRender.*`, `MasterExport.cpp`, `MasterDef.cpp`, `Bounce.cpp`, `AdmWriter.*`,
`Dither.h`, `Resampler.h`, `Loudness.h`, `Ddp.cpp`'s image and file-set writers, `MasteringWindow.cpp` from the settings refresh to the end, the disc
audition in `AppContext::playRenders`); `XfadeWave.h`; every `.cpp` of both programs (69 files, the font data left out) through Clang's static
analyser (`clang++ --analyze`, checkers core, cplusplus, unix, deadcode.DeadStores) - about twenty reports, every one a path that cannot happen or
a value stored and not read; and searches for the shapes behind 3.45.1's freezes (`stopThread` on the message thread: all the remaining ones wait
for work that looks at its cancel flag every block; `callAsync` and `Thread::launch` holding `this`). Not read by eye this time: the edit, take and
mixer windows (apart from the above), the fix tools, fermata:Live's own code. Nothing was run on screen.

**The crossfade pictures' retry loop** (`XfadeWaveCache`, 3.45.0). `work()` returns null when a file will not open; the worker then erased the key
from `pending`, stored nothing in `done`, and still posted `ready` - so the repaint asked for the same mix, which failed again. Now a null mix is
stored like any other (its entry's mod times are the files' as they were: a missing file's is 0), no repaint is posted for it, and `get()` returns
the null (drawn as before 3.45.0) until the five-second look sees a file's time change.

**Pieces through one mixer** (`Bouncer::render`). One engine and one set of plug-in instances render every item; nothing emptied the plug-ins
between items, and with the Mastering window's default Tail of 0 s a reverb was cut mid-ring and went on under the next piece (the delay
compensation's `skip` throws away only the latency's worth). A re-prepare would clear them but is the message thread's business (a VST3's
`prepareToPlay` takes the message lock: a deadlock in waiting if the window is closing). So: `mixersRendered` - when an item's mixer has been
rendered before in this pass (also A, B, A: A's plug-ins stood still while B rendered), the engine is run on silence with the same target, the
captures thrown away, until their peak is under 1e-5 for half a second, a minute at most. Test: "3.46.0 two pieces through one mixer" (a leaky
integrator as the reverb).

**The Atmos export without a stereo source.** `makeAtmosSpec` built its list with `makeFilesSpec`, whose `commonChecks` wanted
`resolveRenderSource` to find a stereo source; and its `MasterRenderBatch` rendered the stereo master of every stale piece on the way. Now
`makeFilesSpec (..., needSource = false)` goes on without a source (never without a mixer), and the batch is made `stemsOnly`: the master renders
are not looked at (no fingerprint, no `check`, no entry in renders.json), not rendered and not asked for at the end. Test: a surround-only project.

**`adm::FileWriter::finish`** checks the stream's status after each flush and the file's size on the disk against what it wrote; a file that
fails is deleted (the destructor only deleted one that was never finished while the stream was still open).

**`MasterExportResult::previousPieces`**: the items the batch says are `isPrevious`, by edit name (`MasterRenderBatch::nameFor`), shown by
`MasteringComponent::showDoneBox` with the warning icon.

**Find other versions for a piece that names no take window** (`VersionsWindow.h`: `versionsSourceFor`, `VersionsSource`). An AAF import makes
its pieces with `windowId` null and no `takeId` (`FixTools.cpp`: "from no take window: null, not a made-up id"), and `TakeWindowDef::targetEdit`
was only ever read by the take window. The resolver's order: the piece's own window / take (as before); the piece's file among the takes' files -
the same `juce::File`, then the same name and size - in the window whose `targetEdit` is the edit first; that window; the only window. The matched
take becomes `srcTakeId`, and `takeFrom` / `takeTo` are the marked part in the take's own samples: a take's files start at its sample 0, so the
place is the piece's `srcIn` less its file's `fileStart` (`shift`). `passageFrom` / `passageTo` stay the piece's own (the fallback read and
`refineJoin` read the piece's files with their `fileStart`). `showVersions` asks with a menu when nothing says which window.

## 3.45.1: freeze reports of 9 November 2026 - the ReHarmoniSer's job, render copies, the render fingerprint, plug-ins starting (9 November 2026)

What the reports were (`Fermata 3.42.0 freeze 103401`, `3.43.0 freeze 133031 / 151447`, `3.45.0 freeze 153007`, read with the stacks the stall watch
wrote into fermata-log.txt): 10:34, closing a ReHarmoniSer window - `~ReHarmoniserEditor` destroyed its `Worker`, whose destructor called
`stopThread (60000)` while the job ran on; 13:30 and 15:30, opening the project - `Project::fromVar` → `busFromVar` → `slotsFromVar` → the
plug-in factory → `VST3ComponentHolder::initialise` inside Seventh Heaven Professional (`InitDll`, `WinVerifyTrust` on its own file, ~190 threads of
its own), "set up 18629 ms"; 15:14, Render previews - `checkRenders` → `MasterRenderBatch` → `Bouncer` → `Project::cloneForRender` → the same
plug-in, 25 s. The same session also had `MasterRenders::check` → `save` → `ReplaceFileW` 5 s (from the window's timer), and
`masterRenderFingerprint` → `describe` → `String::operator+=` 5-7 s. The closing that day: "finished cleanly in 5.9 s" (5.1 s of it the plug-ins
letting go). The two 3.30.0 crash reports of 3 November are from that build and match nothing here.

**ReHarmoniSer jobs** (`ReHarmoniserEditor::Worker`, as `AudioJob` since 3.1.x). One `Worker` per job, made with `Worker::launch`; the editor keeps a
plain pointer to the last one and calls `release()` when it starts the next or closes: cancel, `wanted` = false (its posted `done` is dropped), and
the job deletes itself on the message thread once `run()` has ended (`endLock` / `released` / `finished`: whichever of `release()` and the end of
`run()` comes second deletes). Jobs still never overlap: each new job is given the previous one's `ended` event and its thread waits for it before
it runs its work ("Waiting for the last job to stop") - jobs share the spectrogram (`recombine` changes `spec`'s gains in place). A job let go while
it waits still waits for the one before it before it signals its own end (the first try broke off the wait, and then the job after it ran beside
the one before: found by a test harness built from the class itself, under the address and thread checkers). The work lambdas capture
shared pointers and values only (never the editor), so a job left running after the window has gone is safe. The Cancel button and the progress
timer check for a null job.

**Render copies start only the mixers they render through** (`Project::cloneForRender (sr, block, onlyMixers)`; `g_renderOnlyMixers`, a thread-local
read by `fromVar`'s mixer loop, gives the other mixers an empty factory - their slots keep `savedDescription` / `savedState`, as a plug-in that would
not start). The `Bouncer` works the list out as `render()` picks each item's mixer (its own if it still exists, else the settings' mixer), and
checks only those with `pluginsMissingFrom` - so the processing mixer, which a master render through each piece's own mixer never runs, is neither
started nor checked. The offline engine runs only `offlineMixerId` (`renderOffline`), so a mixer left without its plug-ins is never heard.

**The render fingerprint** (`MasterRender.cpp`, `FpHash`). The description used to be one `juce::String` built with `<<`; a JUCE String grows by
exactly what is added (`makeUniqueWithByteSize`), so each part copied the whole text again - quadratic, seconds with a big plug-in state (and
`lookAtRenders` runs whenever the project changes while the DDP tab is open). Now each part is fed to the hash as it comes. The hash must not change
(the renders.json fingerprints of every render made so far): the old loop stepped a `CharPointer_UTF8` - one step per character, hashing the
character's low 8 bits, not the UTF-8 bytes - and the length was characters; `FpHash::add` does the same. Checked against the old code on a test
project and with non-ASCII plug-in names and states (identical), and a core test rebuilds the old text from `masterRenderParts` and compares.
`MasterRenders::check` no longer adds an empty entry for a piece with nothing rendered (it made the first look write the list, with an fsync).

**Plug-ins starting** (`StallWatch.h`: `PluginStarting`, `PluginStartLabel`; `PluginHost::create` and the factory's `setStateInformation`). While a
plug-in starts, the stall watch counts the time apart: the 5-second stacks and the 15-second freeze report go by Fermata's own time in the stall (or
60 s / 120 s in all), and the stall's line adds "N s of it waiting for a plug-in to start (the longest: <name>, N s)". A start or a settings load of
2 s or more is logged ("Plug-in slow to start: ..."). Nothing makes the plug-in itself start faster: a VST3 plug-in is made on the main thread.

Also: `EditTimeline::gainMarkAt`'s helper lambda was called `near` (3.45.0) - a Windows header macro; renamed `closeTo` (found by check-windows-code).

## 3.45.0: volume changes as boxes round their tracks; crossfades drawn as they sound (9 November 2026)

**Volume change boxes** (`EditTimeline::gainUnits`, used by the painting and by `gainMarkAt`). A *unit* is a lone change, or every point of one
"between the 1/2 marks" change (its `group`, across pieces). Its rows are the tracks whose level the change really moves: for each file, the change's
`db` against `levelAfter (file, i, at)` (the level just before it), 0.05 dB or more; a run of neighbouring rows is one box. A group runs from its first
point to the end of its glide back (role 3: `at + ramp`), or, with no glide back, to the end of the last piece it is in - worked out after every point
has been seen (taking a piece's end before the glide back had been seen made the box, and the range box's title, run to the end of the piece:
fixed in `RangeVolumePanel` too). A lone change runs to the next change in its piece that moves one of the same tracks, or the piece's end. Triangles
(15 x 11 px, was 9 x 7) where a group starts and glides back (role 2, a piece the area only carries on into, has none) and where a lone change starts;
the glides shaded inside the box; a change that moves nothing keeps a thin line. The level a piece starts with keeps its small mark on the lane.
`gainMarkAt`: the triangle (8 px either side, 13 px down), or within 4 px of a box's left or right edge or a glide, on the box's rows.

**Crossfade waveforms** (`src/ui/XfadeWave.h`, `XfadeWaveCache`; the pass after the pieces in `paintAll`; `drawXfadeMix`). For each join, the zone is
[min (a.fadeOutBegin, b.fadeInBegin), max (a.fadeOutFinish, b.fadeInFinish)], cut to the two pieces. On each track (not LTC, not a part out for processing)
the mix is a's file x a's fade gain + b's file x b's (`Fades::gainAt` = `EditRegion::gainAt`, the engine's curves; a side with no file on that track is
silence), worked out on the cache's own thread (newest request first) from the files themselves and kept as a 16-bit min / max outline - one point per
sample up to 32768 samples, coarser beyond (30 s at 96 kHz at most) - in a 64 MB LRU; the key is both files, their offsets, both pieces' fade times and
curves and the zone, so moving a fade asks for a new one; a file rewritten in place is noticed within 5 s (its date). The window repaints once a batch
is done. Drawn only where the zone is 4 px wide or more: the row's fill again (each side in its piece's selection colour), the outline with
`fillEnvelope` (WaveColour from whichever piece is louder at that moment), or the sample curve when zoomed in past a pixel a sample; then hatching
(7 px, theme text at 14 %) and the fades' tints; the selected pieces' second frame line drawn again. Volume changes are not applied, as the rest of
the waveform is the files as they are.
