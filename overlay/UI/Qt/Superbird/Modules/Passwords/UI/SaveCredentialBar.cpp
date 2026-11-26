/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/UI/SaveCredentialBar.h>
#include <UI/Qt/Tab.h>

#include <QBoxLayout>
#include <QMenu>

namespace Superbird::Passwords {

SaveCredentialBar::~SaveCredentialBar()
{
    dismiss();
}

void SaveCredentialBar::dismiss()
{
    if (m_bar) {
        if (m_tab && m_tab->layout())
            m_tab->layout()->removeWidget(m_bar);
        m_bar->hide();
        m_bar->deleteLater();
        m_bar = nullptr;
    }
    m_tab = nullptr;
}

void SaveCredentialBar::mount(
    Ladybird::Tab& tab,
    QString const& message,
    Vector<Ladybird::NotificationBar::Button> buttons,
    Function<void()> on_close)
{
    dismiss();
    auto* layout = qobject_cast<QBoxLayout*>(tab.layout());
    if (!layout)
        return;

    m_tab = &tab;
    m_bar = new Ladybird::NotificationBar(tab, message, AK::move(buttons), AK::move(on_close));
    layout->insertWidget(0, m_bar);
    m_bar->show();
}

void SaveCredentialBar::show_save_prompt(
    Ladybird::Tab& tab,
    std::string const& host,
    std::string const& username,
    std::vector<ProviderKind> const& target_providers,
    std::function<void(ProviderKind)> on_save,
    std::function<void()> on_never,
    std::function<void()> on_dismiss)
{
    if (target_providers.empty())
        return;

    auto primary = target_providers[0];
    QString msg = username.empty()
        ? QString("Save login for %1 to %2?")
              .arg(QString::fromStdString(host), QString::fromUtf8(provider_kind_name(primary).data()))
        : QString("Save login for %1 (%2) to %3?")
              .arg(QString::fromStdString(host), QString::fromStdString(username), QString::fromUtf8(provider_kind_name(primary).data()));

    Vector<Ladybird::NotificationBar::Button> buttons;
    buttons.append({ "Save", [this, primary, on_save] {
                        if (on_save)
                            on_save(primary);
                        dismiss();
                    } });

    if (target_providers.size() > 1) {
        buttons.append({ "Choose Provider…", [this, target_providers, on_save] {
                            if (!m_bar)
                                return;
                            QMenu menu(m_bar);
                            for (auto kind : target_providers) {
                                auto* act = menu.addAction(QString::fromUtf8(provider_kind_name(kind).data()));
                                QObject::connect(act, &QAction::triggered, m_bar, [this, kind, on_save] {
                                    if (on_save)
                                        on_save(kind);
                                    dismiss();
                                });
                            }
                            menu.exec(QCursor::pos());
                        } });
    }

    buttons.append({ "Never for this site", [this, on_never] {
                        if (on_never)
                            on_never();
                        dismiss();
                    } });

    mount(tab, msg, AK::move(buttons), [this, on_dismiss] {
        if (on_dismiss)
            on_dismiss();
        dismiss();
    });
}

void SaveCredentialBar::show_update_prompt(
    Ladybird::Tab& tab,
    std::string const& host,
    CredentialSummary const& existing,
    std::function<void()> on_update,
    std::function<void()> on_save_as_new,
    std::function<void()> on_dismiss)
{
    QString msg = QString("Update password for %1 on %2 (%3)?")
                      .arg(QString::fromStdString(existing.username),
                          QString::fromStdString(host),
                          QString::fromUtf8(provider_kind_name(existing.provider).data()));

    Vector<Ladybird::NotificationBar::Button> buttons;
    buttons.append({ "Update", [this, on_update] {
                        if (on_update)
                            on_update();
                        dismiss();
                    } });
    buttons.append({ "Save as New", [this, on_save_as_new] {
                        if (on_save_as_new)
                            on_save_as_new();
                        dismiss();
                    } });

    mount(tab, msg, AK::move(buttons), [this, on_dismiss] {
        if (on_dismiss)
            on_dismiss();
        dismiss();
    });
}

}
