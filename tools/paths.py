# -*- coding: utf-8 -*-
"""Where the tools find things, on Windows and on Linux alike. Each can be set in the environment.

  MCM2_WORK   the Ghidra workspace: proj (the database), bin\\Mcm2.exe, out (game.asm, game.obj).
              C:\\mcm2ghidra on Windows - outside OneDrive and with no spaces, see README.md -
              and ~/mcm2ghidra elsewhere.
  MCM2_EXE    the retail executable (sha256 in docs/binary.md); bin\\Mcm2.exe in the workspace.
  MCM2_ASM    the exported assembly, out\\game.asm; MCM2_OBJ the object it assembles to.
  MCM2_GAME   a game folder: the dev build goes there, and the release takes its menus from it.
  MCM2_FONTS  the folder with the Windows fonts the modern menus' art is drawn with (Bahnschrift,
              Segoe UI, Consolas); C:\\Windows\\Fonts on Windows. Elsewhere, a copy of them.
"""
import os

WINDOWS = os.name == "nt"

WORK = os.environ.get("MCM2_WORK") or (r"C:\mcm2ghidra" if WINDOWS
                                       else os.path.join(os.path.expanduser("~"), "mcm2ghidra"))
OUT = os.path.join(WORK, "out")
RETAIL = os.environ.get("MCM2_EXE") or os.path.join(WORK, "bin", "Mcm2.exe")
ASM = os.environ.get("MCM2_ASM") or os.path.join(OUT, "game.asm")
OBJ = os.environ.get("MCM2_OBJ") or os.path.join(OUT, "game.obj")

GAME = os.environ.get("MCM2_GAME") or (r"C:\Users\xshxd\OneDrive\Desktop\PC Games\MCM2" if WINDOWS
                                       else os.path.join(os.path.expanduser("~"), "Games", "MCM2"))
FONTS = os.environ.get("MCM2_FONTS") or (r"C:\Windows\Fonts" if WINDOWS
                                         else os.path.join(os.path.expanduser("~"), ".fonts", "windows"))


def find_folder(base, *parts):
    """base/parts..., each part matched whatever its case: a game folder copied from Windows has
    UI or ui, RES or res, and Linux tells them apart. The path as given where nothing matches."""
    path = base
    for part in parts:
        exact = os.path.join(path, part)
        if not os.path.exists(exact) and os.path.isdir(path):
            part = next((name for name in os.listdir(path) if name.lower() == part.lower()), part)
        path = os.path.join(path, part)
    return path
