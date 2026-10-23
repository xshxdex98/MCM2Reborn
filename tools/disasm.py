"""
Disassemble the game's functions from the retail executable, the reference for porting them:

    py -3.12 tools/disasm.py 0x00486120 [0x00486290 ...]
    py -3.12 tools/disasm.py 0x00486090-0x004892F0

Each function runs to the next one's start, as the export has them, and every call, jump or
address that lands on a known function or label is named. The decompiler is a reading aid only:
it often loses the `this` register of a method call, which the instructions show.
"""

import bisect
import re
import struct
import sys

import capstone

from paths import ASM as EXPORTED, RETAIL

ASM = EXPORTED + ".pristine"


def load_names():
    """Function starts and their names, from the export's `; @address` comments, and the imports."""
    names, starts, pending, data = {}, [], None, None
    with open(ASM, encoding="latin-1") as f:
        for line in f:
            m = re.match(r"^D_([0-9A-F]{6}) LABEL", line)
            if m:
                data = int(m.group(1), 16)
                continue
            m = re.match(r"^(__imp__\w+) LABEL DWORD", line)
            if m and data is not None:
                names[data] = m.group(1)
                data = None
                continue
            m = re.match(r"^; @([0-9A-F]{8})$", line)
            if m:
                pending = int(m.group(1), 16)
                starts.append(pending)
                continue
            m = re.match(r"^(\S+) PROC$", line)
            if m and pending is not None:
                names[pending] = m.group(1)
                pending = None
    return names, sorted(starts)


def load_image():
    image = open(RETAIL, "rb").read()
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    count, = struct.unpack_from("<H", image, pe + 6)
    optional, = struct.unpack_from("<H", image, pe + 20)
    base, = struct.unpack_from("<I", image, pe + 24 + 28)
    sections = []
    for i in range(count):
        _, vsize, va, _, raw = struct.unpack_from("<8sIIII", image, pe + 24 + optional + 40 * i)
        sections.append((base + va, vsize, raw))
    return image, sections


def read(image, sections, address, size):
    for start, vsize, raw in sections:
        if start <= address < start + vsize:
            return image[raw + address - start:raw + address - start + min(size, start + vsize - address)]
    sys.exit("0x%08X is not in the image" % address)


def string_at(image, sections, address):
    """The text at a data address, if it holds a printable C string of 2 or more characters."""
    for start, vsize, raw in sections[1:]:
        if start <= address < start + vsize:
            text = image[raw + address - start:raw + address - start + 64].split(b"\0")[0]
            if len(text) >= 2 and all(32 <= c < 127 for c in text):
                return repr(text.decode())
    return None


def main():
    names, starts = load_names()
    image, sections = load_image()

    wanted = []
    for arg in sys.argv[1:]:
        if "-" in arg:
            first, last = (int(x, 16) for x in arg.split("-"))
            wanted += [s for s in starts if first <= s <= last]
        else:
            wanted.append(int(arg, 16))
    if not wanted:
        sys.exit(__doc__)

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for address in wanted:
        i = bisect.bisect_right(starts, address)
        end = starts[i] if i < len(starts) else address + 0x1000
        print("%s @ %08X (%d bytes)" % (names.get(address, "?"), address, end - address))
        for insn in md.disasm(read(image, sections, address, end - address), address):
            text = insn.op_str
            for hit in re.findall(r"0x[0-9a-f]{6,8}", text):
                name = names.get(int(hit, 16)) or string_at(image, sections, int(hit, 16))
                if name:
                    text = text.replace(hit, "%s <%s>" % (hit, name))
            print("  %08X  %-7s %s" % (insn.address, insn.mnemonic, text))
        print()


if __name__ == "__main__":
    main()
