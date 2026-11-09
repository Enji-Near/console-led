#!/usr/bin/env bash
# Build Glorious-Hole's WLED: stock WLED 16.0.1 esp32dev + audioreactive + moving_lines (the build it ran
# before, from Artcade/Glorious-Worm-Hole) + dial_presets. Output: firmware/wled/build/firmware.bin
# Flash over Wi-Fi with tools/ota-glorious-hole.sh (keeps settings and presets).
#   firmware/wled/build.sh
# WLED source: a clean v16.0.1 checkout at $WLED_DIR (default: ../WLED-16.0.1 next to this repo),
# created as a git worktree of $WLED_SRC (the old build checkout) if missing. Needs PlatformIO + Node.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WLED_TAG="v16.0.1"
WLED_DIR="${WLED_DIR:-$REPO/../WLED-16.0.1}"
WLED_SRC="${WLED_SRC:-$REPO/../ZZ-Deprecated/WLED-controller-build}"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"

if [ ! -d "$WLED_DIR" ]; then
  if [ -d "$WLED_SRC/.git" ]; then git -C "$WLED_SRC" worktree add --detach "$WLED_DIR" "$WLED_TAG"
  else git clone --depth 1 --branch "$WLED_TAG" https://github.com/wled/WLED "$WLED_DIR"; fi
  if [ -d "$WLED_SRC/node_modules" ]; then ln -s "$WLED_SRC/node_modules" "$WLED_DIR/node_modules"
  else (cd "$WLED_DIR" && npm ci); fi
fi
[ "$(git -C "$WLED_DIR" describe --tags)" = "$WLED_TAG" ] || { echo "$WLED_DIR is not at $WLED_TAG"; exit 1; }
[ -z "$(git -C "$WLED_DIR" status --porcelain -- wled00)" ] || { echo "$WLED_DIR/wled00 has local changes; want stock"; exit 1; }

cat > "$WLED_DIR/platformio_override.ini" <<INI
; Written by console-led firmware/wled/build.sh
[platformio]
default_envs = glorious_hole

; Stock esp32dev (WLED_RELEASE_NAME "ESP32", so OTA accepts it) + Moving Lines (Glorious-Worm-Hole) + dial_presets.
[env:glorious_hole]
extends = env:esp32dev
custom_usermods = audioreactive $HERE/usermods/moving_lines $HERE/usermods/dial_presets
INI
(cd "$WLED_DIR" && "$PIO" run -e glorious_hole)
mkdir -p "$HERE/build"
cp "$WLED_DIR/.pio/build/glorious_hole/firmware.bin" "$HERE/build/firmware.bin"
echo "Built $HERE/build/firmware.bin"
