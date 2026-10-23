# -*- coding: utf-8 -*-
"""Hand the game's functions over to their C ports.

A C function marked `PORT(0x0044C1D0)` replaces the game's function at that address. This renames
the original in game.asm to orig_<name> - its bytes stay exactly where they are, so nothing in
.text moves - and declares the old name external, so every call to it and every vtable slot that
names it binds to the C instead (tools/link.py does the binding). The original stays callable from
C as game_orig_<name>, for comparison or as a fallback.

A port is refused if code elsewhere jumps to a label inside the function: those jumps would still
land in the original, half of it running under the port.

The exporter writes a relative call or jump as its opcode bytes and `dd target - ($ + 4)`, which
MASM cannot assemble once the target is external. Those become `call`, `jmp` or the conditional
jump instead, which MASM encodes at the same full length for an external target - so no byte
moves, and tools/verify.py checks that it did not.

The listing is indexed once - where each function starts, which lines name each symbol, which
lines branch to each target - so a port costs its own lines rather than the whole listing's.

It works from game.asm.pristine, the export as ExportAsm wrote it, so running it again is safe.
Writes build/ports.tsv - address, the game's name, the C name - for link.py and verify_text.py.
"""
import bisect
import glob
import os
import re
import sys

from paths import ASM

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "code", "mcm2")
PRISTINE = ASM + ".pristine"
PORTS = os.path.join(ROOT, "build", "ports.tsv")

# PORT(0x0044C1D0) then the definition: the name is the last word before the parameter list.
MARK = re.compile(r"\bPORT\(\s*0x([0-9A-Fa-f]+)\s*\)\s*\n[^;{]*?\b(\w+)\s*\(", re.MULTILINE)
LABEL = re.compile(r"^(\S+):$")
# A symbol as symbol() sees one: mangled names hold ?, @ and $.
WORD = re.compile(r"[\w?@$]+")
BRANCH = re.compile(r"^    dd (\S+) - \(\$ \+ 4\)$")


# Conditional jumps by their second opcode byte, 0F 80 .. 0F 8F.
CONDITIONS = ["jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja",
              "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg"]


def branches_as_instructions(lines, name, branch_lines):
    """Rewrite each `db <opcode>` + `dd name - ($ + 4)` pair as the instruction it encodes.

    The `dd` lines become None, and main() drops them once every port is done, so the indexed
    line numbers hold meanwhile.
    """
    for n in branch_lines.get(name, ()):
        opcode = lines[n - 1].strip()
        if opcode == "db 0E8h":
            mnemonic = "call"
        elif opcode == "db 0E9h":
            mnemonic = "jmp"
        else:
            m = re.match(r"^db 00?Fh, 0?8([0-9A-F])h$", opcode)
            if not m:
                sys.exit("cannot port %s: line %d refers to it after `%s`" % (name, n + 1, opcode))
            mnemonic = CONDITIONS[int(m.group(1), 16)]
        lines[n - 1] = "    %s %s" % (mnemonic, name)
        lines[n] = None


def symbol(name):
    """A pattern for `name` as a whole symbol. Mangled names hold ? and @, which \\b does not see."""
    return r"(?<![\w?@$])%s(?![\w?@$])" % re.escape(name)


def ports_in_source():
    found = {}
    for path in sorted(glob.glob(os.path.join(SRC, "*.c"))):
        with open(path, encoding="utf-8") as f:
            for m in MARK.finditer(f.read()):
                address = int(m.group(1), 16)
                if address in found:
                    sys.exit("0x%08X is ported twice: %s and %s" % (address, found[address], m.group(2)))
                found[address] = m.group(2)
    return found


def index(lines):
    """(each function's `; @address` line, every `; @` line, the lines naming each symbol, each
    branch target's lines)."""
    tags, marks, uses, branch_lines = {}, [], {}, {}
    for n, line in enumerate(lines):
        if line.startswith("; @"):
            marks.append(n)
            if len(line) == 11:
                tags[int(line[3:], 16)] = n
            continue
        for word in set(WORD.findall(line)):
            uses.setdefault(word, []).append(n)
        m = BRANCH.match(line)
        if m:
            branch_lines.setdefault(m.group(1), []).append(n)
    return tags, marks, uses, branch_lines


def find_function(lines, tags, address):
    """(name, first line, line after ENDP) of the function starting at `address`."""
    i = tags.get(address)
    if i is not None:
        j = i + 1
        if lines[j].startswith("PUBLIC "):
            j += 1
        name = lines[j].split()[0]
        if lines[j].split()[1:2] == ["PROC"]:
            for k in range(j + 1, len(lines)):
                if lines[k] is not None and lines[k].startswith(name + " ENDP"):
                    return name, i, k + 1
    sys.exit("no function starts at 0x%08X in %s" % (address, ASM))


def main():
    ports = ports_in_source()
    with open(PRISTINE, encoding="utf-8") as f:
        lines = f.read().split("\n")
    tags, starts, uses, branch_lines = index(lines)

    rows, externs = [], []
    for address, c_name in sorted(ports.items()):
        name, start, end = find_function(lines, tags, address)

        # The function owns everything up to the next one, such as a jump table after its ENDP.
        at = bisect.bisect_left(starts, end)
        extent = starts[at] if at < len(starts) else len(lines)
        for line in lines[start:end]:
            m = LABEL.match(line or "")
            if not m:
                continue
            for n in uses.get(m.group(1), ()):
                # A `push imm32` whose value merely equals an address inside (a colour such as
                # 0x404040, say) is data, not a way in.
                if n and lines[n - 1] is not None and lines[n - 1].strip() == "db 068h":
                    continue
                if n < start or n >= extent:
                    sys.exit("cannot port %s (0x%08X): line %d jumps inside it - %s"
                             % (name, address, n + 1, (lines[n] or "").strip()))

        definition = re.compile(r"^(PUBLIC )?" + symbol(name))
        for n in range(start, end):
            if lines[n] is not None:
                lines[n] = definition.sub(lambda m: (m.group(1) or "") + "orig_" + name, lines[n])
        branches_as_instructions(lines, name, branch_lines)
        externs.append("EXTERN %s:PROC" % name)
        rows.append("%08X\t%s\t%s" % (address, name, c_name))

    lines = [l for l in lines if l is not None]
    if externs:
        at = lines.index("INCLUDE game_externs.inc") + 1
        lines[at:at] = ["; Ported to C (tools/port.py)"] + externs

    with open(ASM, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    os.makedirs(os.path.dirname(PORTS), exist_ok=True)
    with open(PORTS, "w", encoding="utf-8") as f:
        f.write("".join(r + "\n" for r in rows))
    print("%d function(s) ported" % len(rows))


if __name__ == "__main__":
    main()
