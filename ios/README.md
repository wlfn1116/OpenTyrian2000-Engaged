# iOS build

This target produces an unsigned `.ipa` for sideloading. It is not an App Store
package, and it needs a Mac with Xcode to build.

## Requirements

- Xcode 15 or newer, with the iOS SDK
- CMake 3.21 or newer

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

## Build

```sh
cmake -S ios -B ios/build -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="" \
  -DOPENTYRIAN_COMMIT=$(git rev-parse --short HEAD)
cmake --build ios/build --config Release
```

## Install

The build is unsigned, so it installs the way any other sideloaded app does:
through AltStore, Sideloadly, TrollStore, or a re-sign against your own
development team. Wrap the bundle in the layout those tools expect:

```sh
rm -rf Payload && mkdir Payload
cp -R ios/build/Release-iphoneos/OpenTyrian2000.app Payload/
zip -qry OpenTyrian2000-Engaged-iOS.ipa Payload
```

To sign it yourself instead, drop the three `CODE_SIGN` options and pass
`-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<your team id>`.

## Files

Configuration, saves, and logs live in the app's Application Support directory
inside its sandbox. iOS deletes the container on uninstall, so export a save
before removing the app.

## Controls

Steering is screen-relative: drag anywhere and the ship follows your finger,
which also holds the main weapon down. Two on-screen buttons sit outside the
playfield in the pillarbox, pause at the top left and rear weapon mode at the
top right. MFi and Bluetooth controllers work through SDL's joystick interface.
Text fields raise the system keyboard over an in-game prompt.
