#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"

UPSTREAM_GIT_DIR="$(git -C "$UPSTREAM_DIR" rev-parse --git-dir)"
if [[ "$UPSTREAM_GIT_DIR" != /* ]]; then
    UPSTREAM_GIT_DIR="$UPSTREAM_DIR/$UPSTREAM_GIT_DIR"
fi

BASE_FILE="$UPSTREAM_GIT_DIR/sidelady-base"
MANIFEST_FILE="$UPSTREAM_GIT_DIR/sidelady-overlay-manifest"
EXCLUDE_FILE="$UPSTREAM_GIT_DIR/info/exclude"

TARGET_SHA="${1:-}"
if [[ -z "$TARGET_SHA" ]]; then
    TARGET_SHA="$(git -C "$ROOT_DIR" ls-tree HEAD upstream 2>/dev/null | awk '{print $3}' || true)"
fi
if [[ -z "$TARGET_SHA" && -f "$BASE_FILE" ]]; then
    TARGET_SHA="$(cat "$BASE_FILE")"
fi
if [[ -z "$TARGET_SHA" ]]; then
    TARGET_SHA="$(git -C "$UPSTREAM_DIR" rev-parse HEAD)"
fi

# Abort any in-progress git am/rebase
if [[ -d "$UPSTREAM_GIT_DIR/rebase-apply" ]]; then
    git -C "$UPSTREAM_DIR" am --abort || true
fi
if [[ -d "$UPSTREAM_GIT_DIR/rebase-merge" ]]; then
    git -C "$UPSTREAM_DIR" rebase --abort || true
fi

# Remove previously linked overlay files and empty directories created for them
if [[ -f "$MANIFEST_FILE" ]]; then
    while IFS= read -r rel; do
        [[ -z "$rel" ]] && continue
        rm -f "$UPSTREAM_DIR/$rel"
        dir="$(dirname "$UPSTREAM_DIR/$rel")"
        while [[ "$dir" != "$UPSTREAM_DIR" && -d "$dir" ]]; do
            rmdir "$dir" 2>/dev/null || break
            dir="$(dirname "$dir")"
        done
    done < "$MANIFEST_FILE"
    rm -f "$MANIFEST_FILE"
fi

# Remove managed exclude block
if [[ -f "$EXCLUDE_FILE" ]]; then
    python3 - "$EXCLUDE_FILE" <<'PY'
import sys
path = sys.argv[1]
with open(path, "r", encoding="utf-8") as f:
    lines = f.readlines()
out = []
skip = False
for line in lines:
    if line.strip() == "# BEGIN SIDELADY OVERLAY":
        skip = True
        continue
    if line.strip() == "# END SIDELADY OVERLAY":
        skip = False
        continue
    if not skip:
        out.append(line)
with open(path, "w", encoding="utf-8") as f:
    f.writelines(out)
PY
fi

git -C "$UPSTREAM_DIR" reset --hard -q
git -C "$UPSTREAM_DIR" checkout --detach -q "$TARGET_SHA"
git -C "$UPSTREAM_DIR" branch -D sidelady >/dev/null 2>&1 || true
