#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "games/warcraft-3/common/area_preview.h"

TEST(wc3_area_preview, circle_intersection_includes_edge_and_selection_radius) {
    vec2_t center = {100.0f, 100.0f};
    vec2_t point = {240.0f, 100.0f};
    T_ASSERT(WC3_AreaPreviewIntersects(&center, 100.0f, &point, 40.0f));
    point.x = 241.0f;
    T_ASSERT(!WC3_AreaPreviewIntersects(&center, 100.0f, &point, 40.0f));
    point.x = 120.0f;
    T_ASSERT(WC3_AreaPreviewIntersects(&center, 100.0f, &point, 0.0f));
    T_ASSERT(!WC3_AreaPreviewIntersects(&center, 0.0f, &point, 40.0f));
    T_ASSERT(!WC3_AreaPreviewIntersects(&center, -1.0f, &point, 40.0f));
}

TEST(wc3_area_preview, tint_preserves_alpha_and_base_tint) {
    color32_t original = {200, 100, 80, 51};
    color32_t tinted = WC3_AreaPreviewGreen(original);
    T_EQ(tinted.r, 50);
    T_EQ(tinted.g, 216);
    T_EQ(tinted.b, 20);
    T_EQ(tinted.a, 51);
    T_EQ(original.r, 200);
    T_EQ(original.a, 51);
    tinted = WC3_AreaPreviewGreen((color32_t){ 255, 255, 255, 255 });
    T_EQ(tinted.r, 63);
    T_EQ(tinted.g, 255);
    T_EQ(tinted.b, 63);
    T_EQ(tinted.a, 255);
    tinted = WC3_AreaPreviewGreen((color32_t){ 255, 0, 0, 100 });
    T_ASSERT(tinted.g > tinted.r); /* a fully red scripted tint still previews green */
    T_EQ(tinted.a, 100);
}

TEST(wc3_area_preview, caster_race_and_custom_skin_override) {
    stbIniCache_t old_theme = game.config.theme, old_skin = game.config.map_skin;
    game.config.theme = (stbIniCache_t){0};
    game.config.map_skin = (stbIniCache_t){0};
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.theme,
        "[Default]\nPlacementCursor=Default.blp\n"
        "[Human]\nPlacementCursor=Human.blp\n"
        "[Orc]\nPlacementCursor=Orc.blp\n"
        "[Undead]\nPlacementCursor=Undead.blp\n"
        "[NightElf]\nPlacementCursor=NightElf.blp\n"));
    T_STREQ(Theme_UnitString("human", "PlacementCursor", "Fallback.blp"), "Human.blp");
    T_STREQ(Theme_UnitString("orc", "PlacementCursor", "Fallback.blp"), "Orc.blp");
    T_STREQ(Theme_UnitString("undead", "PlacementCursor", "Fallback.blp"), "Undead.blp");
    T_STREQ(Theme_UnitString("nightelf", "PlacementCursor", "Fallback.blp"), "NightElf.blp");
    T_STREQ(Theme_UnitString("naga", "PlacementCursor", "Fallback.blp"), "Default.blp");
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.map_skin,
        "[CustomSkin]\nPlacementCursor=Custom.blp\n"));
    T_STREQ(Theme_UnitString("orc", "PlacementCursor", "Fallback.blp"), "Custom.blp");
    Stb_IniCacheFree(&game.config.map_skin);
    Stb_IniCacheFree(&game.config.theme);
    game.config.map_skin = old_skin;
    game.config.theme = old_theme;
}
#endif
