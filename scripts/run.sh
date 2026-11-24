#!/usr/bin/env bash
# Build (incrementally) and launch Superbird.
#
#   ./scripts/run.sh [Preset] [-- browser args...]
#
# Set SUPERBIRD_NO_BUILD=1 to launch the last build without rebuilding.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"

PRESET="${BUILD_PRESET:-Release}"
if [[ $# -gt 0 && "$1" != "--" ]]; then
    PRESET="$1"
    shift
fi
if [[ $# -gt 0 && "$1" == "--" ]]; then
    shift
fi

case "$PRESET" in
    All_Debug)       BUILD_SUBDIR="alldebug" ;;
    ThreadSanitizer) BUILD_SUBDIR="tsan" ;;
    *)               BUILD_SUBDIR="$(printf '%s' "$PRESET" | tr '[:upper:]' '[:lower:]')" ;;
esac
BUILD_DIR="$UPSTREAM_DIR/Build/$BUILD_SUBDIR"

if [[ "${SUPERBIRD_NO_BUILD:-0}" != "1" ]]; then
    "$ROOT_DIR/scripts/build.sh" "$PRESET"
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
    BIN="$BUILD_DIR/bin/Superbird.app/Contents/MacOS/Superbird"
else
    BIN="$BUILD_DIR/bin/Superbird"
fi

if [[ ! -x "$BIN" ]]; then
    echo "Error: $BIN not found. Build first: ./scripts/build.sh $PRESET" >&2
    exit 1
fi

export LADYBIRD_SOURCE_DIR="$UPSTREAM_DIR"
exec "$BIN" "$@"
