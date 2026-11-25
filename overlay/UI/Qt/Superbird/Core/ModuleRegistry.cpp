/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/CharacterTypes.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>

namespace Superbird {

ModuleRegistry& ModuleRegistry::the()
{
    static ModuleRegistry instance;
    return instance;
}

bool ModuleRegistry::register_module(ModuleInfo info)
{
    if (info.id.is_empty() || has_module(info.id))
        return false;

    m_modules.append(info);
    m_enabled_state.set(info.id, info.enabled_by_default);
    return true;
}

Optional<ModuleInfo> ModuleRegistry::module_info(StringView id) const
{
    for (auto const& info : m_modules) {
        if (info.id == id)
            return info;
    }
    return {};
}

bool ModuleRegistry::has_module(StringView id) const
{
    return m_enabled_state.contains(id);
}

bool ModuleRegistry::is_enabled(StringView id) const
{
    return m_enabled_state.get(id).value_or(false);
}

bool ModuleRegistry::set_enabled(StringView id, bool enabled)
{
    auto existing = m_enabled_state.get(id);
    if (!existing.has_value() || *existing == enabled)
        return false;

    m_enabled_state.set(id, enabled);
    for (auto* observer : m_observers) {
        if (observer)
            observer->module_enabled_changed(id, enabled);
    }
    return true;
}

void ModuleRegistry::add_observer(ModuleRegistryObserver& observer)
{
    for (auto* existing : m_observers) {
        if (existing == &observer)
            return;
    }
    m_observers.append(&observer);
}

void ModuleRegistry::remove_observer(ModuleRegistryObserver& observer)
{
    m_observers.remove_all_matching([&](auto* candidate) { return candidate == &observer; });
}

void ModuleRegistry::register_action(ActionDescriptor action)
{
    if (action.id.is_empty())
        return;

    m_actions.remove_all_matching([&](auto const& existing) { return existing.id == action.id; });
    m_actions.append(move(action));
}

void ModuleRegistry::unregister_actions_for_module(StringView module_id)
{
    m_actions.remove_all_matching([&](auto const& existing) { return existing.module_id == module_id; });
}

static bool contains_ascii_case_insensitive(StringView haystack, StringView needle)
{
    if (needle.is_empty())
        return true;
    if (needle.length() > haystack.length())
        return false;

    for (size_t i = 0; i + needle.length() <= haystack.length(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.length(); ++j) {
            if (to_ascii_lowercase(haystack[i + j]) != to_ascii_lowercase(needle[j])) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

bool ModuleRegistry::matches_query(ActionDescriptor const& action, StringView query)
{
    if (query.is_empty())
        return true;

    return contains_ascii_case_insensitive(action.title, query)
        || contains_ascii_case_insensitive(action.category, query)
        || contains_ascii_case_insensitive(action.id, query);
}

ActionDescriptor const* ModuleRegistry::action(StringView id) const
{
    for (auto const& action : m_actions) {
        if (action.id == id)
            return &action;
    }
    return nullptr;
}

Vector<ActionDescriptor const*> ModuleRegistry::matching_actions(Optional<StringView> query) const
{
    Vector<ActionDescriptor const*> matches;
    auto filter = query.value_or(""sv);

    for (auto const& action : m_actions) {
        if (!action.module_id.is_empty() && !is_enabled(action.module_id))
            continue;
        if (!action.available())
            continue;
        if (!matches_query(action, filter))
            continue;
        matches.append(&action);
    }
    return matches;
}

}
