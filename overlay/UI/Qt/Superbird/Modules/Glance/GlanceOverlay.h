/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/Optional.h>
#include <AK/kmalloc.h>
#include <LibURL/URL.h>

#include <QPointer>
#include <QWidget>

class QFrame;
class QLabel;
class QToolButton;

namespace Ladybird {
class BrowserWindow;
class Tab;
class WebContentView;
}

namespace Superbird {

// Glance's floating sheet over a tab: the page dims behind a rounded sheet (~80% of it) that holds its own
// WebContentView, with a strip showing the page's title and site, "Open as Tab", "Copy URL" and "Close". Esc or a click
// on the dimmed page closes it.
class GlanceOverlay final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    GlanceOverlay(Ladybird::BrowserWindow&, Ladybird::Tab& over);
    virtual ~GlanceOverlay() override;

    void load(URL::URL const&);
    void set_strip_text(QString const& title, QString const& host);
    void update_chrome_style();
    Ladybird::Tab* tab() const { return m_tab; }

    Function<void(URL::URL const&)> on_navigated;
    Function<void(QString const&)> on_title_changed;
    Function<void()> on_promote;
    Function<void()> on_copy_url;
    Function<void()> on_dismiss;

private:
    virtual void paintEvent(QPaintEvent*) override;
    virtual void mousePressEvent(QMouseEvent*) override;
    virtual void keyPressEvent(QKeyEvent*) override;
    virtual void resizeEvent(QResizeEvent*) override;
    virtual bool eventFilter(QObject*, QEvent*) override;

    void sync_geometry();
    QRect sheet_geometry() const;

    QPointer<Ladybird::Tab> m_tab;
    QFrame* m_sheet { nullptr };
    QLabel* m_title { nullptr };
    QLabel* m_host { nullptr };
    QToolButton* m_open_as_tab { nullptr };
    QToolButton* m_copy_url { nullptr };
    QToolButton* m_close { nullptr };
    Ladybird::WebContentView* m_view { nullptr };
    Optional<u64> m_navigation_listener_id;
};

}
