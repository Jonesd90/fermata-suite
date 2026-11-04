#!/bin/bash
# fermata:Live's recorder core on Linux (0.2.0; 0.3.0: monitoring, playback streams, waveform peaks, flags): builds src/live's core (no window, no device) with the shared core against JUCE and runs
# tests/live_recorder_test.cpp - fake audio, drives as folders, a disk the tests break. Usage: tools/live-check.sh /path/to/JUCE [out folder]
# The objects of the shared core are reused from tools/compat-check.sh's out folder when it is the same (default /tmp/fermata-compat-build).
set -e
JUCE=${1:?path to JUCE}
OUT=${2:-/tmp/fermata-compat-build}
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$OUT"
JOBS=$(nproc 2>/dev/null || echo 2)
DEFS="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_ALSA=0 -DJUCE_JACK=0 -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XSHM=0 -DJUCE_MODAL_LOOPS_PERMITTED=0 -DNDEBUG=1"
export CXX="${LIVE_CXX:-g++ -std=c++17 -O1} -I$JUCE/modules $DEFS -w" OUT
for m in juce_core juce_events juce_audio_basics juce_audio_formats juce_audio_devices juce_data_structures; do
    [ -f "$OUT/$m.o" ] || { echo "#include <$m/$m.cpp>" > "$OUT/$m.cpp"; echo "$m"; }
done | xargs -r -P"$JOBS" -I{} bash -c 'echo "building JUCE {}"; $CXX -c "$OUT/{}.cpp" -o "$OUT/{}.o"'
[ -f "$OUT/ct.o" ] || { echo "#include <juce_core/juce_core_CompilationTime.cpp>" > "$OUT/ct.cpp"; $CXX -c "$OUT/ct.cpp" -o "$OUT/ct.o"; }
NEWEST=$(ls -t "$HERE"/src/core/* "$HERE"/src/reharmoniser/third_party/* $(find "$HERE/third_party" -type f) | head -1)
for f in "$HERE"/src/core/*.cpp; do
    o="$OUT/core_$(basename "$f" .cpp).o"
    [ -f "$o" ] && [ "$o" -nt "$NEWEST" ] || echo "$f"
done | xargs -r -P"$JOBS" -I{} bash -c 'f={}; echo "building $(basename $f)"; $CXX -c "$f" -o "$OUT/core_$(basename "$f" .cpp).o"'
CORE_OBJS=""; for f in "$HERE"/src/core/*.cpp; do CORE_OBJS="$CORE_OBJS $OUT/core_$(basename "$f" .cpp).o"; done
# the recorder core: every src/live .cpp that needs no window or device
LIVE_OBJS=""
for f in DriveWriter LiveRecorder Merge StreamServer MonitorMix; do
    $CXX -c "$HERE/src/live/$f.cpp" -o "$OUT/live_$f.o"
    LIVE_OBJS="$LIVE_OBJS $OUT/live_$f.o"
done
$CXX -c "$HERE/tests/live_recorder_test.cpp" -o "$OUT/live_recorder_test.o"
g++ "$OUT/live_recorder_test.o" $LIVE_OBJS $CORE_OBJS "$OUT"/juce_*.o "$OUT/ct.o" -o "$OUT/live_recorder_test" ${LIVE_LINK:-} -lpthread -ldl -lrt
"$OUT/live_recorder_test"
