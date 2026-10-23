"""release.py: MCM2:Reborn's release package, build/release/MCM2Reborn-<version>.zip, and the
launcher beside it (an offline install: the launcher installs from a package next to it).

The package is MCM2:Reborn's own files, laid out as in the game's folder - never the game's:
  MCM2Reborn.exe            the dev build (build/OpenMCM2.exe), named for its settings file
  MCM2Reborn.ini            dist/MCM2Reborn.ini; the launcher keeps a player's own
  mcm2-eos.exe, EOSSDK-Win64-Shipping.dll, eos.ini   online play (eos.ini from the game folder)
  UI/modern/...             the modern menus, their fonts and photos (from the game folder)
  RES/..., TERAFORM/...     the shipped content (dist/content, tools/content.py)
  MCM2Reborn Launcher.exe   the launcher (tools/build_launcher.py)
  reborn-version.txt        the version, as dev.h's GAME_VERSION

Publishing it is a GitHub release of xshxdex98/MCM2Reborn tagged v<version> with the zip as its asset;
this only builds it.
"""
import os
import re
import shutil
import sys
import zipfile

from paths import GAME, find_folder

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "build", "release")
LAUNCHER = "MCM2Reborn Launcher.exe"


def version():
    with open(os.path.join(REPO, "code", "dev", "dev.h")) as f:
        found = re.search(r'#define GAME_VERSION "([^"]+)"', f.read())
    if not found:
        sys.exit("no GAME_VERSION in code/dev/dev.h")
    return found.group(1)


def need(path):
    if not os.path.exists(path):
        sys.exit("missing %s" % path)
    return path


def main():
    tag = "v" + version()
    files = [
        (need(os.path.join(REPO, "build", "OpenMCM2.exe")), "MCM2Reborn.exe"),
        (need(os.path.join(REPO, "dist", "MCM2Reborn.ini")), "MCM2Reborn.ini"),
        (need(os.path.join(REPO, "build", "eos", "mcm2-eos.exe")), "mcm2-eos.exe"),
        (need(os.path.join(REPO, "build", "eos", "EOSSDK-Win64-Shipping.dll")), "EOSSDK-Win64-Shipping.dll"),
        (need(find_folder(GAME, "eos.ini")), "eos.ini"),
        (need(os.path.join(REPO, "build", "launcher", LAUNCHER)), LAUNCHER),
    ]
    for root, under in ((find_folder(GAME, "UI", "modern"), "UI/modern"),
                        (need(os.path.join(REPO, "dist", "content")), "")):
        for folder, _, names in os.walk(need(root)):
            for name in names:
                path = os.path.join(folder, name)
                files.append((path, "/".join(p for p in (under, os.path.relpath(path, root).replace(os.sep, "/")) if p)))

    os.makedirs(OUT, exist_ok=True)
    package = os.path.join(OUT, "MCM2Reborn-%s.zip" % tag)
    with zipfile.ZipFile(package, "w", zipfile.ZIP_DEFLATED) as z:
        for path, name in files:
            z.write(path, name)
        z.writestr("reborn-version.txt", tag)
    shutil.copy2(os.path.join(REPO, "build", "launcher", LAUNCHER), os.path.join(OUT, LAUNCHER))
    print("%s: %d files, %.1f MB" % (package, len(files) + 1, os.path.getsize(package) / 1e6))


if __name__ == "__main__":
    main()
