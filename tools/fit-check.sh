#!/bin/bash
# The small-screen check (fermata 3.71.0, asked for on 26 November 2026: the Listening copy page's "Your name" box was a sliver on a smaller screen).
# Builds fermata for Linux with the check's driver in it (tools/fit-check/FitCheckDriver.h; FERMATA_FIT_CHECK=1 - never in the program the user
# runs), then runs it under Xvfb on screens of the sizes below. On each, the driver opens the program's windows and dialogs one after another (on
# the project given, or a blank one), writes what does not fit in each (src/ui/FitLayout.h's audit) and a picture of it, and quits.
# Run it whenever windows or their layouts change. Not one of the checks every delivery runs: it needs Xvfb, and takes a while.
#
# Usage: tools/fit-check.sh /path/to/JUCE [project.fermata] [out folder]       (default out folder: /tmp/fermata-fit-check)
#   FIT_PROGRAM=live: fermata:Live instead (tools/fit-check/LiveFitCheck.h: its main window and each window its menu bar opens, on the project given)
#   FIT_STYLE=1: in the Manuscript UI (its serif type is wider: the harder case) - 0, the Fermata style UI, is the default
#   FIT_SIZES (optional): the screens, as the part of the screen a window may use (the taskbar left out). Default:
#     1536x816  - 1920x1080 at 125 % (the user's smaller screen), less the taskbar
#     1366x728  - 1366x768 at 100 %, less the taskbar
#     1280x680  - 1280x720 at 100 %, less the taskbar
#     1920x1040 - 1920x1080 at 100 %: a roomy screen, where no window should need its scroll bar
# Results: <out>/<size>/report.txt (one block per window: its size, whether it scrolls, the problems) and <out>/<size>/*.png, and a summary here.
set -e
JUCE=${1:?path to JUCE}
PROJECT=${2:-}
OUT=${3:-/tmp/fermata-fit-check}
SIZES=${FIT_SIZES:-"1536x816 1366x728 1280x680 1920x1040"}
PROGRAM=${FIT_PROGRAM:-fermata}
STYLE=${FIT_STYLE:-0}
HERE="$(cd "$(dirname "$0")/.." && pwd)"
B="$OUT/build-$PROGRAM"; J="$OUT/build-juce"       # (JUCE's objects are the same for both programs)
mkdir -p "$B" "$J"
JOBS=$(nproc 2>/dev/null || echo 2)
command -v Xvfb > /dev/null || { echo "Xvfb is needed (apt install xvfb)"; exit 2; }
VERSION=$(grep -m1 -oP 'set\(FERMATA_VERSION \K[0-9.]+' "$HERE/CMakeLists.txt" || echo 0.0.0)
LIVE_VERSION=$(grep -m1 -oP 'set\(FERMATA_LIVE_VERSION \K[0-9.]+' "$HERE/src/live/Live.cmake" || echo 0.0.0)

MODS="juce_core juce_events juce_graphics juce_data_structures juce_gui_basics juce_gui_extra juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_processors juce_audio_utils"
DEFS="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_PLUGINHOST_VST3=1 -DJUCE_VST3_CAN_REPLACE_VST2=0 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0"
DEFS="$DEFS -DJUCE_ALSA=0 -DJUCE_JACK=0 -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XSHM=0 -DJUCE_DISPLAY_SPLASH_SCREEN=0 -DNDEBUG=1"
for m in $MODS; do DEFS="$DEFS -DJUCE_MODULE_AVAILABLE_$m=1"; done
export CXX="g++ -std=c++17 -O1 -I$JUCE/modules -I$JUCE/modules/juce_audio_processors/format_types/VST3_SDK $(pkg-config --cflags freetype2 2>/dev/null) $DEFS" B J

# ---- JUCE (once)
for m in $MODS juce_audio_processors_ara juce_audio_processors_lv2_libs juce_graphics_Harfbuzz; do
    base=$m; case $m in juce_audio_processors_*) base=juce_audio_processors ;; juce_graphics_*) base=juce_graphics ;; esac
    [ -f "$J/$m.o" ] || { echo "#include <$base/$m.cpp>" > "$J/$m.cpp"; echo "$m"; }
done | xargs -r -P"$JOBS" -I{} bash -c 'echo "building JUCE {}"; $CXX -w -c "$J/{}.cpp" -o "$J/{}.o"'
[ -f "$J/juce_graphics_Sheenbidi.o" ] || gcc -O1 -I"$JUCE/modules" -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -w -c "$JUCE/modules/juce_graphics/juce_graphics_Sheenbidi.c" -o "$J/juce_graphics_Sheenbidi.o"
[ -f "$J/ct.o" ] || { echo "#include <juce_core/juce_core_CompilationTime.cpp>" > "$J/ct.cpp"; $CXX -w -c "$J/ct.cpp" -o "$J/ct.o"; }

# ---- the program: its sources as its CMake file lists them (the shared core's from cmake/SharedCore.cmake); fermata with the readers that build
#      everywhere. MAIN is the file built with the check's driver in it.
SHARED=$(awk '/^set\(FERMATA_SHARED_SOURCES/,/\)/' "$HERE/cmake/SharedCore.cmake" | grep -oE 'src/[A-Za-z0-9_/]+\.cpp')
if [ "$PROGRAM" = live ]; then
    SRCS="$(awk '/^target_sources\(FermataLive/,/\.cpp\)/' "$HERE/src/live/Live.cmake" | grep -oE '(CMAKE_CURRENT_LIST_DIR\}|FERMATA_ROOT\})/[A-Za-z0-9_/]+\.cpp' | sed -e 's#CMAKE_CURRENT_LIST_DIR}#src/live#' -e 's#FERMATA_ROOT}/##')"
    SRCS="$SRCS $SHARED"
    MAIN=src/live/Main.cpp
    printf '#define FERMATA_LIVE 1\n#define FERMATA_LIVE_VERSION_STRING "%s"\n#define FERMATA_VERSION_STRING "Live %s"\n' "$LIVE_VERSION" "$LIVE_VERSION" > "$B/program-defs.h"
else
    SRCS=$(awk '/^target_sources\(Fermata /,/\.cpp\)/' "$HERE/CMakeLists.txt" | grep -oE 'src/[A-Za-z0-9_/]+\.cpp')
    SRCS="$SRCS $SHARED src/ui/VideoReaderNone.cpp src/ui/PdfPagesNone.cpp"
    MAIN=src/Main.cpp
    printf '#define FERMATA_VERSION_STRING "%s"\n' "$VERSION" > "$B/program-defs.h"
fi
PDEFS="-include $B/program-defs.h"                  # (the program's own definitions, as its CMake file gives them)
NEWEST=$(ls -t "$HERE"/src/*/*.h "$HERE"/src/ui/*.h "$HERE"/tools/fit-check/*.h | head -1)
export HERE NEWEST PDEFS MAIN
for s in $SRCS; do
    o="$B/$(echo "$s" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    [ -f "$o" ] && [ "$o" -nt "$HERE/$s" ] && [ "$o" -nt "$NEWEST" ] || echo "$s"
done | xargs -r -P"$JOBS" -I{} bash -c 's={}; o="$B/$(echo "$s" | tr "/" "_" | sed "s/\.cpp$/.o/")"; echo "building $s";
    extra=""; [ "$s" = "$MAIN" ] && extra="-DFERMATA_FIT_CHECK=1";
    $CXX $PDEFS $extra -c "$HERE/$s" -o "$o" 2> "$o.log" || { echo "FAILED $s"; head -30 "$o.log"; exit 1; }'
OBJS=""; for s in $SRCS; do OBJS="$OBJS $B/$(echo "$s" | tr '/' '_' | sed 's/\.cpp$/.o/')"; done
g++ $OBJS "$J"/juce_*.o "$J/ct.o" -o "$B/fit-check" -lfreetype -lfontconfig -lpthread -ldl -lrt
echo "built $B/fit-check ($PROGRAM)"

# ---- run it on each screen
summary=""
for size in $SIZES; do
    w=${size%x*}; h=${size#*x}
    dir="$OUT/$PROGRAM-$size-style$STYLE"; rm -rf "$dir"; mkdir -p "$dir/home/Fermata"
    proj=""
    if [ -n "$PROJECT" ]; then            # (a copy: the driver opens and closes windows, which the project remembers)
        cp -r "$(dirname "$PROJECT")" "$dir/project"; proj="$dir/project/$(basename "$PROJECT")"
    fi
    # the settings: the UI style, and (fermata:Live opens the project it had last) the project
    settings="$dir/home/Fermata/$([ "$PROGRAM" = live ] && echo fermata-Live || echo Fermata).settings"
    { echo '<?xml version="1.0" encoding="UTF-8"?>'; echo '<PROPERTIES>'; echo "  <VALUE name=\"uiStyle\" val=\"$STYLE\"/>"
      [ "$PROGRAM" = live ] && [ -n "$proj" ] && echo "  <VALUE name=\"lastProject\" val=\"$proj\"/>"; echo '</PROPERTIES>'; } > "$settings"
    [ "$PROGRAM" = live ] && proj=""
    disp=:$((90 + RANDOM % 9))
    Xvfb $disp -screen 0 ${w}x${h}x24 -nolisten tcp > "$dir/xvfb.log" 2>&1 &
    xpid=$!; sleep 1.5
    echo "running on $size ..."
    DISPLAY=$disp HOME="$dir/home" FIT_CHECK_OUT="$dir" timeout 900 "$B/fit-check" ${proj:+"$proj"} > "$dir/run.log" 2>&1 || true
    kill $xpid 2> /dev/null || true; wait $xpid 2> /dev/null || true
    last=$(tail -1 "$dir/report.txt" 2> /dev/null || echo "no report")
    summary="$summary\n$size: $last  (scrolling: $(grep -c ' SCROLLS' "$dir/report.txt" 2> /dev/null || true) windows)"
done
echo -e "SMALL-SCREEN CHECK:$summary"
echo "(reports and pictures: $OUT/$PROGRAM-<size>-style$STYLE/)"
