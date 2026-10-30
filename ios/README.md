# iOS build (iPhone / iPad)

Status: **experimental. Builds and links** (2026-10-28: xtool 1.24.0, Xcode
26.6 -> iOS 26.5 SDK, arm64 app); **not yet run on a device.**

The same C++ engine as the desktop and Android builds, on SDL2 2.30.9 (Metal
renderer, CoreAudio, UIKit touch and keyboard), packaged with
[xtool](https://xtool.sh) on Linux: SwiftPM + the Darwin SDK, no Mac and no
Xcode build system.

## Layout

| Path | What |
|---|---|
| `Package.swift` (repo root) | SwiftPM targets: `SDL2` (ios/Vendor/SDL2), `RA2Assets` (src/), `RA2Engine` (engine/, the app product) |
| `xtool.yml` (repo root) | bundle ID `org.ra2.engine`, Info.plist, icon |
| `ios/Info.plist` | landscape, full screen, launch screen, Files-app sharing, local-network prompt |
| `ios/AppIcon.png` | 1024 px, from the Android launcher icon |
| `ios/scripts/build.sh` | the build (below) |
| `ios/scripts/fetch_sdl2.sh` | pinned SDL2 sources into `ios/Vendor/SDL2` (gitignored), same pin as `cmake/sdl2_pin.cmake` |
| `ios/scripts/link_game_data.sh` | personal testing: bundle your own game files into the app |
| `engine/platform_ios.mm` | Documents folder, bundled-data folder, the in-app importer |
| `engine/ios_main_unit.cpp` | compiles `engine/main.cpp` for SwiftPM (a target holding `main.cpp` would be an executable) |

Engine code uses `#ifdef RA2_IOS` (defined only by `Package.swift`). Most
mobile behaviour is shared with Android (`defined(__ANDROID__) || defined(RA2_IOS)`):
full-screen window, dp-based UI scale and touch slop, the touch command bar
from the first frame, no resolution picker, straight into the main menu.

## One-time setup

1. xtool (1.20+; built with 1.24.0) and its iOS SDK. Download Xcode
   (`Xcode_*.xip`; 26.6 used here, the newest that also runs in an Intel macOS
   VM; Xcode 27 needs Apple silicon and Swift 6.4) from
   developer.apple.com/download/all, then:
   ```sh
   xtool sdk install ~/xcode/Xcode_26.6_Universal.xip   # builds the Darwin SDK (~7.7G)
   xtool auth login                                     # Apple ID / team, needed to install
   xtool sdk status                                     # must not say "Not installed"
   ```
   Set `TMPDIR` to a disk folder if `/tmp` is a small RAM disk.
   The SDK lands in `~/.swiftpm/swift-sdks/darwin.artifactbundle`. Do not
   delete it in disk clean-ups; it cannot be rebuilt without the .xip.
2. The device: USB data cable, unlock it, tap **Trust This Computer**; turn on
   Developer Mode (Settings > Privacy & Security) on iOS 16+. `xtool devices`
   must list it.

## Build and install

```sh
ios/scripts/build.sh                 # release build -> xtool/RA2Engine.app
ios/scripts/build.sh --install       # ... and install on the connected device
ios/scripts/build.sh --debug         # -O0, slow; for debugging only
ios/scripts/build.sh --no-game-data  # ignore ios/GameData for this build
```

`build.sh` fetches SDL2, writes `ios/generated/` (the `RA2_VERSION` /
`RA2_SIM_BUILD` header and the About-page notices, the values CMake
computes), runs `xtool dev build`, optionally copies the bundled game files
into the app, and installs with `xtool install` (which signs for your team).

Logs: `idevicesyslog -m RA2Engine` (the engine logs through `SDL_Log`).

## Game files

The engine reads `Documents/ra2/` (in the Files app: **On My iPhone/iPad >
RA2 Engine > ra2**). Three ways to fill it:

- **Import in the app:** with files missing, the start page offers IMPORT; or
  **Options > Game Files** at any time. Pick the Red Alert 2 folder in the
  Files picker (iCloud Drive, On My iPad, a USB drive, an SMB share). The app
  copies the files it reads under their canonical names, with the same rules
  as the Android importer (one sub-folder level, `Movies/` for `.bik`).
- **Files app / Finder:** copy the files into `RA2 Engine/ra2` directly
  (Finder file sharing on macOS, iTunes / iMazing on Windows), then RETRY.
- **Personal test build only:** bundle them into the app:
  ```sh
  ios/scripts/link_game_data.sh /path/to/your/RA2   # ios/GameData -> that folder
  ios/scripts/build.sh --install
  ios/scripts/link_game_data.sh --remove            # stop bundling
  ```
  `ios/GameData` is a gitignored symlink. The build copies only the files the
  engine reads (about 1 GB for a full install) into `<app>/GameData`, which the
  engine uses while `Documents/ra2/` has no `ra2.mix`. Such a build contains
  EA's game data: keep it on your own devices, never share it.

  CnCNet installs ship some archives (for example `multimd.mix`) as **0-byte
  placeholders**. They are copied as-is (as the Android importer does) and the
  build prints a warning; the maps those archives hold are then missing.

  Personal overrides (gitignored `ios/local/`): `AppIcon.png` replaces the app
  icon and `DisplayName.txt` the home-screen name of your own build.

## Limitations

- **LAN game discovery** uses UDP broadcast, which iOS only allows with the
  `com.apple.developer.networking.multicast` entitlement (Apple grants it on
  request). Without it, join a LAN game by typing the host's address, or play
  through the Internet lobby server.
- Multiplayer platform tag `4` ("iOS"): mixed-platform lobbies need the
  host's **Cross-platform (experimental)** option, as for Android.
- A free Apple ID signs for 7 days (then rebuild / reinstall); a paid
  developer account signs for a year.
- Not for the App Store: the app needs the player's own copyrighted game files.
- The Simulator is not a target (`xtool` builds `arm64-apple-ios` for devices).
