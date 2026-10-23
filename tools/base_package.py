"""base_package.py [game folder]: the base game's package, build/release/MCM2-base.zip.

For a player without a copy of Motocross Madness 2 of their own: the launcher's DOWNLOAD choice
unpacks it, then MCM2:Reborn over it (launcher/Updates.cs, InstallBase). It is the clean copy
given, whole, without what is a player's own: the profiles in UI\\PROFILE (its EMPTYFIL kept, as the game ships it), the
settings the game keeps in mcm2_profile.json, and its error log. The player names a first rider
in the launcher instead.

Publishing it is an asset of the same name on a GitHub release of xshxdex98/MCM2Reborn; this only
builds it.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "build", "release", "MCM2-base.zip")
SOURCE = r"C:\Users\xshxd\OneDrive\Desktop\MCM2" if os.name == "nt" else None
LEFT_OUT = {"mcm2_profile.json", "errorlog.txt"}


def wanted(relative):
    parts = relative.replace("\\", "/").split("/")
    if len(parts) == 1:
        return parts[0].lower() not in LEFT_OUT
    if [p.lower() for p in parts[:2]] == ["ui", "profile"]:
        return len(parts) == 3 and parts[2].upper() == "EMPTYFIL"
    return True


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else SOURCE
    if not source:
        sys.exit("usage: base_package.py <clean game folder>")
    if not os.path.isfile(os.path.join(source, "Mcm2.exe")):
        sys.exit("no Mcm2.exe in %s" % source)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    count = 0
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for folder, _, names in os.walk(source):
            for name in sorted(names):
                path = os.path.join(folder, name)
                relative = os.path.relpath(path, source)
                if wanted(relative):
                    z.write(path, relative.replace(os.sep, "/"))
                    count += 1
    print("%s: %d files, %.0f MB" % (OUT, count, os.path.getsize(OUT) / 1e6))


if __name__ == "__main__":
    main()
