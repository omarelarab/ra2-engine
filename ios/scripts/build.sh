#!/usr/bin/env bash
# Build the iOS app with xtool (SwiftPM + the Darwin SDK; no Mac, no Xcode
# build system) and optionally install it on a connected iPhone / iPad.
# Tested on Linux; plain portable bash, so it also runs on macOS and on
# Windows inside WSL (which is how xtool itself runs on Windows).
#
# Run it with NO arguments for a guided setup: it checks xtool, the SDK and
# your login, asks for your Red Alert 2 folder, and offers to install.
#
#   ios/scripts/build.sh [--debug] [--install] [--udid <udid>]
#                        [--game-data <folder>] [--no-game-data]
#
# --game-data <folder> is for personal test builds only: it remembers your
# Red Alert 2 folder (ios/generated/game_data_path.txt, gitignored; the same
# file link_game_data.sh writes) and bundles the game files the engine reads
# into the app, so it starts without an import. Such a build contains EA's
# game data: keep it on your own devices, never share it.
#
# Steps: SDL2 sources (fetch_sdl2.sh) -> ios/generated/ (version header,
# About-page notices) -> `xtool dev build` -> (personal test builds) the
# game files copied into the .app -> `xtool install` (signs with your xtool
# login, see `xtool auth status`).
#
# Release is the default: a -O0 debug build of the engine is too slow to play.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
GD_FILE="$ROOT/ios/generated/game_data_path.txt"   # single source of truth

CONFIG=release INSTALL=0 UDID="" GAME_DATA=1 GAME_DATA_DIR=""
NO_ARGS=0; [ $# -eq 0 ] && NO_ARGS=1
while [ $# -gt 0 ]; do
    case "$1" in
        --debug) CONFIG=debug ;;
        --release) CONFIG=release ;;
        --install) INSTALL=1 ;;
        --udid) UDID="${2:?--udid needs a value}"; shift ;;
        --game-data) GAME_DATA_DIR="${2:?--game-data needs a folder}"; shift ;;
        --no-game-data) GAME_DATA=0 ;;
        -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

# Guided setup (wizard): no arguments and a terminal (RA2_WIZARD=1 forces it,
# so it can be driven with piped answers). Only fills in the variables above.
if [ "$NO_ARGS" = 1 ] && { [ -t 0 ] || [ "${RA2_WIZARD:-}" = "1" ]; }; then
    echo "== RA2 Engine iOS build: guided setup =="
    if ! command -v xtool >/dev/null 2>&1; then
        cat >&2 <<'MSG'
xtool is not installed yet. Install it, then run this script again:

Linux / Windows WSL
  1. Swift toolchain: https://www.swift.org/install/linux
  2. sudo apt-get install usbmuxd        # or your distro's package
  3. curl -fL "https://github.com/xtool-org/xtool/releases/latest/download/xtool-$(uname -m).AppImage" -o xtool
     chmod +x xtool && sudo mv xtool /usr/local/bin/

macOS
  brew install xtool-org/tap/xtool

Full guides (WSL USB setup included):
  https://xtool.sh/documentation/xtooldocs/installation-linux
  https://xtool.sh/documentation/xtooldocs/installation-macos
MSG
        exit 1
    fi
    echo "1/4 xtool: $(command -v xtool)"
    if ! xtool sdk status 2>&1 | grep -qi "is installed"; then
        echo
        echo "2/4 The Darwin iOS SDK is missing. Download Xcode's .xip once"
        echo "    from https://developer.apple.com/download/all/?q=Xcode"
        echo "    (a free Apple ID is enough; ~8 GB download, ~7.7 GB installed)."
        echo "    The .xip is your own Apple download: keep it out of this repo."
        printf '    Full path to the .xip you downloaded: '
        read -r XIP || XIP=""
        XIP="${XIP%\"}"; XIP="${XIP#\"}"     # strip quotes (Copy as path)
        XIP="${XIP//\\//}"                   # accept Windows-style backslashes
        XIP="${XIP/#\~/$HOME}"
        [ -n "$XIP" ] || { echo "no path given; re-run when you have the .xip" >&2; exit 1; }
        [ -f "$XIP" ] || { echo "no such file: $XIP" >&2; exit 1; }
        echo "    extracting the SDK (a few minutes; needs ~8 GB of free space;"
        echo "    if /tmp is a small RAM disk, set TMPDIR to a disk folder first)"
        xtool sdk install "$XIP"
        xtool sdk status 2>&1 | grep -qi "is installed" ||
            { echo "the SDK is still missing; run 'xtool sdk install <Xcode.xip>' manually" >&2; exit 1; }
    fi
    echo "2/4 SDK: $(xtool sdk status 2>&1 | head -n1)"
    AUTH="$(xtool auth status 2>&1 || true)"
    if ! printf '%s' "$AUTH" | grep -qi "^Logged in"; then
        echo
        echo "3/4 xtool is not logged in (needed to sign the app). Run"
        echo "        xtool auth login"
        echo "    in another terminal: choose Password login and your Apple ID"
        echo "    (a free ID signs the app for 7 days)."
        printf '    Press Enter when done: '
        read -r _ || true
        AUTH="$(xtool auth status 2>&1 || true)"
        printf '%s' "$AUTH" | grep -qi "^Logged in" ||
            { echo "still not logged in; run 'xtool auth login', then re-run this script" >&2; exit 1; }
    fi
    echo "3/4 login: $(printf '%s' "$AUTH" | head -n1)"
    echo
    CUR=""
    [ -f "$GD_FILE" ] && CUR="$(tr -d '\r\n' < "$GD_FILE")"
    if [ -n "$CUR" ]; then
        echo "4/4 Game files: $CUR"
        printf '    Bundle them into the app (personal build)? ([Y]/n) '
        read -r ANS || ANS=""
        case "$ANS" in n|N*) GAME_DATA=0 ;; esac
    else
        echo "4/4 The engine needs your Red Alert 2 files. You can copy them"
        echo "    in the app later (IMPORT / the Files app), or bundle them"
        echo "    into this personal build now. Never share such a build."
        printf '    Full path to your Red Alert 2 folder (Enter to skip): '
        read -r P || P=""
        P="${P%\"}"; P="${P#\"}"
        P="${P//\\//}"
        P="${P/#\~/$HOME}"
        if [ -n "$P" ]; then
            if [ -f "$P/ra2.mix" ] || [ -f "$P/RA2.MIX" ]; then
                P="$(cd "$P" && pwd)"
                mkdir -p "$(dirname "$GD_FILE")"
                printf '%s\n' "$P" > "$GD_FILE"
                echo "    remembered: $P"
            else
                echo "    no ra2.mix in $P; not bundling (import in the app instead)" >&2
            fi
        fi
    fi
    echo
    printf 'Install on the connected device after building? ([Y]/n) '
    read -r ANS || ANS=""
    case "$ANS" in n|N*) ;; *) INSTALL=1 ;; esac
    echo
fi

command -v xtool >/dev/null || { echo "xtool not found (https://xtool.sh)" >&2; exit 1; }
if xtool sdk status 2>/dev/null | grep -qi "not installed"; then
    echo "The Darwin iOS SDK is not installed: run 'xtool setup' with an Xcode .xip first." >&2
    exit 1
fi
# Stamps the app version into Info.plist (plistlib is in the standard library).
PY="$(command -v python3 || command -v python || true)"
[ -n "$PY" ] || { echo "Python 3 not found" >&2; exit 1; }

# Personal game-data folder (PERSONAL TESTING ONLY; see --game-data above).
if [ -n "$GAME_DATA_DIR" ]; then
    [ -f "$GAME_DATA_DIR/ra2.mix" ] || [ -f "$GAME_DATA_DIR/RA2.MIX" ] ||
        { echo "no ra2.mix in $GAME_DATA_DIR" >&2; exit 1; }
    GAME_DATA_DIR="$(cd "$GAME_DATA_DIR" && pwd)"
    mkdir -p "$(dirname "$GD_FILE")"
    printf '%s\n' "$GAME_DATA_DIR" > "$GD_FILE"
fi

# 1. SDL2 (pinned, gitignored).
"$ROOT/ios/scripts/fetch_sdl2.sh"

# 2. Generated headers (the values cmake/ra2_sources.cmake computes).
GEN="$ROOT/ios/generated"
mkdir -p "$GEN"
BASE="$(tr -d '[:space:]' < VERSION)"
VERSION="$BASE"
if DESC="$(git -C "$ROOT" describe --always --dirty --abbrev=7 2>/dev/null)" && [ -n "$DESC" ]; then
    VERSION="$BASE+g$DESC"
fi
SIM="$VERSION"
if HEAD="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null)" && [ -n "$HEAD" ]; then SIM="$HEAD"; fi
write_if_changed() {   # keep the mtime when nothing changed (no needless rebuild)
    local dst="$1" tmp="$1.tmp"
    cat > "$tmp"
    if cmp -s "$tmp" "$dst" 2>/dev/null; then rm -f "$tmp"; else mv "$tmp" "$dst"; fi
}
write_if_changed "$GEN/ra2_build_info.h" <<H
// Generated by ios/scripts/build.sh. Do not edit.
#pragma once
#define RA2_VERSION "$VERSION"
#define RA2_SIM_BUILD "$SIM"
H
{
    printf '// Generated by ios/scripts/build.sh (as cmake/ra2_sources.cmake W6-06-16). Do not edit.\n'
    printf 'static const char kRa2ThirdPartyNotices[] = R"RA2NOTICES('
    cat THIRD_PARTY_NOTICES.md
    printf ')RA2NOTICES";\n'
    printf 'static const char kRa2License[] = R"RA2NOTICES('
    cat LICENSE
    printf ')RA2NOTICES";\n'
} | write_if_changed "$GEN/ra2_notices.inc"
echo "version $VERSION (sim build $SIM)"

# 3. Build.
echo "xtool dev build -c $CONFIG"
xtool dev build -c "$CONFIG"
APP="$ROOT/xtool/RA2Engine.app"
[ -d "$APP" ] || { echo "build finished but $APP is missing" >&2; exit 1; }
# The version from VERSION (the Android build's source too); xtool writes 1.0.0.
# Personal name override: ios/local/DisplayName.txt (gitignored), e.g.
# "Red Alert 2" -- the home-screen name of your own build only.
NAME=""
[ -f "$ROOT/ios/local/DisplayName.txt" ] && NAME="$(head -n1 "$ROOT/ios/local/DisplayName.txt" | tr -d '\r')"
"$PY" - "$APP/Info.plist" "$BASE" "$VERSION" "$NAME" <<'PY'
import plistlib, sys
path, base, full, name = sys.argv[1:5]
with open(path, "rb") as f: info = plistlib.load(f)
if name:
    info["CFBundleDisplayName"] = name
    info["CFBundleName"] = name[:15]          # Apple: short name, <= 15 chars
info["CFBundleShortVersionString"] = base   # 0.9.0 (Apple: numbers only)
info["CFBundleVersion"] = base
info["RA2BuildVersion"] = full              # 0.9.0+g<describe>
with open(path, "wb") as f: plistlib.dump(info, f, fmt=plistlib.FMT_XML)
PY

# 4. Personal test data: the folder remembered by --game-data /
#    link_game_data.sh (a plain text file, no symlink, so it works on every
#    OS). The files the engine reads (the importer's rules) are copied into
#    <app>/GameData.
rm -rf "$APP/GameData"
GD_SRC=""
if [ -f "$GD_FILE" ]; then GD_SRC="$(cat "$GD_FILE")"; fi
if [ "$GAME_DATA" = 1 ] && [ -n "$GD_SRC" ]; then
    [ -d "$GD_SRC" ] || { echo "game-data folder '$GD_SRC' not found (link_game_data.sh --remove forgets it)" >&2; exit 1; }
    echo "bundling game files from $GD_SRC (personal testing only)"
    mkdir -p "$APP/GameData"
    n=0
    shopt -s nullglob nocaseglob
    for f in "$GD_SRC"/*.mix "$GD_SRC"/*.csf "$GD_SRC"/*.bik "$GD_SRC"/[Mm]ovies/*.bik; do
        name="$(basename "$f")"
        l="$(printf '%s' "$name" | tr 'A-Z' 'a-z')"   # portable (bash 3.2 on macOS)
        case "$l" in
            ra2.mix|ra2md.mix|language.mix|multimd.mix|langmd.mix|mapsmd03.mix|theme.mix|\
            thememd.mix|expand01.mix|ra2.csf) dst="$l" ;;
            maps01.mix) dst="Maps01.mix" ;;
            maps02.mix) dst="MAPS02.MIX" ;;
            expand[0-9][0-9].mix|ecache[0-9][0-9].mix|elocal[0-9][0-9].mix) dst="$l" ;;
            movies[0-9][0-9].mix|movmd[0-9][0-9].mix) dst="$l" ;;
            *.bik) dst="Movies/$l" ;;
            *) continue ;;
        esac
        # Copied even when empty, like the Android importer; warn, because an
        # empty archive passes the engine's presence check but holds nothing.
        [ -s "$f" ] || echo "  WARNING: $name is an empty file (copied as-is)"
        mkdir -p "$(dirname "$APP/GameData/$dst")"
        cp -L "$f" "$APP/GameData/$dst"
        n=$((n + 1))
    done
    shopt -u nullglob nocaseglob
    echo "  $n file(s), $(du -sh "$APP/GameData" | cut -f1)"
    [ -f "$APP/GameData/ra2.mix" ] || echo "  WARNING: no ra2.mix bundled; the app will ask for an import"
fi
# Personal icon override: ios/local/AppIcon.png (gitignored; e.g. the official
# Red Alert 2 icon taken from your own Ra2.exe) replaces the repo's icon, which
# Info.plist names as CFBundleIconFile=AppIcon.
if [ -f "$ROOT/ios/local/AppIcon.png" ]; then
    cp "$ROOT/ios/local/AppIcon.png" "$APP/AppIcon.png"
    echo "icon: ios/local/AppIcon.png (personal build)"
fi
echo "app: $APP ($(du -sh "$APP" | cut -f1))"

# 5. Install (xtool signs the bundle for your team at install time).
if [ "$INSTALL" = 1 ]; then
    if [ -n "$UDID" ]; then xtool install --udid "$UDID" "$APP"; else xtool install "$APP"; fi
fi
