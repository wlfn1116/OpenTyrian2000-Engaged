# OpenTyrian2000 Engaged

[![build](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml/badge.svg)](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml)

OpenTyrian2000 Engaged is a fork of
[OpenTyrian2000](https://github.com/KScl/opentyrian2000), an open-source port of
the DOS game Tyrian.

This fork adds:

- a 356x200 widescreen playfield;
- smooth, display-rate rendering with optional sub-pixel supersampling;
- an Endless mode built from the original levels;
- a custom weapon editor;
- expanded health bars, menus, and debug tools;
- Nintendo Switch and PlayStation Vita homebrew builds.
- optional FluidSynth and system MIDI playback (Windows only);

See [GUIDE.md](GUIDE.md) for more information about the additions to this fork.

## Game data

The engine needs the freeware Tyrian 2000 data files:

<https://www.camanis.net/tyrian/tyrian2000.zip>

For a PC build, place the executable beside the `data` directory.

## Main additions

### Widescreen and motion

The playfield is 356x200 instead of 320x200. The HUD remains at the right edge,
while menus keep their original 320-pixel layout and are centred.

The simulation still runs at 35 Hz. Smooth Motion interpolates world rendering
at the display rate. In single-player games, the ship also uses display-rate
movement to reduce input latency. This movement is disabled for demos and
network games.

Sub-pixel rendering draws the playfield internally at up to 5x scale. This makes
slow scrolling smoother without changing the simulation.

### Endless mode

Endless mode selects from the original levels, adds depth-based enemy scaling,
and places an outpost between zones. At each outpost you can:

- buy and upgrade equipment;
- use the Endless Shop;
- choose from several routes with visible risks and rewards;
- take a perk when one is due.

Runs are seeded. The same seed and choices reproduce the same level, shop,
course, and perk sequence. Hardcore runs disable checkpoints.

### Weapons and music

The Custom Weapon Creator stores up to 32 weapons with 11 editable power levels,
an optional rear-gun firing mode, and a live test range. A design can be used as
a front gun, rear gun, or sidekick.

Weapon Tweaks restores selected differences between the Episode 1-3 and Episode
4-5 data.

OPL remains the default music backend. FluidSynth requires a SoundFont (`.sf2`,
`.sf3`, or `.sf`) file. You can also use the system MIDI
synthesizer.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Move |
| Space | Fire |
| Enter | Change rear-weapon mode |
| Ctrl / Alt | Fire left / right sidekick |
| Alt+Enter | Toggle fullscreen |

Controls are rebindable. Controllers are supported.

## Network multiplayer

Both players must start the game from the command line:

```text
opentyrian2000 --net HOSTNAME --net-player-name NAME --net-player-number NUM
```

`HOSTNAME` is the other player's address. `NUM` is `1` or `2`. The game uses UDP
port 1333. 

Network play is inherited from OpenTyrian2000 and is not regularly tested.

## Building

The Windows project is in `visualc`. The root build script can build and collect
the PC, Switch, and Vita targets:

```powershell
.\build-all.ps1
.\build-all.ps1 -Target PC -Clean
.\build-all.ps1 -Target PC,Switch
.\build-all.ps1 -Target PC -Configuration Debug -NoCollect
```

Run `.\build-all.ps1 -Help` for all options. Console builds also require their
platform toolchains:

- [Nintendo Switch build](switch/README.md)
- [PlayStation Vita build](vita/README.md)

On Linux, install the SDL2 development packages and use the root Makefile:

```sh
sudo apt install gcc make pkg-config libsdl2-dev libsdl2-net-dev
make
```

Running a Linux build (including the release tarball) only needs the SDL2
runtime libraries; package names vary by distro:

```sh
sudo apt install libsdl2-2.0-0 libsdl2-net-2.0-0
```

GitHub Actions builds the Windows, Linux, Switch, and Vita targets on every
push. The newest master build is always available from the
[latest pre-release](https://github.com/wlfn1116/OpenTyrian2000-Engaged/releases/tag/latest);
per-commit artifacts are on the
[Actions](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions) tab.
Pushing a tag attaches all four packages to that tag's release automatically.

## License

GNU General Public License, version 2 or later.

## Related projects

- [OpenTyrian2000](https://github.com/KScl/opentyrian2000)
- [OpenTyrian](https://github.com/opentyrian/opentyrian)
