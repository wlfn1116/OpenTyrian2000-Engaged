# Nintendo Switch homebrew build

This target builds `opentyrian2000.nro` for the Homebrew Launcher or a Switch
emulator. It is not an eShop release and requires a homebrew-capable system.

## Requirements

Install devkitPro, then install the Switch toolchain, SDL2 and SDL2_net:

```sh
dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_net
```

`DEVKITPRO` must be set. On Windows, use the devkitPro MSYS2 shell.

## Game data

Choose one layout:

- copy the data files into `switch/romfs` before building; or
- copy them to `sdmc:/switch/opentyrian2000/`.

The SD-card copy takes precedence over bundled data. Filenames are
case-sensitive in romfs.

## Build

From the `switch` directory:

```sh
make
make clean
```

`build.sh` sets up the devkitPro paths when called outside its shell:

```powershell
& "D:\devkitPro\msys2\usr\bin\bash.exe" /path/to/project/switch/build.sh
```

Invoke the script directly. A devkitPro login shell can truncate multi-command
runs on some Windows installations.

## Install

Copy the output to:

```text
sdmc:/switch/opentyrian2000/opentyrian2000.nro
```

Configuration and saves are written to the same directory. Emulators can load
the `.nro` directly.

## Controls

Joy-Con and Pro Controllers use SDL's joystick interface. Defaults cover menu
navigation, movement, and firing; all bindings can be changed in game.

Text-entry screens open the Switch software keyboard.

## Networking

Two-player netplay is available from the Network entry on the main menu. Both
machines must be on the same network; the port is UDP 1333 by default.

Find LAN Games discovers a host on the same subnet, so neither player normally
has to type an address. The host screen shows this console's IP for the cases
where broadcast is blocked and the other player has to join by address.

## Troubleshooting

- `Please set DEVKITPRO`: use the devkitPro shell or load
  `/etc/profile.d/devkit-env.sh`.
- `pkg-config` not found: verify
  `$DEVKITPRO/portlibs/switch/bin/aarch64-none-elf-pkg-config`.
- `SDL_net.h: No such file`: install `switch-sdl2_net`, or drop `-DWITH_NETWORK`
  from `CFLAGS` in the Makefile to build without netplay.
- Missing data: check `switch/romfs` or
  `sdmc:/switch/opentyrian2000/`, including filename case.
