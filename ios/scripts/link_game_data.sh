#!/usr/bin/env bash
# PERSONAL TESTING ONLY. Remembers the folder that holds your own Red Alert 2
# files (ios/generated/game_data_path.txt, gitignored); build.sh then bundles
# the files the engine reads into the app (<app>/GameData), so the app starts
# without an import. Never distribute such a build: it contains EA's game data.
#   ios/scripts/link_game_data.sh <RA2 folder>
#   ios/scripts/link_game_data.sh --remove
# A plain text file (not a symlink), so it works the same on Linux, macOS and
# Windows (Git Bash). build.sh --game-data <folder> writes it too, in one step.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FILE="$ROOT/ios/generated/game_data_path.txt"
if [ "${1:-}" = "--remove" ]; then
    rm -f "$FILE"
    echo "forgot the game-data folder (builds no longer bundle game files)"
    exit 0
fi
SRC="${1:?usage: $0 <folder with your ra2.mix ...> | --remove}"
[ -f "$SRC/ra2.mix" ] || [ -f "$SRC/RA2.MIX" ] || { echo "no ra2.mix in $SRC" >&2; exit 1; }
SRC="$(cd "$SRC" && pwd)"
mkdir -p "$(dirname "$FILE")"
printf '%s\n' "$SRC" > "$FILE"
echo "game files for future builds: $SRC"
