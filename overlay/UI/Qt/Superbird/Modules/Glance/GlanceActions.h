/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Array.h>
#include <AK/StringView.h>
#include <AK/Types.h>

namespace Superbird {

class ModuleRegistry;

inline constexpr StringView GLANCE_MODULE_ID = "glance"sv;
inline constexpr StringView GLANCE_ACTION_CATEGORY = "Glance"sv;

// The Glance commands on the Superbird action bus, offered while a Glance is open. Kept free of Qt so the table and its
// registration are unit-testable on their own.
enum class GlanceCommand : u8 {
    OpenAsTab,
    CopyUrl,
    Close,
};

struct GlanceActionDefinition {
    GlanceCommand command;
    StringView id;
    StringView title;
};

inline constexpr Array<GlanceActionDefinition, 3> GLANCE_ACTIONS { {
    { GlanceCommand::OpenAsTab, "glance.open_as_tab"sv, "Glance: Open as Tab"sv },
    { GlanceCommand::CopyUrl, "glance.copy_url"sv, "Glance: Copy URL"sv },
    { GlanceCommand::Close, "glance.close"sv, "Glance: Close"sv },
} };

class GlanceCommandHandler {
public:
    virtual ~GlanceCommandHandler() = default;
    virtual bool can_run(GlanceCommand) const = 0;
    virtual void run(GlanceCommand) = 0;
};

void register_glance_actions(ModuleRegistry&, GlanceCommandHandler& handler);

}
