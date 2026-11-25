# Superbird Roadmap — Tier 0 & Wave 1

Superbird is a **modular, souped-up fork of [Ladybird](https://github.com/LadybirdBrowser/ladybird)** that runs safely side-by-side with regular Ladybird and lets users pick and choose which power features they want turned on.

Everything here is designed around one hard constraint: upstream Ladybird moves fast (~100 commits/day), so **new code lives in `overlay/` and touches upstream only through tiny, stable seams in `patches/`**.

---

## Core Design Rules

1. **Coexist with stock Ladybird:** Installing and running Superbird should never collide with a running stock Ladybird instance or touch its profile, sessions, or settings.
2. **Pick-and-choose modularity:** Every feature is an independent module under `overlay/UI/Qt/Superbird/Modules/<name>/` with:
   - A runtime toggle in `about:settings` -> **Superbird** (zero UI chrome and zero runtime overhead when disabled).
   - Optional CMake feature flags (`-DSUPERBIRD_MODULE_<NAME>=ON/OFF`) for custom stripped builds.
   - Its own sidecar SQLite database (like `TreeTabs.db`) instead of modifying upstream `Sessions.db` or `Bookmarks.db` schemas.
3. **First-party look and feel:** All new Qt chrome uses Ladybird's `Theme::chrome()`, `ChromeStyle::*`, and native `ChromeIcon`s so enabled modules look built-in, not bolted on.
4. **Ship piecemeal:** Each milestone is self-contained, ships with fast C++ unit tests (`./scripts/test-unit.sh`), and must pass both macOS arm64 and Linux x86_64 CI builds before starting the next.

---

## Tier 0 — Foundation: Rename to Superbird, Coexistence & Modular Core

Before stacking more features on top of Tree-Style Tabs, we rebrand to **Superbird**, isolate its identity so it coexists cleanly with stock Ladybird, and lay down one reusable module framework.

> **Status: ✅ Shipped (M0).** Superbird has its own app, profile, IPC, and desktop identity; `Core/` provides `SuperbirdModule`, `ModuleRegistry` + action bus, and the `WindowHost` seam; Tree-Style Tabs is module #1 with its actions on the bus; `about:settings -> Superbird` toggles modules at runtime.

### 0A. Rename & Side-by-Side Coexistence with Stock Ladybird
Right now the fork inherits Ladybird's app identity, profile directory, and single-instance lock, so running both browsers at once collides on the profile lock and shares `Sessions.db` / `Settings.json`.

- **Rebrand to Superbird:** Update `README.md`, `ROADMAP.md`, helper scripts, CI workflow names/artifacts (`Superbird-macOS-arm64`, `Superbird-Linux-x86_64`), and rename the GitHub repository to `Superbird`.
- **Separate profile root:** Point `WebView::Profile` (`upstream/Libraries/LibWebView/Profile.cpp`) and `m_instance_data_directory` (`upstream/Libraries/LibWebView/Application.cpp`) at `Superbird` instead of `Ladybird` (`~/Library/Application Support/Superbird` on macOS, `~/.local/share/Superbird` & `~/.config/Superbird` on Linux).
- **Separate IPC / single-instance identity:** Use `Superbird-` Mach bootstrap port prefix on macOS (`LibWebView/Application.cpp` and `UI/Qt/Application.cpp`) and separate desktop file / bundle identifier (`org.superbird.Superbird`, `Superbird.app` / `Superbird`).
- **Keep upstream internals working:** Ensure `LADYBIRD_SOURCE_DIR`, helper process lookup, and Lagom resource paths continue working seamlessly with zero regressions.
- **Done when:** Stock Ladybird and Superbird can both be open at the same time on macOS and Linux with independent profiles, sessions, and settings.

### 0B. Modular Core & Unified Settings Hub
Replace ad-hoc per-feature wiring with one reusable module framework:

- **Directory layout:**
  ```text
  overlay/UI/Qt/Superbird/
    Core/
      Module.h               # SuperbirdModule interface + lifecycle & action hooks
      ModuleRegistry.{h,cpp} # Registers compiled modules, handles enable/disable
      Action.h               # Shared action descriptor (Command Palette, menus, shortcuts)
      WindowHost.{h,cpp}     # Single per-BrowserWindow attachment point for all modules
    Modules/
      TreeTabs/              # Migrated from overlay/UI/Qt/TreeTabs/
      CommandPalette/
      ReaderView/
      Workspaces/
      SplitView/
      Glance/
  ```
- **Single `BrowserWindow` / `Application` seam:** Instead of adding new pointers and lifecycle calls into `BrowserWindow.cpp` for every future module, `BrowserWindow` owns one `Superbird::WindowHost` that fans out tab/window/settings/style hooks to enabled modules.
- **Unified `about:settings` -> Superbird tab:**
  - Add one `Superbird` category to `about:settings` (`settings.html` + `settings/superbird.js`) showing a clean toggle card for each compiled module (name, short description, on/off switch, and inline sub-settings that expand only when enabled).
  - Persist module enable flags and per-module JSON config cleanly under a single `superbird` settings block so upstream `Settings.h` barely changes when new modules are added.
- **CMake flags:** Expose `-DSUPERBIRD_MODULE_<NAME>=ON` options in `overlay/UI/Qt/Superbird/CMakeLists.txt`.

### 0C. Migrate Tree-Style Tabs to Module #1
- Move existing `overlay/UI/Qt/TreeTabs/` into `overlay/UI/Qt/Superbird/Modules/TreeTabs/` implementing `SuperbirdModule`.
- Register Tree-Style Tabs actions (collapse/expand branch, new child tab, unload branch, close branch, move branch to new window) through `Superbird::Action` so they automatically show up in future modules like the Command Palette.
- Preserve existing `TreeTabs.db` table schema and all existing `TestTreeTabModel` unit tests.

---

## Wave 1 — Pick-and-Choose Power Modules

Each module below is independent, toggleable in `about:settings -> Superbird`, and ordered so earlier modules make later ones even better.

### M1 — Command Palette (`Cmd/Ctrl+K` or `F2`)
*Inspired by Vivaldi Quick Commands, Arc `Cmd+T`, and SigmaOS.*

> **Status: ✅ Shipped (M1).** `Modules/CommandPalette/` fuzzy-ranks open tabs (with Tree Tabs branch context), bookmarks, history, and every bus action (17 browser commands + module actions), evaluates `=` math, and navigates/searches typed input; one 5-line patch lets modules reserve window shortcuts over a focused page (`WindowHost::reserve_shortcut`). Design: `docs/design/M1-command-palette.md`.
- **What it does:** A fast, centered floating Qt modal over the active window with live fuzzy matching across:
  - **Open tabs** (shows workspace/branch context; jump to tab or close it)
  - **Bookmarks & recent history** (via existing `BookmarkStore` and `HistoryStore`)
  - **Browser & Superbird module actions** (auto-discovered from `Superbird::ModuleRegistry` — e.g. *Toggle Reader View*, *Split Current Tab*, *Unload Branch*, *Toggle Force Dark*, *Switch Workspace*, *Open Settings*)
  - **Quick utility evaluators** (simple math expressions like `= 128 * 1.15`, direct URL navigation)
- **Why it comes right after Tier 0:** Almost 100% self-contained in Qt, tiny upstream footprint (one global shortcut hook on `BrowserWindow`), and instantly gives every later Wave 1 module keyboard-first discoverability without cluttering toolbars.
- **Unit tests:** Fuzzy scorer/ranking + math expression parser + action registry filtering.

### M2 — Reader View *(Promoted to Wave 1)*
*Inspired by Firefox Reader View, Safari Reader, and Brave Speedreader.*

> **Status: ✅ Shipped (M2).** `Modules/ReaderView/` adds a pure C++23 HTML tree + `ArticleExtractor` (scoring, allow-list sanitizer, URL resolution, reading time, readerability) and a script-free reader `WebContentView` overlay with font/size/line-height/width/theme controls. Patches: 0007 read-only `view_request_reader_document` Hub snapshot (48 lines), 0008 `LocationEdit::add_page_action` hook (41), 0009 per-view scripting switch that survives page changes (7, needed to guarantee JS stays off). Shortcut is `Ctrl+Cmd+R` on macOS (Cmd+Opt/Shift+R already reload). Design: `docs/design/M2-reader-view.md`.
- **What it does:** Strips navigation clutter, sidebars, and noisy layout from article pages and renders clean, readable typography — **without mutating or losing your place on the original page**.
  - Toggle via a subtle book icon in the address bar (shown when the page looks article-like, or always triggerable via Command Palette / `Cmd/Ctrl+Alt+R`).
  - Clean floating controls for:
    - **Typography:** Serif / Sans / Mono, font size (`-` / `+`), line height, and column width (Narrow / Medium / Wide).
    - **Theme:** Follow System, Light, Sepia, Dark.
    - **Metadata header:** Article title, byline/author, site name, estimated reading time, and original URL link.
  - Toggling Reader View off immediately reveals the untouched live tab underneath with original scroll state intact.
- **Architecture across Ladybird's Hub boundary:**
  - Because Ladybird's Qt UI talks to `WebContent` through the `Hub` process (`RemoteView` -> `HubConnection`), Reader View uses **one clean, read-only extraction seam**:
    1. Request the current document's rendered HTML / article snapshot through a single `Hub` IPC call (`async_view_get_reader_document` -> `did_get_reader_document`).
    2. Run a pure C++23 Readability-style scoring & sanitization extractor in `overlay/UI/Qt/Superbird/Modules/ReaderView/ArticleExtractor.{h,cpp}` (strips scripts/iframes/forms/nav/footer/ads, scores text-density containers, resolves relative image/link URLs against the page's base URL, outputs clean sanitized HTML + metadata).
    3. Display the result in a dedicated sandboxed reader `WebContentView` overlay (with JS disabled and `load_html(...)`) layered over the tab — keeping the original page untouched and preventing any untrusted script execution in Reader mode.
- **Unit tests:** Extensive C++23 `ArticleExtractor` tests (article scoring, noise stripping, script/XSS sanitization, relative URL resolution, reading-time calculation, non-article detection) runnable locally via `./scripts/test-unit.sh` without a full browser build.

### M3 — Workspaces (Spaces)
*Inspired by Arc Spaces, Zen Workspaces, Vivaldi Workspaces, and Sidebery Panels.*
- **What it does:** Partition a single window's tabs into named contexts (*Default*, *Work*, *Research*, *Personal*, etc.), each with an icon and optional accent tint.
  - Switching workspaces hides tabs from other workspaces and shows only the active workspace's tabs (pinned tabs can be global or per-workspace).
  - Compact workspace switcher bar at the bottom of the vertical sidebar (or toolbar pill in horizontal tab mode), plus keyboard switching and Command Palette integration.
  - Works smoothly **both** with Tree-Style Tabs enabled (each workspace has its own tree forest) and with stock vertical/horizontal tabs.
- **Persistence:** Sidecar `Workspaces.db` (`Workspaces` table + `SessionTabId -> WorkspaceId` mapping), never modifying `Sessions.db`.
- **Unit tests:** Workspace model CRUD, tab assignment/reassignment, active tab fallback on switch, persistence round-trip.

### M4 — Split View (Tab Tiling)
*Inspired by Zen Split View, Vivaldi Tab Tiling, and Arc Split View.*
- **What it does:** View 2 tabs side-by-side (horizontal or vertical split), extendable to a 3–4 pane grid, inside the same browser window.
  - Trigger from tab context menu (*Split with Current Tab*), dragging a tab to the left/right edge of the viewport, or Command Palette (*Split View: Side by Side*, *Swap Panes*, *Unsplit*).
  - Uses a `QSplitter` container hosting the existing `Tab` / `WebContentView` widgets, with a subtle accent focus border on the active pane and a compact header strip for quick swap/unsplit.
  - Clicking inside any tiled pane makes it the active tab (updating the URL bar and toolbar cleanly).
  - Closing one tiled tab cleanly collapses the split back to normal single-tab view.
- **Upstream seam:** One small content-host seam in `TabWidget` so the central page area can switch between single-tab view and `SplitViewHost` without touching `WebContentView` internals.
- **Unit tests:** Split layout tree model (create split, focus switch, swap, close pane collapse, grid ratios).

### M5 — Glance (Quick Peek)
*Inspired by Zen Glance and Little Arc.*
- **What it does:** Preview any link without opening a full tab or cluttering your tab tree.
  - Trigger via `Alt/Opt + Click` on a link (or right-click -> *Peek in Glance*).
  - Opens a centered, rounded floating modal sheet (~80% viewport) hosting a lightweight `WebContentView` with a minimal top strip: page title/domain, **Open as Tab** (promotes it to a real tab — nested as a child if Tree Tabs is on), **Copy URL**, and **Close (`Esc` or click backdrop)**.
  - Navigating inside Glance stays contained inside the sheet until dismissed or promoted.
- **Upstream seam:** Intercept modified link-open request (or add a context-menu action) in `BrowserWindow`/`Tab` to hand the URL to `GlanceModule` when enabled.
- **Unit tests:** Glance state transitions (open, promote-to-tab handoff, dismiss cleanup, window resize clamping).

### M6 — Tree-Style Tabs Polish *(Already Shipped in v0)*
*Inspired by Sidebery & Tree Style Tab.*
- **Already shipped:** Nested tab trees in the vertical sidebar, collapse/expand with count badge, 3-zone drag-and-drop, orphan close policies, multi-select, branch context actions, sidecar SQLite persistence.
- **Wave 1 polish (overlay-only):**
  - Inline quick-filter box at the top of the tree sidebar (filter open tabs by title/URL without leaving the sidebar).
  - Subtle branch color tags (dot/bar accent) persisted in `TreeTabs.db`.

---

## Execution Order

| Milestone | Scope | Upstream Patch Footprint | Key Verification |
| :--- | :--- | :--- | :--- |
| **M0** ✅ | Tier 0: Rename to Superbird + coexistence (`Superbird` profile/bundle/lock) + `SuperbirdModule` registry + `about:settings -> Superbird` + migrate Tree Tabs | Small (Profile, App identity, 1 `WindowHost` seam, 1 Settings tab) | Both browsers run side-by-side; Tree Tabs toggleable on/off cleanly; green macOS + Linux CI |
| **M1** ✅ | Command Palette (`Cmd/Ctrl+K` / `F2`) + action bus | Tiny (1 shortcut hook) | Fuzzy/math unit tests + opens & executes actions on macOS + Linux |
| **M2** ✅ | Reader View (Hub IPC extraction + C++23 `ArticleExtractor` + reader overlay & typography controls) | Small (1 read-only Hub/RemoteView IPC message + address bar icon hook) | `ArticleExtractor` sanitization & scoring unit tests + green macOS + Linux CI |
| **M3** | Workspaces (Spaces) + `Workspaces.db` + sidebar/toolbar switcher | Small (`TabWidget` / `TreeTabView` filter seam) | Workspace model unit tests + works with both Tree Tabs and regular tabs |
| **M4** | Split View (2-pane / grid tab tiling) | Small (`TabWidget` page host seam) | Split layout unit tests + focus/close/resize stability |
| **M5** | Glance (Quick Peek floating sheet + promote to tab) | Tiny (link modifier / context menu hook) | Glance lifecycle tests + promotes cleanly into Tree Tabs |
| **M6** | Tree-Style Tabs Wave 1 polish (sidebar filter box + branch color tags) | None (overlay-only!) | TreeTabModel unit tests |

---

## Deferred to Wave 2+ (Out of Scope for Now)

Keeping Wave 1 tight means holding these until the modular platform and Wave 1 modules are rock-solid:
- Compact / Auto-Hide Chrome mode
- Persistent Web Panels & local Markdown Scratchpad
- Auto-Archive / Tab Lifecycle rules
- Named Session Snapshots & Export
- Vim Mode & Link Hints (`f`/`F` overlays)
- Per-site Boosts (user CSS / element zapper) & Command Chains
