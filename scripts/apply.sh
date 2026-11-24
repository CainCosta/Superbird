#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"
OVERLAY_DIR="$ROOT_DIR/overlay"
PATCHES_DIR="$ROOT_DIR/patches"

if [[ ! -d "$UPSTREAM_DIR/.git" && ! -f "$UPSTREAM_DIR/.git" ]]; then
    echo "Error: upstream/ submodule is not initialized. Run: git submodule update --init --depth 1" >&2
    exit 1
fi

UPSTREAM_GIT_DIR="$(git -C "$UPSTREAM_DIR" rev-parse --git-dir)"
if [[ "$UPSTREAM_GIT_DIR" != /* ]]; then
    UPSTREAM_GIT_DIR="$UPSTREAM_DIR/$UPSTREAM_GIT_DIR"
fi

BASE_FILE="$UPSTREAM_GIT_DIR/superbird-base"
MANIFEST_FILE="$UPSTREAM_GIT_DIR/superbird-overlay-manifest"
EXCLUDE_FILE="$UPSTREAM_GIT_DIR/info/exclude"

# Determine the pinned submodule commit from the superproject (fallback to current HEAD if untracked yet)
PINNED_SHA="$(git -C "$ROOT_DIR" ls-tree HEAD upstream 2>/dev/null | awk '{print $3}' || true)"
if [[ -z "$PINNED_SHA" ]]; then
    if [[ -f "$BASE_FILE" ]]; then
        PINNED_SHA="$(cat "$BASE_FILE")"
    else
        PINNED_SHA="$(git -C "$UPSTREAM_DIR" rev-parse HEAD)"
    fi
fi

# Allow overriding base commit (used by sync-upstream.sh)
BASE_SHA="${SUPERBIRD_BASE_SHA:-$PINNED_SHA}"

echo "[superbird] Resetting upstream/ to base commit ${BASE_SHA:0:12}..."
"$ROOT_DIR/scripts/reset-upstream.sh" "$BASE_SHA"

echo "$BASE_SHA" > "$BASE_FILE"

# Create/reset local superbird branch at BASE_SHA
git -C "$UPSTREAM_DIR" checkout -q -B superbird "$BASE_SHA"

# Apply patches in sorted order
shopt -s nullglob
PATCHES=("$PATCHES_DIR"/*.patch)
shopt -u nullglob

if [[ ${#PATCHES[@]} -gt 0 ]]; then
    for patch in "${PATCHES[@]}"; do
        echo "[superbird] Applying $(basename "$patch")..."
        if ! git -C "$UPSTREAM_DIR" am --3way --keep-cr "$patch"; then
            echo "" >&2
            echo "[superbird] Patch failed: $(basename "$patch")" >&2
            echo "  Resolve conflicts in $UPSTREAM_DIR, then run:" >&2
            echo "    git -C upstream am --continue" >&2
            echo "    ./scripts/export-patches.sh" >&2
            exit 1
        fi
    done
else
    echo "[superbird] No patches in patches/ yet."
fi

# Link overlay directories/files into upstream/ and register them in upstream/.git/info/exclude
mkdir -p "$(dirname "$EXCLUDE_FILE")"
touch "$EXCLUDE_FILE"

# Strip any previous managed exclude block (including legacy sidelady blocks)
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

: > "$MANIFEST_FILE"
EXCLUDE_ENTRIES=()

if [[ -d "$OVERLAY_DIR" ]]; then
    # Symlink individual files from overlay/ into upstream/ so CMake/AUTOMOC sees real parent dirs
    # while every file stays live-linked to overlay/
    while IFS= read -r -d '' src; do
        rel="${src#"$OVERLAY_DIR"/}"
        dst="$UPSTREAM_DIR/$rel"
        mkdir -p "$(dirname "$dst")"
        rm -f "$dst"
        ln -s "$src" "$dst"
        echo "$rel" >> "$MANIFEST_FILE"
        EXCLUDE_ENTRIES+=("/$rel")
    done < <(find "$OVERLAY_DIR" -type f -print0 | sort -z)
fi

if [[ ${#EXCLUDE_ENTRIES[@]} -gt 0 ]]; then
    {
        echo "# BEGIN SUPERBIRD OVERLAY"
        printf '%s\n' "${EXCLUDE_ENTRIES[@]}"
        echo "# END SUPERBIRD OVERLAY"
    } >> "$EXCLUDE_FILE"
fi

echo "[superbird] Done. Applied ${#PATCHES[@]} patch(es) and linked ${#EXCLUDE_ENTRIES[@]} overlay file(s)."
