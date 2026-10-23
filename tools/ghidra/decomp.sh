#!/bin/sh
# Decompile a stretch of the game's code, for porting it:
#   sh tools/ghidra/decomp.sh 0x00486090-0x004892F0 GUIManager
# decompiles every function starting in that range to C:/mcm2ghidra/out/modules/GUIManager.c.
#
# A range rather than a source file: MSVC keeps each file's functions together, but the file
# attribution in out/modules.txt only knows the functions that name their file (in an assert or
# a tracked new), and it also lists inlined library code under the file that inlined it.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/env.sh"
case "$1" in
    0x*-0x*) ;;
    *) echo "usage: decomp.sh 0xSTART-0xEND <name>"; exit 1 ;;
esac
[ -n "$2" ] || { echo "usage: decomp.sh 0xSTART-0xEND <name>"; exit 1; }

FIRST=$(echo "$1" | sed 's/^0x\([0-9A-Fa-f]*\)-.*/\1/' | tr a-f A-F)
LAST=$(echo "$1" | sed 's/.*-0x\([0-9A-Fa-f]*\)$/\1/' | tr a-f A-F)
ADDRESSES=$(sed -n 's/^; @\([0-9A-F]\{8\}\)$/\1/p' "$WORK/out/game.asm.pristine" \
    | awk -v a="$FIRST" -v b="$LAST" '$1 "" >= a "" && $1 "" <= b "" { print "0x" $1 }')
# (The "" forces a string comparison: awk would read 00402E30 as a number, 402 * 10^30.)
[ -n "$ADDRESSES" ] || { echo "no functions start in $1"; exit 1; }

cp "$HERE"/*.java "$WORK/scripts/"
mkdir -p "$WORK/out/modules"
# shellcheck disable=SC2086
"$GH" "$WORK/proj" MCM2 -process Mcm2.exe -noanalysis -readOnly \
    -scriptPath "$WORK/scripts" -postScript Decomp.java "$WORK/out/modules" $ADDRESSES 2>&1 \
    | grep -iE "Decomp.java>|ERROR|Exception" | sed 's/^INFO *//' || true
mv "$WORK/out/modules/decomp.c" "$WORK/out/modules/$2.c"
echo "-> $WORK/out/modules/$2.c"
