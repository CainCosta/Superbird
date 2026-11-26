#!/usr/bin/env bash
# Regenerates Superbird's runtime uBO-compatible scriptlet & redirect resource bundle
# into the user's profile data directory (never vendored into the repo or binary).
# Compatible with macOS stock bash 3.2.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UBO_TAG="${UBO_TAG:-1.77.2}"

if [ -n "${1:-}" ]; then
    OUT_PATH="$1"
else
    if [ "$(uname -s)" = "Darwin" ]; then
        DATA_DIR="$HOME/Library/Application Support/Superbird"
    else
        DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/Superbird"
    fi
    OUT_PATH="$DATA_DIR/ContentBlocking/Resources/ubo-resources.json"
fi

node "$SCRIPT_DIR/build-ubo-resources.mjs" --tag "$UBO_TAG" --out "$OUT_PATH" --stats
