# M4 — Split View: design note

## Files
`overlay/UI/Qt/Superbird/Modules/SplitView/`

| File | Qt? | Purpose |
|---|---|---|
| `SplitLayout.{h,cpp}` | no | Split tree: leaves are panes, inner nodes split by orientation with normalized ratios (min 10%). Create, 2×2 grid, add pane (joins a same-orientation split or nests), remove pane with collapse, swap, replace, rotate, equalize, focus cycling. Max 4 panes. |
| `SplitViewActions.{h,cpp}` | no | Bus actions: Side by Side, Stacked, Grid, Add Next Tab, Swap, Rotate, Focus Next Pane, Equal Sizes, Unsplit. |
| `SplitViewHost.{h,cpp}` | yes | Nested `QSplitter`s mirroring the layout; each pane is a frame with a slim header (title, swap, unsplit, remove) holding a borrowed `Tab`; focused pane gets an accent border. Clicks/focus in a pane activate its tab. |
| `SplitViewModule.{h,cpp}` | yes | Lends the group's tabs to the host, shows/suspends the split as tabs are activated (the group survives visits to other tabs), collapses panes as tabs close, handles tab replacement (discard), steps aside when a pane's tab is hidden by Workspaces, tab menu "Split with Current Tab" / "Add to Split View", drag-to-edge. |

## Seam (verified upstream)
`TabWidget` derived tab indices from its `QStackedWidget`, so a page couldn't leave the stack without breaking `tab(index)`/`index_of`. **Patch 0011 (+70/−25, user-approved over the "small" budget):** `TabWidget` keeps its own `Vector<Tab*>` in tab-bar order (the stack just holds pages), plus a generic page host: `set_page_host(QWidget*)`, `lend_page(Tab&)`, `return_page(Tab&)`. It also adds `TabBar::on_tab_dropped_outside_strips`, consulted before a tab dropped away from every strip detaches into a new window — that's how dropping a tab within 30% of a page edge splits instead.

## Test plan
`tests/TestSplitLayout.cpp`: create, focus switch/cycling, add up to 4 (join vs nest), grid shapes, swap keeps ratios with places, replace, close-pane collapse down to one pane, ratio redistribution and normalization (min share, NaN/negative), rotate; action-table availability.

## Limitations
No drop indicator while dragging toward an edge (the drag is an AppKit/QDrag session owned by `TabBar`). Split groups aren't persisted across restarts.
