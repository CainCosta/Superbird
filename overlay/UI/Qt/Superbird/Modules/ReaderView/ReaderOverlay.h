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
#include <UI/Qt/Superbird/Modules/ReaderView/ArticleExtractor.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderDocument.h>

#include <QPointer>
#include <QWidget>

#include <string>

class QFrame;
class QToolButton;

namespace Ladybird {
class BrowserWindow;
class FindInPageWidget;
class Tab;
class WebContentView;
}

namespace Superbird {

// Reader View's sheet over one tab: a separate, script-disabled WebContentView showing the sanitized article, with
// floating typography and theme controls. The tab's own page stays loaded, untouched and scrolled where it was,
// underneath; closing the overlay simply reveals it again.
class ReaderOverlay final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    ReaderOverlay(Ladybird::BrowserWindow&, Ladybird::Tab&, Reader::Article, std::string page_url, Reader::ReaderPreferences const&);
    virtual ~ReaderOverlay() override;

    Ladybird::Tab& tab() const { return *m_tab; }
    void set_preferences(Reader::ReaderPreferences const&);
    void update_chrome_style();

    Function<void(Reader::ReaderPreferences const&)> on_preferences_changed;
    Function<void()> on_close_requested;
    // A link in the article was followed; the tab should go there instead.
    Function<void(URL::URL const&)> on_follow_link;

private:
    virtual bool eventFilter(QObject*, QEvent*) override;
    virtual void keyPressEvent(QKeyEvent*) override;
    virtual void resizeEvent(QResizeEvent*) override;

    void render();
    void sync_geometry_to_tab_view();
    void position_controls();
    void update_control_labels();
    void change(Function<void(Reader::ReaderPreferences&)> const&);
    // Zooms the reader by delta steps (0 resets), keeping the reading position.
    void zoom(int delta);
    void apply_zoom_steps(int delta);

    QPointer<Ladybird::Tab> m_tab;
    Ladybird::WebContentView* m_view { nullptr };
    QPointer<Ladybird::FindInPageWidget> m_find_in_page;
    QFrame* m_controls { nullptr };
    QToolButton* m_font_button { nullptr };
    QToolButton* m_smaller_button { nullptr };
    QToolButton* m_larger_button { nullptr };
    QToolButton* m_line_height_button { nullptr };
    QToolButton* m_width_button { nullptr };
    QToolButton* m_theme_button { nullptr };
    QToolButton* m_close_button { nullptr };

    Reader::Article m_article;
    std::string m_page_url;
    Reader::ReaderPreferences m_preferences;
    Optional<u64> m_navigation_listener_id;
};

}
