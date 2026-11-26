/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteString.h>
#include <AK/Error.h>
#include <AK/Optional.h>
#include <AK/Span.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Vector.h>
#include <LibURL/URL.h>
#include <LibWebView/Forward.h>
#include <LibWebView/Settings.h>

namespace WebView::SuperbirdContentBlocking {

inline constexpr StringView module_id = "content_blocking"sv;
inline constexpr StringView option_engine_mode = "engine_mode"sv;
inline constexpr StringView option_trust_custom_filters = "trust_custom_filters"sv;
inline constexpr StringView option_enable_logger = "enable_logger"sv;
inline constexpr StringView option_no_cosmetic_sites = "no_cosmetic_sites"sv;
inline constexpr StringView resources_update_identifier = "__superbird_ubo_resources"sv;

inline constexpr i64 minimum_expiry_seconds = 60 * 60;
inline constexpr i64 maximum_expiry_seconds = 14 * 24 * 60 * 60;
inline constexpr i64 default_expiry_seconds = 24 * 60 * 60;
inline constexpr i64 quick_fixes_default_expiry_seconds = 4 * 60 * 60;
inline constexpr i64 resources_expiry_seconds = 24 * 60 * 60;
inline constexpr i64 update_check_interval_seconds = 30 * 60;

struct BundleInputList {
    String id;
    bool trusted { false };
    bool is_hosts_format { false };
    ByteBuffer contents;
};

WEBVIEW_API void register_default_lists(Vector<ContentBlockerList>& lists);
WEBVIEW_API bool is_ubo_mode_enabled(Settings const&);
WEBVIEW_API bool trust_custom_filters_enabled(Settings const&);
WEBVIEW_API bool logger_enabled(Settings const&);
WEBVIEW_API bool is_cosmetic_filtering_enabled_for_host(Settings const&, StringView host);
WEBVIEW_API void set_cosmetic_filtering_enabled_for_host(Settings&, StringView host, bool enabled);
WEBVIEW_API String site_switch_rules(Settings const&);
WEBVIEW_API bool is_trusted_list_identifier(StringView identifier);
WEBVIEW_API bool is_ubo_only_list_identifier(StringView identifier);
WEBVIEW_API bool is_default_enabled_list_identifier(StringView identifier);

WEBVIEW_API i64 parse_list_expiry_seconds(ReadonlyBytes header_bytes, StringView identifier = {});
WEBVIEW_API i64 list_expiry_seconds_for_file(ByteString const& path, StringView identifier);

WEBVIEW_API Optional<URL::URL> ubo_resources_url();

WEBVIEW_API ErrorOr<ByteBuffer> build_bundle(
    bool ubo_mode,
    ReadonlySpan<BundleInputList> lists,
    StringView custom_filters,
    bool trust_custom_filters,
    ReadonlyBytes resources_json = {},
    StringView site_switches_rules = {},
    bool logger_enabled = false);

}
