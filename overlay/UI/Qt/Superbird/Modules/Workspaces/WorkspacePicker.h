/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <UI/Qt/Popover.h>
#include <UI/Qt/Superbird/Modules/Workspaces/WorkspaceModel.h>

#include <optional>
#include <string>
#include <vector>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace Superbird {

// The popover that makes and edits workspaces. Making one starts on a page of presets (Home, Work, School, Focus,
// Relax) and a "Custom" tile, which opens the editor: a name, a line icon (or the name's first letter) and a tint, with a
// live preview of the badge. Editing opens the editor directly. It is drawn with the chrome's message popover style.
class WorkspacePicker final : public Ladybird::Popover {
public:
    AK_ALLOC_WITH_KMALLOC;

    struct Result {
        std::string name;
        std::string icon;
        std::optional<uint32_t> tint;
    };

    explicit WorkspacePicker(QWidget* parent);

    // Shows the presets, leaving out the ones whose names are taken.
    void show_presets(std::vector<std::string> existing_names, uint32_t custom_tint);
    // Shows the editor for an existing workspace.
    void show_editor(Workspaces::Workspace const&);
    void update_chrome_style(QPalette const&);

    Function<void(Result)> on_accept;
    // The card changed size (e.g. on going from the presets to the editor), so the popover should be placed again.
    Function<void()> on_size_changed;

private:
    enum class Page : u8 {
        Presets,
        Editor,
    };
    enum class EditorMode : u8 {
        Create,
        Edit,
    };

    virtual void showEvent(QShowEvent*) override;

    void build_presets_page();
    void build_editor_page();
    void show_page(Page);
    void open_editor(EditorMode, Workspaces::Workspace const&);
    void refresh_presets();
    void refresh_editor_icons();
    void refresh_preview();
    void accept_editor();
    Workspaces::Workspace edited_workspace() const;

    QWidget* m_presets_page { nullptr };
    QWidget* m_editor_page { nullptr };
    Page m_page { Page::Presets };
    EditorMode m_editor_mode { EditorMode::Create };

    Vector<QToolButton*> m_preset_tiles;
    QToolButton* m_custom_tile { nullptr };
    std::vector<std::string> m_existing_names;
    uint32_t m_custom_tint { 0 };

    QLabel* m_editor_title { nullptr };
    QLabel* m_preview { nullptr };
    QLineEdit* m_name_field { nullptr };
    QButtonGroup* m_icon_buttons { nullptr };
    QButtonGroup* m_tint_buttons { nullptr };
    QPushButton* m_back_button { nullptr };
    QPushButton* m_accept_button { nullptr };

    // What the editor holds: an icon text ("glyph:<key>", other text kept from before, or empty for the first letter)
    // and a tint (nothing for the accent).
    std::string m_icon;
    std::optional<uint32_t> m_tint;
};

}
