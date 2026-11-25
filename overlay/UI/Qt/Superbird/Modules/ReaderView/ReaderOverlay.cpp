/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/ChromeStyle.h>
#include <UI/Qt/HubConnection.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/Superbird/Modules/ReaderView/ReaderOverlay.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/Theme.h>
#include <UI/Qt/WebContentView.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QToolButton>
#include <QVBoxLayout>

namespace Superbird {

static constexpr int CONTROLS_MARGIN = 12;

ReaderOverlay::ReaderOverlay(Ladybird::BrowserWindow& window, Ladybird::Tab& tab, Reader::Article article, std::string page_url, Reader::ReaderPreferences const& preferences)
    : QWidget(&tab)
    , m_tab(&tab)
    , m_article(AK::move(article))
    , m_page_url(AK::move(page_url))
    , m_preferences(preferences)
{
    setAccessibleName("Reader View");

    auto initial_state = Ladybird::WebContentViewInitialState {
        .is_private = window.is_private(),
        .maximum_frames_per_second = window.refresh_rate(),
        .display_id = window.display_id(),
        .screen_shows_high_dynamic_range = window.screen_shows_high_dynamic_range(),
        .owner_view_id = {},
        // Not a tab: the reader page can't open windows or tabs, or ask the user anything.
        .is_tab = false,
    };
    m_view = new Ladybird::WebContentView(this, {}, initial_state);
    m_view->set_window_position({ window.x(), window.y() });
    m_view->set_window_size({ window.width(), window.height() });

    // No script ever runs in Reader View, on whichever page the view uses. The article is also sanitized and carries
    // a Content-Security-Policy that forbids scripts; this is the third wall.
    Ladybird::HubConnection::the().async_view_debug_request(m_view->view_id(), "scripting"sv, "off"sv);

    m_navigation_listener_id = m_view->add_navigation_listener({
        .on_load_start = [this](URL::URL const& url) {
            // The reader's own document is about:srcdoc; a web URL means a link in the article was followed.
            if (url.scheme() == "http"sv || url.scheme() == "https"sv) {
                m_view->stop_loading();
                if (on_follow_link)
                    on_follow_link(url);
            }
        },
        .on_load_finish = {},
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view);

    // The floating controls, top right over the article.
    m_controls = new QFrame(this);
    m_controls->setObjectName("SuperbirdReaderControls");
    auto* controls_layout = new QHBoxLayout(m_controls);
    controls_layout->setContentsMargins(6, 4, 6, 4);
    controls_layout->setSpacing(2);

    auto make_button = [&](QString const& tooltip, auto on_click) {
        auto* button = new QToolButton(m_controls);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::ArrowCursor);
        button->setToolTip(tooltip);
        QObject::connect(button, &QToolButton::clicked, this, on_click);
        controls_layout->addWidget(button);
        return button;
    };

    m_font_button = make_button("Font", [this] {
        change([](auto& preferences) {
            preferences.font = preferences.font == Reader::ReaderFont::Serif ? Reader::ReaderFont::Sans
                : preferences.font == Reader::ReaderFont::Sans               ? Reader::ReaderFont::Mono
                                                                             : Reader::ReaderFont::Serif;
        });
    });
    m_smaller_button = make_button("Smaller text", [this] { change([](auto& preferences) { preferences.decrease_font_size(); }); });
    m_larger_button = make_button("Larger text", [this] { change([](auto& preferences) { preferences.increase_font_size(); }); });
    m_line_height_button = make_button("Line spacing", [this] { change([](auto& preferences) { preferences.cycle_line_height(); }); });
    m_width_button = make_button("Column width", [this] {
        change([](auto& preferences) {
            preferences.width = preferences.width == Reader::ReaderWidth::Narrow ? Reader::ReaderWidth::Medium
                : preferences.width == Reader::ReaderWidth::Medium               ? Reader::ReaderWidth::Wide
                                                                                 : Reader::ReaderWidth::Narrow;
        });
    });
    m_theme_button = make_button("Theme", [this] {
        change([](auto& preferences) {
            preferences.theme = preferences.theme == Reader::ReaderTheme::System ? Reader::ReaderTheme::Light
                : preferences.theme == Reader::ReaderTheme::Light                ? Reader::ReaderTheme::Sepia
                : preferences.theme == Reader::ReaderTheme::Sepia                ? Reader::ReaderTheme::Dark
                                                                                 : Reader::ReaderTheme::System;
        });
    });
    m_close_button = make_button("Close Reader View (Esc)", [this] {
        if (on_close_requested)
            on_close_requested();
    });

    m_view->add_floating_surface(*m_controls);

    // The overlay covers exactly the tab's page.
    tab.view().installEventFilter(this);
    sync_geometry_to_tab_view();
    update_chrome_style();
    update_control_labels();
    raise();
    show();
    m_controls->raise();
    render();
    m_view->setFocus(Qt::OtherFocusReason);
}

ReaderOverlay::~ReaderOverlay()
{
    if (m_tab && m_tab->is_loaded())
        m_tab->view().removeEventFilter(this);
    if (m_navigation_listener_id.has_value())
        m_view->remove_navigation_listener(*m_navigation_listener_id);
}

void ReaderOverlay::set_preferences(Reader::ReaderPreferences const& preferences)
{
    if (m_preferences == preferences)
        return;
    m_preferences = preferences;
    update_control_labels();
    render();
}

void ReaderOverlay::change(Function<void(Reader::ReaderPreferences&)> const& mutate)
{
    auto preferences = m_preferences;
    mutate(preferences);
    set_preferences(preferences);
    if (on_preferences_changed)
        on_preferences_changed(m_preferences);
}

void ReaderOverlay::render()
{
    auto html = Reader::build_reader_document(m_article, m_page_url, m_preferences);
    // NB: Said again before each load, in case this one ends up on a page the view hasn't used yet.
    Ladybird::HubConnection::the().async_view_debug_request(m_view->view_id(), "scripting"sv, "off"sv);
    m_view->load_html(StringView { html.data(), html.size() });
}

void ReaderOverlay::update_control_labels()
{
    switch (m_preferences.font) {
    case Reader::ReaderFont::Serif:
        m_font_button->setText("Serif");
        break;
    case Reader::ReaderFont::Sans:
        m_font_button->setText("Sans");
        break;
    case Reader::ReaderFont::Mono:
        m_font_button->setText("Mono");
        break;
    }
    m_smaller_button->setText("A\xE2\x88\x92");
    m_smaller_button->setEnabled(m_preferences.can_decrease_font_size());
    m_larger_button->setText("A+");
    m_larger_button->setEnabled(m_preferences.can_increase_font_size());
    m_line_height_button->setText(QString("\xE2\x86\x95 %1.%2").arg(m_preferences.line_height / 10).arg(m_preferences.line_height % 10));
    switch (m_preferences.width) {
    case Reader::ReaderWidth::Narrow:
        m_width_button->setText("Narrow");
        break;
    case Reader::ReaderWidth::Medium:
        m_width_button->setText("Medium");
        break;
    case Reader::ReaderWidth::Wide:
        m_width_button->setText("Wide");
        break;
    }
    switch (m_preferences.theme) {
    case Reader::ReaderTheme::System:
        m_theme_button->setText("Auto");
        break;
    case Reader::ReaderTheme::Light:
        m_theme_button->setText("Light");
        break;
    case Reader::ReaderTheme::Sepia:
        m_theme_button->setText("Sepia");
        break;
    case Reader::ReaderTheme::Dark:
        m_theme_button->setText("Dark");
        break;
    }
    m_controls->adjustSize();
    position_controls();
}

void ReaderOverlay::update_chrome_style()
{
    auto const& panel = Ladybird::Theme::panel();
    auto style = QString(
        "QFrame#SuperbirdReaderControls { background: %1; border: 1px solid %2; border-radius: 10px; }"
        "QFrame#SuperbirdReaderControls QToolButton { color: %3; background: transparent; border: none; border-radius: 6px; padding: 3px 7px; font-size: 12px; }"
        "QFrame#SuperbirdReaderControls QToolButton:hover { background: %4; }"
        "QFrame#SuperbirdReaderControls QToolButton:pressed { background: %5; }"
        "QFrame#SuperbirdReaderControls QToolButton:disabled { color: %6; }")
                     .arg(Ladybird::ChromeStyle::style_sheet_color(panel.surface),
                         Ladybird::ChromeStyle::style_sheet_color(panel.edge),
                         Ladybird::ChromeStyle::style_sheet_color(panel.text),
                         Ladybird::ChromeStyle::style_sheet_color(panel.control),
                         Ladybird::ChromeStyle::style_sheet_color(panel.pressed),
                         Ladybird::ChromeStyle::style_sheet_color(panel.muted));
    m_controls->setStyleSheet(style);
    m_close_button->setIcon(Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Close, panel.body));
    m_controls->adjustSize();
    position_controls();
}

void ReaderOverlay::sync_geometry_to_tab_view()
{
    if (!m_tab || !m_tab->is_loaded())
        return;
    auto& view = m_tab->view();
    setGeometry(QRect(view.mapTo(m_tab.data(), QPoint(0, 0)), view.size()));
}

void ReaderOverlay::position_controls()
{
    if (!m_controls)
        return;
    m_controls->move(width() - m_controls->width() - CONTROLS_MARGIN, CONTROLS_MARGIN);
    m_controls->raise();
}

void ReaderOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    position_controls();
}

bool ReaderOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (m_tab && m_tab->is_loaded() && watched == &m_tab->view() && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        sync_geometry_to_tab_view();
    return QWidget::eventFilter(watched, event);
}

// Keys the article doesn't use (it has no scripts) come back to the overlay.
void ReaderOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        if (on_close_requested)
            on_close_requested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

}
