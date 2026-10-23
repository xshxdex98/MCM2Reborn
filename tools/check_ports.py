"""
Check each port's calling convention against the original's: the bytes every `ret` pops.

    py -3.12 tools/check_ports.py

A port that pops a different number of argument bytes from the original unbalances its caller's
stack, which usually crashes much later and far away. tools/verify.py cannot see this: it checks
the game's bytes, not what the C does. The original is read from the retail executable up to the
next function; the port from the build, up to the next symbol in the map.
"""

import re
import struct
import sys

import os

import capstone

from paths import ASM as EXPORTED, RETAIL

ROOT = __file__.rsplit("tools", 1)[0]
BUILD = os.path.join(ROOT, "build", "OpenMCM2.exe")
MAP = os.path.join(ROOT, "build", "OpenMCM2.map")
PORTS = os.path.join(ROOT, "build", "ports.tsv")
ASM = EXPORTED + ".pristine"


def sections(image):
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    count, = struct.unpack_from("<H", image, pe + 6)
    optional, = struct.unpack_from("<H", image, pe + 20)
    base, = struct.unpack_from("<I", image, pe + 24 + 28)
    found = []
    for i in range(count):
        _, vsize, va, _, raw = struct.unpack_from("<8sIIII", image, pe + 24 + optional + 40 * i)
        found.append((base + va, vsize, raw))
    return found


def code(image, layout, start, end):
    for va, vsize, raw in layout:
        if va <= start < va + vsize:
            return image[raw + start - va:raw + min(end, va + vsize) - va]
    return b""


JUMP_TABLE = re.compile(r"\[\w+\*4 \+ (0x[0-9a-f]+)\]")


def pops(data, address):
    """The set of byte counts the function's `ret`s pop.

    Only the code reachable from the entry is read, following jumps and switch tables, so a
    jump table after the code, or a static function the map does not name, is not taken for
    part of it.
    """
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    end = address + len(data)
    found = set()
    seen = set()
    todo = [address]
    while todo:
        at = todo.pop()
        while address <= at < end and at not in seen:
            seen.add(at)
            insn = next(decoder.disasm(data[at - address:at - address + 16], at, 1), None)
            if insn is None:
                break
            name = insn.mnemonic
            if name == "ret":
                found.add(int(insn.op_str, 16) if insn.op_str else 0)
                break
            if name == "jmp" or name.startswith("j") or name.startswith("loop"):
                table = JUMP_TABLE.search(insn.op_str)
                if table:
                    entry = int(table.group(1), 16)
                    while address <= entry and entry + 4 <= end:
                        target, = struct.unpack_from("<I", data, entry - address)
                        if not address <= target < end:
                            break
                        todo.append(target)
                        entry += 4
                elif insn.op_str.startswith("0x"):
                    todo.append(int(insn.op_str, 16))
                if name == "jmp":
                    break
            at += insn.size
    return found


def main():
    starts = sorted(int(m.group(1), 16) for m in
                    (re.match(r"^; @([0-9A-F]{8})$", l) for l in open(ASM, encoding="latin-1")) if m)
    symbols = {}
    for line in open(MAP, encoding="latin-1"):
        parts = line.split()
        if len(parts) >= 3 and re.match(r"^[0-9a-f]{8}$", parts[2]):
            symbols.setdefault(parts[1], int(parts[2], 16))
    built_addresses = sorted(set(symbols.values()))

    retail, build = open(RETAIL, "rb").read(), open(BUILD, "rb").read()
    retail_layout, build_layout = sections(retail), sections(build)

    bad = 0
    rows = [line.split() for line in open(PORTS, encoding="utf-8") if line.strip()]
    for address_text, game_name, c_name in rows:
        address = int(address_text, 16)
        following = [s for s in starts if s > address]
        original = pops(code(retail, retail_layout, address, following[0] if following else address + 0x1000),
                        address)
        symbol = next((s for s in symbols if re.match(r"^[_@]%s(@\d+)?$" % re.escape(c_name), s)), None)
        if symbol is None:
            print("%08X %-28s no symbol for %s" % (address, game_name, c_name))
            bad += 1
            continue
        at = symbols[symbol]
        later = [s for s in built_addresses if s > at]
        ported = pops(code(build, build_layout, at, later[0] if later else at + 0x1000), at)
        if ported and original and ported != original:
            print("%08X %-28s pops %s, the port %s pops %s" % (address, game_name, sorted(original),
                                                               symbol, sorted(ported)))
            bad += 1
        elif not ported:
            print("%08X %-28s the port %s has no ret (tail calls only?) - check by hand"
                  % (address, game_name, symbol))
    print("%d port(s), %d with a different calling convention" % (len(rows), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
