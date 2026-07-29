# Nintendo Switch homebrew build

This target builds `opentyrian2000.nro` for the Homebrew Launcher or a Switch
emulator. It is not an eShop release and requires a homebrew-capable system.

## Requirements

Install devkitPro, then install the Switch toolchain and SDL2:

```sh
dkp-pacman -S switch-dev switch-sdl2
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

Text-entry screens use default names when no keyboard is available. Networking
is disabled in this build.

## Troubleshooting

- `Please set DEVKITPRO`: use the devkitPro shell or load
  `/etc/profile.d/devkit-env.sh`.
- `pkg-config` not found: verify
  `$DEVKITPRO/portlibs/switch/bin/aarch64-none-elf-pkg-config`.
- Missing data: check `switch/romfs` or
  `sdmc:/switch/opentyrian2000/`, including filename case.
