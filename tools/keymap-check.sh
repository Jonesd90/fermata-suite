#!/bin/bash
# Developer helper (fermata 3.69.0 / fermata:Live 0.10.16): builds and runs tests/keymap_test.cpp - src/ui/KeyMap.h, the keys that can be changed
# (HotKeys): what a key press becomes where the keyboard is, the standard keys read from both programs' lists, the key file. Run it whenever
# KeyMap.h or HotKeysLines.h changes (a line's keys are the standard keys: the test checks every one can be read). Needs no screen.
# Usage: tools/keymap-check.sh /path/to/JUCE [out folder]      (default /tmp/fermata-keymap-check; JUCE is built there once)
set -e
JUCE=${1:?path to JUCE}
OUT=${2:-/tmp/fermata-keymap-check}
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$OUT"
JOBS=$(nproc 2>/dev/null || echo 2)
MODS="juce_core juce_events juce_graphics juce_data_structures juce_gui_basics"
DEFS="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XSHM=0 -DNDEBUG=1"
for m in $MODS; do DEFS="$DEFS -DJUCE_MODULE_AVAILABLE_$m=1"; done
export CXX="g++ -std=c++17 -O1 -I$JUCE/modules $(pkg-config --cflags freetype2 2>/dev/null) $DEFS -w" OUT
for m in $MODS; do
    [ -f "$OUT/$m.o" ] || { echo "#include <$m/$m.cpp>" > "$OUT/$m.cpp"; echo "$m"; }
done | xargs -r -P"$JOBS" -I{} bash -c 'echo "building JUCE {}"; $CXX -c "$OUT/{}.cpp" -o "$OUT/{}.o"'
[ -f "$OUT/juce_graphics_Harfbuzz.o" ] || { echo "#include <juce_graphics/juce_graphics_Harfbuzz.cpp>" > "$OUT/juce_graphics_Harfbuzz.cpp"; $CXX -c "$OUT/juce_graphics_Harfbuzz.cpp" -o "$OUT/juce_graphics_Harfbuzz.o"; }
[ -f "$OUT/juce_graphics_Sheenbidi.o" ] || gcc -O1 -I"$JUCE/modules" -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -w -c "$JUCE/modules/juce_graphics/juce_graphics_Sheenbidi.c" -o "$OUT/juce_graphics_Sheenbidi.o"
[ -f "$OUT/ct.o" ] || { echo "#include <juce_core/juce_core_CompilationTime.cpp>" > "$OUT/ct.cpp"; $CXX -c "$OUT/ct.cpp" -o "$OUT/ct.o"; }
g++ -std=c++17 -O1 -I"$JUCE/modules" $(pkg-config --cflags freetype2 2>/dev/null) $DEFS -Wall -Wextra -c "$HERE/tests/keymap_test.cpp" -o "$OUT/keymap_test.o"
g++ "$OUT/keymap_test.o" "$OUT"/juce_*.o "$OUT/ct.o" -o "$OUT/keymap_test" -lfreetype -lfontconfig -lpthread -ldl -lrt -lX11 2>/dev/null \
  || g++ "$OUT/keymap_test.o" "$OUT"/juce_*.o "$OUT/ct.o" -o "$OUT/keymap_test" -lfreetype -lfontconfig -lpthread -ldl -lrt
"$OUT/keymap_test"
