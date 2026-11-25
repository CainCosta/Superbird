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
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceStore.h>
#if defined(SUPERBIRD_MODULE_TREE_TABS)
#    include <UI/Qt/Superbird/Modules/TreeTabs/TreeTabController.h>
#endif
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacePicker.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacePresets.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceSwitcher.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspacesModule.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/TabBar.h>
#include <UI/Qt/Theme.h>

#include <QAction>
#include <QCursor>
#include <QEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QTimer>

namespace Superbird {

static constexpr int PERSIST_DELAY_MS = 500;

static QKeySequence switch_shortcut(size_t slot)
{
    auto key = static_cast<Qt::Key>(Qt::Key_1 + static_cast<int>(slot));
#if defined(AK_OS_MACOS)
    // NB: Qt::META is the Control key on macOS. Command+digit already selects a tab, and Control+digit is Mission
    //     Control's "Switch to Desktop N", so workspaces take Control+Option+digit.
    return QKeySequence(Qt::META | Qt::ALT | key);
#else
    return QKeySequence(Qt::CTRL | Qt::ALT | key);
#endif
}

static QString to_qstring(std::string const& string)
{
    return QString::fromUtf8(string.data(), static_cast<qsizetype>(string.size()));
}

namespace {

// Tells the module when its window becomes the active one, so the bus's workspace titles follow the active window.
class ActivationWatcher final : public QObject {
public:
    ActivationWatcher(QObject* parent, Function<void()> on_activate)
        : QObject(parent)
        , m_on_activate(move(on_activate))
    {
    }

private:
    virtual bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::WindowActivate && m_on_activate)
            m_on_activate();
        return false;
    }

    Function<void()> m_on_activate;
};

}

namespace {

// Routes Workspaces actions from the action bus to the active window's module.
class ActiveWindowWorkspacesHandler final : public WorkspacesCommandHandler {
public:
    virtual bool can_run(WorkspacesCommand command, size_t slot) const override
    {
        auto* module = active_module();
        return module && module->can_run(command, slot);
    }

    virtual void run(WorkspacesCommand command, size_t slot) override
    {
        if (auto* module = active_module())
            module->run(command, slot);
    }

    static WorkspacesModule* active_module()
    {
        auto* window = Ladybird::Application::the().active_window_if_any();
        if (!window)
            return nullptr;
        return static_cast<WorkspacesModule*>(window->superbird().module(WORKSPACES_MODULE_ID));
    }
};

ActiveWindowWorkspacesHandler& handler()
{
    static ActiveWindowWorkspacesHandler instance;
    return instance;
}

}

ModuleInfo WorkspacesModule::module_info()
{
    return {
        .id = MODULE_ID,
        .name = "Workspaces"sv,
        .description = "Split a window's tabs into named workspaces (Work, Research, Personal...) and switch between them from the sidebar, the Command Palette or Ctrl+Alt+1-9 (Ctrl+Option+1-9 on macOS)."sv,
        .enabled_by_default = true,
    };
}

WorkspacesModule::WorkspacesModule(Ladybird::BrowserWindow& window)
    : m_window(window)
{
    if (is_enabled())
        install();
}

WorkspacesModule::~WorkspacesModule()
{
    if (m_persist_timer && m_persist_timer->isActive())
        persist_now();
    uninstall();
}

bool WorkspacesModule::is_enabled() const
{
    return ModuleRegistry::the().is_enabled(MODULE_ID);
}

void WorkspacesModule::install()
{
    if (!m_persist_timer) {
        m_persist_timer = new QTimer(&m_window);
        m_persist_timer->setSingleShot(true);
        m_persist_timer->setInterval(PERSIST_DELAY_MS);
        QObject::connect(m_persist_timer, &QTimer::timeout, &m_window, [this] { persist_now(); });
    }
    if (!m_activation_watcher) {
        m_activation_watcher = new ActivationWatcher(&m_window, [this] { publish_action_titles(); });
        m_window.installEventFilter(m_activation_watcher);
    }
    if (m_shortcuts.is_empty()) {
        for (size_t slot = 0; slot < WORKSPACE_SWITCH_SLOTS; ++slot) {
            auto* shortcut = new QShortcut(switch_shortcut(slot), &m_window);
            shortcut->setContext(Qt::WindowShortcut);
            QObject::connect(shortcut, &QShortcut::activated, &m_window, [this, slot] { run(WorkspacesCommand::SwitchToWorkspace, slot); });
            m_shortcuts.append(shortcut);
            WindowHost::reserve_shortcut(switch_shortcut(slot));
        }
    }
    if (!m_switcher) {
        m_switcher = new WorkspaceSwitcher;
        m_switcher->on_activate = [this](auto id) { switch_to(id); };
        m_switcher->on_create = [this] { create_workspace_interactively(); };
        m_switcher->on_build_menu = [this](QMenu& menu, auto id) { fill_workspace_menu(menu, id); };
        m_switcher->on_build_overview_menu = [this](QMenu& menu) { fill_overview_menu(menu); };
        m_window.tab_widget().set_sidebar_footer_widget(m_switcher);
    }

    for (int index = 0; index < m_window.tab_count(); ++index) {
        if (auto* tab = m_window.tab_at(index))
            on_tab_added(*tab);
    }
    // NB: The window's WindowHost is still being made when its modules are, so the first refresh waits for it.
    QTimer::singleShot(0, &m_window, [this] {
        if (is_enabled())
            changed();
    });
}

void WorkspacesModule::uninstall()
{
    for (size_t slot = 0; slot < m_shortcuts.size(); ++slot) {
        WindowHost::release_shortcut(switch_shortcut(slot));
        delete m_shortcuts[slot].data();
    }
    m_shortcuts.clear();
    if (m_switcher) {
        m_window.tab_widget().set_sidebar_footer_widget(nullptr);
        delete m_switcher.data();
    }
    if (m_picker)
        delete m_picker.data();
    for (auto& it : m_pill_actions)
        delete it.value.data();
    m_pill_actions.clear();
    for (auto& it : m_tab_menus) {
        if (it.value)
            it.value->menuAction()->setVisible(false);
    }
    if (m_activation_watcher) {
        m_window.removeEventFilter(m_activation_watcher);
        delete m_activation_watcher.data();
    }
}

void WorkspacesModule::on_enabled_changed(bool enabled)
{
    if (enabled) {
        install();
        return;
    }
    if (m_persist_timer && m_persist_timer->isActive())
        persist_now();
    uninstall();
    // With the module off, every tab shows again.
    m_window.superbird().update_tab_visibility();
}

void WorkspacesModule::on_settings_changed()
{
    if (is_enabled())
        update_pill_actions();
}

void WorkspacesModule::on_chrome_style_changed()
{
    if (!is_enabled())
        return;
    if (m_switcher)
        m_switcher->update();
    update_pill_actions();
}

Workspaces::TabKey WorkspacesModule::key_for(Ladybird::Tab& tab)
{
    if (auto existing = m_keys.get(&tab); existing.has_value())
        return *existing;
    // A tab without a session id (yet) gets a transient negative key, which is never persisted.
    auto key = tab.session_tab_id().has_value() ? *tab.session_tab_id() : m_next_transient_key--;
    m_keys.set(&tab, key);
    return key;
}

std::vector<Workspaces::TabKey> WorkspacesModule::strip_order()
{
    std::vector<Workspaces::TabKey> order;
    for (int index = 0; index < m_window.tab_count(); ++index) {
        if (auto* tab = m_window.tab_at(index))
            order.push_back(key_for(*tab));
    }
    return order;
}

Ladybird::Tab* WorkspacesModule::tab_for_key(Workspaces::TabKey key)
{
    for (auto& it : m_keys) {
        if (it.value == key)
            return it.key;
    }
    return nullptr;
}

// Only a window restored from the session has stored workspaces. Session ids are SQLite row ids, which get reused once
// the session tables empty, so a fresh window's id may match rows a long-gone window left behind; a fresh window ignores
// them, and its first save replaces them.
void WorkspacesModule::load_restored_state()
{
    if (m_loaded || !is_enabled())
        return;
    auto window_id = m_window.session_window_id();
    if (!window_id.has_value())
        return;
    m_loaded = true;

    auto rows = WorkspaceStore::the(m_window.is_private() == WebView::IsPrivate::Yes).load_window(*window_id);
    if (rows.workspaces.empty())
        return;

    // Tabs keep the workspace they were saved in; tabs the store doesn't know stay where they are now.
    auto current_assignments = m_model.snapshot().assignments;
    auto snapshot = Workspaces::rows_to_snapshot(*window_id, rows);
    auto restored = Workspaces::WorkspaceModel::from_snapshot(snapshot);
    for (auto const& [tab, workspace] : current_assignments) {
        if (!restored.workspace_of(tab).has_value())
            restored.add_tab(tab);
    }
    restored.set_pinned_tabs_are_global(true);
    m_model = move(restored);

    // The window shows its current tab's workspace.
    if (auto* current = m_window.current_tab()) {
        if (auto workspace = m_model.workspace_of(key_for(*current)); workspace.has_value() && *workspace != m_model.active_workspace()) {
            auto order = strip_order();
            m_model.activate(*workspace, order);
        }
    }
}

void WorkspacesModule::on_tab_added(Ladybird::Tab& tab)
{
    if (!is_enabled())
        return;
    auto key = key_for(tab);
    m_model.add_tab(key);
    if (!m_switching)
        changed();

    if (!m_pill_actions.contains(&tab)) {
        auto attach_pill = [this, tab_pointer = QPointer(&tab)] {
            if (!tab_pointer || !tab_pointer->is_loaded() || m_pill_actions.contains(tab_pointer.data()) || !is_enabled())
                return;
            auto* action = new QAction(tab_pointer.data());
            QObject::connect(action, &QAction::triggered, tab_pointer.data(), [this] {
                QMenu menu(&m_window);
                fill_overview_menu(menu);
                menu.exec(QCursor::pos());
            });
            m_pill_actions.set(tab_pointer.data(), action);
            tab_pointer->location_edit().add_page_action(action);
            update_pill_actions();
        };
        if (tab.is_loaded())
            attach_pill();
        else
            QObject::connect(&tab, &Ladybird::Tab::loaded, &m_window, attach_pill);
    }
}

void WorkspacesModule::on_tab_removed(Ladybird::Tab& tab)
{
    if (!is_enabled())
        return;
    if (&tab == m_window.current_tab())
        m_current_tab_closing = true;
    if (auto key = m_keys.take(&tab); key.has_value())
        m_model.remove_tab(*key);
    if (auto action = m_pill_actions.take(&tab); action.has_value())
        delete action->data();
    m_tab_menus.remove(&tab);
    schedule_persist();
    update_switcher();

    // Once the window has picked the next tab: if it's in another workspace (or there's none), stay here instead.
    QTimer::singleShot(0, &m_window, [this] {
        if (!m_current_tab_closing)
            return;
        m_current_tab_closing = false;
        auto* current = m_window.current_tab();
        if (current && !is_tab_hidden(*current))
            return;
        auto order = strip_order();
        auto tabs = m_model.tabs_in(m_model.active_workspace(), order);
        m_switching = true;
        if (!tabs.empty()) {
            if (auto* tab = tab_for_key(tabs.front()))
                m_window.activate_tab(m_window.tab_index(tab));
        } else {
            m_window.open_new_tab();
        }
        m_switching = false;
        changed();
    });
}

void WorkspacesModule::on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab)
{
    if (!is_enabled())
        return;
    auto* old_pointer = const_cast<Ladybird::Tab*>(&old_tab);
    auto key = m_keys.take(old_pointer);
    if (auto action = m_pill_actions.take(old_pointer); action.has_value())
        delete action->data();
    m_tab_menus.remove(old_pointer);
    if (key.has_value()) {
        // The replacement keeps the tab's place, workspace and key.
        m_keys.set(&new_tab, *key);
    }
    on_tab_added(new_tab);
    changed();
}

void WorkspacesModule::on_tab_session_id_assigned(Ladybird::Tab& tab)
{
    if (!is_enabled() || !tab.session_tab_id().has_value())
        return;
    auto old_key = m_keys.get(&tab);
    auto new_key = *tab.session_tab_id();
    if (old_key.has_value() && *old_key == new_key)
        return;
    std::optional<Workspaces::WorkspaceId> workspace;
    if (old_key.has_value()) {
        workspace = m_model.workspace_of(*old_key);
        m_model.remove_tab(*old_key);
    }
    m_keys.set(&tab, new_key);
    m_model.add_tab(new_key, workspace);
    schedule_persist();
}

void WorkspacesModule::on_active_tab_changed(Ladybird::Tab* tab)
{
    if (!is_enabled() || !tab || m_switching)
        return;
    auto key = key_for(*tab);
    if (m_current_tab_closing && is_tab_hidden(*tab)) {
        // Handled once the close settles (see on_tab_removed()).
        return;
    }
    // A tab picked from elsewhere (the Command Palette, Ctrl+number, a link) brings its workspace along.
    if (auto other = m_model.tab_activated(key); other.has_value()) {
        auto order = strip_order();
        m_switching = true;
        m_model.activate(*other, order);
        m_model.tab_activated(key);
        m_switching = false;
        changed();
    }
}

void WorkspacesModule::on_tabs_restored()
{
    if (!is_enabled())
        return;
    load_restored_state();
    changed();
}

bool WorkspacesModule::is_tab_hidden(Ladybird::Tab& tab) const
{
    if (!is_enabled())
        return false;
    auto key = m_keys.get(&tab);
    if (!key.has_value())
        return false;
    return !m_model.is_visible(*key, tab.is_pinned());
}

void WorkspacesModule::append_tab_context(Ladybird::Tab& tab, Vector<String>& labels)
{
    if (m_model.workspaces().size() < 2)
        return;
    auto key = m_keys.get(&tab);
    if (!key.has_value())
        return;
    if (auto workspace = m_model.workspace_of(*key); workspace.has_value()) {
        if (auto const* info = m_model.workspace(*workspace))
            labels.append(MUST(String::from_utf8(StringView { info->name.data(), info->name.size() })));
    }
}

void WorkspacesModule::switch_to(Workspaces::WorkspaceId workspace)
{
    auto order = strip_order();
    auto result = m_model.activate(workspace, order);
    if (!result.has_value())
        return;

    auto& host = m_window.superbird();
    m_switching = true;
    // Show the target workspace's tabs before making one current, so the tab bar never selects a hidden tab.
    host.update_tab_visibility(WindowHost::HideTabs::No);
    if (result->tab_to_activate.has_value()) {
        if (auto* tab = tab_for_key(*result->tab_to_activate)) {
            m_window.activate_tab(m_window.tab_index(tab));
            m_model.tab_activated(*result->tab_to_activate);
        }
    } else {
        m_window.open_new_tab();
    }
    m_switching = false;
    changed();
}

// With Tree-Style Tabs, a tab takes its whole branch along, so a tree never ends up split across workspaces.
Vector<Ladybird::Tab*> WorkspacesModule::tab_with_branch(Ladybird::Tab& tab) const
{
#if defined(SUPERBIRD_MODULE_TREE_TABS)
    if (auto* controller = m_window.superbird().tree_tab_controller(); controller && controller->is_enabled()) {
        if (auto node_id = controller->model().node_id_for_tab(&tab); node_id.has_value()) {
            auto tabs = controller->model().subtree_tabs(*node_id);
            if (!tabs.is_empty())
                return tabs;
        }
    }
#endif
    return { &tab };
}

void WorkspacesModule::move_tab_to(Ladybird::Tab& tab, Workspaces::WorkspaceId workspace)
{
    bool moved = false;
    bool moved_current = false;
    for (auto* member : tab_with_branch(tab)) {
        if (m_model.assign_tab(key_for(*member), workspace)) {
            moved = true;
            moved_current |= member == m_window.current_tab();
        }
    }
    if (!moved)
        return;
    // Tabs that leave the active workspace take the window along only if they were the last ones there.
    if (moved_current && workspace != m_model.active_workspace()) {
        auto order = strip_order();
        auto remaining = m_model.tabs_in(m_model.active_workspace(), order);
        if (remaining.empty()) {
            switch_to(workspace);
            return;
        }
        m_switching = true;
        if (auto* next = tab_for_key(remaining.front()))
            m_window.activate_tab(m_window.tab_index(next));
        m_switching = false;
    }
    changed();
}

WorkspacePicker& WorkspacesModule::picker()
{
    if (!m_picker) {
        m_picker = new WorkspacePicker(&m_window);
        m_picker->on_size_changed = [this] { place_picker(); };
    }
    m_picker->update_chrome_style(Ladybird::Theme::chrome_palette(m_window));
    return *m_picker;
}

void WorkspacesModule::place_picker()
{
    if (!m_picker)
        return;
    m_picker->adjustSize();
    // Over the switcher it came from, when the sidebar shows one; otherwise dropping from the top of the window, like the
    // Command Palette.
    if (m_switcher && m_switcher->isVisible() && m_switcher->window() == &m_window) {
        Ladybird::move_popover_above(*m_picker, *m_switcher, Ladybird::PopoverAlignment::Leading);
        m_picker->set_anchor_point(m_switcher->mapToGlobal(QPoint(m_switcher->width() / 2, 0)));
        return;
    }
    auto size = m_picker->size();
    auto top = max(48, m_window.height() / 8);
    auto top_left = m_window.mapToGlobal(QPoint((m_window.width() - size.width()) / 2, top));
    if (auto* screen = m_window.screen()) {
        auto available = screen->availableGeometry();
        top_left.setX(clamp(top_left.x(), available.left(), max(available.left(), available.right() - size.width())));
        top_left.setY(clamp(top_left.y(), available.top(), max(available.top(), available.bottom() - size.height())));
    }
    m_picker->move(top_left);
    m_picker->set_anchor_point(m_window.mapToGlobal(QPoint(m_window.width() / 2, top)));
}

void WorkspacesModule::create_workspace_interactively(Function<void(Workspaces::WorkspaceId)> on_created)
{
    auto& picker = this->picker();
    std::vector<std::string> names;
    for (auto const& workspace : m_model.workspaces())
        names.push_back(workspace.name);
    picker.on_accept = [this, on_created = move(on_created)](WorkspacePicker::Result result) {
        auto workspace = m_model.create_workspace(result.name, result.icon, result.tint);
        if (on_created)
            on_created(workspace);
        else
            switch_to(workspace);
        changed();
    };
    picker.show_presets(std::move(names), Workspaces::default_tint_for_new_workspace(m_model.workspaces().size()));
    place_picker();
    picker.show();
}

void WorkspacesModule::edit_workspace_interactively(Workspaces::WorkspaceId workspace)
{
    auto const* info = m_model.workspace(workspace);
    if (!info)
        return;
    auto& picker = this->picker();
    picker.on_accept = [this, workspace](WorkspacePicker::Result result) {
        auto renamed = m_model.rename_workspace(workspace, result.name);
        auto icon_changed = m_model.set_icon(workspace, result.icon);
        auto tint_changed = m_model.set_tint(workspace, result.tint);
        if (renamed || icon_changed || tint_changed)
            changed();
    };
    picker.show_editor(*info);
    place_picker();
    picker.show();
}

void WorkspacesModule::delete_workspace(Workspaces::WorkspaceId workspace)
{
    if (m_model.workspaces().size() < 2)
        return;
    auto const* info = m_model.workspace(workspace);
    if (!info)
        return;
    auto tab_count = m_model.tab_count(workspace);
    if (tab_count > 0) {
        auto answer = QMessageBox::question(&m_window, "Delete Workspace",
            QString("Delete \"%1\"? Its %2 tab(s) move to the next workspace.").arg(to_qstring(info->name)).arg(tab_count));
        if (answer != QMessageBox::Yes)
            return;
    }
    auto was_active = workspace == m_model.active_workspace();
    m_model.remove_workspace(workspace);
    if (was_active) {
        // The fallback workspace is active now; show its remembered tab.
        auto order = strip_order();
        auto tabs = m_model.tabs_in(m_model.active_workspace(), order);
        m_switching = true;
        m_window.superbird().update_tab_visibility(WindowHost::HideTabs::No);
        if (auto* current = m_window.current_tab(); current && !tabs.empty() && is_tab_hidden(*current)) {
            if (auto* tab = tab_for_key(tabs.front()))
                m_window.activate_tab(m_window.tab_index(tab));
        }
        m_switching = false;
    }
    changed();
}

void WorkspacesModule::fill_workspace_menu(QMenu& menu, Workspaces::WorkspaceId workspace)
{
    auto palette = Ladybird::Theme::chrome_palette(m_window);
    menu.addAction("&Edit Workspace\xE2\x80\xA6", [this, workspace] { edit_workspace_interactively(workspace); });
    auto* colors = menu.addMenu("&Color");
    for (auto const& tint : Workspaces::TINTS) {
        Workspaces::Workspace sample { .id = 0, .name = "", .icon = "", .tint = tint.rgb };
        colors->addAction(workspace_badge_icon(sample, true, palette), QString::fromUtf8(tint.name.data(), static_cast<qsizetype>(tint.name.size())), [this, workspace, rgb = tint.rgb] {
            if (m_model.set_tint(workspace, rgb))
                changed();
        });
    }
    colors->addSeparator();
    colors->addAction("Accent", [this, workspace] {
        if (m_model.set_tint(workspace, {}))
            changed();
    });

    auto index = m_model.index_of(workspace).value_or(0);
    auto* move_left = menu.addAction("Move &Left", [this, workspace, index] {
        if (m_model.move_workspace(workspace, index - 1))
            changed();
    });
    move_left->setEnabled(index > 0);
    auto* move_right = menu.addAction("Move Ri&ght", [this, workspace, index] {
        if (m_model.move_workspace(workspace, index + 1))
            changed();
    });
    move_right->setEnabled(index + 1 < m_model.workspaces().size());
    menu.addSeparator();
    auto* remove = menu.addAction("&Delete Workspace", [this, workspace] { delete_workspace(workspace); });
    remove->setEnabled(m_model.workspaces().size() > 1);
}

void WorkspacesModule::fill_overview_menu(QMenu& menu)
{
    auto palette = Ladybird::Theme::chrome_palette(m_window);
    for (auto const& workspace : m_model.workspaces()) {
        auto active = workspace.id == m_model.active_workspace();
        auto* action = menu.addAction(workspace_badge_icon(workspace, active, palette), to_qstring(workspace.name), [this, id = workspace.id] { switch_to(id); });
        action->setCheckable(true);
        action->setChecked(active);
    }
    menu.addSeparator();
    menu.addAction("&New Workspace\xE2\x80\xA6", [this] { create_workspace_interactively(); });
    auto active = m_model.active_workspace();
    menu.addAction("&Edit Current Workspace\xE2\x80\xA6", [this, active] { edit_workspace_interactively(active); });
    auto* remove = menu.addAction("&Delete Current Workspace", [this, active] { delete_workspace(active); });
    remove->setEnabled(m_model.workspaces().size() > 1);
}

void WorkspacesModule::attach_tab_context_menu(Ladybird::Tab& tab, QMenu& menu)
{
    auto* submenu = new QMenu("Move to &Workspace", &menu);
    QObject::connect(submenu, &QMenu::aboutToShow, submenu, [this, submenu, tab_pointer = QPointer(&tab)] {
        submenu->clear();
        if (!tab_pointer)
            return;
        auto current = m_model.workspace_of(key_for(*tab_pointer));
        auto palette = Ladybird::Theme::chrome_palette(m_window);
        for (auto const& workspace : m_model.workspaces()) {
            auto* action = submenu->addAction(workspace_badge_icon(workspace, false, palette), to_qstring(workspace.name), [this, tab_pointer, id = workspace.id] {
                if (tab_pointer)
                    move_tab_to(*tab_pointer, id);
            });
            action->setEnabled(current != workspace.id);
        }
        submenu->addSeparator();
        submenu->addAction("New Workspace\xE2\x80\xA6", [this, tab_pointer] {
            create_workspace_interactively([this, tab_pointer](Workspaces::WorkspaceId workspace) {
                if (tab_pointer)
                    move_tab_to(*tab_pointer, workspace);
            });
        });
    });
    menu.addMenu(submenu);
    m_tab_menus.set(&tab, submenu);
}

void WorkspacesModule::update_tab_context_menu(Ladybird::Tab& tab)
{
    if (auto submenu = m_tab_menus.get(&tab); submenu.has_value() && *submenu)
        (*submenu)->menuAction()->setVisible(is_enabled() && !tab.is_pinned());
}

void WorkspacesModule::changed()
{
    if (!is_enabled())
        return;
    m_window.superbird().update_tab_visibility();
    update_switcher();
    update_pill_actions();
    publish_action_titles();
    schedule_persist();
}

void WorkspacesModule::update_switcher()
{
    if (m_switcher)
        m_switcher->set_workspaces(m_model.workspaces(), m_model.active_workspace());
}

void WorkspacesModule::update_pill_actions()
{
    // With horizontal tabs there's no sidebar to hold the switcher, so the address field shows the active workspace.
    auto horizontal = !Ladybird::Application::settings().tab_settings().vertical_tabs_enabled;
    auto const* active = m_model.workspace(m_model.active_workspace());
    auto palette = Ladybird::Theme::chrome_palette(m_window);
    for (auto& it : m_pill_actions) {
        auto& action = it.value;
        if (!action || !active)
            continue;
        action->setIcon(workspace_badge_icon(*active, true, palette));
        action->setToolTip(QString("Workspace: %1").arg(to_qstring(active->name)));
        action->setVisible(horizontal && is_enabled());
    }
}

void WorkspacesModule::publish_action_titles()
{
    if (!is_enabled() || Ladybird::Application::the().active_window_if_any() != &m_window)
        return;
    std::vector<std::string> names;
    for (auto const& workspace : m_model.workspaces())
        names.push_back(workspace.name);
    set_workspace_switch_titles(ModuleRegistry::the(), handler(), names);
}

void WorkspacesModule::schedule_persist()
{
    if (m_persist_timer && is_enabled())
        m_persist_timer->start();
}

void WorkspacesModule::persist_now()
{
    if (m_persist_timer)
        m_persist_timer->stop();
    auto window_id = m_window.session_window_id();
    if (!window_id.has_value())
        return;
    auto snapshot = m_model.snapshot();
    // Transient keys belong to tabs that have no session row, so there's nothing to restore them by.
    std::erase_if(snapshot.assignments, [](auto const& assignment) { return assignment.first < 0; });
    WorkspaceStore::the(m_window.is_private() == WebView::IsPrivate::Yes).save_window(*window_id, Workspaces::snapshot_to_rows(*window_id, snapshot));
}

bool WorkspacesModule::can_run(WorkspacesCommand command, size_t slot) const
{
    if (!is_enabled())
        return false;
    switch (command) {
    case WorkspacesCommand::NewWorkspace:
    case WorkspacesCommand::RenameWorkspace:
        return true;
    case WorkspacesCommand::DeleteWorkspace:
    case WorkspacesCommand::NextWorkspace:
    case WorkspacesCommand::PreviousWorkspace:
        return m_model.workspaces().size() > 1;
    case WorkspacesCommand::MoveTabToNextWorkspace:
        return m_model.workspaces().size() > 1 && m_window.current_tab() && !m_window.current_tab()->is_pinned();
    case WorkspacesCommand::SwitchToWorkspace:
        return slot < m_model.workspaces().size() && m_model.workspaces()[slot].id != m_model.active_workspace();
    }
    return false;
}

void WorkspacesModule::run(WorkspacesCommand command, size_t slot)
{
    if (!can_run(command, slot))
        return;
    switch (command) {
    case WorkspacesCommand::NewWorkspace:
        create_workspace_interactively();
        break;
    case WorkspacesCommand::RenameWorkspace:
        edit_workspace_interactively(m_model.active_workspace());
        break;
    case WorkspacesCommand::DeleteWorkspace:
        delete_workspace(m_model.active_workspace());
        break;
    case WorkspacesCommand::NextWorkspace:
    case WorkspacesCommand::PreviousWorkspace:
        if (auto next = m_model.next_workspace(command == WorkspacesCommand::NextWorkspace ? 1 : -1); next.has_value())
            switch_to(*next);
        break;
    case WorkspacesCommand::MoveTabToNextWorkspace:
        if (auto next = m_model.next_workspace(1); next.has_value())
            move_tab_to(*m_window.current_tab(), *next);
        break;
    case WorkspacesCommand::SwitchToWorkspace:
        switch_to(m_model.workspaces()[slot].id);
        break;
    }
}

void WorkspacesModule::register_actions(ModuleRegistry& registry)
{
    register_workspaces_actions(registry, handler());
}

}
