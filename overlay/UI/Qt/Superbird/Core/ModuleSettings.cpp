/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Core/Module.h>

#include <cstdio>

namespace Superbird {

static void append_json_string(std::string& builder, StringView text)
{
    builder += '"';
    for (size_t index = 0; index < text.length(); ++index) {
        auto byte = static_cast<unsigned char>(text.characters_without_null_termination()[index]);
        switch (byte) {
        case '"':
            builder += "\\\"";
            break;
        case '\\':
            builder += "\\\\";
            break;
        case '\n':
            builder += "\\n";
            break;
        case '\r':
            builder += "\\r";
            break;
        case '\t':
            builder += "\\t";
            break;
        default:
            if (byte < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
                builder += escaped;
            } else {
                builder += static_cast<char>(byte);
            }
        }
    }
    builder += '"';
}

static void append_member(std::string& builder, StringView key, StringView value, bool first = false)
{
    if (!first)
        builder += ',';
    append_json_string(builder, key);
    builder += ':';
    append_json_string(builder, value);
}

std::string module_settings_schema_json(ModuleInfo const& info)
{
    std::string builder;
    builder += "{\"options\":[";
    for (size_t index = 0; index < info.options.size(); ++index) {
        auto const& option = info.options[index];
        if (index > 0)
            builder += ',';
        builder += '{';
        append_member(builder, "id"sv, option.id, true);
        append_member(builder, "label"sv, option.label);
        append_member(builder, "description"sv, option.description);
        append_member(builder, "kind"sv, option.kind == ModuleOption::Kind::Switch ? "switch"sv : "choice"sv);
        append_member(builder, "default"sv, option.default_value);
        builder += ",\"choices\":[";
        for (size_t choice_index = 0; choice_index < option.choices.size(); ++choice_index) {
            if (choice_index > 0)
                builder += ',';
            builder += '{';
            append_member(builder, "value"sv, option.choices[choice_index].value, true);
            append_member(builder, "label"sv, option.choices[choice_index].label);
            builder += '}';
        }
        builder += "]}";
    }
    builder += "],\"shortcuts\":[";
    for (size_t index = 0; index < info.shortcuts.size(); ++index) {
        auto const& shortcut = info.shortcuts[index];
        if (index > 0)
            builder += ',';
        builder += '{';
        append_member(builder, "command"sv, shortcut.command_id, true);
        append_member(builder, "title"sv, shortcut.title);
        append_member(builder, "default"sv, shortcut.default_shortcut);
        builder += '}';
    }
    builder += "]}";
    return builder;
}

}
