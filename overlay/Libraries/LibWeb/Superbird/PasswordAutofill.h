/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Types.h>
#include <AK/Utf16String.h>
#include <LibURL/Origin.h>
#include <LibWeb/Export.h>
#include <LibWeb/Forward.h>
#include <LibWebCommon/HTML/CrossProcessId.h>
#include <LibWebCommon/PixelUnits.h>

namespace Web::Superbird {

enum class AutofillFieldKind : u8 {
    None = 0,
    Username = 1,
    Password = 2,
    NewPassword = 3,
    OneTimeCode = 4,
};

struct AutofillFocusedField {
    HTML::CrossProcessId frame_id {};
    Optional<URL::Origin> frame_origin;
    AutofillFieldKind field_kind { AutofillFieldKind::None };
    Optional<DevicePixelRect> input_rect;
    bool has_username { false };
    bool has_password { false };
};

struct SubmittedCredentials {
    URL::Origin origin;
    Utf16String username;
    Utf16String password;
};

WEB_API AutofillFocusedField inspect_focused_field(Page&);

WEB_API bool fill_credentials(
    Page&,
    HTML::CrossProcessId frame_id,
    URL::Origin const& expected_origin,
    Utf16String const& username,
    Utf16String const& password);

WEB_API bool fill_focused_field(
    Page&,
    HTML::CrossProcessId frame_id,
    URL::Origin const& expected_origin,
    Utf16String const& value);

WEB_API Optional<SubmittedCredentials> extract_submitted_credentials(HTML::HTMLFormElement&);

}
