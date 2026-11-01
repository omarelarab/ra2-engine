# iOS build (iPhone / iPad)

Status: **experimental.** Builds and installs with
[xtool](https://xtool.sh) from **Linux, macOS or Windows**: SwiftPM + the
Darwin SDK, no Mac, no Xcode. This port was developed and built entirely on
Linux; the app was installed on an iPad (iOS 18.5).

The same C++ engine as the desktop and Android builds, on SDL2 2.30.9 (Metal
renderer, CoreAudio, UIKit touch and keyboard).

## No Mac needed

xtool cross-compiles arm64 iOS apps without macOS: you download Xcode's
`.xip` from Apple once (a file, not a Mac) and xtool derives the Darwin SDK
from it, then signs with your Apple ID. A Mac with Xcode would only be needed
to publish on the App Store, which this project is not for (the engine ships
no game data; see Legal in the README).

## Layout

| Path | What |
|---|---|
| `Package.swift` (repo root) | SwiftPM targets: `SDL2` (ios/Vendor/SDL2), `RA2Assets` (src/), `RA2Engine` (engine/, the app product) |
| `xtool.yml` (repo root) | bundle ID `org.ra2.engine`, Info.plist, icon |
| `ios/Info.plist` | landscape, full screen, launch screen, Files-app sharing, local-network prompt |
| `ios/AppIcon.png` | 1024 px, from the Android launcher icon |
| `ios/scripts/build.sh` | the build (below) |
| `ios/scripts/fetch_sdl2.sh` | pinned SDL2 sources into `ios/Vendor/SDL2` (gitignored), same pin as `cmake/sdl2_pin.cmake` |
| `ios/scripts/link_game_data.sh` | personal testing: point the build at your own game files |
| `engine/platform_ios.mm` | Documents folder, bundled-data folder, the in-app importer |
| `engine/ios_main_unit.cpp` | compiles `engine/main.cpp` for SwiftPM (a target holding `main.cpp` would be an executable) |

Engine code uses `#ifdef RA2_IOS` (defined only by `Package.swift`). Most
mobile behaviour is shared with Android (`defined(__ANDROID__) || defined(RA2_IOS)`):
full-screen window, dp-based UI scale and touch slop, the touch command bar
from the first frame, no resolution picker, straight into the main menu.

## One-time setup

Works the same on Linux, macOS and Windows. On Windows, xtool itself runs in
**WSL** (Windows Subsystem for Linux): install WSL once, then everything below
happens inside it, unchanged. A USB device reaches WSL through usbipd-win, or
by relaying from iTunes; both are covered in xtool's Linux guide.

1. **xtool** (1.20+; built with 1.24.0) from <https://xtool.sh>:
   - Linux / WSL: Swift 6.4 from <https://www.swift.org/install/linux>, then
     `sudo apt-get install usbmuxd` (or your distro's package), then the
     AppImage from the [xtool releases](https://github.com/xtool-org/xtool/releases/latest).
   - macOS: `brew install xtool-org/tap/xtool`.
2. **The Darwin iOS SDK**, once: download an `Xcode_*.xip` (26.6 used here)
   from developer.apple.com/download/all **with your own Apple ID** (a free
   one is enough). The .xip is a personal Apple download: users fetch it
   themselves, it is never committed to this repo. Then:
   ```sh
   xtool sdk install ~/Downloads/Xcode_26.6_Universal.xip   # ~7.7G
   xtool auth login        # your Apple ID / team, needed to install
   xtool sdk status        # must say "Darwin SDK is installed"
   ```
   Set `TMPDIR` to a disk folder if `/tmp` is a small RAM disk. The SDK lands
   in `~/.swiftpm/swift-sdks/darwin.artifactbundle`; keep it, it cannot be
   rebuilt without the .xip.
3. **Python 3** (stamps the app version into Info.plist; the `python`
   command counts too).
4. The device: USB data cable, unlock it, tap **Trust This Computer**; turn on
   Developer Mode (Settings > Privacy & Security) on iOS 16+. `xtool devices`
   must list it. A free Apple ID signs for 7 days (then rebuild / reinstall);
   a paid developer account signs for a year.

First time? Run the build with **no arguments**:

```sh
ios/scripts/build.sh
```

It is a small wizard: it checks xtool, the SDK and the login (and helps with
what is missing), asks for your Red Alert 2 folder, and offers to install on
the connected device. Everything it does can also be done by hand, below.

## Build and install

```sh
ios/scripts/build.sh                 # release build -> xtool/RA2Engine.app
ios/scripts/build.sh --install       # ... and install on the connected device
ios/scripts/build.sh --debug         # -O0, slow; for debugging only
ios/scripts/build.sh --no-game-data  # do not bundle game files this time
```

`build.sh` fetches SDL2, writes `ios/generated/` (the `RA2_VERSION` /
`RA2_SIM_BUILD` header and the About-page notices, the values CMake
computes), runs `xtool dev build`, stamps the app version from `VERSION`, and
installs with `xtool install` (which signs for your team).

Personal test build that bundles your own game files and installs in one
command (warning below):

```sh
ios/scripts/build.sh --game-data /path/to/your/RA2 --install
```

Logs: `idevicesyslog -m RA2Engine` (the engine logs through `SDL_Log`).

## Where do the game files go?

The engine reads `Documents/ra2/` (in the Files app: **On My iPhone/iPad >
RA2 Engine > ra2**). Copy **every `.mix` and `.csf` file** from your Red
Alert 2 folder, about 1 GB, and nothing is missing. What they are for:

| File | Needed? | Used for |
|---|---|---|
| `ra2.mix`, `ra2md.mix`, `language.mix`, `multimd.mix` | **required** | rules, art, units, sounds, voices, menus, skirmish maps |
| `langmd.mix`, `ra2.csf` | recommended | Yuri's Revenge names and text |
| `theme.mix`, `thememd.mix` | recommended | music |
| `Maps01.mix`, `MAPS02.MIX`, `mapsmd03.mix` | recommended | campaign maps, default map |
| `expand01.mix`, `ecache01.mix`, `expand02.mix` ... | if present | official patch data |
| `MOVIES01.MIX`, `MOVMD03.MIX`, other `movies*`/`movmd*` | if present | campaign cutscenes |
| `*.bik` | if present | cutscene videos; go into a `Movies/` sub-folder |

Three ways to fill `Documents/ra2/`:

- **Import in the app** (recommended): with files missing, the start page
  offers IMPORT; or **Options > Game Files** at any time. Pick the Red Alert 2
  folder in the Files picker (iCloud Drive, On My iPad, a USB drive, an SMB
  share). The app copies the files above under their canonical names, with
  the same rules as the Android importer (one sub-folder level, `Movies/`
  for `.bik`).
- **Files app / Finder:** copy the files into `RA2 Engine/ra2` directly
  (Finder file sharing on macOS, iTunes / iMazing on Windows), then RETRY.
- **Personal test build only:** bundle them into the app with
  `--game-data` (see above). The remembered folder is
  `ios/generated/game_data_path.txt` (gitignored); `link_game_data.sh`
  writes the same file, `--remove` forgets it. Such a build contains EA's
  game data: keep it on your own devices, never share it.

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
- Not for the App Store: the app needs the player's own copyrighted game files.
- The Simulator is not a target (`xtool` builds `arm64-apple-ios` for devices).
