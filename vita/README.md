# Build for PlayStation Vita

The Vita target produces `vita/build/OpenTyrian2000.vpk`. Install it on a
homebrew-capable Vita with VitaShell.

The title ID is `OTYR20000`.

## Requirements

- VitaSDK with SDL2: `vdpm sdl2`
- Native CMake 3.16 or newer
- Ninja

The Windows script searches common CMake and Ninja locations. Set `CMAKE_EXE`
or `NINJA_EXE` to override them. Do not use MSYS CMake with native Ninja; it
passes incompatible POSIX paths.

## Build

PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File vita\build.ps1
powershell -ExecutionPolicy Bypass -File vita\build.ps1 -Clean
```

Git Bash:

```sh
bash vita/build.sh
bash vita/build.sh clean
```

The script builds `eboot.bin`, stages the package, and creates the VPK. Staging
also avoids filenames that do not survive per-file arguments through `cmd.exe`.

## Game data and files

Tyrian data is bundled under `app0:data`. An external copy under
`ux0:data/opentyrian2000` takes priority.

Configuration, saves, and logs also live under `ux0:data/opentyrian2000`.

## LiveArea art

`make_livearea.ps1` regenerates package images from `switch/icon.jpg`.

LiveArea images must be indexed PNGs. True-colour PNGs fail installation with
error `0x8010113D`.

## Controls

| Input | Action |
| --- | --- |
| D-pad or either stick | Move or navigate |
| Cross | Fire or confirm |
| Circle | Rear mode or cancel |
| L / R | Left / right sidekick |
| Start | In-game menu |
| Select | Pause |
| Front touch | Tap menus; drag to steer and fire |

Bindings and touch sensitivity are configurable. Text fields use the system
IME. Rear touch and MIDI are disabled.

Smooth Motion is available, but Sub-pixel rendering is fixed at 1x.

## Online play

Open **Online Multiplayer** from the main menu. LAN discovery and direct address
joining are available on UDP port 1333.

Vita can play with Windows, Linux, and Switch builds of the same game version.
VitaSDK has no SDL2_net package, so `src/vita_net.c` supplies the UDP subset used
by the game.
