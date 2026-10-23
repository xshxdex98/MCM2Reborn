# MCM2:Reborn

**Motocross Madness 2** (Rainbow Studios, 2000), rebuilt to run and play well on today's PCs. It has
modern menus, a new race HUD, an OpenGL renderer, online play for 30 riders, lap leaderboards and
proper gamepad support. Gameplay and visual options include the original's behaviour; controllers
use the modern input system.

## Installing

1. Download **MCM2Reborn-Launcher.exe** from the [latest release](https://github.com/xshxdex98/MCM2Reborn/releases/latest).
2. Run it. It looks for a copy of the game on your PC: a retail install, a portable copy, or the
   CD. If you don't have one, pick **DOWNLOAD MOTOCROSS MADNESS 2** and it fetches the game.
3. Type your rider's name and press **INSTALL**. MCM2:Reborn goes in its own folder, with desktop
   and Start menu shortcuts.
4. A short setup sets the picture quality, vsync, your controller and the look. Then press **PLAY**.

The launcher keeps MCM2:Reborn up to date by itself and shows how many riders are online. Your
settings are kept across updates.

### Linux and Steam Deck

MCM2:Reborn runs through Wine or Proton. **GE-Proton** (the latest) is the one it is tested with.

- **Steam** (*Games → Add a Non-Steam Game*): add `MCM2Reborn-Launcher.exe`. In its *Properties*, set
  *Compatibility* to GE-Proton, and the *Launch Options* to `--play`. Start it once to install. From
  then on *Play* updates MCM2:Reborn and goes straight into the game, and Steam sees it running until
  you quit the game.
- **Lutris** (*Add locally installed game*, runner *Wine*): set *Executable* to
  `MCM2Reborn-Launcher.exe` and *Arguments* to `--play`. Use a 64-bit prefix and a Wine with
  wine-mono (Wine-GE or a recent Wine).

Under Wine the launcher also finds a copy of the game in your Linux home folder (`~/Games`,
`~/Downloads`, ...) and a game CD or disc image that Linux has mounted. It makes no desktop
shortcuts there; Steam or Lutris starts the game.

### The videos

The game's videos (intro, credits and the main menu's preview) are Indeo 5, which nothing plays any
more. MCM2:Reborn never plays the originals. If **ffmpeg** is found when installing (or at a later
start of the launcher), it makes an MPEG-1 copy of each (`UI\INTRO.mpg` beside `UI\INTRO.AVI`), and the
game plays those itself, the same on every system. Without ffmpeg the videos are skipped.

The launcher looks for ffmpeg in this order:

- the program `MCM2_FFMPEG` names;
- `ffmpeg.exe` beside the launcher or in the game folder;
- `ffmpeg.exe` on the `PATH`;
- under Wine, the Linux system's own (`/usr/bin/ffmpeg`, `~/.local/bin/ffmpeg`, Homebrew's).

Proton runs in Steam's container, which cannot see the system's ffmpeg. There, put a Windows
`ffmpeg.exe` beside the launcher.

## What's new

**Racing**
- Up to 30 riders on the gate, in single races and online.
- The bike's physics behave the same at any frame rate, and computer riders move smoothly.
- The Pro Circuit's rider gear is in every event.
- Add as many of your own skins as you like: every `.tga` in `RIDERS\` and `BIKES\` is its own choice.
- Track pickers have All, Base Game and Custom filters for every event, in single player and hosted multiplayer.

**Online**
- Lobbies through Epic Online Services, up to 30 riders each, found from the game's Internet screen.
- If you're missing the host's track or another rider's skins, the lobby offers to download them.
- Each rider's ping and a ready count show in the lobby. If the host's game drops, the session ends cleanly.

**Leaderboards**
- Live boards shared by every player, for every track, event (Baja and Enduro by route, Nationals,
  Supercross) and bike class. Custom tracks show by name even if you do not have them.
- Your best lap from the second lap on counts, with one entry per rider. It changes only when you beat it.
  In Nationals and Supercross, more than two seconds off the course voids that lap.
- Open them from the main menu or the multiplayer panel. Laps wait on your PC until the server has
  them (`tools/leaderboard-worker`, a Cloudflare Worker).

**Picture**
- Modern menus and HUD, widescreen at your desktop's resolution.
- The intro, credits and menu videos play again, from copies the launcher makes with ffmpeg (see [The videos](#the-videos)).
- Anti-aliasing, texture filtering, vsync and a frame-rate limit.
- Edge smoothing (FXAA), sharpening, colour vibrance, brightness and bloom.

**Controls**
- Xbox/XInput and native DualShock 4/DualSense gamepads steer by their sticks, with response, sensitivity, smoothing and dead-zone settings.
- Connect or reconnect a pad at startup, in menus or during a race.
- Navigate menus with the stick or D-pad, select with A/Cross, and go back with B/Circle. Start/Options pauses.
- Every action can be bound to any button or trigger: click it, then press the button.
- Alt-tabbing no longer flips the camera or locks the controls.

For the 0.3.8 fixes, automated checks and remaining hardware validation, see [the update notes](docs/release-0.3.8.md).

## Settings

Everything is in the game's **Options**, under REBORN and GAMEPAD. The same settings are kept in
`MCM2Reborn.ini` beside the game. The launcher's **SETUP** button changes
the main ones in one go.

## Building it

MCM2:Reborn is built from the retail executable. The game is disassembled, reassembled and
relinked by its own toolchain, and new code is linked in beside it. So the game runs from the
first build, and any function can be replaced by C and checked against the original.

```sh
sh tools/ghidra/reimport.sh    # build the Ghidra database from Mcm2.exe (once, ~15 minutes)
sh tools/ghidra/cycle.sh       # export -> port -> assemble -> compile -> link -> verify
sh tools/deploy_dev.sh         # copy the build into a game folder
py tools/build_launcher.py     # the launcher
py tools/release.py            # the release package, build/release/
py tools/base_package.py       # the base game's package, from a clean copy
```

- **Unchanged sections:** the rebuilt game's `.text`, `.rdata` and `.data` match retail byte for
  byte, so every pointer in its data stays right.
- **Our code:** it lives in a section of its own after the game's. The link refuses any build in
  which one of the game's sections moved or grew.
- **Ports:** a function marked `PORT(address)` replaces the game's own. `tools/verify.py` accepts no
  difference from retail other than the calls redirected to it.
- **Rendering:** the game gets DirectDraw 7 and Direct3D 7 objects written on OpenGL, so it needs no
  wrapper.
- **The dev build:** MCM2:Reborn's own features are built under `OPENMCM2_DEV`.

The Ghidra workspace lives at `C:\mcm2ghidra\`, outside OneDrive and with no spaces in its path.
OneDrive corrupts Ghidra's project files, and `analyzeHeadless.bat` splits its arguments on spaces.
`MCM2_WORK` puts it elsewhere. `tools/paths.py` lists what else can be set, such as the game folder
(`MCM2_GAME`).

### On Linux

The same MSVC runs under Wine, through [msvc-wine](https://github.com/mstorsjo/msvc-wine). The
Windows build's own tools are used: `ml`, `cl` and `link` from MSVC 14.51. A Linux build of 0.4.0.1
matched the Windows release in every byte of code and data; only the link timestamps and the
linker's tool-version stamp (the Rich header) differed.

The easy way is the build container (podman or docker, `tools/docker`). It has MSVC 14.51, the
Windows SDK, Ghidra 12.1.2, Python and the .NET SDK, on Fedora's Wine:

```sh
mkdir -p ~/mcm2ghidra/bin && cp /path/to/Mcm2.exe ~/mcm2ghidra/bin/   # the retail exe (docs/binary.md)
MSVC_LICENSE=accept sh tools/docker/run.sh sh tools/ghidra/reimport.sh   # makes the image first
sh tools/docker/run.sh                                  # the cycle: export .. link, verify
sh tools/docker/run.sh python3 tests/run_regressions.py # the tests, run under Wine
MCM2_GAME=~/Games/MCM2 sh tools/deploy_dev.sh           # on the host, into a game folder
```

`run.sh` runs whatever command it is given in the container, `cycle.sh` when there is none. The
repository is mounted at `/src` and `~/mcm2ghidra` (`MCM2_WORK`) at `/work`. `MCM2_GAME` and
`MCM2_EOS_SDK` are mounted too when set, for `tools/build_launcher.py`, `tools/build_eos.py` and
`tools/release.py`. Files the build writes belong to you.

The image is made once, in about 15 minutes, and takes about 9 GB. Making it downloads Visual
Studio's build tools from Microsoft, which means accepting
[their license](https://go.microsoft.com/fwlink/?LinkId=2086102). `MSVC_LICENSE=accept` says you do,
and is needed only then.

Without the container, you need:

- Wine. On Fedora, `wine-core`, `wine-common` and `wine-cms`.
- msitools, git and Perl, for msvc-wine.
- Python 3 with capstone, a JDK 21 or newer, and Ghidra 12.1.2.

Unpack MSVC with msvc-wine's `vsdownload.py` and `install.sh`, as `tools/docker/Dockerfile` does. Set
`MSVC_ROOT` to it (`/opt/msvc` by default) and `GHIDRA_INSTALL_DIR` to Ghidra. `reimport.sh` copies
`tools/ghidra/symbols` into Ghidra's own folder, so that needs to be writable. Those files name the
functions Mcm2.exe imports by ordinal alone. On Windows Ghidra takes these names from the system's
DLLs; Linux has none.

| | |
|---|---|
| `code/mcm2/` | what is linked into the game: start-up, the renderer, the menus' code and the ported functions |
| `code/dev/` | MCM2:Reborn's features: HUD, gamepads, leaderboards, cameras, online, settings |
| `code/eos/` | the Epic Online Services helper (64-bit, beside the game) |
| `launcher/` | the launcher (C#, .NET Framework 4.8) |
| `tools/` | the build, the release packages, and the checks against retail |
| `tools/modern/` | the modern menus' layouts and art |
| `tools/docker/` | the Linux build container: MSVC under Wine, Ghidra, Python and .NET |
| `docs/` | measured facts about the game: start with [`binary.md`](docs/binary.md) and [`harness.md`](docs/harness.md) |
