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
and is the only icon the app carries. Its artwork has a transparent background
and it holds dark and tinted variants, so the system draws its own material
behind the ship rather than the icon supplying a slab of colour, which is what
the Liquid Glass treatment expects.

Two things are easy to get wrong here, and both were.

Nothing declares `CFBundleIconFiles`. Flat bundle-root PNGs were there as a
fallback while it was still unclear whether `actool` would run without the Xcode
generator; it does. Those PNGs have to be opaque, because that path composites
alpha onto black, so leaving them declared handed iOS a second and slab-backed
answer to the same question.

And a compiled catalog is not enough on its own. `actool` names the icons it
rasterised in the file it writes to `--output-partial-info-plist`, and iOS reads
those keys, not the catalog, to find them. Xcode merges that file into the app's
Info.plist as a matter of course; this build has to do it explicitly, which is
what the `PlistBuddy -c Merge` step after `actool` is for. Skip it and the icon
compiles into `Assets.car` and is then unreachable, so the app shows no icon at
all. CI checks the merged plist for `CFBundlePrimaryIcon` so that cannot pass
unnoticed again.

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

Use Ninja rather than `-G Xcode`. SDL2's CMakeLists runs several hundred
feature probes, and the Xcode generator makes each one a separate `xcodebuild`
process, which stretches configure from about a minute to over ten. Nothing in
this project depends on Xcode-generator behaviour.

## Install

The build is unsigned, so it installs the way any other sideloaded app does:
through AltStore, Sideloadly, TrollStore, or a re-sign against your own
development team. Wrap the bundle in the layout those tools expect:

```sh
rm -rf Payload && mkdir Payload
cp -R ios/build/OpenTyrian2000.app Payload/
zip -qry OpenTyrian2000-Engaged-iOS.ipa Payload
```

Non-Xcode generators do not sign at all, so there is nothing to switch off. To
sign the bundle yourself, run `codesign` over it afterwards with your own
identity and entitlements.

## Frame rate

CoreAnimation caps an app at 60Hz unless its bundle declares
`CADisableMinimumFrameDuration`, so `Info.plist.in` carries that key. Without it
a ProMotion device presents 60 of the frames Smooth Motion draws, and no FPS
Cap setting reaches past that.

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
