/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Popover.h>
#include <UI/Qt/Superbird/Core/Confirm.h>
#include <UI/Qt/Theme.h>

#include <QScreen>
#include <QWidget>

namespace Superbird {

static constexpr int CONFIRM_POPOVER_WIDTH = 340;

void ask_to_confirm(QWidget& window, QString const& title, QString const& body, QString const& accept_text, Function<void()> on_accept)
{
    auto* popover = new Ladybird::MessagePopover(&window, CONFIRM_POPOVER_WIDTH, title, body, "Cancel", accept_text);
    popover->on_accept = [popover, on_accept = AK::move(on_accept)] {
        popover->deleteLater();
        if (on_accept)
            on_accept();
    };
    popover->on_dismiss = [popover] { popover->deleteLater(); };
    popover->update_chrome_style(Ladybird::Theme::chrome_palette(window));
    popover->adjustSize();

    // Centered over the window, a little below its top, like the Command Palette.
    auto size = popover->size();
    auto top = qMax(48, window.height() / 8);
    auto top_left = window.mapToGlobal(QPoint((window.width() - size.width()) / 2, top));
    if (auto* screen = window.screen()) {
        auto available = screen->availableGeometry();
        top_left.setX(qBound(available.left(), top_left.x(), qMax(available.left(), available.right() - size.width())));
        top_left.setY(qBound(available.top(), top_left.y(), qMax(available.top(), available.bottom() - size.height())));
    }
    popover->move(top_left);
    popover->set_anchor_point(window.mapToGlobal(QPoint(window.width() / 2, top)));
    popover->show();
}

}
