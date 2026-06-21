/*
 * ui/screens/lan_join.c - LAN browser and create-game map selection screens.
 */

#include <stdlib.h>

#ifndef _WIN32
#include <strings.h>
#endif

#ifndef SDLK_RETURN
#define SDLK_RETURN 13
#define SDLK_KP_ENTER 1073741912
#endif

#include "../ui_local.h"
#include "../ui_screen.h"
#include "../generated/local_multiplayer_join.h"
#include "../generated/local_multiplayer_create.h"
#include "../generated/map_list_box.h"
#include "../generated/map_info_pane.h"

#define LAN_VISIBLE_MAPS 14
#define LAN_VISIBLE_GAMES 11
#define LAN_FILELIST_SIZE 32768
#define LAN_PLAYER_NAME_DEFAULT "Player"
#define LAN_PLAYER_NAME_MAX_CHARS 20

typedef enum {
    LAN_MODE_BROWSER,
    LAN_MODE_CREATE,
} lanMode_t;

typedef struct lan_join_state_s {
    LocalMultiplayerJoin_t join_frames;
    LocalMultiplayerCreate_t create_frames;
    lanMode_t mode;
    uiMapListState_t games;
    uiMapListState_t maps;
    BOOL ready;
    LPFRAMEDEF root;
    LPFRAMEDEF join_button;
    LPFRAMEDEF play_button;
    MapListBox_t map_list_template;
    MapListBox_t game_list_box;
    MapListBox_t create_map_list_box;
    MapInfoPane_t map_info_template;
    MapInfoPane_t game_map_info_pane;
    MapInfoPane_t create_map_info_pane;
    LPFRAMEDEF game_speed_slider;
    LPFRAMEDEF game_speed_value;
} lan_join_state_t;

static lan_join_state_t lan;

static BOOL LANJoin_LoadScreen(void) {
    BOOL ok = true;

    ok = UI_EnsureFDF("UI\\FrameDef\\GlobalStrings.fdf") && ok;
    ok = UI_EnsureFDF("UI\\FrameDef\\Glue\\StandardTemplates.fdf") && ok;
    ok = LocalMultiplayerJoin_Load(&lan.join_frames) && ok;
    ok = LocalMultiplayerCreate_Load(&lan.create_frames) && ok;
    ok = MapListBox_Load(&lan.map_list_template) && ok;
    ok = MapInfoPane_Load(&lan.map_info_template) && ok;
    return ok;
}

static void LAN_CopyString(LPSTR out, size_t out_size, LPCSTR value) {
    if (!out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s", value ? value : "");
}

static void LAN_CopyTitleSubtitle(LPSTR out, size_t out_size, LPCSTR title, LPCSTR subtitle) {
    if (!out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s: %s", title ? title : "", subtitle ? subtitle : "");
}

static BOOL LAN_HasMapExtension(LPCSTR path) {
    LPCSTR dot = strrchr(path, '.');
    return dot && (!strcasecmp(dot, ".w3m") || !strcasecmp(dot, ".w3x"));
}

static BOOL LAN_IsCampaignMap(LPCSTR path) {
    return path && (!strncasecmp(path, "Maps\\Campaign\\", 14) ||
                    !strncasecmp(path, "Maps/Campaign/", 14) ||
                    !strncasecmp(path, "Maps\\FrozenThrone\\Campaign\\", 26) ||
                    !strncasecmp(path, "Maps/FrozenThrone/Campaign/", 26));
}

static void LAN_SetMapDisplayName(uiMapListItem_t *item,
                                  LPCMAPINFO info)
{
    char name[128];
    char description[128];
    char loading_title[64];
    char loading_subtitle[96];

    uiimport.ResolveMapInfoString(info, info ? info->mapName : NULL, name, sizeof(name));
    uiimport.SanitizeMapListField(name);
    if (name[0]) {
        LAN_CopyString(item->name, sizeof(item->name), name);
    }

    if (strncasecmp(item->path, "Maps\\Campaign\\", 14) ||
        !uiimport.MapNameMatchesFile(item->name, item->path)) {
        return;
    }

    uiimport.ResolveMapInfoString(info, info ? info->mapDescription : NULL, description, sizeof(description));
    uiimport.ResolveMapInfoString(info, info ? info->loadingScreenTitle : NULL, loading_title, sizeof(loading_title));
    uiimport.ResolveMapInfoString(info, info ? info->loadingScreenSubtitle : NULL, loading_subtitle, sizeof(loading_subtitle));
    uiimport.SanitizeMapListField(description);
    uiimport.SanitizeMapListField(loading_title);
    uiimport.SanitizeMapListField(loading_subtitle);

    if (loading_title[0] && loading_subtitle[0]) {
        LAN_CopyTitleSubtitle(item->name, sizeof(item->name), loading_title, loading_subtitle);
    } else if (description[0]) {
        LAN_CopyString(item->name, sizeof(item->name), description);
    } else if (loading_title[0]) {
        LAN_CopyString(item->name, sizeof(item->name), loading_title);
    } else if (loading_subtitle[0]) {
        LAN_CopyString(item->name, sizeof(item->name), loading_subtitle);
    }
}

static DWORD LAN_CountMapPlayers(LPCMAPINFO info) {
    DWORD count = 0;

    FOR_LOOP(i, MAX_PLAYERS) {
        if (info && info->players[i].used) {
            count++;
        }
    }
    return count;
}

static BOOL LAN_ParseMapInfo(uiMapListItem_t *item) {
    MAPINFO info;
    LPCSTR tileset;

    if (!item || !uiimport.ReadMapInfo || !uiimport.FreeMapInfo ||
        !uiimport.ResolveMapInfoString || !uiimport.MapTilesetName ||
        !uiimport.MapSizeName || !uiimport.MapNameMatchesFile ||
        !uiimport.SanitizeMapListField || !uiimport.SanitizeMapInfoText) {
        return false;
    }
    if (!uiimport.ReadMapInfo(item->path, &info)) {
        return false;
    }

    LAN_SetMapDisplayName(item, &info);
    uiimport.ResolveMapInfoString(&info, info.mapDescription, item->description, sizeof(item->description));
    uiimport.ResolveMapInfoString(&info, info.playersRecommended, item->suggestedPlayers, sizeof(item->suggestedPlayers));
    uiimport.SanitizeMapInfoText(item->description);
    uiimport.SanitizeMapInfoText(item->suggestedPlayers);
    snprintf(item->mapSize, sizeof(item->mapSize), "%s", uiimport.MapSizeName(info.playableArea.width, info.playableArea.height));
    tileset = uiimport.MapTilesetName((BYTE)info.mainGroundType);
    snprintf(item->tileset, sizeof(item->tileset), "%s", tileset ? tileset : UI_GetString("UNKNOWNMAP_TILESET"));
    item->flags = info.flags;
    item->players = MIN(LAN_CountMapPlayers(&info), 16);
    uiimport.FreeMapInfo(&info);
    return true;
}

static BOOL LAN_MapExists(LPCSTR path) {
    for (DWORD i = 0; i < lan.maps.count; i++) {
        if (!strcasecmp(lan.maps.items[i].path, path)) {
            return true;
        }
    }
    return false;
}

static void LAN_AddMap(LPCSTR path) {
    uiMapListItem_t *item;

    if (!path || !*path || lan.maps.count >= UI_MAX_MAP_LIST_ITEMS ||
        !LAN_HasMapExtension(path) || LAN_IsCampaignMap(path) || LAN_MapExists(path)) {
        return;
    }

    item = &lan.maps.items[lan.maps.count++];
    memset(item, 0, sizeof(*item));
    snprintf(item->path, sizeof(item->path), "%s", path);
    for (char *p = item->path; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
    if (uiimport.DefaultMapName) {
        uiimport.DefaultMapName(item->path, item->name, sizeof(item->name));
    } else {
        LAN_CopyString(item->name, sizeof(item->name), item->path);
    }
    snprintf(item->description, sizeof(item->description), "%s", UI_GetString("UNKNOWNMAP_DESCRIPTION"));
    snprintf(item->suggestedPlayers, sizeof(item->suggestedPlayers), "%s", UI_GetString("UNKNOWNMAP_SUGGESTEDPLAYERS"));
    snprintf(item->mapSize, sizeof(item->mapSize), "%s", UI_GetString("UNKNOWNMAP_MAPSIZE"));
    snprintf(item->tileset, sizeof(item->tileset), "%s", UI_GetString("UNKNOWNMAP_TILESET"));
    LAN_ParseMapInfo(item);
}

static int LAN_CompareMaps(const void *a, const void *b) {
    const uiMapListItem_t *ma = a;
    const uiMapListItem_t *mb = b;

    if (ma->players != mb->players) {
        return ma->players < mb->players ? -1 : 1;
    }
    return strcasecmp(ma->name, mb->name);
}

static void LAN_LoadMapsWithExtension(LPCSTR extension) {
    char listbuf[LAN_FILELIST_SIZE];
    int count;
    char *cursor;

    if (!uiimport.FS_GetFileList) {
        return;
    }

    memset(listbuf, 0, sizeof(listbuf));
    count = uiimport.FS_GetFileList("Maps", extension, listbuf, sizeof(listbuf));
    cursor = listbuf;
    for (int i = 0; i < count && *cursor; i++) {
        LAN_AddMap(cursor);
        cursor += strlen(cursor) + 1;
    }
}

static void LAN_LoadMaps(void) {
    memset(&lan.maps, 0, sizeof(lan.maps));
    LAN_LoadMapsWithExtension(".w3m");
    LAN_LoadMapsWithExtension(".w3x");
    qsort(lan.maps.items, lan.maps.count, sizeof(lan.maps.items[0]), LAN_CompareMaps);
    lan.maps.visualScroll = (FLOAT)lan.maps.scroll;
    uiimport.Printf("LAN_LoadMaps: %d maps\n", (int)lan.maps.count);
}

static void LAN_SetTextIfPresent(LPFRAMEDEF frame, LPCSTR format, ...) {
    va_list argptr;
    char text[1024];

    if (!frame || !format) {
        return;
    }
    va_start(argptr, format);
    vsnprintf(text, sizeof(text), format, argptr);
    va_end(argptr);
    UI_SetText(frame, "%s", text);
}

static LPCSTR LAN_GameSpeedValueText(DWORD value) {
    switch (value) {
        case 0:
            return UI_GetString("SLOW");
        case 1:
            return UI_GetString("NORMAL");
        case 2:
        default:
            return UI_GetString("FAST");
    }
}

static LPCSTR LAN_PlayerName(void) {
    LPCSTR name = uiimport.Cvar_String
        ? uiimport.Cvar_String("name", LAN_PLAYER_NAME_DEFAULT)
        : LAN_PLAYER_NAME_DEFAULT;
    return name && name[0] ? name : LAN_PLAYER_NAME_DEFAULT;
}

static BOOL LAN_HasVisibleText(LPCSTR text) {
    if (!text) {
        return false;
    }
    while (*text) {
        if (*text > ' ') {
            return true;
        }
        text++;
    }
    return false;
}

static void LAN_InitPlayerNameEditBox(void) {
    if (!lan.join_frames.PlayerNameEditBox) {
        return;
    }
    lan.join_frames.PlayerNameEditBox->Edit.MaxChars = LAN_PLAYER_NAME_MAX_CHARS;
    UI_SetEditValue(lan.join_frames.PlayerNameEditBox, LAN_PlayerName());
}

void LAN_ApplyPlayerName(void) {
    LPCSTR text;

    if (!lan.join_frames.PlayerNameEditBox) {
        return;
    }
    text = UI_EditValue(lan.join_frames.PlayerNameEditBox);
    if (!LAN_HasVisibleText(text)) {
        text = LAN_PLAYER_NAME_DEFAULT;
        UI_SetEditValue(lan.join_frames.PlayerNameEditBox, text);
    }
    if (uiimport.Cvar_Set) {
        uiimport.Cvar_Set("name", text);
    }
}

static LPCSTR LAN_GameSpeedText(void) {
    int value;

    if (!lan.game_speed_slider) {
        return UI_GetString("FAST");
    }

    value = (int)floorf(lan.game_speed_slider->Slider.InitialValue + 0.5f);
    return LAN_GameSpeedValueText((DWORD)value);
}

static void LAN_UpdateGameSpeed(void) {
    LAN_SetTextIfPresent(lan.game_speed_value, "%s", LAN_GameSpeedText());
}

static void LAN_BindMapInfoPane(LPFRAMEDEF container, MapInfoPane_t *pane) {
    LPFRAMEDEF root;

    if (!pane || pane->MapInfoPane || !container) {
        return;
    }

    if (!lan.map_info_template.MapInfoPane) {
        uiimport.Printf("LAN_BindMapInfoPane: MapInfoPane missing\n");
        return;
    }

    root = UI_CloneFrameTree(lan.map_info_template.MapInfoPane, container);
    if (!root || !MapInfoPane_Bind(pane, root)) {
        return;
    }
    UI_SetPoint(pane->MapInfoPane, FRAMEPOINT_TOPLEFT, container, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    UI_SetPoint(pane->MapInfoPane, FRAMEPOINT_BOTTOMRIGHT, container, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
    UI_LayoutMapInfoPane(pane->MapInfoPane);
}

static void LAN_CreateMapListFrame(LPFRAMEDEF container,
                                   LPFRAMEDEF label,
                                   MapListBox_t *list_box,
                                   uiMapListState_t *state,
                                   DWORD visible_rows) {
    LPFRAMEDEF root;

    if (!list_box || !state || list_box->MapListBox || !container) {
        return;
    }
    if (!lan.map_list_template.MapListBox) {
        uiimport.Printf("LAN_CreateMapListFrame: MapListBox missing\n");
        return;
    }
    root = UI_CloneFrameTree(lan.map_list_template.MapListBox, container);
    if (!root || !MapListBox_Bind(list_box, root)) {
        return;
    }
    UI_SetPoint(list_box->MapListBox, FRAMEPOINT_TOPLEFT, container, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    UI_SetPoint(list_box->MapListBox, FRAMEPOINT_BOTTOMRIGHT, container, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
    UI_BindMapList(list_box->MapListBox, state, label, visible_rows, "menu_lan_select %u");
}

static void LAN_UpdateMapInfo(MapInfoPane_t *pane, uiMapListState_t *items) {
    uiMapListItem_t *item;

    if (!pane || !items) {
        return;
    }
    if (items->count == 0 || items->selected >= items->count) {
        UI_SetHidden(pane->MinimapImage, true);
        LAN_SetTextIfPresent(pane->MaxPlayersValue, " ");
        LAN_SetTextIfPresent(pane->MapNameValue, " ");
        LAN_SetTextIfPresent(pane->SuggestedPlayersValue, "%s", UI_GetString("UNKNOWNMAP_SUGGESTEDPLAYERS"));
        LAN_SetTextIfPresent(pane->MapSizeValue, "%s", UI_GetString("UNKNOWNMAP_MAPSIZE"));
        LAN_SetTextIfPresent(pane->MapTilesetValue, "%s", UI_GetString("UNKNOWNMAP_TILESET"));
        LAN_SetTextIfPresent(pane->MapDescValue, "%s", UI_GetString("UNKNOWNMAP_DESCRIPTION"));
        return;
    }

    item = &items->items[items->selected];
    if (pane->MinimapImage && uiimport.FindMapPreviewTexture) {
        PATHSTR preview;

        if (uiimport.FindMapPreviewTexture(item->path, preview, sizeof(preview))) {
            UI_SetTexture(pane->MinimapImage, preview, false);
            UI_SetHidden(pane->MinimapImage, false);
        } else {
            UI_SetHidden(pane->MinimapImage, true);
        }
    }
    LAN_SetTextIfPresent(pane->MaxPlayersValue, "%u", (unsigned)item->players);
    LAN_SetTextIfPresent(pane->MapNameValue, "%s", item->name[0] ? item->name : item->path);
    LAN_SetTextIfPresent(pane->SuggestedPlayersValue, "%s", item->suggestedPlayers);
    LAN_SetTextIfPresent(pane->MapSizeValue, "%s", item->mapSize);
    LAN_SetTextIfPresent(pane->MapTilesetValue, "%s", item->tileset);
    LAN_SetTextIfPresent(pane->MapDescValue, "%s", item->description);
}

static void LAN_ClearGames(void) {
    memset(&lan.games, 0, sizeof(lan.games));
}

static void LAN_CopyGameMapInfo(uiMapListItem_t *item, const uiLanGame_t *game) {
    uiMapListItem_t map_item;

    if (!item || !game) {
        return;
    }

    memset(&map_item, 0, sizeof(map_item));
    snprintf(map_item.path, sizeof(map_item.path), "%s", item->path);
    if (LAN_ParseMapInfo(&map_item)) {
        snprintf(item->description, sizeof(item->description), "%s", map_item.description);
        snprintf(item->suggestedPlayers, sizeof(item->suggestedPlayers), "%s", map_item.suggestedPlayers);
        snprintf(item->mapSize, sizeof(item->mapSize), "%s", map_item.mapSize);
        snprintf(item->tileset, sizeof(item->tileset), "%s", map_item.tileset);
        item->players = map_item.players ? map_item.players : item->players;
        return;
    }

    snprintf(item->description, sizeof(item->description), "%s", game->address);
    snprintf(item->suggestedPlayers,
             sizeof(item->suggestedPlayers),
             "%u/%u",
             (unsigned)game->players,
             (unsigned)game->maxPlayers);
    snprintf(item->mapSize, sizeof(item->mapSize), "%s", UI_GetString("UNKNOWNMAP_MAPSIZE"));
    snprintf(item->tileset, sizeof(item->tileset), "%s", UI_GetString("UNKNOWNMAP_TILESET"));
}

static void LAN_CopyGame(uiMapListItem_t *item, const uiLanGame_t *game, DWORD index) {
    PATHSTR path;
    BOOL path_changed;

    if (!item || !game) {
        return;
    }

    snprintf(path, sizeof(path), "%s", game->mapname);
    for (char *p = path; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }

    path_changed = strcmp(item->path, path) != 0;
    snprintf(item->path, sizeof(item->path), "%s", path);
    snprintf(item->name, sizeof(item->name), "%s", game->hostname[0] ? game->hostname : game->address);
    item->players = game->slots ? game->slots : game->maxPlayers;
    item->flags = index;

    if (path_changed || !item->tileset[0]) {
        LAN_CopyGameMapInfo(item, game);
    }
}

static void LAN_LoadGames(void) {
    DWORD count;

    if (!uiimport.LANNumServers || !uiimport.LANServer) {
        return;
    }

    count = uiimport.LANNumServers();
    if (count > UI_MAX_MAP_LIST_ITEMS) {
        count = UI_MAX_MAP_LIST_ITEMS;
    }
    FOR_LOOP(i, count) {
        uiLanGame_t game;
        uiMapListItem_t *item;

        if (!uiimport.LANServer(i, &game)) {
            continue;
        }
        if (i >= lan.games.count) {
            item = &lan.games.items[lan.games.count++];
            memset(item, 0, sizeof(*item));
        } else {
            item = &lan.games.items[i];
        }
        LAN_CopyGame(item, &game, i);
    }
    if (lan.games.count > count) {
        memset(&lan.games.items[count], 0, sizeof(lan.games.items[0]) * (lan.games.count - count));
        lan.games.count = count;
    }
    if (lan.games.selected >= lan.games.count) {
        lan.games.selected = lan.games.count ? lan.games.count - 1 : 0;
    }
    if (lan.games.scroll > lan.games.selected) {
        lan.games.scroll = lan.games.selected;
    }
}

static void LAN_UpdateBrowserControls(void) {
    uiMapListItem_t *item;

    if (lan.join_button) {
        if (lan.games.count > 0) {
            UI_SetOnClick(lan.join_button, "menu_lan_join");
        } else {
            lan.join_button->OnClick[0] = '\0';
        }
    }

    if (lan.games.count == 0 || lan.games.selected >= lan.games.count) {
        LAN_SetTextIfPresent(lan.join_frames.GameCreatorValue, " ");
        LAN_SetTextIfPresent(lan.join_frames.GameSpeedValue, " ");
        LAN_UpdateMapInfo(&lan.game_map_info_pane, &lan.games);
        return;
    }

    item = &lan.games.items[lan.games.selected];
    LAN_SetTextIfPresent(lan.join_frames.GameCreatorValue, "%s", item->name);
    LAN_SetTextIfPresent(lan.join_frames.GameSpeedValue, "%s", LAN_GameSpeedValueText(2));
    if (uiimport.LANServer) {
        uiLanGame_t game;

        if (uiimport.LANServer(item->flags, &game)) {
            LAN_SetTextIfPresent(lan.join_frames.GameSpeedValue, "%s", LAN_GameSpeedValueText(game.speed));
        }
    }
    LAN_UpdateMapInfo(&lan.game_map_info_pane, &lan.games);
}

static void LAN_UpdateControls(void) {
    if (!lan.ready) {
        return;
    }

    if (lan.mode == LAN_MODE_BROWSER) {
        LAN_UpdateBrowserControls();
        return;
    }

    if (lan.play_button) {
        if (lan.maps.count > 0) {
            UI_SetOnClick(lan.play_button, "menu_lan_start");
        } else {
            lan.play_button->OnClick[0] = '\0';
        }
    }
    LAN_UpdateGameSpeed();
    LAN_UpdateMapInfo(&lan.create_map_info_pane, &lan.maps);
}

static void LAN_RequestServerRefresh(void) {
    if (uiimport.LANRefreshServers) {
        uiimport.LANRefreshServers();
    }
}

static BOOL LAN_BuildBrowserFrames(void) {
    lan.ready = false;
    lan.root = lan.join_frames.LocalMultiplayerJoin;
    if (!lan.root) {
        uiimport.Printf("LAN_BuildBrowserFrames: LocalMultiplayerJoin missing\n");
        return false;
    }
    UI_SetAllPoints(lan.root);

    lan.join_button = lan.join_frames.JoinButton;
    if (!lan.join_button) {
        uiimport.Printf("LAN_BuildBrowserFrames: JoinButton missing\n");
        return false;
    }

    LAN_CreateMapListFrame(lan.join_frames.GameListContainer,
                           lan.join_frames.GameListLabel,
                           &lan.game_list_box,
                           &lan.games,
                           LAN_VISIBLE_GAMES);
    LAN_BindMapInfoPane(lan.join_frames.MapInfoPaneContainer, &lan.game_map_info_pane);

    LAN_InitPlayerNameEditBox();
    UI_SetOnClick(lan.join_frames.CreateButton, "menu_startserver");
    UI_SetOnClick(lan.join_button, "menu_lan_join");
    UI_SetOnClick(lan.join_frames.CancelButton, "menu_main");
    return true;
}

static BOOL LAN_BuildCreateFrames(void) {
    lan.ready = false;
    lan.root = lan.create_frames.LocalMultiplayerCreate;
    if (!lan.root) {
        uiimport.Printf("LAN_BuildCreateFrames: LocalMultiplayerCreate missing\n");
        return false;
    }
    UI_SetAllPoints(lan.root);

    lan.play_button = lan.create_frames.PlayButton;
    if (!lan.play_button) {
        uiimport.Printf("LAN_BuildCreateFrames: PlayButton missing\n");
        return false;
    }
    lan.game_speed_slider = lan.create_frames.GameSpeedSlider;
    lan.game_speed_value = lan.create_frames.GameSpeedValue;

    LAN_CreateMapListFrame(lan.create_frames.MapListContainer,
                           lan.create_frames.MapListLabel,
                           &lan.create_map_list_box,
                           &lan.maps,
                           LAN_VISIBLE_MAPS);
    LAN_BindMapInfoPane(lan.create_frames.MapInfoPaneContainer, &lan.create_map_info_pane);

    UI_SetHidden(lan.create_frames.MapInfoPanel, false);
    UI_SetHidden(lan.create_frames.AdvancedOptionsPanel, true);
    UI_SetOnClick(lan.create_frames.MapInfoButton, "menu_startserver");
    UI_SetOnClick(lan.create_frames.AdvancedOptionsButton, "menu_startserver");
    UI_SetOnClick(lan.play_button, "menu_lan_start");
    UI_SetOnClick(lan.create_frames.CancelButton, "menu_multiplayer");
    return true;
}

static void LAN_BuildFrames(lanMode_t mode) {
    lan.mode = mode;
    if (mode == LAN_MODE_BROWSER) {
        lan.ready = LAN_BuildBrowserFrames();
    } else {
        lan.ready = LAN_BuildCreateFrames();
    }
}

static void LANJoin_Init(void) {
    uiimport.Printf("LANJoin_Init\n");
    UI_PreloadGlueSceneModels();
    LAN_BuildFrames(lan.mode);
    if (!lan.ready) {
        return;
    }
    if (lan.mode == LAN_MODE_BROWSER) {
        LAN_ClearGames();
        LAN_RequestServerRefresh();
        LAN_LoadGames();
    } else {
        LAN_LoadMaps();
    }
    LAN_UpdateControls();
}

static void LANJoin_Shutdown(void) {
}

static void LANJoin_Refresh(int msec) {
    uiMapListState_t *list;
    FLOAT target;
    FLOAT diff;
    FLOAT alpha;

    if (!lan.ready) {
        return;
    }

    list = lan.mode == LAN_MODE_BROWSER ? &lan.games : &lan.maps;
    if (lan.mode == LAN_MODE_BROWSER) {
        LAN_LoadGames();
        LAN_UpdateControls();
    }

    target = (FLOAT)list->scroll;
    diff = target - list->visualScroll;
    alpha = (FLOAT)msec / 90.0f;

    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    if (diff > -0.001f && diff < 0.001f) {
        list->visualScroll = target;
    } else {
        list->visualScroll += diff * alpha;
    }
}

static void LANJoin_Draw(void) {
    if (!lan.ready) {
        return;
    }

    UI_DrawGlueScene(lan.mode == LAN_MODE_BROWSER
                     ? "BattlenetCustom Stand"
                     : "BattlenetCustomCreate Stand");
    if (lan.mode == LAN_MODE_CREATE) {
        LAN_UpdateGameSpeed();
    }
    UI_DrawFrame(lan.root);
}

static void LANJoin_KeyEvent(int key, BOOL down) {
    if (!down) {
        return;
    }
    if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) &&
        UI_EditHasFocus(lan.join_frames.PlayerNameEditBox)) {
        LAN_ApplyPlayerName();
        UI_ClearEditFocus();
    }
}

static void LANJoin_MouseEvent(int x, int y, int buttons) {
    (void)x;
    (void)y;
    (void)buttons;
}

LPCSTR LAN_SelectedMapPath(void) {
    if (!lan.ready) {
        return NULL;
    }
    if (lan.maps.count == 0 || lan.maps.selected >= lan.maps.count) {
        return NULL;
    }
    return lan.maps.items[lan.maps.selected].path;
}

LPCSTR LAN_SelectedMapName(void) {
    if (!lan.ready) {
        return NULL;
    }
    if (lan.maps.count == 0 || lan.maps.selected >= lan.maps.count) {
        return NULL;
    }
    return lan.maps.items[lan.maps.selected].name[0]
        ? lan.maps.items[lan.maps.selected].name
        : lan.maps.items[lan.maps.selected].path;
}

DWORD LAN_SelectedGameSpeed(void) {
    int value;

    if (!lan.ready || !lan.game_speed_slider) {
        return 2;
    }
    value = (int)floorf(lan.game_speed_slider->Slider.InitialValue + 0.5f);
    if (value < 0) {
        return 0;
    }
    if (value > 2) {
        return 2;
    }
    return (DWORD)value;
}

void LAN_StartSelectedMap(void) {
    if (!LAN_SelectedMapPath()) {
        return;
    }
    if (uiimport.Cvar_Set) {
        uiimport.Cvar_Set("connect", "");
    } else if (uiimport.Cmd_ExecuteText) {
        uiimport.Cmd_ExecuteText("seta connect \"\"\n");
    }
    UI_ShowGameSetupMenu();
}

void LAN_SelectMapIndex(DWORD index) {
    uiMapListState_t *list;
    DWORD visible_rows;

    if (!lan.ready) {
        return;
    }
    list = lan.mode == LAN_MODE_BROWSER ? &lan.games : &lan.maps;
    visible_rows = lan.mode == LAN_MODE_BROWSER ? LAN_VISIBLE_GAMES : LAN_VISIBLE_MAPS;
    if (index >= list->count) {
        return;
    }
    list->selected = index;
    if (list->selected < list->scroll) {
        list->scroll = list->selected;
    }
    if (list->selected >= list->scroll + visible_rows) {
        list->scroll = list->selected - visible_rows + 1;
    }
    LAN_UpdateControls();
}

void LAN_JoinSelectedGame(void) {
    if (!lan.ready || lan.mode != LAN_MODE_BROWSER || !uiimport.LANConnectServer) {
        return;
    }
    if (lan.games.count == 0 || lan.games.selected >= lan.games.count) {
        return;
    }
    LAN_ApplyPlayerName();
    uiimport.LANConnectServer(lan.games.items[lan.games.selected].flags);
}

void LAN_ShowBrowser(void) {
    LAN_BuildFrames(LAN_MODE_BROWSER);
    if (!lan.ready) {
        return;
    }
    LAN_ClearGames();
    LAN_RequestServerRefresh();
    LAN_LoadGames();
    LAN_UpdateControls();
}

void LAN_ShowCreate(void) {
    LAN_BuildFrames(LAN_MODE_CREATE);
    if (!lan.ready) {
        return;
    }
    LAN_LoadMaps();
    LAN_UpdateControls();
}

void LAN_RefreshMaps(void) {
    if (!lan.ready) {
        return;
    }
    if (lan.mode == LAN_MODE_BROWSER) {
        LAN_ClearGames();
        LAN_RequestServerRefresh();
        LAN_LoadGames();
    } else {
        LAN_LoadMaps();
    }
    LAN_UpdateControls();
}

uiScreen_t lanJoinScreen = {
    .name = "lan",
    .load = LANJoin_LoadScreen,
    .init = LANJoin_Init,
    .shutdown = LANJoin_Shutdown,
    .refresh = LANJoin_Refresh,
    .draw = LANJoin_Draw,
    .key_event = LANJoin_KeyEvent,
    .mouse_event = LANJoin_MouseEvent,
};
