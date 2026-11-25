#!/usr/bin/env bash
# Regenerate Superbird's app icons into overlay/: the master SVGs, the PNGs, the macOS iconset and the Icon Composer
# icon. Needs Swift (Xcode command line tools); macOS's NSImage renders the SVGs.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HERE="$ROOT_DIR/scripts/icons"
OUT="$ROOT_DIR/overlay/UI/Icons/superbird"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

swiftc -O "$HERE/render.swift" -o "$WORK/render"
python3 "$HERE/generate.py" "$ROOT_DIR/upstream/Meta/CMake/freedesktop/org.ladybird.Ladybird.svg" "$WORK/out"

mkdir -p "$OUT/macos/app_icon.iconset" "$OUT/macos/AppIcon.icon/Assets"
cp "$WORK/out/superbird.svg" "$WORK/out/superbird-private.svg" "$OUT/"
cp "$WORK/out/superbird.svg" "$ROOT_DIR/overlay/Meta/CMake/freedesktop/org.superbird.Superbird.svg"
cp "$WORK/out/AppIcon.icon/icon.json" "$OUT/macos/AppIcon.icon/"
cp "$WORK/out/AppIcon.icon/Assets/"*.svg "$OUT/macos/AppIcon.icon/Assets/"

"$WORK/render" "$OUT/superbird.svg" "$OUT/superbird.png" 256
"$WORK/render" "$OUT/superbird-private.svg" "$OUT/superbird-private.png" 256
for size in 16 32 128 256 512; do
    "$WORK/render" "$OUT/superbird.svg" "$OUT/macos/app_icon.iconset/icon_${size}x${size}.png" "$size"
    "$WORK/render" "$OUT/superbird.svg" "$OUT/macos/app_icon.iconset/icon_${size}x${size}@2x.png" "$((size * 2))"
done

echo "[superbird] Icons written to $OUT"
