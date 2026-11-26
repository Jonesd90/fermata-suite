#!/bin/bash
# Developer helper: compiles (syntax only) the Windows part of src/core/CoreReservation.cpp, src/core/CrashReport.cpp and src/live/SecondSeat.cpp on Linux, against tools/win-stubs/windows.h, checks
# that src/core/FileSync.h compiles on its own (3.1.3: it no longer has a Windows branch - its disk sync goes through JUCE's stream), and scans
# every source for identifiers that windows.h turns into macros (small, near, far, hyper, pascal: MSVC then fails with "'int' followed by 'char'
# is illegal" or "type 'char' unexpected" in any file that includes a Windows header).
# Usage: tools/check-windows-code.sh /path/to/JUCE
JUCE=${1:?path to JUCE}
HERE="$(cd "$(dirname "$0")/.." && pwd)"
FLAGS="-std=c++17 -fsyntax-only -Wall -Wextra -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_MODULE_AVAILABLE_juce_core=1 -I$HERE/tools/win-stubs -isystem $JUCE/modules"
g++ $FLAGS -DFERMATA_CPU_WINDOWS_STUBS=1 "$HERE/src/core/CoreReservation.cpp" || exit 1
g++ $FLAGS -DFERMATA_CPU_WINDOWS_STUBS=1 "$HERE/src/core/CrashReport.cpp" || exit 1
# fermata:Live 0.6.0: the second seat's Windows part (the Interception library loaded at run time, the keys' names, the click-through pointer)
g++ $FLAGS -DFERMATA_SEAT_WINDOWS_STUBS=1 -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1 -DJUCE_MODULE_AVAILABLE_juce_graphics=1 -DJUCE_MODULE_AVAILABLE_juce_events=1 \
    -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 -Wno-unused-parameter "$HERE/src/live/SecondSeat.cpp" || exit 1
# fermata 3.68.0: the video reader's Windows file (Media Foundation), against stand-ins for the Windows headers it uses (tools/win-stubs/mf): its own
# grammar, with and without the graphics card's decoder. And that it never calls a Media Foundation function by name - those are found in
# Windows' DLLs when the first video is opened, so that fermata still starts on a Windows without them (see the top of that file).
for d in 1 0; do
    g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wno-unknown-pragmas -Wno-cast-function-type -DFERMATA_VIDEO_D3D=$d -I"$HERE/tools/win-stubs/mf" "$HERE/src/ui/VideoReaderWin.cpp" || exit 1
done
if grep -nE '(^|[^A-Za-z0-9_:&])(MF(Startup|Shutdown|Create[A-Za-z0-9]+|GetStrideForBitmapInfoHeader)|D3D11CreateDevice) *\(' "$HERE/src/ui/VideoReaderWin.cpp"; then
    echo "src/ui/VideoReaderWin.cpp calls a Media Foundation function by name (use the pointers in Mf: fermata must start without mfplat.dll)"; exit 1
fi
echo '#include "src/core/FileSync.h"
int main() { return td::filesync::syncToDisk (juce::File()) ? 0 : 1; }' | g++ $FLAGS -I"$HERE" -x c++ - || exit 1
python3 - "$HERE" <<'PY' || exit 1
import re, sys, glob, os
root = sys.argv[1]
names = r'\b(small|near|far|hyper|pascal)\b'
bad = []
for f in glob.glob(root + '/src/**/*.cpp', recursive=True) + glob.glob(root + '/src/**/*.h', recursive=True):
    if 'third_party' in f: continue
    code = open(f, encoding='utf-8', errors='replace').read()
    code = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), code, flags=re.S)
    code = re.sub(r'//[^\n]*', '', code)
    code = re.sub(r'"(\\.|[^"\\])*"', '""', code)
    for i, line in enumerate(code.split('\n'), 1):
        if line.lstrip().startswith('#'): continue
        if re.search(names, line): bad.append(f'{os.path.relpath(f, root)}:{i}: {line.strip()[:100]}')
if bad:
    print('identifiers that are Windows macro names (rename them):'); print('\n'.join(bad)); sys.exit(1)
PY
# Live 0.10.13 (25 November 2026): 'this' in the capture of a lambda that is itself inside a lambda ("[safe = SafePointer<X> (this)]" in a lambda's body). Microsoft's
# compiler (14.52, Visual Studio 18) takes that 'this' for the outer lambda's own object and stops ("cannot convert from 'const <lambda> *'");
# GCC accepts it, so nothing here noticed: the Windows build of fermata:Live 0.10.11 / 0.10.12 failed on two such lines. Make the value in the
# outer lambda's body and capture it by name instead.
python3 - "$HERE" <<'PY' || exit 1
import re, sys, glob, os
root = sys.argv[1]
lam = re.compile(r'\]\s*(\((?:[^()]|\([^()]*\))*\))?\s*(?:mutable\s*|noexcept\s*|constexpr\s*)*(?:->\s*[\w:<>*&,\s]+?)?\s*$')
cap = re.compile(r'\[([^\[\]]*=[^\[\]]*\bthis\b[^\[\]]*)\]\s*(?:\(|\{|mutable)')
bad = []
for f in glob.glob(root + '/src/**/*.cpp', recursive=True) + glob.glob(root + '/src/**/*.h', recursive=True):
    if 'third_party' in f: continue
    code = open(f, encoding='utf-8', errors='replace').read()
    code = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), code, flags=re.S)
    code = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), code)
    code = re.sub(r'"(\\.|[^"\\\n])*"', lambda m: '"' + ' ' * (len(m.group(0)) - 2) + '"', code)
    code = re.sub(r"'(\\.|[^'\\\n])'", lambda m: "' '" if len(m.group(0)) == 3 else "'" + ' ' * (len(m.group(0)) - 2) + "'", code)
    hits = [m.start() for m in cap.finditer(code)]
    if not hits: continue
    stack = []; opened = {}                    # for each position: is it inside a lambda's body?
    inside = [False] * (len(code) + 1); depth_l = 0
    for i, ch in enumerate(code):
        inside[i] = depth_l > 0
        if ch == '{':
            is_l = lam.search(code[max(0, i - 400):i]) is not None
            stack.append(is_l); depth_l += 1 if is_l else 0
        elif ch == '}' and stack:
            if stack.pop(): depth_l -= 1
    for h in hits:
        if inside[h]:
            line = code.count('\n', 0, h) + 1
            bad.append(f'{os.path.relpath(f, root)}:{line}')
if bad:
    print("'this' in the capture of a lambda inside a lambda (Microsoft's compiler stops there - make the value in the outer lambda's body):"); print('\n'.join(bad)); sys.exit(1)
PY
echo "Windows code: OK"
