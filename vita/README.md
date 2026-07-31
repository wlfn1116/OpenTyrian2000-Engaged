# PlayStation Vita homebrew build

This target builds `vita/build/OpenTyrian2000.vpk`. It requires a
homebrew-capable Vita and is installed through VitaShell.

The package uses title ID `OTYR20000`. Configuration and saves are stored under
`ux0:data/opentyrian2000`.

## Requirements

- VitaSDK with SDL2: `vdpm sdl2`
- native Windows CMake 3.16 or newer
- `ninja.exe`

The build script finds CMake in common Windows locations. Set `CMAKE_EXE` or
`NINJA_EXE` to override its choices. Do not use the MSYS CMake: it passes POSIX
paths to native Ninja.

## Build

From PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File vita\build.ps1
powershell -ExecutionPolicy Bypass -File vita\build.ps1 -Clean
```

From Git Bash:

```sh
bash vita/build.sh
bash vita/build.sh clean
```

The script builds `eboot.bin`, stages the package tree, and creates the VPK as a
zip. Staging is required because several Tyrian data filenames do not survive
per-file arguments passed through `cmd.exe`.

Tyrian data is copied into `app0:data`. Windows-only files, saves, configuration,
and the large FluidSynth SoundFont are excluded. A copy under
`ux0:data/opentyrian2000` takes precedence.

## LiveArea art

`make_livearea.ps1` regenerates the package images from `switch/icon.jpg`.
LiveArea images must be indexed PNGs; true-colour images fail installation with
`0x8010113D`.

## Install

Copy `OpenTyrian2000.vpk` to the Vita and install it with VitaShell.

## Default controls

| Input | Action |
| --- | --- |
| D-pad or either stick | Move / navigate |
| Cross | Fire / confirm |
| Circle | Rear mode / cancel |
| L / R | Left / right sidekick |
| Start | In-game menu |
| Select | Pause |
| Front touch | Tap menus; drag to steer and fire |

Bindings and touch sensitivity are configurable. Text fields use the system IME.

MIDI is disabled. If performance is poor, reduce Sub-pixel in the Graphics menu;
the port normally forces the inexpensive presentation path.

## Networking

Two-player netplay is available from the Network entry on the main menu. Both
machines must be on the same network; the port is UDP 1333 by default.

Find LAN Games discovers a host on the same subnet, so neither player normally
has to type an address. The host screen shows this console's IP for the cases
where broadcast is blocked and the other player has to join by address.

VitaSDK ships no SDL2_net, so `src/vita_net.c` implements the UDP subset the
netcode uses directly on SceNet. It needs no extra `vdpm` package.
