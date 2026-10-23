# -*- coding: utf-8 -*-
"""The MSVC tools - cl, link, ml, rc - on Windows, or on Linux under Wine.

On Windows they are Visual Studio's: cl and rc in the environment vcvarsall sets up, link and ml
the newest Hostx64 copies, as the build has always found them.

On Linux they are the same MSVC, unpacked by msvc-wine (https://github.com/mstorsjo/msvc-wine:
vsdownload.py and install.sh) into MSVC_ROOT, /opt/msvc by default - tools/docker/Dockerfile makes
an image with it - and run through Wine by msvc-wine's wine-msvc.sh. This module sets up their
environment itself rather than through msvc-wine's msvcenv.sh, which puts the SDK's um libraries
on LIB: link.py keeps those off the search list on purpose, so a Win32 call the game does not
import fails the link instead of quietly building an import table.

The tools then see paths as Windows does. native() turns a Linux path into one (z: is / in every
Wine prefix), for options and response files the wrapper does not rewrite.
"""
import glob
import os
import re
import shutil
import subprocess
import sys

WINDOWS = os.name == "nt"
MSVC_ROOT = os.environ.get("MSVC_ROOT") or "/opt/msvc"


def native(path):
    """A path as the MSVC tools take it: itself on Windows, z:\\... under Wine.

    With backslashes, so msvc-wine's wrapper leaves it alone: it rewrites any argument that looks
    like an option followed by a Linux path, and -Foz:/work/game.obj looks like option -Foz: and
    path /work/game.obj."""
    path = os.path.abspath(path)
    return path if WINDOWS else "z:" + path.replace("/", "\\")


def run_exe(path):
    """The command that runs a Windows program built here: itself, or under Wine."""
    return [path] if WINDOWS else [shutil.which("wine") or "wine", path]


# ---- Windows: Visual Studio ----

def _visual_studio():
    """The newest Visual Studio with the C++ tools. -products * counts the Build Tools too, which
    vswhere leaves out by default, and which install under Program Files (x86)."""
    vswhere = os.path.join(os.environ["ProgramFiles(x86)"], "Microsoft Visual Studio", "Installer", "vswhere.exe")
    vs = subprocess.run([vswhere, "-latest", "-products", "*",
                         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                         "-property", "installationPath"],
                        capture_output=True, text=True, check=True).stdout.strip()
    if not vs:
        sys.exit("no Visual Studio with the C++ x86/x64 tools")
    return vs


def _vcvars(arch):
    """The environment vcvarsall sets up for `arch` builds."""
    vcvars = os.path.join(_visual_studio(), "VC", "Auxiliary", "Build", "vcvarsall.bat")
    out = subprocess.run('"%s" %s >nul && set' % (vcvars, arch), shell=True, capture_output=True, text=True,
                         check=True).stdout
    return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)


def _newest(name):
    hits = sorted(glob.glob(os.path.join(_visual_studio(), r"VC\Tools\MSVC\*\bin\Hostx64\x86\%s.exe" % name)))
    if not hits:
        sys.exit("could not find %s.exe" % name)
    return hits[-1]


# ---- Linux: msvc-wine ----

def _installed_versions():
    """(MSVC version, Windows SDK version) as msvc-wine's install.sh chose them."""
    env = os.path.join(MSVC_ROOT, "bin", "x86", "msvcenv.sh")
    if not os.path.isfile(env):
        sys.exit("no msvc-wine install in %s (set MSVC_ROOT; tools/docker/Dockerfile makes one)" % MSVC_ROOT)
    with open(env) as f:
        text = f.read()
    return (re.search(r"^MSVCVER=(\S+)", text, re.M).group(1),
            re.search(r"^SDKVER=(\S+)", text, re.M).group(1))


def _wine_dirs(arch):
    msvc, sdk = _installed_versions()
    base = "z:" + MSVC_ROOT
    return {
        "msvc": base + "/vc/tools/msvc/" + msvc,
        "sdk_include": base + "/kits/10/include/" + sdk,
        "sdk_lib": base + "/kits/10/lib/" + sdk,
        "bin": os.path.join(MSVC_ROOT, "vc", "tools", "msvc", msvc, "bin", "Hostx64", arch),
        "sdk_bin": os.path.join(MSVC_ROOT, "kits", "10", "bin", sdk, "x64"),
    }


def _wine_environment(arch):
    d = _wine_dirs(arch)
    env = dict(os.environ)
    env["INCLUDE"] = ";".join([d["msvc"] + "/include"] +
                              [d["sdk_include"] + "/" + p for p in ("shared", "ucrt", "um", "winrt")])
    env["LIB"] = ";".join([d["msvc"] + "/lib/" + arch, d["sdk_lib"] + "/ucrt/" + arch, d["sdk_lib"] + "/um/" + arch])
    env["LIBPATH"] = env["LIB"]
    env["WINEPATH"] = ";".join(["z:" + d["bin"], "z:" + d["sdk_bin"],
                                d["msvc"] + "/bin/Hostx64/x64"])
    env["WINEDLLOVERRIDES"] = "vcruntime140=n;vcruntime140_1=n"
    env.setdefault("WINEDEBUG", "-all")
    return env


# ---- Both ----

def environment(arch="x86"):
    """The environment cl and rc run in: vcvarsall's, or its like under Wine."""
    return _vcvars(arch) if WINDOWS else _wine_environment(arch)


def command(tool, arch="x86", env=None):
    """How to run `tool` (cl, link, ml, rc, lib) for `arch` builds, as a command list."""
    if WINDOWS:
        if tool in ("link", "ml") and arch == "x86":
            return [os.environ.get("LINK_EXE") or _newest("link")] if tool == "link" else [_newest("ml")]
        env = env or environment(arch)
        found = shutil.which(tool, path=env.get("Path") or env.get("PATH"))
        if not found:
            sys.exit("could not find %s.exe" % tool)
        return [found]
    d = _wine_dirs(arch)
    exe = os.path.join(d["sdk_bin"] if tool == "rc" else d["bin"], tool + ".exe")
    if not os.path.isfile(exe):
        sys.exit("could not find %s" % exe)
    return [os.path.join(MSVC_ROOT, "bin", arch, "wine-msvc.sh"), exe]


def lib_dirs(arch="x86"):
    """The MSVC and UCRT library folders, as the linker takes them: never the SDK's um."""
    if WINDOWS:
        link = command("link", arch)[0]
        # link.exe sits at <msvc>/bin/Hostx64/x86/link.exe, so the toolchain root is four levels up.
        msvc_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(link))))
        ucrt = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\Lib\*\ucrt\%s" % arch))
        if not ucrt:
            sys.exit("could not find the UCRT %s libraries" % arch)
        return [os.path.join(msvc_root, "lib", arch), ucrt[-1]]
    d = _wine_dirs(arch)
    return [d["msvc"] + "/lib/" + arch, d["sdk_lib"] + "/ucrt/" + arch]


def link_environment():
    """link.exe's environment: no LIB, so only the -LIBPATH folders link.py names are searched."""
    env = dict(os.environ) if WINDOWS else _wine_environment("x86")
    env.pop("LIB", None)
    env.pop("LIBPATH", None)
    return env
