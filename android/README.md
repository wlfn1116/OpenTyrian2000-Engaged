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

An APK keeps assets inside the archive, where the C library cannot open them, so
the first launch unpacks every listed file into app-private storage, under
`gamedata` rather than `data`. Asset directories cannot be enumerated from C,
which is why the manifest is required. Regenerate it whenever the data set
changes. Files whose length no longer matches the asset are unpacked again, so
an updated data set arrives with the next build.

## Build

From `android`:

```sh
gradle assembleRelease -PopentyrianCommit=$(git rev-parse --short HEAD)
```

The APK lands in `app/build/outputs/apk/release/`. Release builds are signed
with Gradle's debug key: a sideloaded game has no store identity to protect, and
CI caches that key so a new build installs over the previous one. Replacing the
key means uninstalling first.

Only `arm64-v8a` and `armeabi-v7a` are built. Add `x86_64` to `abiFilters` in
`app/build.gradle` for an emulator build.

## Files

Configuration, saves, logs, and the unpacked data live in app-private storage
(`/data/data/net.opentyrian.engaged/files`). Android deletes all of it on
uninstall, so export a save before removing the app.

## Controls

Steering is screen-relative: drag anywhere and the ship follows your finger,
which also holds the main weapon down. Two on-screen buttons sit outside the
playfield in the pillarbox, pause at the top left and rear weapon mode at the
top right. Gamepads work through SDL's joystick interface. Text fields raise the
system keyboard over an in-game prompt.
