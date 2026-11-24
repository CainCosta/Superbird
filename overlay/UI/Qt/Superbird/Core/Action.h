/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/StringView.h>

namespace Superbird {

// Describes a user-triggerable action exposed by a Superbird module (for Command Palette,
// menus, and keyboard shortcuts). Uses StringView for static metadata so action descriptors
// stay lightweight and header-only testable without linking LibWebView/Qt.
struct ActionDescriptor {
    StringView id;
    StringView module_id;
    StringView title;
    StringView category;
    Optional<StringView> shortcut {};
    Function<bool()> is_available {};
    Function<void()> trigger {};

    bool available() const
    {
        return !is_available || is_available();
    }
};

}
