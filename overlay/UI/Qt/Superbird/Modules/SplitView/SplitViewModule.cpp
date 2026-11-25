/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitViewHost.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitViewModule.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/TabBar.h>

#include <QAction>
#include <QApplication>
#include <QMenu>

namespace Superbird {

// A tab dropped this close to an edge of the page (as a share of its width or height) splits toward that edge.
static constexpr double EDGE_ZONE = 0.3;

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

}

ModuleInfo SplitViewModule::module_info()
{
    return {
        .id = MODULE_ID,
        .name = "Split View"sv,
        .description = "Tile two to four tabs side by side, stacked or in a grid. Split from a tab's menu, the Command Palette, or by dropping a tab at the page's edge."sv,
        .enabled_by_default = true,
    };
}

SplitViewModule::SplitViewModule(Ladybird::BrowserWindow& window)
    : m_window(window)
{
}

SplitViewModule::~SplitViewModule()
{
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
        if (m_layout)
            m_layout->set_ratios_around(pane, std::move(ratios));
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
}

void SplitViewModule::unsplit()
{
    hide_split();
    m_layout.reset();
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
        return;
    }
    show_split();
    focus_pane(m_layout->focused());
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
        return;
    }
    if (auto* current = m_window.current_tab(); current && m_layout->contains(pane_for(*current)))
        show_split();
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

bool SplitViewModule::drop_tab_at(Ladybird::Tab& tab, QPoint global_position)
{
    if (!is_enabled() || m_window.tab_index(&tab) < 0)
        return false;
    QWidget* page = m_host ? static_cast<QWidget*>(m_host.data()) : nullptr;
    auto* current = m_window.current_tab();
    if (!page && current && current->is_loaded())
        page = &current->view();
    if (!page)
        return false;

    auto local = page->mapFromGlobal(global_position);
    if (!page->rect().contains(local))
        return false;
    auto x = static_cast<double>(local.x()) / page->width();
    auto y = static_cast<double>(local.y()) / page->height();

    std::optional<Split::Orientation> orientation;
    bool goes_first = false;
    if (x < EDGE_ZONE) {
        orientation = Split::Orientation::Horizontal;
        goes_first = true;
    } else if (x > 1 - EDGE_ZONE) {
        orientation = Split::Orientation::Horizontal;
    } else if (y < EDGE_ZONE) {
        orientation = Split::Orientation::Vertical;
        goes_first = true;
    } else if (y > 1 - EDGE_ZONE) {
        orientation = Split::Orientation::Vertical;
    }
    if (!orientation.has_value())
        return false;

    if (m_host && m_layout) {
        if (m_layout->contains(pane_for(tab)) || m_layout->pane_count() >= Split::MAXIMUM_PANES)
            return false;
        add_to_split(tab, *orientation);
        return true;
    }
    if (!current || current == &tab)
        return false;
    split_with(*current, tab, *orientation);
    if (goes_first && m_layout) {
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
}

}
