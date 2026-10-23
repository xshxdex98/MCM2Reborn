"""
Check the C's declarations of game functions against the game: the bytes each pops.

    py -3.12 tools/check_calls.py

A __fastcall (that is, __thiscall) or __stdcall declaration with the wrong number of stack
arguments unbalances the stack of every C function that calls it. Each declaration's stack
arguments are counted (__fastcall passes its first two in registers) and compared with the
`ret` of the game function it binds to. __cdecl functions are popped by the caller and skipped.
"""

import glob
import re
import struct
import sys

import capstone

from paths import ASM as EXPORTED, RETAIL

ROOT = __file__.rsplit("tools", 1)[0]
ASM = EXPORTED + ".pristine"
DECLARATION = re.compile(
    r"\b(__fastcall|__stdcall)\s*\**\s*game_(\w+)\s*\(([^;{]*?)\)\s*;", re.S)


def stack_bytes(convention, parameters):
    params = [p.strip() for p in parameters.split(",") if p.strip() and p.strip() != "void"]
    sizes = [8 if re.match(r"^(double|__int64|LONGLONG)\b", p)
             else 16 if re.match(r"^(const\s+)?GUID\s+\w+$", p)     # a GUID by value
             else 12 if re.match(r"^(const\s+)?vector3\s+\w+$", p)  # a vector by value
             else 4 for p in params]
    if convention == "__fastcall":
        sizes = sizes[2:]
    return sum(sizes)


def main():
    labels, starts, pending = {}, [], None
    for line in open(ASM, encoding="latin-1"):
        m = re.match(r"^; @([0-9A-F]{8})$", line)
        if m:
            pending = int(m.group(1), 16)
            starts.append(pending)
            continue
        m = re.match(r"^(\S+) PROC$", line)
        if m and pending is not None:
            labels[m.group(1)] = pending
            pending = None
    starts.sort()

    image = open(RETAIL, "rb").read()
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    count, = struct.unpack_from("<H", image, pe + 6)
    optional, = struct.unpack_from("<H", image, pe + 20)
    layout = [struct.unpack_from("<8sIIII", image, pe + 24 + optional + 40 * i)[1:]
              for i in range(count)]

    def pops(address):
        following = [s for s in starts if s > address]
        end = following[0] if following else address + 0x1000
        for vsize, va, _, raw in layout:
            if 0x400000 + va <= address < 0x400000 + va + vsize:
                data = image[raw + address - 0x400000 - va:raw + end - 0x400000 - va]
                break
        found = set()
        for insn in capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32).disasm(data, address):
            if insn.mnemonic == "ret":
                found.add(int(insn.op_str, 16) if insn.op_str else 0)
        return found

    bad = checked = 0
    for path in glob.glob(ROOT + r"code\mcm2\*.[ch]"):
        text = open(path, encoding="utf-8").read()
        for m in DECLARATION.finditer(text):
            convention, name, parameters = m.groups()
            if name not in labels:
                continue            # bound to a port, or not a function start
            expected = stack_bytes(convention, parameters)
            actual = pops(labels[name])
            checked += 1
            if actual and expected not in actual:
                line = text.count("\n", 0, m.start()) + 1
                print("%s:%d game_%s takes %d stack bytes here; the game pops %s"
                      % (path.rsplit("\\", 1)[-1], line, name, expected, sorted(actual)))
                bad += 1
    print("%d declaration(s) checked, %d wrong" % (checked, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
