#!/bin/bash
# Developer helper (fermata 3.68.0): builds and runs tests/video_source_test.cpp - src/ui/VideoSource.cpp (the decoder's thread, the frames kept
# ready, the small pictures, the sound written out: the part of the video that is the same on every system) over a made-up decoder. Run it
# whenever VideoSource.cpp or VideoReader.h changes. Not one of the checks every delivery runs: nothing else touches these files.
# Usage: tools/video-check.sh /path/to/JUCE [plain|tsan|asan] [out folder]      (default: plain, /tmp/fermata-video-check; JUCE is built there once per mode)
set -e
JUCE=${1:?path to JUCE}
MODE=${2:-plain}
OUT=${3:-/tmp/fermata-video-check}/$MODE
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$OUT"
JOBS=$(nproc 2>/dev/null || echo 2)
case "$MODE" in
    plain) SAN="-O1" ;;
    tsan)  SAN="-O1 -g -fsanitize=thread" ;;
    asan)  SAN="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" ;;
    *) echo "mode: plain, tsan or asan"; exit 2 ;;
esac
MODS="juce_core juce_events juce_graphics juce_audio_basics juce_audio_formats juce_audio_devices juce_data_structures"
DEFS="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_ALSA=0 -DJUCE_JACK=0 -DNDEBUG=1"
for m in $MODS; do DEFS="$DEFS -DJUCE_MODULE_AVAILABLE_$m=1"; done
export CXX="g++ -std=c++17 $SAN -I$JUCE/modules $(pkg-config --cflags freetype2 2>/dev/null) $DEFS -w" OUT
for m in $MODS; do
    [ -f "$OUT/$m.o" ] || { echo "#include <$m/$m.cpp>" > "$OUT/$m.cpp"; echo "$m"; }
done | xargs -r -P"$JOBS" -I{} bash -c 'echo "building JUCE {}"; $CXX -c "$OUT/{}.cpp" -o "$OUT/{}.o"'
# (JUCE's graphics module has two more files of its own: the text shaping it is built with)
[ -f "$OUT/juce_graphics_Harfbuzz.o" ] || { echo "#include <juce_graphics/juce_graphics_Harfbuzz.cpp>" > "$OUT/juce_graphics_Harfbuzz.cpp"; $CXX -c "$OUT/juce_graphics_Harfbuzz.cpp" -o "$OUT/juce_graphics_Harfbuzz.o"; }
[ -f "$OUT/juce_graphics_Sheenbidi.o" ] || gcc ${SAN} -I"$JUCE/modules" -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -w -c "$JUCE/modules/juce_graphics/juce_graphics_Sheenbidi.c" -o "$OUT/juce_graphics_Sheenbidi.o"
[ -f "$OUT/ct.o" ] || { echo "#include <juce_core/juce_core_CompilationTime.cpp>" > "$OUT/ct.cpp"; $CXX -c "$OUT/ct.cpp" -o "$OUT/ct.o"; }
# the shared core (VideoSource.cpp writes the sound out with it): every file, built again when anything in src/core is newer than its object
NEWEST=$(ls -t "$HERE"/src/core/* | head -1)
for f in "$HERE"/src/core/*.cpp; do
    o="$OUT/core_$(basename "$f" .cpp).o"
    [ -f "$o" ] && [ "$o" -nt "$NEWEST" ] || echo "$f"
done | xargs -r -P"$JOBS" -I{} bash -c 'f={}; echo "building $(basename $f)"; $CXX -c "$f" -o "$OUT/core_$(basename "$f" .cpp).o"'
CORE_OBJS=""; for f in "$HERE"/src/core/*.cpp; do CORE_OBJS="$CORE_OBJS $OUT/core_$(basename "$f" .cpp).o"; done
g++ -std=c++17 $SAN -I"$JUCE/modules" $(pkg-config --cflags freetype2 2>/dev/null) $DEFS -Wall -Wextra -c "$HERE/src/ui/VideoSource.cpp" -o "$OUT/VideoSource.o"
g++ -std=c++17 $SAN -I"$JUCE/modules" $(pkg-config --cflags freetype2 2>/dev/null) $DEFS -Wall -Wextra -c "$HERE/tests/video_source_test.cpp" -o "$OUT/video_source_test.o"
g++ $SAN "$OUT/VideoSource.o" "$OUT/video_source_test.o" $CORE_OBJS "$OUT"/juce_*.o "$OUT/ct.o" -o "$OUT/video_source_test" -lfreetype -lfontconfig -lpthread -ldl -lrt
TSAN_OPTIONS="halt_on_error=0 second_deadlock_stack=1" "$OUT/video_source_test"
