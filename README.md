# OpenTyrian2000 Engaged

[![build](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml/badge.svg)](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions/workflows/build.yml)

OpenTyrian2000 Engaged is a fork of
[OpenTyrian2000](https://github.com/KScl/opentyrian2000), an open-source port of
the DOS game Tyrian 2000.

This fork adds:

- a wider 16:9 playfield and smooth high-refresh rendering;
- Endless mode built from the original levels and new gameplay effects;
- two-player online play across every mode;
- an in-game remake of the SHIPEDIT.EXE ship editor;
- an in-game custom weapon editor;
- optional restored weapons, effects, sprites, and level objects;
- FluidSynth and native MIDI support on Windows x86-64;
- Android (8.0+), iOS (13.0+), macOS (11.0+), Nintendo Switch and PlayStation Vita ports;
- save and custom-data transfers between platforms.

See the [player guide](GUIDE.md) for menu paths and feature details.

## Showcase

- [Online campaign co-op, Destruct, and high-refresh rendering](https://www.youtube.com/watch?v=6XYol6TJdhE)
- [Quick Endless mode gameplay](https://www.youtube.com/watch?v=uuwwIsWoOMQ)
- [Endless Zone 100 clear](https://www.youtube.com/watch?v=O9BM6xOAqes)

## Game data

Release packages include the freeware Tyrian 2000 data. Source builds need a
copy from [camanis.net](https://www.camanis.net/tyrian/tyrian2000.zip).

On Windows and Linux, the executable should be beside the `data` directory.
The macOS app bundle and the console packages carry the data inside them;
the consoles can also use an external copy. See their build guides.

## Display and controls

The game uses a 356x200 frame: a 299-pixel playfield and the
original 57-pixel HUD. Menus retain their centered 320-pixel layout.

The simulation runs at 35 Hz. Smooth Motion interpolates between ticks,
while Sub-pixel rendering removes whole-pixel stepping from slow movement
and scrolling. Smooth Motion also moves your own ship at the display rate,
rollback network games included. Demos keep fixed-step ship movement.

Default keyboard controls:

| Key | Action |
| --- | --- |
| Arrow keys | Move |
| Space | Fire |
| Enter | Change rear-weapon mode |
| Ctrl / Alt | Fire left / right sidekick |
| Alt+Enter | Toggle fullscreen |

Keys and controllers can be rebound in Setup.

On Android and iOS a drag anywhere steers the ship and also fires; pause and
rear weapon mode get on-screen buttons in the pillarbox beside the playfield.
See [Touch controls](GUIDE.md#touch-controls).

## Online play

Open **Online Multiplayer** from the main menu. A host can be found over LAN
or joined by address. The default port is UDP 1333.

Players can mix Windows, macOS, Linux, Android, iOS, Switch, and Vita builds
when both copies use the same game version. Rollback is the default netcode;
delay-based lockstep remains available from the lobby. Campaign and Endless
sessions can be saved and resumed through their original game type.

For lobby settings, co-op rules, saving, and desync reports, see
[Online play](GUIDE.md#online-play).

## Build

### Windows

The Visual Studio project is under `visualc`. The root script can build
one or more targets and collect their outputs under `build`:

```powershell
.\build-all.ps1
.\build-all.ps1 -Target PC -Clean
.\build-all.ps1 -Target PC,Switch
.\build-all.ps1 -Target PC -Configuration Debug -NoCollect
```

Run `.\build-all.ps1 -Help` for the complete option list.

`-Platform ARM64` builds for Windows on ARM. SDL ships x86 and x64 import
libraries only, so `SDL2BaseDir` and `SDL2netBaseDir` in
`visualc\sdl_paths.props` have to point at SDKs built from source, each
holding an `include` directory and a `lib\arm64` directory.

The `windows-arm` job in `.github/workflows/build.yml` shows how to build
and stage them. ARM64 builds omit the x86-64-only MIDI backends.

### Linux

Install the SDL2 development packages, then use the root Makefile:

```sh
sudo apt install gcc make pkg-config libsdl2-dev libsdl2-net-dev
make
```

Release builds need only the SDL2 runtime packages. Package names vary
by distribution. The same Makefile builds the released aarch64 package.

### macOS

The root Makefile also works here:

```sh
brew install sdl2 sdl2_net pkg-config
make
```

For the universal `.app` bundle that release packages ship, which carries its
own static SDL2 and game data, see [macos/README.md](macos/README.md).

### Consoles and mobile

- [Nintendo Switch](switch/README.md)
- [PlayStation Vita](vita/README.md)
- [Android](android/README.md)
- [iOS](ios/README.md)

## Tests

With the game data in `data/`:

```sh
make test
make sanitize-test
```

The suite covers deterministic replays, rollback state, save migrations,
malformed inputs, Endless generation, and two network peers behind a fault
proxy. [testing/README.md](testing/README.md) lists the runners and scenarios.

GitHub Actions builds Windows (x86-64 and ARM64), Linux (x86-64 and aarch64),
macOS, Android, iOS, Switch, and Vita. The suite runs on both architectures
of both desktop systems.
Current artifacts are available from the [latest pre-release](https://github.com/wlfn1116/OpenTyrian2000-Engaged/releases/tag/latest)
and the [Actions page](https://github.com/wlfn1116/OpenTyrian2000-Engaged/actions).

## License

GNU General Public License, version 3 or later.

Related projects:

- [OpenTyrian2000](https://github.com/KScl/opentyrian2000)
- [OpenTyrian](https://github.com/opentyrian/opentyrian)
