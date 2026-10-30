#!/usr/bin/env bash
# Download the pinned SDL2 release (cmake/sdl2_pin.cmake: same version and
# SHA-256 as the desktop and Android builds) into ios/Vendor/SDL2 (gitignored).
# Reuses <repo>/build/_deps/sdl2-src or $RA2_SDL2_SRC_DIR when present.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$ROOT/ios/Vendor/SDL2"
PIN="$ROOT/cmake/sdl2_pin.cmake"
VER=$(sed -n 's/^set(RA2_SDL2_VERSION "\(.*\)")/\1/p' "$PIN")
URL=$(sed -n 's/^set(RA2_SDL2_URL "\(.*\)")/\1/p' "$PIN")
SHA=$(sed -n 's/^set(RA2_SDL2_SHA256 "\(.*\)")/\1/p' "$PIN")
[ -n "$VER" ] && [ -n "$URL" ] && [ -n "$SHA" ] || { echo "cannot parse $PIN" >&2; exit 1; }

# SwiftPM scans the whole target folder for resources: SDL's hidapi test app
# (an English.lproj there makes SwiftPM demand defaultLocalization) must go.
prune() { rm -rf "$DEST/src/hidapi/testgui"; }
if [ -f "$DEST/.ra2-sdl-version" ] && [ "$(cat "$DEST/.ra2-sdl-version")" = "$VER" ]; then
    prune
    echo "SDL2 $VER already in $DEST"; exit 0
fi
rm -rf "$DEST"; mkdir -p "$DEST"
SRC="${RA2_SDL2_SRC_DIR:-}"
[ -z "$SRC" ] && [ -f "$ROOT/build/_deps/sdl2-src/CMakeLists.txt" ] && SRC="$ROOT/build/_deps/sdl2-src"
if [ -n "$SRC" ]; then
    grep -q "SDL_PATCHLEVEL $(echo "$VER" | cut -d. -f3)\$" "$SRC/include/SDL_version.h" ||
        { echo "$SRC is not SDL $VER" >&2; exit 1; }
    echo "copying SDL2 $VER from $SRC"
    cp -a "$SRC/include" "$SRC/src" "$DEST/"
else
    TMP="$(mktemp -d "${XDG_CACHE_HOME:-$HOME/.cache}/ra2-sdl2.XXXXXX")"
    trap 'rm -rf "$TMP"' EXIT
    echo "downloading $URL"
    curl -fL --retry 3 -o "$TMP/sdl2.tar.gz" "$URL"
    echo "$SHA  $TMP/sdl2.tar.gz" | sha256sum -c -
    tar -xzf "$TMP/sdl2.tar.gz" -C "$TMP"
    cp -a "$TMP/SDL2-$VER/include" "$TMP/SDL2-$VER/src" "$DEST/"
fi
# SwiftPM turns a target's public header dir into a Clang module; an
# umbrella-directory module would pull in every SDL header (Windows, EGL, test
# headers...). Expose SDL.h only; the rest is reached textually.
cat > "$DEST/include/module.modulemap" <<'MM'
module SDL2 [system] {
    header "SDL.h"
    export *
}
MM
prune
echo "$VER" > "$DEST/.ra2-sdl-version"
echo "SDL2 $VER ready in $DEST"
