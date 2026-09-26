/* Loading must survive the real FDF loader and the initial-layout serialization path. */
#ifdef BZ_TESTS
#include "shared/test.h"
#include "../hud/hud_local.h"

typedef struct {
    uint32_t sprites, bar, back, texts, sent, bytes, previews, names, rows;
    uiFrame_t preview;
    int32_t opcode, layer;
    uiFrame_t progress;
    char anim[32];
} loadCap_t;
static loadCap_t loadcap;

/* Capture what a connecting client receives, rather than inspecting manually constructed frames. */
static void loading_write(pfWriteType_t type, void const *data) {
    if (type == PF_BYTE) {
        if (!loadcap.bytes++) loadcap.opcode = *(int32_t const *)data;
        else loadcap.layer = *(int32_t const *)data;
    }
    if (type != PF_UIFRAME) return;
    uiFrame_t const * frame = data;
    if (frame->flags.type == FT_SPRITE) {
        loadcap.sprites++;
        if (frame->stat == UI_STAT_LOADING_PROGRESS) {
            loadcap.bar = frame->tex.index;
            loadcap.progress = *frame;
            T_STREQ(frame->text, "#0");
        } else {
            loadcap.back = frame->tex.index;
            strlcpy(loadcap.anim, frame->text, sizeof(loadcap.anim));
        }
    }
    if (frame->flags.type == FT_BACKDROP) loadcap.rows++;
    if (frame->flags.type == FT_MINIMAP) { loadcap.previews++; loadcap.preview = *frame; }
    if (frame->flags.type == FT_TEXT && frame->text) {
        if (!strcmp(frame->text, "Alice") || !strcmp(frame->text, "Computer (Normal)")) loadcap.names++;
        if (!strcmp(frame->text, "Chapter")) loadcap.texts |= 1;
        if (!strcmp(frame->text, "Subtitle")) loadcap.texts |= 2;
        if (!strcmp(frame->text, "Description")) loadcap.texts |= 4;
    }
}

static void loading_unicast(edict_t * ent) { (void)ent; loadcap.sent++; }

/* tests.mpq carries the native FDF plus ROC/TFT WorldEditData rows and decorated skin keys. */
TEST(wc3_loading, initial_layout_resolves_campaign_custom_and_melee_art) {
    static const struct { uint32_t row; string_t custom; cstring_t model, anim; } cases[] = {
        { 0, NULL, "UI\\Glues\\Loading\\Backgrounds\\Campaigns\\LordaeronBackground.mdx", "#!0" },
        { 1, NULL, "UI\\Glues\\Loading\\Backgrounds\\Campaigns\\LordaeronExpansionBackground.mdx", "#!6" },
        { 1, "war3mapImported\\LoadingScreen.mdx", "war3mapImported\\LoadingScreen.mdx", "#!0" },
        { (uint32_t)-1, NULL, "UI\\Glues\\Loading\\Multiplayer\\Load-Multiplayer-Random.mdx", "#!0" },
    };
    __typeof__(gi.Write) old_write = gi.Write;
    __typeof__(gi.unicast) old_send = gi.unicast;
    mapInfo_t const * old_info = level.mapinfo;
    mapInfo_t info = { .mapName = "Chapter", .loadingScreenTitle = "Chapter",
                    .loadingScreenSubtitle = "Subtitle", .loadingScreenText = "Description" };

    info.players[0] = (mapPlayer_t){ .used = true, .playerType = kPlayerTypeHuman, .playerName = "Alice" };
    info.players[2] = (mapPlayer_t){ .used = true, .playerType = kPlayerTypeComputer, .playerName = "Computer (Normal)" };
    info.players[3] = (mapPlayer_t){ .used = true, .playerType = kPlayerTypeNone, .playerName = "Closed" };
    info.players[12] = (mapPlayer_t){ .used = true, .playerType = kPlayerTypeNeutral, .playerName = "Neutral" };
    UI_ResetHud(); UI_LoadHudLoading();
    T_NOT_NULL(hud.loading.Loading); T_NOT_NULL(hud.loading.LoadingBar);
    T_NOT_NULL(hud.loading.LoadingBackground);
    if (!hud.loading.Loading || !hud.loading.LoadingBar || !hud.loading.LoadingBackground) return;
    gi.Write = loading_write; gi.unicast = loading_unicast; level.mapinfo = &info;
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        info.campaignBackgroundNumber = cases[i].row;
        info.loadingScreenModel = cases[i].custom;
        /* Empty title must resolve the authored map name; custom art must override the campaign row. */
        info.loadingScreenTitle = i == 3 ? "" : "Chapter";
        memset(&loadcap, 0, sizeof(loadcap));
        UI_WriteLoadingLayout(g_edicts, &info);
        T_EQ(loadcap.opcode, svc_layout); T_EQ(loadcap.layer, LAYER_LOADING); T_EQ(loadcap.sent, 1);
        T_EQ(loadcap.sprites, 2); T_NE(loadcap.bar, 0); T_NE(loadcap.back, 0); T_EQ(loadcap.texts, i == 3 ? 1 : 7);
        T_EQ(loadcap.previews, i == 3 ? 1 : 0);
        T_EQ(loadcap.names, i == 3 ? 2 : 0);
        if (i == 3) {
            T_ASSERT(loadcap.preview.flagsvalue & UIFLAG_MINIMAP_PREVIEW);
            T_FEQ(loadcap.preview.size.width, 0.16f, 0.0001f);
        }
        T_STREQ(gi.GetConfigstring(CS_MODELS + loadcap.back), cases[i].model);
        T_STREQ(gi.GetConfigstring(CS_MODELS + loadcap.bar), "UI\\Glues\\Loading\\LoadBar\\LoadBar.mdx");
        T_STREQ(loadcap.anim, cases[i].anim);
        T_FEQ(loadcap.progress.size.width, 0, 0.00001f); T_FEQ(loadcap.progress.size.height, 0, 0.00001f);
        T_EQ(loadcap.progress.points.y[FPP_MAX].offset, (int16_t)(0.0025f * UI_FRAMEPOINT_SCALE));
    }
    info.flags |= hide_minimap_in_preview_screens;
    memset(&loadcap, 0, sizeof(loadcap));
    UI_WriteLoadingLayout(g_edicts, &info);
    T_EQ(loadcap.previews, 0); T_EQ(loadcap.names, 2);
    info.flags = 0;
    FOR_LOOP(i, PLAYER_NEUTRAL_AGGRESSIVE)
        info.players[i] = (mapPlayer_t){ .used = true, .playerType = kPlayerTypeHuman, .playerName = "Alice" };
    memset(&loadcap, 0, sizeof(loadcap));
    UI_WriteLoadingLayout(g_edicts, &info);
    T_EQ(loadcap.previews, 1); T_EQ(loadcap.rows, PLAYER_NEUTRAL_AGGRESSIVE);
    T_EQ(loadcap.names, PLAYER_NEUTRAL_AGGRESSIVE);
    gi.Write = old_write; gi.unicast = old_send; level.mapinfo = old_info;
    UI_ResetHud();
}
#endif
