# M6 — Tree-Style Tabs polish: design note

Overlay only (no patches).

- **Quick filter:** a field at the top of the tree sidebar (`TreeTabView`'s viewport margin; hidden on the collapsed rail). `TreeTabs::tab_matches_filter()` (pure: every word in title or URL, ASCII case-insensitive) and `TreeTabModel::filtered_rows()` (matches plus ancestors, branches opened, guides recomputed; stored collapse state untouched). Esc clears, Enter opens the first match. Bus action "Filter Tabs in Sidebar" focuses it.
- **Branch color tags:** `BranchColor` on `TreeNode` (own tag; `effective_branch_color` inherits down the branch until a descendant has its own), carried in `VisibleTreeRow` and `PersistedTreeNodeState`. Drawn as a 3px bar at the row's leading edge. Set from the tab menu's "Branch Color" submenu.
- **Persistence:** `TreeTabs.db` migration v2 `ALTER TABLE TreeTabNodes ADD COLUMN color INTEGER NOT NULL DEFAULT 0` (additive; old rows read untagged; unknown values decode as no color).

Tests (`tests/TestTreeTabModel.cpp`): tag inheritance/override, rows carry effective color, snapshot → apply round trip, storage decoding, tags survive tab replacement, filtered rows (deep match through a collapsed branch, last-sibling recomputation, none/all), filter text matching.
