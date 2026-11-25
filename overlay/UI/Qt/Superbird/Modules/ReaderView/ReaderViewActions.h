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

inline constexpr StringView READER_VIEW_MODULE_ID = "reader_view"sv;

// The Reader View commands on the Superbird action bus. Kept free of Qt so the table and its registration are
// unit-testable on their own.
enum class ReaderCommand : u8 {
    Toggle,
    IncreaseFontSize,
    DecreaseFontSize,
    CycleFont,
    CycleTheme,
    CycleWidth,
};

struct ReaderActionDefinition {
    ReaderCommand command;
    StringView id;
    StringView title;
};

inline constexpr StringView READER_VIEW_ACTION_CATEGORY = "Reader View"sv;

inline constexpr Array<ReaderActionDefinition, 6> READER_VIEW_ACTIONS { {
    { ReaderCommand::Toggle, "reader_view.toggle"sv, "Toggle Reader View"sv },
    { ReaderCommand::IncreaseFontSize, "reader_view.increase_font_size"sv, "Reader View: Larger Text"sv },
    { ReaderCommand::DecreaseFontSize, "reader_view.decrease_font_size"sv, "Reader View: Smaller Text"sv },
    { ReaderCommand::CycleFont, "reader_view.cycle_font"sv, "Reader View: Next Font"sv },
    { ReaderCommand::CycleTheme, "reader_view.cycle_theme"sv, "Reader View: Next Theme"sv },
    { ReaderCommand::CycleWidth, "reader_view.cycle_width"sv, "Reader View: Next Column Width"sv },
} };

class ReaderCommandHandler {
public:
    virtual ~ReaderCommandHandler() = default;
    virtual bool can_run(ReaderCommand) const = 0;
    virtual void run(ReaderCommand) = 0;
};

void register_reader_view_actions(ModuleRegistry&, ReaderCommandHandler& handler);

}
