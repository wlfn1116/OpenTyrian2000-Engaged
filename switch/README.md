# Nintendo Switch build

This target produces `opentyrian2000.nro` for the Homebrew Launcher and Switch
emulators. It is not an eShop package.

## Requirements

Install devkitPro, then add the Switch toolchain and SDL libraries:

```sh
dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_net
```

`DEVKITPRO` must be set. On Windows, build from the devkitPro MSYS2 shell.

## Game data

Use either location:

- `switch/romfs` before building;
- `sdmc:/switch/opentyrian2000/` on the SD card.

The SD-card copy takes precedence. Romfs filenames are case-sensitive.

## Build

From `switch`:

```sh
make
make clean
```

From outside the devkitPro shell, call the helper with devkitPro bash:

```powershell
& "D:\devkitPro\msys2\usr\bin\bash.exe" /path/to/project/switch/build.sh
```

Invoke the script as one command. Some Windows devkitPro installations truncate
multi-command login-shell runs.

## Install

Copy the result to:

```text
sdmc:/switch/opentyrian2000/opentyrian2000.nro
```

Configuration, saves, and logs use the same directory. Emulators can open the
`.nro` directly.

## Controls and text

Joy-Con and Pro Controllers use SDL's joystick interface. Defaults cover menus,
movement, and firing; bindings can be changed in Setup.

Text fields open the Switch software keyboard. The right stick also moves the
ship. MIDI is disabled.

## Online play

Open **Online Multiplayer** from the main menu. LAN discovery and direct address
joining are available. The default port is UDP 1333.

Switch can play with Windows, Linux, and Vita builds of the same game version.

## Troubleshooting

- `Please set DEVKITPRO`: use the devkitPro shell or source
  `/etc/profile.d/devkit-env.sh`.
- `pkg-config` missing: check
  `$DEVKITPRO/portlibs/switch/bin/aarch64-none-elf-pkg-config`.
- `SDL_net.h` missing: install `switch-sdl2_net`. To build without online play,
  remove `-DWITH_NETWORK` from `CFLAGS`.
- Missing game data: check both supported locations and filename case.
