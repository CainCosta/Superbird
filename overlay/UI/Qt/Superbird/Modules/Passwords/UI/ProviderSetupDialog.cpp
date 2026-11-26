/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Keyring/Keyring.h>
#include <UI/Qt/Superbird/Modules/Passwords/PasswordManagerService.h>
#include <UI/Qt/Superbird/Modules/Passwords/UI/ProviderSetupDialog.h>
#include <UI/Qt/Theme.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace Superbird::Passwords {

ProviderSetupDialog::ProviderSetupDialog(QWidget* parent, ProviderKind initial_tab)
    : QDialog(parent)
{
    setWindowTitle("Superbird Password Providers");
    setMinimumWidth(460);
    setModal(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(build_bitwarden_tab(), "Bitwarden / Vaultwarden");
    m_tabs->addTab(build_local_vault_tab(), "Local Vault");
    m_tabs->addTab(build_onepassword_tab(), "1Password");

    if (initial_tab == ProviderKind::LocalVault)
        m_tabs->setCurrentIndex(1);
    else if (initial_tab == ProviderKind::OnePassword)
        m_tabs->setCurrentIndex(2);
    else
        m_tabs->setCurrentIndex(0);

    root->addWidget(m_tabs);

    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    auto* close_btn = new QPushButton("Close", this);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(close_btn);
    root->addLayout(footer);

    apply_theme();
    refresh_status_labels();
}

void ProviderSetupDialog::apply_theme()
{
    auto const& chrome = Ladybird::Theme::chrome(*this);
    setStyleSheet(QString(
        "QDialog { background: %1; color: %2; }"
        "QLabel { color: %2; }"
        "QLineEdit, QComboBox { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 6px 8px; }"
        "QPushButton { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 6px 12px; }"
        "QPushButton:hover { border-color: %5; }")
            .arg(chrome.toolbar.name(), chrome.text.name(), chrome.field.name(), chrome.divider.name(), Ladybird::Theme::accent().name()));
}

QWidget* ProviderSetupDialog::build_bitwarden_tab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* form = new QFormLayout;
    form->setSpacing(8);

    m_bw_region = new QComboBox(page);
    m_bw_region->addItem("Bitwarden Cloud (US)", static_cast<int>(BitwardenRegion::US));
    m_bw_region->addItem("Bitwarden Cloud (EU)", static_cast<int>(BitwardenRegion::EU));
    m_bw_region->addItem("Self-Hosted / Vaultwarden", static_cast<int>(BitwardenRegion::Custom));
    form->addRow("Server", m_bw_region);

    m_bw_custom_url = new QLineEdit(page);
    m_bw_custom_url->setPlaceholderText("https://vault.example.com");
    m_bw_custom_url->setVisible(false);
    form->addRow("Server URL", m_bw_custom_url);

    connect(m_bw_region, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_bw_custom_url->setVisible(idx == 2);
    });

    m_bw_auth_mode = new QComboBox(page);
    m_bw_auth_mode->addItem("Email & Master Password", 0);
    m_bw_auth_mode->addItem("API Key + Master Password", 1);
    form->addRow("Sign-in method", m_bw_auth_mode);

    m_bw_email = new QLineEdit(page);
    m_bw_email->setPlaceholderText("you@example.com");
    form->addRow("Email", m_bw_email);

    m_bw_password = new QLineEdit(page);
    m_bw_password->setEchoMode(QLineEdit::Password);
    m_bw_password->setPlaceholderText("Master Password");
    form->addRow("Master Password", m_bw_password);

    layout->addLayout(form);

    m_bw_api_row = new QWidget(page);
    auto* api_form = new QFormLayout(m_bw_api_row);
    api_form->setContentsMargins(0, 0, 0, 0);
    m_bw_client_id = new QLineEdit(m_bw_api_row);
    m_bw_client_id->setPlaceholderText("user.xxxxxxxx-xxxx-...");
    m_bw_client_secret = new QLineEdit(m_bw_api_row);
    m_bw_client_secret->setEchoMode(QLineEdit::Password);
    api_form->addRow("Client ID", m_bw_client_id);
    api_form->addRow("Client Secret", m_bw_client_secret);
    m_bw_api_row->setVisible(false);
    layout->addWidget(m_bw_api_row);

    connect(m_bw_auth_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_bw_api_row->setVisible(idx == 1);
    });

    m_bw_2fa_row = new QWidget(page);
    auto* tfa_form = new QFormLayout(m_bw_2fa_row);
    tfa_form->setContentsMargins(0, 0, 0, 0);
    m_bw_2fa_provider = new QComboBox(m_bw_2fa_row);
    m_bw_2fa_provider->addItem("Authenticator App (TOTP)", 0);
    m_bw_2fa_provider->addItem("Email Code", 1);
    m_bw_2fa_provider->addItem("YubiKey OTP", 3);
    m_bw_2fa_code = new QLineEdit(m_bw_2fa_row);
    m_bw_2fa_code->setPlaceholderText("6-digit verification code");
    tfa_form->addRow("2FA Method", m_bw_2fa_provider);
    tfa_form->addRow("2FA Code", m_bw_2fa_code);
    m_bw_2fa_row->setVisible(false);
    layout->addWidget(m_bw_2fa_row);

    m_bw_new_device_row = new QWidget(page);
    auto* nd_form = new QFormLayout(m_bw_new_device_row);
    nd_form->setContentsMargins(0, 0, 0, 0);
    m_bw_new_device_otp = new QLineEdit(m_bw_new_device_row);
    m_bw_new_device_otp->setPlaceholderText("Email new-device verification code");
    nd_form->addRow("New Device Code", m_bw_new_device_otp);
    m_bw_new_device_row->setVisible(false);
    layout->addWidget(m_bw_new_device_row);

    m_bw_biometric = new QCheckBox("Enable Touch ID / OS Keyring unlock", page);
    m_bw_biometric->setChecked(Keyring::supports_biometrics());
    m_bw_biometric->setEnabled(Keyring::is_available());
    layout->addWidget(m_bw_biometric);

    m_bw_status = new QLabel(page);
    m_bw_status->setWordWrap(true);
    layout->addWidget(m_bw_status);

    auto* buttons = new QHBoxLayout;
    m_bw_signin_btn = new QPushButton("Sign In & Sync", page);
    m_bw_signout_btn = new QPushButton("Sign Out", page);
    buttons->addWidget(m_bw_signin_btn);
    buttons->addWidget(m_bw_signout_btn);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto& bw = PasswordManagerService::the().bitwarden();
    if (bw.is_configured()) {
        m_bw_region->setCurrentIndex(static_cast<int>(bw.region()));
        m_bw_custom_url->setText(QString::fromStdString(bw.custom_server_url()));
        m_bw_email->setText(QString::fromStdString(bw.email()));
    }

    connect(m_bw_signin_btn, &QPushButton::clicked, this, [this] {
        auto& bw = PasswordManagerService::the().bitwarden();
        auto region = static_cast<BitwardenRegion>(m_bw_region->currentData().toInt());
        auto custom_url = m_bw_custom_url->text().trimmed().toStdString();
        auto email = m_bw_email->text().trimmed().toStdString();
        auto pw = m_bw_password->text().toStdString();
        bool bio = m_bw_biometric->isChecked();

        m_bw_signin_btn->setEnabled(false);
        m_bw_status->setText("Signing in and syncing vault…");

        auto handle_result = [this](BitwardenLoginResult result) {
            m_bw_signin_btn->setEnabled(true);
            if (result.ok) {
                m_bw_password->clear();
                m_bw_2fa_code->clear();
                m_bw_new_device_otp->clear();
                m_bw_2fa_row->setVisible(false);
                m_bw_new_device_row->setVisible(false);
                m_bw_status->setText("Signed in and vault synced.");
                refresh_status_labels();
                return;
            }
            if (result.challenge.has_value()) {
                if (result.challenge->is_new_device_verification) {
                    m_bw_new_device_row->setVisible(true);
                    m_bw_new_device_otp->setFocus();
                } else {
                    m_bw_2fa_row->setVisible(true);
                    m_bw_2fa_code->setFocus();
                }
                m_bw_status->setText(QString::fromStdString(result.challenge->message));
                return;
            }
            m_bw_status->setText(QString::fromStdString(result.error));
        };

        if (m_bw_auth_mode->currentData().toInt() == 1) {
            bw.login_with_api_key(
                region,
                custom_url,
                email,
                m_bw_client_id->text().trimmed().toStdString(),
                m_bw_client_secret->text().trimmed().toStdString(),
                pw,
                bio,
                std::move(handle_result));
        } else {
            std::optional<int> tfa_prov;
            std::optional<std::string> tfa_code;
            if (m_bw_2fa_row->isVisible() && !m_bw_2fa_code->text().trimmed().isEmpty()) {
                tfa_prov = m_bw_2fa_provider->currentData().toInt();
                tfa_code = m_bw_2fa_code->text().trimmed().toStdString();
            }
            std::optional<std::string> nd_code;
            if (m_bw_new_device_row->isVisible() && !m_bw_new_device_otp->text().trimmed().isEmpty())
                nd_code = m_bw_new_device_otp->text().trimmed().toStdString();

            bw.login_with_password(
                region,
                custom_url,
                email,
                pw,
                tfa_prov,
                tfa_code ? std::optional<std::string_view>(*tfa_code) : std::nullopt,
                nd_code ? std::optional<std::string_view>(*nd_code) : std::nullopt,
                bio,
                std::move(handle_result));
        }
        cleanse_string(pw);
    });

    connect(m_bw_signout_btn, &QPushButton::clicked, this, [this] {
        PasswordManagerService::the().bitwarden().sign_out();
        m_bw_status->setText("Signed out of Bitwarden and cleared offline cache.");
        refresh_status_labels();
    });

    return page;
}

QWidget* ProviderSetupDialog::build_local_vault_tab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* desc = new QLabel("Encrypted built-in vault stored locally in Passwords.db (Argon2id + AES-256-GCM). Zero plaintext domains or usernames on disk.", page);
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto* form = new QFormLayout;
    m_lv_passphrase = new QLineEdit(page);
    m_lv_passphrase->setEchoMode(QLineEdit::Password);
    m_lv_passphrase->setPlaceholderText("Choose a master passphrase");
    m_lv_confirm = new QLineEdit(page);
    m_lv_confirm->setEchoMode(QLineEdit::Password);
    m_lv_confirm->setPlaceholderText("Confirm master passphrase");
    form->addRow("Master Passphrase", m_lv_passphrase);
    form->addRow("Confirm", m_lv_confirm);
    layout->addLayout(form);

    m_lv_biometric = new QCheckBox("Enable Touch ID / OS Keyring unlock", page);
    m_lv_biometric->setChecked(Keyring::supports_biometrics());
    m_lv_biometric->setEnabled(Keyring::is_available());
    layout->addWidget(m_lv_biometric);

    m_lv_status = new QLabel(page);
    m_lv_status->setWordWrap(true);
    layout->addWidget(m_lv_status);

    auto* row1 = new QHBoxLayout;
    m_lv_setup_btn = new QPushButton("Create Local Vault", page);
    m_lv_reset_btn = new QPushButton("Reset Vault", page);
    row1->addWidget(m_lv_setup_btn);
    row1->addWidget(m_lv_reset_btn);
    row1->addStretch(1);
    layout->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    m_lv_import_btn = new QPushButton("Import CSV (Apple / Chrome / Bitwarden / 1Password)…", page);
    m_lv_export_btn = new QPushButton("Export CSV…", page);
    row2->addWidget(m_lv_import_btn);
    row2->addWidget(m_lv_export_btn);
    layout->addLayout(row2);

    connect(m_lv_setup_btn, &QPushButton::clicked, this, [this] {
        auto p1 = m_lv_passphrase->text().toStdString();
        auto p2 = m_lv_confirm->text().toStdString();
        if (p1 != p2) {
            cleanse_string(p1);
            cleanse_string(p2);
            m_lv_status->setText("Passphrases do not match.");
            return;
        }
        std::string err;
        bool ok = PasswordManagerService::the().local_vault().setup_vault(p1, m_lv_biometric->isChecked(), err);
        cleanse_string(p1);
        cleanse_string(p2);
        m_lv_passphrase->clear();
        m_lv_confirm->clear();
        m_lv_status->setText(ok ? "Local Vault created and unlocked." : QString::fromStdString(err));
        refresh_status_labels();
    });

    connect(m_lv_import_btn, &QPushButton::clicked, this, [this] {
        auto& lv = PasswordManagerService::the().local_vault();
        if (lv.status() != ProviderState::Unlocked) {
            m_lv_status->setText("Unlock Local Vault before importing CSV.");
            return;
        }
        auto path = QFileDialog::getOpenFileName(this, "Import Passwords CSV", {}, "CSV Files (*.csv);;All Files (*)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            m_lv_status->setText("Could not open CSV file.");
            return;
        }
        auto bytes = file.readAll();
        size_t imported = 0;
        auto res = lv.import_csv({ bytes.constData(), static_cast<size_t>(bytes.size()) }, imported);
        if (res.detected_format == CsvFormat::Unknown)
            m_lv_status->setText("Unrecognized CSV header format.");
        else
            m_lv_status->setText(QString("Imported %1 login(s) (%2 skipped).").arg(imported).arg(res.skipped_rows));
        refresh_status_labels();
    });

    connect(m_lv_export_btn, &QPushButton::clicked, this, [this] {
        auto& lv = PasswordManagerService::the().local_vault();
        if (lv.status() != ProviderState::Unlocked) {
            m_lv_status->setText("Unlock Local Vault before exporting CSV.");
            return;
        }
        auto answer = QMessageBox::warning(
            this,
            "Export Unencrypted Passwords",
            "Exporting writes all Local Vault logins as unencrypted plaintext CSV. Continue?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        auto path = QFileDialog::getSaveFileName(this, "Export Passwords CSV", "superbird-passwords.csv", "CSV Files (*.csv)");
        if (path.isEmpty())
            return;
        auto csv = lv.export_csv(CsvFormat::ApplePasswords);
        if (!csv) {
            m_lv_status->setText("Failed to export vault.");
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            cleanse_string(*csv);
            m_lv_status->setText("Could not write CSV file.");
            return;
        }
        file.write(csv->data(), static_cast<qint64>(csv->size()));
        cleanse_string(*csv);
        m_lv_status->setText("Exported Local Vault to CSV.");
    });

    connect(m_lv_reset_btn, &QPushButton::clicked, this, [this] {
        auto answer = QMessageBox::warning(
            this,
            "Reset Local Vault",
            "This permanently deletes all items in Superbird's Local Vault. Continue?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        PasswordManagerService::the().local_vault().reset_vault();
        m_lv_status->setText("Local Vault reset.");
        refresh_status_labels();
    });

    return page;
}

QWidget* ProviderSetupDialog::build_onepassword_tab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* guidance = new QLabel(QString::fromStdString(OnePasswordProvider::setup_guidance()), page);
    guidance->setWordWrap(true);
    layout->addWidget(guidance);

    m_op_status = new QLabel(page);
    m_op_status->setWordWrap(true);
    layout->addWidget(m_op_status);

    m_op_connect_btn = new QPushButton("Connect / Refresh with 1Password Desktop", page);
    connect(m_op_connect_btn, &QPushButton::clicked, this, [this] {
        m_op_connect_btn->setEnabled(false);
        m_op_status->setText("Waiting for 1Password Desktop authorization…");
        PasswordManagerService::the().one_password().unlock({ UnlockMethod::DesktopDelegated, {} }, [this](bool ok, std::string err) {
            m_op_connect_btn->setEnabled(true);
            if (ok) {
                auto count = PasswordManagerService::the().one_password().all_credentials().size();
                m_op_status->setText(QString("Connected to 1Password (%1 login(s) indexed; zero secrets persisted in Superbird).").arg(count));
            } else {
                m_op_status->setText(QString::fromStdString(err));
            }
            refresh_status_labels();
        });
    });
    layout->addWidget(m_op_connect_btn);
    layout->addStretch(1);

    return page;
}

void ProviderSetupDialog::refresh_status_labels()
{
    auto& svc = PasswordManagerService::the();

    auto& bw = svc.bitwarden();
    m_bw_signout_btn->setEnabled(bw.is_configured());
    if (bw.status() == ProviderState::Unlocked)
        m_bw_status->setText(QString("Unlocked: %1 (%2 login(s)).").arg(QString::fromStdString(bw.email())).arg(bw.all_credentials().size()));
    else if (bw.is_configured() && m_bw_status->text().isEmpty())
        m_bw_status->setText(QString("Configured for %1 (locked).").arg(QString::fromStdString(bw.email())));

    auto& lv = svc.local_vault();
    m_lv_reset_btn->setEnabled(lv.is_configured());
    m_lv_import_btn->setEnabled(lv.status() == ProviderState::Unlocked);
    m_lv_export_btn->setEnabled(lv.status() == ProviderState::Unlocked);
    if (lv.status() == ProviderState::Unlocked)
        m_lv_status->setText(QString("Local Vault unlocked (%1 login(s)).").arg(lv.all_credentials().size()));
    else if (lv.is_configured() && m_lv_status->text().isEmpty())
        m_lv_status->setText("Local Vault is configured (locked).");

    auto op_bin = OnePasswordProvider::find_op_binary();
    if (op_bin.isEmpty()) {
        m_op_status->setText("1Password CLI (`op`) was not found on PATH or Homebrew locations.");
        m_op_connect_btn->setEnabled(false);
    } else if (svc.one_password().status() == ProviderState::Unlocked) {
        m_op_status->setText(QString("Connected via %1 (%2 login(s) indexed).").arg(op_bin).arg(svc.one_password().all_credentials().size()));
        m_op_connect_btn->setEnabled(true);
    } else {
        m_op_status->setText(QString("Found 1Password CLI at %1. Click below to authorize with 1Password Desktop.").arg(op_bin));
        m_op_connect_btn->setEnabled(true);
    }
}

}
