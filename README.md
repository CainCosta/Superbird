# Superbird

Superbird is a modular fork of the [Ladybird](https://github.com/LadybirdBrowser/ladybird) web browser with pick-and-choose power features you can enable or disable anytime. It runs safely side-by-side with stock Ladybird using its own app identity, profile directory, IPC namespace, and settings.

## Current Features

### Core & Coexistence
- **Side-by-side with stock Ladybird:** Dedicated `Superbird.app` / `org.superbird.Superbird` bundle, isolated profile root (`~/Library/Application Support/Superbird` on macOS, `~/.local/share/Superbird` & `~/.config/Superbird` on Linux), separate Mach bootstrap / IPC names, and Superbird's own app icon and logomark.
- **Modular architecture & unified Settings hub:** Toggle every feature on or off at runtime in `about:settings` → **Superbird** (disabled modules leave no UI chrome behind), configure per-module options and custom keyboard shortcuts, or strip modules at build time with `-DSUPERBIRD_MODULE_<NAME>=OFF`.
- **Upstream auto-updates disabled:** Stock Ladybird update checks and Sparkle are turned off so Superbird never overwrites itself with stock Ladybird.

### Shipped Modules
- **Tree-Style Vertical Tabs:** Collapsible tab branches with descendant count badges, 3-zone drag-and-drop nesting and reordering, multi-select and branch actions (unload, close, move branch to new window), customizable parent-tab close policies, inline sidebar quick-filter, inherited branch color tags, and sidecar SQLite persistence (`TreeTabs.db`) across restarts.
- **Command Palette (`Cmd/Ctrl+K` or `F2`):** Centered floating palette with live fuzzy search across open tabs (with workspace and branch context), bookmarks, history, browser and Superbird module actions, inline `=` math evaluation, and direct URL navigation or search.
- **Reader View (`Ctrl+Cmd+R` on macOS / `Ctrl+Alt+R` on Linux, or address-bar icon):** Extracts articles into a clean, script-free overlay without modifying or losing scroll position on the underlying page, with controls for font family (Serif / Sans / Mono), font size, line height, column width, and Light / Sepia / Dark / System themes.
- **Workspaces (`Cmd/Ctrl+1–9`, tabs via `Cmd/Ctrl+Shift+1–9`):** Partition a window's tabs into named spaces with icons and accent tints, switch via the sidebar footer, toolbar pill, or Command Palette, move tabs between workspaces, keep pinned tabs global, and persist state in a sidecar `Workspaces.db` (works with both Tree Tabs and stock tabs).
- **Split View (Tab Tiling):** Tile 2–4 tabs side-by-side, stacked, or in a grid inside one window via the tab context menu, Command Palette, or dragging a tab to a page edge, with per-pane headers, accent focus border, pane swapping, and clean collapse when closing a tiled tab.
- **Glance (Quick Link Peek):** Preview any link via `Alt/Opt+Click` or right-click → *Peek in Glance* in a floating sheet without cluttering your tab bar, with one-click **Open as Tab** (nested under the opener when Tree Tabs is enabled), **Copy URL**, and `Esc`/backdrop dismiss.
- **uBO-compatible Content Blocking:** Built on `adblock-rust` with 9 default-enabled filter lists (uBO's 7 default lists + EasyList & EasyPrivacy), `! Expires:` / `! Diff-Expires:` auto-refresh, `!#if` preprocessing, trust-isolated bundles, document-start scriptlet injection across frames, `$redirect`, `$removeparam`, `$csp`, and WebSocket blocking, procedural & action cosmetic filtering, per-site cosmetic filtering toggle in Site Settings, `SBCE0001` binary snapshot cache, opt-in filtering logger, and a live switch between **uBO-compatible** and **Default Ladybird** engines.

## Upcoming Features (Wave 2+)

See [ROADMAP.md](ROADMAP.md) for the full roadmap and design docs under `docs/design/`.

- Compact / auto-hide toolbar mode
- Persistent web panels and local Markdown scratchpad
- Auto-archive inactive tabs
- Named session snapshots and export
- Vim mode and keyboard link hints
- Per-site custom styles (boosts) and command chains

## Quick Start

```sh
git clone https://github.com/CainCosta/Superbird.git && cd Superbird
make setup        # brew deps, upstream submodule, Rust toolchain, git hooks, apply patches
make run          # build + launch Superbird
make test         # fast C++23, Rust, Node, and settings JS checks
make install      # build and install portable Superbird.app into /Applications (macOS)
```

For architecture details, patch workflow, module development, and upstream rebasing (`make test-rebase` / `scripts/rebase-upstream.sh`), see [DEVELOPING.md](DEVELOPING.md).
