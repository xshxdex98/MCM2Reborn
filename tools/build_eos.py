# -*- coding: utf-8 -*-
"""Build mcm2-eos.exe, the 64-bit EOS helper (code/eos/helper.c), into build/eos.

Epic's SDK is 64-bit only, so this is an ordinary 64-bit program with the modern CRT, unlike the
game's own code. The SDK is looked for in the folder named by MCM2_EOS_SDK, else in SDK beside
the game (MCM2_GAME, see tools/paths.py). Its DLL is copied next to the exe. On Linux the MSVC
tools run under Wine (tools/toolchain.py).
"""
import os
import shutil
import subprocess
import sys

import toolchain
from paths import GAME

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "code", "eos")
OUT = os.path.join(ROOT, "build", "eos")
SDK = os.environ.get("MCM2_EOS_SDK") or os.path.join(GAME, "SDK")
DLL = "EOSSDK-Win64-Shipping.dll"


def main():
    if not os.path.isfile(os.path.join(SDK, "Include", "eos_sdk.h")):
        sys.exit("no EOS SDK in %s (set MCM2_EOS_SDK)" % SDK)
    os.makedirs(OUT, exist_ok=True)
    env = toolchain.environment("x64")
    cl, rc = toolchain.command("cl", "x64", env), toolchain.command("rc", "x64", env)
    native = toolchain.native
    resources = os.path.join(OUT, "helper.res")       # its file details and manifest (helper.rc)
    res = subprocess.run(rc + ["/nologo", "/fo", native(resources), native(os.path.join(SRC, "helper.rc"))],
                         env=env, cwd=SRC, capture_output=True, text=True)
    if res.returncode:
        sys.exit(res.stdout + res.stderr)
    res = subprocess.run(cl + ["/nologo", "/O2", "/W4", "/WX", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                               "/I" + native(os.path.join(SDK, "Include")), native(os.path.join(SRC, "helper.c")),
                               "/Fo" + native(OUT) + "\\", "/Fe" + native(os.path.join(OUT, "mcm2-eos.exe")),
                               "/link", native(resources), native(os.path.join(SDK, "Lib", "EOSSDK-Win64-Shipping.lib"))],
                         env=env, cwd=OUT, capture_output=True, text=True)
    sys.stdout.write(res.stdout + res.stderr)
    if res.returncode:
        sys.exit(res.returncode)
    shutil.copy2(os.path.join(SDK, "Bin", DLL), os.path.join(OUT, DLL))
    print("built %s" % os.path.join(OUT, "mcm2-eos.exe"))


if __name__ == "__main__":
    main()
