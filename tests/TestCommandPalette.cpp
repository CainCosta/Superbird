/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashTable.h>
#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/CommandPaletteActions.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/FuzzyMatcher.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/MathExpression.h>
#include <UI/Qt/Superbird/Modules/CommandPalette/PaletteRanking.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace Superbird;
using namespace Superbird::Palette;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                          \
        }                                                                          \
    } while (0)

static int score_of(std::string_view query, std::string_view candidate)
{
    auto match = fuzzy_match(query, candidate);
    EXPECT(match.has_value());
    return match->score;
}

// ---- Fuzzy matcher ----

static void test_fuzzy_requires_in_order_subsequence()
{
    EXPECT(fuzzy_match("abc", "a big cat").has_value());
    EXPECT(!fuzzy_match("abc", "cab").has_value());
    EXPECT(!fuzzy_match("xyz", "New Tab").has_value());
    EXPECT(!fuzzy_match("longer than candidate", "short").has_value());
}

static void test_fuzzy_is_case_insensitive()
{
    EXPECT(fuzzy_match("NEW TAB", "new tab").has_value());
    EXPECT(fuzzy_match("settings", "Open Settings").has_value());
}

static void test_fuzzy_empty_query_matches_everything()
{
    auto match = fuzzy_match("", "anything");
    EXPECT(match.has_value());
    EXPECT(match->score == 0);
    EXPECT(match->positions.empty());
    EXPECT(fuzzy_match("   ", "anything").has_value());
}

static void test_fuzzy_reports_positions()
{
    auto match = fuzzy_match("nt", "New Tab");
    EXPECT(match.has_value());
    // Prefers the word-boundary 'T' over the earlier lowercase 't' that doesn't exist here, and the start 'N'.
    EXPECT(match->positions.size() == 2);
    EXPECT(match->positions[0] == 0);
    EXPECT(match->positions[1] == 4);
}

static void test_fuzzy_prefers_prefix_boundaries_and_runs()
{
    // Prefix beats middle.
    EXPECT(score_of("set", "Settings") > score_of("set", "Reset Zoom"));
    // Contiguous run beats scattered letters.
    EXPECT(score_of("tab", "New Tab") > score_of("tab", "Turnip And Beets"));
    // Word-boundary initials beat letters buried inside words.
    EXPECT(score_of("cb", "Close Branch") > score_of("cb", "ascribe"));
    // camelCase humps count as boundaries.
    EXPECT(score_of("fd", "forceDark") > score_of("fd", "fiddle"));
}

static void test_fuzzy_multi_token_any_order()
{
    EXPECT(fuzzy_match("branch close", "Close Current Branch").has_value());
    EXPECT(fuzzy_match("unl br", "Unload Current Branch").has_value());
    EXPECT(!fuzzy_match("unl zz", "Unload Current Branch").has_value());
    auto match = fuzzy_match("tab new", "New Tab");
    EXPECT(match.has_value());
    EXPECT(match->positions.size() == 6);
}

static void test_fuzzy_caps_long_candidates()
{
    std::string long_candidate(FUZZY_MAX_CANDIDATE_LENGTH + 100, 'a');
    long_candidate.append("zebra");
    EXPECT(!fuzzy_match("zebra", long_candidate).has_value());
    EXPECT(fuzzy_match("aaa", long_candidate).has_value());
}

static void test_fuzzy_prefers_tighter_match()
{
    // Same letters, but the tighter cluster should score better.
    EXPECT(score_of("abc", "xx abc xx") > score_of("abc", "a x x x x b x x x x c"));
}

// ---- Math ----

static bool approx(double a, double b)
{
    return std::fabs(a - b) < 1e-9 * std::fmax(1.0, std::fabs(b));
}

static double eval_ok(std::string_view expression)
{
    auto result = evaluate_math_expression(expression);
    if (!result.ok())
        std::fprintf(stderr, "unexpected error for '%.*s': %s\n", static_cast<int>(expression.size()), expression.data(), result.error.c_str());
    EXPECT(result.ok());
    return *result.value;
}

static void test_math_precedence_and_associativity()
{
    EXPECT(approx(eval_ok("1 + 2 * 3"), 7));
    EXPECT(approx(eval_ok("(1 + 2) * 3"), 9));
    EXPECT(approx(eval_ok("10 - 4 - 3"), 3));
    EXPECT(approx(eval_ok("100 / 10 / 5"), 2));
    EXPECT(approx(eval_ok("2 ^ 3 ^ 2"), 512));
    EXPECT(approx(eval_ok("-2 ^ 2"), -4));
    EXPECT(approx(eval_ok("2 ^ -1"), 0.5));
    EXPECT(approx(eval_ok("--3"), 3));
    EXPECT(approx(eval_ok("+4"), 4));
    EXPECT(approx(eval_ok("7 % 3"), 1));
    EXPECT(approx(eval_ok("128 * 1.15"), 147.2));
    EXPECT(approx(eval_ok("6 \xC3\x97 7"), 42));
    EXPECT(approx(eval_ok("1 \xC3\xB7 4"), 0.25));
}

static void test_math_numbers()
{
    EXPECT(approx(eval_ok(".5"), 0.5));
    EXPECT(approx(eval_ok("2e3"), 2000));
    EXPECT(approx(eval_ok("1.5E-2"), 0.015));
    EXPECT(approx(eval_ok("0.1 + 0.2"), 0.3));
    EXPECT(approx(eval_ok("12345678901234567890"), 12345678901234567890.0));
}

static void test_math_functions_and_constants()
{
    EXPECT(approx(eval_ok("sqrt(16)"), 4));
    EXPECT(approx(eval_ok("abs(-3.5)"), 3.5));
    EXPECT(approx(eval_ok("round(2.5) + floor(2.9) + ceil(2.1)"), 3 + 2 + 3));
    EXPECT(approx(eval_ok("log(1000)"), 3));
    EXPECT(approx(eval_ok("ln(e)"), 1));
    EXPECT(approx(eval_ok("cos(0) + sin(0)"), 1));
    EXPECT(approx(eval_ok("2 * pi"), 2 * 3.14159265358979323846));
}

static void test_math_errors()
{
    EXPECT(!evaluate_math_expression("").ok());
    EXPECT(!evaluate_math_expression("1 +").ok());
    EXPECT(!evaluate_math_expression("(1 + 2").ok());
    EXPECT(!evaluate_math_expression("1 + 2)").ok());
    EXPECT(!evaluate_math_expression("1 / 0").ok());
    EXPECT(!evaluate_math_expression("5 % 0").ok());
    EXPECT(!evaluate_math_expression("foo(2)").ok());
    EXPECT(!evaluate_math_expression("sqrt 4").ok());
    EXPECT(!evaluate_math_expression("sqrt(-1)").ok());
    EXPECT(!evaluate_math_expression("10 ^ 400").ok());
    EXPECT(!evaluate_math_expression("2e").ok());
    EXPECT(!evaluate_math_expression("1,5").ok());
    EXPECT(!evaluate_math_expression("1 2").ok());
    EXPECT(evaluate_math_expression("1 / 0").error == "Division by zero");

    // Deep nesting is refused rather than overflowing the stack.
    std::string deep(500, '(');
    deep += "1";
    deep += std::string(500, ')');
    EXPECT(!evaluate_math_expression(deep).ok());
    std::string unary(500, '-');
    unary += "1";
    EXPECT(!evaluate_math_expression(unary).ok());
}

static void test_math_formatting()
{
    EXPECT(format_math_result(42) == "42");
    EXPECT(format_math_result(-7) == "-7");
    EXPECT(format_math_result(0) == "0");
    EXPECT(format_math_result(-0.0) == "0");
    EXPECT(format_math_result(147.2) == "147.2");
    EXPECT(format_math_result(0.1 + 0.2) == "0.3");
    EXPECT(format_math_result(1.0 / 3.0) == "0.333333333333");
    EXPECT(format_math_result(1e20) == "1e+20");
    EXPECT(format_math_result(2.5e-7) == "2.5e-07");
}

// ---- Query parsing & URL detection ----

static void test_parse_query()
{
    auto math = parse_query("  = 128 * 1.15 ");
    EXPECT(math.mode == QueryMode::Math);
    EXPECT(math.text == "128 * 1.15");
    auto search = parse_query("  new tab ");
    EXPECT(search.mode == QueryMode::Search);
    EXPECT(search.text == "new tab");
    EXPECT(parse_query("").text.empty());
}

static void test_looks_like_url()
{
    EXPECT(looks_like_url("example.com"));
    EXPECT(looks_like_url("news.ycombinator.com/item?id=1"));
    EXPECT(looks_like_url("https://ladybird.org"));
    EXPECT(looks_like_url("about:settings"));
    EXPECT(looks_like_url("localhost:8080"));
    EXPECT(looks_like_url("localhost"));
    EXPECT(looks_like_url("192.168.0.1"));
    EXPECT(looks_like_url("192.168.0.1:3000/path"));
    EXPECT(!looks_like_url("new tab"));
    EXPECT(!looks_like_url("settings"));
    EXPECT(!looks_like_url("1.5"));
    EXPECT(!looks_like_url("1.2.3"));
    EXPECT(!looks_like_url("example."));
    EXPECT(!looks_like_url(".com"));
    EXPECT(!looks_like_url("about:"));
    EXPECT(!looks_like_url("example.com:abc"));
    EXPECT(!looks_like_url(""));
}

// ---- Ranking ----

static Entry entry(EntryKind kind, std::string title, std::string subtitle = {}, std::string key = {})
{
    return { kind, std::move(title), std::move(subtitle), std::move(key) };
}

static void test_rank_empty_query_groups_by_kind_in_given_order()
{
    std::vector<Entry> entries {
        entry(EntryKind::History, "H1"),
        entry(EntryKind::Action, "A1"),
        entry(EntryKind::Tab, "T1"),
        entry(EntryKind::Bookmark, "B1"),
        entry(EntryKind::Tab, "T2"),
        entry(EntryKind::Action, "A2"),
    };
    auto ranked = rank_entries("", entries, 100);
    EXPECT(ranked.size() == entries.size());
    EXPECT(entries[ranked[0].index].title == "T1");
    EXPECT(entries[ranked[1].index].title == "T2");
    EXPECT(entries[ranked[2].index].title == "A1");
    EXPECT(entries[ranked[3].index].title == "A2");
    EXPECT(entries[ranked[4].index].title == "B1");
    EXPECT(entries[ranked[5].index].title == "H1");
}

static void test_rank_filters_and_orders_by_score()
{
    std::vector<Entry> entries {
        entry(EntryKind::Action, "Toggle Force Dark", "Browser"),
        entry(EntryKind::Action, "Open Settings", "Browser"),
        entry(EntryKind::Tab, "Settings - Superbird", "about:settings", "about:settings"),
        entry(EntryKind::History, "Reset your settings", "https://example.com/reset", "https://example.com/reset"),
    };
    auto ranked = rank_entries("settings", entries, 10);
    EXPECT(ranked.size() == 3);
    // The tab wins over the action thanks to its kind weight with an equal prefix match.
    EXPECT(entries[ranked[0].index].kind == EntryKind::Tab);
    EXPECT(entries[ranked[1].index].title == "Open Settings");
    EXPECT(entries[ranked[2].index].kind == EntryKind::History);
    EXPECT(!ranked[0].title_positions.empty());
}

static void test_rank_matches_subtitle_for_less()
{
    std::vector<Entry> entries {
        entry(EntryKind::Bookmark, "My favourite site", "https://ladybird.org/", "https://ladybird.org/"),
        entry(EntryKind::Bookmark, "Ladybird", "https://example.com/", "https://example.com/"),
    };
    auto ranked = rank_entries("ladybird", entries, 10);
    EXPECT(ranked.size() == 2);
    EXPECT(entries[ranked[0].index].title == "Ladybird");
    EXPECT(ranked[1].title_positions.empty());
}

static void test_rank_ties_break_on_kind_then_length_then_order()
{
    std::vector<Entry> entries {
        entry(EntryKind::Bookmark, "Docs Long Title"),
        entry(EntryKind::Bookmark, "Docs"),
        entry(EntryKind::Bookmark, "Docs"),
    };
    auto ranked = rank_entries("docs", entries, 10);
    EXPECT(ranked.size() == 3);
    EXPECT(ranked[0].index == 1);
    EXPECT(ranked[1].index == 2);
    EXPECT(ranked[2].index == 0);
}

static void test_rank_special_entries()
{
    std::vector<Entry> entries {
        entry(EntryKind::Action, "Example Action"),
        entry(EntryKind::Navigate, "Go to example.com", {}, "example.com"),
        entry(EntryKind::Calculator, "= 4"),
    };
    auto as_address = rank_entries("example.com", entries, 10);
    EXPECT(entries[as_address[0].index].kind == EntryKind::Calculator);
    EXPECT(entries[as_address[1].index].kind == EntryKind::Navigate);

    std::vector<Entry> search_entries {
        entry(EntryKind::Navigate, "Search for example", {}, "example"),
        entry(EntryKind::Action, "Example Action"),
    };
    auto as_search = rank_entries("example", search_entries, 10);
    EXPECT(as_search.size() == 2);
    EXPECT(search_entries[as_search[0].index].kind == EntryKind::Action);
    EXPECT(search_entries[as_search[1].index].kind == EntryKind::Navigate);
}

static void test_rank_limit_keeps_navigate()
{
    std::vector<Entry> entries;
    for (int i = 0; i < 20; ++i)
        entries.push_back(entry(EntryKind::Tab, "tab " + std::to_string(i)));
    entries.push_back(entry(EntryKind::Navigate, "Search for tab", {}, "tab"));
    auto ranked = rank_entries("tab", entries, 5);
    EXPECT(ranked.size() == 5);
    EXPECT(entries[ranked.back().index].kind == EntryKind::Navigate);
    EXPECT(entries[ranked[0].index].kind == EntryKind::Tab);

    EXPECT(rank_entries("tab", entries, 0).empty());
}

static void test_remove_duplicate_pages()
{
    std::vector<Entry> entries {
        entry(EntryKind::Tab, "Open", {}, "https://a.example/"),
        entry(EntryKind::Bookmark, "Bookmark of open", {}, "https://a.example"),
        entry(EntryKind::Bookmark, "Other bookmark", {}, "https://b.example/page"),
        entry(EntryKind::Bookmark, "Duplicate bookmark", {}, "https://b.example/page#section"),
        entry(EntryKind::History, "History of bookmark", {}, "https://b.example/page/"),
        entry(EntryKind::History, "History of open", {}, "https://a.example/#top"),
        entry(EntryKind::History, "Unique history", {}, "https://c.example/"),
        entry(EntryKind::History, "Unique history again", {}, "https://c.example"),
        entry(EntryKind::Action, "Action", {}, "browser.new_tab"),
    };
    remove_duplicate_pages(entries);
    EXPECT(entries.size() == 4);
    EXPECT(entries[0].title == "Open");
    EXPECT(entries[1].title == "Other bookmark");
    EXPECT(entries[2].title == "Unique history");
    EXPECT(entries[3].title == "Action");
}

// ---- Actions ----

class FakeBrowserHandler final : public BrowserCommandHandler {
public:
    virtual bool can_run(BrowserCommand command) const override { return !unavailable.contains(static_cast<u8>(command)); }
    virtual void run(BrowserCommand command) override { ran.append(command); }

    HashTable<u8> unavailable;
    Vector<BrowserCommand> ran;
};

static void test_browser_action_table_is_well_formed()
{
    HashTable<StringView> ids;
    HashTable<u8> commands;
    for (auto const& definition : BROWSER_ACTIONS) {
        EXPECT(definition.id.length() > 8 && definition.id.substring_view(0, 8) == "browser."sv);
        EXPECT(!definition.title.is_empty());
        EXPECT(ids.set(definition.id) == HashSetResult::InsertedNewEntry);
        EXPECT(commands.set(static_cast<u8>(definition.command)) == HashSetResult::InsertedNewEntry);
    }
    EXPECT(commands.size() == static_cast<size_t>(BrowserCommand::OpenSettings) + 1);
    EXPECT(ids.contains("browser.toggle_force_dark"sv));
    EXPECT(ids.contains("browser.open_settings"sv));
}

static void register_module(ModuleRegistry& registry, StringView id, bool enabled_by_default = true)
{
    EXPECT(registry.register_module({ .id = id, .name = id, .description = ""sv, .enabled_by_default = enabled_by_default }));
}

static void test_action_entries_follow_module_state_and_availability()
{
    ModuleRegistry registry;
    register_module(registry, COMMAND_PALETTE_MODULE_ID);
    register_module(registry, "other"sv);

    FakeBrowserHandler handler;
    register_browser_actions(registry, handler);
    bool other_available = true;
    registry.register_action({ .id = "other.thing"sv, .module_id = "other"sv, .title = "Other Thing"sv, .category = "Other"sv, .is_available = [&] { return other_available; }, .trigger = [] {} });
    registry.register_action({ .id = "global.thing"sv, .module_id = ""sv, .title = "Global Thing"sv, .category = "Global"sv, .is_available = {}, .trigger = [] {} });

    auto entries = collect_action_entries(registry);
    EXPECT(entries.size() == BROWSER_ACTIONS.size() + 2);
    for (auto const& e : entries)
        EXPECT(e.kind == EntryKind::Action && !e.key.empty() && !e.title.empty());

    // Unavailable actions are not offered.
    handler.unavailable.set(static_cast<u8>(BrowserCommand::CloseTab));
    other_available = false;
    entries = collect_action_entries(registry);
    EXPECT(entries.size() == BROWSER_ACTIONS.size() - 1 + 1);

    // Disabling a module hides its actions; module-less actions stay.
    registry.set_enabled(COMMAND_PALETTE_MODULE_ID, false);
    entries = collect_action_entries(registry);
    EXPECT(entries.size() == 1);
    EXPECT(entries[0].key == "global.thing");
}

static void test_trigger_action_runs_only_offered_actions()
{
    ModuleRegistry registry;
    register_module(registry, COMMAND_PALETTE_MODULE_ID);
    FakeBrowserHandler handler;
    register_browser_actions(registry, handler);

    EXPECT(trigger_action(registry, "browser.toggle_force_dark"));
    EXPECT(handler.ran.size() == 1 && handler.ran[0] == BrowserCommand::ToggleForceDark);

    EXPECT(!trigger_action(registry, "browser.nope"));

    handler.unavailable.set(static_cast<u8>(BrowserCommand::Reload));
    EXPECT(!trigger_action(registry, "browser.reload"));

    registry.set_enabled(COMMAND_PALETTE_MODULE_ID, false);
    EXPECT(!trigger_action(registry, "browser.new_tab"));
    EXPECT(handler.ran.size() == 1);
}

static void test_fuzzy_ranking_over_actions()
{
    ModuleRegistry registry;
    register_module(registry, COMMAND_PALETTE_MODULE_ID);
    FakeBrowserHandler handler;
    register_browser_actions(registry, handler);
    auto entries = collect_action_entries(registry);

    auto ranked = rank_entries("dark", entries, 5);
    EXPECT(!ranked.empty());
    EXPECT(entries[ranked[0].index].key == "browser.toggle_force_dark");

    ranked = rank_entries("nt", entries, 5);
    EXPECT(!ranked.empty());
    EXPECT(entries[ranked[0].index].key == "browser.new_tab");
}

int main()
{
    test_fuzzy_requires_in_order_subsequence();
    test_fuzzy_is_case_insensitive();
    test_fuzzy_empty_query_matches_everything();
    test_fuzzy_reports_positions();
    test_fuzzy_prefers_prefix_boundaries_and_runs();
    test_fuzzy_multi_token_any_order();
    test_fuzzy_caps_long_candidates();
    test_fuzzy_prefers_tighter_match();

    test_math_precedence_and_associativity();
    test_math_numbers();
    test_math_functions_and_constants();
    test_math_errors();
    test_math_formatting();

    test_parse_query();
    test_looks_like_url();

    test_rank_empty_query_groups_by_kind_in_given_order();
    test_rank_filters_and_orders_by_score();
    test_rank_matches_subtitle_for_less();
    test_rank_ties_break_on_kind_then_length_then_order();
    test_rank_special_entries();
    test_rank_limit_keeps_navigate();
    test_remove_duplicate_pages();

    test_browser_action_table_is_well_formed();
    test_action_entries_follow_module_state_and_availability();
    test_trigger_action_runs_only_offered_actions();
    test_fuzzy_ranking_over_actions();

    std::printf("All CommandPalette unit tests passed!\n");
    return 0;
}
