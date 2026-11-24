#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"
PRESET="${1:-Release}"
TARGET="${2:-ladybird}"

if [[ "$(git -C "$UPSTREAM_DIR" rev-parse --abbrev-ref HEAD)" != "sidelady" ]]; then
    "$ROOT_DIR/scripts/apply.sh"
fi

echo "[sidelady] Configuring Ladybird ($PRESET)..."
cmake -S "$UPSTREAM_DIR" \
    --preset "$PRESET" \
    -DLADYBIRD_ENABLE_UPDATE_CHECK=OFF \
    -DLADYBIRD_ENABLE_SPARKLE=OFF

echo "[sidelady] Building target '$TARGET' ($PRESET)..."
cmake --build "$UPSTREAM_DIR/Build/${PRESET,,}" --target "$TARGET"
