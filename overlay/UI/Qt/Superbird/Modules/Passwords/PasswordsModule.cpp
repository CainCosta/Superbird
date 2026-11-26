/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Array.h>
#include <UI/Qt/Actions.h>
#include <UI/Qt/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/Menu.h>
#include <UI/Qt/Popover.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Core/WindowHost.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/PasswordGenerator.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/Totp.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/UriMatcher.h>
#include <UI/Qt/Superbird/Modules/Passwords/PasswordManagerService.h>
#include <UI/Qt/Superbird/Modules/Passwords/PasswordsModule.h>
#include <UI/Qt/Superbird/Modules/Passwords/UI/InlineAutofillPopover.h>
#include <UI/Qt/Superbird/Modules/Passwords/UI/PasswordPopover.h>
#include <UI/Qt/Superbird/Modules/Passwords/UI/ProviderSetupDialog.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/Theme.h>

#include <QAction>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <chrono>

namespace Superbird {

namespace {

constexpr StringView OPT_PROVIDER_BITWARDEN = "provider_bitwarden"sv;
constexpr StringView OPT_PROVIDER_LOCAL_VAULT = "provider_local_vault"sv;
constexpr StringView OPT_PROVIDER_ONEPASSWORD = "provider_onepassword"sv;
constexpr StringView OPT_AUTO_LOCK_TIMEOUT = "auto_lock_timeout"sv;
constexpr StringView OPT_INLINE_DROPDOWN = "inline_dropdown"sv;
constexpr StringView OPT_SAVE_PROMPTS = "save_prompts"sv;
constexpr StringView OPT_PRIVATE_AUTOFILL = "private_window_autofill"sv;

constexpr Array<ModuleChoice, 4> AUTO_LOCK_CHOICES { {
    { .value = "5m"sv, .label = "5 minutes"sv },
    { .value = "15m"sv, .label = "15 minutes"sv },
    { .value = "60m"sv, .label = "1 hour"sv },
    { .value = "never"sv, .label = "On quit"sv },
} };

constexpr Array<ModuleOption, 7> OPTIONS { {
    { .id = OPT_PROVIDER_BITWARDEN, .label = "Bitwarden / Vaultwarden"sv, .description = "Native zero-knowledge client for Bitwarden Cloud (US/EU) and self-hosted Vaultwarden."sv, .kind = ModuleOption::Kind::Switch, .default_value = "true"sv },
    { .id = OPT_PROVIDER_LOCAL_VAULT, .label = "Built-in Local Vault"sv, .description = "Encrypted local vault in Passwords.db with Argon2id + AES-256-GCM and CSV import/export."sv, .kind = ModuleOption::Kind::Switch, .default_value = "true"sv },
    { .id = OPT_PROVIDER_ONEPASSWORD, .label = "1Password (CLI v2)"sv, .description = "Connect to the running 1Password desktop app via the official `op` CLI."sv, .kind = ModuleOption::Kind::Switch, .default_value = "true"sv },
    { .id = OPT_AUTO_LOCK_TIMEOUT, .label = "Auto-lock vaults after"sv, .description = "Lock unlocked vaults and cleanse keys from memory after inactivity."sv, .kind = ModuleOption::Kind::Choice, .default_value = "15m"sv, .choices = AUTO_LOCK_CHOICES.span() },
    { .id = OPT_INLINE_DROPDOWN, .label = "Show inline autofill dropdown"sv, .description = "Anchor a matching logins menu below focused username, password, and OTP fields."sv, .kind = ModuleOption::Kind::Switch, .default_value = "true"sv },
    { .id = OPT_SAVE_PROMPTS, .label = "Offer to save and update logins"sv, .description = "Show a bar to save new logins or update changed passwords after form submission (never in private windows)."sv, .kind = ModuleOption::Kind::Switch, .default_value = "true"sv },
    { .id = OPT_PRIVATE_AUTOFILL, .label = "Allow autofill in private windows"sv, .description = "Off by default. Private windows never offer to save passwords."sv, .kind = ModuleOption::Kind::Switch, .default_value = "false"sv },
} };

constexpr Array<ModuleShortcut, 7> SHORTCUTS { {
    { .command_id = "passwords.autofill"sv, .title = "Autofill login"sv, .default_shortcut = "Ctrl+Shift+L"sv },
    { .command_id = "passwords.open"sv, .title = "Open Password Manager"sv },
    { .command_id = "passwords.search"sv, .title = "Search passwords"sv },
    { .command_id = "passwords.copy_totp"sv, .title = "Copy TOTP for current site"sv },
    { .command_id = "passwords.generate"sv, .title = "Generate strong password"sv },
    { .command_id = "passwords.sync"sv, .title = "Sync vaults"sv },
    { .command_id = "passwords.lock"sv, .title = "Lock all vaults"sv },
} };

QIcon create_key_icon(QColor color, bool show_match_dot, QColor accent)
{
    QIcon icon;
    for (int scale = 1; scale <= 3; ++scale) {
        QPixmap pm(16 * scale, 16 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);

        QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // Key head (ring)
        p.drawEllipse(QPointF(5.5, 8.0), 3.0, 3.0);
        // Key shaft + teeth
        p.drawLine(QPointF(8.5, 8.0), QPointF(14.0, 8.0));
        p.drawLine(QPointF(11.5, 8.0), QPointF(11.5, 10.5));
        p.drawLine(QPointF(13.8, 8.0), QPointF(13.8, 10.2));

        if (show_match_dot) {
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawEllipse(QPointF(13.0, 3.2), 2.3, 2.3);
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

std::string url_to_std_string(URL::URL const& url)
{
    auto bs = url.to_byte_string();
    return std::string(bs.characters(), bs.length());
}

std::string origin_to_url_string(URL::Origin const& origin)
{
    auto s = origin.serialize();
    auto sv = s.bytes_as_string_view();
    return std::string(sv.characters_without_null_termination(), sv.length());
}

Utf16String to_utf16(std::string_view s)
{
    return Utf16String::from_utf8(StringView { s.data(), s.size() });
}

int64_t now_epoch_seconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

class ActiveWindowPasswordsHandler final : public PasswordsCommandHandler {
public:
    virtual bool can_run(PasswordsCommand command) const override
    {
        auto* module = active_module();
        return module && module->can_run(command);
    }

    virtual void run(PasswordsCommand command) override
    {
        if (auto* module = active_module())
            module->run(command);
    }

private:
    static PasswordsModule* active_module()
    {
        auto* window = Ladybird::Application::the().active_window_if_any();
        if (!window)
            return nullptr;
        return static_cast<PasswordsModule*>(window->superbird().module(PASSWORDS_MODULE_ID));
    }
};

} // namespace

ModuleInfo PasswordsModule::module_info()
{
    return {
        .id = MODULE_ID,
        .name = "Passwords"sv,
        .description = "Multi-provider password manager with Native Bitwarden/Vaultwarden, Built-in Local Vault, and 1Password CLI autofill."sv,
        .enabled_by_default = true,
        .options = OPTIONS.span(),
        .shortcuts = SHORTCUTS.span(),
    };
}

void PasswordsModule::register_actions(ModuleRegistry& registry)
{
    static ActiveWindowPasswordsHandler handler;
    register_passwords_actions(registry, handler);
}

PasswordsModule::PasswordsModule(Ladybird::BrowserWindow& window)
    : m_window(window)
{
    if (is_enabled())
        install();
}

PasswordsModule::~PasswordsModule()
{
    uninstall();
}

bool PasswordsModule::is_enabled() const
{
    return ModuleRegistry::the().is_enabled(MODULE_ID);
}

bool PasswordsModule::is_autofill_allowed() const
{
    if (!is_enabled())
        return false;
    if (m_window.is_private() == WebView::IsPrivate::Yes)
        return WindowHost::module_option_is_on(MODULE_ID, OPT_PRIVATE_AUTOFILL);
    return true;
}

void PasswordsModule::install()
{
    m_service_connection = QObject::connect(
        &Passwords::PasswordManagerService::the(),
        &Passwords::PasswordManagerService::stateChanged,
        &m_window,
        [this] { update_all_actions(); });

    for (int i = 0; i < m_window.tab_count(); ++i) {
        if (auto* tab = m_window.tab_at(i))
            attach(*tab);
    }
}

void PasswordsModule::uninstall()
{
    QObject::disconnect(m_service_connection);
    hide_inline_popover();
    if (m_popover) {
        m_popover->hide();
        m_popover->deleteLater();
        m_popover = nullptr;
    }

    Vector<Ladybird::Tab*> tabs;
    for (auto const& it : m_tabs)
        tabs.append(it.key);
    for (auto* tab : tabs)
        detach(*tab);
}

void PasswordsModule::on_enabled_changed(bool enabled)
{
    if (enabled)
        install();
    else
        uninstall();
}

void PasswordsModule::on_settings_changed()
{
    if (!is_enabled())
        return;
    Passwords::PasswordManagerService::the().refresh_settings();
    if (!WindowHost::module_option_is_on(MODULE_ID, OPT_INLINE_DROPDOWN))
        hide_inline_popover();
    update_all_actions();
}

void PasswordsModule::on_chrome_style_changed()
{
    if (m_popover)
        m_popover->update_chrome_style();
    if (m_inline_popover)
        m_inline_popover->update_chrome_style();
    update_all_actions();
}

PasswordsModule::TabState* PasswordsModule::state_for(Ladybird::Tab* tab)
{
    auto it = m_tabs.find(tab);
    return it == m_tabs.end() ? nullptr : it->value.ptr();
}

PasswordsModule::TabState const* PasswordsModule::state_for(Ladybird::Tab* tab) const
{
    auto it = m_tabs.find(tab);
    return it == m_tabs.end() ? nullptr : it->value.ptr();
}

void PasswordsModule::attach(Ladybird::Tab& tab)
{
    if (!is_enabled() || m_tabs.contains(&tab))
        return;
    auto state = make<TabState>();
    state->tab = &tab;
    auto& ref = *state;
    m_tabs.set(&tab, move(state));

    if (tab.is_loaded()) {
        attach_view(ref);
    } else {
        ref.loaded_connection = QObject::connect(&tab, &Ladybird::Tab::loaded, &m_window, [this, tab_ptr = QPointer(&tab)] {
            if (!tab_ptr)
                return;
            if (auto* st = state_for(tab_ptr.data()); st && !st->action)
                attach_view(*st);
        });
    }
}

void PasswordsModule::attach_view(TabState& state)
{
    auto* tab = state.tab.data();
    if (!tab || !tab->is_loaded())
        return;

    auto* action = new QAction(tab);
    QObject::connect(action, &QAction::triggered, tab, [this, tab_ptr = QPointer(tab)] {
        if (tab_ptr)
            open_popover(*tab_ptr);
    });
    state.action = action;
    tab->location_edit().add_page_action(action);

    state.navigation_listener_id = tab->view().add_navigation_listener({
        .on_load_start = [this, tab_ptr = QPointer(tab)](URL::URL const&) {
            if (!tab_ptr)
                return;
            hide_inline_popover();
            if (auto* st = state_for(tab_ptr.data())) {
                st->focused_field_kind = Passwords::AutofillFieldKind::None;
                st->focused_frame_origin.clear();
                st->focused_input_rect.clear();
                update_action(*st);
            }
        },
        .on_load_finish = [this, tab_ptr = QPointer(tab)](URL::URL const&) {
            if (!tab_ptr)
                return;
            if (auto* st = state_for(tab_ptr.data()))
                update_action(*st);
        },
    });

    tab->view().on_autofill_focused_field_change = [this, tab_ptr = QPointer(tab)](
                                                       Web::HTML::CrossProcessId frame_id,
                                                       Optional<URL::Origin> const& frame_origin,
                                                       u8 field_kind,
                                                       Optional<Web::DevicePixelRect> const& input_rect,
                                                       bool has_username,
                                                       bool has_password) {
        if (tab_ptr && is_enabled())
            handle_focused_field_changed(*tab_ptr, frame_id, frame_origin, field_kind, input_rect, has_username, has_password);
    };

    tab->view().on_submit_credentials = [this, tab_ptr = QPointer(tab)](
                                            URL::Origin const& origin,
                                            Utf16String const& username,
                                            Utf16String const& password) {
        if (tab_ptr && is_enabled())
            handle_submitted_credentials(*tab_ptr, origin, username, password);
    };

    update_action(state);
}

void PasswordsModule::detach(Ladybird::Tab& tab)
{
    auto state = m_tabs.take(&tab);
    if (!state.has_value())
        return;
    auto& st = **state;
    QObject::disconnect(st.loaded_connection);
    if (st.save_bar)
        st.save_bar->dismiss();
    if (tab.is_loaded()) {
        if (st.navigation_listener_id.has_value())
            tab.view().remove_navigation_listener(*st.navigation_listener_id);
        tab.view().on_autofill_focused_field_change = nullptr;
        tab.view().on_submit_credentials = nullptr;
    }
    delete st.action.data();
}

void PasswordsModule::on_tab_added(Ladybird::Tab& tab)
{
    attach(tab);
}

void PasswordsModule::on_tab_removed(Ladybird::Tab& tab)
{
    detach(tab);
}

void PasswordsModule::on_tab_replaced(Ladybird::Tab const& old_tab, Ladybird::Tab& new_tab)
{
    detach(const_cast<Ladybird::Tab&>(old_tab));
    attach(new_tab);
}

void PasswordsModule::on_active_tab_changed(Ladybird::Tab* tab)
{
    hide_inline_popover();
    if (auto* st = state_for(tab))
        update_action(*st);
}

void PasswordsModule::update_action(TabState& state)
{
    if (!state.action)
        return;
    auto* tab = state.tab.data();
    if (!tab || !tab->is_loaded())
        return;

    auto url_str = url_to_std_string(tab->url());
    bool show = is_enabled() && Passwords::UriMatcher::is_autofillable_url(url_str);
    state.action->setVisible(show);
    if (!show)
        return;

    auto& svc = Passwords::PasswordManagerService::the();
    bool unlocked = svc.any_provider_unlocked();
    auto matches = (unlocked && is_autofill_allowed()) ? svc.match_for_url(url_str) : std::vector<Passwords::CredentialSummary> {};

    auto const& chrome = Ladybird::Theme::chrome(m_window);
    auto accent = Ladybird::Theme::accent();
    QColor icon_color = unlocked ? (matches.empty() ? chrome.icon : accent) : chrome.muted;
    state.action->setIcon(create_key_icon(icon_color, !matches.empty(), accent));

    auto shortcut = WindowHost::shortcut_text("passwords.autofill"sv);
    QString tip = !unlocked ? "Passwords (Locked)"
        : matches.empty()   ? "Passwords"
                            : QString("Passwords (%1 match%2)").arg(matches.size()).arg(matches.size() == 1 ? "" : "es");
    if (!shortcut.isEmpty())
        tip += QString(" (%1)").arg(shortcut);
    state.action->setToolTip(tip);
}

void PasswordsModule::update_all_actions()
{
    for (auto& it : m_tabs)
        update_action(*it.value);
}

void PasswordsModule::hide_inline_popover()
{
    if (m_inline_popover)
        m_inline_popover->hide();
}

void PasswordsModule::handle_focused_field_changed(
    Ladybird::Tab& tab,
    Web::HTML::CrossProcessId frame_id,
    Optional<URL::Origin> const& frame_origin,
    u8 field_kind_raw,
    Optional<Web::DevicePixelRect> const& input_rect,
    bool has_username,
    bool has_password)
{
    auto* state = state_for(&tab);
    if (!state)
        return;

    auto kind = static_cast<Passwords::AutofillFieldKind>(field_kind_raw);
    state->focused_frame_id = frame_id;
    state->focused_frame_origin = frame_origin;
    state->focused_field_kind = kind;
    state->focused_input_rect = input_rect;
    state->focused_has_username = has_username;
    state->focused_has_password = has_password;

    if (&tab != m_window.current_tab()
        || !is_autofill_allowed()
        || !WindowHost::module_option_is_on(MODULE_ID, OPT_INLINE_DROPDOWN)
        || kind == Passwords::AutofillFieldKind::None
        || !frame_origin.has_value()
        || !input_rect.has_value()) {
        hide_inline_popover();
        return;
    }

    auto origin_url = origin_to_url_string(*frame_origin);
    if (!Passwords::UriMatcher::is_autofillable_url(origin_url)) {
        hide_inline_popover();
        return;
    }

    auto& svc = Passwords::PasswordManagerService::the();
    auto matches = svc.match_for_url(origin_url);
    bool offer_gen = (kind == Passwords::AutofillFieldKind::NewPassword);
    bool locked = !svc.any_provider_unlocked() && svc.any_provider_configured();

    if (matches.empty() && !offer_gen && !locked) {
        hide_inline_popover();
        return;
    }

    if (!m_inline_popover) {
        m_inline_popover = new Passwords::InlineAutofillPopover(&m_window);
        m_inline_popover->on_pick_credential = [this](Passwords::CredentialSummary const& cred) {
            if (auto* current = m_window.current_tab())
                fill_credential(*current, cred);
        };
        m_inline_popover->on_request_unlock = [this] {
            if (auto* current = m_window.current_tab())
                open_popover(*current);
        };
        m_inline_popover->on_request_generate_password = [this] {
            if (auto* current = m_window.current_tab()) {
                auto generated = Passwords::PasswordGenerator::generate_password();
                fill_focused_value(*current, generated);
                Passwords::PasswordManagerService::the().copy_secret_to_clipboard(generated);
                Passwords::cleanse_string(generated);
            }
        };
    }

    double dpr = tab.view().device_pixel_ratio();
    if (dpr <= 0.0)
        dpr = 1.0;
    QPoint local_bottom_left(
        static_cast<int>(input_rect->x().value() / dpr),
        static_cast<int>((input_rect->y().value() + input_rect->height().value()) / dpr));
    int field_width = static_cast<int>(input_rect->width().value() / dpr);
    QPoint global_anchor = tab.view().mapToGlobal(local_bottom_left);

    m_inline_popover->show_credentials(global_anchor, field_width, matches, locked, offer_gen);
}

void PasswordsModule::handle_submitted_credentials(
    Ladybird::Tab& tab,
    URL::Origin const& origin,
    Utf16String const& username_u16,
    Utf16String const& password_u16)
{
    // Non-negotiable security invariant: NEVER prompt to save credentials in private windows.
    if (m_window.is_private() == WebView::IsPrivate::Yes)
        return;
    if (!is_enabled() || !WindowHost::module_option_is_on(MODULE_ID, OPT_SAVE_PROMPTS))
        return;

    auto origin_url = origin_to_url_string(origin);
    if (!Passwords::UriMatcher::is_autofillable_url(origin_url))
        return;

    auto parsed_url = Passwords::UriMatcher::parse_url(origin_url);
    if (!parsed_url)
        return;

    auto& svc = Passwords::PasswordManagerService::the();
    if (svc.is_never_save_host(parsed_url->host))
        return;

    std::vector<Passwords::ProviderKind> writable_providers;
    for (auto* p : svc.unlocked_providers()) {
        if (p->capabilities().can_create)
            writable_providers.push_back(p->kind());
    }
    if (writable_providers.empty())
        return;

    auto user_utf8 = username_u16.to_utf8();
    auto pass_utf8 = password_u16.to_utf8();
    std::string username(user_utf8.bytes_as_string_view().characters_without_null_termination(), user_utf8.bytes_as_string_view().length());
    std::string password(pass_utf8.bytes_as_string_view().characters_without_null_termination(), pass_utf8.bytes_as_string_view().length());
    if (password.empty())
        return;

    std::string host = parsed_url->host;
    svc.classify_submitted_credential(
        origin_url,
        username,
        password,
        [this, tab_ptr = QPointer(&tab), origin_url, host, username, password, writable_providers](Passwords::PasswordManagerService::SaveMatchInfo match_info) mutable {
            if (!tab_ptr || !is_enabled()) {
                Passwords::cleanse_string(password);
                return;
            }
            if (match_info.kind == Passwords::PasswordManagerService::SaveMatchInfo::Kind::AlreadyIdentical) {
                Passwords::cleanse_string(password);
                return;
            }
            auto* st = state_for(tab_ptr.data());
            if (!st) {
                Passwords::cleanse_string(password);
                return;
            }
            if (!st->save_bar)
                st->save_bar = make<Passwords::SaveCredentialBar>();

            auto make_new_login = [host, origin_url, username, password]() -> Passwords::NewLogin {
                Passwords::NewLogin nl;
                nl.name = host;
                nl.username = username;
                nl.password = password;
                nl.uris.push_back({ origin_url, Passwords::UriMatchType::BaseDomain });
                return nl;
            };

            if (match_info.kind == Passwords::PasswordManagerService::SaveMatchInfo::Kind::UpdateExisting && match_info.existing.has_value()) {
                auto existing = *match_info.existing;
                st->save_bar->show_update_prompt(
                    *tab_ptr,
                    host,
                    existing,
                    [existing, password]() {
                        Passwords::UpdatedLogin upd;
                        upd.password = password;
                        Passwords::PasswordManagerService::the().update_login(existing, upd, {});
                    },
                    [primary = writable_providers[0], make_new_login]() {
                        Passwords::PasswordManagerService::the().create_login(primary, make_new_login(), {});
                    },
                    [] {});
            } else {
                st->save_bar->show_save_prompt(
                    *tab_ptr,
                    host,
                    username,
                    writable_providers,
                    [make_new_login](Passwords::ProviderKind chosen) {
                        Passwords::PasswordManagerService::the().create_login(chosen, make_new_login(), {});
                    },
                    [host]() {
                        Passwords::PasswordManagerService::the().add_never_save_host(host);
                    },
                    [] {});
            }
            Passwords::cleanse_string(password);
        });
}

void PasswordsModule::fill_credential(Ladybird::Tab& tab, Passwords::CredentialSummary const& summary)
{
    if (!tab.is_loaded() || !is_autofill_allowed())
        return;

    auto* st = state_for(&tab);
    Web::HTML::CrossProcessId frame_id {};
    Optional<URL::Origin> expected_origin;
    auto field_kind = Passwords::AutofillFieldKind::None;

    if (st && st->focused_frame_origin.has_value()) {
        frame_id = st->focused_frame_id;
        expected_origin = *st->focused_frame_origin;
        field_kind = st->focused_field_kind;
    } else {
        expected_origin = tab.url().origin();
    }

    if (!expected_origin.has_value())
        return;

    hide_inline_popover();

    Passwords::PasswordManagerService::the().reveal_secret(
        summary,
        [tab_ptr = QPointer(&tab), frame_id, expected_origin = *expected_origin, field_kind](std::optional<Passwords::CredentialSecret> secret, std::string const&) {
            if (!tab_ptr || !tab_ptr->is_loaded() || !secret)
                return;

            if (field_kind == Passwords::AutofillFieldKind::OneTimeCode) {
                std::string otp;
                if (secret->totp_uri) {
                    if (auto code = Passwords::Totp::generate_from_uri(*secret->totp_uri, now_epoch_seconds()))
                        otp = std::move(code->code);
                } else if (secret->precomputed_totp) {
                    otp = *secret->precomputed_totp;
                }
                if (!otp.empty())
                    tab_ptr->view().fill_focused_field(frame_id, expected_origin, to_utf16(otp));
                Passwords::cleanse_string(otp);
                return;
            }

            tab_ptr->view().fill_credentials(frame_id, expected_origin, to_utf16(secret->username), to_utf16(secret->password));
            if (secret->totp_uri) {
                if (auto code = Passwords::Totp::generate_from_uri(*secret->totp_uri, now_epoch_seconds()))
                    Passwords::PasswordManagerService::the().copy_secret_to_clipboard(code->code);
            }
        });
}

void PasswordsModule::fill_focused_value(Ladybird::Tab& tab, std::string const& value)
{
    if (!tab.is_loaded() || !is_autofill_allowed() || value.empty())
        return;
    auto* st = state_for(&tab);
    Web::HTML::CrossProcessId frame_id = st ? st->focused_frame_id : Web::HTML::CrossProcessId {};
    URL::Origin expected_origin = (st && st->focused_frame_origin.has_value()) ? *st->focused_frame_origin : tab.url().origin();
    tab.view().fill_focused_field(frame_id, expected_origin, to_utf16(value));
}

void PasswordsModule::open_popover(Ladybird::Tab& tab, bool focus_search, bool show_generator)
{
    if (!is_enabled() || !tab.is_loaded())
        return;
    hide_inline_popover();

    if (!m_popover) {
        m_popover = new Passwords::PasswordPopover(&m_window);
        m_popover->on_fill_credential = [this](Passwords::CredentialSummary const& cred) {
            if (auto* current = m_window.current_tab())
                fill_credential(*current, cred);
        };
        m_popover->on_fill_generated_password = [this](std::string const& pw) {
            if (auto* current = m_window.current_tab())
                fill_focused_value(*current, pw);
        };
        m_popover->on_open_setup = [this](Passwords::ProviderKind kind) {
            open_setup_dialog(kind);
        };
    }

    auto* st = state_for(&tab);
    std::string target_url = (st && st->focused_frame_origin.has_value())
        ? origin_to_url_string(*st->focused_frame_origin)
        : url_to_std_string(tab.url());
    m_popover->set_page_url(std::move(target_url));
    m_popover->refresh();

    if (show_generator)
        m_popover->show_generator_tab();

    Ladybird::move_popover_below(*m_popover, tab.location_edit(), Ladybird::PopoverAlignment::Trailing);
    m_popover->show();
    if (focus_search)
        m_popover->focus_search();
}

void PasswordsModule::open_setup_dialog(Passwords::ProviderKind initial_tab)
{
    Passwords::ProviderSetupDialog dialog(&m_window, initial_tab);
    dialog.exec();
    update_all_actions();
}

void PasswordsModule::autofill_top_match_or_open(Ladybird::Tab& tab)
{
    if (!is_autofill_allowed() || !tab.is_loaded())
        return;

    auto& svc = Passwords::PasswordManagerService::the();
    if (!svc.any_provider_unlocked()) {
        open_popover(tab);
        return;
    }

    auto* st = state_for(&tab);
    std::string target_url = (st && st->focused_frame_origin.has_value())
        ? origin_to_url_string(*st->focused_frame_origin)
        : url_to_std_string(tab.url());

    auto matches = svc.match_for_url(target_url);
    if (matches.size() == 1) {
        fill_credential(tab, matches[0]);
        return;
    }
    open_popover(tab);
}

void PasswordsModule::copy_top_totp(Ladybird::Tab& tab)
{
    if (!tab.is_loaded())
        return;
    auto& svc = Passwords::PasswordManagerService::the();
    auto matches = svc.match_for_url(url_to_std_string(tab.url()));
    for (auto const& m : matches) {
        if (!m.has_totp)
            continue;
        svc.reveal_secret(m, [](std::optional<Passwords::CredentialSecret> secret, std::string const&) {
            if (!secret)
                return;
            if (secret->totp_uri) {
                if (auto code = Passwords::Totp::generate_from_uri(*secret->totp_uri, now_epoch_seconds()))
                    Passwords::PasswordManagerService::the().copy_secret_to_clipboard(code->code);
            } else if (secret->precomputed_totp) {
                Passwords::PasswordManagerService::the().copy_secret_to_clipboard(*secret->precomputed_totp);
            }
        });
        return;
    }
}

void PasswordsModule::extend_editable_context_menu(Ladybird::Tab& tab, Ladybird::Menu& menu, URL::URL const& url)
{
    if (!is_autofill_allowed())
        return;

    auto url_str = url_to_std_string(url);
    auto& svc = Passwords::PasswordManagerService::the();
    auto matches = svc.match_for_url(url_str);

    if (!matches.empty()) {
        auto top = matches[0];
        auto label = MUST(String::formatted("Fill Login ({})", StringView { top.username.empty() ? top.name.data() : top.username.data(), top.username.empty() ? top.name.size() : top.username.size() }));
        menu.add_action(Ladybird::Action::create(label, Ladybird::ActionID::SuperbirdModuleAction, [this, tab_ptr = QPointer(&tab), top] {
            if (tab_ptr && is_enabled())
                fill_credential(*tab_ptr, top);
        }));
    } else {
        menu.add_action(Ladybird::Action::create("Fill Login…"sv, Ladybird::ActionID::SuperbirdModuleAction, [this, tab_ptr = QPointer(&tab)] {
            if (tab_ptr && is_enabled())
                open_popover(*tab_ptr);
        }));
    }

    bool has_totp = false;
    for (auto const& m : matches) {
        if (m.has_totp) {
            has_totp = true;
            break;
        }
    }
    if (has_totp) {
        menu.add_action(Ladybird::Action::create("Copy TOTP Code"sv, Ladybird::ActionID::SuperbirdModuleAction, [this, tab_ptr = QPointer(&tab)] {
            if (tab_ptr && is_enabled())
                copy_top_totp(*tab_ptr);
        }));
    }

    menu.add_action(Ladybird::Action::create("Generate Strong Password"sv, Ladybird::ActionID::SuperbirdModuleAction, [this, tab_ptr = QPointer(&tab)] {
        if (tab_ptr && is_enabled()) {
            auto pw = Passwords::PasswordGenerator::generate_password();
            fill_focused_value(*tab_ptr, pw);
            Passwords::PasswordManagerService::the().copy_secret_to_clipboard(pw);
            Passwords::cleanse_string(pw);
        }
    }));
}

bool PasswordsModule::can_run(PasswordsCommand command) const
{
    if (!is_enabled())
        return false;
    auto& svc = Passwords::PasswordManagerService::the();
    switch (command) {
    case PasswordsCommand::AutofillLogin:
    case PasswordsCommand::OpenPopover:
    case PasswordsCommand::SearchPasswords:
    case PasswordsCommand::GeneratePassword:
        return m_window.current_tab() && m_window.current_tab()->is_loaded();
    case PasswordsCommand::CopyTotp:
        return m_window.current_tab() && m_window.current_tab()->is_loaded() && svc.any_provider_unlocked();
    case PasswordsCommand::SyncVaults:
    case PasswordsCommand::LockAllVaults:
        return svc.any_provider_unlocked();
    }
    return false;
}

void PasswordsModule::run(PasswordsCommand command)
{
    if (!can_run(command))
        return;
    auto* tab = m_window.current_tab();
    switch (command) {
    case PasswordsCommand::AutofillLogin:
        if (tab)
            autofill_top_match_or_open(*tab);
        break;
    case PasswordsCommand::OpenPopover:
        if (tab)
            open_popover(*tab);
        break;
    case PasswordsCommand::SearchPasswords:
        if (tab)
            open_popover(*tab, /* focus_search */ true);
        break;
    case PasswordsCommand::CopyTotp:
        if (tab)
            copy_top_totp(*tab);
        break;
    case PasswordsCommand::GeneratePassword:
        if (tab)
            open_popover(*tab, /* focus_search */ false, /* show_generator */ true);
        break;
    case PasswordsCommand::SyncVaults:
        Passwords::PasswordManagerService::the().sync_all();
        break;
    case PasswordsCommand::LockAllVaults:
        Passwords::PasswordManagerService::the().lock_all();
        break;
    }
}

}
