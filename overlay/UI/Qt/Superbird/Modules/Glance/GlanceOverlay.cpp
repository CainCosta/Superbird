/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/ChromeStyle.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/Superbird/Modules/Glance/GlanceModel.h>
#include <UI/Qt/Superbird/Modules/Glance/GlanceOverlay.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/Theme.h>
#include <UI/Qt/WebContentView.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace Superbird {

static constexpr int STRIP_HEIGHT = 36;
static constexpr int SHEET_RADIUS = 12;

GlanceOverlay::GlanceOverlay(Ladybird::BrowserWindow& window, Ladybird::Tab& over)
    : QWidget(&over)
    , m_tab(&over)
{
    setAccessibleName("Glance");
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    m_sheet = new QFrame(this);
    m_sheet->setObjectName("SuperbirdGlanceSheet");
#if defined(AK_OS_MACOS)
    // NB: The tab's page is a layer above whatever Qt paints over it, and a native view can't let it show through. So,
    //     as with Ladybird's JavaScript dialogs, only the sheet is a native view, and the page view dims the page itself.
    m_sheet->setAttribute(Qt::WA_DontCreateNativeAncestors);
    m_sheet->setAttribute(Qt::WA_NativeWindow);
#endif
    auto* sheet_layout = new QVBoxLayout(m_sheet);
    sheet_layout->setContentsMargins(1, 0, 1, 1);
    sheet_layout->setSpacing(0);

    auto* strip = new QWidget(m_sheet);
    strip->setObjectName("SuperbirdGlanceStrip");
    strip->setFixedHeight(STRIP_HEIGHT);
    auto* strip_layout = new QHBoxLayout(strip);
    strip_layout->setContentsMargins(14, 0, 8, 0);
    strip_layout->setSpacing(6);
    m_title = new QLabel(strip);
    m_title->setObjectName("SuperbirdGlanceTitle");
    m_host = new QLabel(strip);
    m_host->setObjectName("SuperbirdGlanceHost");
    strip_layout->addWidget(m_title, 1);
    strip_layout->addWidget(m_host);

    auto make_button = [&](QString const& text, QString const& tooltip) {
        auto* button = new QToolButton(strip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setText(text);
        button->setToolTip(tooltip);
        button->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        strip_layout->addWidget(button);
        return button;
    };
    m_back = make_button({}, "Back");
    m_reload = make_button({}, "Reload");
    m_open_as_tab = make_button("Open as Tab", "Open this page as a tab");
    m_copy_url = make_button("Copy URL", "Copy this page's address");
    m_close = make_button({}, "Close (Esc)");
    QObject::connect(m_back, &QToolButton::clicked, this, [this] { m_view->traverse_the_history_by_delta(-1); });
    QObject::connect(m_reload, &QToolButton::clicked, this, [this] { m_view->reload(); });
    QObject::connect(m_open_as_tab, &QToolButton::clicked, this, [this] {
        if (on_promote)
            on_promote();
    });
    QObject::connect(m_copy_url, &QToolButton::clicked, this, [this] {
        if (on_copy_url)
            on_copy_url();
    });
    QObject::connect(m_close, &QToolButton::clicked, this, [this] {
        if (on_dismiss)
            on_dismiss();
    });

    auto initial_state = Ladybird::WebContentViewInitialState {
        .is_private = window.is_private(),
        .maximum_frames_per_second = window.refresh_rate(),
        .display_id = window.display_id(),
        .screen_shows_high_dynamic_range = window.screen_shows_high_dynamic_range(),
        .owner_view_id = {},
        // Not a tab: what the page opens stays out of the window, and navigation stays inside the sheet.
        .is_tab = false,
    };
    m_view = new Ladybird::WebContentView(m_sheet, {}, initial_state);
    m_view->set_window_position({ window.x(), window.y() });
    m_view->set_window_size({ window.width(), window.height() });
    m_view->on_title_change = [this](auto const& title) {
        if (on_title_changed)
            on_title_changed(qstring_from_utf16_string(title));
    };
    m_view->on_state_change = [this](auto const&) { m_back->setEnabled(m_view->can_navigate_back()); };
    m_back->setEnabled(false);
    m_navigation_listener_id = m_view->add_navigation_listener({
        .on_load_start = [this](URL::URL const& url) {
            if (on_navigated)
                on_navigated(url);
        },
        .on_load_finish = {},
    });
    sheet_layout->addWidget(strip);
    sheet_layout->addWidget(m_view, 1);

    // The dimmed backdrop and the sheet float over the tab's page.
    if (over.is_loaded()) {
        over.view().add_floating_surface(*this);
        // NB: Where the page is a layer of its own (macOS, Wayland), clicks on the dimmed page go to the page view.
        over.view().installEventFilter(this);
        over.view().set_page_dim(1);
    }
    update_chrome_style();
    sync_geometry();
    raise();
    show();
    m_view->setFocus(Qt::OtherFocusReason);
}

GlanceOverlay::~GlanceOverlay()
{
    if (m_navigation_listener_id.has_value())
        m_view->remove_navigation_listener(*m_navigation_listener_id);
    if (m_tab && m_tab->is_loaded()) {
        m_tab->view().removeEventFilter(this);
        m_tab->view().remove_floating_surface(*this);
        m_tab->view().set_page_dim(0);
    }
}

void GlanceOverlay::load(URL::URL const& url)
{
    m_view->load(url);
}

void GlanceOverlay::set_strip_text(QString const& title, QString const& host)
{
    m_title->setText(m_title->fontMetrics().elidedText(title, Qt::ElideRight, max(80, m_title->width())));
    m_host->setText(host == title ? QString {} : host);
}

void GlanceOverlay::update_chrome_style()
{
    auto const& panel = Ladybird::Theme::panel();
    m_sheet->setStyleSheet(QString(
        "QFrame#SuperbirdGlanceSheet { background: %1; border: 1px solid %2; border-radius: %3px; }"
        "QWidget#SuperbirdGlanceStrip { background: transparent; }"
        "QLabel#SuperbirdGlanceTitle { color: %4; font-weight: 600; }"
        "QLabel#SuperbirdGlanceHost { color: %5; }"
        "QWidget#SuperbirdGlanceStrip QToolButton { color: %4; border: none; border-radius: 6px; padding: 3px 8px; }"
        "QWidget#SuperbirdGlanceStrip QToolButton:hover { background: %6; }")
                               .arg(Ladybird::ChromeStyle::style_sheet_color(panel.surface),
                                   Ladybird::ChromeStyle::style_sheet_color(panel.edge))
                               .arg(SHEET_RADIUS)
                               .arg(Ladybird::ChromeStyle::style_sheet_color(panel.text),
                                   Ladybird::ChromeStyle::style_sheet_color(panel.muted),
                                   Ladybird::ChromeStyle::style_sheet_color(panel.control)));
    m_open_as_tab->setIcon(Ladybird::create_chrome_icon(Ladybird::ChromeIcon::ExternalLink, panel.body));
    m_back->setIcon(Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Back, panel.body));
    m_reload->setIcon(Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Reload, panel.body));
    m_close->setIcon(Ladybird::create_chrome_icon(Ladybird::ChromeIcon::Close, panel.body));
    update();
}

QRect GlanceOverlay::sheet_geometry() const
{
    auto rect = Glance::sheet_rect({ 0, 0, width(), height() });
    return { rect.x, rect.y, rect.width, rect.height };
}

void GlanceOverlay::sync_geometry()
{
    if (!m_tab || !m_tab->is_loaded())
        return;
    auto& view = m_tab->view();
    setGeometry(QRect(view.mapTo(m_tab.data(), QPoint(0, 0)), view.size()));
}

void GlanceOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_tab && m_tab->is_loaded())
        m_tab->view().set_page_dim(1);
}

void GlanceOverlay::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (m_tab && m_tab->is_loaded())
        m_tab->view().set_page_dim(0);
}

void GlanceOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_sheet->setGeometry(sheet_geometry());
}

void GlanceOverlay::paintEvent(QPaintEvent*)
{
#if !defined(AK_OS_MACOS)
    // NB: On macOS the page view dims itself (see set_page_dim()); anything painted here would lie under the page.
    QPainter painter(this);
    auto dim = QColor(0, 0, 0);
    dim.setAlphaF(Ladybird::Theme::is_dark() ? 0.5 : 0.3);
    painter.fillRect(rect(), dim);
#endif
}

void GlanceOverlay::mousePressEvent(QMouseEvent* event)
{
    if (!m_sheet->geometry().contains(event->position().toPoint()) && on_dismiss) {
        on_dismiss();
        return;
    }
    QWidget::mousePressEvent(event);
}

// Keys the page in the sheet doesn't take come back up here.
void GlanceOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier && on_dismiss) {
        on_dismiss();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool GlanceOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (m_tab && m_tab->is_loaded() && watched == &m_tab->view()) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move)
            sync_geometry();
        // A click on the dimmed page closes the Glance, and the page doesn't see it.
        if (event->type() == QEvent::MouseButtonPress && isVisible()) {
            auto position = m_tab->view().mapTo(this, static_cast<QMouseEvent*>(event)->position().toPoint());
            if (!m_sheet->geometry().contains(position) && on_dismiss) {
                on_dismiss();
                return true;
            }
        }
        if ((event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick) && isVisible())
            return true;
    }
    return QWidget::eventFilter(watched, event);
}

}
