# RA2 Engine

An **unofficial, open-source engine for Command & Conquer: Red Alert 2 — Yuri's
Revenge**, for **Windows and Android**. You play the original campaign,
skirmish and multiplayer with the game files from **your own copy** of the
game. No game data is included.

The campaign mission picker (Single Player > New Campaign) is not in the
menu for now; campaign games saved earlier still open from Load Saved Game.

The engine is written from scratch in C++17 with SDL2. Its behaviour is
calibrated against the original game: unit stats come from your own
`rulesmd.ini`, and the rules that are not in the data files (combat, movement,
AI, timing and so on) were worked out by studying how the original game
behaves. The notes in [`docs/re/`](docs/re/) record those findings in our own
words.

> **Status: playable beta (0.9).** The Allied and Soviet campaigns, skirmish
> against the AI and LAN / Internet multiplayer work on desktop and Android.
> It is not yet a 1:1 copy of the original: see [Known limitations](#known-limitations).

## What you need

- **Red Alert 2 with Yuri's Revenge**, installed from a legally owned copy
  (CD, The First Decade, Origin / EA App or Steam). The engine reads the
  Yuri's Revenge data, so a Red Alert 2-only install is not enough.
- **Windows 10 or later**, or **Android 5.0 or later**.

## Install

### 1. Get the game files

Open your Red Alert 2 install folder (the folder that holds `ra2.mix`) and
copy **every `.mix` and `.csf` file** in it, about 1 GB in total. That is the
easy way to make sure nothing is missing. You can leave out the `.exe`, `.dll`
and other files.

What the files are for:

| File | Needed? | Used for |
|---|---|---|
| `ra2.mix`, `ra2md.mix`, `language.mix`, `multimd.mix` | **required** | rules, art, units, sounds, voices, menus, skirmish maps |
| `langmd.mix`, `ra2.csf` | recommended | Yuri's Revenge names and text |
| `theme.mix`, `thememd.mix` | recommended | music (Red Alert 2 and Yuri's Revenge soundtracks) |
| `Maps01.mix`, `MAPS02.MIX`, `mapsmd03.mix` | recommended | Allied and Soviet campaign maps, default map |
| `expand01.mix`, `ecache01.mix`, `expandmd01.mix` … | if present | official patch data |
| `MOVIES01.MIX`, `MOVIES02.MIX`, `MOVMD03.MIX` | if present | campaign cutscenes; some digital versions ship them as tiny placeholder files |

Only the four required files stop the game from starting. Without the
others you lose that part of the game: music, campaigns, cutscenes or patch
rules. If a required file is missing, the game opens a **GAME FILES NEEDED**
page that lists what is missing and where it looked.

### 2a. Windows

1. Download the Windows zip from the [Releases](../../releases) page and unzip
   it anywhere, or [build from source](#build-from-source).
2. Either put the game files in the same folder as `ra2game.exe`, or just start
   `ra2game.exe`. If it cannot find the files, the **GAME FILES NEEDED** page
   has a **BROWSE** button to pick your Red Alert 2 folder, and it remembers
   the choice.

The game also finds a standard retail install from the Windows registry, so on
a PC that already has Red Alert 2 installed it usually just starts.

### 2b. Android

1. Install the APK from the [Releases](../../releases) page.
2. Copy the game files to the phone, for example into `Download/RA2`, using a
   USB cable (file transfer) or any file manager.
3. Open **RA2 Engine** and tap **Import game files**. Pick the `RA2` folder
   and allow access. The app copies the files into its own storage (about a
   minute) and starts the game.

To add or replace files later (music, campaign maps, movies), put them in the
same folder and import again: long-press the app icon, or use **Options →
Game Files** in the game.

The full player guide covers controls (mouse and touch), movies, network play,
saves and troubleshooting: **[docs/INSTALL.md](docs/INSTALL.md)**.

## Multiplayer

- **LAN:** games on the same Wi-Fi or network show up automatically, or join by
  IP address.
- **Internet:** through a small lobby / relay server that you host yourself
  (one binary, no account system). See **[docs/SERVER.md](docs/SERVER.md)**.
- Up to 8 players, AI slots, the original game types, alliances, chat and
  beacons. Every player must run the same engine version and the same game
  files; the lobby checks both.

## Build from source

Requirements: CMake 3.20+, Ninja, a C++17 compiler (MinGW-w64 GCC on Windows),
and Python 3 for the tools. SDL2 2.30.9 is fetched automatically.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
build/ra2game        # the game (ra2engine is the console build)
build/ra2assets_tests  # the unit tests
```

Android APK (JDK 17+, Android SDK and NDK r27c). Configure the desktop build
first, because the Android build reuses the SDL2 source it downloads (or set
`RA2_SDL2_SRC_DIR` to an SDL 2.30.9 source folder):

```sh
cd android
./gradlew assembleDebug
```

iPhone / iPad (experimental), from Linux, macOS or Windows (inside WSL) with
[xtool](https://xtool.sh) (SwiftPM + the Darwin SDK): no Mac, no Xcode. In the
app you import your game files (the same `.mix` / `.csf` files as above)
through the iOS Files picker; setup, the file list and the limitations are in
**[ios/README.md](ios/README.md)**. For a personal test build that bundles
your own files and installs on the device in one command:

```sh
ios/scripts/build.sh --game-data /path/to/your/RA2 --install
```

Developer documentation, including the test and determinism gates, the
`--play` script harness and the architecture notes, is in
**[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)**.

## Known limitations

- Yuri's faction is not playable (Allied and Soviet only); the Yuri's Revenge
  campaign is not included in the campaign menu yet.
- Some details still differ from the original: for example units tilting on
  slopes, the Siege Chopper deploy, the Spy Plane and the random map
  generator.
- Phone-versus-PC multiplayer is switched off by default until it has been
  tested more on real phones.
- Saved games do not carry over between engine versions yet.

## Legal

This is a non-commercial fan project. It contains an engine only and **does
not include or distribute any Command & Conquer: Red Alert 2 / Yuri's
Revenge game data** — no art, sounds, music, movies, maps or rules files. The
engine runs only on game files that the player supplies from their own legally
obtained copy.

"Command & Conquer", "Red Alert" and "Yuri's Revenge" are trademarks of
Electronic Arts Inc. This project is not affiliated with, endorsed by or
sponsored by Electronic Arts. The art, logos, sounds and text shown while
playing come from the player's own files. The notes in `docs/re/` describe how
the original game behaves, in our own words, so that the engine can match it;
they contain no Electronic Arts source code, art, audio or map files.

## License

RA2 Engine is free for everyone, under the **Zero-Clause BSD licence**
(0BSD). The full text is in [LICENSE](LICENSE).

Anyone may use, copy, change and share it, for any purpose, with or without
payment, and with no conditions at all. The software comes as is, without any
warranty.

Author: Omar El Arab

Third-party components keep their own licences; they are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
