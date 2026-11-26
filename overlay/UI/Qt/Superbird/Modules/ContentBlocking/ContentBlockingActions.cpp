/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <UI/Qt/Superbird/Modules/ContentBlocking/ContentBlockingActions.h>

namespace Superbird {

static constexpr Array<ModuleChoice, 2> ENGINE_MODE_CHOICES { {
    { "ubo"sv, "uBO-compatible"sv },
    { "default"sv, "Default Ladybird blocker"sv },
} };

static constexpr Array<ModuleOption, 3> CONTENT_BLOCKING_OPTIONS { {
    {
        .id = "engine_mode"sv,
        .label = "Blocking engine"sv,
        .description = "Switch between uBO-compatible content blocking and Ladybird's default content blocker."sv,
        .kind = ModuleOption::Kind::Choice,
        .default_value = "ubo"sv,
        .choices = ENGINE_MODE_CHOICES.span(),
    },
    {
        .id = "trust_custom_filters"sv,
        .label = "Trust custom filters"sv,
        .description = "Allow custom filter rules to invoke trusted-* scriptlets. Disabled by default for safety."sv,
        .kind = ModuleOption::Kind::Switch,
        .default_value = "false"sv,
    },
    {
        .id = "enable_logger"sv,
        .label = "Enable filtering logger"sv,
        .description = "Record per-tab content blocking decisions for inspection. Disabled by default to avoid overhead."sv,
        .kind = ModuleOption::Kind::Switch,
        .default_value = "false"sv,
    },
} };

ModuleInfo content_blocking_module_info()
{
    return {
        .id = CONTENT_BLOCKING_MODULE_ID,
        .name = "uBO-compatible Content Blocking"sv,
        .description = "Runs uBO-compatible filter lists, scriptlets, redirects, and procedural hiding rules. Turn off or change the engine below to use Ladybird's default blocker."sv,
        .enabled_by_default = true,
        .options = CONTENT_BLOCKING_OPTIONS.span(),
    };
}

void register_content_blocking_actions(ModuleRegistry&)
{
}

}
