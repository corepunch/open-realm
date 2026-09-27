#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "games/warcraft-3/common/cursor.h"
#include "games/warcraft-3/common/minimap.h"
void setup_test_world(void);

static char cursor_registered_path[256];
static int CursorTestModelIndex(cstring_t path) {
    snprintf(cursor_registered_path, sizeof(cursor_registered_path), "%s", path);
    return 71;
}
static bool CursorTestTarget(edict_t *client, vec2_t const *point) { (void)client; (void)point; return false; }

TEST(wc3_cursor, race_map_skin_and_target_lifecycle) {
    gameClient_t client = {0};
    stbIniCache_t theme = game.config.theme, skin = game.config.map_skin;
    int (*model_index)(cstring_t) = gi.ModelIndex;
    game.config.theme = (stbIniCache_t){0}; game.config.map_skin = (stbIniCache_t){0};
    gi.ModelIndex = CursorTestModelIndex;
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.theme,
        "[Default]\nCursor=Default.mdl\n[Human]\nCursor=HumanAuthored.mdl\n"
        "[Orc]\nCursor=OrcAuthored.mdl\n[Undead]\nCursor=UndeadAuthored.mdl\n"
        "[NightElf]\nCursor=NightElfAuthored.mdl\n"));
    uint8_t races[] = {kPlayerRaceHuman, kPlayerRaceOrc, kPlayerRaceUndead, kPlayerRaceNightElf};
    cstring_t names[] = {"HumanAuthored.mdl", "OrcAuthored.mdl", "UndeadAuthored.mdl", "NightElfAuthored.mdl"};
    FOR_LOOP(i, 4) {
        client.ps.race = races[i];
        UI_UpdateCursorPresentation(&client);
        T_STREQ(cursor_registered_path, names[i]);
        T_EQ(client.ps.stats[UI_PLAYERSTAT_CURSOR_INTERACTIONL], 71);
    }
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.map_skin, "[CustomSkin]\nCursor=MapCursor.mdl\n"));
    UI_UpdateCursorPresentation(&client);
    T_STREQ(cursor_registered_path, "MapCursor.mdl");
    client.menu.on_location_selected = CursorTestTarget;
    UI_UpdateCursorPresentation(&client);
    T_EQ(client.ps.stats[UI_PLAYERSTAT_CURSOR_INTERACTION], WC3_POINTER_TARGETING);
    memset(&client.menu, 0, sizeof(client.menu));
    UI_UpdateCursorPresentation(&client);
    T_EQ(client.ps.stats[UI_PLAYERSTAT_CURSOR_INTERACTION], 0);
    T_EQ(client.ps.stats[UI_PLAYERSTAT_CURSOR_IMAGE], 0);
    Stb_IniCacheFree(&game.config.map_skin);
    UI_UpdateCursorPresentation(&client);
    T_STREQ(cursor_registered_path, "NightElfAuthored.mdl");
    Stb_IniCacheFree(&game.config.theme);
    game.config.theme = theme; game.config.map_skin = skin; gi.ModelIndex = model_index;
}
static int CursorTestImageIndex(cstring_t path) {
    snprintf(cursor_registered_path, sizeof(cursor_registered_path), "%s", path);
    return 83;
}
TEST(wc3_cursor, signal_preserves_underlying_target_and_uses_skin_color) {
    setup_test_world();
    edict_t *ent = g_edicts;
    gameClient_t *client = ent->client;
    int (*image_index)(cstring_t) = gi.ImageIndex;
    stbIniCache_t skin = game.config.map_skin, misc = game.config.misc;
    game.config.misc = (stbIniCache_t){0};
    game.config.map_skin = (stbIniCache_t){0};
    gi.ImageIndex = CursorTestImageIndex;
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.map_skin,
        "[CustomSkin]\nTeamColors=7\nTeamColor=CustomColors\\Swatch\n"));
    client->ps.color = 9;
    client->menu.on_location_selected = CursorTestTarget;
    cstring_t signal[] = {"signal"}, cancel[] = {"cancel"}, point[] = {"point", "123", "456"};
    G_ClientCommand(ent, 1, signal);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_CURSOR_INTERACTION], WC3_POINTER_SIGNALING);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_CURSOR_IMAGE], 83);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_CURSOR_FLAGS], CURSOR_INPUT_MINIMAP_POINT);
    T_STREQ(cursor_registered_path, "CustomColors\\Swatch02.blp");
    T_ASSERT(client->menu.on_location_selected == CursorTestTarget);
    G_ClientCommand(ent, 1, cancel);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_CURSOR_INTERACTION], WC3_POINTER_TARGETING);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_CURSOR_FLAGS], 0);
    T_ASSERT(client->menu.on_location_selected == CursorTestTarget);
    G_ClientCommand(ent, 1, signal);
    G_ClientCommand(ent, 3, point);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_CURSOR_INTERACTION], WC3_POINTER_TARGETING);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_CURSOR_FLAGS], 0);
    T_ASSERT(client->menu.on_location_selected == CursorTestTarget);
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc,
        "[TeamColorFilter]\nColorIndexPlayer=4\n"));
    client->ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR] = WC3_MINIMAP_ALLY_COLOR_WORLD;
    G_ClientCommand(ent, 1, signal);
    T_STREQ(cursor_registered_path, "CustomColors\\Swatch04.blp");
    G_ClientCommand(ent, 1, cancel);
    client->ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR] = 0;
    memset(&client->menu, 0, sizeof(client->menu));
    Stb_IniCacheFree(&game.config.misc); game.config.misc = misc;
    Stb_IniCacheFree(&game.config.map_skin); game.config.map_skin = skin;
    gi.ImageIndex = image_index;
}
#endif
