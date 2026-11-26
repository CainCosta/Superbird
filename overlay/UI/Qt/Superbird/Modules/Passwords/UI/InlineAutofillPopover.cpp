/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/UI/InlineAutofillPopover.h>
#include <UI/Qt/Theme.h>

#include <QKeyEvent>
#include <QScreen>
#include <QVBoxLayout>

namespace Superbird::Passwords {

InlineAutofillPopover::InlineAutofillPopover(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setObjectName("SuperbirdInlineAutofill");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setCursor(Qt::PointingHandCursor);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        int row = m_list->row(item);
        if (row >= 0) {
            m_list->setCurrentRow(row);
            activate_current_row();
        }
    });
    layout->addWidget(m_list);

    update_chrome_style();
}

void InlineAutofillPopover::show_credentials(
    QPoint global_anchor_bottom_left,
    int min_width,
    std::vector<CredentialSummary> const& credentials,
    bool vaults_locked,
    bool offer_generate_password)
{
    m_list->clear();
    m_entries.clear();

    for (auto const& c : credentials) {
        QString text = QString::fromStdString(c.username.empty() ? c.name : c.username);
        if (!c.name.empty() && c.name != c.username)
            text += " — " + QString::fromStdString(c.name);
        text += " [" + QString::fromUtf8(provider_kind_name(c.provider).data()) + "]";
        auto* item = new QListWidgetItem(text, m_list);
        item->setSizeHint({ 0, 32 });
        m_entries.push_back({ RowAction::Credential, c });
    }

    if (offer_generate_password) {
        auto* item = new QListWidgetItem("Suggest Strong Password…", m_list);
        item->setSizeHint({ 0, 32 });
        m_entries.push_back({ RowAction::GeneratePassword, {} });
    }

    if (vaults_locked && credentials.empty()) {
        auto* item = new QListWidgetItem("Unlock Passwords…", m_list);
        item->setSizeHint({ 0, 32 });
        m_entries.push_back({ RowAction::Unlock, {} });
    }

    if (m_entries.empty()) {
        hide();
        return;
    }

    m_list->setCurrentRow(0);
    int rows = std::min<int>(static_cast<int>(m_entries.size()), 6);
    int w = std::clamp(min_width, 260, 380);
    int h = rows * 32 + 12;
    setFixedSize(w, h);

    QPoint pos = global_anchor_bottom_left + QPoint(0, 4);
    if (auto* screen = QWidget::screen()) {
        auto avail = screen->availableGeometry();
        if (pos.x() + w > avail.right())
            pos.setX(avail.right() - w);
        if (pos.y() + h > avail.bottom())
            pos.setY(global_anchor_bottom_left.y() - h - 36);
    }

    move(pos);
    show();
    raise();
}

void InlineAutofillPopover::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    if (event->key() == Qt::Key_Down) {
        int next = (m_list->currentRow() + 1) % std::max(1, m_list->count());
        m_list->setCurrentRow(next);
        return;
    }
    if (event->key() == Qt::Key_Up) {
        int prev = (m_list->currentRow() - 1 + m_list->count()) % std::max(1, m_list->count());
        m_list->setCurrentRow(prev);
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        activate_current_row();
        return;
    }
    QWidget::keyPressEvent(event);
}

void InlineAutofillPopover::activate_current_row()
{
    int row = m_list->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= m_entries.size())
        return;
    auto entry = m_entries[static_cast<size_t>(row)];
    hide();
    switch (entry.action) {
    case RowAction::Credential:
        if (on_pick_credential)
            on_pick_credential(entry.credential);
        break;
    case RowAction::Unlock:
        if (on_request_unlock)
            on_request_unlock();
        break;
    case RowAction::GeneratePassword:
        if (on_request_generate_password)
            on_request_generate_password();
        break;
    }
}

void InlineAutofillPopover::update_chrome_style()
{
    auto const& chrome = Ladybird::Theme::chrome(*this);
    auto accent = Ladybird::Theme::accent();
    setStyleSheet(QString(
        "QWidget#SuperbirdInlineAutofill { background: %1; border: 1px solid %2; border-radius: 8px; }"
        "QListWidget { background: transparent; color: %3; outline: 0; }"
        "QListWidget::item { padding: 4px 8px; border-radius: 6px; }"
        "QListWidget::item:selected, QListWidget::item:hover { background: %4; color: white; }")
            .arg(chrome.toolbar.name(), chrome.divider.name(), chrome.text.name(), accent.name()));
}

}
