# Sidelady

A small fork of [Ladybird](https://github.com/LadybirdBrowser/ladybird) that adds tree-style tabs (like Tree Style Tab / Sidebery) to the vertical sidebar.

Trying to keep it as close to regular Ladybird as possible so updating upstream isn't a nightmare. All the new code lives in `overlay/UI/Qt/TreeTabs/` and gets patched onto the `upstream/` submodule with a few small patches.

## What it adds

- Tree tabs toggle right in `about:settings` (under Appearance -> Layout, when vertical tabs are on) and in the right-click Tab Layout menu
- Links opened in a new tab nest under the tab you opened them from
- Collapse/expand tab branches (with a count badge when collapsed)
- Drag and drop tabs to reorder or nest them under another tab (hold Alt to drag just one tab instead of the whole branch)
- Setting for what happens when you close a parent tab (promote first child, move children up, or close the whole branch)
- Multi-select tabs (`Ctrl`/`Cmd` + click or `Shift` + click) and right-click branch actions (close, unload, bookmark folder, move to new window)
- Saves tree state across restarts in its own small SQLite db (`TreeTabs.sqlite`)

## Building

Make sure you have Ladybird's build dependencies installed first (see `upstream/Documentation/BuildInstructionsLadybird.md`).

```bash
git clone --recurse-submodules https://github.com/CainCosta/Sidelady.git
cd Sidelady

./scripts/apply.sh
./scripts/build.sh Release
```

## Working on it

- `./scripts/apply.sh` — applies the patches in `patches/` and symlinks `overlay/` into `upstream/`
- `./scripts/test-unit.sh` — runs the C++ unit tests for the tree model without needing a full browser build
- `./scripts/export-patches.sh` — regenerates `patches/` if you changed any upstream files on the `sidelady` branch
- `./scripts/sync-upstream.sh` — pulls newer Ladybird `master` and checks if the patches still apply
- `./scripts/reset-upstream.sh` — puts `upstream/` back to a clean state
