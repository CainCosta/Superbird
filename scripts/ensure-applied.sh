#!/usr/bin/env bash
# Make sure upstream/ has the current patch stack and overlay applied.
#
# Re-runs ./scripts/apply.sh when upstream/ isn't on the `superbird` branch, or when
# patches/ (or the pinned upstream commit) changed since the last apply, e.g. after
# `git pull`. It refuses to re-apply over unexported work in upstream/ so local
# patch edits are never silently discarded.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"

if [[ ! -e "$UPSTREAM_DIR/.git" ]]; then
    echo "[superbird] Initializing upstream/ submodule..."
    git -C "$ROOT_DIR" submodule update --init --depth 1
fi

UPSTREAM_GIT_DIR="$(git -C "$UPSTREAM_DIR" rev-parse --absolute-git-dir)"
STAMP_FILE="$UPSTREAM_GIT_DIR/superbird-applied"

current_inputs() {
    {
        git -C "$ROOT_DIR" ls-tree HEAD upstream 2>/dev/null | awk '{print $3}'
        cat "$ROOT_DIR"/patches/*.patch 2>/dev/null
    } | shasum | awk '{print $1}'
}

INPUTS="$(current_inputs)"

write_stamp() {
    printf '%s\n%s\n' "$INPUTS" "$(git -C "$UPSTREAM_DIR" rev-parse HEAD)" > "$STAMP_FILE"
}

# `--mark`: record the current state as applied (called by apply.sh and export-patches.sh).
if [[ "${1:-}" == "--mark" ]]; then
    write_stamp
    exit 0
fi

BRANCH="$(git -C "$UPSTREAM_DIR" rev-parse --abbrev-ref HEAD)"

if [[ "$BRANCH" != "superbird" || ! -f "$STAMP_FILE" ]]; then
    "$ROOT_DIR/scripts/apply.sh"
elif [[ "$(sed -n 1p "$STAMP_FILE")" != "$INPUTS" ]]; then
    APPLIED_HEAD="$(sed -n 2p "$STAMP_FILE")"
    if [[ "$(git -C "$UPSTREAM_DIR" rev-parse HEAD)" != "$APPLIED_HEAD" \
          || -n "$(git -C "$UPSTREAM_DIR" status --porcelain --untracked-files=no)" ]]; then
        echo "[superbird] patches/ changed, but upstream/ has local commits or edits that aren't exported." >&2
        echo "  Export them first:   ./scripts/export-patches.sh" >&2
        echo "  Or discard them:     ./scripts/apply.sh" >&2
        exit 1
    fi
    echo "[superbird] patches/ changed since last apply; re-applying..."
    "$ROOT_DIR/scripts/apply.sh"
fi
