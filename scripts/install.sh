#!/usr/bin/env bash
# Install Superbird as a self-contained app.
#
#   ./scripts/install.sh [Preset]            (or: make install)
#
# macOS: builds (unless SUPERBIRD_NO_BUILD=1), installs the runtime into a staging prefix, makes the bundle portable
# and re-signs it (scripts/macos-bundle.py), then replaces $SUPERBIRD_INSTALL_DIR/Superbird.app (default
# /Applications). Quit Superbird first. Linux: installs the runtime into $SUPERBIRD_INSTALL_PREFIX (default ~/.local).
#
# Works with macOS's stock bash 3.2.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="${1:-${BUILD_PRESET:-Release}}"
BUILD_DIR="$ROOT_DIR/upstream/Build/$(printf '%s' "$PRESET" | tr '[:upper:]' '[:lower:]')"

if [[ "${SUPERBIRD_NO_BUILD:-0}" != "1" ]]; then
    "$ROOT_DIR/scripts/build.sh" "$PRESET"
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
    PREFIX="${SUPERBIRD_INSTALL_PREFIX:-$HOME/.local}"
    cmake --install "$BUILD_DIR" --strip --component ladybird_Runtime --prefix "$PREFIX"
    echo "[superbird] Installed into $PREFIX"
    exit 0
fi

DEST_DIR="${SUPERBIRD_INSTALL_DIR:-/Applications}"
STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

echo "[superbird] Staging the runtime..."
cmake --install "$BUILD_DIR" --strip --component ladybird_Runtime --prefix "$STAGING" > "$STAGING/install.log"
APP="$STAGING/bundle/Superbird.app"
python3 "$ROOT_DIR/scripts/macos-bundle.py" "$APP" "$BUILD_DIR" "$ROOT_DIR/upstream/Meta/Entitlements.plist" > "$STAGING/bundle.log" 2>&1 || { cat "$STAGING/bundle.log" >&2; exit 1; }
tail -1 "$STAGING/bundle.log"

if pgrep -f "$DEST_DIR/Superbird.app/Contents/MacOS/Superbird" > /dev/null; then
    echo "Error: Superbird is running from $DEST_DIR; quit it first." >&2
    exit 1
fi
rm -rf "$DEST_DIR/Superbird.app"
ditto "$APP" "$DEST_DIR/Superbird.app"
codesign --verify --deep "$DEST_DIR/Superbird.app"
echo "[superbird] Installed $DEST_DIR/Superbird.app"
