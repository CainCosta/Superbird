/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Platform.h>
#include <UI/Qt/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ArticleExtractor.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderOverlay.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderViewModule.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/Theme.h>

#include <QAction>
#include <QKeySequence>
#include <QShortcut>

namespace Superbird {

// Pages are checked for an article on every load, on the UI thread, so the check reads at most this much of a page.
static constexpr size_t READERABLE_CHECK_LIMIT = 2 * 1024 * 1024;

// Shared by every window for this run.
static Reader::ReaderPreferences& reader_preferences()
{
    static Reader::ReaderPreferences preferences;
    return preferences;
}

static QKeySequence toggle_shortcut()
{
#if defined(AK_OS_MACOS)
    // NB: Qt::META is the Control key on macOS, and Qt::CTRL is Command. Cmd+Option+R and Cmd+Shift+R already reload
    //     bypassing the cache there.
    return QKeySequence(Qt::META | Qt::CTRL | Qt::Key_R);
#else
    return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R);
#endif
}

static bool is_web_page(URL::URL const& url)
{
    return url.scheme() == "http"sv || url.scheme() == "https"sv;
}

ModuleInfo ReaderViewModule::module_info()
{
    return {
        .id = MODULE_ID,
        .name = "Reader View"sv,
        .description = "Read articles as clean, script-free text with adjustable fonts, sizes, widths and themes, without changing the page underneath."sv,
        .enabled_by_default = true,
    };
}

ReaderViewModule::ReaderViewModule(Ladybird::BrowserWindow& window)
    : m_window(window)
{
    if (is_enabled())
        install();
}

ReaderViewModule::~ReaderViewModule()
{
    uninstall();
}

bool ReaderViewModule::is_enabled() const
{
    return ModuleRegistry::the().is_enabled(MODULE_ID);
}

void ReaderViewModule::install()
{
    if (!m_shortcut) {
        m_shortcut = new QShortcut(toggle_shortcut(), &m_window);
        m_shortcut->setContext(Qt::WindowShortcut);
        QObject::connect(m_shortcut, &QShortcut::activated, &m_window, [this] {
            if (auto* tab = m_window.current_tab(); tab && can_run(ReaderCommand::Toggle))
                toggle(*tab);
        });
        WindowHost::reserve_shortcut(toggle_shortcut());
    }
    for (int index = 0; index < m_window.tab_count(); ++index) {
        if (auto* tab = m_window.tab_at(index))
            attach(*tab);
    }
}

void ReaderViewModule::uninstall()
{
    if (m_shortcut) {
        delete m_shortcut.data();
        WindowHost::release_shortcut(toggle_shortcut());
    }
    Vector<Ladybird::Tab*> tabs;
    for (auto const& it : m_tabs)
        tabs.append(it.key);
    for (auto* tab : tabs)
        detach(*tab);
}

void ReaderViewModule::on_enabled_changed(bool enabled)
{
    if (enabled)
        install();
    else
        uninstall();
}

ReaderViewModule::TabState* ReaderViewModule::state_for(Ladybird::Tab* tab)
{
    auto it = m_tabs.find(tab);
    return it == m_tabs.end() ? nullptr : it->value.ptr();
}

ReaderViewModule::TabState const* ReaderViewModule::state_for(Ladybird::Tab* tab) const
{
    auto it = m_tabs.find(tab);
    return it == m_tabs.end() ? nullptr : it->value.ptr();
}

void ReaderViewModule::attach(Ladybird::Tab& tab)
{
    if (!is_enabled() || m_tabs.contains(&tab))
        return;
    auto state = make<TabState>();
    state->tab = &tab;
    auto& state_ref = *state;
    m_tabs.set(&tab, move(state));

    if (tab.is_loaded()) {
        attach_view(state_ref);
    } else {
        // A restored tab has no view until it's first shown.
        state_ref.loaded_connection = QObject::connect(&tab, &Ladybird::Tab::loaded, &m_window, [this, tab = QPointer(&tab)] {
            if (!tab)
                return;
            if (auto* state = state_for(tab.data()); state && !state->action)
                attach_view(*state);
        });
    }
}

void ReaderViewModule::attach_view(TabState& state)
{
    auto* tab = state.tab.data();
    if (!tab || !tab->is_loaded())
        return;

    auto* action = new QAction(tab);
    action->setToolTip(QString("Reader View (%1)").arg(toggle_shortcut().toString(QKeySequence::NativeText)));
    action->setVisible(false);
    QObject::connect(action, &QAction::triggered, tab, [this, tab = QPointer(tab)] {
        if (tab)
            toggle(*tab);
    });
    state.action = action;
    tab->location_edit().add_page_action(action);

    state.navigation_listener_id = tab->view().add_navigation_listener({
        .on_load_start = [this, tab = QPointer(tab)](URL::URL const&) {
            if (!tab)
                return;
            if (auto* state = state_for(tab.data()))
                page_started_loading(*state);
        },
        .on_load_finish = [this, tab = QPointer(tab)](URL::URL const&) {
            if (!tab || tab.data() != m_window.current_tab())
                return;
            if (auto* state = state_for(tab.data()))
                check_readerable(*state);
        },
    });

    update_action(state);
    if (tab == m_window.current_tab())
        check_readerable(state);
}

void ReaderViewModule::detach(Ladybird::Tab& tab)
{
    auto state = m_tabs.take(&tab);
    if (!state.has_value())
        return;
    auto& tab_state = **state;
    QObject::disconnect(tab_state.loaded_connection);
    if (tab_state.overlay) {
        tab_state.overlay->hide();
        tab_state.overlay->deleteLater();
    }
    if (tab_state.navigation_listener_id.has_value() && tab.is_loaded())
        tab.view().remove_navigation_listener(*tab_state.navigation_listener_id);
    // NB: Deleting the action takes its button out of the address field.
    delete tab_state.action.data();
}

void ReaderViewModule::on_tab_added(Ladybird::Tab& tab)
{
    attach(tab);
}

void ReaderViewModule::on_tab_removed(Ladybird::Tab& tab)
{
    detach(tab);
}

void ReaderViewModule::on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab)
{
    detach(const_cast<Ladybird::Tab&>(old_tab));
    attach(new_tab);
}

void ReaderViewModule::on_active_tab_changed(Ladybird::Tab* tab)
{
    if (auto* state = state_for(tab); state && tab->is_loaded())
        check_readerable(*state);
}

void ReaderViewModule::on_chrome_style_changed()
{
    for (auto& it : m_tabs) {
        if (it.value->overlay)
            it.value->overlay->update_chrome_style();
        update_action(*it.value);
    }
}

void ReaderViewModule::page_started_loading(TabState& state)
{
    ++state.generation;
    state.readerable = false;
    state.opening = false;
    state.checked_url.clear();
    close_reader(state);
}

void ReaderViewModule::check_readerable(TabState& state)
{
    auto* tab = state.tab.data();
    if (!tab || !tab->is_loaded() || tab->view().is_loading())
        return;
    auto url = tab->url();
    if (!is_web_page(url)) {
        state.readerable = false;
        update_action(state);
        return;
    }
    auto url_string = qstring_from_ak_string(url.to_byte_string());
    if (state.checked_url == url_string)
        return;
    state.checked_url = url_string;

    tab->view().request_reader_document()->when_resolved([this, tab = QPointer(tab), generation = state.generation](String& html) {
        // NB: A live tab means a live window, and so this module.
        if (!tab)
            return;
        auto* state = state_for(tab.data());
        if (!state || state->generation != generation)
            return;
        auto view = html.bytes_as_string_view();
        std::string_view document { view.characters_without_null_termination(), min(view.length(), READERABLE_CHECK_LIMIT) };
        state->readerable = Reader::is_probably_readerable(document);
        update_action(*state);
    });
}

void ReaderViewModule::toggle(Ladybird::Tab& tab)
{
    auto* state = state_for(&tab);
    if (!state)
        return;
    if (state->overlay)
        close_reader(*state);
    else
        open_reader(*state);
}

void ReaderViewModule::open_reader(TabState& state)
{
    auto* tab = state.tab.data();
    if (!tab || !tab->is_loaded() || state.opening || state.overlay)
        return;
    state.opening = true;

    auto promise = tab->view().request_reader_document();
    promise->when_resolved([this, tab = QPointer(tab), generation = state.generation](String& html) {
        if (!tab)
            return;
        auto* state = state_for(tab.data());
        if (!state || state->generation != generation || !state->opening)
            return;
        state->opening = false;

        auto url = tab->url().to_byte_string();
        std::string page_url(url.characters(), url.length());
        auto view = html.bytes_as_string_view();
        auto article = Reader::extract_article({ view.characters_without_null_termination(), view.length() }, page_url);

        auto* overlay = new ReaderOverlay(m_window, *tab, std::move(article), std::move(page_url), reader_preferences());
        overlay->on_close_requested = [this, tab] {
            if (auto* state = tab ? state_for(tab.data()) : nullptr)
                close_reader(*state);
        };
        overlay->on_follow_link = [this, tab](URL::URL const& target) {
            if (!tab)
                return;
            if (auto* state = state_for(tab.data()))
                close_reader(*state);
            tab->navigate(target);
        };
        overlay->on_preferences_changed = [this](Reader::ReaderPreferences const& preferences) {
            reader_preferences() = preferences;
            apply_preferences_everywhere();
        };
        state->overlay = overlay;
        update_action(*state);
    });
    promise->when_rejected([this, tab = QPointer(tab)](Error const&) {
        if (!tab)
            return;
        if (auto* state = state_for(tab.data()))
            state->opening = false;
    });
}

void ReaderViewModule::close_reader(TabState& state)
{
    state.opening = false;
    if (!state.overlay)
        return;
    // NB: Deleted later, since this may run from inside one of the overlay's own callbacks.
    state.overlay->hide();
    state.overlay->deleteLater();
    state.overlay = nullptr;
    if (auto* tab = state.tab.data(); tab && tab->is_loaded() && tab == m_window.current_tab())
        tab->view().setFocus(Qt::OtherFocusReason);
    update_action(state);
}

void ReaderViewModule::update_action(TabState& state)
{
    if (!state.action)
        return;
    auto active = !state.overlay.isNull();
    auto palette = Ladybird::Theme::chrome_palette(m_window);
    state.action->setIcon(active ? Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Document, Ladybird::Theme::accent())
                                 : Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Document, palette));
    state.action->setCheckable(true);
    state.action->setChecked(active);
    state.action->setVisible(is_enabled() && (active || state.readerable));
}

void ReaderViewModule::apply_preferences_everywhere()
{
    for (auto& it : m_tabs) {
        if (it.value->overlay)
            it.value->overlay->set_preferences(reader_preferences());
    }
}

bool ReaderViewModule::can_run(ReaderCommand command) const
{
    auto* tab = m_window.current_tab();
    if (!is_enabled() || !tab || !tab->is_loaded())
        return false;
    auto const* state = state_for(tab);
    if (!state)
        return false;
    if (command == ReaderCommand::Toggle)
        return state->overlay || is_web_page(tab->url());
    return !state->overlay.isNull();
}

void ReaderViewModule::run(ReaderCommand command)
{
    if (!can_run(command))
        return;
    auto* tab = m_window.current_tab();
    auto& preferences = reader_preferences();
    switch (command) {
    case ReaderCommand::Toggle:
        toggle(*tab);
        return;
    case ReaderCommand::IncreaseFontSize:
        preferences.increase_font_size();
        break;
    case ReaderCommand::DecreaseFontSize:
        preferences.decrease_font_size();
        break;
    case ReaderCommand::CycleFont:
        preferences.font = preferences.font == Reader::ReaderFont::Serif ? Reader::ReaderFont::Sans
            : preferences.font == Reader::ReaderFont::Sans               ? Reader::ReaderFont::Mono
                                                                         : Reader::ReaderFont::Serif;
        break;
    case ReaderCommand::CycleTheme:
        preferences.theme = preferences.theme == Reader::ReaderTheme::System ? Reader::ReaderTheme::Light
            : preferences.theme == Reader::ReaderTheme::Light                ? Reader::ReaderTheme::Sepia
            : preferences.theme == Reader::ReaderTheme::Sepia                ? Reader::ReaderTheme::Dark
                                                                             : Reader::ReaderTheme::System;
        break;
    case ReaderCommand::CycleWidth:
        preferences.width = preferences.width == Reader::ReaderWidth::Narrow ? Reader::ReaderWidth::Medium
            : preferences.width == Reader::ReaderWidth::Medium               ? Reader::ReaderWidth::Wide
                                                                             : Reader::ReaderWidth::Narrow;
        break;
    }
    apply_preferences_everywhere();
}

namespace {

// Routes Reader View actions from the action bus to the active window's module.
class ActiveWindowReaderHandler final : public ReaderCommandHandler {
public:
    virtual bool can_run(ReaderCommand command) const override
    {
        auto* module = active_module();
        return module && module->can_run(command);
    }

    virtual void run(ReaderCommand command) override
    {
        if (auto* module = active_module())
            module->run(command);
    }

private:
    static ReaderViewModule* active_module()
    {
        auto* window = Ladybird::Application::the().active_window_if_any();
        if (!window)
            return nullptr;
        return static_cast<ReaderViewModule*>(window->superbird().module(READER_VIEW_MODULE_ID));
    }
};

}

void ReaderViewModule::register_actions(ModuleRegistry& registry)
{
    static ActiveWindowReaderHandler handler;
    register_reader_view_actions(registry, handler);
}

}
