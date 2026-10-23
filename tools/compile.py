# -*- coding: utf-8 -*-
"""Compile code/mcm2/*.c into build/obj for the link.

The objects are built without the CRT (the game carries its own, and its startup runs, not the
modern one). Their sections are then moved into one section of our own, .open, so that none of
our code, constants or data lands in the game's .text, .rdata or .data: those three must keep
their retail sizes and addresses.

Every part of .open is marked as initialised data that can also be executed. The linker places
sections by kind, and only initialised data goes after the game's .data: a code section would go
straight after .text, and zero-filled data before .rdata, moving every section the game addresses.
"""
import glob
import os
import subprocess
import sys

import coff
import toolchain

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "code", "mcm2")
# The dev build's own code: debugging tools and changes to the game, built with OPENMCM2_DEV.
DEV = os.path.join(ROOT, "code", "dev")
OBJ = os.path.join(ROOT, "build", "obj")

# /arch:IA32: floating point on the x87, as the game's own code does it, so ported arithmetic
# rounds as the original did instead of in SSE registers.
FLAGS = ["/nologo", "/c", "/O2", "/W4", "/WX", "/GS-", "/Zl", "/Gy", "/std:c11", "/arch:IA32",
         "/D_CRT_SECURE_NO_WARNINGS", "/DOPENMCM2_DEV", "/I" + SRC]

# Where each kind of section goes within .open. Section names are limited to eight characters,
# so the compiler's own $ suffixes (.text$mn) are dropped.
SECTIONS = {".text": ".open$t", ".rdata": ".open$r", ".data": ".open$d", ".bss": ".open$z"}

# Sections whose contents need the modern CRT's startup, which never runs here.
UNSUPPORTED = (".CRT", ".tls")


def msvc_environment():
    """The environment cl runs in for 32-bit builds (tools/toolchain.py)."""
    return toolchain.environment("x86")


def rename(name):
    base = name.partition("$")[0]
    if base in UNSUPPORTED:
        raise ValueError("section %s needs CRT startup, which does not run in this image" % name)
    return SECTIONS.get(base, name)


def main():
    sources = sorted(glob.glob(os.path.join(SRC, "*.c"))) + sorted(glob.glob(os.path.join(DEV, "*.c")))
    os.makedirs(OBJ, exist_ok=True)
    for stale in glob.glob(os.path.join(OBJ, "*.obj")):
        os.remove(stale)
    if not sources:
        print("no sources in %s" % SRC)
        return

    env = msvc_environment()
    cl = toolchain.command("cl", "x86", env)
    flags = [f if not f.startswith("/I") else "/I" + toolchain.native(f[2:]) for f in FLAGS]
    res = subprocess.run(cl + flags + ["/Fo" + toolchain.native(OBJ) + "\\"]
                         + [toolchain.native(s) for s in sources], env=env, cwd=SRC,
                         capture_output=True, text=True)
    print((res.stdout + res.stderr).strip())
    if res.returncode != 0:
        sys.exit("compile failed")

    for path in glob.glob(os.path.join(OBJ, "*.obj")):
        with open(path, "rb") as f:
            data = f.read()
        with open(path, "wb") as f:
            f.write(coff.move_sections(data, rename))
    print("compiled %d file(s) -> %s" % (len(sources), OBJ))


if __name__ == "__main__":
    main()
