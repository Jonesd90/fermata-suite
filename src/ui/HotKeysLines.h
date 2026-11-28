#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace td::hotkeys
{
/** HotKeys (Help menu): every key of fermata and fermata:Live and what it does, window by window. 3.69.0 (asked for on 26 November 2026: "a hot
    keys option in the menu that lists the hot keys. Can we make that editable? And then have a save/export hotkeys option? So multiple users can
    set and save their preferences"): the keys of the windows' own jobs can be changed in the HotKeys window (src/ui/HotKeysWindow.h), and the
    changes are kept in a file both programs read (src/ui/KeyMap.h). Shared by both programs (COMPATIBILITY.md): fermata's list and fermata:Live's
    are both here, so each program knows every key in the file and keeps the other's changes when it saves.

    Whenever a key is added or changed anywhere in either program, its line here is changed with it (DEVELOPMENT-NOTES, 3.0.1). A line's keys are
    its DEFAULT keys - what the program does with no key file - and KeyMap reads them from here: write them as they are pressed.
    keys: "Ctrl+Z", "Shift+4", "Num +". "or" stands between keys that do the same job; "/" between a pair whose two jobs the description gives in
    the same order ("Home / End" - "the start / the end"); ", " between two such groups. click, drag, double-click, right-click and wheel are the
    mouse. An empty keys is a note: it is written across the whole line, under the heading.
    Which keys can be changed is KeyMap's rule (keymap::editableLine): the keys of a window's own jobs; never Esc or Tab, a box's keys ("In the
    ... box"), the mouse, or the questions' and boxes' keys. */
struct Line { const char* section; const char* keys; const char* what; };

/** fermata's keys (shown by fermata's HotKeys window). */
inline const std::vector<Line>& fermataLines()
{
    static const std::vector<Line> v
    {
        // ---------------------------------------------------------------------------------------------------- everywhere
        { "Everywhere", "", "In the main window and every window on the desk (take, edit, Trim, mixer, meter bridge ...), except while you are typing in a box: the keys then type. The repair, pitch, ReHarmoniSer and ReTimEr windows, plug-in windows and the Take Display screens have keys of their own, further down." },
        { "Everywhere", "Space", "Play or stop the take or edit window you used last, from any window that has no play of its own." },
        { "Everywhere", "Space", "While a take is recording: stop the recording, from every window." },
        { "Everywhere", "R", "Start recording into the take window you used last, if it is open (with no take window open, R does nothing). Recording needs Rec mode (the Rec half of the Rec | Pre button): without it, a warning offers to switch it on. In the edit window R butts pieces together, in ReTouch and Noise mode it does the ReTouch, on the pitch curve it previews; in ReTimEr, ReHarmoniSer and plug-in windows it does nothing." },
        { "Everywhere", "M", "Open or close the mixer: the edit's own mixer while you work in an edit or its Trim window, otherwise the processing mixer. If it is open but behind other windows, M brings it forward." },
        { "Everywhere", "Ctrl+B", "Open or close the meter bridge." },
        { "Everywhere", "U", "Open or close the pitch pipe (a minimised one comes back)." },
        { "Everywhere", "I", "Open or close the click (a minimised one comes back)." },
        { "Everywhere", "Ctrl+Z", "Undo: the edits, the mastering and the takes' details (bars, duds, names, positions, removed takes), up to 30 steps back." },
        { "Everywhere", "Ctrl+Y or Ctrl+Shift+Z", "Redo." },
        { "Everywhere", "", "Keys pressed in the mixer, phase scope, meter bridge, pitch pipe, click, Media, Take Display, Display Organiser, Align and HotKeys windows go on to the take or edit window you used last, so Space, 1, 2, L and the rest work from there too." },

        // ---------------------------------------------------------------------------------------------------- main window
        { "Main window", "click", "The Rec | Pre button in the menu bar: Rec (left half) switches Rec mode on or off - recording / tracking, nothing heavy in the background; Pre (right half) switches the lead-in on or off. Right-click: how many seconds Pre keeps (1 to 10)." },
        { "Main window", "click", "The MATCHING box (top row): how far the take matching has got, Pause / Resume, and its switch for every take window." },
        { "Main window", "Enter", "In an input or output name box: done, the name is kept." },
        { "Main window", "right-click", "An input strip (not its keys or knob): Make left channel of stereo input - then click the input that becomes the right channel (Esc cancels). The right one takes the left one's preamp settings, and from then on a preamp control changed on either is changed on both. On a linked strip (ST L / ST R): Unlink stereo input." },
        { "Main window", "", "Project > Save this window: the take window, edit window or mixer you used last is kept as it is now. Fermata saves everything automatically as you work; this marks what you want to keep. When you quit, the windows that changed since you last saved them (Save, or Save this window) are listed with a tick each: untick one and it goes back to your last save of it (new recordings are always kept)." },
        { "Main window", "Esc", "In a name box: back to the name it had." },
        { "Main window", "Tab", "In a name box: on to the next one, to name the inputs one after another." },
        { "Main window", "double-click or Alt+click", "An input's gain knob: back to 20 dB (as the readout shows it)." },
        { "Main window", "double-click", "An input's gain readout: type a gain." },
        { "Main window", "click / right-click", "An input's HEADROOM (above its E S / S-L T keys): reset that input's peak / a menu - reset every input's, or show it as the peak in dBFS instead of the headroom. RESET PEAKS (top row) resets every input." },
        { "Main window", "double-click / Ctrl+click", "A level knob of the stage speaker boxes (top right): type a level / back to 0 dB." },
        { "Main window", "double-click / Ctrl+click", "A pan knob of the stage speaker boxes: type a position / back to the centre." },

        // ---------------------------------------------------------------------------------------------------- take window
        { "Take window", "Space", "Play or stop from the playhead. With Loop on and marks 1 and 2 set: round and round the marked part." },
        { "Take window", "R", "Start recording into this take window (in Rec mode: without it, a warning offers to switch it on)." },
        { "Take window", "1 / 2", "Mark 1 / mark 2 at the playhead, in the take under it." },
        { "Take window", "P", "Marks 1 and 2 round the whole of the selected take, on every track (then Ctrl + Alt + click tracks inside them to mark only those)." },
        { "Take window", "5", "Take marks 1 and 2 away." },
        { "Take window", "Enter", "Open ReTouch, DeClick & Denoise for the part between marks 1 and 2 (or the take selected whole)." },
        { "Take window", "3", "Send the marked part to the edit, after its last piece." },
        { "Take window", "4", "Put the marked part at the edit window's playhead, over the same length of what is there (nothing else moves)." },
        { "Take window", "Shift+4", "Insert the marked part at the edit window's playhead: what is there is cut in two and everything after it moves later. (Shift + click on To playhead does the same.)" },
        { "Take window", "D", "Mark the selected takes as duds, or take the mark off again (while recording: the take being recorded)." },
        { "Take window", "L", "Loop on / off." },
        { "Take window", "C", "Scroll so the playhead is in the middle of the window (the zoom stays)." },
        { "Take window", "J / Shift+J", "The same place in the music in the next / the previous take (found by matching the takes' harmony; duds are passed over; while playing, play goes on from there). Needs take matching switched on for the window (the MATCHING lamp in its marks row)." },
        { "Take window", "click / right-click", "The MATCHING lamp at the right of the marks row: switch take matching on or off for this window / how far it has got, Pause, every window's switch." },
        { "Take window", "Home / End", "Go to the start / to the end of the last take." },
        { "Take window", "Left / Right", "Zoom out / in, round the playhead." },
        { "Take window", "Shift+Left / Shift+Right", "Move the playhead 10 ms earlier / later." },
        { "Take window", "Up / Down", "All tracks smaller / bigger." },
        { "Take window", ". / ,", "Bigger / smaller waveforms (the take and edit windows share the size)." },
        { "Take window", "B", "The bars and notes box of the take clicked last - also the take being recorded: type its IN Bar, Tab, its OUT Bar, Enter." },
        { "Take window", "Shift+B", "Open or close the take chart: every take as a line along the piece's bars, the bars with no take / only one / two / three or more lit up and listed." },
        { "Take window", "N", "The OUT Bar of the take being recorded (or, when nothing is recording, of the take recorded last): one box, already filled in - type the bar it really ended in, Enter. A take with no bars yet gets the full IN / OUT box." },
        { "Take window", "V", "While recording: the take being recorded gets the bars (IN and OUT) of the take before it, at once - its waveform box shows them." },
        { "Take window", "Ctrl+A", "Select every take." },

        // ---------------------------------------------------------------------------------------------------- take window: a take's bars and notes box
        { "Take window: a take's bars and notes box", "", "Opened by B (the take clicked last), double-clicking a take's audio or right-clicking the strip above its tracks (also while it records)." },
        { "Take window: a take's bars and notes box", "Enter", "In IN Bar: on to OUT Bar. In OUT Bar: save." },
        { "Take window: a take's bars and notes box", "Tab", "On to the next box (IN, OUT, Notes, the buttons)." },
        { "Take window: a take's bars and notes box", "Ctrl+Enter", "In Notes: save (Enter there starts a new line)." },
        { "Take window: a take's bars and notes box", "Esc", "Close without saving." },
        // ---------------------------------------------------------------------------------------------------- the project's sample rate (3.9.0)
        { "Project sample rate box", "", "Comes up when a project is made (after its folder and name), before the first take of a project that has no rate yet, and from Project > Project sample rate." },
        { "Project sample rate box", "Enter", "OK, once a rate is picked (nothing is picked to begin with)." },
        { "Project sample rate box", "Esc", "Cancel: no project is made / nothing changes." },
        // ---------------------------------------------------------------------------------------------------- the take chart (3.8.0)
        { "Take chart", "", "Drops down from BARS at the left of a take window's bar strip (click it), or Shift+B. No take / Only 1 / 2 takes / 3 takes / 4+ light those bars up and list them (crimson, bright red, red-orange, yellow, light to dark green)." },
        { "Take chart", "BARS / Shift+B / Esc", "Put it away (also its Close button)." },
        { "Take chart", "click", "A take's line: that bar in that take. The ALL line, or a line of the list: that bar (the take window goes there)." },
        { "Take chart", "drag", "Its bottom-right corner: bigger or smaller (it keeps the size)." },

        // ---------------------------------------------------------------------------------------------------- take window: with the mouse
        { "Take window: with the mouse", "click / drag", "The time ruler: the playhead goes there; dragged along the ruler, it follows the mouse (scrubbing). While playing, play carries on from the spot clicked, or from where the drag let go." },
        { "Take window: with the mouse", "Ctrl+click", "A take: add it to the selected takes, or take it out (the selection Bounce Out's \"Selected takes\" uses). Ctrl + Shift + click does the same." },
        { "Take window: with the mouse", "Ctrl+click", "An empty part of the timeline: the selected takes stay selected (a plain click there lets go of them)." },
        { "Take window: with the mouse", "Alt+drag", "Over the tracks: a lasso. Every take it touches is selected." },
        { "Take window: with the mouse", "Alt+click", "Select only the take under the mouse (or none)." },
        { "Take window: with the mouse", "Ctrl+Alt+drag", "Across a take: marks 1 and 2 on only the tracks the drag covers." },
        { "Take window: with the mouse", "Ctrl+Alt+click", "A track inside marks 1 - 2 (no drag): mark only that track - the first click - or add one more, or take one out again; the last one out marks every track again." },
        { "Take window: with the mouse", "drag", "Mark 1 or mark 2 - its flag at the top, or its line over the tracks (a left-right arrow shows): move that mark. The tracks marked with Ctrl + Alt stay as they are." },
        { "Take window: with the mouse", "Ctrl+Shift+drag", "A take's clip up or down: move the clips on that track to another track (those of all the selected takes, if this take is one of them)." },
        { "Take window: with the mouse", "click", "A bar on the bar strip (under the head): the takes that have it are framed and the playhead goes to it. BARS at its left: the take chart." },
        { "Take window: with the mouse", "right-click", "The rainbow WaveColour button: how readily a knock is drawn black - Normal (as before), Light, Medium, Strong. A stronger weighting catches a quieter knock under the music (and more of a deep bass note). Black dots on the button show a weighting above Normal. The same in every window; WaveColour comes on if it was off." },
        { "Take window: with the mouse", "click / right-click", "The compress-waveforms button (beside the rainbow WaveColour button): compressed waveforms on / off - quiet passages drawn bigger, the picture only / how strong (Light, Medium, Strong). The same in the edit and trim windows." },
        { "Take window: with the mouse", "right-click", "The bar strip: how many bars the piece has (the strip is red until you enter it)." },
        { "Take window: with the mouse", "double-click", "A take's audio: its bars and notes box." },
        { "Take window: with the mouse", "double-click", "The strip above a take's tracks: Take settings (rename the take and its files)." },
        { "Take window: with the mouse", "right-click", "A take's audio: re-draw the waveform, bars and notes, room tone, or use it as the edit window's visual guide." },
        { "Take window: with the mouse", "right-click", "A \"Waiting for corrected audio\" block: re-link the corrected audio, or stop waiting and keep the original." },
        { "Take window: with the mouse", "Shift+click / Ctrl+click", "A track name: every track from the last one clicked to this one / this track in or out of the selection." },
        { "Take window: with the mouse", "right-click", "A track name: colour, move its clips onto another track, remove or delete tracks. With two mono tracks selected (click one name, Ctrl + click the other): make one stereo track from them." },

        // ---------------------------------------------------------------------------------------------------- take and edit windows: markers and the clock
        { "Take and edit windows: markers and the clock", "right-click", "The marker line (with Markers shown): add a marker, or clear them all." },
        { "Take and edit windows: markers and the clock", "right-click / double-click", "A marker: rename or delete it / rename it." },
        { "Take and edit windows: markers and the clock", "right-click", "The big clock: show the position as time, minutes:seconds, timecode or samples; the timecode frame rate and start; copy; go to a time." },
        { "Take and edit windows: markers and the clock", "double-click", "The big clock: type a time and go there." },

        // ---------------------------------------------------------------------------------------------------- edit window
        { "Edit window", "Space", "Play or stop." },
        { "Edit window", "1 / 2", "Mark 1 / mark 2 at the playhead (where you clicked, when nothing plays): the part the pitch, repair and ReHarmoniSer tools work on, and Loop plays." },
        { "Edit window", "P", "Marks 1 and 2 round the selected pieces, exactly at their start and end, on every track (then Ctrl + Alt + click tracks inside them to mark only those)." },
        { "Edit window", "5", "Take marks 1 and 2 away." },
        { "Edit window", "Enter", "Open ReTouch, DeClick & Denoise for the part between marks 1 and 2." },
        { "Edit window", "H", "Open the ReHarmoniSer for the part between marks 1 and 2." },
        { "Edit window", "T", "Open the selected join (a numbered flag) in the Trim window; with a piece selected, the join before it." },
        { "Edit window", "F1", "Snip at the playhead: the piece there is cut with a short, normal crossfade (an \"sn\" flag, not a numbered edit point). Playback carries on." },
        { "Edit window", "F3", "Glue mode on / off (the glue button): the snips light up in yellow; click one - its \"sn\" flag or its crossfade - to glue it back together, as though it had never been made (a moment rolled out of it comes back). Or right-click a snip: Remove snip." },
        { "Edit window", "Esc", "Leaves glue mode." },
        { "Edit window", "R", "Butt the selected piece up against the piece before it." },
        { "Edit window", "4", "Lay the part marked in a take window down at this edit's playhead, over the same length of what is there." },
        { "Edit window", "Shift+4", "Insert the part marked in a take window at this edit's playhead: what is there is cut in two and everything after it moves later." },
        { "Edit window", "Delete or Backspace", "Delete the selected piece (or overdub)." },
        { "Edit window", "L", "Loop on / off (round marks 1 and 2)." },
        { "Edit window", "C", "Scroll so the playhead is in the middle of the window." },
        { "Edit window", "Home / End", "Go to the start / to the end of the last piece." },
        { "Edit window", "Left / Right", "Zoom out / in." },
        { "Edit window", "Up / Down", "Shorter / taller tracks." },
        { "Edit window", ". / ,", "Bigger / smaller waveforms." },
        { "Edit window", "Up / Down, . / ,", "With the visual guide's name box selected (click GUIDE, its S or the take's name): the guide alone - shorter / taller, bigger / smaller waveform. It keeps its own size from then on (while the window is open). A click anywhere else ends it." },

        // ---------------------------------------------------------------------------------------------------- edit window: with the mouse
        { "Edit window: with the mouse", "Ctrl+click", "A piece: select the whole piece, ready to move. Ctrl + click more pieces to add them; Ctrl + click a selected one to take it out." },
        { "Edit window: with the mouse", "Ctrl+drag", "A piece (or drag it once it is selected): slide it sideways. An overdub moves freely." },
        { "Edit window: with the mouse", "Ctrl+click", "Outside the audio: the selected pieces stay selected (a plain click there lets go of them)." },
        { "Edit window: with the mouse", "Alt+drag", "Over the audio: a lasso. Every piece it touches is selected, overdubs too; the playhead stays where it is." },
        { "Edit window: with the mouse", "Alt+click", "Select only the piece under the mouse (or none)." },
        { "Edit window: with the mouse", "Ctrl+Alt+drag", "Over the audio: marks 1 and 2 on only the tracks the drag covers." },
        { "Edit window: with the mouse", "right-click", "The rainbow WaveColour button: how readily a knock is drawn black - Normal, Light, Medium, Strong (as in the take window)." },
        { "Edit window: with the mouse", "click / right-click", "The compress-waveforms button (beside the rainbow WaveColour button): compressed waveforms on / off - quiet passages drawn bigger, the picture only / how strong (Light, Medium, Strong). The same in the take and trim windows." },
        { "Edit window: with the mouse", "Ctrl+Alt+click", "A track inside marks 1 - 2 (no drag): mark only that track - the first click - or add one more, or take one out again; the last one out marks every track again." },
        { "Edit window: with the mouse", "drag", "The line of mark 1 or mark 2 (a left-right arrow shows within a few pixels of it): move that mark - it snaps to a piece's start or end, and never passes the other mark. The tracks marked with Ctrl + Alt stay as they are." },
        { "Edit window: with the mouse", "Shift+drag", "A track's clip up or down: move that clip to another track (it swaps with a clip already there; mono to mono, stereo to stereo)." },
        { "Edit window: with the mouse", "Shift+click / Ctrl+click", "A track name: every track from the last one clicked to this one / this track in or out of the selection." },
        { "Edit window: with the mouse", "double-click", "A piece: the Trim window, on the join before it." },
        { "Edit window: with the mouse", "double-click", "A numbered flag: the Trim window, on that join." },
        { "Edit window: with the mouse", "double-click", "A volume change's teal triangle at the top of the tracks, or the edge of its teal box (the box goes round only the tracks it changes - 3.45.0): change it again - the amount for each track and the fade in / fade out, heard as you change them (Play in the box) - or Remove this change. Right-click it: the same, as \"Change this volume change...\"." },
        { "Edit window: with the mouse", "right-click", "A piece: volume changes (to the next edit, between marks 1 and 2, between the fades), remove them, delete an overdub, re-draw the waveform." },
        { "Edit window: with the mouse", "right-click", "A piece, with marks 1 and 2 inside it: Find other versions - every place in the take window's takes where the marked music was played (\"possible\" ones: a false start, or the same notes elsewhere); Audition plays each in place, Use this one puts it in (everything after moves to fit; Ctrl+Z takes it out). Needs take matching switched on for that take window." },
        { "Edit window: with the mouse", "right-click", "An \"In ...\" block (Export for Processing > Open in a program: RX, Melodyne ...): bring in what the program has saved now, open it there again, show the files, or cancel and keep the original audio. (Each file saved there comes back by itself; Ctrl+Z takes it out.)" },
        { "Edit window: with the mouse", "right-click", "A track name: colour, remove tracks from this edit." },
        { "Edit window: with the mouse", "right-click", "The visual guide: move its start (to 0:00 or the playhead), hide it, remove it." },
        { "Edit window: with the mouse", "right-click or Shift+click or Ctrl+click", "An automation point (Automation on): delete it." },
        { "Edit window: with the mouse", "Alt+drag", "An automation point: move it ten times finer (with snap on, nothing is caught)." },
        { "Edit window: with the mouse", "click", "An automation lane with snap on (the magnet beside the padlock - on unless switched off): a new point goes on the line, at the level already there at that moment, however high or low the click was. Hold and drag it - or click it again and drag - to move it up or down from there; it catches the levels of the lane's other points (a dashed line across the lane shows it). Click the magnet: snap off for that lane - a point goes where you click." },
        // ---------------------------------------------------------------------------------------------------- trim window
        { "Trim window", "F2", "Audition the join (press again to stop)." },
        { "Trim window", "click / right-click", "The compress-waveforms button (right of Reset crossfade): compressed waveforms on / off / how strong. The same setting as the take and edit windows." },
        { "Trim window", "Space", "Stop what is playing." },
        { "Trim window", "Enter", "Keep the changes and go back to the edit window." },
        { "Trim window", "[ / ]", "Keep this fade and go to the join on the left / on the right." },
        { "Trim window", "A", "Copy A's fade-out onto B's fade-in." },
        { "Trim window", "Z", "Copy B's fade-in onto A's fade-out." },
        { "Trim window", "Left / Right", "Zoom out / in." },
        { "Trim window", ". / ,", "Bigger / smaller waveforms (the Trim window has its own size)." },
        { "Trim window", "wheel", "Over the waveforms: zoom in (wheel up) / out (wheel down)." },
        { "Trim window", "Shift+wheel", "Over the waveforms: move the view left / right (a sideways swipe or a tilt of the wheel does the same). The scroll bar under the waveforms reaches all of both takes." },
        { "Trim window", "drag", "A fade, the crossfade or a take dragged to the left or right edge: the view moves along (faster further out), so the join can be taken far away while zoomed in." },
        { "Trim window", "double-click or Alt+click", "\"Level of A (out)\" or \"Level of B (in)\": back to 0 dB." },

        // ---------------------------------------------------------------------------------------------------- mixer
        { "Mixer", "Enter", "In \"Mixer name\": rename the mixer (an edit's mixer renames the edit too)." },
        { "Mixer", "Enter / Esc", "In a VCA strip's name box: keep the name / back to the old one." },
        { "Mixer", "double-click", "An Int Bus or Ext Bus name plate (the name at the bottom of the strip): rename the bus. Enter or a click elsewhere keeps the new name, Esc goes back to the old one. The same name as in the Project Designer, and an undo step. Not while recording. (A single click on the plate still picks the colour, a moment later.)" },
        { "Mixer", "Shift+click / Ctrl+click", "A strip's name plate: every strip from the last one clicked to this one / this strip in or out of the selection." },
        { "Mixer", "Shift+Alt", "Held, with two or more strips selected: they are linked while you hold them (bold orange outlines). Faders, sends and pans move together; M, S and the sends' mute and pre are set the same; "
                                "so are the output (OUT, and where it goes: hold the keys as you open its list), LC, polarity and the low cut's settings, a send to another track (+) and the colour; "
                                "plug-ins go on, come off or are bypassed on every one." },
        { "Mixer", "right-click", "A strip's name plate: colour (all the selected strips, when it is one of them), hide strips from this mixer, select all or none." },
        { "Mixer", "drag", "An Int Bus, an Ext Bus that is not a main output, or a VCA, by its coloured bar or its name plate, sideways: the strip moves there (a bright line shows where; the view scrolls at its edges). Kept with the mixer. Right-click its name plate: next to the strips that feed it (a VCA: that it controls), or back to its usual place." },
        { "Mixer", "click", "An edit's own mixer, in the METERS strip: Match edit hides the strips of tracks that are not in the edit and shows those that are (once, now); Show all strips brings every hidden strip back." },
        { "Mixer", "double-click or Ctrl+click", "A fader cap: back to 0 dB." },
        { "Mixer", "double-click", "The dB number under a fader: type a level." },
        { "Mixer", "click", "Press and hold the small arrows beside the fader number: 1 dB steps that keep repeating." },
        { "Mixer", "double-click or Ctrl+click", "A PAN slider: back to the centre (a stereo track's two, side by side: the left one hard left, the right one hard right)." },
        { "Mixer", "double-click", "A send dial, or the number under it: type a level (just the little box; \"off\" or -inf turns the send off)." },
        { "Mixer", "Ctrl+click", "A send dial: 0 dB." },
        { "Mixer", "click", "A send dial (and wait a moment): its panel - pre / post fader, MUTE, follow mute, its inserts and a fader." },
        { "Mixer", "click", "The transport keys right of \"Mixer name\" (|< << Play >> >|): back to the start, back 5 s, play / stop, forward 5 s, to the end - in the window this mixer plays: an edit's mixer its edit, the processing and cue mixers the take window playing (or the one used last). They work with that window closed too." },
        { "Mixer", "double-click or Alt+click", "The fader in a send's panel: 0 dB." },
        { "Mixer", "Alt+drag or Shift+drag", "An insert's name onto another slot, in any mixer: an exact copy with all its settings (a plain drag moves the plug-in; two plug-ins swap places)." },
        { "Mixer", "right-click", "An insert slot: open, bypass, remove or replace the plug-in, scan for plug-ins." },
        { "Mixer", "right-click", "The LC key (or press and hold it): the low-cut panel. A click switches the low cut on or off." },
        { "Mixer", "double-click or Alt+click / double-click", "The low-cut panel's slider: back to 80 Hz / its value: type a frequency." },
        { "Mixer", "wheel / Shift+wheel", "Over the strips: scroll left and right / up and down (when the window is shorter than the strips). Over the inserts and sends when they have a scroll bar: up and down." },
        { "Mixer", "drag", "The line above the pans (a grip in the middle of every strip): up for taller faders - the inputs, inserts and sends above it are covered and get a scroll bar at the right - down for shorter ones. Remembered for every mixer." },
        { "Mixer", "double-click", "The line above the pans: the faders back to their normal size." },

        // ---------------------------------------------------------------------------------------------------- retouch, declick & denoise (spectral repair)
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "Space", "Play the part of the picture in view (from the red playhead if one is placed), as \"Listen to\" says, or stop." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "R", "The ReTouch tab, and ReTouch the box." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "G", "The Gain tab, and the gain set there applied to the box at once (silence to +18 dB; set the amount on the tab's slider, then G for each box)." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "D", "The De-click tab, and de-click the box's time (everything, with no box)." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "L", "The Denoise tab, and the box becomes the room tone (at least half a second)." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "N", "The Denoise tab, and denoise the box with the room tone." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "C", "The Consonants tab: Copy consonant / Copy and remove keep the consonant in the box (every track and channel) with the project." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "P", "Paste the consonant chosen in the Consonants list at the red playhead (click the time ruler to place it)." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", ", / .", "Nudge the consonant just pasted 1 ms earlier / later." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "Shift+, / Shift+.", "Nudge the consonant just pasted 5 ms earlier / later." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "H", "The De-hum tab, and look for the hums in the box (3.44.0): every tone that never changes - the mains, a buzz and its harmonics (one line), a whistle - listed loudest first with a tick and its pitch. Tick, untick or retype, then press De-hum. Press H again afterwards to see what is left." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "Shift+H", "Harmonics: boxes on the harmonics of the note in the box (the fundamental is the 1st), or take them away (3.33.0; it was H until 3.44.0)." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "S / T / B", "Select with a Rectangle / a stretch of Time (every pitch) / the Brush (3.33.0)." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "[ / ]", "The brush smaller / larger." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "Z / \\ / X", "Before <-> After (Side) to L (0 %, only the sound before the box) / C (50 %) / R (100 %, only after it) (3.33.0)." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "Click", "A click in the picture without a drag puts the red playhead there and takes the selection away (3.35.0). Shift or Alt + click keeps it; so does a click on the time ruler." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "Left / Right", "Zoom out / in, in time." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "Up / Down", "Zoom out / in, in pitch." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "Esc", "Close, as Cancel (it asks first when fixes have not been written back)." },
        { "ReTouch, DeClick & Denoise (Spectral Repair)", "", "In the OUT STREAM and IN STREAM windows (a marked part with an edit point in it) the keys are the same, but Esc does nothing: close them with the X or Cancel. The same goes for the pitch and ReHarmoniSer stream windows." },

        // ---------------------------------------------------------------------------------------------------- noise mode
        { "Noise mode", "Space", "Play from the red playhead (or the left of the page) to the end of the edit, turning the pages (Loop repeats the page), or stop." },
        { "Noise mode", "R / G / D / L / N", "As in Spectral Repair, but each fix goes straight into the edit; D with no box covers the whole edit, and asks first (3.38.1: Enter and Esc are Cancel)." },
        { "Noise mode", "H", "The De-hum tab, and look for the hums in the box, as in Spectral Repair (3.44.0). De-hum puts the fix straight into the edit; with no box it covers the whole edit, and asks first." },
        { "Noise mode", "Shift+H / S / T / B / [ / ] / Z / \\ / X / Click", "As in Spectral Repair: harmonics, the selection tools and the brush's size, L / C / R, and a click for the playhead (3.33.0)." },
        { "Noise mode", "C / P / , / .", "Consonants (3.36.0), as in Spectral Repair: C the tab, P pastes the chosen consonant at the playhead, comma and full stop nudge the one just pasted 1 ms (with Shift 5 ms)." },
        { "Noise mode", "Home / End", "Playhead to the start / the end of the edit." },
        { "Noise mode", "Left / Right", "Zoom out / in, in time." },
        { "Noise mode", "Up / Down", "Zoom out / in, in pitch." },
        { "Noise mode", "Esc", "Close, as \"Finished de-noising\" (asks Keep / Throw away / Cancel when fixes have not been written back)." },

        // ---------------------------------------------------------------------------------------------------- spectral repair and noise mode: with the mouse
        { "Spectral Repair and Noise mode: with the mouse", "drag", "The picture, outside the box: draw a new box (time and pitch)." },
        { "Spectral Repair and Noise mode: with the mouse", "Shift+drag", "Another box added to the selection: ReTouch, Gain, De-click, Denoise and Room tone work on all of it at once (each rectangle on its own, the same settings)." },
        { "Spectral Repair and Noise mode: with the mouse", "Alt+drag", "The part you draw over is taken out of the selection (a corner cut out of a box, say); where nothing is selected nothing happens." },
        { "Spectral Repair and Noise mode: with the mouse", "drag", "A side of the box: move that side only (left / right: the time, top / bottom: the pitch). A corner: its two sides." },
        { "Spectral Repair and Noise mode: with the mouse", "drag", "Inside the box (the four-way arrow): move the whole box, the same size, in time and pitch. It stops at the start and end and at the top and bottom of the pitch range." },
        { "Spectral Repair and Noise mode: with the mouse", "", "Wherever the mouse is over the picture, the pitch scale shows the frequency at that height (yellow tag), and the line under the picture the exact frequency, the nearest note (at the pitch pipe's A) and the time." },
        { "Spectral Repair and Noise mode: with the mouse", "click / Shift+click or right-click", "The colour roll (bottom left of the picture): the next / the previous colour look." },
        { "Spectral Repair and Noise mode: with the mouse", "wheel", "Over the colour roll: roll through the colour looks." },
        { "Spectral Repair and Noise mode: with the mouse", "double-click", "The colour roll: back to the standard colours." },
        { "Spectral Repair and Noise mode: with the mouse", "double-click", "The time ruler above the picture: show everything again." },
        { "Spectral Repair and Noise mode: with the mouse", "double-click", "The picture: clear the box." },
        { "Spectral Repair and Noise mode: with the mouse", "double-click or Alt+click", "Denoise Amount / Residual: back to 0 dB / -12 dB." },
        { "Spectral Repair and Noise mode: with the mouse", "double-click or Alt+click", "The slider of the box's gain: back to 0 dB." },
        { "Spectral Repair and Noise mode: with the mouse", "double-click or Alt+click", "De-hum's Soft <-> Hard slider: back to 30 dB (3.44.0)." },
        { "Spectral Repair and Noise mode: with the mouse", "double-click or Alt+click", "Spectral Repair, Consonants tab: the Level slider back to 0 dB." },
        { "Spectral Repair and Noise mode: with the mouse", "right-click / double-click", "Noise mode's clock: as the take and edit windows' clock." },

        // ---------------------------------------------------------------------------------------------------- pitch
        { "Pitch", "Space", "Pitch curve: play the part of the line in view, from the playhead placed on the ruler (round and round with Loop lit), or stop." },
        { "Pitch", "R", "Pitch curve: Preview, work out the corrected sound and play it." },
        { "Pitch", "Left / Right", "Pitch curve: zoom out / in, in time." },
        { "Pitch", "Up / Down", "Pitch curve: zoom out / in, in cents." },
        { "Pitch", "Delete or Backspace", "Pitch curve: remove the selected point." },
        { "Pitch", "Esc", "Close the window (a preview that was not accepted is put back)." },
        { "Pitch", "right-click or double-click", "A point on the pitch curve: delete it." },
        { "Pitch", "double-click", "The pitch curve's time ruler: the whole part again." },
        { "Pitch", "double-click or Alt+click", "Pitch process: Semitones or Cents back to 0." },

        // ---------------------------------------------------------------------------------------------------- reharmoniser
        { "ReHarmoniSer", "Space", "Play from the playhead, or stop (between the loop markers with Loop on; with Isolate on, just the picked note)." },
        { "ReHarmoniSer", "Enter", "Apply correction: the picked note goes into Corrected Notes." },
        { "ReHarmoniSer", "Ctrl+Z / Ctrl+Shift+Z", "Undo / redo: with a note picked, its setting changes; with none, the last corrected note comes out / goes back." },
        { "ReHarmoniSer", "1 / 2", "Loop start / loop end marker at the playhead." },
        { "ReHarmoniSer", "5", "Clear both loop markers." },
        { "ReHarmoniSer", "L", "Loop on / off." },
        { "ReHarmoniSer", "Left / Right", "Zoom out / in, in time." },
        { "ReHarmoniSer", "Up / Down", "Zoom out / in, in pitch." },
        { "ReHarmoniSer", "Esc", "With the \"Pitch over time\" chart open: close the chart." },
        { "ReHarmoniSer", "Esc", "Otherwise: close the window. With corrected notes not yet written back it asks first (Write back to clip keeps them); so do Cancel and the X." },
        { "ReHarmoniSer", "Enter", "In the \"meant to be\" box: the target note (F5, Ab4 ...). In \"Reference A4\": the A (400 - 480 Hz)." },
        { "ReHarmoniSer", "wheel / Ctrl+wheel / Shift+wheel", "Over the picture: zoom in time round the mouse / zoom the pitch range / scroll in time." },
        { "ReHarmoniSer", "drag", "The time ruler, zoomed in: move along in time (the picture follows the mouse). A click on it puts the playhead; the playhead's white triangle drags the playhead (with everything in view, so does the whole ruler)." },
        { "ReHarmoniSer", "drag", "The piano keys on the left: move up and down in pitch." },
        { "ReHarmoniSer", "double-click", "The time ruler: the whole length." },
        { "ReHarmoniSer", "double-click", "The picture: clear the selection and put the playhead there." },
        { "ReHarmoniSer", "double-click", "The Colour wheel (View): back to the original amber." },
        { "ReHarmoniSer", "double-click or Alt+click", "A channel's brightness slider (See mixer): back to 0 dB." },

        // ---------------------------------------------------------------------------------------------------- retimer
        { "ReTimEr", "Space", "Stop, if anything plays; otherwise Play retimed (it makes the retimed audio first if it needs to)." },
        { "ReTimEr", "O", "Play original: the edit as it is, from the pre-roll to 2 s past the part." },
        { "ReTimEr", "Enter", "In a number box (new length, %, ramps): tidy the number and give the keyboard back." },
        { "ReTimEr", "Esc", "Close, as Cancel (the edit is left as it was)." },

        // ---------------------------------------------------------------------------------------------------- mastering
        { "Mastering", "", "These are the keys of the Exports window's DDP tab (the window was called Mastering until 3.76.0, and this list keeps that name). On its other tabs, Space plays the take or edit window you used last." },
        { "Mastering", "Space", "Play the disc from the playhead, or stop." },
        { "Mastering", "Left / Right", "Zoom out / in, on the playhead." },
        { "Mastering", "C", "Scroll so the playhead is in the middle." },
        { "Mastering", "Home", "Playhead to the start of the disc." },
        { "Mastering", "Enter", "In \"Pause before this track\": that pause for the selected track." },
        { "Mastering", "wheel / Ctrl+wheel", "Over the disc: scroll / zoom round the playhead." },
        { "Mastering", "double-click", "A PQ flag: back to its automatic place." },

        // ---------------------------------------------------------------------------------------------------- display organiser
        { "Display Organiser", "Enter", "Arrange: the windows go where the picture shows them." },
        { "Display Organiser", "double-click", "A layout in the list: load it and arrange the windows at once." },

        // ---------------------------------------------------------------------------------------------------- take display
        { "Take Display", "Esc", "On a Take Display screen: close that screen." },
        { "Take Display", "double-click", "A Take Display screen: close it." },
        { "Take Display", "Space", "On a screen: play or stop the take or edit window you used last." },
        { "Take Display", "Enter", "In the session timer's box (the Take Display window): set the timer, as Start." },

        // ---------------------------------------------------------------------------------------------------- plug-in windows
        { "Plug-in windows", "Space", "Play or stop the take or edit window you used last, even while the plug-in has the keyboard." },
        { "Plug-in windows", "R", "Left to the plug-in (for typing a preset name): it never starts a recording." },

        // ---------------------------------------------------------------------------------------------------- project designer
        { "Project Designer", "Delete or Backspace", "Delete the selected track, bus, VCA or mixer (it asks first; not while recording)." },
        { "Project Designer", "right-click", "A row: move it up or down, delete it; on Mixers, open or delete the mixer." },
        { "Project Designer", "double-click", "A Name cell: rename it there." },

        // ---------------------------------------------------------------------------------------------------- pitch pipe, click and media
        { "Pitch pipe, Click and Media", "click", "Pitch pipe: press and hold a key: it sounds while you hold it. In chord mode, press and hold Play: the lit keys sound together." },
        { "Pitch pipe, Click and Media", "double-click or Alt+click", "Pitch pipe: the Tone slider back to its usual setting." },
        { "Pitch pipe, Click and Media", "double-click or Alt+click", "Click: the quaver tick's level back to -20 dB." },
        { "Pitch pipe, Click and Media", "double-click", "Media: a file is shown in Explorer; a folder opens or closes." },
        { "Pitch pipe, Click and Media", "right-click", "Media: show the file or folder in Explorer, or copy its path." },

        // ---------------------------------------------------------------------------------------------------- boxes for typing a value
        { "Boxes for typing a value", "Enter / Esc", "The box that opens when you double-click a fader number, a pan, the low cut, a preamp gain, a stage knob or a clock: apply / leave the value alone." },
        { "Boxes for typing a value", "Enter / Esc", "A marker's name box: keep the name / the old name." },
        { "Boxes for typing a value", "Enter", "Align spot mics, a spot's delay box: that delay for this piece (0 - 200 ms)." },
        { "Boxes for typing a value", "Enter", "Import AAF as edit windows, the silence-gap box: read the AAF again with that gap." },
        { "Boxes for typing a value", "Esc", "The Plug-ins window's search box: clear the search." },
        { "Boxes for typing a value", "double-click or Alt+click", "The faders of the Change volume windows (right-click a piece in the edit window): back to 0 dB." },

        // ---------------------------------------------------------------------------------------------------- messages and questions
        { "Messages and questions", "Enter", "OK: the main button." },
        { "Messages and questions", "Esc", "Close or cancel. Esc also closes the Preamps, Plug-ins, CPU cores, Change volume, Loudness report and Import windows, and the repair tools' settings boxes." },
        { "Messages and questions", "Tab", "On to the next box or button." },
        { "Messages and questions", "", "A question with two buttons: Enter or the first button's first letter presses the first, Esc or the second's first letter the second. With three: the first letter of each of the first two, Esc for the third." },
        { "Messages and questions", "O / I / B", "Export for Processing, when the marked part has an edit point in it: send the Out stream / the In stream / Both." },
    };
    return v;
}

/** The sections of fermata's list that are shown in fermata:Live's HotKeys window too (fermata:Live's Fixer opens the same ReTouch window). */
inline bool sharedWithLive (const char* section)
{
    const juce::String s (section);
    return s == "ReTouch, DeClick & Denoise (Spectral Repair)" || s == "Spectral Repair and Noise mode: with the mouse";
}

/** fermata:Live's keys (shown by fermata:Live's HotKeys window, with the ReTouch window's from fermata's list). Their sections start "fermata:Live: "
    so that they never meet fermata's in the key file. */
inline const std::vector<Line>& liveLines()
{
    static const std::vector<Line> v
    {
        // ---------------------------------------------------------------------------------------------------- the Primary
        { "fermata:Live: the Primary's take window", "", "In the Primary's take window, and in its main window and mixer (their keys go on to the take window). Not while you are typing in a box: the keys then type." },
        { "fermata:Live: the Primary's take window", "Space", "Play or stop (not while a take records)." },
        { "fermata:Live: the Primary's take window", "R", "Start a take. A take that is recording is ended only by STOP TAKE (the red button): R never does." },
        { "fermata:Live: the Primary's take window", "Enter", "A flag at the playhead (while recording: now). With a note open: save the note." },
        { "fermata:Live: the Primary's take window", "N", "A note at the playhead (while recording: now)." },
        { "fermata:Live: the Primary's take window", "M", "Open or close the mixer." },
        { "fermata:Live: the Primary's take window", "1 / 2", "Mark 1 / mark 2 at the playhead, in the merged take under it (what Bounce out bounces between)." },
        { "fermata:Live: the Primary's take window", "P", "Marks 1 and 2 round the whole take under the playhead." },
        { "fermata:Live: the Primary's take window", "5", "Take marks 1 and 2 away." },
        { "fermata:Live: the Primary's take window", "L", "Loop on / off." },
        { "fermata:Live: the Primary's take window", "C", "Scroll so the playhead is in the middle of the window." },
        { "fermata:Live: the Primary's take window", "Home / End", "Go to the start / to the end." },
        { "fermata:Live: the Primary's take window", "Shift+Left / Shift+Right", "Move the playhead 10 ms earlier / later (not while recording)." },
        { "fermata:Live: the Primary's take window", "Left / Right", "Zoom out / in, round the playhead." },
        { "fermata:Live: the Primary's take window", "Up / Down", "All tracks smaller / bigger." },
        { "fermata:Live: the Primary's take window", ". or = / , or -", "Bigger / smaller waveforms." },
        { "fermata:Live: the Primary's take window", "Num +", "Talkback on / off (also from the Primary's main window)." },
        { "fermata:Live: the Primary's take window", "", "In the Primary's main window: click an input's HEADROOM (at the bottom of its strip) to reset that input's peak, right-click it to reset every input's or show the peak in dBFS instead; RESET PEAKS (beside DRIVER INPUTS) resets every input." },

        // ---------------------------------------------------------------------------------------------------- the Fixer
        { "fermata:Live: the Fixer", "", "In the Fixer's window, and in its mixer and second-seat windows (their keys go on to the Fixer). Not while you are typing in a box." },
        { "fermata:Live: the Fixer", "Space", "Play or stop." },
        { "fermata:Live: the Fixer", "Enter", "Into the reply box of the flags list, to reply to the chosen flag." },
        { "fermata:Live: the Fixer", "N", "A note at the flag point (or the playhead)." },
        { "fermata:Live: the Fixer", "M", "Open or close the Fixer's mixer." },
        { "fermata:Live: the Fixer", "F", "Follow on / off: play just behind the recording, by the delay." },
        { "fermata:Live: the Fixer", "P", "Select the whole of the chosen take (click its header first) to fix." },
        { "fermata:Live: the Fixer", "C", "Scroll so the playhead is in the middle of the window." },
        { "fermata:Live: the Fixer", "Left / Right", "Zoom out / in, round the playhead." },
        { "fermata:Live: the Fixer", "Up / Down", "All tracks smaller / bigger." },
        { "fermata:Live: the Fixer", ". or = / , or -", "Bigger / smaller waveforms." },
    };
    return v;
}
} // namespace td::hotkeys
