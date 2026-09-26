/* Serializes the stock Loading.fdf tree before gameplay UI exists. */
#include "hud_local.h"
#include "../generated/loading_slot.h"

/* WC3 has twelve playable slots; engine MAX_CLIENTS exceeds the W3I player array. */
static LoadingSlot_t loadslot[PLAYER_NEUTRAL_AGGRESSIVE];
static cstring_t const loadrace[] = { "RANDOM", "HUMAN", "ORC", "UNDEAD", "NIGHT_ELF" };

/* The native zero-size loading bar gets its geometry from its MDX, not a portrait viewport. */
void UI_LoadHudLoading(void) {
    if (!LoadingScreen_Load(&hud.loading)) {
        fprintf(stderr, "UI_LoadHudLoading: missing Loading.fdf\n");
        return;
    }
    LoadingSlot_t tmpl;
    memset(loadslot, 0, sizeof(loadslot));
    if (!LoadingSlot_Load(&tmpl)) return;
    FOR_LOOP(i, PLAYER_NEUTRAL_AGGRESSIVE) {
        frameDef_t *row = UI_CloneFrameTree(tmpl.LoadingPlayerSlot, hud.loading.LoadingMeleePlayerContainer);
        if (!LoadingSlot_Bind(&loadslot[i], row)) return;
        UI_SetHidden(row, true);
        UI_SetHidden(loadslot[i].LoadingPlayerSlotLevel, true);
        UI_SetHidden(loadslot[i].LoadingPlayerSlotReadyHighlight, true);
    }
    if (hud.loading.LoadingBar) {
        hud.loading.LoadingBar->Portrait.model = UI_LoadModel("LoadingProgressBar", true);
        hud.loading.LoadingBar->Stat = UI_STAT_LOADING_PROGRESS;
        UI_SetText(hud.loading.LoadingBar, "#0");
    }
}

static cstring_t loading_text(mapInfo_t const *info, cstring_t text) { return text && *text ? G_MapString(info, text) : " "; }

/* Repeat native rows using their authored stride; only participating lobby slots enter this list. */
static void loading_players(mapInfo_t const *info) {
    frameDef_t *pane = hud.loading.LoadingMeleePlayerContainer;
    uint32_t count = 0;
    FOR_LOOP(i, PLAYER_NEUTRAL_AGGRESSIVE)
        if (info->players[i].used && (info->players[i].playerType == kPlayerTypeHuman ||
            info->players[i].playerType == kPlayerTypeComputer)) count++;
    uint32_t rows = (count + 1) / 2, n = 0;
    FOR_LOOP(i, PLAYER_NEUTRAL_AGGRESSIVE) UI_SetHidden(loadslot[i].LoadingPlayerSlot, true);
    FOR_LOOP(i, PLAYER_NEUTRAL_AGGRESSIVE) {
        mapPlayer_t const *player = &info->players[i];
        if (!player->used || (player->playerType != kPlayerTypeHuman && player->playerType != kPlayerTypeComputer)) continue;
        LoadingSlot_t *slot = &loadslot[n];
        frameDef_t *row = slot->LoadingPlayerSlot;
        /* The container supplies the first column; the template supplies row and column spacing. */
        UI_SetPoint(row, FRAMEPOINT_TOPLEFT, pane, FRAMEPOINT_LEFT, (n / rows) * pane->Width, (rows * 0.5f - n % rows) * row->Height);
        UI_SetText(slot->LoadingPlayerSlotName, "%s", loading_text(info, player->playerName));
        UI_SetText(slot->LoadingPlayerSlotRace, "%s", UI_GetString(loadrace[player->playerRace]));
        UI_SetHidden(row, false);
        n++;
    }
}

/* Resolve W3I presentation before the server publishes the loading-only media table. */
void UI_WriteLoadingLayout(edict_t *ent, mapInfo_t const *info) {
    cstring_t title = info && info->loadingScreenTitle && *info->loadingScreenTitle ? info->loadingScreenTitle :
                   info ? info->mapName : NULL;
    uint32_t model = 0, seq = 0;
    bool melee = info && info->campaignBackgroundNumber == (uint32_t)-1 &&
                 (!info->loadingScreenModel || !*info->loadingScreenModel);

    if (!hud.loading.Loading) return;
    /* The custom panel anchors its title on the right; stock melee art requires the other FDF panel. */
    UI_SetHidden(hud.loading.LoadingCustomPanel, melee);
    UI_SetHidden(hud.loading.LoadingMeleePanel, !melee);
    if (melee) {
        UI_SetText(hud.loading.LoadingMeleeMapName, "%s", loading_text(info, info->mapName));
        UI_SetHidden(hud.loading.LoadingMeleeGameTypeLabel, true);
        UI_SetHidden(hud.loading.LoadingMeleeGameTypeValue, true);
        hud.loading.MinimapImage->Type = FT_MINIMAP;
        hud.loading.MinimapImage->ui_flags |= UIFLAG_MINIMAP_PREVIEW;
        UI_SetText(hud.loading.MinimapImage, "%s", gi.GetConfigstring(CS_ASSET_SCOPE));
        UI_SetHidden(hud.loading.MinimapImage, info->flags & hide_minimap_in_preview_screens);
        loading_players(info);
    }
    if (hud.loading.LoadingTitleText)
        UI_SetText(hud.loading.LoadingTitleText, "%s", loading_text(info, title));
    if (hud.loading.LoadingSubtitleText)
        UI_SetText(hud.loading.LoadingSubtitleText, "%s", loading_text(info, info ? info->loadingScreenSubtitle : NULL));
    if (hud.loading.LoadingText)
        UI_SetText(hud.loading.LoadingText, "%s", loading_text(info, info ? info->loadingScreenText : NULL));
    /* Loading.fdf authors screen-space sprites; portrait conversion discarded their native geometry. */
    if (info && info->loadingScreenModel && *info->loadingScreenModel) {
        model = UI_LoadModel(info->loadingScreenModel, false);
    } else if (info && info->campaignBackgroundNumber != (uint32_t)-1) {
        stbIniCache_t data = { 0 };
        PATHSTR path;
        char key[16];
        Stb_IniCacheLoad(&data, "UI\\WorldEditData.txt");
        snprintf(key, sizeof(key), "%02u", (unsigned)info->campaignBackgroundNumber);
        cstring_t row = Stb_IniCacheFind(&data, "LoadingScreens", key);
        if (UI_ParseLoadingRow(row, &seq, path)) model = UI_LoadModel(path, false);
        else fprintf(stderr, "UI_WriteLoadingLayout: invalid LoadingScreens[%s]: %s\n", key, row ? row : "(missing)");
        Stb_IniCacheFree(&data);
    } else {
        model = UI_LoadModel("LoadingMeleeBackground", true);
    }
    if (hud.loading.LoadingBackground) {
        hud.loading.LoadingBackground->Portrait.model = model;
        UI_SetText(hud.loading.LoadingBackground, "#!%u", (unsigned)seq);
    }
    UI_WriteLayout(ent, hud.loading.Loading, LAYER_LOADING);
}
