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

inline constexpr StringView SPLIT_VIEW_MODULE_ID = "split_view"sv;
inline constexpr StringView SPLIT_VIEW_ACTION_CATEGORY = "Split View"sv;

// The Split View commands on the Superbird action bus. Kept free of Qt so the table and its registration are
// unit-testable on their own.
enum class SplitCommand : u8 {
    SideBySide,
    Stacked,
    Grid,
    AddNextTab,
    SwapPanes,
    ToggleOrientation,
    FocusNextPane,
    EqualizePanes,
    Unsplit,
};

struct SplitActionDefinition {
    SplitCommand command;
    StringView id;
    StringView title;
};

inline constexpr Array<SplitActionDefinition, 9> SPLIT_VIEW_ACTIONS { {
    { SplitCommand::SideBySide, "split_view.side_by_side"sv, "Split View: Side by Side"sv },
    { SplitCommand::Stacked, "split_view.stacked"sv, "Split View: Stacked"sv },
    { SplitCommand::Grid, "split_view.grid"sv, "Split View: Grid of Four Tabs"sv },
    { SplitCommand::AddNextTab, "split_view.add_next_tab"sv, "Split View: Add Next Tab"sv },
    { SplitCommand::SwapPanes, "split_view.swap"sv, "Split View: Swap Panes"sv },
    { SplitCommand::ToggleOrientation, "split_view.toggle_orientation"sv, "Split View: Rotate"sv },
    { SplitCommand::FocusNextPane, "split_view.focus_next_pane"sv, "Split View: Focus Next Pane"sv },
    { SplitCommand::EqualizePanes, "split_view.equalize"sv, "Split View: Equal Sizes"sv },
    { SplitCommand::Unsplit, "split_view.unsplit"sv, "Split View: Unsplit"sv },
} };

class SplitCommandHandler {
public:
    virtual ~SplitCommandHandler() = default;
    virtual bool can_run(SplitCommand) const = 0;
    virtual void run(SplitCommand) = 0;
};

void register_split_view_actions(ModuleRegistry&, SplitCommandHandler& handler);

}
