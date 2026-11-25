/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/ChromeStyle.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/Superbird/Modules/SplitView/SplitViewHost.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/Theme.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace Superbird {

static constexpr int HEADER_HEIGHT = 24;
static constexpr char const* PANE_PROPERTY = "superbirdSplitPane";

SplitViewHost::SplitViewHost(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("SuperbirdSplitViewHost");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
}

SplitViewHost::~SplitViewHost()
{
    // NB: The tabs belong to the window, not to the host; whatever still holds them lets go before the panes go.
    release_tabs();
}

void SplitViewHost::release_tabs()
{
    for (auto& it : m_panes) {
        auto& tab = it.value.tab;
        if (!tab)
            continue;
        if (tab->is_loaded())
            tab->view().removeEventFilter(this);
        tab->setParent(nullptr);
    }
    m_panes.clear();
    if (m_root)
        delete m_root.data();
}

void SplitViewHost::rebuild(Split::SplitLayout const& layout, Function<Ladybird::Tab*(Split::PaneId)> const& tab_for)
{
    // Keep the tabs out of harm's way while the old panes go.
    for (auto& it : m_panes) {
        if (auto& tab = it.value.tab) {
            if (tab->is_loaded())
                tab->view().removeEventFilter(this);
            tab->hide();
            tab->setParent(this);
        }
    }
    m_panes.clear();
    if (m_root)
        delete m_root.data();

    m_root = build_node(layout.root(), tab_for);
    if (m_root)
        static_cast<QVBoxLayout*>(this->layout())->addWidget(m_root);
    m_focused = layout.focused();
    for (auto& it : m_panes)
        style_pane(it.key, it.value);
}

QWidget* SplitViewHost::build_node(Split::SplitLayout::Node const& node, Function<Ladybird::Tab*(Split::PaneId)> const& tab_for)
{
    if (node.is_leaf()) {
        auto* tab = tab_for(*node.pane);
        if (!tab)
            return nullptr;
        return build_pane(*node.pane, *tab);
    }

    auto* splitter = new QSplitter(node.orientation == Split::Orientation::Horizontal ? Qt::Horizontal : Qt::Vertical);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(4);
    std::optional<Split::PaneId> leaf_child;
    for (auto const& child : node.children) {
        if (auto* widget = build_node(*child, tab_for))
            splitter->addWidget(widget);
        if (child->is_leaf() && !leaf_child.has_value())
            leaf_child = *child->pane;
    }

    // The splitter learns its size only once it's shown, so the ratios are applied then.
    auto ratios = node.ratios;
    QTimer::singleShot(0, splitter, [splitter, ratios] {
        auto total = splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height();
        QList<int> sizes;
        for (size_t i = 0; i < ratios.size() && static_cast<int>(i) < splitter->count(); ++i)
            sizes.append(static_cast<int>(ratios[i] * total));
        if (sizes.size() == splitter->count())
            splitter->setSizes(sizes);
    });

    // Only a split with a pane of its own can report its ratios to the layout, which finds splits by their panes.
    if (leaf_child.has_value()) {
        QObject::connect(splitter, &QSplitter::splitterMoved, this, [this, splitter, pane = *leaf_child] {
            if (!on_ratios_changed)
                return;
            std::vector<double> ratios;
            double total = 0;
            for (auto size : splitter->sizes())
                total += size;
            if (total <= 0)
                return;
            for (auto size : splitter->sizes())
                ratios.push_back(size / total);
            on_ratios_changed(pane, std::move(ratios));
        });
    }
    return splitter;
}

QFrame* SplitViewHost::build_pane(Split::PaneId pane_id, Ladybird::Tab& tab)
{
    auto* frame = new QFrame;
    frame->setObjectName("SuperbirdSplitPane");
    frame->setProperty(PANE_PROPERTY, QVariant::fromValue<qlonglong>(pane_id));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(frame);
    header->setObjectName("SuperbirdSplitPaneHeader");
    header->setFixedHeight(HEADER_HEIGHT);
    header->setProperty(PANE_PROPERTY, QVariant::fromValue<qlonglong>(pane_id));
    header->installEventFilter(this);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(8, 0, 4, 0);
    header_layout->setSpacing(2);

    auto* title = new QLabel(header);
    title->setObjectName("SuperbirdSplitPaneTitle");
    title->setTextInteractionFlags(Qt::NoTextInteraction);
    header_layout->addWidget(title, 1);

    auto make_button = [&](QString const& tooltip) {
        auto* button = new QToolButton(header);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(tooltip);
        button->setFixedSize(20, 20);
        header_layout->addWidget(button);
        return button;
    };
    auto* swap = make_button("Swap Panes");
    swap->setProperty("superbirdChromeIcon", static_cast<int>(Ladybird::ChromeIcon::DoubleChevronRight));
    QObject::connect(swap, &QToolButton::clicked, this, [this] {
        if (on_swap)
            on_swap();
    });
    auto* unsplit = make_button("Unsplit");
    unsplit->setProperty("superbirdChromeIcon", static_cast<int>(Ladybird::ChromeIcon::WindowMaximize));
    QObject::connect(unsplit, &QToolButton::clicked, this, [this] {
        if (on_unsplit)
            on_unsplit();
    });
    auto* remove = make_button("Remove from Split");
    remove->setProperty("superbirdChromeIcon", static_cast<int>(Ladybird::ChromeIcon::TabClose));
    QObject::connect(remove, &QToolButton::clicked, this, [this, pane_id] {
        if (on_remove_pane)
            on_remove_pane(pane_id);
    });

    layout->addWidget(header);
    tab.setParent(frame);
    layout->addWidget(&tab, 1);
    tab.show();
    if (tab.is_loaded())
        tab.view().installEventFilter(this);

    m_panes.set(pane_id, Pane { .frame = frame, .title = title, .tab = &tab });
    update_titles();
    return frame;
}

std::optional<QRect> SplitViewHost::pane_global_rect(Split::PaneId pane) const
{
    auto it = m_panes.find(pane);
    if (it == m_panes.end() || !it->value.frame)
        return {};
    auto* frame = it->value.frame.data();
    return QRect(frame->mapToGlobal(QPoint(0, 0)), frame->size());
}

void SplitViewHost::update_titles()
{
    for (auto& it : m_panes) {
        auto& pane = it.value;
        if (!pane.title || !pane.tab)
            continue;
        auto metrics = pane.title->fontMetrics();
        pane.title->setText(metrics.elidedText(pane.tab->title(), Qt::ElideRight, max(40, pane.title->width())));
    }
}

void SplitViewHost::set_focused(Split::PaneId pane)
{
    m_focused = pane;
    for (auto& it : m_panes)
        style_pane(it.key, it.value);
}

void SplitViewHost::update_chrome_style()
{
    for (auto& it : m_panes)
        style_pane(it.key, it.value);
}

void SplitViewHost::style_pane(Split::PaneId pane_id, Pane const& pane)
{
    if (!pane.frame)
        return;
    auto palette = Ladybird::Theme::chrome_palette(*this);
    auto const& chrome = Ladybird::Theme::chrome(palette);
    auto focused = pane_id == m_focused;
    auto border = focused ? Ladybird::Theme::accent() : chrome.divider;
    pane.frame->setStyleSheet(QString(
        // NB: Square corners: the page inside is a native view, which a rounded frame can't clip.
        "QFrame#SuperbirdSplitPane { border: %1px solid %2; }"
        "QWidget#SuperbirdSplitPaneHeader { background: %3; }"
        "QLabel#SuperbirdSplitPaneTitle { color: %4; font-size: 12px; }"
        "QWidget#SuperbirdSplitPaneHeader QToolButton { color: %5; border: none; border-radius: 4px; }"
        "QWidget#SuperbirdSplitPaneHeader QToolButton:hover { background: %6; }")
            .arg(focused ? 2 : 1)
            .arg(Ladybird::ChromeStyle::style_sheet_color(border),
                Ladybird::ChromeStyle::style_sheet_color(chrome.toolbar),
                Ladybird::ChromeStyle::style_sheet_color(focused ? chrome.text : chrome.muted),
                Ladybird::ChromeStyle::style_sheet_color(chrome.icon),
                Ladybird::ChromeStyle::style_sheet_color(chrome.hover)));
    for (auto* button : pane.frame->findChildren<QToolButton*>()) {
        if (auto icon = button->property("superbirdChromeIcon"); icon.isValid())
            button->setIcon(Ladybird::create_chrome_icon(static_cast<Ladybird::ChromeIcon>(icon.toInt()), chrome.icon));
    }
}

bool SplitViewHost::eventFilter(QObject* watched, QEvent* event)
{
    auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::FocusIn)
        return QWidget::eventFilter(watched, event);

    for (auto& it : m_panes) {
        auto& pane = it.value;
        auto matches = (pane.tab && pane.tab->is_loaded() && watched == &pane.tab->view())
            || (watched->property(PANE_PROPERTY).isValid() && watched->property(PANE_PROPERTY).toLongLong() == it.key);
        if (!matches)
            continue;
        if (it.key != m_focused && on_pane_activated) {
            auto pane_id = it.key;
            // NB: Activated after the event is handled, since activating may rebuild the panes.
            QTimer::singleShot(0, this, [this, pane_id] {
                if (on_pane_activated)
                    on_pane_activated(pane_id);
            });
        }
        break;
    }
    return QWidget::eventFilter(watched, event);
}

}
