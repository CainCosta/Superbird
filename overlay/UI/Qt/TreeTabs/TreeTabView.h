/*
 * Copyright (c) 2026, Sidelady contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <UI/Qt/TreeTabs/TreeTabModel.h>

#include <QAbstractScrollArea>
#include <QPoint>
#include <QPointer>
#include <QRect>

class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QTimer;
class QWheelEvent;

namespace Ladybird {

class Tab;
class TreeTabController;

// Native Qt6 tree-style sidebar widget styled directly with Ladybird::Theme and ChromeStyle.
class TreeTabView final : public QAbstractScrollArea {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    explicit TreeTabView(TreeTabController& controller, QWidget* parent = nullptr);
    virtual ~TreeTabView() override = default;

    void rebuild_rows();
    void update_chrome_style();
    void set_collapsed_rail(bool collapsed_rail);
    void ensure_tab_visible(Tab const* tab);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void resizeEvent(QResizeEvent*) override;
    virtual void mousePressEvent(QMouseEvent*) override;
    virtual void mouseMoveEvent(QMouseEvent*) override;
    virtual void mouseReleaseEvent(QMouseEvent*) override;
    virtual void mouseDoubleClickEvent(QMouseEvent*) override;
    virtual void leaveEvent(QEvent*) override;
    virtual void contextMenuEvent(QContextMenuEvent*) override;
    virtual void keyPressEvent(QKeyEvent*) override;
    virtual void wheelEvent(QWheelEvent*) override;

private:
    struct PinnedLayout {
        Vector<Tab*> tabs;
        int columns { 4 };
        int tile_width { 0 };
        int total_height { 0 };
    };

    enum class RowHitPart : u8 {
        None,
        Row,
        Twisty,
        AudioButton,
        CloseButton,
    };

    struct HitResult {
        bool is_pinned { false };
        int index { -1 }; // pinned index or visible row index
        RowHitPart part { RowHitPart::None };
    };

    PinnedLayout compute_pinned_layout() const;
    QRect pinned_tile_rect(PinnedLayout const&, int index) const;
    QRect tree_row_rect(int row_index, int pinned_height) const;
    QRect twisty_rect_for_row(QRect const& row_rect, size_t depth) const;
    QRect close_button_rect_for_row(QRect const& row_rect) const;
    QRect audio_button_rect_for_row(QRect const& row_rect, bool has_close_button) const;

    HitResult hit_test(QPoint const& viewport_pos) const;
    void update_scrollbars();
    void update_drop_target(QPoint const& viewport_pos);
    void clear_drag_state();
    void update_loading_timer();

    void paint_pinned_region(QPainter&, PinnedLayout const&, QPalette const&);
    void paint_tree_row(QPainter&, int row_index, QRect const& row_rect, QPalette const&);
    void paint_drop_indicator(QPainter&, int pinned_height);

    TreeTabController& m_controller;
    Vector<VisibleTreeRow> m_rows;
    bool m_collapsed_rail { false };

    HitResult m_hover {};
    Optional<TreeNodeId> m_pressed_node_id;
    QPointer<Tab> m_pressed_pinned_tab;
    QPoint m_press_pos;
    bool m_dragging { false };
    bool m_drag_include_subtree { true };
    Optional<int> m_drop_row_index;
    TreeDropPosition m_drop_position { TreeDropPosition::Below };

    QTimer* m_auto_expand_timer { nullptr };
    Optional<TreeNodeId> m_auto_expand_node_id;
    QTimer* m_loading_repaint_timer { nullptr };
};

}
