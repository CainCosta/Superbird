/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/OwnPtr.h>
#include <AK/Utf16String.h>
#include <AK/kmalloc.h>
#include <LibURL/Origin.h>
#include <LibWebCommon/HTML/CrossProcessId.h>
#include <LibWebCommon/PixelUnits.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordProvider.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordsActions.h>
#include <UI/Qt/Superbird/Modules/Passwords/UI/SaveCredentialBar.h>

#include <QMetaObject>
#include <QPointer>

class QAction;

namespace Ladybird {
class BrowserWindow;
class Menu;
class Tab;
}

namespace Superbird {

namespace Passwords {
class InlineAutofillPopover;
class PasswordPopover;
}

class PasswordsModule final : public SuperbirdModule {
public:
    AK_ALLOC_WITH_KMALLOC;

    static constexpr StringView MODULE_ID = PASSWORDS_MODULE_ID;

    static ModuleInfo module_info();
    static void register_actions(ModuleRegistry&);

    explicit PasswordsModule(Ladybird::BrowserWindow&);
    virtual ~PasswordsModule() override;

    virtual StringView id() const override { return MODULE_ID; }
    virtual void on_enabled_changed(bool enabled) override;
    virtual void on_settings_changed() override;
    virtual void on_chrome_style_changed() override;

    virtual void on_tab_added(Ladybird::Tab&) override;
    virtual void on_tab_removed(Ladybird::Tab&) override;
    virtual void on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab) override;
    virtual void on_active_tab_changed(Ladybird::Tab*) override;

    virtual void extend_editable_context_menu(Ladybird::Tab&, Ladybird::Menu&, URL::URL const&) override;

    bool can_run(PasswordsCommand) const;
    void run(PasswordsCommand);

private:
    struct TabState {
        AK_ALLOC_WITH_KMALLOC;

        QPointer<Ladybird::Tab> tab;
        QPointer<QAction> action;
        QMetaObject::Connection loaded_connection;
        Optional<u64> navigation_listener_id;

        Web::HTML::CrossProcessId focused_frame_id {};
        Optional<URL::Origin> focused_frame_origin;
        Passwords::AutofillFieldKind focused_field_kind { Passwords::AutofillFieldKind::None };
        Optional<Web::DevicePixelRect> focused_input_rect;
        bool focused_has_username { false };
        bool focused_has_password { false };

        OwnPtr<Passwords::SaveCredentialBar> save_bar;
    };

    bool is_enabled() const;
    bool is_autofill_allowed() const;
    void install();
    void uninstall();
    void attach(Ladybird::Tab&);
    void attach_view(TabState&);
    void detach(Ladybird::Tab&);
    TabState* state_for(Ladybird::Tab*);
    TabState const* state_for(Ladybird::Tab*) const;

    void update_action(TabState&);
    void update_all_actions();
    void open_popover(Ladybird::Tab&, bool focus_search = false, bool show_generator = false);
    void open_setup_dialog(Passwords::ProviderKind initial_tab);
    void hide_inline_popover();

    void handle_focused_field_changed(
        Ladybird::Tab&,
        Web::HTML::CrossProcessId frame_id,
        Optional<URL::Origin> const& frame_origin,
        u8 field_kind,
        Optional<Web::DevicePixelRect> const& input_rect,
        bool has_username,
        bool has_password);

    void handle_submitted_credentials(
        Ladybird::Tab&,
        URL::Origin const& origin,
        Utf16String const& username,
        Utf16String const& password);

    void fill_credential(Ladybird::Tab&, Passwords::CredentialSummary const&);
    void fill_focused_value(Ladybird::Tab&, std::string const& value);
    void autofill_top_match_or_open(Ladybird::Tab&);
    void copy_top_totp(Ladybird::Tab&);

    Ladybird::BrowserWindow& m_window;
    HashMap<Ladybird::Tab*, NonnullOwnPtr<TabState>> m_tabs;
    QPointer<Passwords::PasswordPopover> m_popover;
    QPointer<Passwords::InlineAutofillPopover> m_inline_popover;
    QMetaObject::Connection m_service_connection;
};

}
