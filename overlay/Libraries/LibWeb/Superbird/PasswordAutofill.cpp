/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <LibGC/RootVector.h>
#include <LibWeb/ARIA/AttributeNames.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Element.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/HTML/AttributeNames.h>
#include <LibWeb/HTML/EventNames.h>
#include <LibWeb/HTML/HTMLFormElement.h>
#include <LibWeb/HTML/HTMLInputElement.h>
#include <LibWeb/HTML/LocalNavigable.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HighResolutionTime/TimeOrigin.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/Superbird/PasswordAutofill.h>

namespace Web::Superbird {

namespace {

ByteString lower_attr(DOM::Element const& element, Utf16FlyString const& name)
{
    auto attr = element.attribute(name);
    if (!attr.has_value())
        return {};
    return attr->to_utf8().to_byte_string().to_lowercase();
}

bool contains_any(ByteString const& haystack, ReadonlySpan<StringView> needles)
{
    if (haystack.is_empty())
        return false;
    for (auto needle : needles) {
        if (haystack.contains(needle))
            return true;
    }
    return false;
}

bool is_fillable_text_like_input(HTML::HTMLInputElement const& input)
{
    if (!input.is_mutable())
        return false;
    switch (input.type_state()) {
    case HTML::HTMLInputElement::TypeAttributeState::Text:
    case HTML::HTMLInputElement::TypeAttributeState::Email:
    case HTML::HTMLInputElement::TypeAttributeState::Password:
    case HTML::HTMLInputElement::TypeAttributeState::Telephone:
    case HTML::HTMLInputElement::TypeAttributeState::Number:
    case HTML::HTMLInputElement::TypeAttributeState::Search:
        return true;
    default:
        return false;
    }
}

bool is_username_candidate_type(HTML::HTMLInputElement const& input)
{
    if (!input.is_mutable())
        return false;
    switch (input.type_state()) {
    case HTML::HTMLInputElement::TypeAttributeState::Email:
    case HTML::HTMLInputElement::TypeAttributeState::Text:
    case HTML::HTMLInputElement::TypeAttributeState::Telephone:
        return true;
    default:
        return false;
    }
}

AutofillFieldKind classify_input(HTML::HTMLInputElement const& input)
{
    if (!is_fillable_text_like_input(input))
        return AutofillFieldKind::None;

    auto autocomplete = lower_attr(input, HTML::AttributeNames::autocomplete);
    if (autocomplete.contains("one-time-code"sv))
        return AutofillFieldKind::OneTimeCode;
    if (autocomplete.contains("new-password"sv))
        return AutofillFieldKind::NewPassword;
    if (autocomplete.contains("current-password"sv))
        return AutofillFieldKind::Password;
    if (autocomplete.contains("username"sv) || autocomplete.contains("email"sv))
        return AutofillFieldKind::Username;

    if (input.type_state() == HTML::HTMLInputElement::TypeAttributeState::Password)
        return AutofillFieldKind::Password;
    if (input.type_state() == HTML::HTMLInputElement::TypeAttributeState::Email)
        return AutofillFieldKind::Username;

    auto name = lower_attr(input, HTML::AttributeNames::name);
    auto id = lower_attr(input, HTML::AttributeNames::id);
    auto placeholder = lower_attr(input, HTML::AttributeNames::placeholder);
    auto aria_label = lower_attr(input, ARIA::AttributeNames::aria_label);

    StringBuilder combined_builder;
    combined_builder.append(name);
    combined_builder.append(' ');
    combined_builder.append(id);
    combined_builder.append(' ');
    combined_builder.append(placeholder);
    combined_builder.append(' ');
    combined_builder.append(aria_label);
    auto combined = combined_builder.to_byte_string();

    static constexpr Array<StringView, 7> OTP_TOKENS {
        "otp"sv, "totp"sv, "2fa"sv, "mfa"sv, "one-time"sv, "verification code"sv, "authcode"sv
    };
    if (contains_any(combined, OTP_TOKENS))
        return AutofillFieldKind::OneTimeCode;

    static constexpr Array<StringView, 8> USERNAME_TOKENS {
        "user"sv, "email"sv, "e-mail"sv, "login"sv, "account"sv, "identifier"sv, "handle"sv, "userid"sv
    };
    if (contains_any(combined, USERNAME_TOKENS))
        return AutofillFieldKind::Username;

    return AutofillFieldKind::None;
}

GC::RootVector<GC::Ref<HTML::HTMLInputElement>> collect_scope_inputs(DOM::Document& document, HTML::HTMLFormElement* form)
{
    GC::RootVector<GC::Ref<HTML::HTMLInputElement>> inputs;
    if (form) {
        for (auto& element : form->get_submittable_elements()) {
            if (auto* input = as_if<HTML::HTMLInputElement>(*element)) {
                if (is_fillable_text_like_input(*input))
                    inputs.append(*input);
            }
        }
        if (!inputs.is_empty())
            return inputs;
    }

    document.for_each_in_inclusive_subtree_of_type<HTML::HTMLInputElement>([&](HTML::HTMLInputElement& input) {
        if (is_fillable_text_like_input(input))
            inputs.append(input);
        return TraversalDecision::Continue;
    });
    return inputs;
}

struct LoginFieldPair {
    GC::Ptr<HTML::HTMLInputElement> username;
    GC::Ptr<HTML::HTMLInputElement> password;
};

LoginFieldPair find_login_pair(
    GC::RootVector<GC::Ref<HTML::HTMLInputElement>> const& inputs,
    HTML::HTMLInputElement* preferred_input = nullptr)
{
    LoginFieldPair pair;

    // 1. Find password field (preferring the focused input if it's a password field).
    if (preferred_input) {
        auto kind = classify_input(*preferred_input);
        if (kind == AutofillFieldKind::Password || kind == AutofillFieldKind::NewPassword)
            pair.password = preferred_input;
        else if (kind == AutofillFieldKind::Username)
            pair.username = preferred_input;
    }

    size_t password_index = inputs.size();
    for (size_t i = 0; i < inputs.size(); ++i) {
        auto& input = *inputs[i];
        auto kind = classify_input(input);
        if (!pair.password && (kind == AutofillFieldKind::Password || kind == AutofillFieldKind::NewPassword)) {
            pair.password = &input;
            password_index = i;
        } else if (pair.password && pair.password.ptr() == &input) {
            password_index = i;
        }
    }

    // 2. Find username field: prefer explicit username/email before password, then any text-like input right before password.
    if (!pair.username) {
        size_t search_limit = pair.password ? password_index : inputs.size();
        for (size_t i = search_limit; i > 0; --i) {
            auto& input = *inputs[i - 1];
            if (&input == pair.password.ptr())
                continue;
            if (classify_input(input) == AutofillFieldKind::Username) {
                pair.username = &input;
                break;
            }
        }
    }

    if (!pair.username && pair.password && password_index > 0) {
        for (size_t i = password_index; i > 0; --i) {
            auto& input = *inputs[i - 1];
            if (is_username_candidate_type(input) && classify_input(input) != AutofillFieldKind::OneTimeCode) {
                pair.username = &input;
                break;
            }
        }
    }

    // 3. If still no username and no password, scan forward for an explicitly classified username input.
    if (!pair.username) {
        for (auto const& input : inputs) {
            if (input.ptr() == pair.password.ptr())
                continue;
            if (classify_input(*input) == AutofillFieldKind::Username) {
                pair.username = input.ptr();
                break;
            }
        }
    }

    return pair;
}

void apply_value_and_dispatch_events(HTML::HTMLInputElement& input, Utf16String const& value)
{
    if (!input.is_mutable())
        return;

    if (input.set_value(value).is_error())
        return;

    auto& global = HTML::relevant_global_object(input);
    auto now = HighResolutionTime::current_high_resolution_time(global);

    // Fire trusted `input` (bubbles + composed) and `change` (bubbles) events so React/Vue/Next.js controlled inputs update.
    auto input_event = DOM::Event::create(HTML::EventNames::input, { .bubbles = true, .composed = true }, now);
    input_event->set_is_trusted(true);
    input.dispatch_event(input_event);

    auto change_event = DOM::Event::create(HTML::EventNames::change, { .bubbles = true }, now);
    change_event->set_is_trusted(true);
    input.dispatch_event(change_event);
}

GC::Ptr<HTML::LocalNavigable> resolve_target_navigable(Page& page, HTML::CrossProcessId frame_id)
{
    if (frame_id != HTML::CrossProcessId {}) {
        if (auto navigable = page.navigable_with_id(frame_id))
            return as_if<HTML::LocalNavigable>(navigable.ptr());
        return nullptr;
    }
    // If focus is currently in a remote (cross-origin / OOPIF) navigable, refuse to fall back to the top-level frame.
    if (auto focused = page.focused_navigable())
        return as_if<HTML::LocalNavigable>(focused.ptr());
    if (page.has_local_traversable())
        return page.local_traversable();
    return nullptr;
}

} // namespace

AutofillFocusedField inspect_focused_field(Page& page)
{
    AutofillFocusedField info;
    auto navigable = page.hosted_focused_navigable();
    if (!navigable)
        return info;

    auto document = navigable->active_document();
    if (!document)
        return info;

    info.frame_id = navigable->id();
    info.frame_origin = document->origin();

    auto focused = document->focused_area();
    auto* input = as_if<HTML::HTMLInputElement>(focused.ptr());
    if (!input || !is_fillable_text_like_input(*input))
        return info;

    auto kind = classify_input(*input);
    auto inputs = collect_scope_inputs(*document, input->form());
    auto pair = find_login_pair(inputs, input);

    // If a plain text input sits immediately before a password input in a login form, treat it as Username.
    if (kind == AutofillFieldKind::None && pair.username.ptr() == input && pair.password)
        kind = AutofillFieldKind::Username;

    info.field_kind = kind;
    info.has_username = pair.username != nullptr;
    info.has_password = pair.password != nullptr;

    if (kind != AutofillFieldKind::None) {
        auto css_rect = input->get_bounding_client_rect();
        if (css_rect.width() > 0 && css_rect.height() > 0)
            info.input_rect = page.enclosing_device_rect(css_rect);
    }

    return info;
}

bool fill_credentials(
    Page& page,
    HTML::CrossProcessId frame_id,
    URL::Origin const& expected_origin,
    Utf16String const& username,
    Utf16String const& password)
{
    auto navigable = resolve_target_navigable(page, frame_id);
    if (!navigable)
        return false;

    auto document = navigable->active_document();
    if (!document)
        return false;

    // Non-negotiable security invariant: refuse if the active document's origin does not match expected_origin.
    if (!document->origin().is_same_origin(expected_origin))
        return false;

    auto* focused_input = as_if<HTML::HTMLInputElement>(document->focused_area().ptr());
    HTML::HTMLFormElement* form = focused_input ? focused_input->form() : nullptr;

    auto inputs = collect_scope_inputs(*document, form);
    auto pair = find_login_pair(inputs, focused_input);

    // If the focused input wasn't in a login form, fall back to scanning the whole document.
    if (!pair.username && !pair.password && form) {
        auto doc_inputs = collect_scope_inputs(*document, nullptr);
        pair = find_login_pair(doc_inputs, nullptr);
    }

    bool filled_any = false;
    if (pair.username && !username.is_empty()) {
        apply_value_and_dispatch_events(*pair.username, username);
        filled_any = true;
    }
    if (pair.password && !password.is_empty()) {
        apply_value_and_dispatch_events(*pair.password, password);
        filled_any = true;
    }
    return filled_any;
}

bool fill_focused_field(
    Page& page,
    HTML::CrossProcessId frame_id,
    URL::Origin const& expected_origin,
    Utf16String const& value)
{
    auto navigable = resolve_target_navigable(page, frame_id);
    if (!navigable)
        return false;

    auto document = navigable->active_document();
    if (!document)
        return false;

    // Non-negotiable security invariant: verify frame origin matches expected_origin.
    if (!document->origin().is_same_origin(expected_origin))
        return false;

    auto* focused_input = as_if<HTML::HTMLInputElement>(document->focused_area().ptr());
    if (!focused_input || !is_fillable_text_like_input(*focused_input))
        return false;

    apply_value_and_dispatch_events(*focused_input, value);
    return true;
}

Optional<SubmittedCredentials> extract_submitted_credentials(HTML::HTMLFormElement& form)
{
    auto& document = form.document();
    auto inputs = collect_scope_inputs(document, &form);
    auto pair = find_login_pair(inputs, nullptr);
    if (!pair.password)
        return {};

    auto password = pair.password->value();
    if (password.is_empty())
        return {};

    Utf16String username;
    if (pair.username)
        username = pair.username->value();

    return SubmittedCredentials {
        .origin = document.origin(),
        .username = move(username),
        .password = move(password),
    };
}

}
