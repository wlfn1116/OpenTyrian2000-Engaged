# Build for macOS

The macOS target produces a universal `OpenTyrian2000.app` containing the game
data and a static SDL2.

For a local command-line build instead, use the root Makefile:

```sh
brew install sdl2 sdl2_net pkg-config
make
```

That build reads `data/` from the working directory and stores files under
`~/.config/opentyrian2000`.

## Requirements

- Xcode 15 or newer, or the Xcode command line tools
- CMake 3.21 or newer
- Ninja

## Add SDL

SDL2 is linked statically. The four SDL2_net C files are compiled into the game
target.

```sh
mkdir -p macos/external/SDL2 macos/external/SDL2_net
curl -fL -o sdl2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
curl -fL -o sdl2net.tar.gz https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-2.2.0.tar.gz
tar -xzf sdl2.tar.gz --strip-components=1 -C macos/external/SDL2
tar -xzf sdl2net.tar.gz --strip-components=1 -C macos/external/SDL2_net
```

`macos/external` is ignored by Git.

## Add game data

Put the Tyrian 2000 data files in the repository's `data/` directory before
configuring. CMake copies them into:

```text
OpenTyrian2000.app/Contents/Resources/data
```

Run CMake again after changing the data set.

## Build

```sh
cmake -S macos -B macos/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENTYRIAN_COMMIT=$(git rev-parse --short HEAD)
cmake --build macos/build --parallel
```

The default build contains x86-64 and ARM64. Add
`-DCMAKE_OSX_ARCHITECTURES=arm64` for a smaller local build.

Use Ninja. The Xcode generator starts a separate `xcodebuild` for each of SDL's
feature probes and is much slower here.

## Icon

`tools/make_app_icons.ps1` generates the committed
`macos/tyrian2000.iconset` from `visualc/tyrian2000.ico`. The build converts it
to `tyrian2000.icns` with `iconutil`.

The artwork uses an 824/1024 rounded-square plate because macOS does not apply
the iOS or Android mask. Rerun the generator after changing the source icon.

## Signing and Gatekeeper

Copying data into the bundle invalidates the linker's ad-hoc signature. Sign the
finished app again for Apple Silicon:

```sh
codesign --force --sign - macos/build/OpenTyrian2000.app
```

Release bundles are not notarized. If macOS quarantines a download, open it
once from Finder's right-click menu, allow it under **System Settings > Privacy
& Security**, or remove the quarantine attribute:

```sh
xattr -dr com.apple.quarantine /Applications/OpenTyrian2000.app
```

## Files and controls

The app bundle stores configuration, saves, and logs under:

```text
~/Library/Application Support/OpenTyrian/OpenTyrian2000
```

The Makefile build uses the Linux location, so the two builds do not share
saves.

Keyboard, mouse, and SDL-compatible controllers work as on Windows and Linux.
