/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <UI/Qt/Popover.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>

#include <QTimer>

#include <functional>
#include <string>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QStackedWidget;
class QVBoxLayout;

namespace Superbird::Passwords {

class PasswordPopover final : public Ladybird::Popover {
    Q_OBJECT

public:
    explicit PasswordPopover(QWidget* parent = nullptr);

    void set_page_url(std::string url);
    void focus_search();
    void show_generator_tab();
    void refresh();
    void update_chrome_style();

    std::function<void(CredentialSummary const&)> on_fill_credential;
    std::function<void(std::string const&)> on_fill_generated_password;
    std::function<void(ProviderKind)> on_open_setup;

protected:
    virtual void showEvent(QShowEvent*) override;
    virtual void hideEvent(QHideEvent*) override;

private:
    QWidget* build_logins_page();
    QWidget* build_generator_page();
    void rebuild_logins_content();
    void regenerate_password();
    void update_totp_countdowns();

    std::string m_page_url;
    QPushButton* m_tab_logins_btn { nullptr };
    QPushButton* m_tab_gen_btn { nullptr };
    QPushButton* m_sync_btn { nullptr };
    QPushButton* m_lock_btn { nullptr };
    QPushButton* m_setup_btn { nullptr };

    QStackedWidget* m_stack { nullptr };
    QLineEdit* m_search_edit { nullptr };
    QScrollArea* m_scroll_area { nullptr };
    QWidget* m_list_container { nullptr };
    QVBoxLayout* m_list_layout { nullptr };
    QLabel* m_feedback_label { nullptr };

    // Generator tab
    QLineEdit* m_gen_output { nullptr };
    QLabel* m_gen_strength { nullptr };
    QCheckBox* m_gen_passphrase_mode { nullptr };
    QSlider* m_gen_length_slider { nullptr };
    QLabel* m_gen_length_label { nullptr };
    QCheckBox* m_gen_upper { nullptr };
    QCheckBox* m_gen_lower { nullptr };
    QCheckBox* m_gen_digits { nullptr };
    QCheckBox* m_gen_symbols { nullptr };
    QCheckBox* m_gen_no_ambiguous { nullptr };

    QTimer* m_totp_timer { nullptr };
    std::vector<QPushButton*> m_totp_buttons;
};

}
