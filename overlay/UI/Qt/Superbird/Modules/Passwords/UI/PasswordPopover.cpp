/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/ChromeStyle.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordGenerator.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/Totp.h>
#include <UI/Qt/Superbird/Modules/Passwords/PasswordManagerService.h>
#include <UI/Qt/Superbird/Modules/Passwords/UI/PasswordPopover.h>
#include <UI/Qt/Theme.h>

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <chrono>

namespace Superbird::Passwords {

namespace {

constexpr int POPOVER_WIDTH = 368;
constexpr int MAX_LIST_HEIGHT = 320;

QString provider_badge_text(ProviderKind kind)
{
    switch (kind) {
    case ProviderKind::Bitwarden:
        return "BW";
    case ProviderKind::LocalVault:
        return "Local";
    case ProviderKind::OnePassword:
        return "1P";
    }
    return {};
}

int64_t now_epoch_seconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

PasswordPopover::PasswordPopover(QWidget* parent)
    : Ladybird::Popover(parent)
    , m_totp_timer(new QTimer(this))
{
    setAccessibleName("Passwords");
    card().setObjectName("LadybirdMessagePopover");
    card().setFixedWidth(POPOVER_WIDTH);

    auto* root = new QVBoxLayout(&card());
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    // Header row
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);

    auto* title = new QLabel("Passwords", &card());
    title->setObjectName("LadybirdMessagePopoverTitle");
    header->addWidget(title);
    header->addStretch(1);

    m_sync_btn = new QPushButton("Sync", &card());
    m_sync_btn->setObjectName("SuperbirdPwSmallBtn");
    m_sync_btn->setCursor(Qt::PointingHandCursor);
    connect(m_sync_btn, &QPushButton::clicked, this, [this] {
        PasswordManagerService::the().sync_all();
        m_feedback_label->setText("Syncing vaults…");
    });
    header->addWidget(m_sync_btn);

    m_lock_btn = new QPushButton("Lock", &card());
    m_lock_btn->setObjectName("SuperbirdPwSmallBtn");
    m_lock_btn->setCursor(Qt::PointingHandCursor);
    connect(m_lock_btn, &QPushButton::clicked, this, [this] {
        PasswordManagerService::the().lock_all();
        refresh();
    });
    header->addWidget(m_lock_btn);

    m_setup_btn = new QPushButton("Providers", &card());
    m_setup_btn->setObjectName("SuperbirdPwSmallBtn");
    m_setup_btn->setCursor(Qt::PointingHandCursor);
    connect(m_setup_btn, &QPushButton::clicked, this, [this] {
        hide();
        if (on_open_setup)
            on_open_setup(ProviderKind::Bitwarden);
    });
    header->addWidget(m_setup_btn);

    root->addLayout(header);

    // Mode switch row (Logins | Generator)
    auto* mode_row = new QHBoxLayout;
    mode_row->setContentsMargins(0, 0, 0, 0);
    mode_row->setSpacing(6);

    m_tab_logins_btn = new QPushButton("Vault", &card());
    m_tab_logins_btn->setCheckable(true);
    m_tab_logins_btn->setChecked(true);
    m_tab_logins_btn->setObjectName("SuperbirdPwTabBtn");
    m_tab_logins_btn->setCursor(Qt::PointingHandCursor);

    m_tab_gen_btn = new QPushButton("Generator", &card());
    m_tab_gen_btn->setCheckable(true);
    m_tab_gen_btn->setObjectName("SuperbirdPwTabBtn");
    m_tab_gen_btn->setCursor(Qt::PointingHandCursor);

    connect(m_tab_logins_btn, &QPushButton::clicked, this, [this] {
        m_tab_logins_btn->setChecked(true);
        m_tab_gen_btn->setChecked(false);
        m_stack->setCurrentIndex(0);
    });
    connect(m_tab_gen_btn, &QPushButton::clicked, this, [this] {
        show_generator_tab();
    });

    mode_row->addWidget(m_tab_logins_btn, 1);
    mode_row->addWidget(m_tab_gen_btn, 1);
    root->addLayout(mode_row);

    m_stack = new QStackedWidget(&card());
    m_stack->addWidget(build_logins_page());
    m_stack->addWidget(build_generator_page());
    root->addWidget(m_stack);

    m_feedback_label = new QLabel(&card());
    m_feedback_label->setObjectName("SuperbirdPwFeedback");
    m_feedback_label->setWordWrap(true);
    m_feedback_label->hide();
    root->addWidget(m_feedback_label);

    m_totp_timer->setInterval(1000);
    connect(m_totp_timer, &QTimer::timeout, this, [this] { update_totp_countdowns(); });

    connect(&PasswordManagerService::the(), &PasswordManagerService::stateChanged, this, [this] {
        if (isVisible())
            refresh();
    });

    update_chrome_style();
}

QWidget* PasswordPopover::build_logins_page()
{
    auto* page = new QWidget(&card());
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_search_edit = new QLineEdit(page);
    m_search_edit->setObjectName("SuperbirdPwSearch");
    m_search_edit->setPlaceholderText("Search logins across unlocked vaults…");
    m_search_edit->setClearButtonEnabled(true);
    m_search_edit->setAttribute(Qt::WA_MacShowFocusRect, false);
    connect(m_search_edit, &QLineEdit::textChanged, this, [this] { rebuild_logins_content(); });
    layout->addWidget(m_search_edit);

    m_scroll_area = new QScrollArea(page);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setMaximumHeight(MAX_LIST_HEIGHT);
    m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_list_container = new QWidget(m_scroll_area);
    m_list_layout = new QVBoxLayout(m_list_container);
    m_list_layout->setContentsMargins(0, 0, 0, 0);
    m_list_layout->setSpacing(6);
    m_scroll_area->setWidget(m_list_container);

    layout->addWidget(m_scroll_area);
    return page;
}

QWidget* PasswordPopover::build_generator_page()
{
    auto* page = new QWidget(&card());
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_gen_output = new QLineEdit(page);
    m_gen_output->setReadOnly(true);
    m_gen_output->setObjectName("SuperbirdPwGenOutput");
    layout->addWidget(m_gen_output);

    auto* strength_row = new QHBoxLayout;
    m_gen_strength = new QLabel(page);
    m_gen_strength->setObjectName("SuperbirdPwSub");
    strength_row->addWidget(m_gen_strength);
    strength_row->addStretch(1);

    auto* regen_btn = new QPushButton("Regenerate", page);
    regen_btn->setObjectName("SuperbirdPwSmallBtn");
    connect(regen_btn, &QPushButton::clicked, this, [this] { regenerate_password(); });
    strength_row->addWidget(regen_btn);

    auto* copy_btn = new QPushButton("Copy", page);
    copy_btn->setObjectName("SuperbirdPwSmallBtn");
    connect(copy_btn, &QPushButton::clicked, this, [this] {
        auto pw = m_gen_output->text().toStdString();
        PasswordManagerService::the().copy_secret_to_clipboard(pw);
        cleanse_string(pw);
        m_feedback_label->setText("Generated password copied (clears in 30s).");
        m_feedback_label->show();
    });
    strength_row->addWidget(copy_btn);

    auto* fill_btn = new QPushButton("Fill", page);
    fill_btn->setObjectName("SuperbirdPwPrimaryBtn");
    connect(fill_btn, &QPushButton::clicked, this, [this] {
        auto pw = m_gen_output->text().toStdString();
        if (on_fill_generated_password)
            on_fill_generated_password(pw);
        cleanse_string(pw);
        hide();
    });
    strength_row->addWidget(fill_btn);
    layout->addLayout(strength_row);

    m_gen_passphrase_mode = new QCheckBox("Passphrase (words)", page);
    connect(m_gen_passphrase_mode, &QCheckBox::toggled, this, [this] { regenerate_password(); });
    layout->addWidget(m_gen_passphrase_mode);

    auto* len_row = new QHBoxLayout;
    m_gen_length_label = new QLabel("Length: 20", page);
    m_gen_length_slider = new QSlider(Qt::Horizontal, page);
    m_gen_length_slider->setRange(8, 64);
    m_gen_length_slider->setValue(20);
    connect(m_gen_length_slider, &QSlider::valueChanged, this, [this](int val) {
        m_gen_length_label->setText(m_gen_passphrase_mode->isChecked()
                ? QString("Words: %1").arg(std::clamp(val / 4, 3, 10))
                : QString("Length: %1").arg(val));
        regenerate_password();
    });
    len_row->addWidget(m_gen_length_label);
    len_row->addWidget(m_gen_length_slider, 1);
    layout->addLayout(len_row);

    auto* checks = new QHBoxLayout;
    m_gen_upper = new QCheckBox("A-Z", page);
    m_gen_upper->setChecked(true);
    m_gen_lower = new QCheckBox("a-z", page);
    m_gen_lower->setChecked(true);
    m_gen_digits = new QCheckBox("0-9", page);
    m_gen_digits->setChecked(true);
    m_gen_symbols = new QCheckBox("!@#", page);
    m_gen_symbols->setChecked(true);
    for (auto* cb : { m_gen_upper, m_gen_lower, m_gen_digits, m_gen_symbols }) {
        connect(cb, &QCheckBox::toggled, this, [this] { regenerate_password(); });
        checks->addWidget(cb);
    }
    layout->addLayout(checks);

    m_gen_no_ambiguous = new QCheckBox("Avoid ambiguous characters (I, l, 1, O, 0)", page);
    connect(m_gen_no_ambiguous, &QCheckBox::toggled, this, [this] { regenerate_password(); });
    layout->addWidget(m_gen_no_ambiguous);

    regenerate_password();
    return page;
}

void PasswordPopover::set_page_url(std::string url)
{
    m_page_url = std::move(url);
}

void PasswordPopover::focus_search()
{
    m_tab_logins_btn->setChecked(true);
    m_tab_gen_btn->setChecked(false);
    m_stack->setCurrentIndex(0);
    m_search_edit->setFocus();
    m_search_edit->selectAll();
}

void PasswordPopover::show_generator_tab()
{
    m_tab_logins_btn->setChecked(false);
    m_tab_gen_btn->setChecked(true);
    m_stack->setCurrentIndex(1);
    regenerate_password();
}

void PasswordPopover::showEvent(QShowEvent* event)
{
    refresh();
    m_totp_timer->start();
    Ladybird::Popover::showEvent(event);
}

void PasswordPopover::hideEvent(QHideEvent* event)
{
    m_totp_timer->stop();
    Ladybird::Popover::hideEvent(event);
}

void PasswordPopover::refresh()
{
    auto& svc = PasswordManagerService::the();
    bool any_unlocked = svc.any_provider_unlocked();
    m_lock_btn->setVisible(any_unlocked);
    m_sync_btn->setVisible(any_unlocked);
    m_search_edit->setVisible(any_unlocked);
    rebuild_logins_content();
    adjustSize();
}

void PasswordPopover::rebuild_logins_content()
{
    m_totp_buttons.clear();
    while (QLayoutItem* item = m_list_layout->takeAt(0)) {
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }

    auto& svc = PasswordManagerService::the();
    auto providers = svc.enabled_providers();

    // 1. Unlocked credentials (matching site or search query)
    auto query = m_search_edit->text().trimmed().toStdString();
    std::vector<CredentialSummary> items;
    QString section_title;
    if (svc.any_provider_unlocked()) {
        if (!query.empty()) {
            items = svc.search(query);
            section_title = QString("Search Results (%1)").arg(items.size());
        } else {
            items = svc.match_for_url(m_page_url);
            section_title = QString("Matching This Site (%1)").arg(items.size());
        }

        auto* header = new QLabel(section_title, m_list_container);
        header->setObjectName("SuperbirdPwSection");
        m_list_layout->addWidget(header);

        if (items.empty()) {
            auto* empty = new QLabel(query.empty() ? "No saved logins match this site." : "No matching logins found.", m_list_container);
            empty->setObjectName("SuperbirdPwSub");
            m_list_layout->addWidget(empty);
        } else {
            for (auto const& cred : items) {
                auto* row = new QFrame(m_list_container);
                row->setObjectName("SuperbirdPwRow");
                auto* rl = new QHBoxLayout(row);
                rl->setContentsMargins(8, 6, 8, 6);
                rl->setSpacing(6);

                auto* fill_btn = new QPushButton(row);
                fill_btn->setObjectName("SuperbirdPwFillRow");
                fill_btn->setCursor(Qt::PointingHandCursor);
                QString label = QString::fromStdString(cred.name.empty() ? cred.username : cred.name);
                if (!cred.username.empty() && cred.username != cred.name)
                    label += "\n" + QString::fromStdString(cred.username);
                fill_btn->setText(label);
                connect(fill_btn, &QPushButton::clicked, this, [this, cred] {
                    if (on_fill_credential)
                        on_fill_credential(cred);
                    hide();
                });
                rl->addWidget(fill_btn, 1);

                auto* badge = new QLabel(provider_badge_text(cred.provider), row);
                badge->setObjectName("SuperbirdPwBadge");
                rl->addWidget(badge);

                if (!cred.username.empty()) {
                    auto* copy_user = new QPushButton("User", row);
                    copy_user->setObjectName("SuperbirdPwActionBtn");
                    copy_user->setToolTip("Copy username");
                    copy_user->setCursor(Qt::PointingHandCursor);
                    connect(copy_user, &QPushButton::clicked, this, [this, u = cred.username] {
                        PasswordManagerService::the().copy_plain_to_clipboard(u);
                        m_feedback_label->setText("Username copied.");
                        m_feedback_label->show();
                    });
                    rl->addWidget(copy_user);
                }

                if (cred.has_password) {
                    auto* copy_pass = new QPushButton("Pass", row);
                    copy_pass->setObjectName("SuperbirdPwActionBtn");
                    copy_pass->setToolTip("Copy password (auto-clears in 30s)");
                    copy_pass->setCursor(Qt::PointingHandCursor);
                    connect(copy_pass, &QPushButton::clicked, this, [this, cred] {
                        PasswordManagerService::the().reveal_secret(cred, [this](std::optional<CredentialSecret> secret, std::string err) {
                            if (!secret) {
                                m_feedback_label->setText(QString::fromStdString(err));
                                m_feedback_label->show();
                                return;
                            }
                            PasswordManagerService::the().copy_secret_to_clipboard(secret->password);
                            m_feedback_label->setText("Password copied (clears in 30s).");
                            m_feedback_label->show();
                        });
                    });
                    rl->addWidget(copy_pass);
                }

                if (cred.has_totp) {
                    auto* copy_totp = new QPushButton("TOTP", row);
                    copy_totp->setObjectName("SuperbirdPwActionBtn");
                    copy_totp->setToolTip("Copy TOTP code (auto-clears in 30s)");
                    copy_totp->setCursor(Qt::PointingHandCursor);
                    m_totp_buttons.push_back(copy_totp);
                    connect(copy_totp, &QPushButton::clicked, this, [this, cred] {
                        PasswordManagerService::the().reveal_secret(cred, [this](std::optional<CredentialSecret> secret, std::string err) {
                            if (!secret) {
                                m_feedback_label->setText(QString::fromStdString(err));
                                m_feedback_label->show();
                                return;
                            }
                            std::string code;
                            if (secret->totp_uri) {
                                if (auto generated = Totp::generate_from_uri(*secret->totp_uri, now_epoch_seconds()))
                                    code = generated->code;
                            } else if (secret->precomputed_totp) {
                                code = *secret->precomputed_totp;
                            }
                            if (code.empty()) {
                                m_feedback_label->setText("Could not generate TOTP code.");
                                m_feedback_label->show();
                                return;
                            }
                            PasswordManagerService::the().copy_secret_to_clipboard(code);
                            m_feedback_label->setText(QString("TOTP %1 copied (clears in 30s).").arg(QString::fromStdString(code)));
                            m_feedback_label->show();
                        });
                    });
                    rl->addWidget(copy_totp);
                }

                m_list_layout->addWidget(row);
            }
        }
    }

    // 2. Locked / Unconfigured providers section
    bool has_locked_or_unconfigured = false;
    for (auto* p : providers) {
        if (p->status() != ProviderState::Unlocked) {
            has_locked_or_unconfigured = true;
            break;
        }
    }

    if (has_locked_or_unconfigured) {
        auto* pheader = new QLabel("Providers", m_list_container);
        pheader->setObjectName("SuperbirdPwSection");
        m_list_layout->addWidget(pheader);

        for (auto* p : providers) {
            if (p->status() == ProviderState::Unlocked)
                continue;

            auto* box = new QFrame(m_list_container);
            box->setObjectName("SuperbirdPwRow");
            auto* bl = new QVBoxLayout(box);
            bl->setContentsMargins(8, 8, 8, 8);
            bl->setSpacing(6);

            auto* top = new QHBoxLayout;
            auto* name_lbl = new QLabel(QString::fromStdString(p->display_label()), box);
            name_lbl->setObjectName("SuperbirdPwName");
            top->addWidget(name_lbl, 1);

            if (p->status() == ProviderState::Unconfigured) {
                auto* setup_btn = new QPushButton("Set Up…", box);
                setup_btn->setObjectName("SuperbirdPwSmallBtn");
                setup_btn->setCursor(Qt::PointingHandCursor);
                auto kind = p->kind();
                connect(setup_btn, &QPushButton::clicked, this, [this, kind] {
                    hide();
                    if (on_open_setup)
                        on_open_setup(kind);
                });
                top->addWidget(setup_btn);
                bl->addLayout(top);
                if (kind == ProviderKind::OnePassword) {
                    auto* hint = new QLabel("Install `op` CLI & enable Developer integration in 1Password Desktop.", box);
                    hint->setObjectName("SuperbirdPwSub");
                    hint->setWordWrap(true);
                    bl->addWidget(hint);
                }
            } else {
                // Locked provider
                if (p->capabilities().supports_biometric_unlock) {
                    auto* bio_btn = new QPushButton(p->kind() == ProviderKind::OnePassword ? "Unlock via 1Password" : "Touch ID / Keyring", box);
                    bio_btn->setObjectName("SuperbirdPwPrimaryBtn");
                    bio_btn->setCursor(Qt::PointingHandCursor);
                    auto kind = p->kind();
                    connect(bio_btn, &QPushButton::clicked, this, [this, kind] {
                        auto* prov = PasswordManagerService::the().provider(kind);
                        if (!prov)
                            return;
                        m_feedback_label->setText("Unlocking…");
                        m_feedback_label->show();
                        UnlockRequest req;
                        req.method = (kind == ProviderKind::OnePassword) ? UnlockMethod::DesktopDelegated : UnlockMethod::Biometric;
                        prov->unlock(req, [this](bool ok, std::string err) {
                            if (!ok) {
                                m_feedback_label->setText(QString::fromStdString(err));
                                m_feedback_label->show();
                            } else {
                                m_feedback_label->hide();
                            }
                            refresh();
                        });
                    });
                    top->addWidget(bio_btn);
                }
                bl->addLayout(top);

                if (p->kind() != ProviderKind::OnePassword) {
                    auto* pw_row = new QHBoxLayout;
                    auto* pw_edit = new QLineEdit(box);
                    pw_edit->setEchoMode(QLineEdit::Password);
                    pw_edit->setPlaceholderText("Master password…");
                    pw_edit->setAttribute(Qt::WA_MacShowFocusRect, false);
                    auto* unlock_btn = new QPushButton("Unlock", box);
                    unlock_btn->setObjectName("SuperbirdPwSmallBtn");
                    unlock_btn->setCursor(Qt::PointingHandCursor);

                    auto do_unlock = [this, pw_edit, kind = p->kind()] {
                        auto* prov = PasswordManagerService::the().provider(kind);
                        if (!prov)
                            return;
                        UnlockRequest req;
                        req.method = UnlockMethod::MasterPassword;
                        req.master_password = pw_edit->text().toStdString();
                        pw_edit->clear();
                        prov->unlock(req, [this](bool ok, std::string err) {
                            if (!ok) {
                                m_feedback_label->setText(QString::fromStdString(err));
                                m_feedback_label->show();
                            } else {
                                m_feedback_label->hide();
                            }
                            refresh();
                        });
                        cleanse_string(req.master_password);
                    };

                    connect(pw_edit, &QLineEdit::returnPressed, this, do_unlock);
                    connect(unlock_btn, &QPushButton::clicked, this, do_unlock);
                    pw_row->addWidget(pw_edit, 1);
                    pw_row->addWidget(unlock_btn);
                    bl->addLayout(pw_row);
                }
            }
            m_list_layout->addWidget(box);
        }
    }

    m_list_layout->addStretch(1);
    update_totp_countdowns();
}

void PasswordPopover::update_totp_countdowns()
{
    uint32_t rem = 30 - static_cast<uint32_t>(now_epoch_seconds() % 30);
    for (auto* btn : m_totp_buttons) {
        if (btn)
            btn->setText(QString("OTP %1s").arg(rem));
    }
}

void PasswordPopover::regenerate_password()
{
    std::string generated;
    if (m_gen_passphrase_mode->isChecked()) {
        PassphraseGeneratorOptions opts;
        opts.word_count = static_cast<size_t>(std::clamp(m_gen_length_slider->value() / 4, 3, 10));
        generated = PasswordGenerator::generate_passphrase(opts);
    } else {
        PasswordGeneratorOptions opts;
        opts.length = static_cast<size_t>(m_gen_length_slider->value());
        opts.uppercase = m_gen_upper->isChecked();
        opts.lowercase = m_gen_lower->isChecked();
        opts.digits = m_gen_digits->isChecked();
        opts.symbols = m_gen_symbols->isChecked();
        opts.avoid_ambiguous = m_gen_no_ambiguous->isChecked();
        generated = PasswordGenerator::generate_password(opts);
    }

    auto strength = PasswordGenerator::estimate_strength(generated);
    m_gen_output->setText(QString::fromStdString(generated));
    m_gen_strength->setText(QString("%1 (%2 bits)").arg(QString::fromUtf8(strength.label.data())).arg(static_cast<int>(strength.entropy_bits)));
    cleanse_string(generated);
}

void PasswordPopover::update_chrome_style()
{
    auto palette = Ladybird::Theme::chrome_palette(*this);
    auto const& chrome = Ladybird::Theme::chrome(*this);
    auto accent = Ladybird::Theme::accent();
    setPalette(palette);
    setStyleSheet(
        Ladybird::ChromeStyle::message_popover_style_sheet(palette)
        + QString(
            "QLineEdit#SuperbirdPwSearch, QLineEdit#SuperbirdPwGenOutput, QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 8px; }"
            "QFrame#SuperbirdPwRow { background: %1; border: 1px solid %3; border-radius: 8px; }"
            "QPushButton#SuperbirdPwFillRow { text-align: left; border: none; background: transparent; color: %2; padding: 2px 4px; }"
            "QPushButton#SuperbirdPwFillRow:hover { color: %4; }"
            "QPushButton#SuperbirdPwSmallBtn, QPushButton#SuperbirdPwActionBtn, QPushButton#SuperbirdPwTabBtn { background: transparent; color: %2; border: 1px solid %3; border-radius: 6px; padding: 3px 8px; font-size: 12px; }"
            "QPushButton#SuperbirdPwTabBtn:checked { background: %4; color: white; border-color: %4; }"
            "QPushButton#SuperbirdPwPrimaryBtn { background: %4; color: white; border: none; border-radius: 6px; padding: 4px 10px; font-size: 12px; }"
            "QLabel#SuperbirdPwSection { color: %5; font-size: 11px; font-weight: 600; text-transform: uppercase; }"
            "QLabel#SuperbirdPwSub, QLabel#SuperbirdPwFeedback { color: %5; font-size: 12px; }"
            "QLabel#SuperbirdPwBadge { color: %5; border: 1px solid %3; border-radius: 4px; padding: 1px 5px; font-size: 10px; }")
              .arg(chrome.field.name(), chrome.text.name(), chrome.divider.name(), accent.name(), chrome.muted.name()));
}

}
