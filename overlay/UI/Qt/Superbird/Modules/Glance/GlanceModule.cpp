/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <LibURL/Parser.h>
#include <UI/Qt/Actions.h>
#include <UI/Qt/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/Glance/GlanceModule.h>
#include <UI/Qt/Superbird/Modules/Glance/GlanceOverlay.h>
#include <UI/Qt/Tab.h>

#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>

namespace Superbird {

static std::string to_std_string(ByteString const& string)
{
    return std::string(string.characters(), string.length());
}

static QString to_qstring(std::string const& string)
{
    return QString::fromUtf8(string.data(), static_cast<qsizetype>(string.size()));
}

namespace {

// Watches the window's page views for Alt/Option+click on a link, and takes the click (and its release) for Glance.
class AltClickFilter final : public QObject {
public:
    AltClickFilter(QObject* parent, Function<bool(QObject*)> on_alt_click)
        : QObject(parent)
        , m_on_alt_click(move(on_alt_click))
    {
    }

private:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            auto modifiers = mouse->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
            if (mouse->button() == Qt::LeftButton && modifiers == Qt::AltModifier && m_on_alt_click(watched)) {
                m_swallow_release = true;
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease && m_swallow_release) {
            m_swallow_release = false;
            return true;
        }
        return false;
    }

    Function<bool(QObject*)> m_on_alt_click;
    bool m_swallow_release { false };
};

class ActiveWindowGlanceHandler final : public GlanceCommandHandler {
public:
    virtual bool can_run(GlanceCommand command) const override
    {
        auto* module = active_module();
        return module && module->can_run(command);
    }

    virtual void run(GlanceCommand command) override
    {
        if (auto* module = active_module())
            module->run(command);
    }

private:
    static GlanceModule* active_module()
    {
        auto* window = Ladybird::Application::the().active_window_if_any();
        if (!window)
            return nullptr;
        return static_cast<GlanceModule*>(window->superbird().module(GLANCE_MODULE_ID));
    }
};

}

static constexpr StringView ALT_CLICK_OPTION = "alt_click"sv;

static constexpr Array<ModuleOption, 1> OPTIONS { {
    { .id = ALT_CLICK_OPTION, .label = "Peek with Alt/Option+click"sv, .description = "Alt/Option+click a link to open it in Glance. Peek in Glance in a link's menu works either way."sv },
} };

static constexpr Array<ModuleShortcut, 3> SHORTCUTS { {
    { .command_id = "glance.open_as_tab"sv, .title = "Open the peeked page as a tab"sv },
    { .command_id = "glance.copy_url"sv, .title = "Copy the peeked page's URL"sv },
    { .command_id = "glance.close"sv, .title = "Close Glance"sv },
} };

ModuleInfo GlanceModule::module_info()
{
    return {
        .id = MODULE_ID,
        .name = "Glance"sv,
        .description = "Alt/Option+click a link (or choose Peek in Glance) to preview it in a floating sheet, then close it or open it as a tab."sv,
        .enabled_by_default = true,
        .options = OPTIONS.span(),
        .shortcuts = SHORTCUTS.span(),
    };
}

GlanceModule::GlanceModule(Ladybird::BrowserWindow& window)
    : m_window(window)
{
    m_click_filter = new AltClickFilter(&m_window, [this](QObject* watched) {
        if (!is_enabled() || !WindowHost::module_option_is_on(MODULE_ID, ALT_CLICK_OPTION))
            return false;
        for (int index = 0; index < m_window.tab_count(); ++index) {
            auto* tab = m_window.tab_at(index);
            if (!tab || !tab->is_loaded() || watched != &tab->view())
                continue;
            auto link = tab->hovered_link();
            if (link.isEmpty())
                return false;
            auto url = URL::Parser::basic_parse(ak_string_from_qstring(link));
            if (!url.has_value() || !Glance::is_glanceable_url(to_std_string(url->to_byte_string())))
                return false;
            open(*tab, *url);
            return true;
        }
        return false;
    });
}

GlanceModule::~GlanceModule()
{
    if (m_overlay)
        delete m_overlay.data();
}

bool GlanceModule::is_enabled() const
{
    return ModuleRegistry::the().is_enabled(MODULE_ID);
}

void GlanceModule::watch(Ladybird::Tab& tab)
{
    auto install = [this, tab_pointer = QPointer(&tab)] {
        if (tab_pointer && tab_pointer->is_loaded() && m_click_filter)
            tab_pointer->view().installEventFilter(m_click_filter);
    };
    if (tab.is_loaded())
        install();
    else
        QObject::connect(&tab, &Ladybird::Tab::loaded, m_click_filter, install);
}

void GlanceModule::unwatch(Ladybird::Tab& tab)
{
    if (tab.is_loaded() && m_click_filter)
        tab.view().removeEventFilter(m_click_filter);
}

Glance::OpenerId GlanceModule::opener_id(Ladybird::Tab& tab)
{
    if (auto existing = m_opener_ids.get(&tab); existing.has_value())
        return *existing;
    auto id = m_next_opener_id++;
    m_opener_ids.set(&tab, id);
    return id;
}

Ladybird::Tab* GlanceModule::opener_tab(Glance::OpenerId id) const
{
    for (auto const& it : m_opener_ids) {
        if (it.value == id)
            return it.key;
    }
    return nullptr;
}

void GlanceModule::on_enabled_changed(bool enabled)
{
    if (!enabled)
        dismiss();
}

void GlanceModule::on_chrome_style_changed()
{
    if (m_overlay)
        m_overlay->update_chrome_style();
}

void GlanceModule::on_tab_added(Ladybird::Tab& tab)
{
    watch(tab);
}

void GlanceModule::on_tab_removed(Ladybird::Tab& tab)
{
    unwatch(tab);
    if (auto id = m_opener_ids.take(&tab); id.has_value())
        m_model.opener_closed(*id);
    if (m_overlay && m_overlay->tab() == &tab)
        dismiss();
}

void GlanceModule::on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab)
{
    auto* old_pointer = const_cast<Ladybird::Tab*>(&old_tab);
    unwatch(*old_pointer);
    if (auto id = m_opener_ids.take(old_pointer); id.has_value())
        m_opener_ids.set(&new_tab, *id);
    if (m_overlay && m_overlay->tab() == old_pointer)
        dismiss();
    watch(new_tab);
}

void GlanceModule::on_active_tab_changed(Ladybird::Tab*)
{
    // A Glance belongs to the tab it was opened over: it hides and shows along with that tab, and stays open meanwhile.
}

void GlanceModule::extend_link_context_menu(Ladybird::Tab& tab, Ladybird::Menu& menu, URL::URL const& url)
{
    if (!Glance::is_glanceable_url(to_std_string(url.to_byte_string())))
        return;
    menu.add_action(Ladybird::Action::create("Peek in Glance"sv, Ladybird::ActionID::SuperbirdModuleAction, [this, tab_pointer = QPointer(&tab), url] {
        if (tab_pointer && is_enabled())
            open(*tab_pointer, url);
    }));
}

void GlanceModule::open(Ladybird::Tab& opener, URL::URL const& url)
{
    if (!is_enabled() || !m_model.open(to_std_string(url.to_byte_string()), opener_id(opener)))
        return;
    if (m_overlay && m_overlay->tab() != &opener)
        delete m_overlay.data();
    if (!m_overlay) {
        m_overlay = new GlanceOverlay(m_window, opener);
        m_overlay->on_navigated = [this](URL::URL const& url) {
            m_model.navigated(to_std_string(url.to_byte_string()));
            update_strip();
        };
        m_overlay->on_title_changed = [this](QString const& title) {
            m_model.title_changed(to_std_string(ak_byte_string_from_qstring(title)));
            update_strip();
        };
        m_overlay->on_promote = [this] { promote(); };
        m_overlay->on_copy_url = [this] { run(GlanceCommand::CopyUrl); };
        m_overlay->on_dismiss = [this] { dismiss(); };
    }
    m_overlay->load(url);
    update_strip();
}

void GlanceModule::update_strip()
{
    if (m_overlay)
        m_overlay->set_strip_text(to_qstring(m_model.display_title()), to_qstring(m_model.display_host()));
}

void GlanceModule::promote()
{
    auto promotion = m_model.promote();
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
    if (!promotion.has_value())
        return;
    auto url = URL::Parser::basic_parse(StringView { promotion->url.data(), promotion->url.size() });
    if (!url.has_value())
        return;
    // Opened from its opener, so Tree-Style Tabs nests it there.
    auto* opener = promotion->opener.has_value() ? opener_tab(*promotion->opener) : nullptr;
    m_window.superbird().on_pending_opener(opener);
    auto location = opener ? Ladybird::BrowserWindow::TabLocation::after_tab(*opener) : Ladybird::BrowserWindow::TabLocation::after_current_tab();
    m_window.new_tab_from_url(*url, Web::HTML::ActivateTab::Yes, location);
}

void GlanceModule::dismiss()
{
    m_model.dismiss();
    if (!m_overlay)
        return;
    auto* tab = m_overlay->tab();
    // NB: Deleted later, since this may run from inside one of the overlay's own callbacks.
    m_overlay->hide();
    m_overlay->deleteLater();
    m_overlay = nullptr;
    if (tab && tab->is_loaded() && tab == m_window.current_tab())
        tab->view().setFocus(Qt::OtherFocusReason);
}

bool GlanceModule::can_run(GlanceCommand) const
{
    return is_enabled() && m_model.is_open();
}

void GlanceModule::run(GlanceCommand command)
{
    if (!can_run(command))
        return;
    switch (command) {
    case GlanceCommand::OpenAsTab:
        promote();
        break;
    case GlanceCommand::CopyUrl:
        QApplication::clipboard()->setText(to_qstring(m_model.url()));
        break;
    case GlanceCommand::Close:
        dismiss();
        break;
    }
}

void GlanceModule::register_actions(ModuleRegistry& registry)
{
    static ActiveWindowGlanceHandler handler;
    register_glance_actions(registry, handler);
}

}
