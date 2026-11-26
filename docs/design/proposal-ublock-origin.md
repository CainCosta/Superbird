# Proposal — uBO-compatible content blocking

*Status: **Shipped (M0–M6)** — implemented, benchmarked, and browser-verified 2026-11-25 against upstream `e99ae68e`, `adblock` 0.13.5, and uBO `1.77.2` resources. See [Shipped Implementation & Verification](#shipped-implementation--verification-2026-11-25) below for exact patches, divergences from the initial draft, measured numbers, and known gaps.*

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
- **Update cadence.** Upstream refreshes every list once a day, which is too slow for YouTube. uBO's Quick fixes list is patched within hours; its header advertised `Diff-Expires: 317 minutes` when checked. Honor each list's `! Expires:` header, with a floor of about 1 hour, instead of a fixed 24 h.
- **First-run defaults** switch from EasyList+EasyPrivacy to uBO's default set (decided). `welcome.js` already enables lists by identifier, so this is a one-line list change in the 0017 seam.
- `!#if` preprocessor in Rust, using uBO's semantics: unknown tokens are false, `!` negates, and `&&`/`||` are supported. Superbird's env: every `env_*` false, `ext_ubol` false, `cap_html_filtering` false, `cap_ipaddress` false. Run it per list before parsing.
- **Seams:**
  - **0017 Settings catalogue** (~15 lines): either the extra `add_list` entries or a hook so the Superbird module can register lists. A hook is preferred.
  - **0018 Rust hook** (~5 lines): `mod superbird;` in `lib.rs`, plus `rust_content_blocker_create` delegating when the buffer starts with `!superbird:`. A child module can reach the parent's private `ContentBlockerEngine` fields, so the real code lives in `overlay/Libraries/LibWeb/ContentBlocker/Rust/superbird.rs`. *Verify that cbindgen follows `#[path]` modules. If it doesn't, the hook declares the FFI functions in `lib.rs` and forwards.*
  - **0019** `Application::load_content_blocker_lists` (~20 lines): write the list markers and trust flags.
- **Done when** the uBO lists load with correct `!#if` branches, and blocking parity on network and cosmetic rules is confirmed on public ad-block test pages.

### Phase 1 — Scriptlets (+ trusted lists)
- **Keep resources in step with the lists.** uBO's lists start using new scriptlets (for example `json-edit-fetch-response`) soon after a uBO release. If our pinned `resources.json` is older, those rules silently do nothing. A CI job rebuilds and republishes the resources on every uBO release tag, and the browser refreshes them like a list.
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
5. **Mode switch:** `about:settings -> Superbird` exposes a live **uBO-compatible** vs **Default Ladybird** engine mode switch (`content_blocking.engine_mode`) so users can compare or fall back without restarting.

---

## Shipped Implementation & Verification (2026-11-25)

### Patch & Overlay Footprint

All upstream changes are carried as seven small seam patches (`+712 / -63` lines total); the engine integration, bundle/expiry logic, procedural cosmetic runner, Qt module, resource pipeline, and tests live in `overlay/`, `scripts/`, and `tests/` (`3,703` lines total). Zero GPLv3 filter lists, scriptlet JS files, or `ubo-resources.json` files are committed or compiled into binaries.

| Patch / Location | Size | Role |
|---|---|---|
| `patches/0022-LibWeb-ContentBlocker-hook-Superbird-uBO-compatible-.patch` | `+20 / -2` | Rust `mod superbird` hook + `ContentBlocker::is_ubo_mode()` |
| `patches/0023-LibWebView-wire-Superbird-uBO-compatible-lists-expir.patch` | `+176 / -41` | Default list registration, `! Expires:`/`! Diff-Expires:` cadence, `ubo-resources.json` download, bundle construction, mode switch wiring |
| `patches/0024-LibWeb-inject-uBO-compatible-scriptlets-at-document-.patch` | `+105 / -11` | Document-start scriptlet injection across main docs, `<iframe>`, `srcdoc`, and initial `about:blank` + `Internals` hooks |
| `patches/0025-LibWeb-support-uBO-compatible-redirects-removeparam-.patch` | `+261 / -1` | `$redirect` synthetic HTTP 200 responses, `$removeparam` URL rewriting, `$csp` header injection, HTTP method forwarding, WebSocket blocking |
| `patches/0026-LibWeb-ContentBlocker-keep-procedural-cosmetic-selec.patch` | `+4 / -1` | Filters procedural cosmetic selectors out of plain CSS stylesheet injection in `lib.rs` |
| `patches/0027-UI-Settings-show-uBO-compatible-engine-status-and-pe.patch` | `+81 / -5` | Engine status on `about:settings#blocking` and `SiteSettingsPopover` + per-site **Cosmetic filtering** toggle |
| `patches/0028-LibWeb-LibWebView-add-binary-snapshot-cache-cosmetic.patch` | `+65 / -2` | `SBCE0001` snapshot cache hook, opt-in filtering logger wiring, `internals.contentBlockerFilterLog()` / `contentBlockerStats()` |
| `overlay/Libraries/LibWeb/ContentBlocker/Rust/src/superbird.rs` | `2,284` lines | `!#if` evaluator, byte-framed bundle parser (prevents trust-escalation smuggling), `SuperbirdResourceStorage`, procedural selector parser & JS runner, `no_cosmetic_hosts`, `SBCE0001` cache, opt-in logger, unit tests & release benchmark |
| `overlay/Libraries/LibWebView/SuperbirdContentBlocking{Format.h,.h,.cpp}` | `669` lines | List catalogue (URLhaus via `malware-filter.gitlab.io/urlhaus-filter/urlhaus-filter-agh-online.txt`, Peter Lowe, 5 uAssetsCDN lists + EasyList/EasyPrivacy), expiry parser (`[1h, 14d]`, 4h default for `uboQuickFixes`), bundle encoder, host-list serializer |
| `overlay/UI/Qt/Superbird/Modules/ContentBlocking/*` | `111` lines | `"content_blocking"` Superbird module (`engine_mode`, `trust_custom_filters`, `enable_logger`) + Command Palette actions |
| `scripts/build-ubo-resources.mjs`, `scripts/update-ubo-resources.sh` | `363` lines | Runtime/CI converter turning a pinned uBO tag (`1.77.2`) into `ubo-resources.json` (`202` resources: `99` scriptlets, `54` `.fn` helpers, `35` trusted, `49` redirects, `0` skipped, `410,611` bytes) |
| `tests/TestContentBlocking.cpp`, `tests/test-build-ubo-resources.mjs`, `overlay/Tests/LibWeb/Text/input/superbird-content-blocker-*.html` | `783` lines | Unit tests, converter tests, and 4 end-to-end LibWeb `test-web` suites (scriptlets, network, procedural cosmetics, logger) |

### Key Divergences from the Initial Draft (and Why)

1. **Procedural cosmetic runner (`M3`)**: `upstream/Cargo.lock` pins `adblock = "0.13.5"` without the `css-validation` feature (enabling it would pull in `cssparser`/`selectors` and mutate `upstream/Cargo.lock`). Without `css-validation`, `adblock-rust` returns procedural selectors inside `hide_selectors` as raw strings rather than `procedural_actions` JSON. Instead of touching `Cargo.lock`, `superbird.rs` parses procedural selectors with a balanced-parenthesis parser (`parse_procedural_selector`) and emits a document-start IIFE (`PROCEDURAL_COSMETIC_RUNNER_JS`) with a debounced `MutationObserver` supporting `:has-text`, `:-abp-contains`, `:upward`, `:matches-css`, `:matches-css-before`, `:matches-css-after`, `:matches-attr`, `:matches-path`, `:min-text-length`, `:xpath`, `:remove()`, `:remove-attr()`, `:remove-class()`, and chained `:style()`. Pure `:style()` rules remain native CSS.
2. **Per-site Cosmetic filtering toggle (`M4`)**: `adblock-0.13.5` rejects `$elemhide` options on exception lines. `superbird.rs` handles the `!superbird:list id=site-switches` section directly in `no_cosmetic_hosts` so disabling cosmetic filtering on a site turns off generic, specific, and procedural hiding while keeping network blocking and scriptlets active.
3. **Missing-resource visibility (`M1`)**: `SuperbirdResourceStorage` logs missing scriptlet/redirect resources to `stderr` (`[Superbird ContentBlocker] Missing uBO-compatible resource '...'`) and records them for `internals.contentBlockerMissingResources()` rather than silently dropping rules when `ubo-resources.json` lags behind filter lists.
4. **Bundle framing (`M0`)**: `!superbird:list` and `!superbird:resources` sections carry explicit byte lengths (`bytes=<N>`) so an untrusted filter list containing literal `!superbird:list trust=1` lines cannot smuggle trusted rules.

### Measured Performance & Memory

**Rust engine benchmark (release build, Apple Silicon macOS, live 9-list + 202-resource bundle `6,617,916` bytes / `179,039` lines):**

| Metric | Measured | Budget / Comparison |
|---|---:|---|
| Cold load (9 lists + `!#if` + 202 resources + serialize cache) | **110.94 ms** | Default 2-list cold load: `58.10 ms` |
| Warm load (`SBCE0001` binary snapshot hit, `8.67 MB` on disk) | **11.64 ms** | **9.5× faster** than cold; well under `30 ms` budget |
| Per-request network check (10k mixed URLs: 2k block, 2k redirect, 1k removeparam, 5k allow) | **4.95 µs/req** (`49.47 ms` / 10k) | Well under `50 µs` budget |
| YouTube scriptlet generation (`https://www.youtube.com/watch?v=...`) | **0.26 ms** (`98,998 B`) | `0` missing resources across all 64 live YouTube `+js(...)` rules |
| YouTube cosmetic CSS generation | **0.10 ms** (`1,705 B`) | — |
| Corrupt `SBCE0001` snapshot recovery | **PASS** | SeaHash mismatch falls back cleanly to cold build |

**Live headless `Superbird.app` (`WebDriver`) process RSS & page-load impact:**

| Mode | Session startup | `example.com` load | `WebContent` RSS (`example.com`) | Wikipedia load | `WebContent` RSS (Wikipedia) |
|---|---:|---:|---:|---:|---:|
| Content blocking disabled | `0.42 s` | `1.08 s` | `106.4 MB` | `0.75 s` | `248.3 MB` |
| Default Ladybird (2 lists) | `0.42 s` | `0.37 s` | `107.2 MB` | `0.70 s` | `220.3 MB` |
| uBO-compatible (9 lists + 202 resources, cold) | `0.40 s` | `0.41 s` | `106.8 MB` | `0.74 s` | `227.7 MB` |
| uBO-compatible (9 lists + 202 resources, warm) | `0.41 s` | `0.43 s` | `106.7 MB` | `0.74 s` | `225.4 MB` |

Per-process memory delta for full uBO-compatible blocking vs Default Ladybird is **~5–7 MB** on Wikipedia (and negligible on `example.com`).

### Real-World Browser Verification & Side-by-Side Comparison

Tested in built `Superbird.app` (`WebDriver --headless`) with all 9 live default lists + `ubo-resources.json` (from uBO `1.77.2`) side-by-side against **Brave/Chromium 158 (Brave Shields disabled) + real uBlock Origin `1.77.2` MV2**:

| Test / Site | Superbird (uBO-compatible) | Superbird (Default Ladybird) | Chromium 158 + uBO `1.77.2` |
|---|---|---|---|
| `adblock-tester.com` | **100 / 100** | 100 / 100 | 96 / 100 |
| `adblock.turtlecute.org` (130 checks) | **83 / 130** blocked | 82 / 130 blocked | 81 / 130 blocked |
| `canyoublockit.com/extreme-test/` | Ad banners hidden | Ad banners hidden | Ad banners hidden |
| `d3ward.github.io/toolz/adblock` | *(Site archived by author)* | *(Site archived)* | *(Site archived)* |
| YouTube `watch?v=jNQXAC9IVRw` | Ad-free, `readyState=4`, `cur=12.6s`, 0 ad slots visible | Ad-free, `readyState=4`, `cur=12.4s` | Ad-free, `readyState=4`, `cur=10.8s` |
| YouTube `watch?v=dQw4w9WgXcQ` (fresh) | **Ad-free** (`adShowing=false`, `adPlacements=null`, `readyState=4`, `dur=213.1s`, `cur=14.5s`) | **60.1s pre-roll ad played** (`adShowing=true`, `adPlacements=1`, `adSlots=3`) | **Ad-free** (`adShowing=false`, `adPlacements=null`, `dur=213.1s`, `cur=5.6s`) |
| YouTube `watch?v=9bZkp7q19f0` (fresh) | **Ad-free** (`adShowing=false`, `adPlacements=null`, `readyState=4`, `dur=252.2s`, `cur=14.5s`) | **30.1s pre-roll ad played** (`adShowing=true`, `adPlacements=1`, `adSlots=6`) | — |
| YouTube Shorts (`SXHMnicI6Pg`) | Ad-free, `readyState=4`, `cur=3.6s`, 0 ad slots visible | Ad-free, `readyState=4`, `cur=4.1s` | Ad-free, `readyState=4`, `cur=2.0s` |
| YouTube anti-adblock enforcement walls | **0** observed | **0** observed | **0** observed |

- **Ad-heavy & tracker-heavy sites** (`speedtest.net`, `theverge.com`, `fandom.com`, `cnn.com`): rendered cleanly with **0 visible ad iframes** and **0 anti-adblock walls**. (`forbes.com` returned a DataDome headless bot challenge identically with content blocking completely disabled.)
- **Popular-site breakage checks** (`github.com/login`, `en.wikipedia.org/wiki/Web_browser`, `news.ycombinator.com`, `amazon.com`, `reddit.com`, `stripe.com`): all loaded with intact navigation, content, images, and login forms (`#login_field`/`#password` verified on GitHub) and **0** false-positive blanking.
- **Settings live mode switch**: Verified switching `content_blocking.engine_mode` live between `ubo` and `default` immediately updates `about:settings#blocking` (`Active engine: uBO-compatible (...)` ↔ `Active engine: Default Ladybird blocker`).
- **Resilience & freshness**: Tested missing `ubo-resources.json`, corrupt `ubo-resources.json`, missing list file (`uboFilters.txt`), corrupt `.sbce` snapshot, and a future unknown scriptlet (`example.com##+js(future-unreleased-scriptlet-2099, test)`). All loaded cleanly with zero crashes; missing/corrupt resources fell back from `$redirect` to hard network blocking and logged explicit `[Superbird ContentBlocker]` warnings to `stderr`.

### Honest Known Gaps vs. uBlock Origin on Chromium

1. **Hosted `ubo-resources.json` release asset (deployment prerequisite):** `DEFAULT_UBO_RESOURCES_URL` in `SuperbirdContentBlocking.cpp` currently points to a placeholder GitHub release URL until a CI job publishes `ubo-resources.json` on each uBO release tag. Until published, users/devs can set `SUPERBIRD_UBO_RESOURCES_URL` or run `./scripts/update-ubo-resources.sh`.
2. **Sandboxed macOS `WebContent` disk cache:** On macOS, `WebContent`'s Seatbelt sandbox denies filesystem writes, so `WebContent` cannot write `.sbce` cache files to disk and currently incurs the ~111 ms cold compile per new origin process. The `SBCE0001` snapshot cache is verified in unsandboxed processes/benchmarks (11.6 ms warm load); moving snapshot serialization into the unsandboxed UI process (`LibWebView::Application`) and shipping the serialized bytes over the existing `AnonymousBuffer` IPC is the clean follow-up to bring 11.6 ms warm loads to sandboxed macOS processes.
3. **Intermittent buffering on sequential monetized YouTube navigations:** Fresh watch pages (`jNQXAC9IVRw`, `dQw4w9WgXcQ`, `9bZkp7q19f0`) and Shorts played ad-free reliably, and a sequential `jNQXAC9IVRw → dQw4w9WgXcQ` navigation played immediately on one run (`state=1`, `24.6 s` buffered, `adformat=null`), but in 2 of 4 sequential navigations to a monetized video in the same tab the ad was stripped (`adShowing=false`) while the player remained in `state=3` (buffering) for >14 s.
4. **`$removeparam` regex and negation syntax:** `adblock-0.13.5` supports literal parameter names (`$removeparam=utm_source`) but silently ignores regex (`$removeparam=/pattern/`) and negated (`$removeparam=~key`) syntax.
5. **Nested procedural `:has(...)` selectors:** `parse_procedural_selector` handles chained top-level procedural operators (`span:has-text(Ad):upward(div)`) but does not yet unwrap procedural operators nested inside CSS `:has(...)` (e.g. `div:has(span:has-text(Ad))`). Also, because `PROCEDURAL_COSMETIC_RUNNER_JS` executes in the page's main JS realm (Ladybird has no isolated worlds), hostile page scripts could in principle detect or interfere with it compared to uBO's isolated content-script world.
6. **Deferred Phase 4 UI tools:** `SiteSettingsPopover` ships engine status + per-site Power and **Cosmetic filtering** switches, and `enable_logger` records matches for `internals.contentBlockerFilterLog()`, but a dedicated interactive **Logger UI window**, **Element Picker / Zapper**, per-site **no-popups / no-large-media / no-remote-fonts** switches, and uBO's **dynamic filtering matrix** are not built yet.
7. **Firefox-only uBO features:** HTML filtering (`##^`), `$responseheader`, and `$replace` remain out of scope (`cap_html_filtering = false`), matching uBO on Chromium.
