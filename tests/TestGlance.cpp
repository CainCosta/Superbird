/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Core/ModuleRegistry.h>
#include <UI/Qt/Superbird/Modules/Glance/GlanceActions.h>
#include <UI/Qt/Superbird/Modules/Glance/GlanceModel.h>

#include <cstdio>
#include <cstdlib>

using namespace Superbird::Glance;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                          \
        }                                                                          \
    } while (0)

static void test_open_and_navigate()
{
    GlanceModel model;
    EXPECT(model.state() == GlanceModel::State::Closed);
    EXPECT(!model.navigated("https://example.com/"));
    EXPECT(!model.title_changed("x"));

    EXPECT(!model.open("javascript:alert(1)", 1));
    EXPECT(!model.open("file:///etc/passwd", 1));
    EXPECT(!model.open("mailto:a@example.com", 1));
    EXPECT(!model.is_open());

    EXPECT(model.open("https://www.example.com/a", 7));
    EXPECT(model.is_open());
    EXPECT(model.opener() == 7);
    EXPECT(model.display_title() == "example.com");
    EXPECT(model.title_changed("Article"));
    EXPECT(model.display_title() == "Article");

    // Navigation inside the sheet stays in the sheet; a new page drops the old title.
    EXPECT(model.navigated("https://other.example/b"));
    EXPECT(model.url() == "https://other.example/b");
    EXPECT(model.display_title() == "other.example");
    EXPECT(model.display_host() == "other.example");

    // Opening another link while open shows it instead, with its own opener.
    EXPECT(model.open("HTTP://example.org/", 9));
    EXPECT(model.url() == "HTTP://example.org/" && model.opener() == 9);
}

static void test_promote_hands_off_latest_page()
{
    GlanceModel model;
    EXPECT(!model.promote().has_value());
    model.open("https://example.com/start", 3);
    model.navigated("https://example.com/end");
    auto promotion = model.promote();
    EXPECT(promotion.has_value());
    EXPECT(promotion->url == "https://example.com/end");
    EXPECT(promotion->opener == 3);
    // Promotion closes the sheet and leaves nothing behind.
    EXPECT(!model.is_open());
    EXPECT(model.url().empty() && model.title().empty() && !model.opener().has_value());
    EXPECT(!model.promote().has_value());
}

static void test_dismiss_cleans_up()
{
    GlanceModel model;
    EXPECT(!model.dismiss());
    model.open("https://example.com/", 1);
    model.title_changed("T");
    EXPECT(model.dismiss());
    EXPECT(!model.is_open());
    EXPECT(model.url().empty() && model.title().empty() && !model.opener().has_value());
    EXPECT(!model.dismiss());
}

static void test_opener_closed()
{
    GlanceModel model;
    model.open("https://example.com/", 1);
    model.opener_closed(2);
    EXPECT(model.opener() == 1);
    model.opener_closed(1);
    EXPECT(!model.opener().has_value());
    EXPECT(model.is_open());
    auto promotion = model.promote();
    EXPECT(promotion.has_value() && !promotion->opener.has_value());
}

static void test_sheet_rect_clamping()
{
    // A roomy area: 80%, centered.
    auto rect = sheet_rect({ 0, 0, 1000, 800 });
    EXPECT(rect == (Rect { 100, 80, 800, 640 }));
    // Offset areas stay offset.
    rect = sheet_rect({ 50, 100, 1000, 800 });
    EXPECT(rect.x == 150 && rect.y == 180);
    // Small areas get the minimum, as long as the margins fit.
    rect = sheet_rect({ 0, 0, 400, 320 });
    EXPECT(rect.width == MINIMUM_SHEET_WIDTH && rect.height == MINIMUM_SHEET_HEIGHT);
    EXPECT(rect.x == 20 && rect.y == 20);
    // Smaller still: whatever fits inside the margins.
    rect = sheet_rect({ 0, 0, 200, 150 });
    EXPECT(rect.width == 200 - 2 * SHEET_MARGIN && rect.height == 150 - 2 * SHEET_MARGIN);
    // Tiny: the whole area. Empty: nothing.
    rect = sheet_rect({ 0, 0, 20, 20 });
    EXPECT(rect == (Rect { 0, 0, 20, 20 }));
    rect = sheet_rect({ 5, 5, 0, 300 });
    EXPECT(rect.width == 0 && rect.height == 0);
    // Resizing follows.
    EXPECT(sheet_rect({ 0, 0, 2000, 1000 }).width == 1600);
}

static void test_url_helpers()
{
    EXPECT(is_glanceable_url("https://a.example"));
    EXPECT(is_glanceable_url("http://a.example"));
    EXPECT(is_glanceable_url("about:blank"));
    EXPECT(!is_glanceable_url("data:text/html,x"));
    EXPECT(!is_glanceable_url(""));
    EXPECT(host_of("https://user@www.example.com:8080/x?y#z") == "example.com:8080");
    EXPECT(host_of("about:blank") == "about:blank");
}

class FakeGlanceHandler final : public Superbird::GlanceCommandHandler {
public:
    virtual bool can_run(Superbird::GlanceCommand) const override { return model.is_open(); }
    virtual void run(Superbird::GlanceCommand command) override
    {
        if (command == Superbird::GlanceCommand::Close)
            model.dismiss();
        else if (command == Superbird::GlanceCommand::OpenAsTab)
            promoted = model.promote();
    }
    GlanceModel model;
    std::optional<GlanceModel::Promotion> promoted;
};

static void test_glance_actions_follow_state()
{
    using namespace Superbird;
    ModuleRegistry registry;
    EXPECT(registry.register_module({ .id = GLANCE_MODULE_ID, .name = "Glance"sv, .description = ""sv, .enabled_by_default = true }));
    FakeGlanceHandler handler;
    register_glance_actions(registry, handler);
    EXPECT(registry.matching_actions().is_empty());
    handler.model.open("https://example.com/", 1);
    EXPECT(registry.matching_actions().size() == GLANCE_ACTIONS.size());
    registry.matching_actions("open as tab"sv).at(0)->trigger();
    EXPECT(handler.promoted.has_value() && handler.promoted->url == "https://example.com/");
    EXPECT(registry.matching_actions().is_empty());
}

int main()
{
    test_glance_actions_follow_state();
    test_open_and_navigate();
    test_promote_hands_off_latest_page();
    test_dismiss_cleans_up();
    test_opener_closed();
    test_sheet_rect_clamping();
    test_url_helpers();
    std::printf("All Glance unit tests passed!\n");
    return 0;
}
