# OpenTyrian2000 Engaged

[![build](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml/badge.svg)](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml)

OpenTyrian2000 Engaged is a fork of
[OpenTyrian2000](https://github.com/KScl/opentyrian2000), an open-source port of
the DOS game Tyrian 2000.

This fork adds:

- a wider playfield and smooth high-refresh rendering;
- Endless mode, built from the original levels;
- two-player online Campaign, Arcade, Endless, SuperTyrian, Super Arcade, and
  Destruct;
- a custom weapon editor;
- optional restored weapons, effects, sprites, and level objects;
- FluidSynth and native MIDI support on Windows;
- Nintendo Switch and PlayStation Vita homebrew ports.

See the [player guide](GUIDE.md) for menu paths and feature details.

## Game data

Release packages include the freeware Tyrian 2000 data. Source builds need a
copy from [camanis.net](https://www.camanis.net/tyrian/tyrian2000.zip).

On Windows and Linux, the executable should be next to the `data` directory.
Console packages bundle the data and can also use an external copy; see their
build guides.

## Display and controls

The game uses a 356x200 frame: a 299-pixel playfield and the original 57-pixel
HUD. Menus retain their centered 320-pixel layout.

The simulation runs at 35 Hz. Smooth Motion interpolates between ticks, while
Sub-pixel rendering removes whole-pixel stepping from slow movement and
scrolling. Smooth Motion also moves your own ship at the display rate, rollback
network games included. Demos keep fixed-step ship movement.

Default keyboard controls:

| Key | Action |
| --- | --- |
| Arrow keys | Move |
| Space | Fire |
| Enter | Change rear-weapon mode |
| Ctrl / Alt | Fire left / right sidekick |
| Alt+Enter | Toggle fullscreen |

Keys and controllers can be rebound in Setup.

## Online play

Open **Online Multiplayer** from the main menu. A host can be found over LAN or
joined by address. The default port is UDP 1333.

Players can mix Windows, Linux, Switch, and Vita builds when both copies use the
same game version. Rollback is the default netcode; delay-based lockstep remains
available from the lobby. Campaign and Endless sessions can be saved and resumed
through their original game type.

For lobby settings, co-op rules, saving, and desync reports, see
[Online play](GUIDE.md#online-play).

## Build

### Windows

The Visual Studio project is under `visualc`. The root script can build one or
more targets and collect their outputs under `build`:

```powershell
.\build-all.ps1
.\build-all.ps1 -Target PC -Clean
.\build-all.ps1 -Target PC,Switch
.\build-all.ps1 -Target PC -Configuration Debug -NoCollect
```

Run `.\build-all.ps1 -Help` for the complete option list.

### Linux

Install the SDL2 development packages, then use the root Makefile:

```sh
sudo apt install gcc make pkg-config libsdl2-dev libsdl2-net-dev
make
```

Release builds need only the SDL2 runtime packages. Package names vary by
distribution.

### Consoles

- [Nintendo Switch](switch/README.md)
- [PlayStation Vita](vita/README.md)

## Tests

With the game data in `data/`:

```sh
make test
make sanitize-test
```

The suite covers deterministic replays, rollback state, save migrations,
malformed inputs, Endless generation, and two network peers behind a fault
proxy. [testing/README.md](testing/README.md) lists the runners and scenarios.

GitHub Actions builds Windows, Linux, Switch, and Vita. Current artifacts are
available from the [latest pre-release](https://github.com/wlfn1116/OpenTyrian2000-Engaged/releases/tag/latest)
and the [Actions page](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions).

## Contributing

Read [doc/STYLE.md](doc/STYLE.md) before changing code or documentation. Longer
implementation constraints live in [doc/notes.md](doc/notes.md).

Vendored code under `stuff/` and `src/midiproc/` keeps its upstream style.

## License

GNU General Public License, version 2 or later.

Related projects:

- [OpenTyrian2000](https://github.com/KScl/opentyrian2000)
- [OpenTyrian](https://github.com/opentyrian/opentyrian)
