# iOS build

This target produces an unsigned `.ipa` for sideloading. It is not an App Store
package, and it needs a Mac with Xcode to build.

## Requirements

- Xcode 15 or newer, with the iOS SDK
- CMake 3.21 or newer
- Ninja

## SDL sources

SDL2 is compiled into the app as a static library, and SDL2_net's four C files
are compiled straight into the game target, so unpack both beside the project:

```sh
mkdir -p ios/external/SDL2 ios/external/SDL2_net
curl -fL -o sdl2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
curl -fL -o sdl2net.tar.gz https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-2.2.0.tar.gz
tar -xzf sdl2.tar.gz --strip-components=1 -C ios/external/SDL2
tar -xzf sdl2net.tar.gz --strip-components=1 -C ios/external/SDL2_net
```

`ios/external` is ignored by Git.

## Game data

Put the Tyrian 2000 data files in the repository's `data/` directory before
configuring. CMake reads that directory at configure time and copies it into
`OpenTyrian2000.app/data`, where the game finds it through `SDL_GetBasePath`.
Re-run CMake after changing the data set.

## Icon and name

The app is named "Tyrian 2000 Engaged". Its icons are generated from
`visualc/tyrian2000.ico` by `tools/make_app_icons.ps1`. The output is
committed, so a build never runs it; re-run it after changing the source icon.

`ios/Assets.xcassets` is compiled into the bundle by `actool` during the build,
and is the only icon source. The transparent artwork includes dark and tinted
variants so iOS can provide the background material.

The post-build step merges `actool`'s partial plist into the bundle plist. iOS
uses those keys to find `Assets.car`; do not add `CFBundleIconFiles` or separate
bundle-root icons. CI checks the merged plist for `CFBundlePrimaryIcon`.

After editing `Info.plist.in`, delete `ios/build/OpenTyrian2000.app` before
rebuilding. CMake rewrites the plist, while the post-build merge only runs when
the target links.

## Build

```sh
cmake -S ios -B ios/build -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENTYRIAN_COMMIT=$(git rev-parse --short HEAD)
cmake --build ios/build --parallel
```

Use Ninja. SDL2 runs several hundred feature probes, and the Xcode generator
starts a separate `xcodebuild` for each one. This project does not require it.

## Install

The build is unsigned, so it installs the way any other sideloaded app does:
through AltStore, Sideloadly, TrollStore, or a re-sign against your own
development team. Wrap the bundle in the layout those tools expect:

```sh
rm -rf Payload && mkdir Payload
cp -R ios/build/OpenTyrian2000.app Payload/
zip -qry OpenTyrian2000-Engaged-iOS.ipa Payload
```

Ninja leaves the bundle unsigned. To sign it yourself, run `codesign` afterwards
with your own identity and entitlements.

## Frame rate

`Info.plist.in` sets `CADisableMinimumFrameDurationOnPhone`, allowing ProMotion
iPhones to present above 60 Hz. The older unsuffixed beta key is ignored. iPad
Pro needs no opt-in.

SDL's Metal renderer always presents display-synced on iOS: `displaySyncEnabled`
is a macOS-only knob, and the iOS path sets `SDL_RENDERER_PRESENTVSYNC` on the
renderer whatever was asked for. The in-game VSync row therefore does not change
how presents are paced here, only whether the FPS Cap is applied on top of them.

## Files

Configuration, saves, and logs live in the app's Application Support directory
inside its sandbox. iOS deletes the container on uninstall, so export a save
before removing the app.

## Controls

Steering is screen-relative: drag anywhere and the ship follows your finger,
which also holds the main weapon down. On-screen buttons in the pillarbox cover
everything else, and change with the screen; see
[Touch controls](../GUIDE.md#touch-controls). MFi and Bluetooth controllers work
through SDL's joystick interface. Text fields raise the system keyboard over an
in-game prompt.
