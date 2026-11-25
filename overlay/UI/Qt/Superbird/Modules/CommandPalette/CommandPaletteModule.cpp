/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <LibURL/Parser.h>
#include <LibWebView/HistoryStore.h>
#include <UI/Qt/Application.h>
#include <UI/Qt/ApplicationActions.h>
#include <UI/Qt/BookmarkStore.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/CommandPaletteModule.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/CommandPalettePopover.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/FuzzyMatcher.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/MathExpression.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/Theme.h>

#include <QApplication>
#include <QClipboard>
#include <QScreen>
#include <QTimer>

namespace Superbird {

static constexpr size_t RESULT_LIMIT = 60;
static constexpr size_t HISTORY_LIMIT = 12;

static std::string to_std_string(QString const& string)
{
    auto utf8 = string.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

static std::string to_std_string(StringView view)
{
    return std::string(view.characters_without_null_termination(), view.length());
}

static std::string to_std_string(String const& string)
{
    return to_std_string(string.bytes_as_string_view());
}

static QString to_qstring(std::string const& string)
{
    return QString::fromUtf8(string.data(), static_cast<qsizetype>(string.size()));
}

static QString host_for_display(URL::URL const& url)
{
    auto host = url.serialized_host();
    if (!host.is_empty())
        return qstring_from_ak_string(host);
    return qstring_from_ak_string(url.to_byte_string());
}

// Converts UTF-8 byte offsets (from the fuzzy matcher) into QString indices of the same text.
static Vector<int> highlight_indices(std::string const& utf8, std::vector<size_t> const& byte_positions)
{
    Vector<int> indices;
    if (byte_positions.empty())
        return indices;
    size_t next = 0;
    int qstring_index = 0;
    for (size_t byte = 0; byte < utf8.size() && next < byte_positions.size();) {
        auto lead = static_cast<unsigned char>(utf8[byte]);
        size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2
            : (lead >> 4) == 0xE                              ? 3
                                                              : 4;
        if (byte == byte_positions[next]) {
            indices.append(qstring_index);
            ++next;
        }
        qstring_index += length == 4 ? 2 : 1;
        byte += length;
        while (next < byte_positions.size() && byte_positions[next] < byte)
            ++next;
    }
    return indices;
}

static constexpr StringView INCLUDE_BOOKMARKS_OPTION = "include_bookmarks"sv;
static constexpr StringView INCLUDE_HISTORY_OPTION = "include_history"sv;

static constexpr Array<ModuleOption, 2> OPTIONS { {
    { .id = INCLUDE_BOOKMARKS_OPTION, .label = "Search bookmarks"sv, .description = "List bookmarks among the results."sv },
    { .id = INCLUDE_HISTORY_OPTION, .label = "Search history"sv, .description = "List recently visited pages among the results (never in private windows)."sv },
} };

static constexpr Array<ModuleShortcut, 1> SHORTCUTS { {
    // NB: Ctrl is Command on macOS.
    { .command_id = COMMAND_PALETTE_OPEN_ACTION_ID, .title = "Open the Command Palette"sv, .default_shortcut = "Ctrl+K; F2"sv },
} };

ModuleInfo CommandPaletteModule::module_info()
{
    return {
        .id = MODULE_ID,
        .name = "Command Palette"sv,
        .description = "Press Cmd/Ctrl+K or F2 to fuzzy-search open tabs, bookmarks, history and every Superbird and browser command, calculate with =, or jump to an address."sv,
        .enabled_by_default = true,
        .options = OPTIONS.span(),
        .shortcuts = SHORTCUTS.span(),
    };
}

CommandPaletteModule::CommandPaletteModule(Ladybird::BrowserWindow& window)
    : m_window(window)
{
}

CommandPaletteModule::~CommandPaletteModule() = default;

void CommandPaletteModule::on_enabled_changed(bool enabled)
{
    if (enabled)
        return;
    if (m_popover) {
        m_popover->hide();
        m_popover->deleteLater();
    }
    m_base_entries.clear();
    m_base_targets.clear();
    m_row_entries.clear();
    m_row_targets.clear();
}

void CommandPaletteModule::on_chrome_style_changed()
{
    if (m_popover)
        m_popover->update_chrome_style(Ladybird::Theme::chrome_palette(m_window));
}

void CommandPaletteModule::open()
{
    if (!ModuleRegistry::the().is_enabled(MODULE_ID))
        return;
    if (m_popover && m_popover->isVisible()) {
        m_popover->hide();
        return;
    }

    if (!m_popover) {
        m_popover = new CommandPalettePopover(&m_window);
        m_popover->on_query_changed = [this](QString const& query) { update_results(query); };
        m_popover->on_activate = [this](size_t row) { activate(row, OpenIn::CurrentTab); };
        m_popover->on_activate_in_new_tab = [this](size_t row) { activate(row, OpenIn::NewTab); };
        m_popover->on_close_requested = [this](size_t row) { close_tab(row); };
    }

    m_popover->update_chrome_style(Ladybird::Theme::chrome_palette(m_window));
    gather_base_entries();
    m_popover->clear_query();
    update_results({});

    // Centered over the window, a little below the top, like a sheet dropping from the toolbar.
    m_popover->adjustSize();
    auto size = m_popover->size();
    auto top_left = m_window.mapToGlobal(QPoint((m_window.width() - size.width()) / 2, max(48, m_window.height() / 8)));
    if (auto* screen = m_window.screen()) {
        auto available = screen->availableGeometry();
        top_left.setX(clamp(top_left.x(), available.left(), max(available.left(), available.right() - size.width())));
        top_left.setY(clamp(top_left.y(), available.top(), max(available.top(), available.bottom() - size.height())));
    }
    m_popover->move(top_left);
    m_popover->set_anchor_point(m_window.mapToGlobal(QPoint(m_window.width() / 2, max(48, m_window.height() / 8))));
    m_popover->show();
}

void CommandPaletteModule::gather_base_entries()
{
    m_base_entries.clear();
    m_base_targets.clear();

    auto add = [&](Palette::Entry entry, Target target) {
        m_base_entries.push_back(move(entry));
        m_base_targets.append(move(target));
    };

    // Open tabs: this window's first, then other windows of the same kind (private or not), skipping the current tab.
    auto add_tabs_of = [&](Ladybird::BrowserWindow& window) {
        for (int index = 0; index < window.tab_count(); ++index) {
            auto* tab = window.tab_at(index);
            if (!tab || tab == m_window.current_tab())
                continue;
            auto url = tab->url();
            auto url_string = to_std_string(url.serialize(URL::ExcludeFragment::No));
            auto detail = QString("Tab \xC2\xB7 %1").arg(host_for_display(url));
            for (auto const& label : window.superbird().tab_context(*tab))
                detail += QString(" \xC2\xB7 %1").arg(qstring_from_ak_string(label));
            if (&window != &m_window)
                detail += " \xC2\xB7 other window";
            add({ Palette::EntryKind::Tab, to_std_string(tab->title()), url_string, url_string },
                { tab->tab_icon(), detail, &window, tab });
        }
    };
    add_tabs_of(m_window);
    for (auto* widget : QApplication::topLevelWidgets()) {
        auto* window = dynamic_cast<Ladybird::BrowserWindow*>(widget);
        if (window && window != &m_window && window->is_private() == m_window.is_private())
            add_tabs_of(*window);
    }

    // Commands from every enabled module and the browser.
    auto action_icon = Ladybird::create_chrome_icon(Ladybird::ChromeIcon::DoubleChevronRight, Ladybird::Theme::panel().muted);
    for (auto& entry : collect_action_entries(ModuleRegistry::the())) {
        auto detail = to_qstring(entry.subtitle);
        if (auto shortcut = WindowHost::shortcut_text(StringView { entry.key.data(), entry.key.size() }); !shortcut.isEmpty())
            detail += QString(" \xC2\xB7 %1").arg(shortcut);
        add(move(entry), { action_icon, detail, {}, {} });
    }

    // Bookmarks, flattened out of their folders.
    if (!WindowHost::module_option_is_on(MODULE_ID, INCLUDE_BOOKMARKS_OPTION))
        return;
    auto bookmark_icon = Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Star, Ladybird::Theme::panel().muted);
    Function<void(Vector<Ladybird::BookmarkItem> const&)> add_bookmarks = [&](Vector<Ladybird::BookmarkItem> const& items) {
        for (auto const& item : items) {
            if (item.is_folder()) {
                add_bookmarks(item.folder().children);
                continue;
            }
            auto const& bookmark = item.bookmark();
            auto url_string = to_std_string(bookmark.url.serialize(URL::ExcludeFragment::No));
            auto title = bookmark.title.has_value() ? to_std_string(*bookmark.title) : url_string;
            auto icon = bookmark_icon;
            if (bookmark.favicon_hash.has_value()) {
                // Decoded once per favicon for the life of the window, not on every open.
                if (auto cached = m_favicon_cache.get(*bookmark.favicon_hash); cached.has_value()) {
                    icon = *cached;
                } else if (auto png = Ladybird::Application::bookmark_store().favicon_png(*bookmark.favicon_hash); png.has_value()) {
                    icon = Ladybird::favicon_from_png(png->bytes(), 16);
                    m_favicon_cache.set(*bookmark.favicon_hash, icon);
                }
            }
            add({ Palette::EntryKind::Bookmark, title, url_string, url_string }, { icon, QString("Bookmark \xC2\xB7 %1").arg(host_for_display(bookmark.url)), {}, {} });
        }
    };
    add_bookmarks(Ladybird::Application::bookmark_store().root_items());
}

void CommandPaletteModule::update_results(QString const& query)
{
    if (!m_popover)
        return;

    auto query_utf8 = to_std_string(query);
    auto parsed = Palette::parse_query(query_utf8);
    auto text = std::string(parsed.text);

    m_row_entries.clear();
    m_row_targets.clear();
    m_calculator_result.clear();
    Vector<CommandPalettePopover::Row> rows;

    if (parsed.mode == Palette::QueryMode::Math) {
        auto result = Palette::evaluate_math_expression(text);
        CommandPalettePopover::Row row;
        row.icon = Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Document, Ladybird::Theme::panel().muted);
        if (result.ok()) {
            m_calculator_result = to_qstring(Palette::format_math_result(*result.value));
            row.title = QString("= %1").arg(m_calculator_result);
            row.detail = "Copy result";
        } else {
            row.title = text.empty() ? QString("=") : to_qstring(text);
            row.detail = to_qstring(result.error);
            row.is_error = true;
        }
        m_row_entries.push_back({ Palette::EntryKind::Calculator, to_std_string(row.title), {}, {} });
        m_row_targets.append({});
        rows.append(move(row));
        m_popover->set_rows(move(rows));
        return;
    }

    // Everything gathered on open, plus history and a way to navigate when there is something to look for.
    std::vector<Palette::Entry> entries = m_base_entries;
    Vector<Target> targets = m_base_targets;

    if (!text.empty()) {
        if (m_window.is_private() == WebView::IsPrivate::No && WindowHost::module_option_is_on(MODULE_ID, INCLUDE_HISTORY_OPTION)) {
            auto history_icon = Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Back, Ladybird::Theme::panel().muted);
            auto history = Ladybird::Application::history_store().autocomplete_entries(StringView { text.data(), text.size() }, HISTORY_LIMIT);
            for (auto const& history_entry : history) {
                auto url_string = to_std_string(history_entry.url);
                auto title = history_entry.title.has_value() && !history_entry.title->is_empty() ? to_std_string(*history_entry.title) : url_string;
                auto icon = history_entry.favicon_png.has_value() ? Ladybird::favicon_from_png(history_entry.favicon_png->bytes(), 16) : history_icon;
                QString host = to_qstring(url_string);
                if (auto url = URL::Parser::basic_parse(history_entry.url); url.has_value())
                    host = host_for_display(*url);
                entries.push_back({ Palette::EntryKind::History, title, url_string, url_string });
                targets.append({ icon, QString("History \xC2\xB7 %1").arg(host), {}, {} });
            }
        }

        auto is_address = Palette::looks_like_url(text);
        entries.push_back({ Palette::EntryKind::Navigate, is_address ? "Go to " + text : "Search for " + text, {}, text });
        targets.append({ Ladybird::create_chrome_icon(is_address ? Ladybird::ChromeIcon::Globe : Ladybird::ChromeIcon::Search, Ladybird::Theme::panel().muted), is_address ? QString("Address") : QString("Search"), {}, {} });
    }

    // Each page once, in its most useful form.
    std::vector<Palette::Entry> distinct_entries;
    Vector<Target> distinct_targets;
    for (auto index : Palette::distinct_page_indices(entries)) {
        distinct_entries.push_back(move(entries[index]));
        distinct_targets.append(move(targets[index]));
    }

    for (auto& ranked : Palette::rank_entries(text, distinct_entries, RESULT_LIMIT)) {
        auto const& entry = distinct_entries[ranked.index];
        auto const& target = distinct_targets[ranked.index];
        CommandPalettePopover::Row row;
        row.icon = target.icon;
        row.title = to_qstring(entry.title);
        row.detail = target.detail;
        row.highlights = highlight_indices(entry.title, ranked.title_positions);
        row.can_close = entry.kind == Palette::EntryKind::Tab;
        rows.append(move(row));
        m_row_entries.push_back(entry);
        m_row_targets.append(target);
    }

    m_popover->set_rows(move(rows));
}

void CommandPaletteModule::activate(size_t row, OpenIn open_in)
{
    if (row >= m_row_entries.size())
        return;
    auto entry = m_row_entries[row];
    auto target = m_row_targets[row];

    // A result is copied with a word of confirmation, after which the sheet goes.
    if (entry.kind == Palette::EntryKind::Calculator) {
        if (m_calculator_result.isEmpty())
            return;
        QApplication::clipboard()->setText(m_calculator_result);
        m_popover->set_row_detail(row, "Copied \xE2\x9C\x93");
        QTimer::singleShot(700, m_popover.data(), [popover = m_popover] {
            if (popover)
                popover->hide();
        });
        return;
    }

    auto is_page = entry.kind == Palette::EntryKind::Navigate || entry.kind == Palette::EntryKind::Bookmark || entry.kind == Palette::EntryKind::History;
    if (open_in == OpenIn::NewTab && !is_page)
        open_in = OpenIn::CurrentTab;
    m_popover->hide();

    // NB: Act once the sheet has gone, so focus is back in the window that the action works on.
    QTimer::singleShot(0, &m_window, [this, entry = move(entry), target = move(target), open_in] {
        switch (entry.kind) {
        case Palette::EntryKind::Calculator:
            break;
        case Palette::EntryKind::Tab:
            if (target.window && target.tab) {
                auto index = target.window->tab_index(target.tab.data());
                if (index >= 0) {
                    target.window->activate_tab(index);
                    if (target.window.data() != &m_window)
                        target.window->bring_to_front();
                }
            }
            break;
        case Palette::EntryKind::Action:
            trigger_action(ModuleRegistry::the(), entry.key);
            break;
        case Palette::EntryKind::Navigate:
        case Palette::EntryKind::Bookmark:
        case Palette::EntryKind::History:
            if (open_in == OpenIn::NewTab)
                m_window.open_new_tab();
            navigate_current_tab(to_qstring(entry.key));
            break;
        }
    });
}

void CommandPaletteModule::navigate_current_tab(QString const& input)
{
    auto* tab = m_window.current_tab();
    if (!tab) {
        m_window.open_new_tab();
        tab = m_window.current_tab();
    }
    if (!tab)
        return;
    tab->ensure_loaded();
    auto utf8 = input.toUtf8();
    tab->view().load_from_user_input(StringView { utf8.constData(), static_cast<size_t>(utf8.size()) });
    tab->view().setFocus(Qt::OtherFocusReason);
}

void CommandPaletteModule::close_tab(size_t row)
{
    if (row >= m_row_entries.size() || m_row_entries[row].kind != Palette::EntryKind::Tab)
        return;
    auto const& target = m_row_targets[row];
    if (!target.window || !target.tab)
        return;
    auto index = target.window->tab_index(target.tab.data());
    if (index < 0)
        return;
    target.window->request_to_close_tab(index);

    // The tab may close later (after its page agrees), so refresh once the event loop has run.
    QTimer::singleShot(0, &m_window, [this] {
        if (!m_popover || !m_popover->isVisible())
            return;
        gather_base_entries();
        update_results(m_popover->query());
    });
}

namespace {

// Runs browser commands against the active window, through the same Ladybird actions its menus use.
class ActiveWindowBrowserHandler final : public BrowserCommandHandler {
public:
    virtual bool can_run(BrowserCommand command) const override
    {
        auto* window = Ladybird::Application::the().active_window_if_any();
        auto* tab = window ? window->current_tab() : nullptr;
        auto& actions = Ladybird::ApplicationActions::the();

        switch (command) {
        case BrowserCommand::NewWindow:
            return true;
        case BrowserCommand::NewPrivateWindow:
            return Ladybird::Application::the().supports_private_browsing_windows();
        case BrowserCommand::OpenSettings:
            return actions.open_settings_page_action().enabled();
        case BrowserCommand::OpenDownloads:
            return actions.open_downloads_page_action().enabled();
        case BrowserCommand::NewTab:
            return window != nullptr;
        case BrowserCommand::ToggleForceDark:
            return actions.force_dark_action().enabled();
        case BrowserCommand::CloseTab:
        case BrowserCommand::DuplicateTab:
        case BrowserCommand::TogglePinTab:
        case BrowserCommand::FindInPage:
            return tab != nullptr;
        case BrowserCommand::ToggleMuteTab:
            return tab && tab->is_loaded();
        case BrowserCommand::BookmarkPage:
            return tab && tab->is_loaded() && !Ladybird::Application::bookmark_store().is_bookmarked(tab->url());
        case BrowserCommand::Reload:
            return tab && actions.reload_action().enabled();
        case BrowserCommand::ZoomIn:
            return tab && actions.zoom_in_action().enabled();
        case BrowserCommand::ZoomOut:
            return tab && actions.zoom_out_action().enabled();
        case BrowserCommand::ResetZoom:
            return tab && actions.reset_zoom_action().enabled();
        case BrowserCommand::ViewSource:
            return tab && actions.view_source_action().enabled();
        }
        VERIFY_NOT_REACHED();
    }

    virtual void run(BrowserCommand command) override
    {
        if (!can_run(command))
            return;
        auto* window = Ladybird::Application::the().active_window_if_any();
        auto* tab = window ? window->current_tab() : nullptr;
        auto& actions = Ladybird::ApplicationActions::the();

        switch (command) {
        case BrowserCommand::NewTab:
            window->open_new_tab();
            break;
        case BrowserCommand::NewWindow:
            Ladybird::Application::the().open_new_window(WebView::IsPrivate::No);
            break;
        case BrowserCommand::NewPrivateWindow:
            Ladybird::Application::the().open_new_window(WebView::IsPrivate::Yes);
            break;
        case BrowserCommand::CloseTab:
            window->request_to_close_tab(window->tab_index(tab));
            break;
        case BrowserCommand::DuplicateTab:
            window->duplicate_tab(*tab);
            break;
        case BrowserCommand::TogglePinTab:
            if (tab->is_pinned())
                window->unpin_tab(*tab);
            else
                window->pin_tab(*tab);
            break;
        case BrowserCommand::ToggleMuteTab:
            tab->view().toggle_page_mute_state();
            break;
        case BrowserCommand::Reload:
            actions.reload_action().activate();
            break;
        case BrowserCommand::FindInPage:
            window->show_find_in_page();
            break;
        case BrowserCommand::ZoomIn:
            actions.zoom_in_action().activate();
            break;
        case BrowserCommand::ZoomOut:
            actions.zoom_out_action().activate();
            break;
        case BrowserCommand::ResetZoom:
            actions.reset_zoom_action().activate();
            break;
        case BrowserCommand::ToggleForceDark:
            actions.force_dark_action().activate();
            break;
        case BrowserCommand::BookmarkPage:
            tab->show_bookmark_popover();
            break;
        case BrowserCommand::ViewSource:
            actions.view_source_action().activate();
            break;
        case BrowserCommand::OpenDownloads:
            actions.open_downloads_page_action().activate();
            break;
        case BrowserCommand::OpenSettings:
            actions.open_settings_page_action().activate();
            break;
        }
    }
};

}

void CommandPaletteModule::register_actions(ModuleRegistry& registry)
{
    static ActiveWindowBrowserHandler handler;
    register_browser_actions(registry, handler);

    // Opening the palette is a command too, so its shortcut can be changed in about:settings like any other.
    registry.register_action(ActionDescriptor {
        .id = COMMAND_PALETTE_OPEN_ACTION_ID,
        .module_id = MODULE_ID,
        .title = "Open Command Palette"sv,
        .category = "Command Palette"sv,
        .is_available = [] { return Ladybird::Application::the().active_window_if_any() != nullptr; },
        .trigger = [] {
            auto* window = Ladybird::Application::the().active_window_if_any();
            if (!window)
                return;
            if (auto* module = window->superbird().module(MODULE_ID))
                static_cast<CommandPaletteModule*>(module)->open();
        },
    });
}

}
