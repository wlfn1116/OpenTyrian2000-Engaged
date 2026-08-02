# OpenTyrian2000 Engaged

[![build](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml/badge.svg)](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml)

A fork of [OpenTyrian2000](https://github.com/KScl/opentyrian2000), which is an
open-source port of the DOS game Tyrian.

The original campaigns play as they always did. What the fork adds:

- a 356x200 widescreen playfield;
- smooth display-rate rendering, with optional sub-pixel supersampling;
- **Endless mode**, a roguelite run built from the shipped levels;
- a custom weapon editor;
- **cross-platform online play** with rollback netcode;
- content the original game shipped but never used, each behind its own toggle;
- expanded health bars, menus, and debug tools;
- optional FluidSynth and system MIDI playback (Windows);
- Nintendo Switch and PlayStation Vita homebrew builds.

[GUIDE.md](GUIDE.md) is the player guide for all of it.

## Game data

The engine needs the freeware Tyrian 2000 data files:

<https://www.camanis.net/tyrian/tyrian2000.zip>

On PC, put the executable beside the `data` directory.

## Highlights

### Widescreen and motion

The playfield is 356x200 instead of 320x200, with the HUD still at the right
edge. Menus keep their original 320-pixel layout, centred.

The simulation still runs at 35 Hz. Smooth Motion interpolates the world at the
display rate, and in single-player the ship moves at the display rate too, which
cuts input latency. Demos and network games keep the fixed-step movement.
Sub-pixel rendering draws the playfield internally at up to 5x — or one sample
per screen pixel on Native — which is what makes slow scrolling stop stepping.

### Endless mode

Endless picks from the original levels, scales enemies with depth, and puts an
outpost between zones where you shop, take perks, and choose your next route
from several with visible risks and rewards. Sector modifiers supply the rest of
the difficulty and most of the payout.

Runs are seeded: the same seed and choices reproduce the same levels, shops,
courses and perks. Hardcore runs disable checkpoints.

### Weapons and music

The Custom Weapon Creator stores up to 32 weapons, each with 11 editable power
levels, an optional rear-gun firing mode, and a live test range. A design can be
equipped as a front gun, rear gun, or sidekick.

Game Tweaks restores differences between the Episode 1-3 and Episode 4-5 weapon
data, and wakes content the original left dormant.

OPL is still the default music backend. FluidSynth needs a SoundFont (`.sf2`,
`.sf3`, or `.sf`); the Windows system MIDI synthesizer also works.

## Online play

Two-player arcade over the network, set up entirely in-game: **2 Player Online
Arcade** on the main menu opens a lobby with Host Game, Find LAN Games, and Join
by IP Address. On a shared network, LAN discovery finds the host without anyone
typing an address. The game uses UDP port 1333 by default.

Cross-platform play works, and not just on paper: a Windows player, a Linux
player, someone on a Switch and someone on a Vita can all play each other, as
long as everyone is on the same version. Every build runs the same simulation
tick for tick. Saves cross over too — both machines keep their own copy of a
session, so a game you started against a handheld can be resumed later with the
desktop player hosting.

- **Rollback netcode** by default. Your ship answers input on the tick it
  happens, the same feel as single player, while the other ship is predicted and
  silently corrected when its real input arrives. The original delay-based
  lockstep is still selectable in the host's lobby.
- **Desync recovery.** If the two machines drift apart, the host ships its whole
  game state across and both resume from it, instead of playing out two different
  levels.
- **Save and resume.** Save from the shop mid-session, or when a session drops
  under you. Online games share the regular 2-player save page, so a run can move
  between couch co-op and online.
- A ping readout in the outpost, per-player gauge tagging in the HUD, and a
  session log for diagnosing desyncs.

The host's lobby choices bind the session; joiners keep their own settings, which
are restored afterwards. [GUIDE.md](GUIDE.md#online-play) covers each lobby row,
the saving and resuming flow, and what to attach to a desync report.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Move |
| Space | Fire |
| Enter | Change rear-weapon mode |
| Ctrl / Alt | Fire left / right sidekick |
| Alt+Enter | Toggle fullscreen |

Controls are rebindable, and controllers are supported.

## Building

The Windows project is in `visualc`. The root script builds and collects the PC,
Switch, and Vita targets:

```powershell
.\build-all.ps1
.\build-all.ps1 -Target PC -Clean
.\build-all.ps1 -Target PC,Switch
.\build-all.ps1 -Target PC -Configuration Debug -NoCollect
```

`.\build-all.ps1 -Help` lists every option. The console targets need their own
toolchains:

- [Nintendo Switch build](switch/README.md)
- [PlayStation Vita build](vita/README.md)

On Linux, install the SDL2 development packages and use the root Makefile:

```sh
sudo apt install gcc make pkg-config libsdl2-dev libsdl2-net-dev
make
```

Running a Linux build, including the release tarball, only needs the SDL2
runtime libraries. Package names vary by distro:

```sh
sudo apt install libsdl2-2.0-0 libsdl2-net-2.0-0
```

GitHub Actions builds Windows, Linux, Switch, and Vita on every push. The newest
master build is always at the
[latest pre-release](https://github.com/wlfn1116/OpenTyrian2000-Engaged/releases/tag/latest),
and per-commit artifacts are on the
[Actions](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions) tab.
Pushing a tag attaches all four packages to that tag's release.

## License

GNU General Public License, version 2 or later.

## Related projects

- [OpenTyrian2000](https://github.com/KScl/opentyrian2000)
- [OpenTyrian](https://github.com/opentyrian/opentyrian)
