/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/HashMap.h>
#include <AK/kmalloc.h>

#include <optional>
#include <UI/Qt/Superbird/Modules/SplitView/SplitLayout.h>

#include <QPointer>
#include <QRect>
#include <QWidget>

class QFrame;
class QLabel;
class QSplitter;

namespace Ladybird {
class Tab;
}

namespace Superbird {

// Split View's page host: nested QSplitters that mirror a SplitLayout, each pane a frame holding a borrowed Tab with a
// slim header (title, swap, remove-from-split) and an accent border on the focused pane.
class SplitViewHost final : public QWidget {
public:
    // Where a pane's frame is on screen, if the pane is here.
    std::optional<QRect> pane_global_rect(Split::PaneId) const;

    AK_ALLOC_WITH_KMALLOC;

    explicit SplitViewHost(QWidget* parent = nullptr);
    virtual ~SplitViewHost() override;

    // Rebuilds the panes for the layout. `tab_for` maps panes to the tabs to show (already lent by the TabWidget).
    void rebuild(Split::SplitLayout const&, Function<Ladybird::Tab*(Split::PaneId)> const& tab_for);
    // Gives every tab back (unparented from the panes) so the TabWidget can take them again.
    void release_tabs();
    void set_focused(Split::PaneId);
    void update_titles();
    void update_chrome_style();

    Function<void(Split::PaneId)> on_pane_activated;
    Function<void(Split::PaneId)> on_remove_pane;
    Function<void()> on_swap;
    Function<void()> on_unsplit;
    Function<void(Split::PaneId, std::vector<double>)> on_ratios_changed;

private:
    struct Pane {
        QPointer<QFrame> frame;
        QPointer<QLabel> title;
        QPointer<Ladybird::Tab> tab;
    };

    virtual bool eventFilter(QObject*, QEvent*) override;

    QWidget* build_node(Split::SplitLayout::Node const&, Function<Ladybird::Tab*(Split::PaneId)> const& tab_for);
    QFrame* build_pane(Split::PaneId, Ladybird::Tab&);
    void style_pane(Split::PaneId, Pane const&);

    QPointer<QWidget> m_root;
    HashMap<Split::PaneId, Pane> m_panes;
    Split::PaneId m_focused { 0 };
};

}
