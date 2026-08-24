# Build for iOS

The iOS target produces an unsigned ARM64 `.ipa` for sideloading. It is not an
App Store package.

## Requirements

- A Mac with Xcode 15 or newer and the iOS SDK
- CMake 3.21 or newer
- Ninja

## Add SDL

SDL2 is linked as a static library. The four SDL2_net C files are compiled into
the game target.

```sh
mkdir -p ios/external/SDL2 ios/external/SDL2_net
curl -fL -o sdl2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-2.30.11.tar.gz
curl -fL -o sdl2net.tar.gz https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-2.2.0.tar.gz
tar -xzf sdl2.tar.gz --strip-components=1 -C ios/external/SDL2
tar -xzf sdl2net.tar.gz --strip-components=1 -C ios/external/SDL2_net
```

`ios/external` is ignored by Git.

## Add game data

Put the Tyrian 2000 data files in the repository's `data/` directory before
configuring. CMake copies them into `OpenTyrian2000.app/data`.

Run CMake again after changing the data set.

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

Use Ninja. The Xcode generator starts a separate `xcodebuild` for each of SDL's
feature probes and is much slower here.

## Package and install

Sideloading tools expect the app inside a `Payload` directory:

```sh
rm -rf Payload && mkdir Payload
cp -R ios/build/OpenTyrian2000.app Payload/
zip -qry OpenTyrian2000-Engaged-iOS.ipa Payload
```

Install the IPA with AltStore, Sideloadly, TrollStore, or another sideloading
tool. Ninja leaves the bundle unsigned. To sign it yourself, run `codesign`
with your development identity and entitlements.

## Bundle metadata and icons

The displayed name is **Tyrian 2000 Engaged**. `tools/make_app_icons.ps1`
generates the committed `ios/Assets.xcassets` from `visualc/tyrian2000.ico`.
`actool` compiles that catalog during the build. The transparent artwork
includes dark and tinted variants for the system background material.

The post-build step merges `actool`'s partial plist into the bundle plist. Do
not add `CFBundleIconFiles` or bundle-root icons. CI checks the result for
`CFBundlePrimaryIcon`.

After changing `Info.plist.in`, remove `ios/build/OpenTyrian2000.app` before
rebuilding so both the CMake and post-build steps run.

## Frame pacing

`CADisableMinimumFrameDurationOnPhone` allows ProMotion iPhones to present above
60 Hz. iPad Pro needs no opt-in.

SDL's Metal renderer is display-synced on iOS. The in-game VSync setting does
not change that; it only decides whether the FPS cap is applied on top.

## Networking

iOS asks for local-network permission when the game first opens a socket. The
setting is under **Settings > Privacy & Security > Local Network**.
`Info.plist.in` supplies the prompt text.

Broadcast discovery is unavailable because this build has no multicast
entitlement. Use one of these routes:

- Host a game, then join it by address from the other device.
- For transfers, choose **Download > Wait for a sender** on iOS and send to the
  displayed address.

After permission is granted, **Join by IP Address** and **Enter an address...**
also work. Address prompts show Wi-Fi and omit loopback, carrier, and tunnel
addresses.

UDP port 1333 is used for games and 1332 for transfers. A firewall on the other
machine may need to allow them.

## Files and controls

Configuration, saves, and logs live in the app's Application Support directory.
iOS removes the sandbox on uninstall, so export saves first.

Drag anywhere to steer and fire. Pillarbox buttons change with the current
screen. See [Touch controls](../GUIDE.md#touch-controls). MFi and Bluetooth
controllers work through SDL; text fields use the system keyboard.
