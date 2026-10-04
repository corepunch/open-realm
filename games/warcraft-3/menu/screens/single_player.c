/*
 * ui/screens/single_player.c — Single player menu screen.
 */

#include "../menu_local.h"
#include "../menu_screen.h"
#include "../generated/single_player_menu.h"
#include "common/campaign_progress.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#ifndef _WIN32
#include <strings.h>
#endif

#define SINGLE_PLAYER_MAX_CAMPAIGNS 16 // campaigns; UI parse/storage capacity; bounds authored campaign entries
#define SINGLE_PLAYER_MAX_MISSIONS 128 // missions; UI parse/storage capacity; bounds authored mission entries
#define SINGLE_PLAYER_LIST_ROWS 10 // rows; TFT CampaignListBox.fdf scroll bar (0.315625) holds ten list slots
#define SINGLE_PLAYER_LIST_ROW_HEIGHT 0.0315625f // FDF units; one slot; retail rows measure 0.032 apart
#define SINGLE_PLAYER_LIST_WIDTH 0.27f // FDF units; arrow, header and name column
#define SINGLE_PLAYER_LIST_X -0.287f // FDF units from CampaignMenu TOPRIGHT; retail arrows start at x=0.513
#define SINGLE_PLAYER_LIST_Y -0.1274f // FDF units from CampaignMenu TOPRIGHT; centres the box 0.285 below the top
#define SINGLE_PLAYER_LIST_MAX_ENTRIES (UI_MAX_MAP_LIST_ITEMS * 2) // entries; every campaign row is followed by a blank
#define SINGLE_PLAYER_CAMPAIGN_VISIBILITY_CVAR "wc3_campaign_visibility"
#define SINGLE_PLAYER_LIST_FLAG_CINEMATIC 0x80000000u // bit; marks cinematic list items; separates them from mission indices
#define SINGLE_PLAYER_LIST_INDEX_MASK 0x7fffffffu // bitmask; retains the 31-bit item index; strips the cinematic marker

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

typedef enum {
    SINGLE_PLAYER_CINEMATIC_INTRO,
    SINGLE_PLAYER_CINEMATIC_OPEN,
    SINGLE_PLAYER_CINEMATIC_END,
    SINGLE_PLAYER_CINEMATIC_COUNT,
} singlePlayerCinematicKind_t;

typedef struct {
    UINAME header;
    UINAME name;
    PATHSTR movie_path;
} singlePlayerCinematic_t;

typedef struct {
    playerRace_t race;
    UINAME key;
    UINAME header;
    UINAME name;
    UINAME background;
    bool default_open;
    singlePlayerMission_t missions[SINGLE_PLAYER_MAX_MISSIONS];
    uint32_t num_missions;
    singlePlayerCinematic_t cinematics[SINGLE_PLAYER_CINEMATIC_COUNT];
} singlePlayerCampaign_t;

typedef struct {
    cstring_t name;
    size_t offset, size;
    bzFieldType_t type;
} singlePlayerCampaignField_t;

#define CAMPAIGN_FIELD(NAME, FIELD, TYPE) \
    { NAME, offsetof(singlePlayerCampaign_t, FIELD), sizeof(((singlePlayerCampaign_t *)0)->FIELD), TYPE }

static singlePlayerCampaignField_t const campaign_fields[] = {
    CAMPAIGN_FIELD("Header", header, BZ_FIELD_CHAR_ARRAY),
    CAMPAIGN_FIELD("Name", name, BZ_FIELD_CHAR_ARRAY),
    CAMPAIGN_FIELD("Background", background, BZ_FIELD_CHAR_ARRAY),
    CAMPAIGN_FIELD("Cursor", race, BZ_FIELD_U32),
    CAMPAIGN_FIELD("DefaultOpen", default_open, BZ_FIELD_BOOL),
};

static cstring_t const solo_lft[] = {
    "ProfilePanel",
    NULL,
};

/* Data fallbacks and failures that blank the campaign screen must be visible in the in-game console, not only stderr. */
static void SinglePlayer_Warn(cstring_t fmt, ...) {
    char message[512];
    va_list args;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    fprintf(stderr, "UI: warning: %s\n", message);
    if (mi.Printf) mi.Printf("Warning: %s", message);
}

static SinglePlayerMenu_t single_player;

/* Retail builds both selectors from CampaignStrings at runtime (Warsmash's CampaignMenuUI does the same): rows of
 * an arrow (map/campaign) or camera (movie) button with a gold header and a grey name, laid out in fixed slots.
 * Campaigns are separated by a blank slot and the Introduction cinematic is followed by one; a column that fits is
 * centred in the ten-slot box, a longer one scrolls. The static TutorialFrame/Mission0Frame rows authored by the
 * 1.00 CampaignMenu.fdf are never shown by patched clients, so they stay hidden. */
typedef struct {
    frameDef_t *slot, *arrow, *camera, *label, *desc;
} singlePlayerListRow_t;

typedef struct {
    uint32_t item; // index into the list's uiMapListState_t; UINT32_MAX for a blank slot
    bool camera;
    cstring_t header, name;
} singlePlayerListEntry_t;

typedef struct {
    frameDef_t *box;
    singlePlayerListRow_t rows[SINGLE_PLAYER_LIST_ROWS];
    singlePlayerListEntry_t entries[SINGLE_PLAYER_LIST_MAX_ENTRIES];
    uint32_t count, scroll;
    cstring_t command;
} singlePlayerList_t;

static singlePlayerList_t campaign_rows = { .command = "menu_single_player_campaign_select %u" };
static singlePlayerList_t mission_rows = { .command = "menu_single_player_mission_select %u" };
static singlePlayerCampaign_t campaigns[SINGLE_PLAYER_MAX_CAMPAIGNS];
static uint32_t campaign_count;
static uint32_t campaign_order[SINGLE_PLAYER_MAX_CAMPAIGNS];
static uint32_t campaign_order_count;
static uiMapListState_t campaign_list;
static uiMapListState_t mission_list;
static uint32_t campaign_background_model = 0;
static uiBirthSequence_t campaign_background_birth;
static bool campaign_background_has_render_time;
static uint32_t campaign_background_last_render_time;
static uint32_t selected_campaign_index = SINGLE_PLAYER_MAX_CAMPAIGNS;
static singlePlayerView_t current_view = SINGLE_PLAYER_VIEW_MAIN;
static wc3CampaignProgress_t campaign_progress;

static void SinglePlayer_ReleaseCampaignBackdropState(void) {
    refExport_t *renderer = mi.GetRenderer ? mi.GetRenderer() : NULL;
    if (renderer && renderer->ReleaseEntityCameraEvents)
        renderer->ReleaseEntityCameraEvents((uintptr_t)&campaign_background_model);
}

static bool SinglePlayerMenu_LoadScreen(void) {
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
    bool quoted = false;
    for (char *p = line; *p; p++) {
        if (*p == '"') quoted = !quoted;
        if (!quoted && p[0] == '/' && p[1] == '/') {
            *p = '\0';
            return;
        }
    }
}

static bool SinglePlayer_ReadQuoted(char **cursor, string_t out, uint32_t out_size) {
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

static singlePlayerCampaign_t *SinglePlayer_FindCampaignMutable(cstring_t key) {
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

static singlePlayerCampaign_t *SinglePlayer_EnsureCampaign(cstring_t key) {
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

static singlePlayerCampaign_t const *SinglePlayer_FindCampaign(cstring_t name) {
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

static void SinglePlayer_AddCampaignOrder(cstring_t key) {
    singlePlayerCampaign_t *campaign = SinglePlayer_EnsureCampaign(key);
    if (!campaign || campaign_order_count >= SINGLE_PLAYER_MAX_CAMPAIGNS) {
        return;
    }
    FOR_LOOP(i, campaign_order_count) {
        if (campaign_order[i] == (uint32_t)(campaign - campaigns)) {
            return;
        }
    }
    campaign_order[campaign_order_count++] = (uint32_t)(campaign - campaigns);
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

static bool SinglePlayer_ParseIndexedKey(cstring_t key, cstring_t prefix, uint32_t *index) {
    size_t prefix_len = strlen(prefix);

    if (strncasecmp(key, prefix, prefix_len))
        return false;
    char *end = NULL;
    unsigned long value = strtoul(key + prefix_len, &end, 10);
    if (*end || value >= SINGLE_PLAYER_MAX_MISSIONS)
        return false;
    *index = (uint32_t)value;
    return true;
}

static void SinglePlayer_SetMissionCount(singlePlayerCampaign_t *campaign, uint32_t index) {
    if (campaign && index < SINGLE_PLAYER_MAX_MISSIONS && campaign->num_missions <= index) {
        campaign->num_missions = index + 1;
    }
}

static void SinglePlayer_ParseMissionValue(singlePlayerCampaign_t *campaign, uint32_t index, char *value) {
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

static void SinglePlayer_ParseFileValue(singlePlayerCampaign_t *campaign, uint32_t index, char *value) {
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

static void SinglePlayer_ParseTitleValue(singlePlayerCampaign_t *campaign, uint32_t index, char *value) {
    char *cursor = value;
    UINAME header;

    if (!campaign || index >= SINGLE_PLAYER_MAX_MISSIONS || !SinglePlayer_ReadQuoted(&cursor, header, sizeof(header))) {
        return;
    }
    snprintf(campaign->missions[index].header, sizeof(campaign->missions[index].header), "%s", header);
    SinglePlayer_SetMissionCount(campaign, index);
}

static void SinglePlayer_ParseCinematicValue(singlePlayerCinematic_t *cinematic, char *value) {
    char *cursor = value;
    UINAME header;
    UINAME name;
    PATHSTR movie;

    if (!cinematic ||
        !SinglePlayer_ReadQuoted(&cursor, header, sizeof(header)) ||
        !SinglePlayer_ReadQuoted(&cursor, name, sizeof(name)) ||
        !SinglePlayer_ReadQuoted(&cursor, movie, sizeof(movie))) {
        return;
    }

    snprintf(cinematic->header, sizeof(cinematic->header), "%s", header);
    snprintf(cinematic->name, sizeof(cinematic->name), "%s", name);
    snprintf(cinematic->movie_path, sizeof(cinematic->movie_path), "%s", movie);
}

/* Scalar CampaignStrings fields use one descriptor grammar; repeated productions remain explicit. */
static bool SinglePlayer_ParseCampaignField(singlePlayerCampaign_t *campaign, cstring_t key, char *value) {
    if (!campaign || !key) return false;
    FOR_LOOP(i, sizeof(campaign_fields) / sizeof(campaign_fields[0])) {
        singlePlayerCampaignField_t const *field = &campaign_fields[i];
        uint8_t *dst;
        if (strcasecmp(key, field->name)) continue;
        dst = (uint8_t *)campaign + field->offset;
        if (field->type == BZ_FIELD_CHAR_ARRAY) {
            char *cursor = value;
            UINAME text;
            if (SinglePlayer_ReadQuoted(&cursor, text, sizeof(text))) snprintf((string_t)dst, field->size, "%s", text);
        } else if (field->type == BZ_FIELD_BOOL) {
            *(bool *)dst = value && (atoi(value) != 0 || !strcasecmp(value, "true"));
        } else if (field->type == BZ_FIELD_U32) {
            uint32_t const number = value ? (uint32_t)atoi(value) : 0;
            memcpy(dst, &number, MIN(field->size, sizeof(number)));
        } else {
            fprintf(stderr, "CampaignStrings: unsupported scalar type %u for '%s'\n", (unsigned)field->type, key);
        }
        return true;
    }
    return false;
}

/* Retail 1.00 War3.mpq predates the CampaignList schema: missions are split into TitleN/MissionN/FileN,
 * cinematics carry only a display name, and their shared headers live in a [Label] section. */
static cstring_t const legacy_cinematic_keys[SINGLE_PLAYER_CINEMATIC_COUNT] = { "InCinematic", "OpCinematic", "EdCinematic" };
static cstring_t const legacy_cinematic_suffixes[SINGLE_PLAYER_CINEMATIC_COUNT] = { "In", "Op", "Ed" };
static UINAME legacy_cinematic_labels[SINGLE_PLAYER_CINEMATIC_COUNT];

static bool SinglePlayer_ParseLegacyCinematic(singlePlayerCampaign_t *campaign, cstring_t key, char *value) {
    FOR_LOOP(i, SINGLE_PLAYER_CINEMATIC_COUNT) {
        singlePlayerCinematic_t *cinematic;
        char *cursor = value;
        UINAME name;

        if (strcasecmp(key, legacy_cinematic_keys[i])) continue;
        if (!campaign) {
            if (SinglePlayer_ReadQuoted(&cursor, name, sizeof(name)))
                snprintf(legacy_cinematic_labels[i], sizeof(legacy_cinematic_labels[i]), "%s", name);
            return true;
        }
        cinematic = &campaign->cinematics[i];
        if (!SinglePlayer_ReadQuoted(&cursor, name, sizeof(name)) || !name[0]) return true;
        snprintf(cinematic->name, sizeof(cinematic->name), "%s", name);
        snprintf(cinematic->movie_path, sizeof(cinematic->movie_path), "%s%s", campaign->key, legacy_cinematic_suffixes[i]);
        return true;
    }
    return false;
}

/* Later CampaignStrings name the war3skins backdrop explicitly; 1.00 relies on the <Key>Backdrop skin entry. */
static void SinglePlayer_FinalizeLegacyCampaigns(void) {
    FOR_LOOP(i, campaign_count) {
        singlePlayerCampaign_t *campaign = &campaigns[i];
        if (!campaign->background[0]) {
            snprintf(campaign->background, sizeof(campaign->background), "%.*sBackdrop",
                     (int)(sizeof(campaign->background) - 9), campaign->key);
            SinglePlayer_Warn("CampaignStrings: campaign '%s' has no Background key (pre-1.01 data?); using skin '%s'",
                              campaign->key, campaign->background);
        }
        FOR_LOOP(j, SINGLE_PLAYER_CINEMATIC_COUNT) {
            singlePlayerCinematic_t *cinematic = &campaign->cinematics[j];
            if (cinematic->movie_path[0] && !cinematic->header[0])
                snprintf(cinematic->header, sizeof(cinematic->header), "%s", legacy_cinematic_labels[j]);
        }
    }
}

static void SinglePlayer_ParseCampaignLine(singlePlayerCampaign_t *campaign, char *key, char *value) {
    if (SinglePlayer_ParseLegacyCinematic(campaign, key, value)) return;
    if (!campaign) return;
    if (SinglePlayer_ParseCampaignField(campaign, key, value)) return;
    if (!strcasecmp(key, "IntroCinematic")) {
        SinglePlayer_ParseCinematicValue(&campaign->cinematics[SINGLE_PLAYER_CINEMATIC_INTRO], value);
    } else if (!strcasecmp(key, "OpenCinematic")) {
        SinglePlayer_ParseCinematicValue(&campaign->cinematics[SINGLE_PLAYER_CINEMATIC_OPEN], value);
    } else if (!strcasecmp(key, "EndCinematic")) {
        SinglePlayer_ParseCinematicValue(&campaign->cinematics[SINGLE_PLAYER_CINEMATIC_END], value);
    } else {
        uint32_t index;
        if (SinglePlayer_ParseIndexedKey(key, "Mission", &index)) SinglePlayer_ParseMissionValue(campaign, index, value);
        else if (SinglePlayer_ParseIndexedKey(key, "File", &index)) SinglePlayer_ParseFileValue(campaign, index, value);
        else if (SinglePlayer_ParseIndexedKey(key, "Title", &index)) SinglePlayer_ParseTitleValue(campaign, index, value);
    }
}

static bool SinglePlayer_LoadCampaignFile(cstring_t file_name) {
    void *buffer = NULL;
    UINAME section = "";
    singlePlayerCampaign_t *campaign = NULL;
    int size = mi.FS_ReadFile(file_name, &buffer);
    if (size <= 0 || !buffer) {
        return false;
    }
    string_t text = mi.MemAlloc(size + 1);
    if (!text) {
        mi.FS_FreeFile(buffer);
        return false;
    }
    memcpy(text, buffer, (size_t)size);
    text[size] = '\0';
    mi.FS_FreeFile(buffer);

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
            campaign = strcasecmp(section, "Index") && strcasecmp(section, "Label")
                ? SinglePlayer_EnsureCampaign(section) : NULL;
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

    mi.MemFree(text);
    SinglePlayer_FinalizeLegacyCampaigns();
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

static bool SinglePlayer_ExpansionEnabled(void) {
    cstring_t value = mi.Cvar_String("fs_expansion", "0");
    return value && atoi(value) != 0;
}

static void SinglePlayer_LoadCampaignData(void) {
    cstring_t campaign_file;
    bool const expansion = SinglePlayer_ExpansionEnabled();

    memset(campaigns, 0, sizeof(campaigns));
    memset(campaign_order, 0, sizeof(campaign_order));
    memset(legacy_cinematic_labels, 0, sizeof(legacy_cinematic_labels));
    campaign_count = 0;
    campaign_order_count = 0;

    campaign_file = Theme_String("CampaignFile", "Default");
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

static bool SinglePlayer_ShowCampaign(singlePlayerCampaign_t const *campaign);

static singlePlayerCampaign_t const *SinglePlayer_DefaultCampaign(void) {
    FOR_LOOP(i, campaign_order_count) {
        uint32_t const campaign_index = campaign_order[i];
        if (campaign_index < campaign_count && SinglePlayer_ShowCampaign(&campaigns[campaign_index])) {
            return &campaigns[campaign_index];
        }
    }
    return NULL;
}

static cstring_t SinglePlayer_FirstMissionMap(singlePlayerCampaign_t const *campaign) {
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

static void SinglePlayer_SetHidden(frameDef_t *frame, bool hidden) {
    if (frame) {
        UI_SetHidden(frame, hidden);
    }
}

static void SinglePlayer_HideAuthoredRows(void) {
    frameDef_t *const frames[] = {
        single_player.TutorialFrame, single_player.HumanFrame, single_player.UndeadFrame,
        single_player.OrcFrame, single_player.NightElfFrame,
    };
    char name[32];

    FOR_LOOP(i, sizeof(frames) / sizeof(frames[0])) SinglePlayer_SetHidden(frames[i], true);
    for (uint32_t i = 0; single_player.MissionSelectFrame; i++) {
        frameDef_t *row;
        snprintf(name, sizeof(name), "Mission%uFrame", (unsigned)i);
        if (!(row = UI_FindChildFrame(single_player.MissionSelectFrame, name))) break;
        SinglePlayer_SetHidden(row, true);
    }
}

static void SinglePlayer_SetView(singlePlayerView_t view) {
    bool const show_campaign = view == SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT ||
                               view == SINGLE_PLAYER_VIEW_MISSION_SELECT;

    current_view = view;

    SinglePlayer_SetHidden(single_player.SinglePlayerMenu, view != SINGLE_PLAYER_VIEW_MAIN);
    SinglePlayer_SetHidden(single_player.MainPanel, false);
    SinglePlayer_SetHidden(single_player.ProfilePanel, true);

    SinglePlayer_SetHidden(single_player.CampaignMenu, !show_campaign);
    SinglePlayer_SetHidden(single_player.CampaignBackdrop_2, true);
    SinglePlayer_SetHidden(single_player.CampaignSelectFrame, view != SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT);
    SinglePlayer_SetHidden(single_player.MissionSelectFrame, view != SINGLE_PLAYER_VIEW_MISSION_SELECT);
    SinglePlayer_HideAuthoredRows();
    SinglePlayer_SetHidden(single_player.SlidingDoors, true);
}

static void SinglePlayer_SetCampaignBackdrop(singlePlayerCampaign_t const *campaign) {
    if (campaign && !campaign->background[0])
        SinglePlayer_Warn("campaign '%s' has no backdrop model; the campaign screen will be black", campaign->key);
    if (single_player.CampaignBackdrop_2 && campaign && campaign->background[0]) {
        SinglePlayer_ReleaseCampaignBackdropState();
        campaign_background_model = UI_LoadModel(campaign->background, true);
        single_player.CampaignBackdrop_2->Portrait.model = campaign_background_model;
        fprintf(stderr, "[UI] Campaign backdrop: skin=\"%s\" model_idx=%u\n",
                campaign->background, (unsigned)campaign_background_model);
        if (!campaign_background_model || !UI_GetModel(campaign_background_model)) {
            SinglePlayer_Warn("campaign backdrop '%s' did not resolve to a loaded model; the campaign screen will be black",
                              campaign->background);
        }
    }
}

static void SinglePlayer_BeginCampaignBackdropBirth(singlePlayerCampaign_t const *campaign) {
    refExport_t *renderer = mi.GetRenderer();
    model_t const *model = UI_GetModel(campaign_background_model);

    SinglePlayer_ReleaseCampaignBackdropState();
    UI_BirthSequenceReset(&campaign_background_birth);
    campaign_background_has_render_time = false;
    UI_BirthSequenceBegin(&campaign_background_birth, renderer, model,
                          campaign ? campaign->background : "campaign backdrop");
}

static cstring_t SinglePlayer_CampaignBackdropAnimation(refExport_t *renderer,
                                                        string_t anim, size_t anim_size) {
    uint32_t duration;
    cstring_t stable = "Stand";

    /* SetEntityAnimFrame runs before RenderFrame installs this backdrop's clock. */
    if (renderer && renderer->GetModelAnimationDuration &&
        renderer->GetModelAnimationDuration(UI_GetModel(campaign_background_model), "Stand", &duration) &&
        duration) {
        snprintf(anim, anim_size, "Stand@%.4f", (float)(M_Time() % duration) / (float)duration);
        stable = anim;
    }
    return UI_BirthSequenceAnimation(&campaign_background_birth, anim, anim_size, stable);
}

static void SinglePlayer_DrawCampaignBackdrop(void) {
    refExport_t *renderer = mi.GetRenderer();
    model_t const *model = UI_GetModel(campaign_background_model);

    if (renderer && renderer->RenderFrame && model) {
        uint32_t scene_time = M_Time();
        uint32_t delta_time = campaign_background_has_render_time &&
                scene_time >= campaign_background_last_render_time
            ? scene_time - campaign_background_last_render_time : 0;
        renderEntity_t entity = {0};
        entity.model = model;
        entity.number = MAX_GAME_ENTITIES - 3;
        entity.instance_id = (uintptr_t)&campaign_background_model;
        entity.scale = 1.0f;
        entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
        if (renderer->SetEntityAnimFrame) {
            char anim[32];
            renderer->SetEntityAnimFrame(model,
                                         SinglePlayer_CampaignBackdropAnimation(renderer, anim, sizeof(anim)),
                                         &entity);
        }

        viewDef_t viewdef = {0};
        viewdef.viewport = (rect_t){0, 0, 1, 1};
        viewdef.time = scene_time;
        viewdef.deltaTime = delta_time;
        viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG |
                          RDF_USE_ENTITY_CAMERA | RDF_ISOLATED_PARTICLES;
        viewdef.num_entities = 1;
        viewdef.entities = &entity;

        renderer->RenderFrame(&viewdef);
        campaign_background_last_render_time = scene_time;
        campaign_background_has_render_time = true;
    }
}

static bool SinglePlayer_UnlockedOnly(void) {
    cstring_t mode = mi.Cvar_String
        ? mi.Cvar_String(SINGLE_PLAYER_CAMPAIGN_VISIBILITY_CVAR, "all")
        : "all";
    return mode && !strcasecmp(mode, "unlocked");
}

static uint32_t SinglePlayer_CampaignEdition(void) {
    return SinglePlayer_ExpansionEnabled() ? WC3_CAMPAIGN_EDITION_TFT : WC3_CAMPAIGN_EDITION_ROC;
}

static void SinglePlayer_LoadCampaignProgress(void) {
    PATHSTR path;

    wc3_campaign_progress_init(&campaign_progress);
    path[0] = '\0';
    mi.UserPath(WC3_CAMPAIGN_PROGRESS_FILENAME, path, sizeof(path));
    if (path[0]) wc3_campaign_progress_load(path, &campaign_progress);
}

static int32_t SinglePlayer_CampaignProgressIndex(singlePlayerCampaign_t const *campaign) {
    if (!campaign) return -1;
    return wc3_campaign_progress_campaign_index(SinglePlayer_CampaignEdition(), campaign->key);
}

static bool SinglePlayer_ShowCampaign(singlePlayerCampaign_t const *campaign) {
    int32_t campaign_index;

    if (!campaign || !SinglePlayer_UnlockedOnly()) return campaign != NULL;
    campaign_index = SinglePlayer_CampaignProgressIndex(campaign);
    if (campaign_index >= 0) {
        wc3CampaignProgressKey_t const key = MAKE(wc3CampaignProgressKey_t,
            .edition = SinglePlayer_CampaignEdition(),
            .campaign = (uint32_t)campaign_index);
        if (wc3_campaign_progress_has_campaign(&campaign_progress, key))
            return wc3_campaign_progress_campaign_available(&campaign_progress, key);
    }
    return campaign->default_open;
}

static bool SinglePlayer_ShowMission(singlePlayerCampaign_t const *campaign, uint32_t mission_index) {
    int32_t campaign_index;

    if (!campaign || mission_index >= campaign->num_missions) return false;
    if (!SinglePlayer_UnlockedOnly()) return true;
    campaign_index = SinglePlayer_CampaignProgressIndex(campaign);
    if (campaign_index >= 0) {
        wc3CampaignProgressKey_t const key = MAKE(wc3CampaignProgressKey_t,
            .edition = SinglePlayer_CampaignEdition(),
            .campaign = (uint32_t)campaign_index,
            .mission = mission_index);
        if (wc3_campaign_progress_has_mission(&campaign_progress, key))
            return wc3_campaign_progress_mission_available(&campaign_progress, key);
    }
    return mission_index == 0 && SinglePlayer_ShowCampaign(campaign);
}

static void SinglePlayer_LaunchMission(singlePlayerCampaign_t const *campaign, uint32_t mission_index) {
    char command[MAX_PATHLEN + 7];
    cstring_t map_path;

    if (!campaign || mission_index >= campaign->num_missions) return;
    map_path = campaign->missions[mission_index].map_path;
    if (!map_path[0]) return;
    snprintf(command, sizeof(command), "map \"%s\"", map_path);
    UI_QueueCommand(command);
}

#ifdef BZ_FFMPEG
static void SinglePlayer_MovieAssetPath(cstring_t movie, string_t out, uint32_t out_size) {
    if (!out || !out_size) {
        return;
    }
    out[0] = '\0';
    if (!movie || !movie[0]) {
        return;
    }

    if (strchr(movie, '\\') || strchr(movie, '/')) {
        snprintf(out, out_size, "%s", movie);
    } else if (strchr(movie, '.')) {
        snprintf(out, out_size, "Movies\\%s", movie);
    } else {
        snprintf(out, out_size, "Movies\\%s.mpq", movie);
    }
}

static void SinglePlayer_AddCinematicItem(singlePlayerCampaign_t const *campaign,
                                           singlePlayerCinematicKind_t kind) {
    singlePlayerCinematic_t const *cinematic;
    uiMapListItem_t *item;

    if (!campaign || kind >= SINGLE_PLAYER_CINEMATIC_COUNT ||
        mission_list.count >= UI_MAX_MAP_LIST_ITEMS) {
        return;
    }
    cinematic = &campaign->cinematics[kind];
    if (!cinematic->movie_path[0]) {
        return;
    }

    item = &mission_list.items[mission_list.count++];
    if (cinematic->header[0] && cinematic->name[0]) {
        snprintf(item->name, sizeof(item->name),
                 "Cinematic: %.43s: %.66s", cinematic->header, cinematic->name);
    } else {
        cstring_t label = cinematic->name[0] ? cinematic->name : cinematic->header;
        snprintf(item->name, sizeof(item->name), "Cinematic: %.115s",
                 label[0] ? label : cinematic->movie_path);
    }
    SinglePlayer_MovieAssetPath(cinematic->movie_path, item->path, sizeof(item->path));
    item->flags = SINGLE_PLAYER_LIST_FLAG_CINEMATIC | (uint32_t)kind;
}
#endif

static void SinglePlayer_PopulateMissionList(singlePlayerCampaign_t const *campaign) {
    memset(&mission_list, 0, sizeof(mission_list));
    if (!campaign) {
        return;
    }

#ifdef BZ_FFMPEG
    /* Campaign cinematics surround the playable mission sequence. Intro is
     * the campaign-wide introductory movie, Open is the campaign opening, and
     * End belongs after the final mission. Keep the temporary text rows in
     * that structural order until the retail camera-button presentation lands. */
    SinglePlayer_AddCinematicItem(campaign, SINGLE_PLAYER_CINEMATIC_INTRO);
    SinglePlayer_AddCinematicItem(campaign, SINGLE_PLAYER_CINEMATIC_OPEN);
#endif

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
            snprintf(item->name, sizeof(item->name), "%.*s", (int)sizeof(item->name) - 1,
                     mission->name[0] ? mission->name : mission->map_path);
        }
        snprintf(item->path, sizeof(item->path), "%s", mission->map_path);
        item->flags = i;
    }

#ifdef BZ_FFMPEG
    SinglePlayer_AddCinematicItem(campaign, SINGLE_PLAYER_CINEMATIC_END);
#endif
}

static frameDef_t *SinglePlayer_CloneTemplate(cstring_t template_name, frameDef_t *parent, cstring_t suffix) {
    frameDef_t const *source = UI_FindFrame(template_name);
    frameDef_t *frame = source ? UI_CloneFrameTree(source, parent) : NULL;

    if (!frame) {
        SinglePlayer_Warn("campaign list template '%s' is missing; selector rows cannot be built", template_name);
        return NULL;
    }
    snprintf(frame->Name, sizeof(frame->Name), "%s%s", parent->Name, suffix);
    return frame;
}

/* The FDF tree outlives screen re-inits, so a box built by an earlier init is re-bound by name, not respawned. */
static bool SinglePlayer_BindList(singlePlayerList_t *list, frameDef_t *parent, cstring_t name) {
    char row_name[sizeof(UINAME)];

    if (!(list->box = UI_FindChildFrame(parent, name))) return false;
    FOR_LOOP(i, SINGLE_PLAYER_LIST_ROWS) {
        singlePlayerListRow_t *row = &list->rows[i];
        cstring_t const parts[] = { "Button", "CameraButton", "Label", "Desc" };
        frameDef_t **frames[] = { &row->arrow, &row->camera, &row->label, &row->desc };

        snprintf(row_name, sizeof(row_name), "%sRow%u", name, (unsigned)i);
        row->slot = UI_FindChildFrame(list->box, row_name);
        FOR_LOOP(j, sizeof(parts) / sizeof(parts[0])) {
            char part_name[sizeof(UINAME) * 2];
            snprintf(part_name, sizeof(part_name), "%s%s", row_name, parts[j]);
            *frames[j] = row->slot ? UI_FindChildFrame(row->slot, part_name) : NULL;
        }
        if (!row->arrow || !row->camera || !row->label || !row->desc) memset(row, 0, sizeof(*row));
    }
    return true;
}

/* Build the fixed row widgets once; entries are mapped onto them on every fill or scroll. */
static void SinglePlayer_CreateList(singlePlayerList_t *list, frameDef_t *parent, cstring_t name) {
    color32_t const name_color = { .r = 195, .g = 195, .b = 195, .a = 255 }; // 0.764, as the 1.00 static rows

    memset(list->rows, 0, sizeof(list->rows));
    list->box = NULL;
    if (!parent || !single_player.CampaignMenu || SinglePlayer_BindList(list, parent, name)) return;
    list->box = UI_Spawn(FT_FRAME, parent);
    if (!list->box) return;
    snprintf(list->box->Name, sizeof(list->box->Name), "%s", name);
    UI_SetSize(list->box, SINGLE_PLAYER_LIST_WIDTH, SINGLE_PLAYER_LIST_ROWS * SINGLE_PLAYER_LIST_ROW_HEIGHT);
    UI_SetPoint(list->box, FRAMEPOINT_TOPLEFT, single_player.CampaignMenu, FRAMEPOINT_TOPRIGHT,
                SINGLE_PLAYER_LIST_X, SINGLE_PLAYER_LIST_Y);
    FOR_LOOP(i, SINGLE_PLAYER_LIST_ROWS) {
        singlePlayerListRow_t *row = &list->rows[i];

        row->slot = UI_Spawn(FT_FRAME, list->box);
        if (!row->slot) return;
        snprintf(row->slot->Name, sizeof(row->slot->Name), "%sRow%u", name, (unsigned)i);
        UI_SetSize(row->slot, SINGLE_PLAYER_LIST_WIDTH, SINGLE_PLAYER_LIST_ROW_HEIGHT);
        row->arrow = SinglePlayer_CloneTemplate("CampaignArrowButtonTemplate", row->slot, "Button");
        row->camera = SinglePlayer_CloneTemplate("CampaignCameraButtonTemplate", row->slot, "CameraButton");
        row->label = SinglePlayer_CloneTemplate("StandardSmallTextTemplate", row->slot, "Label");
        row->desc = SinglePlayer_CloneTemplate("StandardTitleTextTemplate", row->slot, "Desc");
        if (!row->arrow || !row->camera || !row->label || !row->desc) {
            memset(row, 0, sizeof(*row));
            return;
        }
        /* Same geometry as the authored 1.00 rows: label beside the button, name under the label. */
        UI_SetPoint(row->arrow, FRAMEPOINT_LEFT, row->slot, FRAMEPOINT_LEFT, 0, 0);
        UI_SetPoint(row->camera, FRAMEPOINT_LEFT, row->slot, FRAMEPOINT_LEFT, 0, 0);
        UI_SetPoint(row->label, FRAMEPOINT_TOPLEFT, row->arrow, FRAMEPOINT_TOPRIGHT, 0.004f, -0.005f);
        UI_SetPoint(row->desc, FRAMEPOINT_TOPLEFT, row->label, FRAMEPOINT_BOTTOMLEFT, 0, 0);
        row->desc->Font.Color = name_color;
    }
}

static void SinglePlayer_LayoutList(singlePlayerList_t *list) {
    uint32_t const max_scroll = list->count > SINGLE_PLAYER_LIST_ROWS ? list->count - SINGLE_PLAYER_LIST_ROWS : 0;
    float const top = list->count < SINGLE_PLAYER_LIST_ROWS
        ? (float)(SINGLE_PLAYER_LIST_ROWS - list->count) * SINGLE_PLAYER_LIST_ROW_HEIGHT * 0.5f : 0.0f;

    list->scroll = MIN(list->scroll, max_scroll);
    FOR_LOOP(i, SINGLE_PLAYER_LIST_ROWS) {
        singlePlayerListRow_t const *row = &list->rows[i];
        uint32_t const index = list->scroll + i;
        singlePlayerListEntry_t const *entry = index < list->count ? &list->entries[index] : NULL;
        bool const blank = !entry || entry->item == UINT32_MAX;

        if (!row->slot) continue;
        UI_SetPoint(row->slot, FRAMEPOINT_TOPLEFT, list->box, FRAMEPOINT_TOPLEFT,
                    0, -(top + (float)i * SINGLE_PLAYER_LIST_ROW_HEIGHT));
        SinglePlayer_SetHidden(row->slot, blank);
        if (blank) continue;
        UI_SetText(row->label, "%s", entry->header ? entry->header : "");
        UI_SetText(row->desc, "%s", entry->name ? entry->name : "");
        UI_SetOnClick(row->arrow, list->command, (unsigned)entry->item);
        UI_SetOnClick(row->camera, list->command, (unsigned)entry->item);
        SinglePlayer_SetHidden(row->arrow, entry->camera);
        SinglePlayer_SetHidden(row->camera, !entry->camera);
    }
}

static void SinglePlayer_AddListEntry(singlePlayerList_t *list, uint32_t item, bool camera,
                                      cstring_t header, cstring_t name) {
    if (list->count >= SINGLE_PLAYER_LIST_MAX_ENTRIES) return;
    list->entries[list->count++] = (singlePlayerListEntry_t){ item, camera, header, name };
}

static void SinglePlayer_FillMissionRows(singlePlayerCampaign_t const *campaign) {
    mission_rows.count = mission_rows.scroll = 0;
    FOR_LOOP(i, mission_list.count) {
        uint32_t const flags = mission_list.items[i].flags;
        uint32_t const index = flags & SINGLE_PLAYER_LIST_INDEX_MASK;
        bool const cinematic = (flags & SINGLE_PLAYER_LIST_FLAG_CINEMATIC) != 0;
        cstring_t header = "", name = mission_list.items[i].name;

        if (cinematic && index < SINGLE_PLAYER_CINEMATIC_COUNT) {
            header = campaign->cinematics[index].header;
            name = campaign->cinematics[index].name;
        } else if (!cinematic && index < campaign->num_missions) {
            header = campaign->missions[index].header;
            name = campaign->missions[index].name[0] ? campaign->missions[index].name : name;
        }
        SinglePlayer_AddListEntry(&mission_rows, i, cinematic, header, name);
        if (cinematic && index == SINGLE_PLAYER_CINEMATIC_INTRO && i + 1 < mission_list.count)
            SinglePlayer_AddListEntry(&mission_rows, UINT32_MAX, false, NULL, NULL);
    }
    SinglePlayer_LayoutList(&mission_rows);
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
    SinglePlayer_FillMissionRows(campaign);
}

static void SinglePlayer_SelectCampaign(singlePlayerCampaign_t const *campaign) {
    if (!campaign) {
        return;
    }
    SinglePlayer_LoadCampaignProgress();
    selected_campaign_index = (uint32_t)(campaign - campaigns);
    SinglePlayer_SetCampaignBackdrop(campaign);
    SinglePlayer_BeginCampaignBackdropBirth(campaign);
    SinglePlayer_PopulateMissionSelect(campaign);
    SinglePlayer_SetView(SINGLE_PLAYER_VIEW_MISSION_SELECT);
}

static void SinglePlayer_PopulateCampaignList(void) {
    memset(&campaign_list, 0, sizeof(campaign_list));
    FOR_LOOP(i, campaign_order_count) {
        uint32_t const campaign_index = campaign_order[i];
        uiMapListItem_t *item;
        singlePlayerCampaign_t const *campaign;

        if (campaign_index >= campaign_count) {
            continue;
        }
        campaign = &campaigns[campaign_index];
        if (!SinglePlayer_FirstMissionMap(campaign) || !SinglePlayer_ShowCampaign(campaign)) {
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

    campaign_rows.count = campaign_rows.scroll = 0;
    FOR_LOOP(i, campaign_list.count) {
        singlePlayerCampaign_t const *campaign = &campaigns[campaign_list.items[i].flags];
        SinglePlayer_AddListEntry(&campaign_rows, i, false, campaign->header, campaign->name);
        SinglePlayer_AddListEntry(&campaign_rows, UINT32_MAX, false, NULL, NULL);
    }
    SinglePlayer_LayoutList(&campaign_rows);
    fprintf(stderr, "Campaign screen: %u campaign(s) listed\n", (unsigned)campaign_list.count);
    FOR_LOOP(i, campaign_list.count) {
        uiMapListItem_t const *item = &campaign_list.items[i];
        fprintf(stderr, "Campaign screen: [%u] %s (key=%s)\n",
                (unsigned)i, item->name, item->path);
    }
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
        cstring_t name = mi.Cvar_String ? mi.Cvar_String("name", "Player") : "Player";
        UI_SetText(single_player.ProfileNameText, "%s", name && name[0] ? name : "Player");
    }
}

static cstring_t SinglePlayer_DifficultyName(uint32_t difficulty) {
    switch (difficulty) {
        case 0: return UI_GetString("EASY");
        case 2: return UI_GetString("HARD");
        default: return UI_GetString("NORMAL");
    }
}

static void SinglePlayer_UpdateDifficultyTitle(uint32_t difficulty) {
    frameDef_t *title = single_player.DifficultySelect
        ? UI_FindChildFrame(single_player.DifficultySelect, "CampaignPopupMenuTitleTextTemplate")
        : NULL;

    if (title) {
        UI_SetText(title, "%s", SinglePlayer_DifficultyName(difficulty));
    }
}

static void SinglePlayer_BindCampaignMenu(void) {
    frameDef_t *DifficultyMenu;
    uint32_t difficulty = 1;
    cstring_t difficulty_value;

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
    difficulty_value = mi.Cvar_String
        ? mi.Cvar_String("wc3_campaign_difficulty", "1") : "1";
    if (difficulty_value) {
        int32_t value = atoi(difficulty_value);
        if (value >= 0 && value <= 2) {
            difficulty = (uint32_t)value;
        }
    }
    SinglePlayer_UpdateDifficultyTitle(difficulty);
}

static void SinglePlayerMenu_Init(void) {
    mi.Printf("SinglePlayerMenu_Init\n");
    SinglePlayer_LoadCampaignData();
    SinglePlayer_LoadCampaignProgress();
    memset(&campaign_list, 0, sizeof(campaign_list));
    memset(&mission_list, 0, sizeof(mission_list));
    /* The FDF offset leaves the sprite origin off-screen; like the main menu (and Warsmash), retail places the
     * MainMenuLogo model explicitly, mirrored to the right edge. */
    if (single_player.WarCraftIIILogo) {
        uint32_t logo_model = UI_LoadModel("MainMenuLogo", true);
        if (logo_model) single_player.WarCraftIIILogo->Portrait.model = logo_model;
        else SinglePlayer_Warn("campaign screen logo 'MainMenuLogo' did not load");
        UI_SetPoint(single_player.WarCraftIIILogo, FRAMEPOINT_TOPRIGHT, single_player.CampaignMenu,
                    FRAMEPOINT_TOPRIGHT, -0.13f, -0.08f);
    }

    SinglePlayer_BindMainMenu();
    SinglePlayer_BindCampaignMenu();
    SinglePlayer_CreateList(&campaign_rows, single_player.CampaignSelectFrame, "CampaignListBox");
    SinglePlayer_CreateList(&mission_rows, single_player.MissionSelectFrame, "MissionListBox");
    SinglePlayer_SetCampaignBackdrop(SinglePlayer_DefaultCampaign());
    selected_campaign_index = SINGLE_PLAYER_MAX_CAMPAIGNS;
    SinglePlayer_SetView(SINGLE_PLAYER_VIEW_MAIN);
}

static void SinglePlayerMenu_Shutdown(void) {
    SinglePlayer_ReleaseCampaignBackdropState();
}

/* The wheel scrolls whichever selector column is under the pointer, including over its buttons. */
static bool SinglePlayerMenu_Scroll(float fdf_x, float fdf_y, int delta) {
    singlePlayerList_t *list = current_view == SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT ? &campaign_rows
        : current_view == SINGLE_PLAYER_VIEW_MISSION_SELECT ? &mission_rows : NULL;

    if (!list || !list->box || !delta || !UI_FrameContainsPoint(list->box, fdf_x, fdf_y)) return false;
    if (delta > 0 && list->scroll > 0) list->scroll--;
    else if (delta < 0) list->scroll++;
    SinglePlayer_LayoutList(list);
    return true;
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

    if (single_player.SinglePlayerMenu) {
        UI_DrawFrame(single_player.SinglePlayerMenu);
    }
}

static void SinglePlayerMenu_KeyEvent(int key, bool down) {
    (void)key;
    (void)down;
}

void SinglePlayerMenu_ShowMain(void) {
    if (single_player.ProfileNameText) {
        cstring_t name = mi.Cvar_String ? mi.Cvar_String("name", "Player") : "Player";
        UI_SetText(single_player.ProfileNameText, "%s", name && name[0] ? name : "Player");
    }
    SinglePlayer_SetView(SINGLE_PLAYER_VIEW_MAIN);
}

void SinglePlayerMenu_ShowCampaign(void) {
    SinglePlayer_LoadCampaignProgress();
    SinglePlayer_PopulateCampaignList();
    singlePlayerCampaign_t const *campaign = SinglePlayer_DefaultCampaign();
    SinglePlayer_SetCampaignBackdrop(campaign);
    SinglePlayer_BeginCampaignBackdropBirth(campaign);
    selected_campaign_index = SINGLE_PLAYER_MAX_CAMPAIGNS;
    SinglePlayer_SetView(SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT);
}

void SinglePlayerMenu_BackCampaign(void) {
    if (current_view == SINGLE_PLAYER_VIEW_MISSION_SELECT) {
        /* Picking a campaign moved the camera into that campaign's scene; returning restores the selector's
         * scene through the same Birth camera move that first entering the campaign screen plays. */
        singlePlayerCampaign_t const *campaign = SinglePlayer_DefaultCampaign();
        SinglePlayer_SetCampaignBackdrop(campaign);
        SinglePlayer_BeginCampaignBackdropBirth(campaign);
        selected_campaign_index = SINGLE_PLAYER_MAX_CAMPAIGNS;
        SinglePlayer_SetView(SINGLE_PLAYER_VIEW_CAMPAIGN_SELECT);
        return;
    }
    M_ShowSinglePlayerMenu();
}

void SinglePlayerMenu_LaunchCampaign(cstring_t name) {
    singlePlayerCampaign_t const *campaign = SinglePlayer_FindCampaign(name);
    if (!campaign) fprintf(stderr, "UI: unknown campaign '%s'\n", name ? name : "(null)");
    SinglePlayer_SelectCampaign(campaign);
}

void SinglePlayerMenu_LaunchCampaignIndex(uint32_t index) {
    uint32_t campaign_index;

    if (index >= campaign_list.count) {
        return;
    }
    campaign_list.selected = index;
    campaign_index = campaign_list.items[index].flags;
    if (campaign_index < campaign_count) {
        SinglePlayer_SelectCampaign(&campaigns[campaign_index]);
    }
}

void SinglePlayerMenu_LaunchMissionIndex(uint32_t index) {
    singlePlayerCampaign_t const *campaign = SinglePlayer_SelectedCampaign();
    uint32_t item_flags;
    uint32_t mission_index;

    if (!campaign || index >= mission_list.count) {
        return;
    }
    mission_list.selected = index;
    item_flags = mission_list.items[index].flags;
#ifdef BZ_FFMPEG
    if (item_flags & SINGLE_PLAYER_LIST_FLAG_CINEMATIC) {
        mi.PlayMovie(mission_list.items[index].path);
        return;
    }
#endif
    mission_index = item_flags & SINGLE_PLAYER_LIST_INDEX_MASK;
    SinglePlayer_LaunchMission(campaign, mission_index);
}

void SinglePlayerMenu_SetDifficulty(uint32_t difficulty) {
    char value[8];

    if (difficulty > 2) {
        return;
    }
    SinglePlayer_UpdateDifficultyTitle(difficulty);
    if (mi.Cvar_Set) {
        snprintf(value, sizeof(value), "%u", (unsigned)difficulty);
        mi.Cvar_Set("wc3_campaign_difficulty", value);
    }
}

uiScreen_t singlePlayerMenuScreen = {
    .name = "single-player",
    .left = solo_lft,
    .glue = { .panel = UI_GLUE_SINGLE_PLAYER },
    .load = SinglePlayerMenu_LoadScreen,
    .init = SinglePlayerMenu_Init,
    .shutdown = SinglePlayerMenu_Shutdown,
    .scroll = SinglePlayerMenu_Scroll,
    .draw = SinglePlayerMenu_Draw,
    .key_event = SinglePlayerMenu_KeyEvent,
};
