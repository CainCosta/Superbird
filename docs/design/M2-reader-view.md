# M2 — Reader View: design note

## Files
`overlay/UI/Qt/Superbird/Modules/ReaderView/`

| File | Qt? | Purpose |
|---|---|---|
| `HtmlTree.{h,cpp}` | no | Forgiving HTML tokenizer + tree builder (void/raw-text elements, implied end tags, character references, depth cap 256, 8 MiB cap). Never executes or fetches. |
| `UrlResolver.{h,cpp}` | no | RFC 3986 reference resolution with browser cleanup (tabs/newlines/C0 stripped), and scheme allow-lists: links → http/https/mailto/#fragment, images → http/https. |
| `ArticleExtractor.{h,cpp}` | no | Metadata (og/meta/`<title>`/byline/site), pruning (chrome tags, hidden, ARIA roles, unlikely class/id), Readability-style paragraph scoring with link-density, sibling gathering, allow-list serializer, word count + reading time, Firefox-style `is_probably_readerable`. |
| `ReaderDocument.{h,cpp}` | no | Reader page builder: strict CSP meta, typography (Serif/Sans/Mono, 14–32px, line height 1.2–2.2, Narrow/Medium/Wide) and themes (System via `prefers-color-scheme`, Light, Sepia, Dark), escaped metadata header. |
| `ReaderViewActions.{h,cpp}` | no | Action table: Toggle, Larger/Smaller text, Next font/theme/width. |
| `ReaderOverlay.{h,cpp}` | yes | A `WebContentView` (`is_tab=false`, scripting off) laid exactly over the tab's view, plus floating controls (`Theme::panel()` colors, `ChromeIcon::Close`). Web-URL loads in it (link clicks) are handed to the real tab. |
| `ReaderViewModule.{h,cpp}` | yes | Per-tab state, readerability check on load finish of the current tab, `ChromeIcon::Document` page action in the address field, `Ctrl+Alt+R` (`Ctrl+Cmd+R` on macOS) reserved shortcut, bus handler. |

## Seams (upstream verified)
Upstream matches the roadmap's description: the Qt UI talks to WebContent through the Hub (`RemoteView` → `HubConnection` → `Hub::ConnectionFromUI` → `ViewImplementation` → WebContent). The page-dumps feature already round-trips `request_internal_page_info`, so the reader snapshot mirrors it:

- **Patch 0007 (48 lines):** `PageInfoType::DocumentHTML` (WebContent serializes `document_element()->outer_html()`), one Hub message pair `view_request_reader_document` → `view_reader_document`, `RemoteView::request_reader_document()` (reuses the page-dumps promise map, so no new cleanup path).
- **Patch 0008 (41 lines):** `LocationEdit::add_page_action(QAction*)` — generic page-action buttons placed with the translate button.
- **Patch 0009 (7 lines):** `ViewImplementation` remembers a per-view `scripting` switch and re-applies it to every page the view gets. Without it, `apply_view_options` would turn scripting back on whenever the reader view moved to a new page (e.g. a process swap), so "JS disabled" would not be a guarantee.

## Security
Three independent walls: (1) the extractor's allow-list serializer (no script/style/iframe/object/embed/form/svg/math/template/meta/base, no attributes but a vetted few, `javascript:`/`data:`/`vbscript:`/`file:` refused however spelled); (2) a `default-src 'none'; script-src 'none'` CSP on the reader page; (3) scripting disabled for the reader view on every page it uses. `tests/TestReaderView.cpp` covers (1) and (2) explicitly.

## Deviations
- Shortcut: `Cmd+Option+R` and `Cmd+Shift+R` already mean "reload bypassing cache" on macOS, so Reader View uses `Ctrl+Cmd+R` there (`Ctrl+Alt+R` elsewhere, as planned).
- Preferences are app-wide for the session (in memory); not persisted.
