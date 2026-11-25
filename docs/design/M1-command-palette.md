# M1 — Command Palette: design note

## Files
`overlay/UI/Qt/Superbird/Modules/CommandPalette/`

| File | Qt? | Purpose |
|---|---|---|
| `FuzzyMatcher.{h,cpp}` | no | Multi-token, case-insensitive subsequence scorer (O(m·n) DP with prefix, word-boundary, camelCase and consecutive bonuses, gap penalties) returning match positions for highlighting. |
| `MathExpression.{h,cpp}` | no | Recursive-descent evaluator for `=` queries: `+ - * / % ^`, parentheses, unary signs, `pi`/`e`, and `sqrt abs round floor ceil ln log sin cos tan`. Result formatting. |
| `PaletteRanking.{h,cpp}` | no | Query parsing (`=` → math), URL-vs-search detection, URL de-duplication across tabs/bookmarks/history, and ranking (fuzzy score + kind weight, stable tie-breaks). |
| `CommandPaletteActions.{h,cpp}` | no | Qt-free table of browser commands (New Tab, Close Tab, Toggle Force Dark, Open Settings, ...) registered on the `ModuleRegistry` bus through a handler interface, plus `collect_action_entries()` (enabled + available actions only). |
| `CommandPalettePopover.{h,cpp}` | yes | The floating sheet: a `Ladybird::Popover` subclass that reuses the All Tabs popover's style sheet object names, so it is styled by `ChromeStyle`/`Theme::panel()` in light and dark. |
| `CommandPaletteModule.{h,cpp}` | yes | `SuperbirdModule`: per-window `Cmd/Ctrl+K` + `F2` shortcuts (only while enabled), gathers tabs/bookmarks/history/actions, executes the picked entry. |

## Seams
- **Patch (tiny):** `UI/Qt/WebContentView.cpp` — `is_browser_reserved_shortcut()` also asks `Superbird::WindowHost::is_reserved_shortcut(event)`. Without it a focused page swallows `Cmd+K`/`F2` via `ShortcutOverride`.
- **Core (generic, reusable):** `WindowHost::reserve_shortcut()/release_shortcut()/is_reserved_shortcut()` — a ref-counted, app-wide set of key sequences that modules reserve while enabled. Later modules (Reader View, Workspaces, Split View) reuse it.
- **Core (generic):** `SuperbirdModule::describe_tab(Tab&)` → short context label (Tree Tabs: parent branch; Workspaces: workspace name), fanned out by `WindowHost::tab_context(Tab&)`. The palette shows it next to each open tab.

## Test plan (`tests/TestCommandPalette.cpp`)
- Fuzzy: subsequence required, case-insensitive, prefix/boundary/consecutive ordering, multi-token any order, positions, long-candidate cap.
- Ranking: kind weights, stable tie-breaks, empty query order, limit, URL de-dup.
- Math: precedence, associativity (`^` right), unary, functions/constants, division by zero, malformed input, formatting.
- Actions: registry filtering hides disabled modules and unavailable actions; browser action table well-formed and registered.

## Patch footprint
One small hunk in `WebContentView.cpp` (include + 2 lines). Roadmap: "Tiny (1 shortcut hook)".
