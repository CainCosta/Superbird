/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Popover.h>

#include <QIcon>
#include <QString>

class QLabel;
class QLineEdit;
class QScrollArea;

namespace Superbird {

class CommandPaletteList;

// The Command Palette's floating sheet: a search field over a list of ranked rows. It reuses the All Tabs popover's
// style sheet (and its object names), so it is drawn with the same Theme::panel() colors in light and dark mode.
class CommandPalettePopover final : public Ladybird::Popover {
public:
    AK_ALLOC_WITH_KMALLOC;

    struct Row {
        QIcon icon;
        QString title;
        QString detail;
        // Indices into `title` to draw emphasized, where the query matched.
        Vector<int> highlights;
        // An open tab's row offers a close button while it is highlighted.
        bool can_close { false };
        bool is_error { false };
    };

    explicit CommandPalettePopover(QWidget* parent);

    void set_rows(Vector<Row>);
    void clear_query();
    QString query() const;
    void update_chrome_style(QPalette const&);

    Function<void(QString const&)> on_query_changed;
    Function<void(size_t)> on_activate;
    Function<void(size_t)> on_close_requested;

private:
    virtual void showEvent(QShowEvent*) override;
    virtual bool eventFilter(QObject*, QEvent*) override;

    void ensure_highlight_visible();

    QLineEdit* m_field { nullptr };
    QScrollArea* m_scroll_area { nullptr };
    CommandPaletteList* m_list { nullptr };
    QLabel* m_hint { nullptr };
};

}
