#!/usr/bin/env bash
# Tiered verification runner for Superbird rebases.
#
# Usage:
#   ./scripts/test-rebase.sh [--preset Release|Debug] [--full] [--skip-build] [--skip-bundle]
#
# Default mode runs:
#   Stage 1: Superbird C++23 unit tests + Rust ContentBlocker tests + Node uBO tests + JS syntax
#   Stage 2: Fail-fast browser build (ladybird_impl -> app bundle + WebDriver)
#   Stage 3: CMake invariant check (updater & Sparkle OFF)
#   Stage 4: Reconfigure with BUILD_TESTING=ON, build & run:
#            - Superbird E2E test-web suites (superbird-content-blocker-*.html)
#            - Targeted upstream CTest suites (UI/Qt, LibWebView, LibWeb, DeadRustExports,
#              and WebDriver content-blocker/settings/BiDi/permissions/default-browser tests)
#            (--full runs the entire upstream CTest suite instead of the targeted subset)
#   Stage 5: macOS portable bundle & codesign dry-run in a temp staging dir (never touches /Applications)
#
# Works with macOS's stock bash 3.2.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_DIR="$ROOT_DIR/upstream"

PRESET="${BUILD_PRESET:-Release}"
FULL_MODE=0
SKIP_BUILD=0
SKIP_BUNDLE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset)
            PRESET="${2:?missing value for --preset}"
            shift 2
            ;;
        --full)
            FULL_MODE=1
            shift
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --skip-bundle)
            SKIP_BUNDLE=1
            shift
            ;;
        -h|--help)
            sed -n '2,18p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

case "$PRESET" in
    All_Debug)       BUILD_SUBDIR="alldebug" ;;
    ThreadSanitizer) BUILD_SUBDIR="tsan" ;;
    *)               BUILD_SUBDIR="$(printf '%s' "$PRESET" | tr '[:upper:]' '[:lower:]')" ;;
esac
BUILD_DIR="$UPSTREAM_DIR/Build/$BUILD_SUBDIR"

# Resolve shared main checkout upstream/ when running in a linked Superbird git worktree
MAIN_UPSTREAM_DIR="${LADYBIRD_MAIN_SOURCE_DIR:-$UPSTREAM_DIR}"
if [[ -z "${LADYBIRD_MAIN_SOURCE_DIR:-}" ]]; then
    GIT_DIR_ABS="$(git -C "$ROOT_DIR" rev-parse --path-format=absolute --git-dir 2>/dev/null || true)"
    GIT_COMMON_ABS="$(git -C "$ROOT_DIR" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
    if [[ -n "$GIT_DIR_ABS" && -n "$GIT_COMMON_ABS" && "$GIT_DIR_ABS" != "$GIT_COMMON_ABS" ]]; then
        MAIN_ROOT="$(dirname "$GIT_COMMON_ABS")"
        if [[ -d "$MAIN_ROOT/upstream" ]]; then
            MAIN_UPSTREAM_DIR="$MAIN_ROOT/upstream"
        fi
    fi
fi

JOBS="${MAKEJOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"
RESULTS=()

record_stage() {
    RESULTS+=("$1 | $2 | $3s")
}

run_stage() {
    local name="$1"
    shift
    local start_ts end_ts elapsed
    start_ts="$(date +%s)"
    echo ""
    echo "================================================================"
    echo " [Stage] $name"
    echo "================================================================"
    if "$@"; then
        end_ts="$(date +%s)"
        elapsed="$((end_ts - start_ts))"
        record_stage "PASS" "$name" "$elapsed"
    else
        local rc=$?
        end_ts="$(date +%s)"
        elapsed="$((end_ts - start_ts))"
        record_stage "FAIL" "$name" "$elapsed"
        print_results
        exit "$rc"
    fi
}

print_results() {
    echo ""
    echo "================================================================"
    echo " Rebase Test Summary ($PRESET, mode=$( [[ "$FULL_MODE" == "1" ]] && echo full || echo targeted ))"
    echo "================================================================"
    local line
    for line in "${RESULTS[@]}"; do
        echo "  $line"
    done
    echo "================================================================"
}

stage_unit_tests() {
    CXX="${CXX:-clang++}" "$ROOT_DIR/scripts/test-unit.sh"
    node --check "$UPSTREAM_DIR/Base/res/ladybird/about-pages/settings/tabs.js"
    node --check "$UPSTREAM_DIR/Base/res/ladybird/about-pages/settings/superbird.js"
}

stage_build_ui_layer() {
    "$ROOT_DIR/scripts/build.sh" "$PRESET" ladybird_impl
}

stage_build_app_and_webdriver() {
    "$ROOT_DIR/scripts/build.sh" "$PRESET"
    ninja -C "$BUILD_DIR" -j "$JOBS" WebDriver
}

stage_invariant_audit() {
    local cache="$BUILD_DIR/CMakeCache.txt"
    if [[ ! -f "$cache" ]]; then
        echo "Error: $cache not found." >&2
        return 1
    fi
    local ok=1
    if ! grep -qx 'LADYBIRD_ENABLE_UPDATE_CHECK:BOOL=OFF' "$cache"; then
        echo "FAIL: LADYBIRD_ENABLE_UPDATE_CHECK is not OFF in $cache" >&2
        ok=0
    fi
    if ! grep -qx 'LADYBIRD_ENABLE_SPARKLE:BOOL=OFF' "$cache"; then
        echo "FAIL: LADYBIRD_ENABLE_SPARKLE is not OFF in $cache" >&2
        ok=0
    fi
    if [[ "$ok" != "1" ]]; then
        return 1
    fi
    echo "OK: updater & Sparkle disabled in CMakeCache.txt"
}

stage_configure_and_build_tests() {
    # Ensure vcpkg env matches build.sh when reconfiguring CMake with BUILD_TESTING=ON
    local vcpkg_subdir="vcpkg-release"
    case "$PRESET" in
        Debug|All_Debug)   vcpkg_subdir="vcpkg-debug" ;;
        Distribution|Fuzzers) vcpkg_subdir="vcpkg-distribution" ;;
        Sanitizer)         vcpkg_subdir="vcpkg-sanitizer" ;;
    esac

    LADYBIRD_SOURCE_DIR="$UPSTREAM_DIR" \
    LADYBIRD_MAIN_SOURCE_DIR="$MAIN_UPSTREAM_DIR" \
    VCPKG_ROOT="$MAIN_UPSTREAM_DIR/Build/$vcpkg_subdir" \
    cmake -S "$UPSTREAM_DIR" -B "$BUILD_DIR" \
        -DBUILD_TESTING=ON \
        -DLADYBIRD_ENABLE_UPDATE_CHECK=OFF \
        -DLADYBIRD_ENABLE_SPARKLE=OFF

    if [[ "$FULL_MODE" == "1" ]]; then
        ninja -C "$BUILD_DIR" -j "$JOBS"
    else
        local targets=(
            test-web
            TestUIQt
            TestEventLoopImplementation
            TestFullscreenDebounce
            TestLibWebView
            TestLibWeb
            WebDriver
        )
        if [[ "$(uname -s)" == "Darwin" ]]; then
            targets+=(TestNativeTextEditingBindings)
        fi
        ninja -C "$BUILD_DIR" -j "$JOBS" "${targets[@]}"
    fi
}

stage_superbird_test_web() {
    local test_web="$BUILD_DIR/bin/test-web"
    if [[ ! -x "$test_web" ]]; then
        echo "Error: $test_web not found." >&2
        return 1
    fi
    LADYBIRD_SOURCE_DIR="$UPSTREAM_DIR" "$test_web" --per-test-timeout 120 -f "Text/input/superbird-*"
}

stage_ctest() {
    if [[ "$FULL_MODE" == "1" ]]; then
        ctest --test-dir "$BUILD_DIR" --output-on-failure --parallel "$JOBS"
    else
        local regex='^(TestUIQt|TestEventLoopImplementation|TestFullscreenDebounce|TestNativeTextEditingBindings.*|TestLibWebView|TestLibWeb|TestDeadRustExports|test-webdriver-content-blockers|test-webdriver-settings-changes|test-webdriver-bidi|test-webdriver-permissions-settings|test-webdriver-default-browser|test-webdriver-session-history|test-webdriver-web-ui-pages-share-a-process)$'
        echo "Matched CTest cases:"
        ctest --test-dir "$BUILD_DIR" -N -R "$regex"
        ctest --test-dir "$BUILD_DIR" --output-on-failure --parallel "$JOBS" -R "$regex"
    fi
}

stage_macos_bundle_dry_run() {
    local staging rc=0
    staging="$(mktemp -d)"

    if ! cmake --install "$BUILD_DIR" --strip --component ladybird_Runtime --prefix "$staging" > "$staging/install.log"; then
        cat "$staging/install.log" >&2
        rm -rf "$staging"
        return 1
    fi

    local app="$staging/bundle/Superbird.app"
    if [[ ! -d "$app" ]]; then
        echo "Error: installed Superbird.app missing at $app" >&2
        cat "$staging/install.log" >&2
        rm -rf "$staging"
        return 1
    fi

    if ! python3 "$ROOT_DIR/scripts/macos-bundle.py" "$app" "$BUILD_DIR" "$UPSTREAM_DIR/Meta/Entitlements.plist"; then
        rc=1
    elif ! codesign --verify --deep --strict "$app"; then
        rc=1
    fi

    rm -rf "$staging"
    if [[ "$rc" == "0" ]]; then
        echo "OK: portable Superbird.app verified and signed in temp staging dir."
    fi
    return "$rc"
}

run_stage "1. Unit, Rust, Node & JS Syntax Tests" stage_unit_tests

if [[ "$SKIP_BUILD" == "0" ]]; then
    run_stage "2a. Fail-Fast UI Build (ladybird_impl)" stage_build_ui_layer
    run_stage "2b. Full App & WebDriver Build ($PRESET)" stage_build_app_and_webdriver
fi

run_stage "3. CMake Invariant Audit (Updater/Sparkle OFF)" stage_invariant_audit
run_stage "4a. Configure & Build Test Targets" stage_configure_and_build_tests
run_stage "4b. Superbird E2E test-web Suites" stage_superbird_test_web
run_stage "4c. Upstream CTest Suites ($( [[ "$FULL_MODE" == "1" ]] && echo full || echo targeted ))" stage_ctest

if [[ "$(uname -s)" == "Darwin" && "$SKIP_BUNDLE" == "0" ]]; then
    run_stage "5. macOS Portable Bundle & Codesign Dry-Run" stage_macos_bundle_dry_run
fi

print_results
