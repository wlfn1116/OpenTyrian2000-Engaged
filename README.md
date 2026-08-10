# OpenTyrian2000 Engaged

[![build](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml/badge.svg)](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml)

A fork of [OpenTyrian2000](https://github.com/KScl/opentyrian2000), which is an
open-source port of the DOS game Tyrian. 

The original campaigns play as they always did. What the fork adds:

- a 356x200 widescreen frame with a wider 299-pixel playfield
- smooth display-rate rendering, with optional sub-pixel supersampling
- **Endless mode**, a roguelite run built from the shipped levels
- **cross-platform online play** with rollback netcode covering every single
  mode in the game (including Destruct)
- content the original game shipped but never used, each behind its own toggle
- a custom weapon editor
- expanded health bars, menus, and debug tools
- optional FluidSynth and system MIDI playback (Windows)
- Nintendo Switch and PlayStation Vita homebrew builds

[GUIDE.md](GUIDE.md) is the player guide for all of it.

## Game data

The release downloads already contain the freeware Tyrian 2000 data, so there is
nothing separate to fetch. The Windows and Linux packages ship a `data` folder
beside the binary, and the Switch and Vita builds carry it inside the `.nro` and
`.vpk`.

Building from source is the exception. Get the data from:

<https://www.camanis.net/tyrian/tyrian2000.zip>

On PC, put the executable beside the `data` directory.

## Highlights

### Widescreen and motion

The frame is 356x200 instead of 320x200. The playfield is 299 pixels wide, with
the 57-pixel HUD still at the right edge. Menus keep their original 320-pixel
layout, centred, with a gradient fadeout.

The simulation still runs at 35 Hz. Smooth Motion interpolates the world at the
display rate, and in single-player the ship moves at the display rate too, which
cuts input latency. Demos and network games keep the fixed-step movement.
Sub-pixel rendering draws the playfield internally at up to 5x, or one sample per
screen pixel on Native. That is what stops slow scrolling from stepping.

### Endless mode

Endless picks from the original levels and scales enemies with depth. Between
zones an outpost lets you shop, take perks, and choose your next route from
several with visible risks and rewards. Sector modifiers supply the rest of the
difficulty and most of the payout.

Runs are seeded, so the same seed and choices reproduce the same levels, shops,
courses and perks. A run mode picked at the start sets how forgiving it is:
Relaxed offers a retry over the wreck, Standard ends the run there, and Hardcore
also disables checkpoints.

### Weapons and music

The Custom Weapon Creator stores up to 32 weapons, each with 11 editable power
levels, an optional rear-gun firing mode, and a live test range. A design can be
equipped as a front gun, rear gun, or sidekick.

Game Tweaks restores differences between the Episode 1-3 and Episode 4-5 weapon
data, and wakes content the original left dormant.

OPL is still the default music backend. FluidSynth needs a SoundFont (`.sf2`,
`.sf3`, or `.sf`) next to the executable or in the `data` folder, and is grayed
out in the menu when there is none. The Windows system MIDI synthesizer also
works.

## Online play

Two-player games over the network, set up entirely in-game: Arcade in three
shapes (linked, separate, or a Timed Battle race), full Campaign and Endless
co-op, SuperTyrian, Super Arcade, and the Destruct mini-game.
**Online Multiplayer** on the main menu opens a lobby with Host Game,
Find LAN Games, and Join by IP Address. On a shared network, LAN discovery finds
the host without anyone typing an address. The game uses UDP port 1333 by
default.

Windows, Linux, Switch and Vita players can all play each other, as long as
everyone is on the same version. Every build runs the same simulation tick for
tick. Saves cross over as well. Both machines keep their own copy of a session,
so a game started against a handheld can be resumed later with the desktop
player hosting.

- **Rollback netcode** by default. Your ship answers input on the tick it
  happens, the same feel as single player, while the other ship is predicted and
  silently corrected when its real input arrives. The original delay-based
  lockstep is still selectable in the host's lobby.
- **Desync recovery.** If the two machines drift apart, the host ships its whole
  game state across and both resume from it.
- **Full Campaign co-op.** Each player flies a complete independent ship through
  the episode, earns and spends their own cash, and can use their own shop at the
  same time between levels. Each machine shows the familiar one-player sidebar
  for its local ship. The hidden ENGAGE mini-games play in co-op too, both ships
  issued the same kit.
- **Endless co-op.** The whole roguelite with two ships: one shared run, sector
  and zone counter, while wallets, shop stock, perks and kill-fire drives belong
  to one player each. A downed ship spectates the rest of the zone and rejoins
  at the next outpost. Co-op runs keep their own 2P records.
- **Save and resume.** Save from the shop mid-session, or when a session drops
  under you. Campaign and Endless saves carry both complete ships and load back
  through the lobby that wrote them; linked Arcade saves keep their local
  two-player compatibility.
- A ping readout in the outpost, player names and cash along the bottom of the
  playfield, Arcade gauge tagging, and a session log for diagnosing desyncs.

The host chooses the game type, episode, difficulty, and network settings,
including whether kills and pickups pay both players in full or each ship earns
its own. Arcade runs as the classic linked Silver Ship and Dragonwing pair, as
two Separate personal ships (the single-player arcade game twice over in one
level, each player with their own HUD, lives, weapons and score), or as a Timed
Battle, where those two ships race one of the three battle levels for cash.
SuperTyrian and Super Arcade fly the two one-player rulesets with a ship each,
and Destruct fights any of its five battle modes across two machines.
The joiner confirms those game settings before entering. Joiners keep their own
local settings, which are restored afterwards. [GUIDE.md](GUIDE.md#online-play)
covers each lobby row, the saving and resuming flow, and what to attach to a
desync report.

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

Code, comment, and documentation conventions are defined in
[doc/STYLE.md](doc/STYLE.md). Formatting tools apply to changed lines only;
vendored code keeps its upstream style. Maintainer invariants and implementation
decisions are recorded in [doc/notes.md](doc/notes.md).

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

## Automated testing

With the freeware game data in `data/`, run the project-owned correctness suite
and the Linux sanitizer build with:

```sh
make test
make sanitize-test
```

The suite covers rollback restore and bounded deterministic demo replays, every
supported Endless save revision, malformed save/resync inputs, seeded course
properties, and two real network peers under injected faults. See
[testing/README.md](testing/README.md) for fixture and runner details.

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
