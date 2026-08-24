# Build for Nintendo Switch

The Switch target produces `opentyrian2000.nro` for the Homebrew Launcher and
Switch emulators. It is not an eShop package.

## Requirements

Install devkitPro and the Switch SDL packages:

```sh
dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_net
```

`DEVKITPRO` must be set. On Windows, use the devkitPro MSYS2 shell.

## Add game data

Use either location:

- `switch/romfs` before building
- `sdmc:/switch/opentyrian2000/` on the SD card

The SD-card copy takes priority. Romfs filenames are case-sensitive.

## Build

From `switch`:

```sh
make
```

Use `make clean` before a full rebuild.

Outside the devkitPro shell, call the helper with devkitPro bash:

```powershell
& "D:\devkitPro\msys2\usr\bin\bash.exe" /path/to/project/switch/build.sh
```

Keep that invocation to one shell command. Some Windows devkitPro installations
truncate multi-command login-shell runs.

## Install and files

Copy the result to:

```text
sdmc:/switch/opentyrian2000/opentyrian2000.nro
```

Configuration, saves, and logs use the same directory. Emulators can open the
NRO directly.

## Controls and text

Joy-Con and Pro Controllers use SDL's joystick interface. Default bindings
cover menus, movement, and firing; change them under **Setup**. The right stick
also moves the ship.

Text fields open the Switch software keyboard. MIDI is disabled.

## Online play

Open **Online Multiplayer** from the main menu. LAN discovery and direct address
joining are available on UDP port 1333.

Switch can play with Windows, Linux, and Vita builds of the same game version.

## Troubleshooting

| Problem | Check |
| --- | --- |
| `Please set DEVKITPRO` | Use the devkitPro shell or source `/etc/profile.d/devkit-env.sh`. |
| `pkg-config` missing | Check `$DEVKITPRO/portlibs/switch/bin/aarch64-none-elf-pkg-config`. |
| `SDL_net.h` missing | Install `switch-sdl2_net`. Remove `-DWITH_NETWORK` from `CFLAGS` only for an offline build. |
| Game data missing | Check both supported locations and filename case. |
