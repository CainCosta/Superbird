#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"
PRESET="${1:-Release}"
TARGET="${2:-ladybird}"

if [[ "$(git -C "$UPSTREAM_DIR" rev-parse --abbrev-ref HEAD)" != "superbird" ]]; then
    "$ROOT_DIR/scripts/apply.sh"
fi

case "${PRESET,,}" in
    distribution|fuzzers) VCPKG_TYPE="distribution" ;;
    debug|all_debug)      VCPKG_TYPE="debug" ;;
    sanitizer)            VCPKG_TYPE="sanitizer" ;;
    *)                    VCPKG_TYPE="release" ;;
esac

export LADYBIRD_SOURCE_DIR="$UPSTREAM_DIR"
export LADYBIRD_MAIN_SOURCE_DIR="$UPSTREAM_DIR"
export VCPKG_ROOT="$UPSTREAM_DIR/Build/vcpkg-${VCPKG_TYPE}"

echo "[superbird] Ensuring vcpkg (${VCPKG_TYPE}) is bootstrapped..."
(cd "$UPSTREAM_DIR" && python3 -c "import pathlib, sys; sys.path.insert(0, 'Meta'); from Utils.build_vcpkg import build_vcpkg; build_vcpkg(pathlib.Path('$VCPKG_ROOT').resolve())")

echo "[superbird] Configuring Superbird ($PRESET)..."
cmake -S "$UPSTREAM_DIR" \
    --preset "$PRESET" \
    -DLADYBIRD_ENABLE_UPDATE_CHECK=OFF \
    -DLADYBIRD_ENABLE_SPARKLE=OFF

echo "[superbird] Building target '$TARGET' ($PRESET)..."
cmake --build "$UPSTREAM_DIR/Build/${PRESET,,}" --target "$TARGET"
