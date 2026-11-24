#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"

UPSTREAM_GIT_DIR="$(git -C "$UPSTREAM_DIR" rev-parse --git-dir)"
if [[ "$UPSTREAM_GIT_DIR" != /* ]]; then
    UPSTREAM_GIT_DIR="$UPSTREAM_DIR/$UPSTREAM_GIT_DIR"
fi

BASE_FILE="$UPSTREAM_GIT_DIR/superbird-base"
LEGACY_BASE_FILE="$UPSTREAM_GIT_DIR/sidelady-base"
MANIFEST_FILE="$UPSTREAM_GIT_DIR/superbird-overlay-manifest"
LEGACY_MANIFEST_FILE="$UPSTREAM_GIT_DIR/sidelady-overlay-manifest"
EXCLUDE_FILE="$UPSTREAM_GIT_DIR/info/exclude"

TARGET_SHA="${1:-}"
if [[ -z "$TARGET_SHA" ]]; then
    TARGET_SHA="$(git -C "$ROOT_DIR" ls-tree HEAD upstream 2>/dev/null | awk '{print $3}' || true)"
fi
if [[ -z "$TARGET_SHA" && -f "$BASE_FILE" ]]; then
    TARGET_SHA="$(cat "$BASE_FILE")"
fi
if [[ -z "$TARGET_SHA" && -f "$LEGACY_BASE_FILE" ]]; then
    TARGET_SHA="$(cat "$LEGACY_BASE_FILE")"
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
for manifest in "$MANIFEST_FILE" "$LEGACY_MANIFEST_FILE"; do
    if [[ -f "$manifest" ]]; then
        while IFS= read -r rel; do
            [[ -z "$rel" ]] && continue
            rm -f "$UPSTREAM_DIR/$rel"
            dir="$(dirname "$UPSTREAM_DIR/$rel")"
            while [[ "$dir" != "$UPSTREAM_DIR" && -d "$dir" ]]; do
                rmdir "$dir" 2>/dev/null || break
                dir="$(dirname "$dir")"
            done
        done < "$manifest"
        rm -f "$manifest"
    fi
done
rm -f "$LEGACY_BASE_FILE"

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
    stripped = line.strip()
    if stripped in ("# BEGIN SUPERBIRD OVERLAY", "# BEGIN SIDELADY OVERLAY"):
        skip = True
        continue
    if stripped in ("# END SUPERBIRD OVERLAY", "# END SIDELADY OVERLAY"):
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
git -C "$UPSTREAM_DIR" branch -D superbird >/dev/null 2>&1 || true
git -C "$UPSTREAM_DIR" branch -D sidelady >/dev/null 2>&1 || true
