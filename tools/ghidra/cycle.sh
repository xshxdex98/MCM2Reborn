#!/bin/sh
# Export game.asm from Ghidra, hand the ported functions to C, assemble, compile code/mcm2, link,
# and check the result against retail - as ONE command.
#
# These run together on purpose. Run separately, a stale executable has twice been measured as if
# it were fresh, and that failure is silent: every check passes, because the stale build is
# internally consistent. It is simply not the build the change was meant to produce.
#
# The scripts in tools/ghidra are the master copies. Ghidra runs them from $MCM2_WORK/scripts,
# because analyzeHeadless.bat splits its arguments on spaces and this tree's path has one.
#
# On Linux it runs the same: Ghidra's own analyzeHeadless, and MSVC under Wine (tools/toolchain.py,
# tools/docker). See env.sh for what can be set.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
. "$HERE/env.sh"

mkdir -p "$WORK/scripts" "$WORK/out"
cp "$HERE"/*.java "$WORK/scripts/"
# The verified instruction forms (tools/verify_encodings.py --table data/encodings.tsv), when there
# are some: the export emits those as mnemonics, the rest as bytes. Either way the bytes are retail's.
[ -f "$REPO/data/encodings.tsv" ] && cp "$REPO/data/encodings.tsv" "$WORK/out/"

echo "=== 1/6 export ==="
"$GH" "$WORK/proj" MCM2 -process Mcm2.exe -noanalysis \
    -scriptPath "$WORK/scripts" -postScript ExportAsm.java "$WORK/out" 2>&1 \
    | grep -iE "ExportAsm.java>|ERROR|Exception|error:" | sed 's/^INFO *//' \
    | grep -viE "^ExportAsm.java>   (code|data): " || true

echo "=== 2/6 ports ==="
cd "$REPO"
$PYTHON tools/port.py

echo "=== 3/6 assemble ==="
$PYTHON tools/assemble.py

echo "=== 4/6 compile ==="
$PYTHON tools/compile.py
[ -f build/game.res ] || $PYTHON tools/resources.py

echo "=== 5/6 link ==="
$PYTHON tools/link.py

echo "=== 6/6 verify against retail ==="
$PYTHON tools/verify.py
echo CYCLE_DONE
