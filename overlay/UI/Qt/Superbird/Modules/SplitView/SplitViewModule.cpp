/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <UI/Qt/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitStore.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitViewHost.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitViewModule.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/TabBar.h>
#include <UI/Qt/Theme.h>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

namespace Superbird {

// A tab dropped this close to an edge of the page (as a share of its width or height) splits toward that edge.
static constexpr double EDGE_ZONE = 0.3;
static constexpr int PERSIST_DELAY_MS = 400;

namespace {

// Routes Split View actions from the action bus to the active window's module.
class ActiveWindowSplitHandler final : public SplitCommandHandler {
public:
    virtual bool can_run(SplitCommand command) const override
    {
        auto* module = active_module();
        return module && module->can_run(command);
    }

    virtual void run(SplitCommand command) override
    {
        if (auto* module = active_module())
            module->run(command);
    }

    static SplitViewModule* module_of(Ladybird::BrowserWindow* window)
    {
        if (!window)
            return nullptr;
        return static_cast<SplitViewModule*>(window->superbird().module(SPLIT_VIEW_MODULE_ID));
    }

    static SplitViewModule* active_module()
    {
        return module_of(Ladybird::Application::the().active_window_if_any());
    }
};

// Shows where a tab being dragged would land in a split: a tinted area over the page, as a window of its own, since
// nothing painted inside the window can cover the page's native view.
class DropIndicator final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    DropIndicator()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowTransparentForInput)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void show_over(QRect const& area)
    {
        auto rect = area.adjusted(INSET, INSET, -INSET, -INSET);
        if (geometry() != rect)
            setGeometry(rect);
        if (!isVisible())
            show();
        raise();
    }

private:
    static constexpr int INSET = 6;

    virtual void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto accent = Ladybird::Theme::accent();
        auto fill = accent;
        fill.setAlphaF(0.16);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10);
        painter.fillPath(path, fill);
        painter.setPen(QPen(accent, 2));
        painter.drawPath(path);
    }
};

// Follows a loose tab around while it's dragged, showing where it would land in the window under the pointer.
class DropIndicatorDriver {
public:
    static void set_dragged_tab(Ladybird::Tab* tab)
    {
        static QPointer<QTimer> timer;
        static QPointer<DropIndicator> indicator;
        static QPointer<Ladybird::Tab> dragged;
        dragged = tab;
        if (!tab) {
            if (timer)
                timer->stop();
            if (indicator)
                indicator->hide();
            return;
        }
        if (!timer) {
            timer = new QTimer(qApp);
            timer->setInterval(33);
            QObject::connect(timer, &QTimer::timeout, qApp, [] {
                auto target = dragged ? target_at(*dragged, QCursor::pos()) : std::optional<QRect> {};
                if (!target.has_value()) {
                    if (indicator)
                        indicator->hide();
                    return;
                }
                if (!indicator)
                    indicator = new DropIndicator;
                indicator->show_over(*target);
            });
        }
        timer->start();
    }

private:
    static std::optional<QRect> target_at(Ladybird::Tab& tab, QPoint position)
    {
        // NB: The indicator is a window too, so the window under the pointer is found by geometry rather than asked of Qt.
        Ladybird::BrowserWindow* window = nullptr;
        if (auto* active = Ladybird::Application::the().active_window_if_any(); active && active->frameGeometry().contains(position))
            window = active;
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (window)
                break;
            if (auto* candidate = dynamic_cast<Ladybird::BrowserWindow*>(widget); candidate && candidate->isVisible() && candidate->frameGeometry().contains(position))
                window = candidate;
        }
        auto* module = ActiveWindowSplitHandler::module_of(window);
        if (!module)
            return {};
        auto target = module->drop_target_at(tab, position);
        if (!target.has_value())
            return {};
        return target->area;
    }
};

}

static constexpr StringView EDGE_DROP_OPTION = "edge_drop"sv;

static constexpr Array<ModuleOption, 1> OPTIONS { {
    { .id = EDGE_DROP_OPTION, .label = "Split by dropping a tab at the page's edge"sv, .description = "Drag a tab from the tab bar onto the page to split with it."sv },
} };

static constexpr Array<ModuleShortcut, 9> SHORTCUTS { {
    { .command_id = "split_view.side_by_side"sv, .title = "Split side by side"sv },
    { .command_id = "split_view.stacked"sv, .title = "Split stacked"sv },
    { .command_id = "split_view.grid"sv, .title = "Split into a grid of four tabs"sv },
    { .command_id = "split_view.add_next_tab"sv, .title = "Add the next tab to the split"sv },
    { .command_id = "split_view.focus_next_pane"sv, .title = "Focus the next pane"sv },
    { .command_id = "split_view.swap"sv, .title = "Swap panes"sv },
    { .command_id = "split_view.toggle_orientation"sv, .title = "Rotate"sv },
    { .command_id = "split_view.equalize"sv, .title = "Equal sizes"sv },
    { .command_id = "split_view.unsplit"sv, .title = "Unsplit"sv },
} };

ModuleInfo SplitViewModule::module_info()
{
    return {
        .id = MODULE_ID,
        .name = "Split View"sv,
        .description = "Tile two to four tabs side by side, stacked or in a grid. Split from a tab's menu, the Command Palette, or by dropping a tab at the page's edge."sv,
        .enabled_by_default = true,
        .options = OPTIONS.span(),
        .shortcuts = SHORTCUTS.span(),
    };
}

SplitViewModule::SplitViewModule(Ladybird::BrowserWindow& window)
    : m_window(window)
{
}

SplitViewModule::~SplitViewModule()
{
    if (m_persist_timer && m_persist_timer->isActive())
        persist_now();
    if (m_persist_timer)
        delete m_persist_timer.data();
    // NB: The window takes its tabs down after its modules, so they go back to it now.
    hide_split();
}

bool SplitViewModule::is_enabled() const
{
    return ModuleRegistry::the().is_enabled(MODULE_ID);
}

Split::PaneId SplitViewModule::pane_for(Ladybird::Tab& tab)
{
    if (auto existing = m_pane_ids.get(&tab); existing.has_value())
        return *existing;
    auto id = m_next_pane_id++;
    m_pane_ids.set(&tab, id);
    return id;
}

Ladybird::Tab* SplitViewModule::tab_for(Split::PaneId pane) const
{
    for (auto const& it : m_pane_ids) {
        if (it.value == pane)
            return it.key;
    }
    return nullptr;
}

Ladybird::Tab* SplitViewModule::next_visible_tab_after(Ladybird::Tab& tab) const
{
    auto start = m_window.tab_widget().index_of(&tab);
    auto count = m_window.tab_widget().count();
    for (int step = 1; step < count; ++step) {
        auto* candidate = m_window.tab_at((start + step) % count);
        if (candidate && candidate != &tab && !m_window.superbird().is_tab_hidden(*candidate))
            return candidate;
    }
    return nullptr;
}

void SplitViewModule::on_enabled_changed(bool enabled)
{
    if (enabled)
        return;
    unsplit();
    for (auto& it : m_split_with_current_actions) {
        if (it.value)
            it.value->setVisible(false);
    }
    for (auto& it : m_add_to_split_actions) {
        if (it.value)
            it.value->setVisible(false);
    }
}

void SplitViewModule::on_chrome_style_changed()
{
    if (m_host)
        m_host->update_chrome_style();
}

void SplitViewModule::show_split()
{
    if (!is_enabled() || !m_layout || m_layout->pane_count() < 2 || m_host)
        return;
    m_changing = true;
    auto& tabs = m_window.tab_widget();
    for (auto pane : m_layout->panes()) {
        if (auto* tab = tab_for(pane)) {
            tab->ensure_loaded();
            tabs.lend_page(*tab);
        }
    }
    m_host = new SplitViewHost;
    m_host->on_pane_activated = [this](auto pane) { focus_pane(pane); };
    m_host->on_remove_pane = [this](auto pane) { remove_from_split(pane); };
    m_host->on_swap = [this] { run(SplitCommand::SwapPanes); };
    m_host->on_unsplit = [this] { unsplit(); };
    m_host->on_ratios_changed = [this](auto pane, auto ratios) {
        if (m_layout && m_layout->set_ratios_around(pane, std::move(ratios)))
            layout_changed();
    };
    m_host->rebuild(*m_layout, [this](auto pane) { return tab_for(pane); });
    tabs.set_page_host(m_host);
    m_host->set_focused(m_layout->focused());
    update_pane_visibility(PanesShown::Yes);
    m_changing = false;
}

void SplitViewModule::hide_split()
{
    if (!m_host)
        return;
    m_changing = true;
    update_pane_visibility(PanesShown::No);
    auto& tabs = m_window.tab_widget();
    auto host = m_host;
    m_host = nullptr;
    host->release_tabs();
    tabs.set_page_host(nullptr);
    if (m_layout) {
        for (auto pane : m_layout->panes()) {
            if (auto* tab = tab_for(pane))
                tabs.return_page(*tab);
        }
    }
    // NB: Deleted later, since this may run from inside one of the host's own callbacks.
    host->hide();
    host->deleteLater();
    m_changing = false;
}

void SplitViewModule::relayout()
{
    if (!m_layout)
        return;
    if (m_layout->pane_count() < 2) {
        unsplit();
        return;
    }
    hide_split();
    if (auto* current = m_window.current_tab(); current && m_layout->contains(pane_for(*current)))
        show_split();
    layout_changed();
}

void SplitViewModule::unsplit()
{
    hide_split();
    auto had_layout = m_layout.has_value();
    m_layout.reset();
    if (had_layout)
        layout_changed();
}

void SplitViewModule::split_with(Ladybird::Tab& first, Ladybird::Tab& second, Split::Orientation orientation)
{
    if (&first == &second)
        return;
    unsplit();
    m_layout = Split::SplitLayout::create(pane_for(first), pane_for(second), orientation);
    if (!m_layout)
        return;
    show_split();
    focus_pane(pane_for(first));
    layout_changed();
}

void SplitViewModule::split_grid()
{
    auto* current = m_window.current_tab();
    if (!current)
        return;
    std::vector<Split::PaneId> panes { pane_for(*current) };
    auto* next = current;
    while (panes.size() < Split::MAXIMUM_PANES) {
        next = next_visible_tab_after(*next);
        if (!next || next == current)
            break;
        panes.push_back(pane_for(*next));
    }
    unsplit();
    m_layout = Split::SplitLayout::grid(panes);
    if (!m_layout)
        return;
    show_split();
    focus_pane(pane_for(*current));
    layout_changed();
}

void SplitViewModule::add_to_split(Ladybird::Tab& tab, Split::Orientation orientation)
{
    if (!m_layout || !m_layout->add_pane(pane_for(tab), m_layout->focused(), orientation))
        return;
    relayout();
    focus_pane(pane_for(tab));
}

void SplitViewModule::remove_from_split(Split::PaneId pane)
{
    if (!m_layout || !m_layout->contains(pane))
        return;
    // Every page goes back first, including the one leaving, which stays open as a tab of its own.
    hide_split();
    m_layout->remove_pane(pane);
    if (m_layout->pane_count() < 2) {
        m_layout.reset();
        layout_changed();
        return;
    }
    show_split();
    focus_pane(m_layout->focused());
    layout_changed();
}

void SplitViewModule::focus_pane(Split::PaneId pane)
{
    if (!m_layout || !m_layout->focus(pane))
        return;
    auto* tab = tab_for(pane);
    if (!tab)
        return;
    if (m_window.current_tab() != tab) {
        m_changing = true;
        m_window.activate_tab(m_window.tab_index(tab));
        m_changing = false;
    }
    if (m_host) {
        m_host->set_focused(pane);
        update_pane_visibility(PanesShown::Yes);
    }
    if (tab->is_loaded())
        tab->view().setFocus(Qt::OtherFocusReason);
}

void SplitViewModule::update_pane_visibility(PanesShown shown)
{
    // NB: Selecting a tab tells the page it left that it's hidden, which stops it painting and lets it be discarded. Every
    //     page of a split on screen is seen, so the ones besides the current tab are told they're visible again.
    if (!m_layout)
        return;
    auto* current = m_window.current_tab();
    for (auto pane : m_layout->panes()) {
        auto* tab = tab_for(pane);
        if (!tab || tab == current || !tab->is_loaded())
            continue;
        auto& view = tab->view();
        view.set_system_visibility_state(shown == PanesShown::Yes ? Web::HTML::VisibilityState::Visible : Web::HTML::VisibilityState::Hidden);
        view.set_current_tab_of_window(shown == PanesShown::Yes);
    }
}

void SplitViewModule::on_active_tab_changed(Ladybird::Tab* tab)
{
    if (!is_enabled() || m_changing || !m_layout || !tab)
        return;
    auto pane = m_pane_ids.get(tab);
    if (pane.has_value() && m_layout->contains(*pane)) {
        // Back to a tab of the split group: the split comes back.
        if (!m_host)
            show_split();
        m_layout->focus(*pane);
        if (m_host) {
            m_host->set_focused(*pane);
            update_pane_visibility(PanesShown::Yes);
        }
        return;
    }
    // Another tab: the split steps aside, and comes back when one of its tabs is picked again.
    hide_split();
}

void SplitViewModule::on_tab_removed(Ladybird::Tab& tab)
{
    auto pane = m_pane_ids.take(&tab);
    m_split_with_current_actions.remove(&tab);
    m_add_to_split_actions.remove(&tab);
    if (!pane.has_value() || !m_layout || !m_layout->contains(*pane))
        return;
    hide_split();
    if (!m_layout->remove_pane(*pane) || m_layout->pane_count() < 2) {
        m_layout.reset();
        layout_changed();
        return;
    }
    if (auto* current = m_window.current_tab(); current && m_layout->contains(pane_for(*current)))
        show_split();
    layout_changed();
}

void SplitViewModule::on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab)
{
    auto* old_pointer = const_cast<Ladybird::Tab*>(&old_tab);
    auto pane = m_pane_ids.take(old_pointer);
    m_split_with_current_actions.remove(old_pointer);
    m_add_to_split_actions.remove(old_pointer);
    if (!pane.has_value())
        return;
    // The stand-in keeps the tab's pane.
    m_pane_ids.set(&new_tab, *pane);
    if (m_layout && m_layout->contains(*pane) && m_host) {
        hide_split();
        show_split();
    }
    if (m_layout && m_layout->contains(*pane))
        layout_changed();
}

void SplitViewModule::on_tab_session_id_assigned(Ladybird::Tab& tab)
{
    // A tab of the group can be saved by its id only now.
    if (auto pane = m_pane_ids.get(&tab); pane.has_value() && m_layout && m_layout->contains(*pane))
        layout_changed();
}

void SplitViewModule::on_tabs_restored()
{
    // NB: Only a window restored from the session takes its saved split; a new window's id may be one a closed window
    //     left a row under, which its first save replaces.
    if (!is_enabled() || m_layout)
        return;
    auto window_id = m_window.session_window_id();
    if (!window_id.has_value())
        return;
    auto text = SplitStore::the(m_window.is_private() == WebView::IsPrivate::Yes).load_window(*window_id);
    if (!text.has_value())
        return;
    auto restored = Split::SplitLayout::deserialize(std::string_view(text->characters(), text->length()),
        [this](int64_t session_tab_id) -> std::optional<Split::PaneId> {
            for (int index = 0; index < m_window.tab_count(); ++index) {
                auto* tab = m_window.tab_at(index);
                if (tab && tab->session_tab_id() == session_tab_id)
                    return pane_for(*tab);
            }
            return {};
        });
    if (!restored)
        return;
    m_layout = AK::move(*restored);
    if (auto* current = m_window.current_tab(); current && m_layout->contains(pane_for(*current))) {
        show_split();
        focus_pane(pane_for(*current));
    }
    m_window.superbird().tab_markers_changed();
}

Optional<u32> SplitViewModule::tab_marker_rgb(Ladybird::Tab& tab) const
{
    if (!m_layout)
        return {};
    auto pane = m_pane_ids.get(&tab);
    if (!pane.has_value() || !m_layout->contains(*pane))
        return {};
    return Ladybird::Theme::accent().rgb() & 0xFFFFFF;
}

void SplitViewModule::layout_changed()
{
    m_window.superbird().tab_markers_changed();
    if (!m_persist_timer) {
        m_persist_timer = new QTimer(&m_window);
        m_persist_timer->setSingleShot(true);
        m_persist_timer->setInterval(PERSIST_DELAY_MS);
        QObject::connect(m_persist_timer, &QTimer::timeout, &m_window, [this] { persist_now(); });
    }
    m_persist_timer->start();
}

void SplitViewModule::persist_now()
{
    if (m_persist_timer)
        m_persist_timer->stop();
    auto window_id = m_window.session_window_id();
    if (!window_id.has_value())
        return;
    auto& store = SplitStore::the(m_window.is_private() == WebView::IsPrivate::Yes);
    if (!m_layout) {
        store.save_window(*window_id, {});
        return;
    }
    auto text = m_layout->serialize([this](Split::PaneId pane) -> std::optional<int64_t> {
        auto* tab = tab_for(pane);
        if (!tab)
            return {};
        auto id = tab->session_tab_id();
        if (!id.has_value())
            return {};
        return *id;
    });
    // NB: A tab with no session row yet can't be saved by; its id arriving saves the group again.
    if (text.has_value())
        store.save_window(*window_id, ByteString(text->data(), text->size()));
}

void SplitViewModule::on_tab_metadata_changed()
{
    if (m_host)
        m_host->update_titles();
}

void SplitViewModule::on_tab_visibility_changed()
{
    // A tab of the split went out of view (another workspace): the split steps aside.
    if (!m_host || !m_layout)
        return;
    for (auto pane : m_layout->panes()) {
        if (auto* tab = tab_for(pane); tab && m_window.superbird().is_tab_hidden(*tab)) {
            hide_split();
            return;
        }
    }
}

void SplitViewModule::attach_tab_context_menu(Ladybird::Tab& tab, QMenu& menu)
{
    auto* split_with_current_action = new QAction("Split with Current Tab", &tab);
    QObject::connect(split_with_current_action, &QAction::triggered, &tab, [this, tab_pointer = QPointer(&tab)] {
        auto* current = m_window.current_tab();
        if (tab_pointer && current && current != tab_pointer.data())
            split_with(*current, *tab_pointer, Split::Orientation::Horizontal);
    });
    auto* add_to_split_action = new QAction("Add to Split View", &tab);
    QObject::connect(add_to_split_action, &QAction::triggered, &tab, [this, tab_pointer = QPointer(&tab)] {
        if (tab_pointer)
            add_to_split(*tab_pointer, Split::Orientation::Horizontal);
    });
    menu.addAction(split_with_current_action);
    menu.addAction(add_to_split_action);
    m_split_with_current_actions.set(&tab, split_with_current_action);
    m_add_to_split_actions.set(&tab, add_to_split_action);
}

void SplitViewModule::update_tab_context_menu(Ladybird::Tab& tab)
{
    auto enabled = is_enabled();
    auto in_split = m_layout && m_layout->contains(pane_for(tab));
    if (auto action = m_split_with_current_actions.get(&tab); action.has_value() && *action)
        (*action)->setVisible(enabled && !is_shown() && &tab != m_window.current_tab());
    if (auto action = m_add_to_split_actions.get(&tab); action.has_value() && *action)
        (*action)->setVisible(enabled && is_shown() && !in_split && m_layout->pane_count() < Split::MAXIMUM_PANES);
}

std::optional<SplitViewModule::DropTarget> SplitViewModule::drop_target_at(Ladybird::Tab& tab, QPoint global_position)
{
    if (!is_enabled() || m_window.tab_index(&tab) < 0 || !WindowHost::module_option_is_on(MODULE_ID, EDGE_DROP_OPTION))
        return {};
    QWidget* page = m_host ? static_cast<QWidget*>(m_host.data()) : nullptr;
    auto* current = m_window.current_tab();
    if (!page && current && current->is_loaded())
        page = &current->view();
    if (!page || !page->isVisible())
        return {};

    auto local = page->mapFromGlobal(global_position);
    if (!page->rect().contains(local))
        return {};
    auto x = static_cast<double>(local.x()) / page->width();
    auto y = static_cast<double>(local.y()) / page->height();

    DropTarget target { .orientation = Split::Orientation::Horizontal, .goes_first = false, .area = {} };
    if (x < EDGE_ZONE) {
        target.goes_first = true;
    } else if (x > 1 - EDGE_ZONE) {
    } else if (y < EDGE_ZONE) {
        target.orientation = Split::Orientation::Vertical;
        target.goes_first = true;
    } else if (y > 1 - EDGE_ZONE) {
        target.orientation = Split::Orientation::Vertical;
    } else {
        return {};
    }

    auto half_of = [&](QRect rect, bool first) {
        if (target.orientation == Split::Orientation::Horizontal)
            return first ? QRect(rect.left(), rect.top(), rect.width() / 2, rect.height()) : QRect(rect.left() + rect.width() / 2, rect.top(), rect.width() - rect.width() / 2, rect.height());
        return first ? QRect(rect.left(), rect.top(), rect.width(), rect.height() / 2) : QRect(rect.left(), rect.top() + rect.height() / 2, rect.width(), rect.height() - rect.height() / 2);
    };

    if (m_host && m_layout) {
        // The tab joins the split beside its focused pane, after it.
        auto pane = m_pane_ids.get(&tab);
        if ((pane.has_value() && m_layout->contains(*pane)) || m_layout->pane_count() >= Split::MAXIMUM_PANES)
            return {};
        auto focused_rect = m_host->pane_global_rect(m_layout->focused());
        if (!focused_rect.has_value())
            return {};
        target.goes_first = false;
        target.area = half_of(*focused_rect, false);
        return target;
    }
    if (!current || current == &tab)
        return {};
    target.area = half_of(QRect(page->mapToGlobal(QPoint(0, 0)), page->size()), target.goes_first);
    return target;
}

bool SplitViewModule::drop_tab_at(Ladybird::Tab& tab, QPoint global_position)
{
    auto target = drop_target_at(tab, global_position);
    if (!target.has_value())
        return false;
    if (m_host && m_layout) {
        add_to_split(tab, target->orientation);
        return true;
    }
    auto* current = m_window.current_tab();
    if (!current)
        return false;
    split_with(*current, tab, target->orientation);
    if (target->goes_first && m_layout) {
        m_layout->swap(pane_for(*current), pane_for(tab));
        relayout();
        focus_pane(pane_for(tab));
    }
    return true;
}

bool SplitViewModule::can_run(SplitCommand command) const
{
    if (!is_enabled())
        return false;
    auto* current = m_window.current_tab();
    switch (command) {
    case SplitCommand::SideBySide:
    case SplitCommand::Stacked:
        return current && !is_shown();
    case SplitCommand::Grid:
        return current && !is_shown() && m_window.tab_count() >= 3;
    case SplitCommand::AddNextTab:
        return is_shown() && m_layout->pane_count() < Split::MAXIMUM_PANES;
    case SplitCommand::SwapPanes:
    case SplitCommand::ToggleOrientation:
    case SplitCommand::FocusNextPane:
    case SplitCommand::EqualizePanes:
    case SplitCommand::Unsplit:
        return is_shown();
    }
    return false;
}

void SplitViewModule::run(SplitCommand command)
{
    if (!can_run(command))
        return;
    auto* current = m_window.current_tab();
    switch (command) {
    case SplitCommand::SideBySide:
    case SplitCommand::Stacked: {
        auto orientation = command == SplitCommand::SideBySide ? Split::Orientation::Horizontal : Split::Orientation::Vertical;
        if (auto* partner = next_visible_tab_after(*current)) {
            split_with(*current, *partner, orientation);
            return;
        }
        // Nothing to split with: a new tab takes the other side, ready for an address.
        QPointer<Ladybird::Tab> original = current;
        m_changing = true;
        m_window.open_new_tab();
        m_changing = false;
        auto* fresh = m_window.current_tab();
        if (original && fresh && fresh != original.data()) {
            split_with(*original, *fresh, orientation);
            focus_pane(pane_for(*fresh));
        }
        return;
    }
    case SplitCommand::Grid:
        split_grid();
        return;
    case SplitCommand::AddNextTab: {
        auto* candidate = current;
        for (int i = 0; i < m_window.tab_count() && candidate; ++i) {
            candidate = next_visible_tab_after(*candidate);
            if (candidate && !m_layout->contains(pane_for(*candidate))) {
                add_to_split(*candidate, Split::Orientation::Horizontal);
                return;
            }
        }
        return;
    }
    case SplitCommand::SwapPanes: {
        auto panes = m_layout->panes();
        auto focused = m_layout->focused();
        auto other = m_layout->next_pane(1);
        if (panes.size() >= 2 && m_layout->swap(focused, other))
            relayout();
        return;
    }
    case SplitCommand::ToggleOrientation:
        m_layout->toggle_orientation();
        relayout();
        return;
    case SplitCommand::FocusNextPane:
        focus_pane(m_layout->next_pane(1));
        return;
    case SplitCommand::EqualizePanes:
        m_layout->equalize();
        relayout();
        return;
    case SplitCommand::Unsplit:
        unsplit();
        return;
    }
}

void SplitViewModule::register_actions(ModuleRegistry& registry)
{
    static ActiveWindowSplitHandler handler;
    register_split_view_actions(registry, handler);

    // A tab dropped away from every tab strip asks the window under the pointer before opening in a window of its own.
    Ladybird::TabBar::on_tab_dropped_outside_strips = [](Ladybird::Tab& tab, QPoint global_position) {
        auto* widget = QApplication::widgetAt(global_position);
        auto* window = widget ? dynamic_cast<Ladybird::BrowserWindow*>(widget->window()) : nullptr;
        auto* module = ActiveWindowSplitHandler::module_of(window);
        return module && module->drop_tab_at(tab, global_position);
    };
    Ladybird::TabBar::on_loose_tab_drag_changed = [](Ladybird::Tab* tab) {
        DropIndicatorDriver::set_dragged_tab(tab);
    };
}

}
