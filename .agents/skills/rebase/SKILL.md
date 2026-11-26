---
name: rebase
description: >-
  Rebase Superbird main onto the latest (or specified) upstream Ladybird commit in an
  isolated git worktree, resolve patch and overlay conflicts, run tiered unit, browser,
  UI/Qt, LibWebView, and macOS bundle tests, fix breakages, push the branch, and open a
  pull request to merge into main. Use when asked to rebase Superbird, bump upstream
  Ladybird, sync with newest Ladybird, or update the upstream submodule.
---

# Rebase Superbird onto Latest Ladybird

Superbird is a pinned `upstream/` Ladybird submodule + ordered `patches/*.patch` commits +
symlinked `overlay/` files. Rebasing `main` onto a newer Ladybird commit means replaying
the patch stack onto the new upstream commit in an isolated git worktree, adapting seams
and overlay code to upstream API changes, passing tiered verification, exporting clean
patches, pinning the submodule, pushing, and opening a PR.

## Inputs & Defaults

- **Target upstream ref:** defaults to `origin/master` (or an explicit Ladybird SHA/ref if the user specifies one).
- **Test depth:** targeted upstream suites by default (`./scripts/test-rebase.sh`); pass `--full` if the user asks for the entire upstream `ctest` suite.

## Phase 1 — Create an Isolated Worktree

Never mutate the primary `main` checkout directly.

1. Check if you are already operating inside a dedicated rebase worktree branched off `origin/main`.
2. If not (e.g., you are in the primary checkout), create one:
   ```sh
   ./scripts/rebase-upstream.sh worktree
   ```
   This fetches `origin/main`, creates `../Superbird-rebase-<timestamp>` on branch
   `rebase/ladybird-<timestamp>`, and initializes its `upstream/` submodule reusing local
   git objects.
3. Run all subsequent commands from inside that new worktree directory.
   - `scripts/build.sh` automatically detects linked Superbird worktrees and shares the
     primary checkout's `upstream/Build/{caches,vcpkg-*}` while keeping
     `upstream/Build/<preset>` isolated.

## Phase 2 — Fetch, Inspect Delta, and Rebase Patch Stack

Inside the worktree, run:

```sh
./scripts/rebase-upstream.sh prepare [target-ref]
```

This fetches enough upstream history to connect `OLD_SHA..NEW_SHA`, prints the **Upstream
Rebase Summary**, unlinks `overlay/` temporarily, and runs:
`git -C upstream rebase --onto <NEW_SHA> <OLD_SHA> superbird`.

Review the printed summary before touching code:
- **Watchlist changes:** `vcpkg.json`, `rust-toolchain.toml`, `Cargo.lock`, `.github/workflows/`, `Meta/CMake/cmake_options.cmake`, `Meta/ladybird.py`.
- **Overlaps:** upstream files touched in `OLD_SHA..NEW_SHA` that overlap `patches/` or `overlay/`.
- **Identity spot-check:** new upstream lines mentioning `org.ladybird`, `Ladybird.app`, or `Ladybird-UI` (check whether patch `0001` needs an update to preserve side-by-side isolation).

### Early Circuit Breakers (Pause & Ask the User)

Stop and ask before spending hours if:
- `vcpkg.json` bumps heavy ports (Qt, Skia, ANGLE) that will trigger a multi-hour local rebuild.
- Upstream redesigned a subsystem that a Superbird module depends on architecturally (needs a design decision rather than a mechanical API update).
- `OLD_SHA == NEW_SHA` (already up to date — report and clean up the worktree).

## Phase 3 — Resolve Patch Conflicts & Overlay Collisions

If `prepare` exits with code `2` (rebase paused on conflict):

1. Inspect the conflicted files and check **why** upstream changed them:
   ```sh
   git -C upstream diff --name-only --diff-filter=U
   git -C upstream log -p <OLD_SHA>..<NEW_SHA> -- <path>
   ```
2. Resolve each conflict preserving both upstream's behavioral change and Superbird's seam.
3. Stage and continue:
   ```sh
   git -C upstream add <resolved-files>
   GIT_EDITOR=true git -C upstream rebase --continue
   ```
   - If an entire patch was absorbed upstream and is now empty/redundant, skip it intentionally (`git -C upstream rebase --skip`) and record it for the PR report.
4. Repeat until the rebase finishes, then run:
   ```sh
   ./scripts/rebase-upstream.sh post-rebase
   ```
   This updates `superbird-base` to `NEW_SHA` and runs `./scripts/apply.sh --overlay-only`.
   If it reports an **overlay collision** (upstream now tracks a path in `overlay/`), move
   Superbird's delta into `patches/` or relocate the overlay file before continuing.

## Phase 4 — Fail-Fast Build, Test & Fix Loop

Run the tiered verification script:

```sh
./scripts/test-rebase.sh            # default: targeted upstream suites + Superbird E2E + bundle dry-run
# or, if requested:
./scripts/test-rebase.sh --full     # full upstream ctest suite
```

Stages run fail-fast in order:
1. Fast C++23 unit tests + Rust `ContentBlocker` tests + Node uBO converter tests + JS syntax (`tabs.js`, `superbird.js`).
2. `ladybird_impl` (fail-fast Qt/Superbird build) → full app bundle + `WebDriver`.
3. CMake invariant audit (`LADYBIRD_ENABLE_UPDATE_CHECK=OFF`, `LADYBIRD_ENABLE_SPARKLE=OFF`).
4. `BUILD_TESTING=ON` build → Superbird E2E `test-web` (`superbird-content-blocker-*.html`) → upstream `ctest` (`TestUIQt`, `TestEventLoopImplementation`, `TestFullscreenDebounce`, `TestNativeTextEditingBindings`, `TestLibWebView`, `TestLibWeb`, `TestDeadRustExports`, and WebDriver content-blocker/settings/BiDi/permissions/default-browser/session-history/web-ui suites).
5. macOS portable bundle & `codesign --verify --deep --strict` dry-run in a temp staging dir (never modifies `/Applications/Superbird.app`).

### Fixing Breakages Cleanly

- **Overlay breakages (`overlay/...`):** fix directly in `overlay/`.
- **Upstream seam breakages (`upstream/...`):** keep patch boundaries clean! Commit fixes against the owning patch instead of adding a catch-all `0029-fix-rebase.patch`:
  ```sh
  git -C upstream log --oneline "$(cat "$(git -C upstream rev-parse --git-dir)/superbird-base")..HEAD"
  git -C upstream commit -a --fixup <owning-patch-sha>
  GIT_SEQUENCE_EDITOR=: git -C upstream rebase -i --autosquash "$(cat "$(git -C upstream rev-parse --git-dir)/superbird-base")"
  ```
- **CI & build drift:** compare `upstream/.github/workflows/` against `.github/workflows/build.yml` (especially the `ghcr.io/ladybirdbrowser/ladybird-ci:<tag>` Linux container image, compiler flags, and required dependencies) and update `.github/workflows/build.yml` or `scripts/build.sh` as needed.
- **Rust toolchain drift:** if `upstream/rust-toolchain.toml` changed, run `(cd upstream && rustup toolchain install)` before rebuilding.
- **Upstream regression vs. Superbird regression:** if an upstream test fails and looks unrelated to Superbird's patches, check whether it also fails on unpatched `NEW_SHA` before working around it. If it is a stock upstream failure, report it to the user and ask how to proceed.

Iterate until `./scripts/test-rebase.sh` exits `0` with all stages passing.

## Phase 5 — Finalize, Cold Round-Trip & Commit

Once all tests pass and `upstream/` has no uncommitted changes:

```sh
./scripts/rebase-upstream.sh finalize
```

This automatically:
1. Exports `upstream/` commits into `patches/` (`./scripts/export-patches.sh`).
2. Resets `upstream/` to `NEW_SHA` and stages the submodule pin (`git -c submodule.upstream.ignore=none add upstream`).
3. Runs a cold `SUPERBIRD_BASE_SHA=<NEW_SHA> ./scripts/apply.sh` from scratch to prove CI and fresh clones apply cleanly with zero fuzz.
4. Re-runs `scripts/test-unit.sh` and JS syntax checks.

Then stage the remaining tracked changes (`patches/`, `overlay/`, any `.github/workflows/` or `scripts/` edits), double-check `git diff --cached --stat`, and commit:

```sh
git add patches overlay .github scripts   # only include paths that changed
git commit -m "Bump upstream Ladybird to <short-new-sha>"
```

Verify `.githooks/pre-commit` accepted the commit (it rejects accidental local-patch submodule pins).

## Phase 6 — Push, Open PR & Report

1. Push the branch (`.githooks/pre-push` will verify patches match `upstream/` and re-run unit tests):
   ```sh
   git push -u origin HEAD
   ```
2. Open a pull request against `main` (use `pull_request_create` if available in your tools; otherwise `gh pr create --base main`).
3. Put a structured **Rebase Report** in the PR description and final reply:
   - **Upstream range:** `<OLD_SHA>..<NEW_SHA>` (commit count + upstream commit date)
   - **Watchlist / CI / Dependency updates:** `vcpkg.json`, Rust toolchain, `ladybird-ci` container tag, CMake options
   - **Patch stack changes:** which patches conflicted and how they were resolved, which received fixups, any dropped/absorbed patches
   - **Overlay & script changes:** summary of API adaptations
   - **Verification matrix:** Stage 1–5 results and timings (`targeted` or `--full`), plus confirmation that updater/Sparkle remain OFF and cold re-apply passed
   - **Manual checks (if warranted):** note any UI/Qt surfaces touched that deserve a quick visual check
   - **Worktree cleanup note:** `git worktree remove <worktree-path>` once merged
