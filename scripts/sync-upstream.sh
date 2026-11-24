#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"
TARGET_REF="${1:-origin/master}"

echo "[superbird] Fetching upstream..."
git -C "$UPSTREAM_DIR" fetch --depth 1 origin master

NEW_SHA="$(git -C "$UPSTREAM_DIR" rev-parse "$TARGET_REF")"
echo "[superbird] Testing patches against upstream ${NEW_SHA:0:12} ($TARGET_REF)..."

SUPERBIRD_BASE_SHA="$NEW_SHA" "$ROOT_DIR/scripts/apply.sh"

echo ""
echo "[superbird] All patches applied cleanly on ${NEW_SHA:0:12}!"
echo "Next steps:"
echo "  1. Build and test:  ./scripts/build.sh Release"
echo "  2. If patches needed adjustment, run: ./scripts/export-patches.sh"
echo "  3. To pin the new upstream commit: ./scripts/reset-upstream.sh $NEW_SHA && git -c submodule.upstream.ignore=none add upstream && git commit -m 'Bump upstream Ladybird'"
