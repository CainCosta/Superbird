/*
 * Copyright (c) 2026-present, the Superbird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Libraries/LibWebView/SuperbirdContentBlockingFormat.h>
#include <UI/Qt/Superbird/Core/Module.h>
#include <UI/Qt/Superbird/Modules/ContentBlocking/ContentBlockingActions.h>

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace Superbird::ContentBlockingFormat;

void test_expiry_parsing()
{
    assert(parse_list_expiry_seconds("! Expires: 4 days\n||a.com^\n") == 4 * 86400);
    assert(parse_list_expiry_seconds("! Title: Quick fixes\n! Expires: 4 days\n! Diff-Expires: 4 hours\n||a.com^\n") == 4 * 3600);
    assert(parse_list_expiry_seconds("! Diff-Expires: 30 minutes\n") == minimum_expiry_seconds);
    assert(parse_list_expiry_seconds("! Expires: 90 minutes\n") == 90 * 60);
    assert(parse_list_expiry_seconds("! Expires: 30 days\n") == maximum_expiry_seconds);
    assert(parse_list_expiry_seconds("! Expires: +4 hours\n") == default_expiry_seconds);
    assert(parse_list_expiry_seconds("! Expires: -1 hours\n") == default_expiry_seconds);
    assert(parse_list_expiry_seconds("! Expires: 0 hours\n") == default_expiry_seconds);
    assert(parse_list_expiry_seconds("! Expires: garbage\n") == default_expiry_seconds);
    assert(parse_list_expiry_seconds("", "uboQuickFixes") == quick_fixes_default_expiry_seconds);
    assert(parse_list_expiry_seconds("", "uboFilters") == default_expiry_seconds);
    // Stops scanning after first non-comment rule line.
    assert(parse_list_expiry_seconds("||ads.example^\n! Expires: 2 hours\n") == default_expiry_seconds);
    // Allows [Adblock Plus 2.0] header before comments.
    assert(parse_list_expiry_seconds("[Adblock Plus 2.0]\n! Expires: 6 hours\n||ads.example^\n") == 6 * 3600);
    // Handles CRLF and huge numbers without overflow.
    assert(parse_list_expiry_seconds("! Expires: 999999999999 days\r\n") == maximum_expiry_seconds);
}

void test_trust_and_ubo_list_classification()
{
    assert(is_trusted_list_id("uboFilters"));
    assert(is_trusted_list_id("uboBadware"));
    assert(is_trusted_list_id("uboPrivacy"));
    assert(is_trusted_list_id("uboUnbreak"));
    assert(is_trusted_list_id("uboQuickFixes"));
    assert(!is_trusted_list_id("easyList"));
    assert(!is_trusted_list_id("easyPrivacy"));
    assert(!is_trusted_list_id("peterLowe"));
    assert(!is_trusted_list_id("urlhaus"));
    assert(!is_trusted_list_id("custom"));

    assert(is_ubo_only_list_id("uboFilters"));
    assert(is_ubo_only_list_id("peterLowe"));
    assert(is_ubo_only_list_id("urlhaus"));
    assert(!is_ubo_only_list_id("easyList"));
    assert(!is_ubo_only_list_id("easyPrivacy"));
}

void test_bundle_serialization_ubo_and_default_modes()
{
    std::vector<ListEntry> lists = {
        { .id = "easyList", .trusted = false, .is_hosts_format = false, .contents = "||ad.example^" },
        { .id = "uboFilters", .trusted = true, .is_hosts_format = false, .contents = "example.com##+js(set, a, 1)" },
        { .id = "bad id\ntrust=1", .trusted = false, .is_hosts_format = true, .contents = "0.0.0.0 tracker.test" },
    };

    auto ubo_bundle = build_bundle_string(true, lists, "##.custom", false, "[{\"name\":\"set.js\"}]");
    assert(ubo_bundle.starts_with("!superbird:meta version=1 mode=ubo\n"));
    assert(ubo_bundle.find("!superbird:list id=easyList trust=0 format=standard bytes=13\n||ad.example^\n") != std::string::npos);
    assert(ubo_bundle.find("!superbird:list id=uboFilters trust=1 format=standard bytes=27\nexample.com##+js(set, a, 1)\n") != std::string::npos);
    // Header ID sanitization prevents whitespace/newline header injection.
    assert(ubo_bundle.find("!superbird:list id=bad_id_trust_1 trust=0 format=hosts bytes=20\n0.0.0.0 tracker.test\n") != std::string::npos);
    assert(ubo_bundle.find("!superbird:list id=custom trust=0 format=standard bytes=9\n##.custom\n") != std::string::npos);
    assert(ubo_bundle.find("!superbird:resources bytes=19\n[{\"name\":\"set.js\"}]\n") != std::string::npos);
    assert(ubo_bundle.ends_with("!superbird:end\n"));

    auto trusted_custom_bundle = build_bundle_string(true, {}, "##+js(trusted-set-cookie)", true, "");
    assert(trusted_custom_bundle.find("!superbird:list id=custom trust=1 format=standard bytes=25\n") != std::string::npos);

    // Default Ladybird mode drops uBO-only lists and writes raw rules without !superbird: headers.
    auto default_bundle = build_bundle_string(false, lists, "##.custom", true, "[{\"name\":\"set.js\"}]");
    assert(default_bundle.find("!superbird:") == std::string::npos);
    assert(default_bundle.find("||ad.example^") != std::string::npos);
    assert(default_bundle.find("example.com##+js(set, a, 1)") == std::string::npos);
    assert(default_bundle.find("##.custom") != std::string::npos);

    // Empty inputs yield empty output in both modes.
    assert(build_bundle_string(true, {}, "", false, "").empty());
    assert(build_bundle_string(false, {}, "", false, "").empty());

    // Per-site cosmetic switch rules
    auto hosts = parse_host_list("Example.COM, , sub.site.org, example.com");
    assert(hosts.size() == 2);
    assert(hosts[0] == "example.com");
    assert(hosts[1] == "sub.site.org");
    assert(serialize_host_list(hosts) == "example.com,sub.site.org");
    auto switches = build_site_switches_rules({ "example.com", "bad host\n##.x" });
    assert(switches == "@@||example.com^$elemhide,generichide\n");
    auto bundle_with_switches = build_bundle_string(true, {}, "##.ad", false, "", switches);
    assert(bundle_with_switches.find("!superbird:list id=site-switches trust=0 format=standard") != std::string::npos);
    assert(bundle_with_switches.find("@@||example.com^$elemhide,generichide\n") != std::string::npos);
}

void test_module_info_and_settings_schema()
{
    auto info = Superbird::content_blocking_module_info();
    assert(info.id == "content_blocking"sv);
    assert(info.enabled_by_default);
    // Naming constraint: never "uBlock Origin".
    std::string name(info.name.characters_without_null_termination(), info.name.length());
    std::string desc(info.description.characters_without_null_termination(), info.description.length());
    assert(name.find("uBlock Origin") == std::string::npos);
    assert(desc.find("uBlock Origin") == std::string::npos);
    assert(name.find("uBO-compatible") != std::string::npos);

    auto schema = Superbird::module_settings_schema_json(info);
    assert(schema.find("\"engine_mode\"") != std::string::npos);
    assert(schema.find("\"default\":\"ubo\"") != std::string::npos);
    assert(schema.find("\"Default Ladybird blocker\"") != std::string::npos);
    assert(schema.find("\"trust_custom_filters\"") != std::string::npos);
    assert(schema.find("\"enable_logger\"") != std::string::npos);
    assert(schema.find("uBlock Origin") == std::string::npos);
}

}

int main()
{
    test_expiry_parsing();
    test_trust_and_ubo_list_classification();
    test_bundle_serialization_ubo_and_default_modes();
    test_module_info_and_settings_schema();
    std::cout << "TestContentBlocking: all assertions passed\n";
    return 0;
}
