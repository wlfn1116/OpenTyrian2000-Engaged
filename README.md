# OpenTyrian2000 Engaged

[![build](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml/badge.svg)](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml)

A fork of [OpenTyrian2000](https://github.com/KScl/opentyrian2000), which is an
open-source port of the DOS game Tyrian.

The original campaigns play as they always did. What the fork adds:

- a 356x200 widescreen playfield;
- smooth display-rate rendering, with optional sub-pixel supersampling;
- **Endless mode**, a roguelite run built from the shipped levels;
- a custom weapon editor;
- rollback netcode for online play, with save and resume;
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

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Move |
| Space | Fire |
| Enter | Change rear-weapon mode |
| Ctrl / Alt | Fire left / right sidekick |
| Alt+Enter | Toggle fullscreen |

Controls are rebindable, and controllers are supported.

## Network multiplayer

Host or join from the Multiplayer menu, or start both players from the command
line:

```text
opentyrian2000 --net HOSTNAME --net-player-name NAME --net-player-number NUM
```

`HOSTNAME` is the other player's address and `NUM` is `1` or `2`. The game uses
UDP port 1333.

Netplay uses rollback netcode by default: your ship answers input on the tick it
happens, the same as single player, while the other ship is predicted and
corrected when its real input arrives. The host's choice applies to both
machines. Everything else — the lobby rows, desync recovery, ping, saving and
resuming — is in [GUIDE.md](GUIDE.md#online-play).

Config keys, `enhancements` section:

| Key | Default | Meaning |
| --- | --- | --- |
| `net_rollback` | `on` | Rollback netcode; host-authoritative for the session |
| `net_delay` | `3` | Lockstep only: ticks of input delay |
| `rollback_selftest` | `off` | Debug: verify the rollback snapshot every tick in single player. Runs each tick twice; results in `rollback_selftest.log`. Also a debug-menu row that can arm it mid-level |

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
