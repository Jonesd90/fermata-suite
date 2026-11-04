#!/bin/bash
# The compatibility check between fermata and fermata:Live (fermata 3.30.2 / fermata:Live 0.1.0): builds tests/compat_test.cpp with the WHOLE shared
# core (src/core) against JUCE on Linux - and nothing from either program's own folders, so it also shows that the shared core builds on its own -
# then runs it. Every conversation runs it before delivering a change (see CLAUDE.md), and the GitHub build runs it on every upload.
# Usage: tools/compat-check.sh /path/to/JUCE [out folder]       (out folder: default /tmp/fermata-compat-build; JUCE is built there once)
set -e
JUCE=${1:?path to JUCE}
OUT=${2:-/tmp/fermata-compat-build}
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$OUT"
JOBS=$(nproc 2>/dev/null || echo 2)
DEFS="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_ALSA=0 -DJUCE_JACK=0 -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XSHM=0 -DJUCE_MODAL_LOOPS_PERMITTED=0 -DNDEBUG=1"
export CXX="g++ -std=c++17 -O0 -I$JUCE/modules $DEFS -w" OUT

# JUCE's modules that the shared core uses (built once per out folder)
for m in juce_core juce_events juce_audio_basics juce_audio_formats juce_audio_devices juce_data_structures; do
    [ -f "$OUT/$m.o" ] || { echo "#include <$m/$m.cpp>" > "$OUT/$m.cpp"; echo "$m"; }
done | xargs -r -P"$JOBS" -I{} bash -c 'echo "building JUCE {}"; $CXX -c "$OUT/{}.cpp" -o "$OUT/{}.o"'
[ -f "$OUT/ct.o" ] || { echo "#include <juce_core/juce_core_CompilationTime.cpp>" > "$OUT/ct.cpp"; $CXX -c "$OUT/ct.cpp" -o "$OUT/ct.o"; }

# every file of the shared core: built again when it, or anything in src/core or third_party, is newer than its object
NEWEST=$(ls -t "$HERE"/src/core/* "$HERE"/src/reharmoniser/third_party/* $(find "$HERE/third_party" -type f) | head -1)
for f in "$HERE"/src/core/*.cpp; do
    o="$OUT/core_$(basename "$f" .cpp).o"
    [ -f "$o" ] && [ "$o" -nt "$NEWEST" ] || echo "$f"
done | xargs -r -P"$JOBS" -I{} bash -c 'f={}; echo "building $(basename $f)"; $CXX -c "$f" -o "$OUT/core_$(basename "$f" .cpp).o"'
CORE_OBJS=""; for f in "$HERE"/src/core/*.cpp; do CORE_OBJS="$CORE_OBJS $OUT/core_$(basename "$f" .cpp).o"; done      # (named, not globbed: no stale object is linked)

$CXX -c "$HERE/tests/compat_test.cpp" -o "$OUT/compat_test.o"
g++ "$OUT/compat_test.o" $CORE_OBJS "$OUT"/juce_*.o "$OUT/ct.o" -o "$OUT/compat_test" -lpthread -ldl -lrt
"$OUT/compat_test" "$HERE"
