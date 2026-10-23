"""Build/run native regression fixtures against production C helpers (MSVC; on Linux under Wine,
see tools/toolchain.py). The menu and movie helpers are extracted verbatim so changes are always tested.
"""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import compile as compiler
import toolchain


def function(source, signature):
    start = source.index(signature)
    begin = source.index("{", start)
    depth = 1
    end = begin + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    out = ROOT / "build" / "regressions"
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "code/mcm2/ui.c").read_text(encoding="utf-8")
    start = source.index("BOOL pad_navigation_active = FALSE;")
    (out / "menu_under_test.inc").write_text(source[start:source.rindex("#endif")], encoding="utf-8")
    (out / "movie_under_test.inc").write_text(function(
        (ROOT / "code/mcm2/dlgprocs.c").read_text(encoding="utf-8"),
        "static void menu_movie_pause("), encoding="utf-8")
    (out / "align_under_test.inc").write_text(function(source, "static DWORD reader_alignment("), encoding="utf-8")
    (out / "mark_under_test.inc").write_text(function(source, "static void dialog_pad_mark("), encoding="utf-8")
    counts = (ROOT / "code/mcm2/selectgamepicprocs.c").read_text(encoding="utf-8")
    begin = counts.index("int g_track_counts[6][3]")
    end = counts.index("#endif", begin)
    (out / "counts_under_test.inc").write_text(counts[begin:end], encoding="utf-8")
    env = compiler.msvc_environment()
    native = toolchain.native
    command = toolchain.command("cl", "x86", env) + [
               "/nologo", "/std:c11", "/O2", "/Gy", "/W4", "/WX", "/wd4100", "/wd4505", "/arch:IA32",
               "/DOPENMCM2_DEV", "/D_CRT_SECURE_NO_WARNINGS", "/I" + native(ROOT / "code/mcm2"),
               "/I" + native(out), native(ROOT / "tests/native_regressions.c"), native(ROOT / "code/dev/padsony.c"),
               "/Fe" + native(out / "native.exe"), "/Fo" + native(out) + "\\",
               "/link", "/OPT:REF", "winmm.lib", "user32.lib"]
    subprocess.run(command, cwd=ROOT, env=env, check=True)
    subprocess.run(toolchain.run_exe(str(out / "native.exe")), check=True)
    input_command = command[:]
    source_at = input_command.index(native(ROOT / "tests/native_regressions.c"))
    input_command[source_at:source_at + 2] = [native(ROOT / "tests/input_regressions.c")]
    input_command[input_command.index("/Fe" + native(out / "native.exe"))] = "/Fe" + native(out / "input.exe")
    subprocess.run(input_command, cwd=ROOT, env=env, check=True)
    subprocess.run(toolchain.run_exe(str(out / "input.exe")), check=True)
    counts_command = input_command[:]
    counts_command[counts_command.index(native(ROOT / "tests/input_regressions.c"))] = native(ROOT / "tests/count_regressions.c")
    counts_command[counts_command.index("/Fe" + native(out / "input.exe"))] = "/Fe" + native(out / "counts.exe")
    subprocess.run(counts_command, cwd=ROOT, env=env, check=True)
    subprocess.run(toolchain.run_exe(str(out / "counts.exe")), check=True)


if __name__ == "__main__":
    main()
