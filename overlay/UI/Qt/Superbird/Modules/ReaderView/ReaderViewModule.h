/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderDocument.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderViewActions.h>

#include <QMetaObject>
#include <QPointer>
#include <QString>

class QAction;
class QShortcut;

namespace Ladybird {
class BrowserWindow;
class Tab;
}

namespace Superbird {

class ModuleRegistry;
class ReaderOverlay;

// Reader View: shows an article page as clean, script-free text in a sheet over the tab, without touching the page.
// A book icon in the address field appears on article-like pages; Cmd/Ctrl+Alt+R (Ctrl+Cmd+R on macOS) and the
// Command Palette toggle it on any page.
class ReaderViewModule final : public SuperbirdModule {
public:
    AK_ALLOC_WITH_KMALLOC;

    static constexpr StringView MODULE_ID = READER_VIEW_MODULE_ID;
    static ModuleInfo module_info();
    static void register_actions(ModuleRegistry&);

    explicit ReaderViewModule(Ladybird::BrowserWindow&);
    virtual ~ReaderViewModule() override;

    virtual StringView id() const override { return MODULE_ID; }

    virtual void on_enabled_changed(bool enabled) override;
    virtual void on_chrome_style_changed() override;
    virtual void on_tab_added(Ladybird::Tab&) override;
    virtual void on_tab_removed(Ladybird::Tab&) override;
    virtual void on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab) override;
    virtual void on_active_tab_changed(Ladybird::Tab*) override;

    bool can_run(ReaderCommand) const;
    void run(ReaderCommand);

private:
    struct TabState {
        AK_ALLOC_WITH_KMALLOC;

        QPointer<Ladybird::Tab> tab;
        QPointer<QAction> action;
        QPointer<ReaderOverlay> overlay;
        Optional<u64> navigation_listener_id;
        QMetaObject::Connection loaded_connection;
        // The URL last checked for an article, and what the check found.
        QString checked_url;
        bool readerable { false };
        // Bumped on every navigation, so answers about an earlier page are ignored.
        u64 generation { 0 };
        bool opening { false };
    };

    bool is_enabled() const;
    void install();
    void uninstall();
    void attach(Ladybird::Tab&);
    void attach_view(TabState&);
    void detach(Ladybird::Tab&);
    TabState* state_for(Ladybird::Tab*);
    TabState const* state_for(Ladybird::Tab*) const;

    void page_started_loading(TabState&);
    void check_readerable(TabState&);
    void toggle(Ladybird::Tab&);
    void open_reader(TabState&);
    void close_reader(TabState&);
    void update_action(TabState&);
    void apply_preferences_everywhere();

    Ladybird::BrowserWindow& m_window;
    HashMap<Ladybird::Tab*, NonnullOwnPtr<TabState>> m_tabs;
    QPointer<QShortcut> m_shortcut;
};

}
