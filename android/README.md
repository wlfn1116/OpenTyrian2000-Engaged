# Build for Android

The Android target produces a sideloadable APK for ARM64 and ARMv7. It is not a
Play Store package.

## Requirements

- JDK 17
- Android SDK platform 35
- Android build-tools 35.0.0
- NDK r27 (`ndk;27.2.12479018`)
- Gradle 8.12 or newer

The NDK version is pinned in `app/build.gradle`.

## Add SDL

Android builds SDL2 and SDL2_net from source with `ndk-build`. Unpack both under
the app before building:

```sh
jni=android/app/src/main/jni
mkdir -p "$jni/SDL2" "$jni/SDL2_net"
curl -fL -o sdl2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
curl -fL -o sdl2net.tar.gz https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-2.2.0.tar.gz
tar -xzf sdl2.tar.gz --strip-components=1 -C "$jni/SDL2"
tar -xzf sdl2net.tar.gz --strip-components=1 -C "$jni/SDL2_net"
```

Both directories are ignored by Git.

## Add game data

Copy the Tyrian 2000 data files into `app/src/main/assets/data/`, then rebuild
the asset manifest:

```sh
cd android/app/src/main/assets/data
ls -1 > ../filelist.tmp && mv ../filelist.tmp filelist.txt
```

The game cannot enumerate archived APK assets. On first launch it uses
`filelist.txt` to unpack them into app-private `gamedata`. Regenerate the list
whenever the bundled data changes. A later build replaces unpacked files whose
size changed.

## Build

Run from `android`:

```sh
gradle assembleRelease -PopentyrianCommit=$(git rev-parse --short HEAD)
```

The APK lands in `app/build/outputs/apk/release/`.

Release builds use Gradle's debug key. CI keeps that key stable so an update can
install over an older CI build. A different key requires uninstalling the old
app first.

To build for an x86-64 emulator, add `x86_64` to `abiFilters` in
`app/build.gradle`.

## App assets

The displayed name is **Tyrian 2000 Engaged**. Launcher icons are generated
from `visualc/tyrian2000.ico` by `tools/make_app_icons.ps1` and committed under
`app/src/main/res`. The output includes adaptive-icon foregrounds and legacy
bitmaps. Rerun the generator after changing the source icon.

## Files and networking

Configuration, saves, logs, and unpacked game data live under:

```text
/data/data/net.opentyrian.engaged/files
```

Android removes this directory on uninstall. Transfer saves and custom data
before removing the app.

Online play and data transfer work in either direction without a separate
local-network permission. Address prompts show reachable Wi-Fi addresses and
omit loopback, mobile-data, and VPN addresses.

## Controls

Drag anywhere to steer and hold the main weapon. The buttons in the pillarbox
change with the current screen. See [Touch controls](../GUIDE.md#touch-controls).

SDL-compatible gamepads also work. Text fields use the system keyboard.
