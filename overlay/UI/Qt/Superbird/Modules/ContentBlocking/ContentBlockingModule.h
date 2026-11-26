/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Modules/ContentBlocking/ContentBlockingActions.h>

namespace Superbird {

class ContentBlockingModule final : public SuperbirdModule {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit ContentBlockingModule(Ladybird::BrowserWindow&) { }

    static ModuleInfo module_info() { return content_blocking_module_info(); }
    static void register_actions(ModuleRegistry& registry) { register_content_blocking_actions(registry); }

    StringView id() const override { return CONTENT_BLOCKING_MODULE_ID; }
};

}
