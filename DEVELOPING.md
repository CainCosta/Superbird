# Developing Superbird

Superbird is Ladybird + a small patch stack + an overlay of new files:

| Path | What lives there |
|---|---|
| `upstream/` | Ladybird, as a git submodule pinned to a specific commit |
| `overlay/` | New Superbird files (modules, settings JS, CMake). Symlinked into `upstream/` |
| `patches/` | Small edits to upstream files, as `git format-patch` files applied in order |
| `tests/` | Fast standalone C++ unit tests (no browser build needed) |

## First-time setup (macOS)

Needs the Xcode Command Line Tools (or Xcode) and Homebrew.

```sh
git clone https://github.com/CainCosta/Superbird.git && cd Superbird
make setup      # brew deps, upstream submodule, pinned Rust toolchain, git hooks, apply patches
make run        # first build takes ~1-2h (vcpkg builds Qt, Skia, ANGLE, ...), then launches
```

After that, `make run` is incremental (seconds to a few minutes, depending on what you touched).

## Daily loop

```sh
make run            # rebuild what changed + launch Superbird
make test           # fast unit tests + settings JS syntax check (seconds)
git add -A && git commit -m "..." && git push
```

Builds go to `upstream/Build/<preset>/` (default `Release`; `make run PRESET=Debug` for a debug build).
The app bundle is at `upstream/Build/release/bin/Superbird.app`.

### Editing overlay files (most work)

Files in `overlay/` are symlinked into `upstream/`, so just edit them in `overlay/` and `make run`.
**Adding or removing** an overlay file needs a re-link: `make apply` (then rebuild).

### Editing upstream files (patches)

Patches are edited as real commits inside `upstream/` on its local `superbird` branch:

```sh
cd upstream
# edit files...
git commit -a -m "UI/Qt: Add seam for X"     # a new patch, or amend an existing one:
git commit -a --fixup <sha>
git rebase -i --autosquash "$(cat "$(git rev-parse --git-dir)/superbird-base")"
cd ..
make export          # rewrites patches/ from upstream commits
git add patches && git commit -m "..."
```

- `make apply` **resets `upstream/`** and re-applies `patches/` from scratch, discarding un-exported upstream edits.
- `make build` re-applies automatically when `patches/` changes (e.g. after `git pull`), but refuses if
  `upstream/` has un-exported work.

### Adding a Superbird module

Modules live in `overlay/UI/Qt/Superbird/Modules/<Name>/` (Tree-Style Tabs is the reference):

1. Subclass `Superbird::SuperbirdModule` (`Core/Module.h`) and override only the window/tab hooks you need.
   `WindowHost` creates one instance per `BrowserWindow`; no new `BrowserWindow` seams should be needed.
2. Give it a `ModuleInfo` (id, name, description, default) and register its actions on the
   `ModuleRegistry` action bus. Keep the action table Qt-free so `tests/` can cover it.
3. Register it in `WindowHost::register_compiled_modules()`. That adds it to the registry, the action bus,
   and the `about:settings` -> **Superbird** toggle list (no `LibWebView` patch needed).
4. Add an `option(SUPERBIRD_MODULE_<NAME> ...)` to `overlay/Meta/CMake/SuperbirdModules.cmake` and guard
   its sources in `overlay/UI/Qt/Superbird/CMakeLists.txt`.
5. React to `on_enabled_changed(bool)`: a disabled module must leave no UI behind.

### Running next to stock Ladybird

Superbird has its own identity, so both browsers can run at once:

| | Superbird | Ladybird |
|---|---|---|
| macOS bundle | `Superbird.app`, `org.superbird.Superbird` | `Ladybird.app`, `org.ladybird.Ladybird` |
| Profile root | `~/Library/Application Support/Superbird` (macOS), `~/.local/share/Superbird` + `~/.config/Superbird` (Linux) | `.../Ladybird` |
| Mach bootstrap names | `Superbird-<profile>`, `Superbird-UI` | `Ladybird-<profile>`, `Ladybird-UI` |
| Linux desktop id / package | `org.superbird.Superbird`, `/opt/superbird`, `/usr/bin/superbird` | `org.ladybird.Ladybird`, `/opt/ladybird`, `/usr/bin/ladybird` |

### Bumping upstream Ladybird

```sh
make sync                     # applies patches/ on latest Ladybird master
make run                      # check it builds and works
make export                   # if you had to fix conflicts
./scripts/reset-upstream.sh <new-sha>
git -c submodule.upstream.ignore=none add upstream && git commit -m "Bump upstream Ladybird"
make apply
```

## Guard rails

- `.gitmodules` sets `ignore = all` on `upstream/` so `git add -A` / `commit -a` never pin the submodule
  to a local patched commit (bump it on purpose as shown above).
- `.githooks/pre-commit` rejects a commit that pins `upstream/` to a local patch commit.
- `.githooks/pre-push` blocks pushing when `upstream/` has commits not exported to `patches/`, and runs
  the unit tests (a few seconds). Bypass with `git push --no-verify`.

## CI

Every push to `main` (except Markdown-only changes) runs `.github/workflows/build.yml`: patch/unit-test
verification, then full macOS arm64 and Linux x86_64 builds (artifacts downloadable from the run).
Add `[skip ci]` to a commit message to skip it.

## Build notes

- `scripts/build.sh` reuses upstream's `Meta/ladybird.py` configure/dependency logic and seeds Superbird
  defaults on first configure: update checks + Sparkle off, upstream test binaries off (`BUILD_TESTING=OFF`).
- ccache is used automatically; vcpkg binary cache lives in `upstream/Build/caches/`.
- `make clean` wipes the build dir for the current preset; dependencies rebuild from the vcpkg cache.
