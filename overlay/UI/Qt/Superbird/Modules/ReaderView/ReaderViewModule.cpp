/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
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
#include <QTimer>

namespace Superbird {

// Pages are checked for an article on every load, on the UI thread, so the check reads at most this much of a page.
static constexpr size_t READERABLE_CHECK_LIMIT = 2 * 1024 * 1024;
static constexpr int READERABLE_CHECK_DELAY_MS = 1000;

// Shared by every window for this run.
static Reader::ReaderPreferences& reader_preferences()
{
    static Reader::ReaderPreferences preferences;
    return preferences;
}

static constexpr StringView ADDRESS_BAR_BUTTON_OPTION = "address_bar_button"sv;
static constexpr StringView TOGGLE_ACTION_ID = "reader_view.toggle"sv;

static constexpr Array<ModuleOption, 1> OPTIONS { {
    { .id = ADDRESS_BAR_BUTTON_OPTION, .label = "Show the Reader View button"sv, .description = "Put a book button in the address field on pages that read as articles."sv },
} };

static constexpr Array<ModuleShortcut, 6> SHORTCUTS { {
#if defined(AK_OS_MACOS)
    // NB: Meta is the Control key on macOS, and Ctrl is Command. Cmd+Option+R and Cmd+Shift+R already reload bypassing
    //     the cache there.
    { .command_id = TOGGLE_ACTION_ID, .title = "Toggle Reader View"sv, .default_shortcut = "Meta+Ctrl+R"sv },
#else
    { .command_id = TOGGLE_ACTION_ID, .title = "Toggle Reader View"sv, .default_shortcut = "Ctrl+Alt+R"sv },
#endif
    { .command_id = "reader_view.increase_font_size"sv, .title = "Larger text"sv },
    { .command_id = "reader_view.decrease_font_size"sv, .title = "Smaller text"sv },
    { .command_id = "reader_view.cycle_font"sv, .title = "Next font"sv },
    { .command_id = "reader_view.cycle_theme"sv, .title = "Next theme"sv },
    { .command_id = "reader_view.cycle_width"sv, .title = "Next column width"sv },
} };

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
        .options = OPTIONS.span(),
        .shortcuts = SHORTCUTS.span(),
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
    for (int index = 0; index < m_window.tab_count(); ++index) {
        if (auto* tab = m_window.tab_at(index))
            attach(*tab);
    }
}

void ReaderViewModule::uninstall()
{
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
                schedule_readerable_check(*state);
        },
    });

    update_action(state);
    if (tab == m_window.current_tab())
        schedule_readerable_check(state);
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
        schedule_readerable_check(*state);
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

// The check reads the whole page through the Hub, so it waits until the page has stayed in front for a moment: tabs
// flicked past and links clicked through on the way somewhere else are never read.
void ReaderViewModule::schedule_readerable_check(TabState& state)
{
    auto* tab = state.tab.data();
    if (!tab)
        return;
    QTimer::singleShot(READERABLE_CHECK_DELAY_MS, &m_window, [this, tab = QPointer(tab), generation = state.generation] {
        if (!tab || tab.data() != m_window.current_tab() || !is_enabled())
            return;
        if (auto* state = state_for(tab.data()); state && state->generation == generation)
            check_readerable(*state);
    });
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
    state.action->setVisible(is_enabled() && WindowHost::module_option_is_on(MODULE_ID, ADDRESS_BAR_BUTTON_OPTION) && (active || state.readerable));
    auto shortcut = WindowHost::shortcut_text(TOGGLE_ACTION_ID);
    state.action->setToolTip(shortcut.isEmpty() ? QString("Reader View") : QString("Reader View (%1)").arg(shortcut));
}

void ReaderViewModule::on_settings_changed()
{
    for (auto& it : m_tabs)
        update_action(*it.value);
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
        preferences.zoom_by(1);
        break;
    case ReaderCommand::DecreaseFontSize:
        preferences.zoom_by(-1);
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
