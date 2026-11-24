# Superbird

A modular, power-user fork of [Ladybird](https://github.com/LadybirdBrowser/ladybird) that adds opt-in productivity features while running safely side-by-side with stock Ladybird using its own profile, IPC identity, and app bundle.

Superbird is designed to keep upstream changes minimal so rebasing onto fast-moving Ladybird `master` stays painless: almost all new code lives in `overlay/UI/Qt/Superbird/` and wires into the `upstream/` submodule through a small, clean patch stack in `patches/`.

## What's in Superbird right now

- **Side-by-side coexistence with stock Ladybird** — uses its own `Superbird` profile/data directory, Mach port prefix (`Superbird-*`), and bundle/desktop ID (`org.superbird.Superbird`), so stock Ladybird and Superbird can run simultaneously without colliding on profile locks, sessions, or settings.
- **Pick-and-choose modular architecture** — every feature lives under `overlay/UI/Qt/Superbird/Modules/` with:
  - A runtime toggle in `about:settings` -> **Superbird** (zero UI chrome and zero runtime overhead when disabled).
  - Build-time CMake toggles (`-DSUPERBIRD_MODULE_<NAME>=ON/OFF`) for custom stripped builds.
  - Dedicated sidecar SQLite databases instead of modifying upstream `Sessions.db` or `Bookmarks.db` schemas.
- **Tree-Style Tabs module** *(Module #1)*:
  - Toggle in `about:settings` -> **Superbird** (or **Appearance** / right-click **Tab Layout** menu).
  - Links opened in a new tab nest automatically underneath their parent tab.
  - Sidebery-style continuous vertical branch lines running under each parent's expand/collapse chevron.
  - Collapse/expand tab branches with descendant count badges.
  - 3-zone drag-and-drop to reorder or nest tabs (hold `Alt` to drag a single tab instead of its whole branch).
  - Configurable close behavior when closing a parent tab (promote first child, attach children to grandparent, or close the whole branch).
  - Multi-select (`Ctrl`/`Cmd` + click or `Shift` + click) and right-click branch actions (new child tab, unload branch, bookmark branch to folder, move branch to new window, close branch).
  - Persists tree structure across restarts in a sidecar SQLite database (`TreeTabs.db`).

## Upcoming Features

Superbird is being built in focused, independently toggleable waves (see [ROADMAP.md](ROADMAP.md) for the full technical plan):

### Wave 1 — Pick-and-Choose Power Modules
- **Command Palette (`Cmd/Ctrl+K` or `F2`)** — centered floating modal with live fuzzy search across open tabs, bookmarks, recent history, quick math/URL evaluation, and every enabled Superbird module's actions via the shared action bus.
- **Reader View (`Cmd/Ctrl+Alt+R`)** — strips clutter from article pages into a clean, sandboxed typography overlay (font family/size/width, Light/Sepia/Dark themes, metadata & reading time) without mutating or losing your scroll position on the original page.
- **Workspaces (Spaces)** — partition a window's tabs into named contexts (*Default*, *Work*, *Research*, *Personal*) with their own switcher bar and sidecar `Workspaces.db`, working cleanly with both Tree-Style Tabs and standard tabs.
- **Split View (Tab Tiling)** — view 2 tabs side-by-side or in a 3–4 pane grid inside one window, with active-pane focus indicator, swap/unsplit controls, and automatic collapse when a pane closes.
- **Glance (Quick Peek)** — `Alt/Opt + Click` (or context menu) to preview links in a floating modal sheet (~80% viewport) with one-click promotion to a real tab (nested under its parent when Tree Tabs is enabled).
- **Tree-Style Tabs Polish** — inline sidebar quick-filter box and persisted branch color tags.

### Wave 2+ (Planned Later)
- Compact / Auto-Hide Chrome mode
- Persistent Web Panels & local Markdown Scratchpad
- Auto-Archive / Tab Lifecycle rules
- Named Session Snapshots & Export
- Vim Mode & Link Hints (`f`/`F` overlays)
- Per-site Boosts (custom CSS / element zapper) & Command Chains

## Building

Make sure you have Ladybird's build dependencies installed first (see `upstream/Documentation/BuildInstructionsLadybird.md`).

```bash
git clone --recurse-submodules https://github.com/CainCosta/Superbird.git
cd Superbird

./scripts/apply.sh
./scripts/build.sh Release
```

To omit a module at build time, pass `-DSUPERBIRD_MODULE_<NAME>=OFF` (for example, `-DSUPERBIRD_MODULE_TREE_TABS=OFF`).

## Working on Superbird

- `./scripts/apply.sh` — applies the patches in `patches/` and symlinks `overlay/` into `upstream/`
- `./scripts/test-unit.sh` — runs the fast C++23 unit tests for `ModuleRegistry` and `TreeTabModel` without needing a full browser build
- `./scripts/export-patches.sh` — regenerates `patches/` from commits on the `superbird` branch in `upstream/`
- `./scripts/sync-upstream.sh` — pulls newer Ladybird `master` and checks whether the patch stack still applies cleanly
- `./scripts/reset-upstream.sh` — resets `upstream/` back to a clean base checkout
