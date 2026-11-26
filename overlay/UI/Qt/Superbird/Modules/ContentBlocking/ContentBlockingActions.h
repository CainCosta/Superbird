/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Core/Module.h>

namespace Superbird {

class ModuleRegistry;

inline constexpr StringView CONTENT_BLOCKING_MODULE_ID = "content_blocking"sv;

ModuleInfo content_blocking_module_info();
void register_content_blocking_actions(ModuleRegistry&);

}
