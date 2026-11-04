# fermata:Live - version history

fermata:Live is the live-concert recorder that goes with fermata (see `docs/LIVE-BRIEF.md`). Version numbers are MAJOR.MINOR.PATCH. The number is set in one place, the first lines of `src/live/Live.cmake`, and appears in the window's title and in the properties of `fermata-Live.exe`.

- **PATCH** (0.1.1, 0.1.2 ...): bug fixes and small tweaks only.
- **MINOR** (0.2.0, 0.3.0 ...): a new feature or a visible change.
- **MAJOR** (1.0.0 ...): the first version for real concerts, and later large changes in how the program works.

A line that starts with **Shared:** is a change to the code fermata shares (see `COMPATIBILITY.md`), so fermata's next build mentions it too.

## 0.1.0
The first build: the program's frame, built from fermata's shared core in the same GitHub build as fermata.
- `fermata-Live.exe` opens one window, titled "fermata:Live 0.1.0". It shows the sample rates the program will offer (44.1, 48, 96 and 192 kHz, read from fermata's own list) and the version of the compatibility contract it was built with. It does not record yet.
- Only one copy runs at a time.
- Its icon is fermata's with the dot in red. This is a placeholder until a real one is chosen.
