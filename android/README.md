# Android build

This target produces a sideloadable APK. It is not a Play Store package.

## Requirements

- JDK 17
- Android SDK, platform 35 and build-tools 35.0.0
- NDK r27 (`ndk;27.2.12479018`, pinned in `app/build.gradle`)
- Gradle 8.12 or newer

## SDL sources

The APK links its own SDL, built from source by `ndk-build`, so unpack the
release tarballs beside the game before building:

```sh
jni=android/app/src/main/jni
mkdir -p "$jni/SDL2" "$jni/SDL2_net"
curl -fL -o sdl2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
curl -fL -o sdl2net.tar.gz https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-2.2.0.tar.gz
tar -xzf sdl2.tar.gz --strip-components=1 -C "$jni/SDL2"
tar -xzf sdl2net.tar.gz --strip-components=1 -C "$jni/SDL2_net"
```

Both directories are ignored by Git.

## Game data

Copy the Tyrian 2000 data files into `app/src/main/assets/data/`, then write the
manifest the game reads on first run:

```sh
cd android/app/src/main/assets/data
ls -1 > ../filelist.tmp && mv ../filelist.tmp filelist.txt
```

The C library cannot open archived APK assets or enumerate their directories.
On first launch the game uses this manifest to unpack them into app-private
`gamedata`. Regenerate the manifest whenever the data set changes. A later build
replaces files whose size changed.

## Icon and name

The app is named "Tyrian 2000 Engaged". Its launcher icons are generated from
`visualc/tyrian2000.ico` by `tools/make_app_icons.ps1`, which writes the
adaptive-icon foregrounds and legacy bitmaps under `app/src/main/res`. The
output is committed, so a build never runs it; re-run it after changing the
source icon.

## Build

From `android`:

```sh
gradle assembleRelease -PopentyrianCommit=$(git rev-parse --short HEAD)
```

The APK lands in `app/build/outputs/apk/release/`. Release builds use Gradle's
debug key. CI caches the key so a new build installs over the previous one;
changing it requires an uninstall.

Only `arm64-v8a` and `armeabi-v7a` are built. Add `x86_64` to `abiFilters` in
`app/build.gradle` for an emulator build.

## Networking

Android needs no separate local-network permission. Online play and save
transfers work in either direction. Waiting screens show the device's reachable
Wi-Fi address and omit loopback, mobile-data, and VPN addresses.

## Files

Configuration, saves, logs, and the unpacked data live in app-private storage
(`/data/data/net.opentyrian.engaged/files`). Android deletes all of it on
uninstall, so export a save before removing the app.

## Controls

Steering is screen-relative: drag anywhere and the ship follows your finger,
which also holds the main weapon down. On-screen buttons in the pillarbox cover
everything else, and change with the screen; see
[Touch controls](../GUIDE.md#touch-controls). Gamepads work through SDL's
joystick interface. Text fields raise the system keyboard over an in-game
prompt.
