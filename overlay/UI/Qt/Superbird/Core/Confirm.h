/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>

#include <QString>

class QWidget;

namespace Superbird {

// Asks the user to confirm an action in the chrome's own message popover, which drops from the top of the window like a
// sheet, instead of a platform dialog. `on_accept` runs only if they accept; dismissing does nothing.
void ask_to_confirm(QWidget& window, QString const& title, QString const& body, QString const& accept_text, Function<void()> on_accept);

}
