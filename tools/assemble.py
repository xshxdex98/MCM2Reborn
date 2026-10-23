# -*- coding: utf-8 -*-
"""Assemble game.asm - the export, its ports handed to C by tools/port.py - into game.obj beside it.

MASM (ml.exe), Visual Studio's on Windows and msvc-wine's under Wine on Linux (tools/toolchain.py).
Which ml it is matters: tools/verify_encodings.py measured which mnemonics this one assembles back
to retail's exact bytes, and the export emits only those.
"""
import os
import re
import subprocess
import sys

import toolchain
from paths import ASM, OBJ


def main():
    if not os.path.exists(ASM):
        sys.exit("no assembly at %s - export it first (tools/ghidra/cycle.sh)" % ASM)
    res = subprocess.run(toolchain.command("ml") + ["-nologo", "-c", "-coff", "-Fo" + toolchain.native(OBJ),
                                                    toolchain.native(ASM)],
                         cwd=os.path.dirname(ASM), capture_output=True, text=True)
    said = [line for line in (res.stdout + res.stderr).splitlines()
            if line.strip() and not re.match(r"^\s*Assembling:", line)]
    if said:
        print("\n".join(said))
    if res.returncode:
        sys.exit("assembly failed")
    print("assembled -> %s (%d bytes)" % (OBJ, os.path.getsize(OBJ)))


if __name__ == "__main__":
    main()
