#!/bin/sh
# Rebuild the MCM2 Ghidra database from scratch, to the state everything else assumes.
#
# WHY THIS SCRIPT HAS TO EXIST, AND HAS TO STAY CORRECT
#
# Ghidra's database is state, and every pass that clears or creates a listing writes back into it.
# On the MM2 port an experimental rule cleared roughly 32,000 listings; the clears persisted after
# the rule was reverted, and much later the exporter read the middle of a live instruction as a
# pointer and relocated it. No rule change could undo that - the evidence it needed had been deleted
# from the database. So: measure any rule change against a freshly imported database, and keep this
# script able to produce one.
#
# WHY THE RECOVERY PASS IS PART OF THE IMPORT, NOT AN EXTRA
#
# Ghidra's own analysis leaves 8.41% of .text belonging to no function, in runs up to 5,984 bytes.
# That is not a defect in Ghidra: Mcm2.exe was built by VC6 at /O2, which OMITS THE FRAME POINTER,
# so its functions open with `sub esp, N`, `mov eax, fs:[0]` or a bare `mov eax, [esp+4]`, and the
# MSVC function-start patterns are built around `push ebp; mov ebp, esp`. There is not one of those
# in the whole uncovered set. The functions are reached only through vtables, so no direct call
# leads to them either.
#
# FindFuncs.java recovers them on proof - every dword in the image that points into .text and that
# no function covers - and takes coverage from 91.59% to 98.54%, 4,170 functions to 5,658. A
# database without that pass is a different database, so the pass belongs here.
#
# The scripts in tools/ghidra are the master copies; they are copied to $MCM2_WORK/scripts first,
# because analyzeHeadless.bat splits its arguments on spaces and this tree's path has one.
#
# The retail executable goes in $MCM2_WORK/bin/Mcm2.exe first (its sha256 is in docs/binary.md).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
PROJ=$WORK/proj
SCRIPTS=$WORK/scripts
OUT=$WORK/out
[ -f "$WORK/bin/Mcm2.exe" ] || { echo "no $WORK/bin/Mcm2.exe - put the retail executable there"; exit 1; }

mkdir -p "$SCRIPTS" "$OUT"
cp "$HERE"/*.java "$SCRIPTS/"

# The names of the functions Mcm2.exe imports by ordinal alone (WSOCK32's, DSOUND's, DPLAYX's).
# On Windows Ghidra reads them out of the system's own DLLs; elsewhere there are none, every one of
# them would be Ordinal_<n>, and two DLLs' Ordinal_4 would collide in game.asm. symbols/win32 has
# them (from Wine's .spec files, which keep Windows' ordinals), put where Ghidra looks for its own.
if [ -z "$WINDOWS" ]; then
    SYMBOLS="$(dirname "$(dirname "$GH")")/Ghidra/Features/Base/data/symbols/win32"
    for f in "$HERE"/symbols/win32/*.exports; do
        [ -f "$SYMBOLS/$(basename "$f")" ] || cp "$f" "$SYMBOLS/" \
            || { echo "could not copy $f into $SYMBOLS"; exit 1; }
    done
fi

if [ -d "$WORK/proj.old" ]; then rm -rf "$WORK/proj.old"; fi
if [ -d "$WORK/proj" ]; then mv "$WORK/proj" "$WORK/proj.old"; fi
mkdir -p "$WORK/proj"

echo "=== 1/5 importing and analysing Mcm2.exe ==="
"$GH" "$PROJ" MCM2 -import "$WORK/bin/Mcm2.exe" 2>&1 \
    | grep -iE "ERROR|Exception|Analysis succeeded|Import succeeded" | tail -10

echo "=== 2/5 census before recovery ==="
"$GH" "$PROJ" MCM2 -process Mcm2.exe -noanalysis -scriptPath "$SCRIPTS" \
    -postScript Census.java "$OUT" >/dev/null 2>&1
cp "$OUT/census.txt" "$OUT/census_before.txt"
grep -E "^functions|covered by funcs|NOT covered" "$OUT/census_before.txt"

echo "=== 3/5 recovering the frame-pointer-omitted functions ==="
"$GH" "$PROJ" MCM2 -process Mcm2.exe -noanalysis -scriptPath "$SCRIPTS" \
    -postScript FindFuncs.java "$OUT" apply >/dev/null 2>&1
grep -E "^tier |^absorbed|^skipped|^refused" "$OUT/findfuncs_applied.txt"

echo "=== 4/5 census after recovery ==="
"$GH" "$PROJ" MCM2 -process Mcm2.exe -noanalysis -scriptPath "$SCRIPTS" \
    -postScript Census.java "$OUT" >/dev/null 2>&1
grep -E "^functions|covered by funcs|NOT covered" "$OUT/census.txt"

# The classes' virtual functions named from the RTTI (vf_<slot> in each class): ~10 minutes, and no
# listing changes, but the C binds to those names (game_vf_10_46A300), so a database without them
# exports an assembly the build cannot link against.
echo "=== 5/5 naming the virtual functions from the RTTI ==="
"$GH" "$PROJ" MCM2 -process Mcm2.exe -noanalysis -scriptPath "$SCRIPTS" \
    -postScript RttiMcm2.java "$OUT" 2>&1 | grep -E "RttiMcm2.java> (WROTE|STATS)|ERROR|Exception" || true

echo "=== import done ==="
