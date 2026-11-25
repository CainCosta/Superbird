# Proposal — uBlock Origin–grade content blocking

*Status: planned; decisions settled (see the end). Not started. Researched 2026-11-25 against upstream `e99ae68e` and `adblock` 0.13.5.*

## TL;DR
Don't try to run the uBlock Origin **extension**. Ladybird has no WebExtensions runtime, and uBO needs blocking `webRequest`, content scripts in isolated worlds, `tabs`, `storage`, messaging, and extension pages. That is a multi-month platform project before uBO even loads.

Instead, **make Superbird's engine run uBO's filter lists and scriptlets the way uBO does**, which is what Brave does. Upstream already gets us about half of the way there:

- Ladybird ships **Brave's `adblock-rust`** (`Libraries/LibWeb/ContentBlocker/Rust`, crate `adblock = "0.13.5"`). That is a uBO-syntax-compatible engine.
- It has filter-list subscriptions with daily updates, custom filters, per-site off switches, a blocked-resources list in the site popover, and cosmetic hiding that knows class and id names.
- The C++ wrapper **throws away** most of what the engine can do. It calls only `check_network_request(..).should_block()` plus CSS hiding. Scriptlets, `$redirect`, `$removeparam`, `$csp`, procedural cosmetics, and trusted lists are all unused.

The work splits into five phases. Phase 1 (scriptlets) is the biggest payoff, because it is what defeats YouTube-style ads and anti-adblock walls.

## What upstream has vs. what uBO does

| Capability | uBO | Ladybird today | Engine supports it? | Plan |
|---|---|---|---|---|
| Network block/allow, `$important`, `$badfilter`, `3p`, `domain=`, types | ✅ | ✅ | ✅ | — |
| Request method (`$method=`) | ✅ | ❌ (`""` passed) | ✅ | Phase 2 |
| Cosmetic `##sel` (specific + generic, `generichide`) | ✅ | ✅ | ✅ | — |
| `:style()` | ✅ | ✅ (via `as_css()`) | ✅ | — |
| Procedural `:has-text :upward :xpath :matches-css :min-text-length :matches-path`, actions `:remove() :remove-attr :remove-class` | ✅ | ❌ (dropped unless pure CSS) | parsed, JSON | Phase 3 |
| **Scriptlets `##+js(...)`** (~9.6k rules in uBO filters alone) | ✅ | ❌ (`injected_script` ignored, no resources loaded) | ✅ | **Phase 1** |
| Trusted scriptlets (`trusted-*`, needed for YouTube) | ✅ uBO lists only | ❌ | ✅ `PermissionMask` | Phase 1 |
| `$redirect` / `$redirect-rule` (neutered scripts, 1×1 gifs, etc.) | ✅ | ❌ (plain block) | ✅ `BlockerResult.redirect` | Phase 2 |
| `$removeparam` | ✅ | ❌ | ✅ (simple patterns) | Phase 2 |
| `$csp=` | ✅ | ❌ | ✅ `get_csp_directives` | Phase 2 |
| `!#if` / `!#else` / `!#endif` preprocessing | ✅ | ❌ (treated as comments, so **both branches load**) | ❌ | Phase 0 |
| `!#include` | ✅ | ❌ | ❌ | Avoided by using uBO's CDN builds, which inline includes |
| HTML filtering `##^`, `responseheader()`, `$replace` | Firefox only | ❌ | ❌ | Out of scope (uBO on Chromium lacks these too) |
| Dynamic filtering, per-site switches, element picker, logger | ✅ | per-site on/off only | n/a | Phase 4 (Qt module) |

Measured against today's uBO lists:
- `filters.min.txt`: 26k lines, 9,644 `##+js` rules, 302 `!#if` blocks.
- The top-level uBO lists: 620 `$redirect` rules, 237 `$removeparam` rules, 263 procedural filters.
- The headline YouTube rule is `trusted-replace-xhr-response`. It sits inside `!#if !cap_html_filtering`, so it needs both Phase 0 and Phase 1.

## Why not other routes
- **WebExtensions MV2 to run uBO itself.** It needs an extension process model, isolated JS worlds (LibJS/LibWeb have none), blocking `webRequest`, `tabs`, `storage`, `scripting`, `runtime` messaging, `browserAction` popups, and `chrome-extension://` pages. It's worth having someday, but it is not a milestone.
- **uBO Lite (MV3).** It needs `declarativeNetRequest` and the `scripting` API. That is a smaller surface, but it is still an extension runtime. It is also strictly weaker than the engine route.
- **Rewriting the matcher.** No. `adblock-rust` is already uBO-compatible, fast, and in-tree.

## Architecture

```
UI process (LibWebView::Application)                     WebContent (per page process)
─────────────────────────────────────                    ──────────────────────────────
lists/*.txt  ─┐                                          ContentBlocker::the()
custom rules ─┼─► bundle buffer (one AnonymousBuffer) ──►  └─ Rust: superbird bundle → Engine
resources.json┘   (existing set_content_blockers IPC)         ├─ check(url, src, type, method)
                                                               │    → Allow | Block | Redirect(data:) | Rewrite(url)
                                                               ├─ cosmetic CSS (existing)
                                                               ├─ scriptlet source for URL ──► run in page realm
                                                               ├─ procedural filters JSON ───► ProceduralFilterRunner
                                                               └─ CSP for document requests
```

**Bundle format, so the IPC stays the same.** Today the buffer is every list concatenated. We prefix each list with a comment marker and add one trailing resources section:

```
!superbird:list id=ubo-filters trust=1
...list text...
!superbird:list id=easylist trust=0
...
!superbird:resources
[{"name":"set-constant.js", ...}]   ← adblock-rust `Resource` JSON
```

The markers are `!` comments, so a stock engine still parses the buffer correctly. The Superbird Rust code splits on them, gives each list its own `ParseOptions { permissions }`, preprocesses `!#if`, and calls `engine.use_resources(..)`. The existing `set_content_blockers` IPC is unchanged.

### Scriptlet resources (the uBO part)
uBO's scriptlets live in `gorhill/uBlock/src/js/resources/*.js`. They are ES modules with `builtinScriptlets` entries carrying `name`, `aliases`, `fn`, `dependencies`, and `requiresTrust`. `adblock-rust` wants `Resource { name, aliases, kind: "application/javascript" | "fn/javascript", content: base64, dependencies, permission }`. Its line-based `assemble_scriptlet_resources` is deprecated because it cannot read the new format. Brave converts them with JS at build time.

Brave's published `adblock-resources/dist/resources.json` holds **only 24 Brave-specific resources** (checked), not uBO's. So we need our own converter:

- `scripts/build-ubo-resources.mjs` (Node, dev/CI only). It checks out a **pinned uBO tag** and imports `scriptlets.js`. It emits one resource per scriptlet, with `requiresTrust` mapped to `permission: 1` and `fn` dependencies as `fn/javascript`. It also adds the `web_accessible_resources` files indexed by `redirect-resources.js`, which are the `$redirect` targets. Expected output is about 1–2 MB.
- **Distribution:** uBO's code and uAssets are **GPLv3**, and Superbird/Ladybird is BSD-2. Keep the GPL JS out of the binary. Publish `ubo-resources.json` as a separately downloaded asset (for example a Superbird GitHub release or Pages file) that is fetched and refreshed like a filter list into `ContentBlocking/Resources/`. That mirrors how upstream already downloads GPL filter lists at runtime. *Get a licensing sanity check before shipping; this is not legal advice.*
- **Naming:** call the feature "uBO-compatible" content blocking. Do not call it "uBlock Origin", which is gorhill's project and name.

## Phases

### Phase 0 — uBO lists, done right (small; overlay-heavy)
- Add uBO's default set to the list catalogue. Use the **CDN builds**, which inline `!#include`:
  - uBlock filters: `https://ublockorigin.github.io/uAssetsCDN/filters/filters.min.txt`
  - Badware, Privacy, Unbreak, Quick fixes: the same `uAssetsCDN` paths
  - Peter Lowe's list
  - URLhaus
  - EasyList and EasyPrivacy, which are already present
- **First-run defaults** switch from EasyList+EasyPrivacy to uBO's default set (decided). `welcome.js` already enables lists by identifier, so this is a one-line list change in the 0017 seam.
- `!#if` preprocessor in Rust, using uBO's semantics: unknown tokens are false, `!` negates, and `&&`/`||` are supported. Superbird's env: every `env_*` false, `ext_ubol` false, `cap_html_filtering` false, `cap_ipaddress` false. Run it per list before parsing.
- **Seams:**
  - **0017 Settings catalogue** (~15 lines): either the extra `add_list` entries or a hook so the Superbird module can register lists. A hook is preferred.
  - **0018 Rust hook** (~5 lines): `mod superbird;` in `lib.rs`, plus `rust_content_blocker_create` delegating when the buffer starts with `!superbird:`. A child module can reach the parent's private `ContentBlockerEngine` fields, so the real code lives in `overlay/Libraries/LibWeb/ContentBlocker/Rust/superbird.rs`. *Verify that cbindgen follows `#[path]` modules. If it doesn't, the hook declares the FFI functions in `lib.rs` and forwards.*
  - **0019** `Application::load_content_blocker_lists` (~20 lines): write the list markers and trust flags.
- **Done when** the uBO lists load with correct `!#if` branches, and blocking parity on network and cosmetic rules is confirmed on public ad-block test pages.

### Phase 1 — Scriptlets (+ trusted lists)
- The bundle carries `resources.json`, and the engine runs `use_resources`. uBO's own lists get `trust=1`. "My filters" gets trust only behind an opt-in toggle in settings, as uBO does.
- `ContentBlocker::scriptlet_source_for_url(url)` returns `url_cosmetic_resources(url).injected_script`.
- **Injection point:** once per `Document`, right after `Document::create_and_initialize` for HTML and XHTML documents (`DOM/DocumentLoading.cpp`), and before the parser runs any page script:
  - Run it as a `ClassicScript` in the document's realm (uBO injects scriptlets in the MAIN world too, so no isolated world is needed).
  - Skip it when content blocking is off for the top-level site, or when the view's scripting switch is off (patch 0009).
  - Cover iframes. Each document gets its own pass, including `about:blank` and `srcdoc` documents, which inherit from the parent's URL as uBO does with `match_about_blank`.
  - Bypass page CSP, because the script is not created through a `<script>` element.
- **Seam 0020** (~40 lines in LibWeb): the `ContentBlocker` API plus one call in `DocumentLoading.cpp`.
- **Tests:**
  - LibWeb text tests through `internals` set rules plus a tiny resource set. They check that `set-constant` and `abort-on-property-read` take effect before an inline script, inside iframes, and not on disabled sites.
  - A Rust unit test checks that a trusted scriptlet from an untrusted list is refused.
- **Done when** YouTube video ads and the common anti-adblock walls behave like uBO on Chromium.

### Phase 2 — Network actions: redirect, removeparam, CSP, method
- Replace `is_filtered()` with `check()`, which returns `{Allow, Block, Redirect(data_url), Rewrite(url)}`. Keep `is_filtered()` as a thin wrapper so upstream callers keep compiling.
- `ResourceLoader` acts on the result:
  - **Redirect:** load the engine-provided `data:` URL instead of failing. That covers neutered `googletagmanager.js`, `noop.js`, `1x1.gif`, and similar, and it stops pages breaking where uBO's redirects prevent breakage.
  - **Rewrite:** reissue the request with the param-stripped URL, including top-level navigations.
- Pass the HTTP method into `Request::new`.
- For document responses, append `get_csp_directives()` as an extra `Content-Security-Policy` in navigation response processing.
- **Seam 0021** (~50 lines across `ResourceLoader.cpp` and `Fetching.cpp`/navigation). This one is generic, but it is carried as our own patch (see Decisions).

### Phase 3 — Procedural cosmetic filters
- `ProceduralFilterRunner` (C++, per `Document`) consumes the `procedural_actions` JSON the engine already returns:
  - Operators: `has-text`, `upward`, `xpath`, `matches-css(-before/-after)`, `matches-attr`, `matches-path`, `min-text-length`.
  - Actions: hide, `remove`, `remove-attr`, `remove-class`, `style`.
- It re-evaluates on DOM mutation, batched about every 50 ms as uBO does, and stops after N idle passes.
- Hiding uses a UA-level style via the existing content-blocker stylesheet plus an internal marker attribute, not inline `style`.
- It is implemented in C++ rather than injected JS, so pages can't observe or tamper with it (there are no isolated worlds).
- **Seam:** one hook where the content-blocker stylesheet is installed. The runner lives in `overlay/Libraries/LibWeb/Superbird/` with one CMake line.

### Phase 4 — uBO-style UI (Qt module `Modules/ContentBlocking/`)
- **Toolbar shield popover**, which extends the existing site popover snapshot:
  - Blocked count for the page and the session.
  - Per-site power switch, which already exists.
  - uBO's per-site switches: no popups, no large media, no remote fonts, no JS (the last reuses the scripting switch). They are stored in a sidecar `ContentBlocking.db` and compiled to `$important` rules for that site.
- **Element zapper and picker:** "Block element…" in the page context menu. It asks WebContent for a robust selector of the clicked node (a small Hub IPC like Reader View's 0007) and appends `site##selector` to custom filters. It also covers the Wave 2 "element zapper" item.
- **Logger:** a per-tab request log with the matched filter. It requires building the engine with `debug-info` or `filter` names, so make it toggleable.
- **Command Palette actions:** Toggle blocking on this site, Pick element, Open logger, Update filter lists.
- **Unit tests:**
  - `!#if` evaluator
  - bundle encode/decode
  - per-site switch → rule compiler
  - selector generator
  - These sit alongside `tests/TestContentBlocking.cpp`.

### Phase 5 — Performance
- Today every WebContent process parses all lists from text. With uBO's set (about 180k rules) that costs tens of MB and hundreds of ms per process. Fix it with **build once, ship serialized**:
  - The UI process builds the engine and sends `Engine::serialize()`, which is a flatbuffer. WebContent calls `deserialize`, then `use_resources`, and rebuilds the generic-selector side table from a serialized form as well.
  - Cache it on disk keyed by list hashes, as Brave does.
- Budget: engine load under 30 ms per process, and `check()` p99 under 50 µs. Measure with the existing benches as a guide.

## Upstream strategy
- **Carry everything as Superbird patches** (decided 2026-11-25). That includes Phase 2's generic plumbing (`check()` with redirect/rewrite/CSP/method) and Phase 1's injection seam. Offering pieces upstream later is optional, not a dependency.
- Keep the patches as small seams. Real code lives in `overlay/` (Rust `superbird.rs`, LibWeb runner, Qt module).
- Churn risk is low. The content-blocker files saw about 1 upstream commit in the last ~4 months.

## Decisions (2026-11-25)
1. **Hosting `ubo-resources.json`:** publish it as a separately hosted Superbird asset (release asset), downloaded and refreshed at runtime. It is never compiled into the binary. A licensing check still needs to happen before the first public release that uses it.
2. **Defaults:** uBO's default list set is **on at first run**.
3. **Trusted "My filters":** **off by default**, with an opt-in toggle in settings.
4. **Phase 2 (and everything else):** carried as **Superbird's own patches**, not upstream-first.
