# The target binary

Everything here was read out of the file, not inferred. Where a number matters to the port, the
tool that produced it is named so it can be re-run.

## Which file is the target, and why

The install at `Desktop\PC Games\MCM2` holds three executables. Only one is analysable:

| file | size | what it is |
|---|--:|---|
| `Mcm2.exe` | 1,577,005 | **the target.** A clean MSVC 6.0 PE32 with real code, RTTI and a sane entry point. |
| `MCM2.ICD` | 1,577,005 | the SafeDisc payload. Same length as `Mcm2.exe` and **1,463,835 of its bytes differ** - 93% - so it is still encrypted. Nothing can be recovered from it. |
| `MCM2-original.EXE` | 252,856 | the original SafeDisc loader stub. Not the game. |

`Mcm2.exe` is therefore the decrypted image, and it is what the game actually runs.

    sha256  cfcffdca19314b630954c5dbd0003cd1dd4a7c6b63d4a5739cc293af0bcadda4

**Every address in this project is valid only for that hash.** The copy under
`C:\mcm2ghidra\bin\Mcm2.exe` is byte-identical and is what Ghidra imports; the Ghidra workspace is
deliberately outside OneDrive and space-free, because OneDrive corrupts Ghidra project files and
`analyzeHeadless.bat` re-splits arguments on spaces.

One cosmetic tell that this image was patched: the PE timestamp is `0x6B637566`, which is the ASCII
`fuck` little-endian. It is a cracker's stamp, it has no effect on the code, and it is recorded here
only so nobody later mistakes it for a meaningful date.

## PE headers

Measured by `tools/pedump.py`.

| | |
|---|---|
| machine | `0x014C` i386 |
| optional header | `0x010B` PE32 |
| linker | **6.0** (Visual C++ 6.0) |
| image base | `0x00400000` |
| entry | RVA `0x0013D6F5`, VA **`0x0053D6F5`** |
| section alignment | `0x1000` |
| file alignment | **`0x1000`** |
| size of image | `0x296000` |
| subsystem | 2, WINDOWS_GUI |
| characteristics | `0x010F` = RELOCS_STRIPPED, EXECUTABLE_IMAGE, LINE_NUMS_STRIPPED, LOCAL_SYMS_STRIPPED, 32BIT_MACHINE |

Note the file alignment is `0x1000`, not the usual `0x200`. It matters when reasoning about where a
section's raw bytes sit relative to its virtual address.

### Sections

| name | VA | virtual size | raw ptr | raw size |
|---|---|--:|---|--:|
| `.text` | `0x00401000` | `0x153A7E` | `0x00001000` | `0x154000` |
| `.rdata` | `0x00555000` | `0x01646F` | `0x00155000` | `0x017000` |
| `.data` | `0x0056C000` | `0x1265E0` | `0x0016C000` | `0x012000` |
| `.rsrc` | `0x00693000` | `0x002286` | `0x0017E000` | `0x003000` |

`.text` is 1,391,230 bytes. For scale, MM2's is about 1.76 MB, so this is roughly four fifths the
size of a port that reached a linking binary - the "smaller game" expectation is borne out, but not
by an order of magnitude.

`.data`'s virtual size (`0x1265E0`) is fifteen times its raw size (`0x12000`); the remainder is
uninitialised BSS that the loader zero-fills.

There is **no `.idata` section** - the import table lives inside `.rdata`, exactly as in MM2. That
is the same condition that leaves MM2's `.data` sitting `0x3000` high, because the linker builds its
own import table and places it in `.rdata` on top of the original. Expect to meet it here too.

## The relocation problem is identical to MM2's

`RELOCS_STRIPPED` is set **and** the base-relocation data directory is absent (RVA 0, size 0). So:

- The image can only ever load at `0x400000`; the link must use `-FIXED -BASE:0x400000`.
- Nothing in the file says which four-byte words in `.data` are pointers and which are integers or
  floats that merely look like addresses. Code operands are recoverable because Ghidra's analysis
  determines from context which are addresses; data words are not, and must be treated
  conservatively. `docs/harness.md` in the OpenMM2 tree records what that cost and the four rules
  that were tried before one worked.

This is the single most important structural fact, and it is why the OpenMM2 harness design carries
over unchanged rather than needing a new one.

## Imports

Measured by `tools/imports.py`. **15 DLLs, 244 functions.**

| DLL | funcs | by ordinal | IAT VA |
|---|--:|--:|---|
| `KERNEL32.dll` | 121 | 0 | `0x005550C0` |
| `USER32.dll` | 54 | 0 | `0x005552BC` |
| `GDI32.dll` | 18 | 0 | `0x00555044` |
| `IMM32.dll` | 11 | 0 | `0x00555090` |
| `WSOCK32.dll` | 10 | **10** | `0x005553BC` |
| `ADVAPI32.dll` | 9 | 0 | `0x00555000` |
| `WINMM.dll` | 8 | 0 | `0x00555398` |
| `ole32.dll` | 3 | 0 | `0x005553FC` |
| `blade.dll` | 2 | 0 | `0x005553E8` |
| `MSVFW32.dll` | 2 | 0 | `0x005552A8` |
| `DSOUND.dll` | 2 | **2** | `0x00555038` |
| `SHELL32.dll` | 1 | 0 | `0x005552B4` |
| `d3drm.dll` | 1 | 0 | `0x005553F4` |
| `DINPUT.dll` | 1 | 0 | `0x00555028` |
| `DPLAYX.dll` | 1 | **1** | `0x00555030` |

Three of these would matter to an import library - 13 imports are by ordinal with no name (all of
`WSOCK32`, both of `DSOUND`, the single `DPLAYX` entry), `DPLAYX.dll` and `DINPUT.dll` are gone from
the modern SDK, and `blade.dll` is the game's own - but none is needed any more. The build links with
no import libraries at all and uses retail's import table as it stands (see
[`harness.md`](harness.md)), which carries every one of these correctly already.

`DDraw.dll` and `D3DImm.dll` in the install folder are dgVoodoo wrappers, not the Microsoft
originals. Retail runs on them; OpenMCM2 does not load them, since its own renderer answers
`DirectDrawCreateEx`.

`d3drm.dll` is Direct3D Retained Mode, and the game takes one function from it,
`D3DRMVectorRotate`. Windows has not shipped it since XP, and the CD keeps it in its root, outside
the `GAME` folder an install is copied from, so a copy made from the CD has none, and Windows will
not start an executable that imports a DLL it cannot find. The function is ported
(`code/mcm2/d3drm.c`, within 6e-7 of the CD's DLL), and `tools/link.py` leaves `d3drm.dll` out of
the import directory the loader reads. Retail's import table in `.rdata` is unchanged.

## Toolchain present on this machine

| | |
|---|---|
| `ml.exe`, `link.exe`, `lib.exe` | `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x86` |
| Ghidra | `Desktop\Dev Workspace\ghidra_12.1.2_PUBLIC` |
| Python | 3.12 (default) and 3.10 |

Two environment traps, both already paid for on the MM2 port and both still live here: the headless
launcher re-splits arguments on spaces, so every path handed to it must be absolute and space-free;
and Git Bash rewrites `/c`-style flags into `C:/`, so MASM must be driven with dash flags
(`-c -coff`) rather than slash flags.
