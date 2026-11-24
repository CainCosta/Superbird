# Sidelady

A minimal-diff overlay + patch-stack fork of [Ladybird](https://github.com/LadybirdBrowser/ladybird) whose sole addition is a native, first-party-feeling **Tree-Style Tabs** mode (distilling the core ergonomics of Tree Style Tab and Sidebery) integrated directly into Ladybird's Qt6 vertical sidebar and `about:settings`.

## Repository Layout

```text
Sidelady/
├── upstream/                        # Pinned LadybirdBrowser/ladybird submodule
├── overlay/                         # 100% new files (linked into upstream/)
│   └── UI/Qt/TreeTabs/              # Self-contained native Qt6 Tree Tabs subsystem
├── patches/                         # Minimal numbered seam patches (<260 LOC total)
├── scripts/
│   ├── apply.sh                     # Apply patches/ + link overlay/ onto upstream/
│   ├── reset-upstream.sh            # Restore upstream/ to clean pinned commit
│   ├── export-patches.sh            # Regenerate patches/ from upstream/ sidelady branch
│   ├── sync-upstream.sh             # Bump upstream/ to latest master & test patches
│   └── build.sh                     # Apply + build Ladybird (update checks disabled)
└── .github/workflows/
    └── upstream-sync-check.yml      # Weekly check that patches apply cleanly to master
```

## Tier 1 Features

- **Native Settings Integration:** Lives in `about:settings` → **Appearance → Layout** (right alongside *Tabs on the side* and *Everything in the sidebar*) and in the **Tab Layout** window context menu.
- **Automatic Opener Nesting:** Links opened in new tabs (`target="_blank"`, middle-click, Ctrl/Cmd+click, `window.open()`, Duplicate Tab) automatically nest under their opener tab.
- **Collapsible Branches:** Indented tree rows with twisty chevrons, subtle tree guides matching `Ladybird::Theme::chrome()`, descendant-count pills when collapsed, and active/audio/attention roll-up indicators.
- **3-Zone Drag & Drop:** Drop above (sibling before), middle (nest as child), or below (first child if expanded, sibling after otherwise), auto-expand on drag hover, subtree drag (hold `Alt` to drag a single tab), and drag-out to detach a tab or branch into a new window.
- **Orphan-on-Close Policies:** Configurable in Settings (*Promote first child*, *Attach to grandparent*, or *Close whole branch*) with confirmation guard for multi-tab collapsed branches.
- **Multi-Select & Branch Actions:** `Ctrl/Cmd+Click` and `Shift+Click` selection plus native context menu actions (*New Child Tab*, *Collapse/Expand Branch*, *Unload Branch*, *Bookmark Branch…*, *Move Branch to New Window*, *Close Branch*).
- **Sidecar Persistence:** Stores tree topology and collapsed states in a separate SQLite database (`TreeTabs.sqlite`) keyed by `SessionTabId`, without modifying upstream `SessionStore` schemas.

## Quick Start

```bash
# 1. Apply overlay + patches onto upstream/
./scripts/apply.sh

# 2. Build (Release or Debug)
./scripts/build.sh Release

# 3. Sync to a newer upstream Ladybird commit
./scripts/sync-upstream.sh
```

## Updating Patches

If you edit upstream seam files inside `upstream/` on the `sidelady` branch, commit your changes cleanly and run:

```bash
./scripts/export-patches.sh
```

Overlay files in `overlay/UI/Qt/TreeTabs/` are symlinked directly into `upstream/UI/Qt/TreeTabs/` and excluded from `upstream/` git tracking, so edits there are immediately tracked by the Sidelady repo.
