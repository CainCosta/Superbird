/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QWidget;

namespace Superbird::Passwords {

class ProviderSetupDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ProviderSetupDialog(QWidget* parent = nullptr, ProviderKind initial_tab = ProviderKind::Bitwarden);

private:
    QWidget* build_bitwarden_tab();
    QWidget* build_local_vault_tab();
    QWidget* build_onepassword_tab();
    void apply_theme();
    void refresh_status_labels();

    QTabWidget* m_tabs { nullptr };

    // Bitwarden tab widgets
    QComboBox* m_bw_region { nullptr };
    QLineEdit* m_bw_custom_url { nullptr };
    QComboBox* m_bw_auth_mode { nullptr };
    QLineEdit* m_bw_email { nullptr };
    QLineEdit* m_bw_password { nullptr };
    QLineEdit* m_bw_client_id { nullptr };
    QLineEdit* m_bw_client_secret { nullptr };
    QWidget* m_bw_api_row { nullptr };
    QWidget* m_bw_2fa_row { nullptr };
    QComboBox* m_bw_2fa_provider { nullptr };
    QLineEdit* m_bw_2fa_code { nullptr };
    QWidget* m_bw_new_device_row { nullptr };
    QLineEdit* m_bw_new_device_otp { nullptr };
    QCheckBox* m_bw_biometric { nullptr };
    QLabel* m_bw_status { nullptr };
    QPushButton* m_bw_signin_btn { nullptr };
    QPushButton* m_bw_signout_btn { nullptr };

    // Local Vault tab widgets
    QLineEdit* m_lv_passphrase { nullptr };
    QLineEdit* m_lv_confirm { nullptr };
    QCheckBox* m_lv_biometric { nullptr };
    QLabel* m_lv_status { nullptr };
    QPushButton* m_lv_setup_btn { nullptr };
    QPushButton* m_lv_import_btn { nullptr };
    QPushButton* m_lv_export_btn { nullptr };
    QPushButton* m_lv_reset_btn { nullptr };

    // 1Password tab widgets
    QLabel* m_op_status { nullptr };
    QPushButton* m_op_connect_btn { nullptr };
};

}
