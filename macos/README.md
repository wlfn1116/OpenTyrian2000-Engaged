# macOS build

This target produces `OpenTyrian2000.app`, a self-contained universal bundle
with the game data and a static SDL2 inside it. It needs a Mac with the Xcode
command line tools.

If you only want to run the game locally and do not care about a bundle, the
root `Makefile` works here as it does on Linux:

```sh
brew install sdl2 sdl2_net pkg-config
make
```

That produces a plain `opentyrian2000` binary that reads `data/` from the
working directory and keeps its files under `~/.config/opentyrian2000`, the same
as the Linux build.

## Requirements

- Xcode 15 or newer, or the command line tools
- CMake 3.21 or newer
- Ninja

## SDL sources

SDL2 is compiled into the app as a static library, and SDL2_net's four C files
are compiled straight into the game target, so unpack both beside the project:

```sh
mkdir -p macos/external/SDL2 macos/external/SDL2_net
curl -fL -o sdl2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
curl -fL -o sdl2net.tar.gz https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-2.2.0.tar.gz
tar -xzf sdl2.tar.gz --strip-components=1 -C macos/external/SDL2
tar -xzf sdl2net.tar.gz --strip-components=1 -C macos/external/SDL2_net
```

`macos/external` is ignored by Git.

## Game data

Put the Tyrian 2000 data files in the repository's `data/` directory before
configuring. CMake reads that directory at configure time and copies it into
`OpenTyrian2000.app/Contents/Resources/data`, where `SDL_GetBasePath` points.
Re-run CMake after changing the data set.

## Icon and name

The app is named "Tyrian 2000 Engaged". `macos/tyrian2000.iconset` is generated
from `visualc/tyrian2000.ico` by `tools/make_app_icons.ps1` and committed, so a
build never runs the generator; re-run it after changing the source icon. The
build packs the set into `tyrian2000.icns` with `iconutil`.

The artwork carries the rounded square of Apple's icon grid itself, an 824-of-
1024 plate, because macOS draws no mask of its own the way iOS and Android do.

## Build

```sh
cmake -S macos -B macos/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENTYRIAN_COMMIT=$(git rev-parse --short HEAD)
cmake --build macos/build --parallel
```

The build is universal by default and takes about twice as long for it. For a
local build you only intend to run yourself, add
`-DCMAKE_OSX_ARCHITECTURES=arm64`.

Use Ninja rather than `-G Xcode`. SDL2's CMakeLists runs several hundred
feature probes, and the Xcode generator makes each one a separate `xcodebuild`
process, which stretches configure from about a minute to over ten. Nothing in
this project depends on Xcode-generator behaviour.

## Signing and Gatekeeper

Copying the data into the bundle after the link invalidates the ad-hoc
signature the linker applies, and Apple Silicon will not run a binary whose
signature does not match, so re-sign the finished bundle:

```sh
codesign --force --sign - macos/build/OpenTyrian2000.app
```

That is enough to launch it locally. It is not notarization: a bundle
downloaded from a release is quarantined, and macOS refuses to open it with a
message about an unidentified developer or a damaged app. Either strip the
quarantine flag:

```sh
xattr -dr com.apple.quarantine /Applications/OpenTyrian2000.app
```

or open it once from the Finder's right-click menu, or allow it under System
Settings > Privacy & Security after the first refusal. Removing that step for
good needs a paid Apple Developer account to sign and notarize with, which this
project does not have; the iOS build is unsigned for the same reason.

## Files

Configuration, saves, and logs live in
`~/Library/Application Support/OpenTyrian/OpenTyrian2000`. The `make` build uses
the Linux location instead, so the two do not share saves.

## Controls

Keyboard, mouse, and any controller SDL recognises, the same as the Windows and
Linux builds. See [Controls](../GUIDE.md#controls).
