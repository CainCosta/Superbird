#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/AK" "$TMP_DIR/LibWebView" "$TMP_DIR/UI/Qt"

cat > "$TMP_DIR/AK/Debug.h" <<'EOF'
#pragma once
EOF

cat > "$TMP_DIR/AK/Backtrace.h" <<'EOF'
#pragma once
namespace AK { inline void dump_backtrace(unsigned = 0, unsigned = 0) {} }
EOF

cat > "$TMP_DIR/LibWebView/Settings.h" <<'EOF'
#pragma once
#include <AK/Types.h>
namespace WebView {
enum class TreeTabCloseBehavior : u8 { PromoteFirstChild, AttachToGrandparent, CloseBranch };
enum class TreeTabChildPosition : u8 { Top, Bottom };
}
EOF

cat > "$TMP_DIR/UI/Qt/SessionStore.h" <<'EOF'
#pragma once
#include <AK/Types.h>
namespace Ladybird { using SessionTabId = i64; }
EOF

cat > "$TMP_DIR/ak_stubs.cpp" <<'EOF'
#include <AK/Assertions.h>
#include <AK/kmalloc.h>
#include <cstdio>
#include <cstdlib>
#if defined(__linux__)
#    include <malloc.h>
#endif

extern "C" bool ak_colorize_output() { return false; }
extern "C" void ak_trap() { std::abort(); }
extern "C" void ak_verification_failed(char const* msg) {
    std::fprintf(stderr, "VERIFY FAILED: %s\n", msg);
    std::abort();
}
void* ak_kmalloc(HeapPartition, size_t size) { return std::malloc(size); }
void* ak_kcalloc(size_t count, size_t size) { return std::calloc(count, size); }
void ak_kfree(void* ptr) { std::free(ptr); }
size_t ak_kmalloc_usable_size(void const* ptr) {
#if defined(__linux__)
    return malloc_usable_size(const_cast<void*>(ptr));
#else
    (void)ptr;
    return 0;
#endif
}
EOF

CXX="${CXX:-g++}"
COMMON_FLAGS=(-std=c++23 -Wall -Wextra -Werror -Wno-unused-parameter -I"$TMP_DIR" -I"$ROOT_DIR/overlay" -I"$ROOT_DIR/upstream")
if "$CXX" --version 2>&1 | grep -qi clang; then
    COMMON_FLAGS+=(-Wno-unqualified-std-cast-call -Wno-user-defined-literals -Wno-unknown-warning-option)
else
    COMMON_FLAGS+=(-Wno-literal-suffix)
fi

"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestTreeTabModel.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/TreeTabs/TreeTabModel.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/TreeTabs/TreeTabFilter.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_tree_tab_model"
"$TMP_DIR/test_tree_tab_model"

"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestModuleRegistry.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleRegistry.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleSettings.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_module_registry"
"$TMP_DIR/test_module_registry"

"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestTreeTabsActions.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/TreeTabs/TreeTabsActions.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleRegistry.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_tree_tabs_actions"
"$TMP_DIR/test_tree_tabs_actions"

CP_DIR="$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/CommandPalette"
"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestCommandPalette.cpp" \
    "$CP_DIR/FuzzyMatcher.cpp" \
    "$CP_DIR/MathExpression.cpp" \
    "$CP_DIR/PaletteRanking.cpp" \
    "$CP_DIR/CommandPaletteActions.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleRegistry.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_command_palette"
"$TMP_DIR/test_command_palette"

RV_DIR="$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/ReaderView"
"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestReaderView.cpp" \
    "$RV_DIR/HtmlTree.cpp" \
    "$RV_DIR/UrlResolver.cpp" \
    "$RV_DIR/ArticleExtractor.cpp" \
    "$RV_DIR/ReaderDocument.cpp" \
    -o "$TMP_DIR/test_reader_view"
"$TMP_DIR/test_reader_view"

"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestReaderViewActions.cpp" \
    "$RV_DIR/ReaderViewActions.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleRegistry.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_reader_view_actions"
"$TMP_DIR/test_reader_view_actions"

WS_DIR="$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/Workspaces"
"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestWorkspaces.cpp" \
    "$WS_DIR/WorkspaceModel.cpp" \
    "$WS_DIR/WorkspacePresets.cpp" \
    "$WS_DIR/WorkspacesActions.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleRegistry.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_workspaces"
"$TMP_DIR/test_workspaces"

SV_DIR="$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/SplitView"
"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestSplitLayout.cpp" \
    "$SV_DIR/SplitLayout.cpp" \
    "$SV_DIR/SplitViewActions.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleRegistry.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_split_layout"
"$TMP_DIR/test_split_layout"

GL_DIR="$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/Glance"
"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestGlance.cpp" \
    "$GL_DIR/GlanceModel.cpp" \
    "$GL_DIR/GlanceActions.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleRegistry.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_glance"
"$TMP_DIR/test_glance"

CB_DIR="$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/ContentBlocking"
"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestContentBlocking.cpp" \
    "$CB_DIR/ContentBlockingActions.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleSettings.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_content_blocking"
"$TMP_DIR/test_content_blocking"

PW_DIR="$ROOT_DIR/overlay/UI/Qt/Superbird/Modules/Passwords/Core"
OPENSSL_CFLAGS=()
OPENSSL_LIBS=()
if [ -d "$ROOT_DIR/upstream/Build/release/vcpkg_installed" ]; then
    VCPKG_OSSL_INC="$(find "$ROOT_DIR/upstream/Build/release/vcpkg_installed" -maxdepth 3 -path "*/include/openssl/evp.h" 2>/dev/null | head -n 1 || true)"
    if [ -n "$VCPKG_OSSL_INC" ]; then
        VCPKG_PREFIX="$(dirname "$(dirname "$(dirname "$VCPKG_OSSL_INC")")")"
        OPENSSL_CFLAGS+=(-I"$VCPKG_PREFIX/include")
        OPENSSL_LIBS+=(-L"$VCPKG_PREFIX/lib" -Wl,-rpath,"$VCPKG_PREFIX/lib" -lcrypto)
    fi
fi
if [ ${#OPENSSL_LIBS[@]} -eq 0 ] && command -v pkg-config >/dev/null 2>&1 && pkg-config --exists libcrypto; then
    read -r -a OPENSSL_CFLAGS <<< "$(pkg-config --cflags libcrypto)"
    read -r -a OPENSSL_LIBS <<< "$(pkg-config --libs libcrypto)"
elif [ ${#OPENSSL_LIBS[@]} -eq 0 ]; then
    for brew_prefix in /opt/homebrew/opt/openssl@3 /opt/homebrew/opt/openssl@4 /usr/local/opt/openssl@3; do
        if [ -f "$brew_prefix/include/openssl/evp.h" ]; then
            OPENSSL_CFLAGS+=(-I"$brew_prefix/include")
            OPENSSL_LIBS+=(-L"$brew_prefix/lib" -Wl,-rpath,"$brew_prefix/lib" -lcrypto)
            break
        fi
    done
fi
if [ ${#OPENSSL_LIBS[@]} -eq 0 ]; then
    OPENSSL_LIBS+=(-lcrypto)
fi

"$CXX" "${COMMON_FLAGS[@]}" "${OPENSSL_CFLAGS[@]}" \
    "$ROOT_DIR/tests/TestPasswords.cpp" \
    "$PW_DIR/BitwardenCrypto.cpp" \
    "$PW_DIR/BitwardenProtocol.cpp" \
    "$PW_DIR/LocalVaultCrypto.cpp" \
    "$PW_DIR/UriMatcher.cpp" \
    "$PW_DIR/Totp.cpp" \
    "$PW_DIR/PasswordGenerator.cpp" \
    "$PW_DIR/CsvTransfer.cpp" \
    "$PW_DIR/OnePasswordParser.cpp" \
    "$PW_DIR/PasswordsActions.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleRegistry.cpp" \
    "$TMP_DIR/ak_stubs.cpp" \
    "${OPENSSL_LIBS[@]}" -pthread \
    -o "$TMP_DIR/test_passwords"
"$TMP_DIR/test_passwords"

if [ -f "$ROOT_DIR/upstream/Libraries/LibWeb/ContentBlocker/Rust/src/superbird.rs" ] && command -v cargo >/dev/null 2>&1; then
    cargo test --manifest-path "$ROOT_DIR/upstream/Libraries/LibWeb/ContentBlocker/Rust/Cargo.toml"
fi

if command -v node >/dev/null 2>&1; then
    node "$ROOT_DIR/tests/test-build-ubo-resources.mjs"
fi
