/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/Environment.h>
#include <LibCore/File.h>
#include <LibURL/Parser.h>
#include <LibWebView/SuperbirdContentBlocking.h>
#include <LibWebView/SuperbirdContentBlockingFormat.h>

namespace WebView::SuperbirdContentBlocking {

static constexpr StringView DEFAULT_UBO_RESOURCES_URL = "https://github.com/nicbarker/superbird-ubo-resources/releases/latest/download/ubo-resources.json"sv;

struct BuiltInUboList {
    StringView identifier;
    StringView name;
    StringView description;
    StringView url;
    bool trusted;
};

static constexpr BuiltInUboList s_ubo_default_lists[] = {
    {
        "uboFilters"sv,
        "uBO-compatible filters"sv,
        "Blocks ads, pop-ups, and anti-adblock scripts using uBO-compatible rules."sv,
        "https://ublockorigin.github.io/uAssetsCDN/filters/filters.min.txt"sv,
        true,
    },
    {
        "uboBadware"sv,
        "uBO-compatible badware risks"sv,
        "Protects against sites known to distribute malware or unwanted software."sv,
        "https://ublockorigin.github.io/uAssetsCDN/filters/badware.min.txt"sv,
        true,
    },
    {
        "uboPrivacy"sv,
        "uBO-compatible privacy"sv,
        "Blocks trackers, telemetry endpoints, and URL tracking parameters."sv,
        "https://ublockorigin.github.io/uAssetsCDN/filters/privacy.min.txt"sv,
        true,
    },
    {
        "uboUnbreak"sv,
        "uBO-compatible unbreak"sv,
        "Fixes websites that break under standard filter lists."sv,
        "https://ublockorigin.github.io/uAssetsCDN/filters/unbreak.min.txt"sv,
        true,
    },
    {
        "uboQuickFixes"sv,
        "uBO-compatible quick fixes"sv,
        "Rapidly updated fixes for video platforms and active anti-adblock workarounds."sv,
        "https://ublockorigin.github.io/uAssetsCDN/filters/quick-fixes.min.txt"sv,
        true,
    },
    {
        "peterLowe"sv,
        "Peter Lowe's Ad and tracking server list"sv,
        "Blocks known advertising and tracking server hostnames."sv,
        "https://pgl.yoyo.org/adservers/serverlist.php?hostformat=adblockplus&showintro=1&mimetype=plaintext"sv,
        false,
    },
    {
        "urlhaus"sv,
        "Online Malicious URL Blocklist"sv,
        "Blocks active malware distribution URLs."sv,
        "https://malware-filter.gitlab.io/urlhaus-filter/urlhaus-filter-agh-online.txt"sv,
        false,
    },
};

void register_default_lists(Vector<ContentBlockerList>& lists)
{
    for (auto& list : lists) {
        if (list.identifier == "easyList"sv || list.identifier == "easyPrivacy"sv)
            list.enabled = true;
    }

    for (auto const& def : s_ubo_default_lists) {
        bool already_present = false;
        for (auto const& existing : lists) {
            if (existing.identifier == def.identifier) {
                already_present = true;
                break;
            }
        }
        if (already_present)
            continue;

        lists.append({
            MUST(String::from_utf8(def.identifier)),
            MUST(String::from_utf8(def.name)),
            URL::Parser::basic_parse(def.url),
            true,
            true,
            false,
            MUST(String::from_utf8(def.description)),
        });
    }
}

static Optional<StringView> module_option_value(Settings const& settings, StringView option_id)
{
    auto const& all_options = settings.superbird_settings().module_options;
    auto module_it = all_options.find(module_id);
    if (module_it == all_options.end())
        return {};
    auto option_it = module_it->value.find(option_id);
    if (option_it == module_it->value.end())
        return {};
    return option_it->value.bytes_as_string_view();
}

bool is_ubo_mode_enabled(Settings const& settings)
{
    auto const& enabled_modules = settings.superbird_settings().enabled_modules;
    if (auto it = enabled_modules.find(module_id); it != enabled_modules.end() && !it->value)
        return false;

    auto mode = module_option_value(settings, option_engine_mode).value_or("ubo"sv);
    return mode != "default"sv;
}

bool trust_custom_filters_enabled(Settings const& settings)
{
    if (!is_ubo_mode_enabled(settings))
        return false;
    return module_option_value(settings, option_trust_custom_filters).value_or("false"sv) == "true"sv;
}

bool logger_enabled(Settings const& settings)
{
    if (!is_ubo_mode_enabled(settings))
        return false;
    return module_option_value(settings, option_enable_logger).value_or("false"sv) == "true"sv;
}

bool is_cosmetic_filtering_enabled_for_host(Settings const& settings, StringView host)
{
    if (host.is_empty())
        return true;
    auto csv = module_option_value(settings, option_no_cosmetic_sites).value_or(""sv);
    if (csv.is_empty())
        return true;
    auto hosts = ::Superbird::ContentBlockingFormat::parse_host_list({ csv.characters_without_null_termination(), csv.length() });
    auto target = ::Superbird::ContentBlockingFormat::parse_host_list({ host.characters_without_null_termination(), host.length() });
    if (target.empty())
        return true;
    for (auto const& disabled : hosts) {
        if (disabled == target.front())
            return false;
    }
    return true;
}

void set_cosmetic_filtering_enabled_for_host(Settings& settings, StringView host, bool enabled)
{
    auto target = ::Superbird::ContentBlockingFormat::parse_host_list({ host.characters_without_null_termination(), host.length() });
    if (target.empty())
        return;
    auto csv = module_option_value(settings, option_no_cosmetic_sites).value_or(""sv);
    auto hosts = ::Superbird::ContentBlockingFormat::parse_host_list({ csv.characters_without_null_termination(), csv.length() });

    std::vector<std::string> updated;
    updated.reserve(hosts.size() + 1);
    bool found = false;
    for (auto const& existing : hosts) {
        if (existing == target.front()) {
            found = true;
            if (!enabled)
                updated.push_back(existing);
        } else {
            updated.push_back(existing);
        }
    }
    if (!enabled && !found)
        updated.push_back(target.front());

    auto serialized = ::Superbird::ContentBlockingFormat::serialize_host_list(updated);
    settings.set_superbird_module_option(
        MUST(String::from_utf8(module_id)),
        MUST(String::from_utf8(option_no_cosmetic_sites)),
        MUST(String::from_utf8(StringView { serialized.data(), serialized.size() })));
}

String site_switch_rules(Settings const& settings)
{
    if (!is_ubo_mode_enabled(settings))
        return {};
    auto csv = module_option_value(settings, option_no_cosmetic_sites).value_or(""sv);
    if (csv.is_empty())
        return {};
    auto hosts = ::Superbird::ContentBlockingFormat::parse_host_list({ csv.characters_without_null_termination(), csv.length() });
    auto rules = ::Superbird::ContentBlockingFormat::build_site_switches_rules(hosts);
    return MUST(String::from_utf8(StringView { rules.data(), rules.size() }));
}

bool is_trusted_list_identifier(StringView identifier)
{
    return ::Superbird::ContentBlockingFormat::is_trusted_list_id({ identifier.characters_without_null_termination(), identifier.length() });
}

bool is_ubo_only_list_identifier(StringView identifier)
{
    return ::Superbird::ContentBlockingFormat::is_ubo_only_list_id({ identifier.characters_without_null_termination(), identifier.length() });
}

bool is_default_enabled_list_identifier(StringView identifier)
{
    if (identifier == "easyList"sv || identifier == "easyPrivacy"sv)
        return true;
    return is_ubo_only_list_identifier(identifier);
}

i64 parse_list_expiry_seconds(ReadonlyBytes header_bytes, StringView identifier)
{
    return ::Superbird::ContentBlockingFormat::parse_list_expiry_seconds(
        { reinterpret_cast<char const*>(header_bytes.data()), header_bytes.size() },
        { identifier.characters_without_null_termination(), identifier.length() });
}

i64 list_expiry_seconds_for_file(ByteString const& path, StringView identifier)
{
    auto file_or_error = Core::File::open(path, Core::File::OpenMode::Read);
    if (file_or_error.is_error())
        return parse_list_expiry_seconds({}, identifier);

    Array<u8, 2048> header_buffer;
    auto read_or_error = file_or_error.value()->read_some(header_buffer);
    if (read_or_error.is_error())
        return parse_list_expiry_seconds({}, identifier);

    return parse_list_expiry_seconds(read_or_error.value(), identifier);
}

Optional<URL::URL> ubo_resources_url()
{
    if (auto override_url = Core::Environment::get("SUPERBIRD_UBO_RESOURCES_URL"sv); override_url.has_value()) {
        if (override_url->is_empty())
            return {};
        return URL::Parser::basic_parse(*override_url);
    }
    return URL::Parser::basic_parse(DEFAULT_UBO_RESOURCES_URL);
}

ErrorOr<ByteBuffer> build_bundle(
    bool ubo_mode,
    ReadonlySpan<BundleInputList> lists,
    StringView custom_filters,
    bool trust_custom_filters,
    ReadonlyBytes resources_json,
    StringView site_switches_rules,
    bool logger_enabled)
{
    std::vector<::Superbird::ContentBlockingFormat::ListEntry> entries;
    entries.reserve(lists.size());
    for (auto const& list : lists) {
        auto id_view = list.id.bytes_as_string_view();
        entries.push_back({
            .id = std::string(id_view.characters_without_null_termination(), id_view.length()),
            .trusted = list.trusted,
            .is_hosts_format = list.is_hosts_format,
            .contents = { reinterpret_cast<char const*>(list.contents.data()), list.contents.size() },
        });
    }

    auto out = ::Superbird::ContentBlockingFormat::build_bundle_string(
        ubo_mode,
        entries,
        { custom_filters.characters_without_null_termination(), custom_filters.length() },
        trust_custom_filters,
        { reinterpret_cast<char const*>(resources_json.data()), resources_json.size() },
        { site_switches_rules.characters_without_null_termination(), site_switches_rules.length() },
        logger_enabled);

    if (out.empty())
        return ByteBuffer {};
    return ByteBuffer::copy(out.data(), out.size());
}

}
