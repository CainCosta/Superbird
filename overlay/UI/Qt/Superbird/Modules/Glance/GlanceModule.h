/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Modules/Glance/GlanceActions.h>
#include <UI/Qt/Superbird/Modules/Glance/GlanceModel.h>

#include <QPointer>

class QObject;

namespace Ladybird {
class BrowserWindow;
class Tab;
}

namespace Superbird {

class GlanceOverlay;
class ModuleRegistry;

// Glance: peek at a link in a floating sheet without opening a tab. Alt/Option+click a link, or pick "Peek in Glance"
// from its context menu. "Open as Tab" promotes the page to a real tab, nested under the tab it came from when
// Tree-Style Tabs is on.
class GlanceModule final : public SuperbirdModule {
public:
    AK_ALLOC_WITH_KMALLOC;

    static constexpr StringView MODULE_ID = GLANCE_MODULE_ID;
    static ModuleInfo module_info();
    static void register_actions(ModuleRegistry&);

    explicit GlanceModule(Ladybird::BrowserWindow&);
    virtual ~GlanceModule() override;

    virtual StringView id() const override { return MODULE_ID; }

    virtual void on_enabled_changed(bool enabled) override;
    virtual void on_chrome_style_changed() override;
    virtual void on_tab_added(Ladybird::Tab&) override;
    virtual void on_tab_removed(Ladybird::Tab&) override;
    virtual void on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab) override;
    virtual void on_active_tab_changed(Ladybird::Tab*) override;
    virtual void extend_link_context_menu(Ladybird::Tab&, Ladybird::Menu&, URL::URL const&) override;

    void open(Ladybird::Tab& opener, URL::URL const&);
    bool can_run(GlanceCommand) const;
    void run(GlanceCommand);

private:
    bool is_enabled() const;
    void watch(Ladybird::Tab&);
    void unwatch(Ladybird::Tab&);
    Glance::OpenerId opener_id(Ladybird::Tab&);
    Ladybird::Tab* opener_tab(Glance::OpenerId) const;
    void promote();
    void dismiss();
    void update_strip();

    Ladybird::BrowserWindow& m_window;
    Glance::GlanceModel m_model;
    QPointer<GlanceOverlay> m_overlay;
    QPointer<QObject> m_click_filter;
    HashMap<Ladybird::Tab*, Glance::OpenerId> m_opener_ids;
    Glance::OpenerId m_next_opener_id { 1 };
};

}
