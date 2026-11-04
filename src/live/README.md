# fermata:Live

The recorder for live concerts, from the makers of fermata. It is built for recordings that can never stop or restart, and its projects open in
fermata.

## Installing

fermata:Live is a single program, `fermata-Live.exe`. Put it anywhere and double-click it. The first time, Windows may say it "protected your PC"
because the program is not signed: click **More info**, then **Run anyway**.

## This version (0.2.0): the recorder test bench

The recorder itself is built and this window drives it. It is not the final look: the main window, the Primary and Fixer windows come later.

1. **New project...** (choose where, type a name) or **Open project...** (a fermata project). Type the **Take window** name (the piece or half).
2. Choose the driver type (ASIO), the device, the sample rate and the buffer ("Largest the driver offers" is the safest), and press **Open device**.
   "Test signal" is a pretend 16-input device for trying everything without an interface.
3. **Backup drives**: Add a folder on each backup drive (the primary is the project's own drive). Set **Pre-take** (seconds before Record that the
   take includes, up to 300) and **Buffer** (how long a drive may stall before it loses anything, 30 - 60 s). **Inputs to record**: with a project
   that has no tracks, this many inputs are recorded as mono tracks; a project with tracks records its tracks.
4. **Start session (pre-take)**: the recorder writes from now on. **RECORD** marks the start of a take, **STOP** its end; the merge follows and the
   take goes into the project. The log at the bottom says what happened, and each drive's line shows how far behind it is, its longest write,
   its chunks and gaps - or that it was dropped, and why.
5. **End session** when you are done. **Carry on last session** continues a session that was cut off (the PC or the program stopped).

The recording runs in a separate program, `fermata-Live.exe --recorder`, started by this window (it has no window of its own; it is in Task
Manager). It keeps recording if this window is closed or crashes; start fermata:Live again and it is found. Closing the window while a take
records leaves it recording on purpose - press STOP first if you mean to stop. Logs: `Documents\Fermata\fermata-live-log.txt` (the window) and
`fermata-live-recorder-log.txt` (the recorder).
