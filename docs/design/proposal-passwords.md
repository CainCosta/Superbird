# Proposal — Multi-Provider Password Manager (Native Bitwarden, Local Vault, 1Password)

*Status: **Shipped (M0–M4)** — implemented and verified 2026-11-26 against upstream Ladybird + Superbird's modular Qt/Hub/WebContent architecture.*

## TL;DR

Don't build a WebExtensions runtime to run password manager extensions, and don't link Bitwarden's official `sdk-internal` (GPLv3 / commercial license vs. Superbird's BSD-2-Clause). There is also no single OS API on macOS or Linux that exposes Bitwarden, 1Password, and local vaults to a custom Qt browser.

Instead, build one pick-and-choose Superbird module — **`Modules/Passwords/`** — split into two cleanly separated layers:

1. **Provider-agnostic Autofill Core + Qt UI** (built once):
   - Native C++ login/signup/OTP form inspection and filling inside `WebContent` (no page-context JS injection; immune to CSP and hostile page scripts; strict frame-origin verification).
   - Shared URI matcher (`URL::Host::registrable_domain()` via `libpsl`), RFC 6238 TOTP generator, CSPRNG password/passphrase generator, and OS biometric keyring helper (macOS Touch ID via `Security` + `LocalAuthentication`; Linux Secret Service via D-Bus).
   - Native Qt chrome: address-bar key icon (`LocationEdit::add_page_action`) + `PasswordPopover`, inline dropdown anchored to focused inputs, editable-field context menu, `Cmd/Ctrl+Shift+L` shortcut, Command Palette actions, and Save/Update `NotificationBar`.
2. **Pluggable `PasswordProvider` backends** (one or more active at once):
   - **Bitwarden / Vaultwarden (Native BSD-2 Client)** — zero external installs; talks directly to Bitwarden Cloud (US / EU) or self-hosted Vaultwarden via async HTTPS (`CURL::libcurl` with `CURLSSLOPT_NATIVE_CA`) + **OpenSSL 3.6.5** (already in `vcpkg.json` with PBKDF2, Argon2id, HKDF, AES-256-CBC, HMAC-SHA256, RSA-OAEP).
   - **Local Vault (Built-in)** — Apple Passwords–style built-in encrypted vault stored in sidecar `Passwords.db` (Argon2id + AES-256-GCM + OS biometric unlock + CSV import/export for Apple Passwords, Safari, Chrome, Bitwarden, and 1Password).
   - **1Password (`op` CLI v2 Bridge)** — talks to the user's running 1Password desktop app via the official `op` CLI with native Touch ID / Polkit biometric unlock and zero secrets persisted in Superbird.

---

## Decisions (2026-11-26)

1. **Bitwarden implementation:** **Native BSD-2 client** built directly into Superbird (zero external binaries required), talking to Bitwarden Cloud (US/EU) and self-hosted Bitwarden / Vaultwarden.
2. **Target providers:** **Native Bitwarden/Vaultwarden**, **Built-in Local Vault**, and **1Password** — all behind a single `PasswordProvider` interface so additional backends (e.g., KeePassXC Unix socket, Proton Pass `pass-cli`) can plug in later without touching autofill or UI code.
3. **Zero new vcpkg dependencies:** Everything needed (`OpenSSL 3.6.5` with Argon2id/PBKDF2/HKDF/AES-CBC/AES-GCM/HMAC/RSA-OAEP, `CURL::libcurl` with OpenSSL TLS + `CURLSSLOPT_NATIVE_CA`, `sqlite3`/`LibDatabase`, `libpsl`, `gio-2.0` on Linux, macOS `Security`/`LocalAuthentication`) is already present in the build. Note: Ladybird's vcpkg `qtbase` ships only `qcertonlybackend` (no TLS socket backend), so `BitwardenProvider` uses async `libcurl` on a worker thread rather than `QNetworkAccessManager`.
4. **License hygiene:** Clean-room BSD-2-Clause implementation against Bitwarden's public HTTP API and published zero-knowledge cryptography specification. No code copied or linked from `bitwarden/clients` or `bitwarden/sdk-internal` (GPLv3 / Bitwarden License).

---

## Why Not Other Routes

| Alternative | Why rejected |
|---|---|
| **WebExtensions runtime** | Requires isolated JS worlds, MV3 service workers, `chrome.*` APIs, `chrome-extension://` popups, and Native Messaging — a multi-month platform project (same conclusion as `docs/design/proposal-ublock-origin.md`). |
| **Bitwarden `sdk-internal`** | GPLv3 / commercial dual-license — incompatible with BSD-2-Clause static linking. |
| **Bitwarden Desktop `desktop_proxy`** | Only handles biometric unlock key-release for the official extension; does **not** serve vault items. |
| **`bw serve` (official Node CLI)** | Heavy Node startup + RAM footprint and exposes an **unauthenticated localhost HTTP server** (`127.0.0.1:8087`) reachable by any local process. |
| **macOS `ASCredentialProviderExtension` / Apple Passwords** | macOS AutoFill Credential Providers only surface for Safari / `WKWebView` / native Cocoa text fields; Apple Passwords' `PasswordManagerBrowserExtensionHelper` requires Apple-only private entitlements or Apple's signed mainstream-browser allowlist. A built-in Local Vault with Touch ID + CSV import delivers the local Apple Passwords workflow cleanly on both macOS and Linux. |

---

## Architecture & Process Boundaries

```text
UI Process (unsandboxed, owns vault keys & decrypted in-memory index)
├── Modules/Passwords/
│   ├── PasswordsModule           (SuperbirdModule: per-window UI + shortcuts + actions)
│   ├── PasswordManagerService    (App-wide singleton: providers, lock timer, clipboard timer)
│   ├── Providers/
│   │   ├── BitwardenProvider     (HTTPS via Qt6::Network + OpenSSL 3.6.5 + Passwords.db cache)
│   │   ├── LocalVaultProvider    (SQLite Passwords.db + Argon2id + AES-256-GCM)
│   │   └── OnePasswordProvider   (QProcess -> `op` v2 + in-memory summary cache)
│   ├── Core/                     (Qt-free C++23, unit-tested in tests/TestPasswords.cpp)
│   │   ├── PasswordProvider.h    (Abstract interface + CredentialSummary / Secret / UriRule)
│   │   ├── BitwardenCrypto.{h,cpp}
│   │   ├── LocalVaultCrypto.{h,cpp}
│   │   ├── UriMatcher.{h,cpp}    (BaseDomain / Host / StartsWith / Exact / Regex / Never)
│   │   ├── Totp.{h,cpp}          (RFC 6238 + otpauth:// parser)
│   │   ├── PasswordGenerator.{h,cpp}
│   │   ├── CsvTransfer.{h,cpp}   (Import/export Apple Passwords, Chrome, Bitwarden, 1Password)
│   │   └── PasswordsActions.{h,cpp}
│   ├── Keyring/
│   │   ├── Keyring.h
│   │   ├── KeyringMacOS.mm       (Security + LocalAuthentication Touch ID on worker thread)
│   │   └── KeyringLinux.cpp      (org.freedesktop.secrets over D-Bus)
│   └── UI/
│       ├── PasswordPopover.{h,cpp}        (Address-bar key icon popover)
│       ├── InlineAutofillPopover.{h,cpp}  (Anchored below focused login field)
│       ├── ProviderSetupDialog.{h,cpp}    (Bitwarden sign-in / 2FA / Local Vault / 1Password)
│       └── SaveCredentialBar.{h,cpp}      (NotificationBar wrapper for Save / Update)
│
│  Hub IPC (NEVER sends the vault; sends only the 1 chosen credential on explicit user action)
▼
Hub Process (routes view_id <-> WebContent page_id, no credential persistence)
│
▼
WebContent Process (sandboxed, per-site isolated)
└── LibWeb Autofill Seam (C++, ~120 lines)
    ├── FormInspector             (Classifies username/email/password/new-password/otp inputs)
    ├── Focused field push        (frame_id, origin, field_kind, device_pixel_rect, has_pair)
    ├── Origin-checked Fill       (HTMLInputElement::set_value + trusted input/change events)
    └── Form submit observer      (Emits {origin, username, password} for Save/Update prompt)
```

### Core Security Invariants

1. **Decrypted vault never leaves the UI process.** `WebContent` never receives a list of accounts or passwords. Only the single credential the user explicitly picks is sent over IPC for immediate fill.
2. **Never auto-fill silently on page load.** Prevents hidden/off-screen field credential harvesting; filling always requires an explicit click, Enter, context-menu pick, or `Cmd/Ctrl+Shift+L`.
3. **Strict frame-origin pinning.** Every fill request carries `expected_origin` and `frame_id`. `WebContent` verifies the target frame's active document origin matches `expected_origin` right before writing — a cross-origin `<iframe>` can never receive the top-level page's password.
4. **Authenticate-then-decrypt & memory hygiene.** Bitwarden MACs are verified with constant-time `CRYPTO_memcmp` before AES-CBC decryption. All keys and plaintext secrets are scrubbed with `OPENSSL_cleanse` on lock, auto-lock timeout, and exit. Secrets are never written to logs or crash dumps.
5. **Clipboard hygiene.** Copying a password or TOTP schedules an automatic clipboard clear after 30 seconds (only if the clipboard still holds that copied value).
6. **Private browsing isolation.** Private tabs never prompt to save or update credentials; autofill in private windows is opt-in (off by default, toggleable in `about:settings -> Superbird`).

---

## Provider Interface (`PasswordProvider.h`)

All three providers implement the same Qt-free async-friendly interface, so `PasswordManagerService` can aggregate matches across multiple enabled providers at once:

```cpp
enum class ProviderKind : u8 { Bitwarden, LocalVault, OnePassword };
enum class ProviderState : u8 { Unconfigured, Locked, Unlocking, Syncing, Unlocked, Error };

struct ProviderCapabilities {
    bool can_sync { false };
    bool can_create { false };
    bool can_update { false };
    bool supports_biometric_unlock { false };
    bool supports_totp { true };
};

struct CredentialSummary {
    ProviderKind provider;
    String provider_account_id;
    String id;
    String name;
    String username;
    Vector<UriRule> uris;
    bool has_password { false };
    bool has_totp { false };
    bool favorite { false };
};

struct CredentialSecret {
    String username;
    String password;
    Optional<String> totp_uri;       // otpauth://... or raw Base32 secret
    Optional<String> precomputed_totp; // used when provider computes TOTP (e.g. `op --otp`)
};
```

Key methods on `PasswordProvider`:
- `status()`, `capabilities()`, `last_error()`, `last_sync_time()`
- `unlock(UnlockRequest)` (Master password, Biometric, or Desktop-delegated for `op`), `lock()`, `sync()`
- `match_for_url(URL::URL const&)` → `Vector<CredentialSummary>` (ranked: Exact > StartsWith > Host > BaseDomain, favorites boosted)
- `search(StringView query)` → `Vector<CredentialSummary>`
- `reveal_secret(StringView id)` → `CredentialSecret` (sync for in-memory Bitwarden/LocalVault; async callback for `op`)
- `create_login(NewLogin)` / `update_login(StringView id, UpdatedLogin)`

---

## Provider Specifications

### Provider 1 — Native Bitwarden / Vaultwarden (`BitwardenProvider`)

**Server Presets:**
- **Bitwarden US (default):** Identity `https://identity.bitwarden.com`, API `https://api.bitwarden.com`
- **Bitwarden EU:** Identity `https://identity.bitwarden.eu`, API `https://api.bitwarden.eu`
- **Self-Hosted / Vaultwarden:** Base URL `https://<host>` → Identity `<host>/identity`, API `<host>/api`

**Required Request Headers:**
- `Bitwarden-Client-Name: desktop` (or `web`), `Bitwarden-Client-Version: 2026.1.0` (kept current so Bitwarden cloud doesn't reject with `400`), `Device-Type: 2` (macOS) / `8` (Linux), `Auth-Email: <base64url(email)>`.

**Cryptographic Pipeline (100% OpenSSL 3.6.5, verified in build tree):**
1. **Prelogin:** `POST <identity>/accounts/prelogin` `{ "email": "..." }` → `{ kdf, kdfIterations, kdfMemory, kdfParallelism }`.
2. **Master Key (32 bytes):**
   - `kdf == 0` (PBKDF2-SHA256): `PKCS5_PBKDF2_HMAC(password, lower(trim(email)), iterations, EVP_sha256(), 32)`.
   - `kdf == 1` (Argon2id): `EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr)` with `salt = SHA256(lower(trim(email)))`, `memcost = kdfMemory * 1024`, `iter = kdfIterations`, `lanes = kdfParallelism`, `output = 32`.
3. **Master Password Hash (for `/connect/token`):**
   - `Base64(PBKDF2-HMAC-SHA256(key = MasterKey, salt = password, iterations = 1, len = 32))`.
4. **Stretched Master Key (64 bytes = 32B enc + 32B mac):**
   - HKDF-Expand (SHA-256, no extract step, `PRK = MasterKey`): `enc = HKDF-Expand(MasterKey, "enc", 32)`, `mac = HKDF-Expand(MasterKey, "mac", 32)`.
5. **User Symmetric Key (64 bytes):**
   - Decrypt `Key` returned by `/connect/token` (format `2.<b64_iv>|<b64_ct>|<b64_mac>`) using Stretched Master Key (constant-time HMAC-SHA256 verification before AES-256-CBC decrypt).
6. **Organization Keys (optional, for shared org logins):**
   - Decrypt user's `PrivateKey` (PKCS#8 DER) with User Symmetric Key, then unwrap each org's `Key` using `RSA-OAEP-SHA1` (cipher type `4`) / `RSA-OAEP-SHA256` (type `3`).
7. **Per-Cipher Key & Field Decryption:**
   - If a cipher has its own `key` field, unwrap that 64-byte cipher key with the User (or Org) Symmetric Key; otherwise use the User/Org key directly.
   - Decrypt `name`, `login.username`, `login.password`, `login.totp`, `login.uris[].uri`, and `notes`.

**Authentication Flows:**
- **Email + Master Password** (`grant_type=password`, `scope=api offline_access`):
  - Handles 2FA challenges (`twoFactorProvider`: `0` Authenticator TOTP, `1` Email OTP, `3` YubiKey OTP) and **New Device Email Verification** (`newDeviceOtp`).
- **API Key Login** (`grant_type=client_credentials`, `client_id`, `client_secret`) + local Master Password unlock:
  - Exempt from 2FA and New Device Verification; provides a clean path for accounts using Enterprise SSO or WebAuthn-only 2FA.
- **Token Refresh:** Stores `refresh_token` encrypted (or in OS keyring) and silently refreshes expired `access_token`s on sync.

**Offline Cache & Sync (`Passwords.db`):**
- Stores the **raw server ciphertext** from `GET <api>/sync` in SQLite (`bw_ciphers`, `bw_meta`), so offline unlocking works with zero plaintext ever written to disk.
- Background sync on unlock + periodic refresh (e.g., every 15 minutes while unlocked) + manual Sync button.
- Write operations: `POST <api>/ciphers` (create login) and `PUT <api>/ciphers/{id}` (update password/URI) encrypt locally with User Symmetric Key (`2.<iv>|<ct>|<mac>`) before sending.

---

### Provider 2 — Built-in Local Vault (`LocalVaultProvider`)

Provides a zero-account, Apple Passwords–style built-in password manager that works identically on macOS and Linux:

- **Storage:** Sidecar SQLite database `Passwords.db` in `WindowHost::persistent_data_directory()` (never touches upstream `Sessions.db` or `Bookmarks.db`).
- **Encryption Scheme (OpenSSL 3.6.5):**
  - Random 256-bit **Vault Data Encryption Key (DEK)** generated via `RAND_bytes`.
  - DEK is wrapped twice (optional):
    1. Wrapped by a **Master Passphrase** derived via **Argon2id** (64 MiB, 3 iterations, 4 lanes, random 16-byte salt) using AES-256-GCM.
    2. Optionally wrapped/stored in the **OS Biometric Keyring** (Touch ID on macOS, Secret Service on Linux) so users can unlock with a fingerprint without typing a passphrase.
  - Every item row stores `{ id, created_at, updated_at, nonce (12B), ciphertext, tag (16B) }` encrypted with **AES-256-GCM** with `AAD = "superbird-vault-v1|" + item_id`, encrypting a JSON payload `{ name, username, password, uris[], totp_uri, notes, favorite, password_history[] }`.
  - No domains or usernames are stored in plaintext in SQLite — metadata is decrypted into memory only while unlocked.
- **Import / Export (`CsvTransfer.{h,cpp}`):**
  - **Import CSV** from Apple Passwords / Safari (`Title,URL,Username,Password,Notes,OTPAuth`), Chrome/Edge/Brave, Bitwarden CSV, and 1Password CSV — making migration from Apple Passwords a 2-click flow.
  - **Export CSV** (behind explicit re-authentication / confirmation).

---

### Provider 3 — 1Password (`OnePasswordProvider`)

1Password's desktop browser bridge enforces signed mainstream-browser allowlists, so the sanctioned, secure integration on both macOS and Linux is **1Password CLI v2 (`op`) with Desktop App Integration**:

- **Zero secrets persisted in Superbird.** Authentication, biometrics (Touch ID on macOS / Polkit on Linux), and session expiry are 100% owned by the 1Password desktop app.
- **Fast UX via two-stage fetch:**
  1. On unlock (or first match request), runs `op item list --categories Login --format json` asynchronously via `QProcess` and caches the **non-secret metadata index** (`id`, `title`, `vault`, `urls[]`, `additional_information` username) in memory (invalidated on manual sync, lock, or configurable TTL).
  2. When the user clicks a login to fill or copy, runs `op item get <id> --format json --reveal` (and `--otp` if TOTP requested) to fetch the secret on demand.
  3. Optional write support: `op item create --category Login ...` / `op item edit <id> ...` for Save/Update prompts.
- **Discovery & onboarding:** Detects `op` on `PATH` plus common Homebrew locations (`/opt/homebrew/bin/op`, `/usr/local/bin/op`). If missing or if desktop integration isn't enabled, the popover displays a concise 2-step setup note (*Install `op` CLI* + *1Password → Settings → Developer → Integrate with 1Password CLI*).

---

## Upstream Seams (Minimal Patch Stack)

Following Superbird's patch budget discipline (see `ROADMAP.md` and patches `0007`–`0012`):

1. **Patch 0029 — `WebContent`/`Hub`/`RemoteView` Autofill Seam (~140 lines):**
   - **Form inspection in `LibWeb`:** Helper called alongside `ConnectionFromClient::update_input_method_state()` (`Services/WebContent/ConnectionFromClient.cpp:3393`) and on page load completion:
     - Inspects `HTMLInputElement`s in the active/focused document (and same-origin frames): `type` (`password`, `email`, `text`, `tel`), `autocomplete` (`username`, `email`, `current-password`, `new-password`, `one-time-code`), and token heuristics on `name`/`id`/`placeholder`/`aria-label`.
     - Reports focused autofillable field metadata: `{ frame_id, frame_origin, field_kind, input_rect_device_pixels, has_username, has_password }`.
   - **Origin-checked credential filling:**
     - `HubServer::view_fill_credentials(u64 view_id, u64 frame_id, URL::Origin expected_origin, Utf16String username, Utf16String password)`
     - `HubServer::view_fill_focused_field(u64 view_id, u64 frame_id, URL::Origin expected_origin, Utf16String value)`
     - In `WebContent`, verifies `document->origin() == expected_origin`, updates target `HTMLInputElement` via `set_value(...)`, and dispatches bubbling `input` + `change` events (handles React/Vue/Angular/Svelte controlled inputs cleanly with zero JS injection).
   - **Form submission observer (Phase 3):** Hooks `HTMLFormElement` submit / password-value commit to emit `view_did_submit_credentials(view_id, origin, username, password)` (only for non-empty password fields).
2. **Patch 0030 — Editable Context Menu Seam (~15 lines):**
   - Adds `WindowHost::extend_editable_context_menu(RemoteView&, Menu&, URL::URL const&)` in `UI/Qt/ContextMenus.cpp` (mirroring Patch `0012`'s `extend_link_context_menu`) so the Passwords module appends *Fill Login…*, *Copy TOTP*, and *Generate Password* when right-clicking editable inputs.
3. **Already shipped & reused with 0 patch lines:**
   - `LocationEdit::add_page_action(QAction*)` (Patch `0008`) — address-bar key icon.
   - `WindowHost::reserve_shortcut` + `ModuleShortcut` + `ModuleOption` — `Cmd/Ctrl+Shift+L` and `about:settings -> Superbird` toggles.
   - `Ladybird::Popover` and `Ladybird::NotificationBar` — native macOS/Linux chrome.
   - `overlay/UI/Qt/Superbird/CMakeLists.txt` — links `Qt6::Network`, `OpenSSL::Crypto`, and (on macOS) `-framework Security -framework LocalAuthentication` without touching upstream `UI/Qt/CMakeLists.txt`.

---

## UI & UX Surfaces

- **Address-Bar Key Action (`LocationEdit::add_page_action`):**
  - Shows a crisp key icon (dimmed when locked, highlighted with match count indicator when matches exist for the active tab's URL).
  - Click opens **`PasswordPopover`** (subclasses `Ladybird::Popover`):
    - **Locked state:** Provider badge, Touch ID / Unlock button, master password field (Enter to unlock).
    - **Unlocked state:** *Matching this site* list (one-click Fill, plus inline Copy Username / Copy Password / Copy TOTP buttons with countdown ring), live search bar across all enabled providers, Password Generator tab, Sync / Lock / Add Provider buttons.
- **Inline Field Dropdown (`InlineAutofillPopover`):**
  - Appears cleanly below a focused login/password/OTP field using `input_rect_device_pixels` from `WebContent`. Keyboard navigable (`↓`/`↑`/`Enter`/`Esc`).
- **Keyboard Shortcut & Command Palette:**
  - `Cmd/Ctrl+Shift+L` (configurable in `about:settings -> Superbird`): fills top match immediately (or opens popover if locked / multiple matches).
  - Command Palette actions (`PasswordsActions.{h,cpp}`): *Autofill Login*, *Open Password Manager*, *Search Passwords*, *Copy TOTP for Current Site*, *Generate Strong Password*, *Sync Vaults*, *Lock All Vaults*.
- **Save / Update Prompt (`SaveCredentialBar`):**
  - Uses `Ladybird::NotificationBar` at top of tab after login submission: *"Save login for github.com to Bitwarden?"* (`Save` / `Choose Provider…` / `Never for this site` / `×`).
- **`about:settings -> Superbird` Options:**
  - Enable/disable `Passwords` module (zero overhead & zero UI chrome when disabled).
  - Active providers (Bitwarden, Local Vault, 1Password), auto-lock timeout (`5 min` / `15 min` / `1 hour` / `On quit`), inline dropdown toggle, save-prompt toggle, private-window autofill toggle.

---

## Phased Delivery & Verification Plan

| Phase | Scope | Upstream Patches | Verification |
|---|---|---|---|
| **M0** | Core Qt-free layer (`PasswordProvider`, `BitwardenCrypto`, `LocalVaultCrypto`, `UriMatcher`, `Totp`, `PasswordGenerator`, `CsvTransfer`, `PasswordsActions`) + `Passwords` module registration | None (overlay-only) | Standalone `tests/TestPasswords.cpp` via `./scripts/test-unit.sh`: Bitwarden official test vectors (PBKDF2 + Argon2id + HKDF + `2.<iv>|<ct>|<mac>` + RSA-OAEP + tampered MAC rejection), Local Vault AES-GCM round-trip, RFC 6238 TOTP vectors, eTLD+1 URI matching, CSV import parsers |
| **M1** | `WebContent`/`Hub` autofill seam + editable context menu + address-bar key icon + `PasswordPopover` + **Local Vault** end-to-end (with OS biometric keyring on macOS/Linux) | Patches `0029`, `0030` | Unit tests + headless WebDriver tests on static & React/SPA login forms (verifying `input`/`change` events fire and cross-origin iframe fills are refused) |
| **M2** | **Native Bitwarden / Vaultwarden** provider (US/EU/custom server, Email+Password+2FA/New-Device-OTP + API Key, sync, decrypt, encrypted `Passwords.db` offline cache, Touch ID / Secret Service unlock) | None (overlay-only) | Integration test against local mock Bitwarden/Vaultwarden server + live login/sync/TOTP/fill verification |
| **M3** | **Inline focused-field dropdown** + **Save / Update login** (`NotificationBar` + Bitwarden `POST`/`PUT /api/ciphers` + Local Vault save) | Small form-submit hook inside Patch `0029` | WebDriver tests for new login save, password update, private-window suppression, and "Never for this site" |
| **M4** | **1Password (`op` CLI v2)** provider (async `op item list` cache, on-demand `--reveal` & `--otp`, create/edit, setup guidance) + multi-provider aggregation polish | None (overlay-only) | Unit tests for `op` JSON parser + live macOS Touch ID / Linux Polkit verification |

---

## Shipped Implementation & Verification (2026-11-26)

### Patch & Overlay Footprint
- **Upstream patches (`+221 / -0` total):**
  - `patches/0030-WebContent-Hub-UI-Add-Superbird-password-autofill-an.patch` (`+213` lines across 25 files: 164 lines for the `LibWeb`/`WebContent`/`Hub`/`LibWebView`/`UI/Qt` focused-field, origin-checked fill, and form-submit credential observation seam + 49 lines for `Internals` autofill test hooks).
  - `patches/0031-UI-Qt-Let-Superbird-modules-extend-editable-field-co.patch` (`+8` lines in `UI/Qt/ContextMenus.cpp`, mirroring Patch `0012` for link context menus).
- **Overlay & test footprint (~13,470 lines, BSD-2-Clause, zero new vcpkg dependencies):**
  - `overlay/Libraries/LibWeb/Superbird/PasswordAutofill.{h,cpp}` — field classification (`username`, `password`, `new-password`, `one-time-code`), focused-field metadata + bounding rect, origin-verified `HTMLInputElement::set_value` with trusted `input`/`change` events, OOPIF/cross-origin focus guard, and form-submit credential extraction.
  - `overlay/UI/Qt/Superbird/Modules/Passwords/Core/` — Qt-free C++23 core (`PasswordProvider.h`, `BitwardenCrypto`, `BitwardenProtocol`, `LocalVaultCrypto`, `UriMatcher`, `Totp`, `PasswordGenerator`, `CsvTransfer`, `OnePasswordParser`, `PasswordsActions`). OpenSSL 3.6.5 `EVP_KDF` `"ARGON2ID"` is used in full browser builds, with a clean-room RFC 9106 Argon2id fallback when standalone unit tests link system OpenSSL 3.0.x (e.g., Ubuntu CI `verify`).
  - `overlay/UI/Qt/Superbird/Modules/Passwords/Keyring/` — `KeyringMacOS.mm` (`Security` + `LocalAuthentication` Touch ID off the Qt GUI thread, with graceful fallback on ad-hoc builds without Keychain access group entitlements) and `KeyringLinux.cpp` (GDBus `org.freedesktop.secrets` via already-linked `PkgConfig::GIO`).
  - `overlay/UI/Qt/Superbird/Modules/Passwords/Storage/PasswordsStore.{h,cpp}` — sidecar `Passwords.db` via `LibDatabase` (strictly encrypted Local Vault blobs, raw Bitwarden server ciphertext, and `never_save_hosts`).
  - `overlay/UI/Qt/Superbird/Modules/Passwords/Providers/` — `LocalVaultProvider` (CRUD, password history, CSV import/export), `BitwardenProvider` (async `CURL::libcurl` + OpenSSL 3.6.5, US/EU/Custom servers, password + 2FA + `newDeviceOtp` + API key + refresh tokens, offline encrypted cache, `POST`/`PUT` writes), and `OnePasswordProvider` (async `QProcess` `op` v2 bridge with non-secret metadata index, on-demand `--reveal`/`--otp`, and setup guidance).
  - `overlay/UI/Qt/Superbird/Modules/Passwords/UI/` — `PasswordPopover`, `InlineAutofillPopover`, `ProviderSetupDialog`, `SaveCredentialBar` (`Ladybird::NotificationBar`), `PasswordManagerService`, and `PasswordsModule` (`LocationEdit::add_page_action` key icon, `Ctrl+Shift+L` / `Cmd+Shift+L`, editable context-menu entries, Command Palette actions, `about:settings -> Superbird` toggles/options, 30s clipboard auto-clear, private-browsing save suppression).

### Verification Matrix
1. **Standalone C++23 / Rust / JS unit suite (`make test` / `./scripts/test-unit.sh`):**
   - **PASS** — all 9 `TestPasswords` suites (`BitwardenCrypto`, `LocalVaultCrypto`, RFC 6238 `Totp`, `UriMatcher`, CSPRNG `PasswordGenerator`, `CsvTransfer` for Apple/Chrome/Bitwarden/1Password, `OnePasswordParser`, `PasswordsActions`, and a live loopback TCP HTTP mock Bitwarden/Vaultwarden server E2E test covering prelogin, 2FA, `newDeviceOtp`, API key, refresh token, `/api/sync` with per-cipher & RSA-OAEP org keys, `POST /api/ciphers`, `PUT /api/ciphers/{id}`, and offline cache decrypt) plus all existing Superbird C++, Rust, and JS tests.
2. **Real `LibWeb` autofill browser tests (`test-web -f "Text/input/superbird-*"`):**
   - **PASS (5/5, 0 fail, 0 timeout, 0 crash)** — `superbird-passwords-autofill.html` verifies static HTML forms, React-style controlled-input value-setter overrides receiving trusted `input`/`change` events, OTP focused-field fill, form-submit credential extraction, mismatched-origin fill rejection, and sandboxed cross-origin iframe fill rejection, alongside all 4 existing `superbird-content-blocker-*` suites.
3. **Qt UI & WebDriver verification (`TestUIQt` + `tests/test-webdriver-passwords.py`):**
   - **PASS** — `TestUIQt` passes with zero regressions (6.32s); `test-webdriver-passwords.py` verifies `about:settings#superbird` rendering, live `setSuperbirdModuleEnabled` & `setSuperbirdModuleOption` round-tripping through `Hub`, SPA login form submission, and classic `POST` login form submission/navigation.
4. **Live Provider + `libcurl` + `Passwords.db` Zero-Plaintext Audit:**
   - **PASS** — compiled integration check against built `PasswordsStore`, `LocalVaultProvider`, and `BitwardenProvider` (`libcurl` against a live loopback mock Bitwarden server) verified vault creation, lock/unlock, wrong-password rejection, online sync, offline unlock with server stopped, and scanned every byte of `Passwords.db` + WAL files on disk confirming zero plaintext passwords, usernames, TOTP secrets, notes, site names, domains, or refresh tokens.
5. **Portable macOS bundle dry-run (`cmake --install` + `scripts/macos-bundle.py` + `codesign --verify --deep --strict`):**
   - **PASS** — `Superbird.app` links `@rpath/libcurl.4.8.0.dylib`, `@rpath/libssl.3.dylib`, `@rpath/libcrypto.3.dylib`, `Security.framework`, and `LocalAuthentication.framework`; every `@rpath` library is bundled and passes strict deep code signature verification.

### Honest Known Gaps / Follow-ups
- **1Password live desktop verification:** The `op` CLI and 1Password Desktop were not installed on the macOS build host; `OnePasswordProvider` JSON parsing, item reveal/OTP parsing, CLI detection, and in-UI setup guidance are verified, while live Touch ID / Polkit end-to-end auth against a real 1Password account awaits testing on a machine with 1Password installed.
- **Linux Secret Service & x86_64 CI:** `KeyringLinux.cpp` (`org.freedesktop.secrets` over `PkgConfig::GIO`) and Linux CMake wiring were reviewed and verified against upstream `UI/Qt/CMakeLists.txt`, pending the Linux x86_64 GitHub Actions CI build and a live GNOME Keyring / KWallet runtime check.
- **Passkeys / WebAuthn:** Out of scope for M0–M4 (remains a future milestone).
