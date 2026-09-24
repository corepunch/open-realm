#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "games/warcraft-3/common/minimap.h"

TEST(wc3_minimap, marker_sizes_match_retail_capture_calibration) {
    VECTOR2 const unit = wc3_minimap_marker_size(WC3_MINIMAP_CONTACT_UNIT);
    VECTOR2 const building = wc3_minimap_marker_size(WC3_MINIMAP_CONTACT_BUILDING);
    VECTOR2 const special = wc3_minimap_marker_size(WC3_MINIMAP_CONTACT_GOLD_MINE);
    VECTOR2 const hero = wc3_minimap_marker_size(WC3_MINIMAP_CONTACT_HERO);

    T_FEQ(unit.x, 0.002f, 0.000001f);
    T_FEQ(unit.y, 0.002f, 0.000001f);
    T_FEQ(building.x, 0.005f, 0.000001f);
    T_FEQ(building.y, 0.005f, 0.000001f);
    T_FEQ(special.x, 0.0105f, 0.000001f);
    T_FEQ(special.y, 0.0105f, 0.000001f);
    T_FEQ(hero.x, 0.014f, 0.000001f);
    T_FEQ(hero.y, 0.014f, 0.000001f);
}

TEST(wc3_minimap, ordinary_color_policy_keeps_self_white) {
    wc3MinimapColorParams_t p = {
        .owner = 2,
        .viewer = 2,
        .filter = WC3_MINIMAP_ALLY_COLOR_PLAYERS,
    };

    T_EQ(wc3_minimap_ordinary_color_kind(&p), WC3_MINIMAP_COLOR_SELF_WHITE);
    p.filter = WC3_MINIMAP_ALLY_COLOR_MINIMAP;
    T_EQ(wc3_minimap_ordinary_color_kind(&p), WC3_MINIMAP_COLOR_SELF_WHITE);

    p.owner = 3;
    p.filter = WC3_MINIMAP_ALLY_COLOR_PLAYERS;
    T_EQ(wc3_minimap_ordinary_color_kind(&p), WC3_MINIMAP_COLOR_TEAM);
    p.filter = WC3_MINIMAP_ALLY_COLOR_MINIMAP;
    T_EQ(wc3_minimap_ordinary_color_kind(&p), WC3_MINIMAP_COLOR_ALLY_TEAL);
    p.hostile = true;
    T_EQ(wc3_minimap_ordinary_color_kind(&p), WC3_MINIMAP_COLOR_ENEMY_RED);

    p.owner = PLAYER_NEUTRAL_AGGRESSIVE;
    p.hostile = false;
    T_EQ(wc3_minimap_ordinary_color_kind(&p), WC3_MINIMAP_COLOR_NEUTRAL_BLACK);

    p.owner = 4;
    p.neutral = true;
    T_EQ(wc3_minimap_ordinary_color_kind(&p), WC3_MINIMAP_COLOR_NEUTRAL_BLACK);
}

TEST(wc3_minimap, special_skin_lookup_honors_map_override_then_default) {
    stbIniCache_t theme = { 0 }, map_skin = { 0 };

    T_ASSERT(Stb_IniCacheLoadBuffer(&theme,
        "[Default]\n"
        "MinimapHeroTexture=UI\\Minimap\\minimap-hero.blp\n"
        "MinimapResourceTexture=UI\\Minimap\\minimap-gold.blp\n"));
    T_ASSERT(Stb_IniCacheLoadBuffer(&map_skin,
        "[CustomSkin]\n"
        "MinimapHeroTexture=war3mapImported\\hero.blp\n"));

    T_STREQ(wc3_minimap_skin_key(WC3_MINIMAP_CONTACT_HERO), "MinimapHeroTexture");
    T_STREQ(wc3_minimap_skin_key(WC3_MINIMAP_CONTACT_GOLD_MINE), "MinimapResourceTexture");
    T_STREQ(wc3_minimap_skin_key(WC3_MINIMAP_CONTACT_GOLD_ENTANGLED), "MinimapEntangledResourceTexture");
    T_STREQ(wc3_minimap_skin_key(WC3_MINIMAP_CONTACT_GOLD_HAUNTED), "MinimapHauntedResourceTexture");
    T_STREQ(wc3_minimap_skin_key(WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING), "MinimapNeutralTexture");

    T_STREQ(wc3_minimap_skin_texture_path(&theme, &map_skin, "MinimapHeroTexture"),
            "war3mapImported\\hero.blp");
    T_STREQ(wc3_minimap_skin_texture_path(&theme, &map_skin, "MinimapResourceTexture"),
            "UI\\Minimap\\minimap-gold.blp");
    T_NULL(wc3_minimap_skin_texture_path(&theme, &map_skin, "MissingMinimapTexture"));

    Stb_IniCacheFree(&map_skin);
    Stb_IniCacheFree(&theme);
}
#endif
