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
    "$TMP_DIR/ak_stubs.cpp" \
    -o "$TMP_DIR/test_tree_tab_model"
"$TMP_DIR/test_tree_tab_model"

"$CXX" "${COMMON_FLAGS[@]}" \
    "$ROOT_DIR/tests/TestModuleRegistry.cpp" \
    "$ROOT_DIR/overlay/UI/Qt/Superbird/Core/ModuleRegistry.cpp" \
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
