#!/usr/bin/env bash
# One-time setup of a dev machine / fresh clone. Safe to re-run.
#
#   ./scripts/setup.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$(uname -s)" == "Darwin" ]]; then
    if ! xcode-select -p >/dev/null 2>&1; then
        echo "Install the Xcode Command Line Tools first: xcode-select --install" >&2
        exit 1
    fi
    if ! command -v brew >/dev/null; then
        echo "Install Homebrew first: https://brew.sh" >&2
        exit 1
    fi
    echo "[setup] Installing Homebrew build dependencies..."
    HOMEBREW_NO_AUTO_UPDATE=1 brew install \
        autoconf autoconf-archive automake ccache cmake libtool make nasm ninja pkg-config \
        rustup node gh

    RUSTUP_BIN="$(brew --prefix rustup)/bin"
    if ! command -v rustup >/dev/null; then
        export PATH="$RUSTUP_BIN:$HOME/.cargo/bin:$PATH"
        echo "[setup] NOTE: add rustup to your PATH, e.g.:"
        echo "    echo 'export PATH=\"$RUSTUP_BIN:\$HOME/.cargo/bin:\$PATH\"' >> ~/.zshrc"
    fi
else
    echo "[setup] Non-macOS host: install Ladybird's build deps per upstream/Documentation/BuildInstructionsLadybird.md"
fi

echo "[setup] Initializing upstream/ submodule..."
git -C "$ROOT_DIR" submodule update --init --depth 1

echo "[setup] Installing pinned Rust toolchain..."
(cd "$ROOT_DIR/upstream" && rustup toolchain install)

echo "[setup] Enabling repo git hooks (.githooks/)..."
git -C "$ROOT_DIR" config core.hooksPath .githooks

echo "[setup] Applying patches + overlay..."
"$ROOT_DIR/scripts/ensure-applied.sh"

echo ""
echo "[setup] Done. Next: make run   (first build takes 1-2h; vcpkg builds Qt, Skia, ANGLE, ...)"
