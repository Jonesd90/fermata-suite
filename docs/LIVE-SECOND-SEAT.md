# fermata:Live: the second seat

A mouse and a keyboard of the Fixer's own, on the PC the Primary uses, so two people can work fermata:Live at the same time (0.6.0).

Windows normally makes every mouse move the one pointer and sends every keyboard to whichever window is in front, so two people cannot share a PC.
With the second seat on:
- the Fixer's mouse moves an **orange pointer** of its own, with the Fixer's name beside it, kept on the Fixer's screen. Its clicks, drags and
  wheel work only the Fixer's windows (the main window, its mixer, the ReTouch window, the seat panel);
- the Fixer's keyboard types only into the Fixer: into what its mouse last clicked (the reply box, a note, the name box), else the Fixer's keys
  (Space, N, M, P, the arrows ...);
- the Primary's mouse and keyboard carry on exactly as before and are never touched. The PC's keyboard stays with the Primary's window: nothing
  the Fixer does takes it.

## What it needs

The free **Interception** driver (by Francisco Lopes: https://github.com/oblitum/Interception). It sits between Windows and the keyboards and mice,
and lets one program take chosen devices for itself, so that Windows never sees them. fermata:Live takes only the two you choose, and only while
the Fixer is open with the seat on.

1. Download the latest release (`Interception.zip`) from the releases page of that site, and unzip it.
2. Open a **Command Prompt as administrator** (Start, type cmd, right-click Command Prompt, Run as administrator). Do not double-click the
   installer.
3. Go to the installer's folder (`cd` to `Interception\command line installer`) and run `install-interception.exe /install`.
4. **Restart the PC.**
5. Copy `interception.dll` from the zip's `library\x64` folder into the folder that holds `fermata-Live.exe`.

To take it off again: the same, with `install-interception.exe /uninstall`, then restart.

**Licence.** The library (`interception.dll`) is under the LGPL for non-commercial use, and the driver and installer may be passed on with it. For
commercial use (recording concerts for clients is likely to count) the author offers commercial licences, asked for by email (his address is on the
site). This is not legal advice: read the site's Licensing section and decide.

## Using it

1. Plug in the Fixer's own USB keyboard and mouse (any - a different colour helps tell them apart).
2. In the Fixer's window, **Second seat...** (top right) opens the panel.
3. **Learn** beside Keyboard, then press any key on the Fixer's keyboard. **Learn** beside Mouse, then click with the Fixer's mouse. Nothing is
   taken while learning: every key and click still goes to Windows as normal (for 15 seconds at most).
4. **Start the second seat.** The Fixer's mouse and keyboard now work the Fixer only, and the button at the top says **Seat: ON** in orange. The
   choice is remembered: next time the Fixer opens, the seat starts by itself (if the devices are plugged in).
5. **Pointer speed** sets how fast the orange pointer moves.

A drop-down list (the kind of fix, the mixer's output pair, the ReTouch window's settings) opens as the seat's own list, with an orange edge: click
an item, or press on the list, drag and let go on an item; the wheel scrolls it, Up / Down and Enter choose with the Fixer's keyboard, Esc
closes it.

## Giving them back

Any of these gives both devices back to Windows at once (they become ordinary again):
- **Esc five times quickly** on the Fixer's keyboard (within two seconds);
- **Let go** in the panel (either mouse can click it);
- **closing the Fixer** (or the Fixer stopping in any way: Windows takes the devices back when the program ends).

Safety, because this is the recording PC:
- the recorder never uses the driver; only the Fixer does, and the recording never waits for it;
- the PC's only keyboard, or only mouse, is refused;
- only the two chosen devices are taken; everything else goes straight on to Windows.

## Limits

- The seat's mouse cannot move or resize windows by their title bars (those belong to Windows): move them with the ordinary mouse.
- Tooltips appear for the ordinary mouse only.
- A text box typed into by the seat shows no blinking cursor (the PC's keyboard is the Primary's); the text goes in where it is typed.
- With the seat on, a click on a Fixer window with the **ordinary** mouse works, but the PC's keyboard goes straight back to the window that had
  it (the Primary's): the Fixer's keyboard is the one for the Fixer.
- **Unplugging and plugging back**: the driver gives a device a new number each time it is plugged in again (or the PC wakes from hibernation).
  After about 10 times for a keyboard (20 for a mouse) that device stops working until the PC is restarted - a limit of the driver. Leave the
  Fixer's keyboard and mouse plugged in, and restart the PC before a concert. If the Fixer's devices are not found, Learn them again.
- Two keyboards (or mice) of the same make and model look the same to the driver. The seat remembers which is which by where it is plugged in;
  after one of them has been unplugged, it will not guess - it does not start, says the Fixer's keyboard (or mouse) is not plugged in, and
  Learn chooses again. It never starts with one of the two chosen devices missing.
- If the seat is started with only a mouse learned, the Fixer has no keyboard of its own and the PC's keyboard works as before.
- Only on Windows. Built and tried on Linux with pretend devices; **not yet tried with the real driver** - see the test list in
  `docs/LIVE-DEVELOPMENT-NOTES.md` (0.6.0).
