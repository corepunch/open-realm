#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "games/warcraft-3/common/minimap_render.h"

typedef struct { uint32_t pinned, streamed; char path[256]; int pinned_token, streamed_token; } minimapLoadCapture_t;

static void *test_minimap_load_pinned(void *context, cstring_t path) {
    minimapLoadCapture_t *capture = context;
    capture->pinned++; strlcpy(capture->path, path, sizeof(capture->path));
    return &capture->pinned_token;
}

static void *test_minimap_load_streamed(void *context, cstring_t path) {
    minimapLoadCapture_t *capture = context;
    capture->streamed++; strlcpy(capture->path, path, sizeof(capture->path));
    return &capture->streamed_token;
}

TEST(wc3_minimap, marker_sizes_match_retail_capture_calibration) {
    vector2_t const unit = wc3_minimap_marker_size(WC3_MINIMAP_CONTACT_UNIT);
    vector2_t const building = wc3_minimap_marker_size(WC3_MINIMAP_CONTACT_BUILDING);
    vector2_t const special = wc3_minimap_marker_size(WC3_MINIMAP_CONTACT_GOLD_MINE);
    vector2_t const hero = wc3_minimap_marker_size(WC3_MINIMAP_CONTACT_HERO);

    T_FEQ(unit.x, 0.002f, 0.000001f);
    T_FEQ(unit.y, 0.002f, 0.000001f);
    T_FEQ(building.x, 0.005f, 0.000001f);
    T_FEQ(building.y, 0.005f, 0.000001f);
    T_FEQ(special.x, 0.0105f, 0.000001f);
    T_FEQ(special.y, 0.0105f, 0.000001f);
    T_FEQ(hero.x, 0.014f, 0.000001f);
    T_FEQ(hero.y, 0.014f, 0.000001f);

    rect_t const marker = wc3_minimap_marker_rect(&(vector2_t){ 0.5f, 0.25f }, WC3_MINIMAP_CONTACT_HERO);
    T_FEQ(marker.x, 0.493f, 0.000001f);
    T_FEQ(marker.y, 0.243f, 0.000001f);
    T_FEQ(marker.w, 0.014f, 0.000001f);
    T_FEQ(marker.h, 0.014f, 0.000001f);
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

    p.owner = PLAYER_NEUTRAL_PASSIVE;
    p.hostile = true;
    T_EQ(wc3_minimap_ordinary_color_kind(&p), WC3_MINIMAP_COLOR_NEUTRAL_BLACK);
}

TEST(wc3_minimap, passive_ally_customization_keeps_minimap_ally_color) {
    entityState_t state = { .number = 17, .model = 1 };
    edict_t ally = { .svflags = SVF_MONSTER, .s = { .player = 1 } };
    wc3MinimapColorParams_t params = { .owner = 1, .viewer = 0,
        .filter = WC3_MINIMAP_ALLY_COLOR_MINIMAP };

    game.clients[0].ps.number = 0;
    game.clients[1].ps.number = 1;
    G_SetPlayerAlliance(&game.clients[0].ps, &game.clients[1].ps, ALLIANCE_PASSIVE, true);
    ally.health.value = 100.0f;
    T_EQ(G_SelectionRelation(0, &ally), SELECT_RELATION_NEUTRAL);
    globals.CustomizeEntity(0, &ally, &state);
    params.hostile = (state.flags & EF_HOSTILE) != 0;
    T_ASSERT(state.flags & EF_NEUTRAL); /* selection semantics remain intact */
    T_EQ(wc3_minimap_ordinary_color_kind(&params), WC3_MINIMAP_COLOR_ALLY_TEAL);

    state.flags = EF_NOT_SELECTABLE;
    globals.CustomizeEntity(0, &ally, &state);
    T_ASSERT(!(state.flags & EF_NEUTRAL));
    params.hostile = (state.flags & EF_HOSTILE) != 0;
    T_EQ(wc3_minimap_ordinary_color_kind(&params), WC3_MINIMAP_COLOR_ALLY_TEAL);

    ally.s.player = 2;
    state.flags = 0;
    params.owner = 2;
    globals.CustomizeEntity(0, &ally, &state);
    params.hostile = (state.flags & EF_HOSTILE) != 0;
    T_EQ(wc3_minimap_ordinary_color_kind(&params), WC3_MINIMAP_COLOR_ENEMY_RED);

    ally.s.player = PLAYER_NEUTRAL_PASSIVE;
    state.flags = 0;
    params.owner = PLAYER_NEUTRAL_PASSIVE;
    globals.CustomizeEntity(0, &ally, &state);
    params.hostile = (state.flags & EF_HOSTILE) != 0;
    T_EQ(wc3_minimap_ordinary_color_kind(&params), WC3_MINIMAP_COLOR_NEUTRAL_BLACK);

    ally.s.player = 0;
    state.flags = 0;
    params.owner = 0;
    globals.CustomizeEntity(0, &ally, &state);
    params.hostile = (state.flags & EF_HOSTILE) != 0;
    T_EQ(wc3_minimap_ordinary_color_kind(&params), WC3_MINIMAP_COLOR_SELF_WHITE);
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

TEST(wc3_minimap, fixture_mpq_skin_override_wins_over_stock_default) {
    handle_t archive = NULL, bytes = NULL, texture_bytes = NULL;
    uint32_t size = 0, texture_size = 0;
    char saved_prefix[sizeof(game.data_prefix)];
    stbIniCache_t theme = { 0 }, map_skin = { 0 };
    wc3MinimapSpecialAsset_t assets[5];
    minimapLoadCapture_t capture = { 0 };
    uint32_t count;

    strlcpy(saved_prefix, game.data_prefix, sizeof(saved_prefix));
    game.data_prefix[0] = '\0';
    bytes = gi.ReadFile("Maps\\MapOverlay.w3x", &size);
    T_NOT_NULL(bytes);
    if (!bytes) goto done;
    T_ASSERT(SFileOpenArchiveFromMemory(bytes, size, 0, &archive));
    if (!archive) { gi.MemFree(bytes); goto done; }
    gi.SetPriorityArchive(archive);
    T_ASSERT(Stb_IniCacheLoad(&theme, "UI\\war3skins.txt"));
    T_ASSERT(Stb_IniCacheLoad(&map_skin, "war3mapSkin.txt"));
    T_STREQ(wc3_minimap_skin_texture_path(&theme, NULL, "MinimapHeroTexture"),
            "TestUI\\Textures\\solid_white.blp");
    count = wc3_minimap_special_assets(&theme, &map_skin, assets, 5);
    T_EQ(count, 5);
    T_STREQ(assets[0].path, "Textures\\minimap_hero.blp");
    T_ASSERT(assets[0].map_override);
    T_STREQ(assets[1].path, "TestUI\\Textures\\solid_white.blp");
    T_ASSERT(!assets[1].map_override);
    T_ASSERT(wc3_minimap_register_special_asset(&assets[0], NULL, &capture,
        test_minimap_load_pinned, test_minimap_load_streamed) == &capture.streamed_token);
    T_ASSERT(wc3_minimap_register_special_asset(&assets[1], NULL, &capture,
        test_minimap_load_pinned, test_minimap_load_streamed) == &capture.pinned_token);
    T_EQ(capture.streamed, 1);
    T_EQ(capture.pinned, 1);
    T_STREQ(capture.path, "TestUI\\Textures\\solid_white.blp");
    {
        int placeholder;
        wc3MinimapSpecialAsset_t missing = { WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING,
            "MinimapNeutralTexture", NULL, false };
        T_ASSERT(wc3_minimap_register_special_asset(&missing, &placeholder, &capture,
            test_minimap_load_pinned, test_minimap_load_streamed) == &placeholder);
        T_EQ(capture.pinned, 1);
        T_EQ(capture.streamed, 1);
    }
    texture_bytes = gi.ReadFile(assets[0].path, &texture_size);
    T_NOT_NULL(texture_bytes);

    Stb_IniCacheFree(&map_skin);
    Stb_IniCacheFree(&theme);
    gi.SetPriorityArchive(NULL);
    SFileCloseArchive(archive);
    gi.MemFree(bytes);
    if (texture_bytes) gi.MemFree(texture_bytes);
done:
    strlcpy(game.data_prefix, saved_prefix, sizeof(game.data_prefix));
}

TEST(wc3_minimap, invalid_optional_skin_leaves_stock_default_available) {
    stbIniCache_t theme = { 0 }, invalid_map_skin = { 0 };
    wc3MinimapSpecialAsset_t assets[5];
    minimapLoadCapture_t capture = { 0 };
    uint32_t count;

    T_ASSERT(Stb_IniCacheLoad(&theme, "UI\\war3skins.txt"));
    T_ASSERT(!Stb_IniCacheLoadBuffer(&invalid_map_skin, "invalid text without a section\n"));
    count = wc3_minimap_special_assets(&theme, &invalid_map_skin, assets, 5);
    T_EQ(count, 5);
    FOR_LOOP(i, count) T_ASSERT(!assets[i].map_override);
    T_STREQ(assets[0].path, "TestUI\\Textures\\solid_white.blp");
    T_ASSERT(wc3_minimap_register_special_asset(&assets[0], NULL, &capture,
        test_minimap_load_pinned, test_minimap_load_streamed) == &capture.pinned_token);
    T_EQ(capture.pinned, 1);
    T_EQ(capture.streamed, 0);
    Stb_IniCacheFree(&invalid_map_skin);
    Stb_IniCacheFree(&theme);
}
#endif
