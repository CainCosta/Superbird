/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/Noncopyable.h>
#include <AK/Optional.h>
#include <AK/StringView.h>
#include <AK/Vector.h>
#include <UI/Qt/Superbird/Core/Action.h>
#include <UI/Qt/Superbird/Core/Module.h>

namespace Superbird {

class ModuleRegistryObserver {
public:
    virtual ~ModuleRegistryObserver() = default;
    virtual void module_enabled_changed(StringView module_id, bool enabled) = 0;
};

// Central registry for all compiled Superbird modules, their runtime enabled state,
// and app-wide ActionDescriptors. Pure AK (no Qt dependency) so it is fast to unit-test.
class ModuleRegistry {
    AK_MAKE_NONCOPYABLE(ModuleRegistry);
    AK_MAKE_NONMOVABLE(ModuleRegistry);

public:
    ModuleRegistry() = default;
    static ModuleRegistry& the();

    // Registers a compiled module (idempotent by id).
    bool register_module(ModuleInfo info);
    Vector<ModuleInfo> const& modules() const { return m_modules; }
    Optional<ModuleInfo> module_info(StringView id) const;
    bool has_module(StringView id) const;

    bool is_enabled(StringView id) const;
    bool set_enabled(StringView id, bool enabled);

    // Syncs runtime enabled state from persisted settings. Modules absent from overrides
    // revert to their compiled `enabled_by_default` value.
    template<typename MapType>
    void sync_from_overrides(MapType const& overrides)
    {
        for (auto const& info : m_modules) {
            bool target = info.enabled_by_default;
            for (auto const& [key, value] : overrides) {
                if (key.bytes_as_string_view() == info.id) {
                    target = value;
                    break;
                }
            }
            set_enabled(info.id, target);
        }
    }

    void add_observer(ModuleRegistryObserver&);
    void remove_observer(ModuleRegistryObserver&);

    // App-wide action bus. Modules register their actions once at startup; triggers act on the active window.
    void register_action(ActionDescriptor);
    void unregister_actions_for_module(StringView module_id);

    // Returns pointers to currently available actions belonging to enabled modules (or module-less global actions),
    // optionally filtered by case-insensitive substring match against title/category/id.
    Vector<ActionDescriptor const*> matching_actions(Optional<StringView> query = {}) const;

    static bool matches_query(ActionDescriptor const&, StringView query);

private:
    Vector<ModuleInfo> m_modules;
    HashMap<StringView, bool> m_enabled_state;
    Vector<ModuleRegistryObserver*> m_observers;
    Vector<ActionDescriptor> m_actions;
};

}
