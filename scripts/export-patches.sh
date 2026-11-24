#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"
PATCHES_DIR="$ROOT_DIR/patches"

UPSTREAM_GIT_DIR="$(git -C "$UPSTREAM_DIR" rev-parse --git-dir)"
if [[ "$UPSTREAM_GIT_DIR" != /* ]]; then
    UPSTREAM_GIT_DIR="$UPSTREAM_DIR/$UPSTREAM_GIT_DIR"
fi

BASE_FILE="$UPSTREAM_GIT_DIR/sidelady-base"
if [[ ! -f "$BASE_FILE" ]]; then
    echo "Error: no sidelady-base recorded. Run ./scripts/apply.sh first." >&2
    exit 1
fi

BASE_SHA="$(cat "$BASE_FILE")"
BRANCH="$(git -C "$UPSTREAM_DIR" rev-parse --abbrev-ref HEAD)"
if [[ "$BRANCH" != "sidelady" ]]; then
    echo "Error: upstream/ must be on the 'sidelady' branch (currently: $BRANCH)." >&2
    exit 1
fi

if [[ -n "$(git -C "$UPSTREAM_DIR" status --porcelain)" ]]; then
    echo "Error: upstream/ has uncommitted changes. Commit or stash them before exporting patches." >&2
    git -C "$UPSTREAM_DIR" status --short >&2
    exit 1
fi

mkdir -p "$PATCHES_DIR"
rm -f "$PATCHES_DIR"/*.patch

git -C "$UPSTREAM_DIR" format-patch \
    --zero-commit \
    --no-signature \
    --no-stat \
    -N \
    -o "$PATCHES_DIR" \
    "${BASE_SHA}..HEAD"

echo "[sidelady] Exported patches to $PATCHES_DIR:"
ls -1 "$PATCHES_DIR"/*.patch 2>/dev/null || echo "  (none)"
