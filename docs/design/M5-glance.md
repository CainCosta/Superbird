# M5 — Glance: design note

## Files
`overlay/UI/Qt/Superbird/Modules/Glance/`

| File | Qt? | Purpose |
|---|---|---|
| `GlanceModel.{h,cpp}` | no | State machine (Closed ⇄ Open; open/navigated/title/promote/dismiss/opener_closed), http(s)/about: only, display title/host, and `sheet_rect()` (80% of the page area, centered, min 360×280, 16px margins, clamped for tiny windows). |
| `GlanceActions.{h,cpp}` | no | Bus actions while open: Open as Tab, Copy URL, Close. |
| `GlanceOverlay.{h,cpp}` | yes | Dimmed backdrop + rounded sheet over the tab (registered as a floating surface of the page view), strip with title/site, Open as Tab, Copy URL, Close, and its own `WebContentView` (`is_tab=false`, so navigation stays in the sheet). Esc / backdrop click dismiss. |
| `GlanceModule.{h,cpp}` | yes | Alt/Option+click interception on page views (swallows press+release when a link is hovered), "Peek in Glance" link menu item, promote → `on_pending_opener(opener)` + `new_tab_from_url(after opener)` so Tree Tabs nests it, dismiss on tab switch/close. |

## Seam (verified upstream)
Alt+click is consumed inside LibWeb (only Ctrl/middle clicks reach the UI), so intercepting the modified click there would need LibWeb + Hub changes. Instead the UI-side hover state is used. **Patch 0012 (+6):** `Tab::hovered_link()` (the link the hover label already shows), `ActionID::SuperbirdModuleAction`, and one `WindowHost::extend_link_context_menu()` call in `ContextMenus.cpp`'s link menu (a generic hook any module can use).

## Test plan
`tests/TestGlance.cpp`: open/refuse schemes, navigation inside the sheet, re-open replaces, promote hands off the latest URL + opener and cleans up, dismiss cleanup, opener closed, sheet rect at roomy/small/tiny/empty sizes, URL helpers, actions only while open.
