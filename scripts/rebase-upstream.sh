#!/usr/bin/env bash
# Deterministic helper for rebasing Superbird's patch stack onto a newer upstream Ladybird commit.
#
# Subcommands:
#   worktree [branch-name] [worktree-dir]
#       Create an isolated git worktree off origin/main (or main) and initialize its
#       upstream/ submodule, reusing local git objects when available.
#
#   prepare [target-ref]
#       Fetch enough upstream history to connect the pinned commit to target-ref
#       (default: origin/master), print a risk/delta summary, remove overlay symlinks,
#       and run `git rebase --onto <target> <old-pin> superbird`.
#       Exits 0 if clean (overlay re-linked automatically), or 2 if conflicts need resolution.
#
#   post-rebase
#       After resolving rebase conflicts and finishing `git -C upstream rebase --continue`,
#       records the new superbird-base, re-links overlay/ (failing on tracked collisions),
#       and updates the applied stamp.
#
#   summary
#       Re-print the OLD..NEW upstream delta & risk summary.
#
#   finalize
#       Export patches/, reset upstream/ to NEW_SHA, stage the submodule pin, and run a
#       cold re-apply + unit-test round-trip check.
#
# Works with macOS's stock bash 3.2.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"

usage() {
    cat <<'EOF'
Usage: ./scripts/rebase-upstream.sh <command> [args...]

Commands:
  worktree [branch] [dir]   Create a linked git worktree off origin/main with upstream/ initialized
  prepare [target-ref]      Fetch upstream history, print delta summary, and rebase patch stack
  post-rebase               Finalize metadata and re-link overlay/ after manual conflict resolution
  summary                   Print the upstream delta/risk summary for the current rebase
  finalize                  Export patches, pin upstream/, and verify cold re-apply + unit tests
EOF
}

resolve_upstream_git_dir() {
    if [[ ! -d "$UPSTREAM_DIR/.git" && ! -f "$UPSTREAM_DIR/.git" ]]; then
        echo "Error: upstream/ submodule is not initialized." >&2
        exit 1
    fi
    local dir
    dir="$(git -C "$UPSTREAM_DIR" rev-parse --git-dir)"
    if [[ "$dir" != /* ]]; then
        dir="$UPSTREAM_DIR/$dir"
    fi
    printf '%s\n' "$dir"
}

remove_overlay_links() {
    local git_dir="$1"
    local manifest="$git_dir/superbird-overlay-manifest"
    if [[ -f "$manifest" ]]; then
        while IFS= read -r rel; do
            [[ -z "$rel" ]] && continue
            if [[ -L "$UPSTREAM_DIR/$rel" ]]; then
                rm -f "$UPSTREAM_DIR/$rel"
                local dir
                dir="$(dirname "$UPSTREAM_DIR/$rel")"
                while [[ "$dir" != "$UPSTREAM_DIR" && -d "$dir" ]]; do
                    rmdir "$dir" 2>/dev/null || break
                    dir="$(dirname "$dir")"
                done
            fi
        done < "$manifest"
    fi
}

print_summary() {
    local old_sha="$1"
    local new_sha="$2"
    local count
    count="$(git -C "$UPSTREAM_DIR" rev-list --count "${old_sha}..${new_sha}" 2>/dev/null || echo "?")"
    local old_date new_date
    old_date="$(git -C "$UPSTREAM_DIR" show -s --format='%ci' "$old_sha" 2>/dev/null || echo "unknown")"
    new_date="$(git -C "$UPSTREAM_DIR" show -s --format='%ci' "$new_sha" 2>/dev/null || echo "unknown")"

    echo ""
    echo "================================================================"
    echo " Superbird Upstream Rebase Summary"
    echo "================================================================"
    echo "  Old pin : ${old_sha:0:12} ($old_date)"
    echo "  New ref : ${new_sha:0:12} ($new_date)"
    echo "  Commits : $count"
    echo ""

    echo "── Watchlist changes (deps / toolchain / CI / CMake) ──"
    local watchlist_hits
    watchlist_hits="$(git -C "$UPSTREAM_DIR" diff --name-status "${old_sha}..${new_sha}" -- \
        vcpkg.json \
        rust-toolchain.toml \
        Cargo.toml \
        Cargo.lock \
        .github/workflows \
        Meta/CMake/cmake_options.cmake \
        Meta/CMake/environment.cmake \
        Meta/ladybird.py \
        UI/CMakeLists.txt \
        UI/Qt/CMakeLists.txt \
        Libraries/LibWebView/CMakeLists.txt \
        Libraries/LibWeb/ContentBlocker/Rust/Cargo.toml 2>/dev/null || true)"
    if [[ -n "$watchlist_hits" ]]; then
        printf '%s\n' "$watchlist_hits" | sed 's/^/  /'
    else
        echo "  (none)"
    fi
    echo ""

    echo "── Upstream changes overlapping Superbird patches/ ──"
    local tmp_dir
    tmp_dir="$(mktemp -d)"
    git -C "$UPSTREAM_DIR" diff --name-only "${old_sha}..${new_sha}" | sort -u > "$tmp_dir/upstream_files.txt"
    if ls "$ROOT_DIR"/patches/*.patch >/dev/null 2>&1; then
        grep -h '^+++ b/' "$ROOT_DIR"/patches/*.patch | sed 's|^+++ b/||' | sort -u > "$tmp_dir/patch_files.txt"
    else
        : > "$tmp_dir/patch_files.txt"
    fi
    local overlaps
    overlaps="$(comm -12 "$tmp_dir/upstream_files.txt" "$tmp_dir/patch_files.txt" || true)"
    if [[ -n "$overlaps" ]]; then
        printf '%s\n' "$overlaps" | sed 's/^/  /'
    else
        echo "  (none)"
    fi
    echo ""

    echo "── Upstream changes colliding with Superbird overlay/ ──"
    if [[ -d "$ROOT_DIR/overlay" ]]; then
        (cd "$ROOT_DIR/overlay" && find . -type f | sed 's|^\./||' | sort -u) > "$tmp_dir/overlay_files.txt"
    else
        : > "$tmp_dir/overlay_files.txt"
    fi
    local overlay_hits
    overlay_hits="$(comm -12 "$tmp_dir/upstream_files.txt" "$tmp_dir/overlay_files.txt" || true)"
    if [[ -n "$overlay_hits" ]]; then
        printf '%s\n' "$overlay_hits" | sed 's/^/  [COLLISION] /'
    else
        echo "  (none)"
    fi
    echo ""

    echo "── New upstream lines mentioning Ladybird app identity (spot-check) ──"
    local id_hits
    id_hits="$(git -C "$UPSTREAM_DIR" diff -U0 "${old_sha}..${new_sha}" -- UI/ Libraries/LibWebView/ Meta/ Base/ 2>/dev/null \
        | grep -E '^\+[^+]' \
        | grep -E 'org\.ladybird|Ladybird\.app|Ladybird-UI|Ladybird-<' \
        | head -n 25 || true)"
    if [[ -n "$id_hits" ]]; then
        printf '%s\n' "$id_hits" | sed 's/^/  /'
    else
        echo "  (none)"
    fi
    rm -rf "$tmp_dir"
    echo "================================================================"
    echo ""
}

cmd_worktree() {
    local stamp
    stamp="$(date +%Y%m%d-%H%M)"
    local branch="${1:-rebase/ladybird-$stamp}"
    local parent_dir
    parent_dir="$(dirname "$ROOT_DIR")"
    local default_dir="$parent_dir/Superbird-rebase-$stamp"
    local worktree_dir="${2:-$default_dir}"

    if git -C "$ROOT_DIR" show-ref --verify --quiet "refs/heads/$branch"; then
        echo "Error: branch '$branch' already exists." >&2
        exit 1
    fi

    echo "[superbird] Fetching origin/main..."
    git -C "$ROOT_DIR" fetch origin main

    local base_ref="origin/main"
    if ! git -C "$ROOT_DIR" rev-parse --verify --quiet "$base_ref" >/dev/null; then
        base_ref="main"
    fi

    echo "[superbird] Creating worktree at $worktree_dir (branch: $branch, base: $base_ref)..."
    git -C "$ROOT_DIR" worktree add -b "$branch" "$worktree_dir" "$base_ref"

    echo "[superbird] Initializing upstream/ submodule in worktree..."
    if [[ -e "$UPSTREAM_DIR/.git" ]]; then
        if ! git -C "$worktree_dir" submodule update --init --reference "$UPSTREAM_DIR" upstream 2>/dev/null; then
            git -C "$worktree_dir" submodule update --init --depth 1 upstream
        fi
    else
        git -C "$worktree_dir" submodule update --init --depth 1 upstream
    fi

    echo ""
    echo "[superbird] Rebase worktree ready:"
    echo "  WORKTREE=$worktree_dir"
    echo "  BRANCH=$branch"
    echo "  Next: cd \"$worktree_dir\" && ./scripts/rebase-upstream.sh prepare"
}

cmd_prepare() {
    local target_ref="${1:-origin/master}"
    local git_dir
    git_dir="$(resolve_upstream_git_dir)"

    if [[ -d "$git_dir/rebase-merge" || -d "$git_dir/rebase-apply" ]]; then
        echo "Error: a rebase or am is already in progress in upstream/." >&2
        echo "  Finish it with: git -C upstream rebase --continue && ./scripts/rebase-upstream.sh post-rebase" >&2
        echo "  Or abort with:  git -C upstream rebase --abort" >&2
        exit 1
    fi

    local old_sha
    old_sha="$(git -C "$ROOT_DIR" ls-tree HEAD upstream | awk '{print $3}')"
    if [[ -z "$old_sha" ]]; then
        echo "Error: could not read pinned upstream SHA from HEAD." >&2
        exit 1
    fi

    # Ensure upstream/ has a clean, up-to-date superbird branch starting from old_sha
    local cur_branch cur_base
    cur_branch="$(git -C "$UPSTREAM_DIR" rev-parse --abbrev-ref HEAD)"
    cur_base="$(cat "$git_dir/superbird-base" 2>/dev/null || true)"
    if [[ "$cur_branch" != "superbird" || "$cur_base" != "$old_sha" ]]; then
        echo "[superbird] Applying current patch stack onto pinned base ${old_sha:0:12} first..."
        "$ROOT_DIR/scripts/apply.sh"
    elif [[ -n "$(git -C "$UPSTREAM_DIR" status --porcelain --untracked-files=no)" ]]; then
        echo "Error: upstream/ has uncommitted changes. Commit, stash, or reset before rebasing." >&2
        git -C "$UPSTREAM_DIR" status --short --untracked-files=no >&2
        exit 1
    fi

    # Fetch upstream history so old_sha..new_sha is connected (needed for 3-way merge & git log)
    local is_shallow
    is_shallow="$(git -C "$UPSTREAM_DIR" rev-parse --is-shallow-repository)"
    echo "[superbird] Fetching upstream Ladybird ($target_ref)..."
    if [[ "$is_shallow" == "true" ]]; then
        # Pull commits since 3 days before old_sha so the merge-base is present without full clone
        local old_epoch since_date
        old_epoch="$(git -C "$UPSTREAM_DIR" show -s --format='%ct' "$old_sha" 2>/dev/null || echo "")"
        if [[ -n "$old_epoch" ]]; then
            since_date="$(python3 -c 'import datetime, sys; print((datetime.datetime.fromtimestamp(int(sys.argv[1]), datetime.timezone.utc) - datetime.timedelta(days=3)).strftime("%Y-%m-%d"))' "$old_epoch")"
            git -C "$UPSTREAM_DIR" fetch --shallow-since="$since_date" origin master || git -C "$UPSTREAM_DIR" fetch origin master
        else
            git -C "$UPSTREAM_DIR" fetch origin master
        fi
    else
        git -C "$UPSTREAM_DIR" fetch origin master
    fi

    local new_sha
    if ! new_sha="$(git -C "$UPSTREAM_DIR" rev-parse --verify "${target_ref}^{commit}" 2>/dev/null)"; then
        # Try fetching an explicit SHA if target_ref wasn't a local ref
        git -C "$UPSTREAM_DIR" fetch origin "$target_ref"
        new_sha="$(git -C "$UPSTREAM_DIR" rev-parse --verify "FETCH_HEAD^{commit}")"
    fi

    if [[ "$old_sha" == "$new_sha" ]]; then
        echo "[superbird] Already pinned at ${new_sha:0:12} ($target_ref). Nothing to rebase."
        exit 0
    fi

    # Ensure ancestry connects old_sha -> new_sha for proper 3-way conflict resolution
    if ! git -C "$UPSTREAM_DIR" merge-base --is-ancestor "$old_sha" "$new_sha" 2>/dev/null; then
        echo "[superbird] Deepening shallow history to connect ${old_sha:0:12}..${new_sha:0:12}..."
        local attempt
        for attempt in 1 2 3 4; do
            git -C "$UPSTREAM_DIR" fetch --deepen=250 origin master || break
            if git -C "$UPSTREAM_DIR" merge-base --is-ancestor "$old_sha" "$new_sha" 2>/dev/null; then
                break
            fi
        done
        if ! git -C "$UPSTREAM_DIR" merge-base --is-ancestor "$old_sha" "$new_sha" 2>/dev/null; then
            if [[ "$(git -C "$UPSTREAM_DIR" rev-parse --is-shallow-repository)" == "true" ]]; then
                echo "[superbird] Unshallowing upstream/ to resolve history..."
                git -C "$UPSTREAM_DIR" fetch --unshallow origin master || true
            fi
        fi
    fi

    echo "$old_sha" > "$git_dir/superbird-rebase-old"
    echo "$new_sha" > "$git_dir/superbird-rebase-target"

    print_summary "$old_sha" "$new_sha"

    echo "[superbird] Removing overlay symlinks before rebase..."
    remove_overlay_links "$git_dir"

    echo "[superbird] Rebasing superbird patch stack onto ${new_sha:0:12}..."
    if git -C "$UPSTREAM_DIR" rebase --onto "$new_sha" "$old_sha" superbird; then
        echo "[superbird] Rebase applied cleanly!"
        cmd_post_rebase
        exit 0
    fi

    echo "" >&2
    echo "[superbird] Rebase paused on conflict." >&2
    echo "  Conflicted files:" >&2
    git -C "$UPSTREAM_DIR" diff --name-only --diff-filter=U | sed 's/^/    /' >&2
    if [[ -f "$git_dir/rebase-merge/msgnum" && -f "$git_dir/rebase-merge/end" ]]; then
        echo "  Progress: patch $(cat "$git_dir/rebase-merge/msgnum") of $(cat "$git_dir/rebase-merge/end")" >&2
    fi
    if [[ -f "$git_dir/rebase-merge/message" ]]; then
        echo "  Current patch: $(head -n 1 "$git_dir/rebase-merge/message")" >&2
    fi
    echo "" >&2
    echo "  Inspect upstream history for a conflicted file:" >&2
    echo "    git -C upstream log -p ${old_sha:0:12}..${new_sha:0:12} -- <path>" >&2
    echo "  After resolving & staging in upstream/:" >&2
    echo "    GIT_EDITOR=true git -C upstream rebase --continue" >&2
    echo "  When the entire rebase finishes:" >&2
    echo "    ./scripts/rebase-upstream.sh post-rebase" >&2
    exit 2
}

cmd_post_rebase() {
    local git_dir
    git_dir="$(resolve_upstream_git_dir)"

    if [[ -d "$git_dir/rebase-merge" || -d "$git_dir/rebase-apply" ]]; then
        echo "Error: rebase is still in progress in upstream/. Finish it first." >&2
        exit 1
    fi

    local branch
    branch="$(git -C "$UPSTREAM_DIR" rev-parse --abbrev-ref HEAD)"
    if [[ "$branch" != "superbird" ]]; then
        echo "Error: upstream/ must be on branch 'superbird' (currently: $branch)." >&2
        exit 1
    fi

    local new_sha
    new_sha="$(cat "$git_dir/superbird-rebase-target" 2>/dev/null || true)"
    if [[ -z "$new_sha" ]]; then
        echo "Error: no superbird-rebase-target recorded. Run prepare first." >&2
        exit 1
    fi

    if ! git -C "$UPSTREAM_DIR" merge-base --is-ancestor "$new_sha" HEAD; then
        echo "Error: recorded target ${new_sha:0:12} is not an ancestor of upstream/ HEAD." >&2
        exit 1
    fi

    echo "$new_sha" > "$git_dir/superbird-base"
    echo "[superbird] Updated superbird-base to ${new_sha:0:12}."

    "$ROOT_DIR/scripts/apply.sh" --overlay-only

    echo ""
    echo "[superbird] Post-rebase setup complete."
    echo "  Next:"
    echo "    1. Fast checks:  make test"
    echo "    2. Full verify:  ./scripts/test-rebase.sh"
    echo "    3. Finalize pin: ./scripts/rebase-upstream.sh finalize"
}

cmd_summary() {
    local git_dir
    git_dir="$(resolve_upstream_git_dir)"
    local old_sha new_sha
    old_sha="$(cat "$git_dir/superbird-rebase-old" 2>/dev/null || git -C "$ROOT_DIR" ls-tree HEAD upstream | awk '{print $3}')"
    new_sha="$(cat "$git_dir/superbird-rebase-target" 2>/dev/null || cat "$git_dir/superbird-base" 2>/dev/null || true)"
    if [[ -z "$old_sha" || -z "$new_sha" ]]; then
        echo "Error: no rebase range recorded. Run prepare first." >&2
        exit 1
    fi
    print_summary "$old_sha" "$new_sha"
}

cmd_finalize() {
    local git_dir
    git_dir="$(resolve_upstream_git_dir)"

    if [[ -d "$git_dir/rebase-merge" || -d "$git_dir/rebase-apply" ]]; then
        echo "Error: rebase is still in progress in upstream/." >&2
        exit 1
    fi

    local new_sha
    new_sha="$(cat "$git_dir/superbird-rebase-target" 2>/dev/null || cat "$git_dir/superbird-base" 2>/dev/null || true)"
    if [[ -z "$new_sha" ]]; then
        echo "Error: no target SHA recorded in superbird-rebase-target or superbird-base." >&2
        exit 1
    fi

    echo "[superbird] Exporting rebased patches to patches/..."
    "$ROOT_DIR/scripts/export-patches.sh"

    echo "[superbird] Pinning upstream/ submodule to ${new_sha:0:12}..."
    "$ROOT_DIR/scripts/reset-upstream.sh" "$new_sha"
    # .gitmodules sets ignore = all on upstream/, so update the gitlink entry directly.
    git -C "$ROOT_DIR" update-index --cacheinfo "160000,${new_sha},upstream"
    local staged_sha
    staged_sha="$(git -C "$ROOT_DIR" ls-files --stage upstream | awk '{print $2}')"
    if [[ "$staged_sha" != "$new_sha" ]]; then
        echo "Error: failed to stage upstream/ pin at ${new_sha} (got: ${staged_sha:-empty})." >&2
        exit 1
    fi

    echo "[superbird] Cold re-apply verification onto ${new_sha:0:12}..."
    SUPERBIRD_BASE_SHA="$new_sha" "$ROOT_DIR/scripts/apply.sh"

    echo "[superbird] Running fast unit + JS syntax verification..."
    CXX="${CXX:-clang++}" "$ROOT_DIR/scripts/test-unit.sh"
    node --check "$UPSTREAM_DIR/Base/res/ladybird/about-pages/settings/tabs.js"
    node --check "$UPSTREAM_DIR/Base/res/ladybird/about-pages/settings/superbird.js"

    echo ""
    echo "[superbird] Finalize succeeded! Submodule upstream/ is staged at ${new_sha:0:12}."
    echo "  Review and stage remaining changes (patches/, overlay/, CI/scripts), then commit & push."
    git -C "$ROOT_DIR" status --short
}

CMD="${1:-}"
shift || true

case "$CMD" in
    worktree)    cmd_worktree "$@" ;;
    prepare)     cmd_prepare "$@" ;;
    post-rebase) cmd_post_rebase "$@" ;;
    summary)     cmd_summary "$@" ;;
    finalize)    cmd_finalize "$@" ;;
    -h|--help|"") usage ;;
    *)
        echo "Unknown command: $CMD" >&2
        usage >&2
        exit 1
        ;;
esac
