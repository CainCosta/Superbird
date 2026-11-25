/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/CommandPaletteActions.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/PaletteRanking.h>

#include <QIcon>
#include <QPointer>
#include <QString>

#include <vector>

class QShortcut;

namespace Ladybird {
class BrowserWindow;
class Tab;
}

namespace Superbird {

class CommandPalettePopover;
class ModuleRegistry;

// The Command Palette (Cmd/Ctrl+K or F2): a floating sheet that fuzzy-searches the open tabs, bookmarks, recent
// history and every action on the ModuleRegistry bus, evaluates `=` arithmetic, and navigates to typed addresses.
class CommandPaletteModule final : public SuperbirdModule {
public:
    AK_ALLOC_WITH_KMALLOC;

    static constexpr StringView MODULE_ID = COMMAND_PALETTE_MODULE_ID;
    static ModuleInfo module_info();
    static void register_actions(ModuleRegistry&);

    explicit CommandPaletteModule(Ladybird::BrowserWindow&);
    virtual ~CommandPaletteModule() override;

    virtual StringView id() const override { return MODULE_ID; }

    virtual void on_enabled_changed(bool enabled) override;
    virtual void on_chrome_style_changed() override;

    void open();

private:
    // What activating a row does, kept next to the pure Palette::Entry it was ranked as.
    struct Target {
        QIcon icon;
        QString detail;
        QPointer<Ladybird::BrowserWindow> window;
        QPointer<Ladybird::Tab> tab;
    };

    void install_shortcuts();
    void remove_shortcuts();
    void gather_base_entries();
    void update_results(QString const& query);
    enum class OpenIn : u8 {
        CurrentTab,
        NewTab,
    };
    void activate(size_t row, OpenIn);
    void close_tab(size_t row);
    void navigate_current_tab(QString const& input);

    Ladybird::BrowserWindow& m_window;
    Vector<QPointer<QShortcut>> m_shortcuts;
    QPointer<CommandPalettePopover> m_popover;

    // Tabs, bookmarks and actions, gathered when the palette opens.
    std::vector<Palette::Entry> m_base_entries;
    Vector<Target> m_base_targets;

    // What the visible rows stand for, in row order.
    std::vector<Palette::Entry> m_row_entries;
    Vector<Target> m_row_targets;
    QString m_calculator_result;
    HashMap<String, QIcon> m_favicon_cache;
    QPointer<QObject> m_shortcut_priority_filter;
};

}
