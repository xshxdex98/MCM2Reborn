"""build_launcher.py: MCM2:Reborn's launcher (launcher/), as build/launcher/MCM2Reborn Launcher.exe.

It is a .NET Framework 4.8 program, built with the .NET SDK (dotnet build) from the reference
assemblies it caches, so it runs on any Windows 10 or 11 as it is - and it builds the same on
Linux. The menus' fonts go inside it, from the game folder's ui\\modern\\fonts (MCM2_GAME, see
tools/paths.py).
"""
import os
import shutil
import subprocess
import sys

from paths import GAME, find_folder

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
PROJECT = os.path.join(REPO, "launcher", "MCM2RebornLauncher.csproj")
OUT = os.path.join(REPO, "build", "launcher")
EXE = "MCM2Reborn Launcher.exe"


def main():
    fonts = find_folder(GAME, "UI", "modern", "fonts")
    if not os.path.isdir(fonts):
        sys.exit("no menu fonts in %s (set MCM2_GAME)" % fonts)
    built = os.path.join(REPO, "build", "launcher-obj")
    result = subprocess.run(["dotnet", "build", PROJECT, "-c", "Release", "-nologo", "-v", "q",
                             "-p:FontsDir=" + fonts, "-o", built,
                             "-p:BaseIntermediateOutputPath=" + os.path.join(built, "obj") + os.sep])
    if result.returncode:
        sys.exit("the launcher did not build")
    os.makedirs(OUT, exist_ok=True)
    shutil.copy2(os.path.join(built, EXE), os.path.join(OUT, EXE))
    print("built", os.path.join(OUT, EXE))


if __name__ == "__main__":
    main()
