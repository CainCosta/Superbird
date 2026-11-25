# M3 — Workspaces: design note

## Files
`overlay/UI/Qt/Superbird/Modules/Workspaces/`

| File | Qt? | Purpose |
|---|---|---|
| `WorkspaceModel.{h,cpp}` | no | Workspaces of one window: CRUD (names trimmed/capped on UTF-8 boundaries), tab assignment, visibility (pinned tabs global), last-active tab per workspace with fallback on switch, follow-on-activate, removal with tab migration, snapshot ↔ DB rows. |
| `WorkspacesActions.{h,cpp}` | no | Bus actions (New, Rename, Delete, Next, Previous, Move Tab to Next) plus nine "Switch to Workspace: <name>" actions whose titles track the active window's workspaces. |
| `WorkspaceStore.{h,cpp}` | LibDatabase | Sidecar `Workspaces.db`: `Workspaces(session_window_id, workspace_id, name, icon, tint, position, is_active)` and `WorkspaceTabs(session_tab_id → session_window_id, workspace_id)`. In-memory for private windows / non-persisting profiles. |
| `WorkspaceSwitcher.{h,cpp}` | yes | Sidebar footer: a tinted badge per workspace + "+", context menu (rename, icon, color, move, delete); collapses to the active badge on the narrow rail. |
| `WorkspacesModule.{h,cpp}` | yes | Per-window glue: keys tabs by SessionTabId (transient negative keys until assigned), two-phase switching (show target tabs → activate → hide others), stays in the workspace when its current tab closes, tab context menu "Move to Workspace", address-field badge with horizontal tabs, `Ctrl+Alt+1…9` (`Ctrl+1…9` on macOS), debounced persistence. |

## Seams
- **Core (generic):** `SuperbirdModule::is_tab_hidden()` / `on_tab_visibility_changed()` and `WindowHost::update_tab_visibility(HideTabs)`; `WindowHost::set_profile_data_directory()` / `persistent_data_directory()` for any module's sidecar DB.
- **Tree Tabs:** `TreeTabController` takes a hidden-tab predicate (installed once the window's `WindowHost` exists) and drops hidden tabs from its rows.
- **Patch 0010 (86 lines, approved larger than the roadmap's "small" seam):** Ladybird's vertical tab bar lays tabs out by index, so hiding tabs needed `TabBar` changes: hidden tabs take no row (rects, hit-testing, scroll extent, size hint), horizontal widths/end-room count visible tabs only, drop slots skip hidden tabs, `BrowserWindow::open_next/previous_tab` skip hidden tabs; plus `TabWidget::set_sidebar_footer_widget()` and one line in `Application::create_session_store()` handing the data directory to Superbird.

## Deviations
- Pinned tabs are global (visible in every workspace). Per-workspace pinned tabs would require reworking the pinned grid's index boundary in `TabBar`; the model supports it (`set_pinned_tabs_are_global`) but the module keeps it on.
- The horizontal-tabs "toolbar pill" is a workspace badge page action in the address field (reusing the M2 `add_page_action` hook) rather than a new toolbar seam.
