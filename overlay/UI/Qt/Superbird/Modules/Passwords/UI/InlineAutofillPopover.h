/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>

#include <QListWidget>
#include <QWidget>

#include <functional>
#include <vector>

namespace Superbird::Passwords {

// Lightweight inline autofill dropdown anchored directly below a focused login/password/OTP field.
class InlineAutofillPopover final : public QWidget {
    Q_OBJECT

public:
    explicit InlineAutofillPopover(QWidget* parent = nullptr);

    void show_credentials(
        QPoint global_anchor_bottom_left,
        int min_width,
        std::vector<CredentialSummary> const& credentials,
        bool vaults_locked,
        bool offer_generate_password);

    void update_chrome_style();

    std::function<void(CredentialSummary const&)> on_pick_credential;
    std::function<void()> on_request_unlock;
    std::function<void()> on_request_generate_password;

protected:
    virtual void keyPressEvent(QKeyEvent*) override;

private:
    enum class RowAction : uint8_t {
        Credential,
        Unlock,
        GeneratePassword,
    };
    struct RowEntry {
        RowAction action { RowAction::Credential };
        CredentialSummary credential;
    };

    void activate_current_row();

    QListWidget* m_list { nullptr };
    std::vector<RowEntry> m_entries;
};

}
