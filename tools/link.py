# -*- coding: utf-8 -*-
"""Link game.obj into a runnable executable.

At 0% reimplemented this produces a binary that IS the original game: every function is still the
original machine code, merely assembled and linked by us rather than shipped by Rainbow Studios.
That is the point. It is the baseline the port is measured against, and it is what makes each
ported function individually testable the day it is written.

One setting is not optional:

  -FIXED -BASE:0x400000   Mcm2.exe has IMAGE_FILE_RELOCS_STRIPPED set and no base-relocation
                          table, so it can only ever load at 0x400000. Anything else and every
                          absolute address baked into the code is wrong - silently, because there
                          is no relocation data for the loader to complain about.

No import libraries are linked. Retail's import table is part of the .rdata the assembly emits
and every import is called through its slot there, so the linker has nothing to import and builds
no table of its own. That keeps .text, .rdata and .data at their retail sizes and addresses, so
every pointer in the data is right as it stands. The PE header is then pointed at retail's import
tables (adopt_retail_header), and the build is refused if any of those sections has moved. The one
difference is a DLL whose every import is ported (DROPPED_DLLS): the loader is given retail's
descriptors less that one, so it need not be installed.

Sections of our own (compiled code, see build/obj) land after .data, and .rsrc after them: it is
built by the linker from build/game.res (tools/resources.py), which keeps its RVAs right wherever
it ends up.

Compiled code reaches the game through two conventions, both bound here with /alternatename:

  Win32 calls         `__imp__CreateFileA@28`, as the compiler spells a __stdcall import, is
                      bound to the game's own `__imp__CreateFileA` slot. A function the game does
                      not import cannot be called this way (it would need an import table) and
                      stops the link; resolve it at runtime instead.
  game_<name>         a C declaration of `game_entry` binds to the game's label `entry`.
  ports               a function tools/port.py handed to C: the game's name for it binds to the C
                      definition, whatever its calling convention made of the name.

If the compiled code defines `open_start`, that is the entry point; it runs the game's own.

THE CRT IS NOT LINKED IN. Mcm2.exe statically linked its own 1999 CRT and every one of those
routines is already in game.obj under its ordinary C name - `_memcpy`, `_malloc`, `_sprintf`. The
MSVC and UCRT library directories are on the search list only so a symbol game.obj does NOT define
can be satisfied rather than becoming a hard link error; one that needs a Windows import fails to
link, since there are no import libraries. Nothing that needs CRT initialisation will work
regardless: the entry point is the game's own 1999 startup and the modern initialiser never runs.
"""
import glob
import os
import re
import struct
import subprocess
import sys

import coff
import toolchain
from paths import ASM, OBJ, RETAIL

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

OUT = os.path.join(ROOT, "build", "OpenMCM2.exe")

IMAGE_BASE = "0x400000"

# Which PUBLIC symbol is the entry point depends on the Ghidra database: it creates its own `entry`
# label at the image entry point, and a symbol recovered from elsewhere can win as primary. So the
# name is read out of the assembly rather than assumed. Getting this wrong does not say so - the
# linker falls back to _WinMainCRTStartup and reports THAT as the unresolved symbol, which points
# nowhere near the actual cause.
ENTRY_CANDIDATES = ("entry", "start", "WinMainCRTStartup")

RES = os.path.join(ROOT, "build", "game.res")
PORTS = os.path.join(ROOT, "build", "ports.tsv")

# The sections the game addresses, which must stay exactly where retail has them.
FIXED_SECTIONS = (".text", ".rdata", ".data")


# The data directories taken from retail: their tables are emitted verbatim at retail addresses.
DIR_IMPORT, DIR_IAT = 1, 12

# DLLs the game imports from but no longer calls: every function it takes from them is ported, so
# they are left out of the import directory the loader reads and need not exist. d3drm.dll, for
# D3DRMVectorRotate alone (code/mcm2/d3drm.c): Windows has not shipped it since XP. That directory
# is retail's descriptors less those, in room the C keeps (IMPORT_ROOM); retail's own table stays
# in .rdata as it is, and the IAT directory is still retail's.
DROPPED_DLLS = ("d3drm.dll",)
IMPORT_ROOM = "_import_directory"
MAP = os.path.join(ROOT, "build", "OpenMCM2.map")

# The OS and subsystem versions (four words at optional header +40 and +48). Windows only
# virtualises the registry for an executable that declares a pre-Vista version, and the game keeps
# its settings, its EULA acceptance and its profiles under HKLM: declaring the linker's 6.0 makes
# every one of those writes fail, so it asks again on every launch.
VERSIONS = ((40, 4), (48, 4))


def cpp_objects():
    """Compiled reimplementations. These take precedence over the stripped assembly."""
    return sorted(glob.glob(os.path.join(ROOT, "build", "obj", "*.obj")))


def game_symbols():
    """(the game's import names, its public labels), as defined in the assembly."""
    imports, publics = set(), set()
    with open(ASM, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("PUBLIC "):
                name = line[7:].strip()
                (imports if name.startswith("__imp__") else publics).add(name)
    return imports, publics


# Compiler helpers the compiled code calls, bound to the game's own copies. The game's __ftol is
# MSVC 6's: the value in ST0, the result in EDX:EAX, ST0 popped, which is what __ftol2 is too.
# 0x0053B540 is its _chkstk, unnamed in the export: the frame's size in EAX, each page probed,
# ESP moved below them - the contract a frame over 4 KB calls __chkstk with.
COMPILER_HELPERS = {"__ftol2": "__ftol", "__ftol2_sse": "__ftol", "__chkstk": "FUN_0053b540"}


def ported_names():
    """The game names of the functions tools/port.py handed to C; they are bound to C code."""
    if not os.path.exists(PORTS):
        return set()
    with open(PORTS, encoding="utf-8") as f:
        return {line.split()[1] for line in f if line.strip()}


def bindings(objects):
    """/alternatename options binding the compiled code's undefined symbols to the game."""
    imports, publics = game_symbols()
    publics = publics | ported_names()
    options, missing = set(), set()
    for path in objects:
        with open(path, "rb") as f:
            data = f.read()
        for name, section, storage in coff.symbols(data):
            if section != 0 or storage != 2:     # defined, or not external
                continue
            m = re.match(r"^__imp__(\w+)@\d+$", name)
            if m:
                target = "__imp__" + m.group(1)
                (options.add("/ALTERNATENAME:%s=%s" % (name, target)) if target in imports
                 else missing.add("%s (not imported by the game)" % m.group(1)))
                continue
            # A Win32 call the header does not mark dllimport (imm.h's) goes to a plain routine:
            # the game's own thunk of that name, which jumps through its import slot.
            m = re.match(r"^_(\w+)@\d+$", name)
            if m and m.group(1) in publics and "__imp__" + m.group(1) in imports:
                options.add("/ALTERNATENAME:%s=%s" % (name, m.group(1)))
                continue
            if name in COMPILER_HELPERS:
                target = COMPILER_HELPERS[name]
                (options.add("/ALTERNATENAME:%s=%s" % (name, target)) if target in publics
                 else missing.add("%s (the game has no %s)" % (name, target)))
                continue
            m = re.match(r"^[_@]game_(\w+?)(@\d+)?$", name)
            if m:
                target = m.group(1)
                (options.add("/ALTERNATENAME:%s=%s" % (name, target)) if target in publics
                 else missing.add("%s (no such game label)" % target))
    if missing:
        sys.exit("compiled code needs symbols the game does not have:\n  " + "\n  ".join(sorted(missing)))
    return sorted(options) + port_bindings(objects)


def port_bindings(objects):
    """/alternatename options binding each ported function's game name to its C definition."""
    if not os.path.exists(PORTS):
        return []

    defined = {}
    for path in objects:
        with open(path, "rb") as f:
            for name, section, storage in coff.symbols(f.read()):
                if section > 0 and storage == 2:
                    m = re.match(r"^[_@](\w+?)(@\d+)?$", name)
                    if m:
                        defined[m.group(1)] = name

    options = []
    with open(PORTS, encoding="utf-8") as f:
        for line in f:
            address, game_name, c_name = line.split()
            if c_name not in defined:
                sys.exit("0x%s is ported to %s, which no object defines" % (address, c_name))
            options.append("/ALTERNATENAME:%s=%s" % (game_name, defined[c_name]))
    return options


def defines(objects, name):
    for path in objects:
        with open(path, "rb") as f:
            if any(n == name and section > 0 for n, section, _ in coff.symbols(f.read())):
                return True
    return False


def pe_layout(data):
    """(offset of the data directory table, {section name: (virtual address, virtual size)})."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count, = struct.unpack_from("<H", data, pe + 6)
    opt_size, = struct.unpack_from("<H", data, pe + 20)
    table = pe + 24 + opt_size
    sections = {}
    for i in range(count):
        name, size, va = struct.unpack_from("<8sII", data, table + 40 * i)
        sections[name.rstrip(b"\0").decode()] = (va, size)
    return pe + 24 + 96, sections


def file_offset(data, rva):
    """Where the byte at `rva` is in the file, or None if no section's raw data holds it."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count, = struct.unpack_from("<H", data, pe + 6)
    opt_size, = struct.unpack_from("<H", data, pe + 20)
    for i in range(count):
        va, raw_size, raw = struct.unpack_from("<12xIII", data, pe + 24 + opt_size + 40 * i)
        if va <= rva < va + raw_size:
            return rva - va + raw
    return None


def c_string(data, rva):
    at = file_offset(data, rva)
    return data[at:data.index(b"\0", at)].decode("latin-1")


def import_descriptors(image):
    """[(DLL name, its 20-byte descriptor, the names it imports - None for an ordinal)]."""
    dirs, _ = pe_layout(image)
    rva, = struct.unpack_from("<I", image, dirs + 8 * DIR_IMPORT)
    at, found = file_offset(image, rva), []
    while True:
        entry = image[at:at + 20]
        lookup, _, _, name, slots = struct.unpack("<5I", entry)
        if name == 0:
            return found
        names, n = [], file_offset(image, lookup or slots)
        while True:
            thunk, = struct.unpack_from("<I", image, n)
            if thunk == 0:
                break
            names.append(None if thunk & 0x80000000 else c_string(image, thunk + 2))
            n += 4
        found.append((c_string(image, name), entry, names))
        at += 20


def check_dropped(dll, names, objects):
    """Refuse the build if anything could still call through `dll`'s import slots."""
    ported = ported_names()
    for name in names:
        if name is None or name not in ported:
            sys.exit("%s is left out of the import directory, but its %s is not ported"
                     % (dll, name or "import by ordinal"))
        slot = re.compile(r"(?<![\w?@$])__imp__%s(?![\w?@$])" % re.escape(name))
        proc = None
        with open(ASM, encoding="utf-8", errors="replace") as f:
            for n, line in enumerate(f, 1):
                words = line.split()
                if len(words) >= 2 and words[1] in ("PROC", "ENDP"):
                    proc = words[0] if words[1] == "PROC" else None
                elif slot.search(line) and not line.startswith(("PUBLIC ", "__imp__")) \
                        and not (proc or "").startswith("orig_"):
                    sys.exit("%s is left out of the import directory, but %s line %d still calls "
                             "through its %s slot" % (dll, ASM, n, name))
        for path in objects:
            with open(path, "rb") as f:
                for symbol, section, _ in coff.symbols(f.read()):
                    if section == 0 and re.match(r"^__imp__%s@\d+$" % re.escape(name), symbol):
                        sys.exit("%s is left out of the import directory, but %s imports %s from it"
                                 % (dll, os.path.basename(path), name))


def import_room():
    """(RVA, size) of the room the C keeps for the import directory, from the map: up to the next
    symbol, so never more than it has."""
    addresses = []
    room = None
    with open(MAP, encoding="latin-1") as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 3 and re.match(r"^[0-9a-f]{4}:[0-9a-f]{8}$", parts[0]) \
                    and re.match(r"^[0-9a-f]{8}$", parts[2]):
                address = int(parts[2], 16)
                addresses.append(address)
                if parts[1] == IMPORT_ROOM:
                    room = address
    if room is None:
        sys.exit("no %s in the map: code/mcm2/d3drm.c keeps the import directory's room" % IMPORT_ROOM)
    following = [a for a in addresses if a > room]
    return room - int(IMAGE_BASE, 16), (min(following) - room) if following else 0


def write_import_directory(built, retail, objects):
    """The loader's import directory: retail's descriptors less DROPPED_DLLS, in the C's room."""
    kept = []
    for dll, entry, names in import_descriptors(retail):
        if dll.lower() in DROPPED_DLLS:
            check_dropped(dll, names, objects)
        else:
            kept.append(entry)
    table = b"".join(kept) + bytes(20)
    rva, room = import_room()
    at = file_offset(built, rva)
    if at is None or len(table) > room:
        sys.exit("the import directory needs %d bytes, and %s has %d in the file - make it larger"
                 % (len(table), IMPORT_ROOM, room if at is not None else 0))
    built[at:at + len(table)] = table
    return struct.pack("<II", rva, len(table))


def adopt_retail_header(path, objects):
    """Point the built image at retail's import and IAT tables (the import directory less
    DROPPED_DLLS), and declare retail's versions.

    Those tables, and every pointer in the game's data, are only valid if .text, .rdata and .data
    are at their retail addresses - which is checked first, because a section that moved would
    leave them pointing at whatever now sits there.
    """
    with open(RETAIL, "rb") as f:
        retail = f.read()
    with open(path, "rb") as f:
        built = bytearray(f.read())

    retail_dirs, retail_sections = pe_layout(retail)
    built_dirs, built_sections = pe_layout(built)
    retail_opt, built_opt = retail_dirs - 96, built_dirs - 96

    moved = ["%s 0x%X -> 0x%X" % (name, retail_sections[name][0], built_sections.get(name, (0, 0))[0])
             for name in FIXED_SECTIONS if built_sections.get(name, (0, 0))[0] != retail_sections[name][0]]
    if moved:
        sys.exit("sections are not at their retail addresses: " + ", ".join(moved))

    # Compiled code belongs in .open. Anything that reached .data - a common symbol, say - moves
    # nothing, so the check above cannot see it.
    if built_sections[".data"][1] != retail_sections[".data"][1]:
        sys.exit(".data is 0x%X bytes against retail's 0x%X: compiled code put something in it - see the map"
                 % (built_sections[".data"][1], retail_sections[".data"][1]))

    entry = retail_dirs + 8 * DIR_IAT
    built[built_dirs + 8 * DIR_IAT:built_dirs + 8 * DIR_IAT + 8] = retail[entry:entry + 8]
    built[built_dirs + 8 * DIR_IMPORT:built_dirs + 8 * DIR_IMPORT + 8] = \
        write_import_directory(built, retail, objects)

    for offset, size in VERSIONS:
        built[built_opt + offset:built_opt + offset + size] = retail[retail_opt + offset:retail_opt + offset + size]

    with open(path, "wb") as f:
        f.write(built)


def entry_point():
    if not os.path.exists(ASM):
        sys.exit("no assembly at %s" % ASM)
    declared = set()
    with open(ASM, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("PUBLIC "):
                declared.add(line[7:].strip())
    for name in ENTRY_CANDIDATES:
        if name in declared:
            return name
    sys.exit("no entry point in %s - looked for %s" % (ASM, ", ".join(ENTRY_CANDIDATES)))


def main():
    if not os.path.exists(OBJ):
        sys.exit("no object at %s - assemble game.asm first" % OBJ)
    if not os.path.exists(RES):
        sys.exit("no resources at %s - run tools/resources.py first" % RES)

    link = toolchain.command("link")
    libs = toolchain.lib_dirs()

    objects = cpp_objects()
    entry = "open_start" if defines(objects, "_open_start") else entry_point()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    native = toolchain.native
    cmd = [
        "-NOLOGO",
        "-MACHINE:X86",
        "-SUBSYSTEM:WINDOWS",
        "-BASE:" + IMAGE_BASE,
        "-FIXED",
        "-ENTRY:" + entry,

        # The whole 4 GB a 64-bit Windows gives a 32-bit process. At high resolutions with
        # anti-aliasing the GL driver can use up the first 2 GB after a few races, and crashes.
        "-LARGEADDRESSAWARE",

        # uuid.lib is requested by a CRT directive and holds nothing but COM interface GUID
        # constants, which this image already carries in its own data - they are emitted out of
        # Mcm2.exe's .rdata. It lives only in the SDK's um directory, the one path that must stay
        # off the search list, so it is excluded by name rather than opening that door.
        "-NODEFAULTLIB:uuid.lib",

        # A map, so an address in appended C++ can be named. Without one a crash inside a
        # reimplementation is just a number.
        "-MAP:" + native(os.path.join(ROOT, "build", "OpenMCM2.map")),

        "-OUT:" + native(OUT),

        # Referenced by nothing but the header write after the link, so kept by name.
        "-INCLUDE:" + IMPORT_ROOM,
    ] + ["-LIBPATH:" + lib for lib in libs] + [
        native(OBJ),
        native(RES),
    ] + [native(o) for o in objects] + bindings(objects)

    print("link.exe : %s" % link[-1])
    print("entry    : %s" % entry)
    print("object   : %s (%d bytes)" % (OBJ, os.path.getsize(OBJ)))
    # Every port adds a switch, and the lot outgrows the 32K a command line may be; link.exe
    # reads them from a response file instead, one to a line.
    rsp = os.path.join(ROOT, "build", "link.rsp")
    with open(rsp, "w", encoding="utf-8") as f:
        f.write("\n".join('"%s"' % arg for arg in cmd) + "\n")
    res = subprocess.run(link + ["@" + native(rsp)], env=toolchain.link_environment(), capture_output=True,
                         text=True)
    out = (res.stdout or "") + (res.stderr or "")
    if res.returncode != 0:
        # Unresolved externals come in thousands when something structural is wrong and in ones
        # when a real symbol is missing, so show enough to tell those apart.
        lines = out.splitlines()
        errs = [ln for ln in lines if "error" in ln.lower()]
        print("\n".join(lines[:6]))
        print("... %d error lines; first 25:" % len(errs))
        print("\n".join(errs[:25]))
        sys.exit(res.returncode)

    print(out.strip())
    adopt_retail_header(OUT, objects)
    print("\nlinked -> %s (%d bytes), retail import tables and versions adopted, without %s"
          % (OUT, os.path.getsize(OUT), ", ".join(DROPPED_DLLS)))


if __name__ == "__main__":
    main()
