#!/usr/bin/env bash
# Build Superbird locally.
#
#   ./scripts/build.sh [Preset] [target]
#
# Preset: Release (default), Debug, Sanitizer, Distribution, ...
# target: defaults to the runnable browser (signed .app bundle on macOS, `ladybird` elsewhere).
#
# Configuration and vcpkg dependency staging are delegated to upstream's own
# Meta/ladybird.py machinery (compiler selection, staged deps, triplets), so this
# script keeps working as upstream evolves. Superbird-specific CMake defaults are
# seeded into the cache before the first configure.
#
# Works with macOS's stock bash 3.2.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"
PRESET="${1:-${BUILD_PRESET:-Release}}"
TARGET="${2:-}"

"$ROOT_DIR/scripts/ensure-applied.sh"

case "$PRESET" in
    All_Debug)       BUILD_SUBDIR="alldebug" ;;
    ThreadSanitizer) BUILD_SUBDIR="tsan" ;;
    *)               BUILD_SUBDIR="$(printf '%s' "$PRESET" | tr '[:upper:]' '[:lower:]')" ;;
esac
BUILD_DIR="$UPSTREAM_DIR/Build/$BUILD_SUBDIR"

if [[ -z "$TARGET" ]]; then
    if [[ "$(uname -s)" == "Darwin" ]]; then
        TARGET="sign_ladybird_bundle"
    else
        TARGET="ladybird"
    fi
fi

# Superbird defaults: no Ladybird update checks/Sparkle (they would point at upstream
# Ladybird's release feed), and no upstream test binaries (keeps iteration fast).
# Override any of these by editing $BUILD_DIR/CMakeCache.txt or `cmake -B $BUILD_DIR -D...`.
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    mkdir -p "$BUILD_DIR"
    cat > "$BUILD_DIR/CMakeCache.txt" <<'EOF'
LADYBIRD_ENABLE_UPDATE_CHECK:BOOL=OFF
LADYBIRD_ENABLE_SPARKLE:BOOL=OFF
BUILD_TESTING:BOOL=OFF
EOF
fi

echo "[superbird] Configuring ($PRESET) and staging vcpkg dependencies..."
python3 - "$UPSTREAM_DIR" "$PRESET" <<'PY'
import os, sys
upstream, preset = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(upstream, "Meta"))
import ladybird
from pathlib import Path
from Utils.host_platform import Platform

# upstream/ is a submodule, whose `.git` *file* ladybird.py mistakes for a git worktree
# (and would then put vcpkg outside upstream/). Pin it to upstream/ itself.
ladybird.find_main_ladybird_source_dir = lambda: Path(upstream).resolve()

platform = Platform()
cc, cxx = platform.default_compiler()
cc = os.environ.get("CC", cc)
cxx = os.environ.get("CXX", cxx)
ladybird.configure_main(platform, preset, cc, cxx, os.environ.get("MAKEJOBS"))
PY

echo "[superbird] Building '$TARGET' ($PRESET)..."
NINJA_ARGS=(-C "$BUILD_DIR")
if [[ -n "${MAKEJOBS:-}" ]]; then
    NINJA_ARGS+=(-j "$MAKEJOBS")
fi
ninja "${NINJA_ARGS[@]}" "$TARGET"

echo "[superbird] Build finished: $BUILD_DIR"
