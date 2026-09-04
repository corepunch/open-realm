/*
 * ui/screens/single_player.c — Single player menu screen.
 */

#include "../menu_local.h"
#include "../menu_screen.h"
#include "../generated/single_player_menu.h"
#include <ctype.h>
#include <stdlib.h>
#ifndef _WIN32
#include <strings.h>
#endif

#define SINGLE_PLAYER_MAX_CAMPAIGNS 16
#define SINGLE_PLAYER_MAX_MISSIONS 128
#define SINGLE_PLAYER_MISSION_VISIBLE_ROWS 14
#define SINGLE_PLAYER_MISSION_VISIBILITY_CVAR "wc3_campaign_mission_visibility"

typedef enum {
    SINGLE_PLAYER_VIEW_MAIN,
    SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT,
    SINGLE_PLAYER_VIEW_MISSION_SELECT,
} singlePlayerView_t;

typedef struct {
    UINAME header;
    UINAME name;
    PATHSTR map_path;
} singlePlayerMission_t;

typedef struct {
    playerRace_t race;
    UINAME key;
    UINAME header;
    UINAME name;
    UINAME background;
    singlePlayerMission_t missions[SINGLE_PLAYER_MAX_MISSIONS];
    DWORD num_missions;
} singlePlayerCampaign_t;

static SinglePlayerMenu_t single_player;
static singlePlayerCampaign_t campaigns[SINGLE_PLAYER_MAX_CAMPAIGNS];
static DWORD campaign_count;
static DWORD campaign_order[SINGLE_PLAYER_MAX_CAMPAIGNS];
static DWORD campaign_order_count;
static uiMapListState_t campaign_list;
static LPFRAMEDEF campaign_list_frame;
static uiMapListState_t mission_list;
static LPFRAMEDEF mission_list_frame;
static DWORD campaign_background_model = 0;
static DWORD selected_campaign_index = SINGLE_PLAYER_MAX_CAMPAIGNS;
static singlePlayerView_t current_view = SINGLE_PLAYER_VIEW_MAIN;

static BOOL SinglePlayerMenu_LoadScreen(void) {
    if (SinglePlayerMenu_Load(&single_player)) {
        UI_EnsureFDF("UI\\FrameDef\\Glue\\MapListBox.fdf");
        return true;
    }
    return false;
}

static char *SinglePlayer_Trim(char *text) {
    text += strspn(text, " \t\r\n");
    for (char *end = text + strlen(text); end > text && isspace((unsigned char)end[-1]); )
        *--end = '\0';
    return text;
}

static void SinglePlayer_StripComment(char *line) {
    BOOL quoted = false;
    for (char *p = line; *p; p++) {
        if (*p == '"') quoted = !quoted;
        if (!quoted && p[0] == '/' && p[1] == '/') {
            *p = '\0';
            return;
        }
    }
}

static BOOL SinglePlayer_ReadQuoted(char **cursor, LPSTR out, DWORD out_size) {
    char *p = *cursor + strspn(*cursor, " \t\r\n");
    if (*p != '"')
        return false;
    char *end = strchr(++p, '"');
    if (!end)
        return false;
    size_t len = MIN((size_t)(end - p), out_size ? (size_t)out_size - 1 : 0);
    if (out_size)
        memcpy(out, p, len), out[len] = '\0';
    p = end + 1 + strspn(end + 1, " \t\r\n");
    if (*p == ',')
        p++;
    *cursor = p;
    return true;
}

static singlePlayerCampaign_t *SinglePlayer_FindCampaignMutable(LPCSTR key) {
    if (!key || !*key) {
        return NULL;
    }
    FOR_LOOP(i, campaign_count) {
        if (!strcasecmp(campaigns[i].key, key)) {
            return &campaigns[i];
        }
    }
    return NULL;
}

static singlePlayerCampaign_t *SinglePlayer_EnsureCampaign(LPCSTR key) {
    singlePlayerCampaign_t *campaign = SinglePlayer_FindCampaignMutable(key);

    if (campaign) {
        return campaign;
    }
    if (!key || !*key || campaign_count >= SINGLE_PLAYER_MAX_CAMPAIGNS) {
        return NULL;
    }
    campaign = &campaigns[campaign_count++];
    memset(campaign, 0, sizeof(*campaign));
    snprintf(campaign->key, sizeof(campaign->key), "%s", key);
    campaign->race = kPlayerRaceNone;
    return campaign;
}

static singlePlayerCampaign_t const *SinglePlayer_FindCampaign(LPCSTR name) {
    if (!name) {
        return NULL;
    }
    FOR_LOOP(i, campaign_count) {
        if (!strcasecmp(name, campaigns[i].key)) {
            return &campaigns[i];
        }
    }
    return NULL;
}

static void SinglePlayer_AddCampaignOrder(LPCSTR key) {
    singlePlayerCampaign_t *campaign = SinglePlayer_EnsureCampaign(key);
    if (!campaign || campaign_order_count >= SINGLE_PLAYER_MAX_CAMPAIGNS) {
        return;
    }
    FOR_LOOP(i, campaign_order_count) {
        if (campaign_order[i] == (DWORD)(campaign - campaigns)) {
            return;
        }
    }
    campaign_order[campaign_order_count++] = (DWORD)(campaign - campaigns);
}

static void SinglePlayer_ParseCampaignList(char *value) {
    UINAME field;
    char *cursor = value;

    while (SinglePlayer_ReadQuoted(&cursor, field, sizeof(field))) {
        if (field[0]) {
            SinglePlayer_AddCampaignOrder(field);
        }
    }
}

static BOOL SinglePlayer_ParseIndexedKey(LPCSTR key, LPCSTR prefix, DWORD *index) {
    size_t prefix_len = strlen(prefix);

    if (strncasecmp(key, prefix, prefix_len))
        return false;
    char *end = NULL;
    unsigned long value = strtoul(key + prefix_len, &end, 10);
    if (*end || value >= SINGLE_PLAYER_MAX_MISSIONS)
        return false;
    *index = (DWORD)value;
    return true;
}

static void SinglePlayer_SetMissionCount(singlePlayerCampaign_t *campaign, DWORD index) {
    if (campaign && index < SINGLE_PLAYER_MAX_MISSIONS && campaign->num_missions <= index) {
        campaign->num_missions = index + 1;
    }
}

static void SinglePlayer_ParseMissionValue(singlePlayerCampaign_t *campaign, DWORD index, char *value) {
    char *cursor = value;
    UINAME header;
    UINAME name;
    PATHSTR path;

    if (!campaign || index >= SINGLE_PLAYER_MAX_MISSIONS) {
        return;
    }
    singlePlayerMission_t *mission = &campaign->missions[index];

    if (SinglePlayer_ReadQuoted(&cursor, header, sizeof(header)) &&
        SinglePlayer_ReadQuoted(&cursor, name, sizeof(name)) &&
        SinglePlayer_ReadQuoted(&cursor, path, sizeof(path))) {
        snprintf(mission->header, sizeof(mission->header), "%s", header);
        snprintf(mission->name, sizeof(mission->name), "%s", name);
        snprintf(mission->map_path, sizeof(mission->map_path), "%s", path);
    } else {
        cursor = value;
        if (SinglePlayer_ReadQuoted(&cursor, name, sizeof(name))) {
            snprintf(mission->name, sizeof(mission->name), "%s", name);
        }
    }
    SinglePlayer_SetMissionCount(campaign, index);
}

static void SinglePlayer_ParseFileValue(singlePlayerCampaign_t *campaign, DWORD index, char *value) {
    PATHSTR file;
    char *cursor = value;

    if (!campaign || index >= SINGLE_PLAYER_MAX_MISSIONS) {
        return;
    }
    singlePlayerMission_t *mission = &campaign->missions[index];
    if (!SinglePlayer_ReadQuoted(&cursor, file, sizeof(file)) || !file[0]) {
        return;
    }
    if (strchr(file, '\\') || strchr(file, '/')) {
        snprintf(mission->map_path, sizeof(mission->map_path), "%s", file);
    } else {
        snprintf(mission->map_path, sizeof(mission->map_path), "Maps\\Campaign\\%.*s.w3m", (int)(sizeof(mission->map_path) - 19), file);
    }
    SinglePlayer_SetMissionCount(campaign, index);
}

static void SinglePlayer_ParseCampaignLine(singlePlayerCampaign_t *campaign, char *key, char *value) {
    UINAME field;

    if (!campaign) {
        return;
    }
    if (!strcasecmp(key, "Header")) {
        char *cursor = value;
        if (SinglePlayer_ReadQuoted(&cursor, field, sizeof(field))) {
            snprintf(campaign->header, sizeof(campaign->header), "%s", field);
        }
    } else if (!strcasecmp(key, "Name")) {
        char *cursor = value;
        if (SinglePlayer_ReadQuoted(&cursor, field, sizeof(field))) {
            snprintf(campaign->name, sizeof(campaign->name), "%s", field);
        }
    } else if (!strcasecmp(key, "Background")) {
        char *cursor = value;
        if (SinglePlayer_ReadQuoted(&cursor, field, sizeof(field))) {
            snprintf(campaign->background, sizeof(campaign->background), "%s", field);
        }
    } else if (!strcasecmp(key, "Cursor")) {
        campaign->race = (playerRace_t)atoi(value);
    } else {
        DWORD index;
        if (SinglePlayer_ParseIndexedKey(key, "Mission", &index)) {
            SinglePlayer_ParseMissionValue(campaign, index, value);
        } else if (SinglePlayer_ParseIndexedKey(key, "File", &index)) {
            SinglePlayer_ParseFileValue(campaign, index, value);
        }
    }
}

static BOOL SinglePlayer_LoadCampaignFile(LPCSTR file_name) {
    void *buffer = NULL;
    UINAME section = "";
    singlePlayerCampaign_t *campaign = NULL;
    int size = uiimport.FS_ReadFile(file_name, &buffer);
    if (size <= 0 || !buffer) {
        return false;
    }
    LPSTR text = uiimport.MemAlloc(size + 1);
    if (!text) {
        uiimport.FS_FreeFile(buffer);
        return false;
    }
    memcpy(text, buffer, (size_t)size);
    text[size] = '\0';
    uiimport.FS_FreeFile(buffer);

    char *cursor = text;
    while (*cursor) {
        char *line = cursor;

        while (*cursor && *cursor != '\n' && *cursor != '\r') {
            cursor++;
        }
        if (*cursor) {
            *cursor++ = '\0';
            while (*cursor == '\n' || *cursor == '\r') {
                cursor++;
            }
        }

        if ((unsigned char)line[0] == 0xef &&
            (unsigned char)line[1] == 0xbb &&
            (unsigned char)line[2] == 0xbf) {
            line += 3;
        }
        SinglePlayer_StripComment(line);
        char *key = SinglePlayer_Trim(line);
        if (!*key) {
            continue;
        }
        if (*key == '[') {
            char *end = strchr(key + 1, ']');
            if (!end) {
                continue;
            }
            *end = '\0';
            snprintf(section, sizeof(section), "%s", SinglePlayer_Trim(key + 1));
            campaign = strcasecmp(section, "Index") ? SinglePlayer_EnsureCampaign(section) : NULL;
            continue;
        }
        char *eq = strchr(key, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *value = SinglePlayer_Trim(eq + 1);
        key = SinglePlayer_Trim(key);

        if (!strcasecmp(section, "Index") && !strcasecmp(key, "CampaignList")) {
            SinglePlayer_ParseCampaignList(value);
        } else {
            SinglePlayer_ParseCampaignLine(campaign, key, value);
        }
    }

    uiimport.MemFree(text);
    return campaign_count > 0;
}

static void SinglePlayer_FinalizeCampaignOrder(void) {
    if (campaign_order_count) {
        return;
    }
    FOR_LOOP(i, campaign_count) {
        if (campaigns[i].num_missions > 0 && campaign_order_count < SINGLE_PLAYER_MAX_CAMPAIGNS) {
            campaign_order[campaign_order_count++] = i;
        }
    }
}

static BOOL SinglePlayer_ExpansionEnabled(void) {
    LPCSTR value = uiimport.Cvar_String("fs_expansion", "0");
    return value && atoi(value) != 0;
}

static void SinglePlayer_LoadCampaignData(void) {
    LPCSTR campaign_file;
    BOOL const expansion = SinglePlayer_ExpansionEnabled();

    memset(campaigns, 0, sizeof(campaigns));
    memset(campaign_order, 0, sizeof(campaign_order));
    campaign_count = 0;
    campaign_order_count = 0;

    campaign_file = expansion ? Theme_String("CampaignFile", "Default") : NULL;
    if (campaign_file && strcmp(campaign_file, "CampaignFile") &&
        SinglePlayer_LoadCampaignFile(campaign_file)) {
        SinglePlayer_FinalizeCampaignOrder();
        return;
    }
    if ((expansion && SinglePlayer_LoadCampaignFile("UI\\CampaignStrings_exp.txt")) ||
        SinglePlayer_LoadCampaignFile("UI\\CampaignStrings.txt")) {
        SinglePlayer_FinalizeCampaignOrder();
    }
}

static singlePlayerCampaign_t const *SinglePlayer_DefaultCampaign(void) {
    if (campaign_order_count && campaign_order[0] < campaign_count) {
        return &campaigns[campaign_order[0]];
    }
    return campaign_count ? &campaigns[0] : NULL;
}

static LPCSTR SinglePlayer_FirstMissionMap(singlePlayerCampaign_t const *campaign) {
    if (!campaign) {
        return NULL;
    }
    FOR_LOOP(i, campaign->num_missions) {
        if (campaign->missions[i].map_path[0]) {
            return campaign->missions[i].map_path;
        }
    }
    return NULL;
}

static singlePlayerCampaign_t const *SinglePlayer_SelectedCampaign(void) {
    if (selected_campaign_index >= campaign_count) {
        return NULL;
    }
    return &campaigns[selected_campaign_index];
}

static void SinglePlayer_SetHidden(LPFRAMEDEF frame, BOOL hidden) {
    if (frame) {
        UI_SetHidden(frame, hidden);
    }
}

static void SinglePlayer_SetView(singlePlayerView_t view) {
    BOOL const show_campaign = view == SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT ||
                               view == SINGLE_PLAYER_VIEW_MISSION_SELECT;

    current_view = view;

    SinglePlayer_SetHidden(single_player.SinglePlayerMenu, view != SINGLE_PLAYER_VIEW_MAIN);
    SinglePlayer_SetHidden(single_player.MainPanel, false);
    SinglePlayer_SetHidden(single_player.ProfilePanel, true);

    SinglePlayer_SetHidden(single_player.CampaignMenu, !show_campaign);
    SinglePlayer_SetHidden(single_player.CampaignBackdrop_2, true);
    SinglePlayer_SetHidden(single_player.CampaignSelectFrame, view != SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT);
    SinglePlayer_SetHidden(single_player.MissionSelectFrame, view != SINGLE_PLAYER_VIEW_MISSION_SELECT);
    SinglePlayer_SetHidden(single_player.TutorialFrame, true);
    SinglePlayer_SetHidden(single_player.HumanFrame, true);
    SinglePlayer_SetHidden(single_player.TutorialButton, true);
    SinglePlayer_SetHidden(single_player.HumanButton, true);
    SinglePlayer_SetHidden(single_player.SlidingDoors, true);
    SinglePlayer_SetHidden(campaign_list_frame,
                           view != SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT);
    SinglePlayer_SetHidden(mission_list_frame,
                           view != SINGLE_PLAYER_VIEW_MISSION_SELECT);
}

static void SinglePlayer_SetCampaignBackdrop(singlePlayerCampaign_t const *campaign) {
    if (single_player.CampaignBackdrop_2 && campaign && campaign->background[0]) {
        campaign_background_model = UI_LoadModel(campaign->background, true);
        single_player.CampaignBackdrop_2->Portrait.model = campaign_background_model;
        fprintf(stderr, "[UI] Campaign backdrop: skin=\"%s\" model_idx=%u\n",
                campaign->background, (unsigned)campaign_background_model);
    }
}

static void SinglePlayer_DrawCampaignBackdrop(void) {
    LPRENDERER renderer = uiimport.GetRenderer();
    LPCMODEL model = UI_GetModel(campaign_background_model);

    if (renderer && renderer->RenderFrame && model) {
        renderEntity_t entity = {0};
        entity.model = model;
        entity.scale = 1.0f;
        entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
        renderer->SetEntityAnimFrame(model, "Stand", &entity);

        viewDef_t viewdef = {0};
        viewdef.viewport = (RECT){0, 0, 1, 1};
        viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA;
        viewdef.num_entities = 1;
        viewdef.entities = &entity;

        renderer->RenderFrame(&viewdef);
    }
}

static void SinglePlayer_MissionPlayedCvar(singlePlayerCampaign_t const *campaign,
                                           DWORD mission_index,
                                           LPSTR out,
                                           DWORD out_size) {
    char campaign_key[64];
    DWORD used = 0;

    if (!out || !out_size) {
        return;
    }
    memset(campaign_key, 0, sizeof(campaign_key));
    if (campaign) {
        for (LPCSTR p = campaign->key; *p && used + 1 < sizeof(campaign_key); p++) {
            unsigned char ch = (unsigned char)*p;
            campaign_key[used++] = isalnum(ch) ? (char)tolower(ch) : '_';
        }
    }
    snprintf(out, out_size, "wc3_campaign_played_%s_%u",
             campaign_key[0] ? campaign_key : "campaign",
             (unsigned)mission_index);
}

static BOOL SinglePlayer_ShowMission(singlePlayerCampaign_t const *campaign, DWORD mission_index) {
    LPCSTR mode = uiimport.Cvar_String
        ? uiimport.Cvar_String(SINGLE_PLAYER_MISSION_VISIBILITY_CVAR, "all")
        : "all";

    if (!mode || strcasecmp(mode, "played")) {
        return true;
    }

    char cvar_name[128];
    SinglePlayer_MissionPlayedCvar(campaign, mission_index, cvar_name, sizeof(cvar_name));
    LPCSTR played = uiimport.Cvar_String ? uiimport.Cvar_String(cvar_name, "0") : "0";
    return played && atoi(played) != 0;
}

static void SinglePlayer_MarkMissionPlayed(singlePlayerCampaign_t const *campaign, DWORD mission_index) {
    char cvar_name[128];

    if (!campaign || !uiimport.Cvar_Set) {
        return;
    }
    SinglePlayer_MissionPlayedCvar(campaign, mission_index, cvar_name, sizeof(cvar_name));
    uiimport.Cvar_Set(cvar_name, "1");
}

static void SinglePlayer_LaunchMission(singlePlayerCampaign_t const *campaign, DWORD mission_index) {
    char command[256];
    LPCSTR map_path;

    if (!campaign || mission_index >= campaign->num_missions) {
        return;
    }
    map_path = campaign->missions[mission_index].map_path;
    if (!map_path[0]) {
        return;
    }
    SinglePlayer_MarkMissionPlayed(campaign, mission_index);
    snprintf(command, sizeof(command), "map \"%s\"", map_path);
    UI_MenuCommandLocal(command);
}

static void SinglePlayer_PopulateMissionList(singlePlayerCampaign_t const *campaign) {
    memset(&mission_list, 0, sizeof(mission_list));
    if (!campaign) {
        return;
    }

    FOR_LOOP(i, campaign->num_missions) {
        singlePlayerMission_t const *mission = &campaign->missions[i];
        uiMapListItem_t *item;

        if (!mission->map_path[0] || !SinglePlayer_ShowMission(campaign, i)) {
            continue;
        }
        if (mission_list.count >= UI_MAX_MAP_LIST_ITEMS) {
            break;
        }
        item = &mission_list.items[mission_list.count++];
        if (mission->header[0] && mission->name[0]) {
            snprintf(item->name, sizeof(item->name), "%.52s: %.73s", mission->header, mission->name);
        } else {
            snprintf(item->name, sizeof(item->name), "%s", mission->name[0] ? mission->name : mission->map_path);
        }
        snprintf(item->path, sizeof(item->path), "%s", mission->map_path);
        item->flags = i;
    }
}

static void SinglePlayer_PopulateMissionSelect(singlePlayerCampaign_t const *campaign) {
    if (!campaign) {
        return;
    }
    if (single_player.MissionName) {
        UI_SetText(single_player.MissionName, "%s", campaign->name[0] ? campaign->name : campaign->key);
    }
    if (single_player.MissionNameHeader) {
        UI_SetText(single_player.MissionNameHeader, "%s", campaign->header);
    }
    SinglePlayer_PopulateMissionList(campaign);
}

static void SinglePlayer_SelectCampaign(singlePlayerCampaign_t const *campaign) {
    if (!campaign) {
        return;
    }
    selected_campaign_index = (DWORD)(campaign - campaigns);
    SinglePlayer_SetCampaignBackdrop(campaign);
    SinglePlayer_PopulateMissionSelect(campaign);
    SinglePlayer_SetView(SINGLE_PLAYER_VIEW_MISSION_SELECT);
}

static void SinglePlayer_PopulateCampaignList(void) {
    memset(&campaign_list, 0, sizeof(campaign_list));
    FOR_LOOP(i, campaign_order_count) {
        DWORD const campaign_index = campaign_order[i];
        uiMapListItem_t *item;
        singlePlayerCampaign_t const *campaign;

        if (campaign_index >= campaign_count) {
            continue;
        }
        campaign = &campaigns[campaign_index];
        if (!SinglePlayer_FirstMissionMap(campaign)) {
            continue;
        }
        item = &campaign_list.items[campaign_list.count++];
        if (campaign->header[0] && campaign->name[0]) {
            snprintf(item->name, sizeof(item->name), "%.80s: %.46s", campaign->header, campaign->name);
        } else {
            snprintf(item->name, sizeof(item->name), "%s", campaign->name[0] ? campaign->name : campaign->key);
        }
        snprintf(item->path, sizeof(item->path), "%s", campaign->key);
        item->players = 1;
        item->flags = campaign_index;
    }
}

static void SinglePlayer_CreateCampaignList(void) {
    LPFRAMEDEF template_frame;

    if (campaign_list_frame || !single_player.CampaignSelectFrame) {
        return;
    }

    template_frame = UI_FindFrame("MapListBox");
    if (!template_frame) {
        return;
    }

    campaign_list_frame = UI_CloneFrameTree(template_frame, single_player.CampaignSelectFrame);
    if (!campaign_list_frame) {
        return;
    }

    SinglePlayer_PopulateCampaignList();
    UI_SetSize(campaign_list_frame, 0.34f, 0.11f);
    UI_SetPoint(campaign_list_frame,
                FRAMEPOINT_BOTTOMLEFT,
                single_player.BackButton,
                FRAMEPOINT_TOPLEFT,
                -0.14f,
                0.04f);
    UI_BindMapList(campaign_list_frame, &campaign_list, single_player.DifficultySelectLabel, 4, "menu_single_player_campaign_select %u");
}

static void SinglePlayer_CreateMissionList(void) {
    LPFRAMEDEF template_frame;

    if (mission_list_frame || !single_player.MissionSelectFrame) {
        return;
    }

    template_frame = UI_FindFrame("MapListBox");
    if (!template_frame) {
        return;
    }

    mission_list_frame = UI_CloneFrameTree(template_frame, single_player.MissionSelectFrame);
    if (!mission_list_frame) {
        return;
    }

    UI_SetSize(mission_list_frame, 0.34f, 0.28f);
    UI_SetPoint(mission_list_frame,
                FRAMEPOINT_BOTTOMLEFT,
                single_player.BackButton,
                FRAMEPOINT_TOPLEFT,
                -0.14f,
                0.04f);
    UI_BindMapList(mission_list_frame,
                   &mission_list,
                   single_player.DifficultySelectLabel,
                   SINGLE_PLAYER_MISSION_VISIBLE_ROWS,
                   "menu_single_player_mission_select %u");
}

static void SinglePlayer_BindMainMenu(void) {
    if (!single_player.SinglePlayerMenu) {
        return;
    }

    UI_SetOnClick(single_player.CampaignButton, "menu_single_player_campaign");
    UI_SetOnClick(single_player.LoadSavedButton, "");
    UI_SetOnClick(single_player.ViewReplayButton, "");
    UI_SetOnClick(single_player.SkirmishButton, "menu_single_player_skirmish");
    UI_SetOnClick(single_player.ProfileButton, "");
    UI_SetOnClick(single_player.CancelButton, "menu_main");
    if (single_player.ProfileNameText) {
        LPCSTR name = uiimport.Cvar_String ? uiimport.Cvar_String("name", "Player") : "Player";
        UI_SetText(single_player.ProfileNameText, "%s", name && name[0] ? name : "Player");
    }
}

static LPCSTR SinglePlayer_DifficultyName(DWORD difficulty) {
    switch (difficulty) {
        case 0: return UI_GetString("EASY");
        case 2: return UI_GetString("HARD");
        default: return UI_GetString("NORMAL");
    }
}

static void SinglePlayer_UpdateDifficultyTitle(DWORD difficulty) {
    LPFRAMEDEF title = single_player.DifficultySelect
        ? UI_FindChildFrame(single_player.DifficultySelect, "CampaignPopupMenuTitleTextTemplate")
        : NULL;

    if (title) {
        UI_SetText(title, "%s", SinglePlayer_DifficultyName(difficulty));
    }
}

static void SinglePlayer_BindCampaignMenu(void) {
    LPFRAMEDEF DifficultyMenu;
    DWORD difficulty = 1;
    LPCSTR difficulty_value;

    if (!single_player.CampaignMenu) {
        return;
    }

    UI_SetOnClick(single_player.BackButton, "menu_single_player_campaign_back");
    UI_SetOnClick(single_player.HumanButton, "menu_single_player_campaign_human");
    UI_SetOnClick(single_player.OrcButton, "menu_single_player_campaign_orc");
    UI_SetOnClick(single_player.UndeadButton, "menu_single_player_campaign_undead");
    UI_SetOnClick(single_player.NightElfButton, "menu_single_player_campaign_night_elf");
    UI_SetOnClick(single_player.TutorialButton, "menu_single_player_campaign_tutorial");

    DifficultyMenu = single_player.DifficultySelect
        ? UI_FindChildFrame(single_player.DifficultySelect, "CampaignPopupMenuMenu")
        : NULL;
    if (DifficultyMenu) {
        UI_MenuClearItems(DifficultyMenu);
        UI_MenuAddItem(DifficultyMenu, UI_GetString("EASY"), 0);
        UI_MenuAddItem(DifficultyMenu, UI_GetString("NORMAL"), 1);
        UI_MenuAddItem(DifficultyMenu, UI_GetString("HARD"), 2);
        UI_SetOnClick(DifficultyMenu, "menu_single_player_difficulty %u");
        UI_SetHidden(DifficultyMenu, true);
    }
    difficulty_value = uiimport.Cvar_String
        ? uiimport.Cvar_String("wc3_campaign_difficulty", "1") : "1";
    if (difficulty_value) {
        LONG value = atoi(difficulty_value);
        if (value >= 0 && value <= 2) {
            difficulty = (DWORD)value;
        }
    }
    SinglePlayer_UpdateDifficultyTitle(difficulty);
}

static void SinglePlayerMenu_Init(void) {
    uiimport.Printf("SinglePlayerMenu_Init\n");
    UI_PreloadGlueSceneModels();
    SinglePlayer_LoadCampaignData();
    campaign_list_frame = NULL;
    mission_list_frame = NULL;
    memset(&campaign_list, 0, sizeof(campaign_list));
    memset(&mission_list, 0, sizeof(mission_list));

    if (single_player.WarCraftIIILogo) {
        single_player.WarCraftIIILogo->Portrait.model = UI_LoadModel("CampaignLogo", true);
    }

    SinglePlayer_BindMainMenu();
    SinglePlayer_BindCampaignMenu();
    SinglePlayer_CreateCampaignList();
    SinglePlayer_CreateMissionList();
    SinglePlayer_SetCampaignBackdrop(SinglePlayer_DefaultCampaign());
    selected_campaign_index = SINGLE_PLAYER_MAX_CAMPAIGNS;
    SinglePlayer_SetView(SINGLE_PLAYER_VIEW_MAIN);
}

static void SinglePlayerMenu_Shutdown(void) {
}

static void SinglePlayerMenu_Refresh(int msec) {
    (void)msec;
}

static void SinglePlayerMenu_Draw(void) {
    if (current_view == SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT ||
        current_view == SINGLE_PLAYER_VIEW_MISSION_SELECT) {
        SinglePlayer_DrawCampaignBackdrop();
        if (single_player.CampaignMenu) {
            UI_DrawFrame(single_player.CampaignMenu);
        }
        return;
    }

    UI_DrawGlueScene("SinglePlayer Stand");
    if (single_player.SinglePlayerMenu) {
        UI_DrawFrame(single_player.SinglePlayerMenu);
    }
}

static void SinglePlayerMenu_KeyEvent(int key, BOOL down) {
    (void)key;
    (void)down;
}

void SinglePlayerMenu_ShowMain(void) {
    if (single_player.ProfileNameText) {
        LPCSTR name = uiimport.Cvar_String ? uiimport.Cvar_String("name", "Player") : "Player";
        UI_SetText(single_player.ProfileNameText, "%s", name && name[0] ? name : "Player");
    }
    SinglePlayer_SetView(SINGLE_PLAYER_VIEW_MAIN);
}

void SinglePlayerMenu_ShowCampaign(void) {
    SinglePlayer_SetCampaignBackdrop(SinglePlayer_DefaultCampaign());
    selected_campaign_index = SINGLE_PLAYER_MAX_CAMPAIGNS;
    SinglePlayer_SetView(SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT);
}

void SinglePlayerMenu_BackCampaign(void) {
    if (current_view == SINGLE_PLAYER_VIEW_MISSION_SELECT) {
        SinglePlayer_SetView(SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT);
        return;
    }
    UI_ShowSinglePlayerMenu();
}

void SinglePlayerMenu_LaunchCampaign(LPCSTR name) {
    singlePlayerCampaign_t const *campaign = SinglePlayer_FindCampaign(name);
    SinglePlayer_SelectCampaign(campaign);
}

void SinglePlayerMenu_LaunchCampaignIndex(DWORD index) {
    DWORD campaign_index;

    if (index >= campaign_list.count) {
        return;
    }
    campaign_list.selected = index;
    campaign_index = campaign_list.items[index].flags;
    if (campaign_index < campaign_count) {
        SinglePlayer_SelectCampaign(&campaigns[campaign_index]);
    }
}

void SinglePlayerMenu_LaunchMissionIndex(DWORD index) {
    singlePlayerCampaign_t const *campaign = SinglePlayer_SelectedCampaign();
    DWORD mission_index;

    if (!campaign || index >= mission_list.count) {
        return;
    }
    mission_list.selected = index;
    mission_index = mission_list.items[index].flags;
    SinglePlayer_LaunchMission(campaign, mission_index);
}

void SinglePlayerMenu_SetDifficulty(DWORD difficulty) {
    char value[8];

    if (difficulty > 2) {
        return;
    }
    SinglePlayer_UpdateDifficultyTitle(difficulty);
    if (uiimport.Cvar_Set) {
        snprintf(value, sizeof(value), "%u", (unsigned)difficulty);
        uiimport.Cvar_Set("wc3_campaign_difficulty", value);
    }
}

uiScreen_t singlePlayerMenuScreen = {
    .name = "single-player",
    .load = SinglePlayerMenu_LoadScreen,
    .init = SinglePlayerMenu_Init,
    .shutdown = SinglePlayerMenu_Shutdown,
    .refresh = SinglePlayerMenu_Refresh,
    .draw = SinglePlayerMenu_Draw,
    .key_event = SinglePlayerMenu_KeyEvent,
};
