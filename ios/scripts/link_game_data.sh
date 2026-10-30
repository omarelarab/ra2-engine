#!/usr/bin/env bash
# PERSONAL TESTING ONLY. Points ios/GameData (a gitignored symlink) at a folder
# holding your own Red Alert 2 files; build.sh then copies the files the engine
# reads into the app bundle (<app>/GameData), so the app starts without an
# import. Never distribute such a build: it contains EA's game data.
#   ios/scripts/link_game_data.sh <RA2 folder>
#   ios/scripts/link_game_data.sh --remove
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LINK="$ROOT/ios/GameData"
if [ "${1:-}" = "--remove" ]; then
    rm -f "$LINK"; echo "removed $LINK (builds no longer bundle game files)"; exit 0
fi
SRC="${1:?usage: $0 <folder with your ra2.mix ...> | --remove}"
[ -f "$SRC/ra2.mix" ] || [ -f "$SRC/RA2.MIX" ] || { echo "no ra2.mix in $SRC" >&2; exit 1; }
if [ -e "$LINK" ] && [ ! -L "$LINK" ]; then echo "$LINK exists and is not a symlink" >&2; exit 1; fi
ln -sfn "$SRC" "$LINK"
echo "ios/GameData -> $SRC"
