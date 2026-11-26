/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/kmalloc.h>
#include <UI/Qt/NotificationBar.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>

#include <QPointer>
#include <QString>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Ladybird {
class Tab;
}

namespace Superbird::Passwords {

// Thin per-tab wrapper around Ladybird::NotificationBar for Save / Update credential prompts.
class SaveCredentialBar {
public:
    AK_ALLOC_WITH_KMALLOC;

    SaveCredentialBar() = default;
    ~SaveCredentialBar();

    void show_save_prompt(
        Ladybird::Tab& tab,
        std::string const& host,
        std::string const& username,
        std::vector<ProviderKind> const& target_providers,
        std::function<void(ProviderKind)> on_save,
        std::function<void()> on_never,
        std::function<void()> on_dismiss);

    void show_update_prompt(
        Ladybird::Tab& tab,
        std::string const& host,
        CredentialSummary const& existing,
        std::function<void()> on_update,
        std::function<void()> on_save_as_new,
        std::function<void()> on_dismiss);

    void dismiss();
    bool is_visible() const { return !m_bar.isNull(); }

private:
    void mount(Ladybird::Tab& tab, QString const& message, Vector<Ladybird::NotificationBar::Button> buttons, Function<void()> on_close);

    QPointer<Ladybird::Tab> m_tab;
    QPointer<Ladybird::NotificationBar> m_bar;
};

}
