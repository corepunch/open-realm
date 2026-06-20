/*
 * ui_main.c — UI library entry point and lifecycle management.
 */

#include <stdlib.h>
#include <stdio.h>

#include "ui_local.h"
#include "ui_screen.h"
#include "generated/cinematic_panel.h"
#include "generated/loading_screen.h"
#include "generated/resource_bar.h"

/* Global import table filled by UI_GetAPI */
uiImport_t uiimport;

/* Internal state */
typedef struct {
    BOOL initialized;
    BOOL active;
    BOOL game_mode;
    DWORD time;
    PATHSTR loading_map;
    PATHSTR loading_status;
    FLOAT loading_progress;
} uiState_t;

static uiState_t ui_state;
static uiScreen_t *ui_current_screen = NULL;
static BOOL ui_menu_commands_registered;
static ResourceBar_t resource_bar;
static CinematicPanel_t cinematic_panel;
static LoadingScreen_t loading_screen;

static void UI_EnterGameMode(void);

static BOOL UI_IsMapCommand(LPCSTR command) {
    if (!command) {
        return false;
    }
    while (*command == ' ' || *command == '\t' || *command == '\r' || *command == '\n') {
        command++;
    }
    if (strncmp(command, "map", 3) ||
        (command[3] != ' ' && command[3] != '\t')) {
        return false;
    }
    command += 4;
    while (*command == ' ' || *command == '\t') {
        command++;
    }
    return *command != '\0';
}

typedef struct {
    PATHSTR map;
    char title[256];
    char subtitle[256];
    char text[1024];
    DWORD background_model;
    DWORD background_sequence;
    DWORD progress_model;
} uiLoadingState_t;

static uiLoadingState_t loading_state;

static void UI_SetScreen(uiScreen_t *screen) {
    uiScreen_t *previous_screen = ui_current_screen;

    if (screen) {
        ui_state.game_mode = false;
    }

    if (ui_current_screen == screen) {
        return;
    }

    fprintf(stderr,
            "UI_SetScreen: %s -> %s\n",
            ui_current_screen ? ui_current_screen->name : "(null)",
            screen ? screen->name : "(null)");

    if (!screen) {
        if (ui_current_screen && ui_current_screen->shutdown) {
            fprintf(stderr,
                    "UI_SetScreen: shutting down screen '%s'\n",
                    ui_current_screen->name);
            ui_current_screen->shutdown();
        }
        ui_current_screen = NULL;
        return;
    }
    if (screen->load && !screen->load()) {
        fprintf(stderr,
                "UI_SetScreen: failed to load screen '%s', keeping '%s'\n",
                screen->name,
                previous_screen ? previous_screen->name : "(null)");
        if (1) {
            uiimport.Printf("UI_SetScreen: failed to load screen '%s'\n", screen->name);
        }
        return;
    }
    if (ui_current_screen && ui_current_screen->shutdown) {
        fprintf(stderr,
                "UI_SetScreen: shutting down screen '%s'\n",
                ui_current_screen->name);
        ui_current_screen->shutdown();
    }
    ui_current_screen = screen;
    if (screen->init) {
        fprintf(stderr, "UI_SetScreen: initializing screen '%s'\n", screen->name);
        screen->init();
    }
}

uiScreen_t *UI_GetCurrentScreen(void) {
    return ui_current_screen;
}

void UI_ShowMainMenu(void) {
    UI_SetScreen(&mainMenuScreen);
    MainMenu_ShowMainPanel();
}

void UI_ShowSinglePlayerMenu(void) {
    UI_SetScreen(&singlePlayerMenuScreen);
    SinglePlayerMenu_ShowMain();
}

void UI_ShowOptionsMenu(void) {
    UI_SetScreen(&optionsMenuScreen);
    OptionsMenu_ShowGameplay();
}

void UI_ShowCreditsMenu(void) {
    UI_SetScreen(&creditsMenuScreen);
}

void UI_ShowLanCreateMenu(void) {
    UI_SetScreen(&lanJoinScreen);
    LAN_ShowCreate();
}

void UI_ShowLanBrowserMenu(void) {
    UI_SetScreen(&lanJoinScreen);
    LAN_ShowBrowser();
}

void UI_ShowGameSetupMenu(void) {
    UI_SetScreen(&gameSetupScreen);
}

static void UI_MenuMain_f(void) {
    UI_ShowMainMenu();
}

static void UI_MenuGame_f(void) {
    UI_ShowSinglePlayerMenu();
}

static void UI_MenuMultiplayer_f(void) {
    UI_ShowLanBrowserMenu();
}

static void UI_MenuOptions_f(void) {
    UI_ShowOptionsMenu();
}

static void UI_MenuVideo_f(void) {
    UI_SetScreen(&optionsMenuScreen);
    OptionsMenu_ShowVideo();
}

static void UI_MenuKeys_f(void) {
    UI_SetScreen(&optionsMenuScreen);
    OptionsMenu_ShowKeys();
}

static void UI_MenuLoadGame_f(void) {
}

static void UI_MenuSaveGame_f(void) {
}

static void UI_MenuPlayerConfig_f(void) {
}

static void UI_MenuStartServer_f(void) {
    LAN_ApplyPlayerName();
    UI_ShowLanCreateMenu();
}

static void UI_MenuJoinServer_f(void) {
    UI_ShowLanBrowserMenu();
}

static void UI_MenuCredits_f(void) {
    UI_ShowCreditsMenu();
}

static void UI_MenuQuit_f(void) {
    UI_SetScreen(&mainMenuScreen);
    MainMenu_ShowQuitConfirm();
}

static void UI_MenuDisconnected_f(void) {
    UI_SetScreen(&mainMenuScreen);
    MainMenu_ShowDisconnected();
}

static void UI_MenuRealmSelect_f(void) {
    UI_SetScreen(&mainMenuScreen);
    MainMenu_ShowRealmSelect();
}

static void UI_MenuOptionsGameplay_f(void) {
    UI_SetScreen(&optionsMenuScreen);
    OptionsMenu_ShowGameplay();
}

static void UI_MenuOptionsSound_f(void) {
    UI_SetScreen(&optionsMenuScreen);
    OptionsMenu_ShowSound();
}

static void UI_MenuOptionsApply_f(void) {
    OptionsMenu_Apply();
    UI_MenuMain_f();
}

static void UI_MenuSinglePlayerCampaign_f(void) {
    UI_SetScreen(&singlePlayerMenuScreen);
    SinglePlayerMenu_ShowCampaign();
}

static void UI_MenuLANRefresh_f(void) {
    LAN_RefreshMaps();
}

static void UI_MenuLANStart_f(void) {
    LAN_StartSelectedMap();
}

static void UI_MenuLANJoin_f(void) {
    LAN_JoinSelectedGame();
}

static void UI_MenuGameSetupStart_f(void) {
    if (GameSetup_StartGame()) {
        UI_EnterGameMode();
    }
}

static void UI_MenuInGame_f(void) {
    UI_EnterGameMode();
}

typedef struct {
    LPCSTR command;
    void (*function)(void);
} uiMenuCommandDef_t;

static uiMenuCommandDef_t const ui_menu_command_defs[] = {
    { "menu_main", UI_MenuMain_f },
    { "menu_game", UI_MenuGame_f },
    { "menu_multiplayer", UI_MenuMultiplayer_f },
    { "menu_options", UI_MenuOptions_f },
    { "menu_video", UI_MenuVideo_f },
    { "menu_keys", UI_MenuKeys_f },
    { "menu_loadgame", UI_MenuLoadGame_f },
    { "menu_savegame", UI_MenuSaveGame_f },
    { "menu_playerconfig", UI_MenuPlayerConfig_f },
    { "menu_startserver", UI_MenuStartServer_f },
    { "menu_joinserver", UI_MenuJoinServer_f },
    { "menu_credits", UI_MenuCredits_f },
    { "menu_quit", UI_MenuQuit_f },
    { "menu_disconnected", UI_MenuDisconnected_f },
    { "menu_realm_select", UI_MenuRealmSelect_f },
    { "menu_options_gameplay", UI_MenuOptionsGameplay_f },
    { "menu_options_sound", UI_MenuOptionsSound_f },
    { "menu_options_apply", UI_MenuOptionsApply_f },
    { "menu_single_player_campaign", UI_MenuSinglePlayerCampaign_f },
    { "menu_lan_refresh", UI_MenuLANRefresh_f },
    { "menu_lan_start", UI_MenuLANStart_f },
    { "menu_lan_join", UI_MenuLANJoin_f },
    { "menu_game_setup_start", UI_MenuGameSetupStart_f },
    { "menu_ingame", UI_MenuInGame_f },
    { NULL, NULL },
};

typedef struct {
    LPCSTR texture;
    BOOL decorate;
    RECT screen;
    RECT uv;
} uiConsoleBackdropPart_t;

static void UI_DrawImagePart(LPCSTR texture_name, BOOL decorate, LPCRECT screen, LPCRECT uv) {
    LPRENDERER renderer = uiimport.GetRenderer ? uiimport.GetRenderer() : NULL;
    DWORD texture_id;
    LPCTEXTURE texture;

    if (!renderer || !renderer->DrawImageEx || !texture_name || !*texture_name) {
        return;
    }

    texture_id = UI_LoadTexture(texture_name, decorate);
    texture = UI_GetTexture(texture_id);
    if (!texture) {
        return;
    }

    renderer->DrawImageEx(&MAKE(drawImage_t,
                                .texture = texture,
                                .shader = SHADER_UI,
                                .alphamode = BLEND_MODE_ALPHAKEY,
                                .screen = *screen,
                                .uv = *uv,
                                .color = COLOR32_WHITE,
                                .rotate = false));
}

static void UI_DrawConsoleBackdropPart(uiConsoleBackdropPart_t const *part) {
    if (!part) {
        return;
    }
    UI_DrawImagePart(part->texture, part->decorate, &part->screen, &part->uv);
}

static void UI_DrawConsoleBackdropOnly(void) {
    static uiConsoleBackdropPart_t const parts[] = {
        { "ConsoleTexture01", true, { 0.000f, 0.000f, 0.256f, 0.032f }, { 0.00000000f, 0.000000f, 1.00000000f, 0.125000f } },
        { "ConsoleTexture02", true, { 0.256f, 0.000f, 0.087f, 0.032f }, { 0.00000000f, 0.000000f, 0.33984375f, 0.125000f } },
        { "ConsoleTexture02", true, { 0.459f, 0.000f, 0.053f, 0.032f }, { 0.79296875f, 0.000000f, 0.20703125f, 0.125000f } },
        { "ConsoleTexture03", true, { 0.512f, 0.000f, 0.256f, 0.032f }, { 0.00000000f, 0.000000f, 1.00000000f, 0.125000f } },
        { "ConsoleTexture04", true, { 0.768f, 0.000f, 0.032f, 0.032f }, { 0.00000000f, 0.000000f, 1.00000000f, 0.125000f } },
        { "ConsoleTexture01", true, { 0.000f, 0.424f, 0.256f, 0.176f }, { 0.00000000f, 0.312500f, 1.00000000f, 0.687500f } },
        { "ConsoleTexture02", true, { 0.256f, 0.450f, 0.256f, 0.150f }, { 0.00000000f, 0.414062f, 1.00000000f, 0.585938f } },
        { "ConsoleTexture03", true, { 0.512f, 0.424f, 0.256f, 0.176f }, { 0.00000000f, 0.312500f, 1.00000000f, 0.687500f } },
        { "ConsoleTexture04", true, { 0.768f, 0.424f, 0.032f, 0.176f }, { 0.00000000f, 0.312500f, 1.00000000f, 0.687500f } },
    };

    FOR_LOOP(i, sizeof(parts) / sizeof(parts[0])) {
        UI_DrawConsoleBackdropPart(&parts[i]);
    }
}

static void UI_DrawConsoleMinimap(void) {
    LPRENDERER renderer = uiimport.GetRenderer ? uiimport.GetRenderer() : NULL;
    RECT const rect = { 0.0070f, 0.4525f, 0.1395f, 0.1395f };

    if (!renderer || !renderer->DrawMinimap) {
        return;
    }
    renderer->DrawMinimap(&rect);
}

static void UI_RegisterMenuCommands(void) {
    if (ui_menu_commands_registered || !uiimport.Cmd_AddCommand) {
        return;
    }
    for (uiMenuCommandDef_t const *cmd = ui_menu_command_defs; cmd->command; cmd++) {
        uiimport.Cmd_AddCommand(cmd->command, cmd->function);
    }
    ui_menu_commands_registered = true;
}

static void UI_InitGameResourceBar(void) {
    ResourceBar_Load(&resource_bar);
    if (resource_bar.ResourceBarFrame) {
        UI_SetPoint(resource_bar.ResourceBarFrame,
                    FRAMEPOINT_TOPRIGHT,
                    NULL,
                    FRAMEPOINT_TOPRIGHT,
                    0.0f,
                    0.0f);
    }
}

static void UI_DrawResourceBar(void) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

    if (!ps || !resource_bar.ResourceBarFrame) {
        return;
    }

    if (resource_bar.ResourceBarGoldText) {
        UI_SetText(resource_bar.ResourceBarGoldText, "%u", (unsigned)ps->stats[PLAYERSTATE_RESOURCE_GOLD]);
    }
    if (resource_bar.ResourceBarLumberText) {
        UI_SetText(resource_bar.ResourceBarLumberText, "%u", (unsigned)ps->stats[PLAYERSTATE_RESOURCE_LUMBER]);
    }
    if (resource_bar.ResourceBarSupplyText) {
        UI_SetText(resource_bar.ResourceBarSupplyText,
                   "%u/%u",
                   (unsigned)ps->stats[PLAYERSTATE_RESOURCE_FOOD_USED],
                   (unsigned)ps->stats[PLAYERSTATE_RESOURCE_FOOD_CAP]);
    }
    if (resource_bar.ResourceBarUpkeepText) {
        UI_SetText(resource_bar.ResourceBarUpkeepText, "UPKEEP_NONE");
    }

    UI_DrawFrame(resource_bar.ResourceBarFrame);
}

static BOOL UI_CinematicActive(LPCPLAYER ps) {
    return ps && ps->client_ui_state == CLIENT_UI_CINEMATIC;
}

static BOOL UI_LoadingActive(LPCPLAYER ps) {
    return ps && ps->client_ui_state == CLIENT_UI_LOADING;
}

static void UI_EnterGameMode(void) {
    ui_state.game_mode = true;
    UI_SetScreen(NULL);
}

static void UI_InitCinematicPanel(void) {
    CinematicPanel_Load(&cinematic_panel);
}

static void UI_DrawCinematicPanel(LPCPLAYER ps) {
    if (!ps || !cinematic_panel.CinematicPanel) {
        return;
    }

    if (cinematic_panel.CinematicSpeakerText) {
        UI_SetTextPointer(cinematic_panel.CinematicSpeakerText, ps->texts[PLAYERTEXT_SPEAKER] ? ps->texts[PLAYERTEXT_SPEAKER] : "");
    }
    if (cinematic_panel.CinematicDialogueText) {
        UI_SetTextPointer(cinematic_panel.CinematicDialogueText, ps->texts[PLAYERTEXT_DIALOGUE] ? ps->texts[PLAYERTEXT_DIALOGUE] : "");
    }

    UI_DrawFrame(cinematic_panel.CinematicPanel);

    /* Overlay the speaking unit's live portrait (talking head) in the cinematic
     * portrait frame. Its model is a game configstring index, so render it
     * directly rather than through the FDF model path. */
    if (ps->cinematic_portrait && cinematic_panel.CinematicPortrait) {
        UI_DrawGamePortraitInFrame(cinematic_panel.CinematicPortrait,
                                   ps->cinematic_portrait, "Portrait Talk");
    }
}

/* Text sanitization — replaces whitespace with spaces (inlined from CM_Sanitize*) */
void UI_SanitizeMapListField(LPSTR text) {
    if (!text) return;
    for (LPSTR p = text; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
    }
}

void UI_SanitizeMapInfoText(LPSTR text) {
    if (!text) return;
    for (LPSTR p = text; *p; p++) {
        if (*p == '\t') *p = ' ';
    }
}

static LPCSTR UI_CsvField(LPCSTR text, DWORD index, LPSTR out, DWORD out_size) {
    DWORD field = 0;
    DWORD len = 0;
    LPCSTR p = text;

    if (!out || out_size == 0) {
        return "";
    }
    out[0] = '\0';
    if (!text) {
        return out;
    }

    while (*p && field < index) {
        if (*p++ == ',') {
            field++;
        }
    }
    while (p[len] && p[len] != ',' && len + 1 < out_size) {
        len++;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return out;
}

static LPCSTR UI_LoadingMapPath(void) {
    LPCSTR path = ui_state.loading_map;

    if (path && *path) {
        return path;
    }
    return uiimport.Cvar_String ? uiimport.Cvar_String("map", "") : "";
}

static DWORD UI_LoadCampaignLoadingModel(DWORD campaign_background, DWORD *sequence_index) {
    sheetRow_t *world_edit_data;
    char key[8];
    char sequence[16];
    char model[MAX_PATHLEN];
    LPCSTR row;

    if (sequence_index) {
        *sequence_index = 0;
    }

    world_edit_data = FS_ParseINI("UI\\WorldEditData.txt");
    snprintf(key, sizeof(key), "%02u", (unsigned)campaign_background);
    row = FS_FindSheetCell(world_edit_data, "LoadingScreens", key);
    UI_CsvField(row, 1, sequence, sizeof(sequence));
    UI_CsvField(row, 2, model, sizeof(model));
    if (sequence_index && sequence[0]) {
        *sequence_index = (DWORD)atoi(sequence);
    }
    return model[0] ? UI_LoadModel(model, false) : 0;
}

static DWORD UI_DefaultLoadingModel(void) {
    return UI_LoadModel("LoadingMeleeBackground", true);
}

static DWORD UI_CustomLoadingModel(LPCMAPINFO info) {
    PATHSTR model;

    if (!info || !info->loadingScreenModel || !info->loadingScreenModel[0]) {
        return 0;
    }
    snprintf(model, sizeof(model), "%s", info->loadingScreenModel);
    UI_SanitizeMapInfoText(model);
    return model[0] ? UI_LoadModel(model, false) : 0;
}

static void UI_UpdateLoadingMapInfo(void) {
    MAPINFO info;
    LPCSTR map_path = UI_LoadingMapPath();
    DWORD background_model = 0;
    DWORD background_sequence = 0;

    if (!map_path || !*map_path || !strcmp(loading_state.map, map_path)) {
        return;
    }

    memset(&info, 0, sizeof(info));
    memset(&loading_state, 0, sizeof(loading_state));
    snprintf(loading_state.map, sizeof(loading_state.map), "%s", map_path);

    if (uiimport.ReadMapInfo(map_path, &info)) {
        uiimport.ResolveMapInfoString(&info, info.loadingScreenTitle, loading_state.title, sizeof(loading_state.title));
        if (!loading_state.title[0]) {
            uiimport.ResolveMapInfoString(&info, info.mapName, loading_state.title, sizeof(loading_state.title));
        }
        uiimport.ResolveMapInfoString(&info, info.loadingScreenSubtitle, loading_state.subtitle, sizeof(loading_state.subtitle));
        uiimport.ResolveMapInfoString(&info, info.loadingScreenText, loading_state.text, sizeof(loading_state.text));
        UI_SanitizeMapInfoText(loading_state.title);
        UI_SanitizeMapInfoText(loading_state.subtitle);
        UI_SanitizeMapInfoText(loading_state.text);
        background_model = UI_CustomLoadingModel(&info);
        if (!background_model && info.campaignBackgroundNumber != (DWORD)-1) {
            background_model = UI_LoadCampaignLoadingModel(info.campaignBackgroundNumber, &background_sequence);
        }
        uiimport.FreeMapInfo(&info);
    }

    if (!loading_state.title[0]) {
        uiimport.DefaultMapName(map_path, loading_state.title, sizeof(loading_state.title));
    }

    loading_state.background_model = background_model ? background_model : UI_DefaultLoadingModel();
    loading_state.background_sequence = background_sequence;
    loading_state.progress_model = UI_LoadModel("LoadingProgressBar", true);
}

static void UI_InitLoadingScreen(void) {
    LoadingScreen_Load(&loading_screen);
    if (loading_screen.LoadingCustomPanel) {
        UI_SetHidden(loading_screen.LoadingCustomPanel, false);
    }
    if (loading_screen.LoadingMeleePanel) {
        UI_SetHidden(loading_screen.LoadingMeleePanel, true);
    }
}

static void UI_DrawLoadingScreen(void) {
    FLOAT loading_progress = ui_state.loading_progress;

    UI_UpdateLoadingMapInfo();

    if (!loading_screen.Loading) {
        return;
    }
    if (loading_screen.LoadingBackground) {
        snprintf(loading_screen.LoadingBackground->TextStorage, sizeof(loading_screen.LoadingBackground->TextStorage), "#!%u",
                 (unsigned)loading_state.background_sequence);
        loading_screen.LoadingBackground->base.text = loading_screen.LoadingBackground->TextStorage;
        loading_screen.LoadingBackground->Portrait.model = loading_state.background_model;
    }
    if (loading_screen.LoadingBar) {
        snprintf(loading_screen.LoadingBar->TextStorage, sizeof(loading_screen.LoadingBar->TextStorage), "#0@%.4f", loading_progress);
        loading_screen.LoadingBar->base.text = loading_screen.LoadingBar->TextStorage;
        loading_screen.LoadingBar->Portrait.model = loading_state.progress_model;
    }
    if (loading_screen.LoadingTitleText) {
        UI_SetTextPointer(loading_screen.LoadingTitleText, loading_state.title);
    }
    if (loading_screen.LoadingSubtitleText) {
        UI_SetTextPointer(loading_screen.LoadingSubtitleText, loading_state.subtitle);
    }
    if (loading_screen.LoadingText) {
        UI_SetTextPointer(loading_screen.LoadingText, loading_state.text);
    }

    UI_DrawFrame(loading_screen.Loading);
}

/* Last mouse position in FDF coords — set by UI_MouseEventLocal, read by draw helpers */
static VECTOR2 ui_last_mouse_fdf;

VECTOR2 UI_MouseToFdf(void) {
    return ui_last_mouse_fdf;
}

BOOL UI_MouseContains(LPCRECT rect) {
    VECTOR2 const mouse = UI_MouseToFdf();
    return Rect_contains(rect, &mouse);
}

void UI_SetLoadingState(LPCSTR map, LPCSTR status, FLOAT progress) {
    if (map) snprintf(ui_state.loading_map, sizeof(ui_state.loading_map), "%s", map);
    if (status) snprintf(ui_state.loading_status, sizeof(ui_state.loading_status), "%s", status);
    ui_state.loading_progress = progress;
}

void UI_InitLocal(void) {
    memset(&ui_state, 0, sizeof(ui_state));
    UI_ResetGlueSceneModels();
    UI_RegisterMenuCommands();
    
    uiimport.Printf("UI_InitLocal: loading FDF assets\n");

    UI_LoadTheme("UI\\war3skins.txt");
    UI_ParseFDF("UI\\FrameDef\\GlobalStrings.fdf");
    
    /* Load core menu FDF files */
    UI_ParseFDF("UI\\FrameDef\\UI\\EscMenuTemplates.fdf");
    UI_ParseFDF("UI\\FrameDef\\UI\\EscMenuMainPanel.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\StandardTemplates.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\MainMenu.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\SinglePlayerMenu.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\CampaignMenu.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\DialogWar3.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\MapListBox.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\MapInfoPane.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\LocalMultiplayerJoin.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\LocalMultiplayerCreate.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\TeamSetup.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\PlayerSlot.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\GameChatroom.fdf");
    UI_ParseFDF("UI\\FrameDef\\Glue\\Loading.fdf");
    UI_ParseFDF("UI\\FrameDef\\UI\\ResourceBar.fdf");
    UI_ParseFDF("UI\\FrameDef\\UI\\CinematicPanel.fdf");
    UI_InitLoadingScreen();
    UI_InitGameResourceBar();
    UI_InitCinematicPanel();
    
    ui_state.initialized = true;
    ui_state.active = true;
    
    /*
     * Map launches use the server-authored in-game HUD via svc_layout.  Leave
     * the client-side menu screen idle there so no glue screen covers the game.
     */
    LPCSTR map = uiimport.Cvar_String
        ? uiimport.Cvar_String("map", "")
        : "";
    if (map && *map) {
        UI_EnterGameMode();
        return;
    }

    UI_MenuCommandLocal("menu_main");
}

void UI_ShutdownLocal(void) {
    UI_ResetGlueSceneModels();
    UI_SetScreen(NULL);
    UI_ClearTemplates();
    memset(&ui_state, 0, sizeof(ui_state));
}

void UI_RefreshLocal(DWORD msec) {
    if (!ui_state.active) {
        return;
    }
    ui_state.time += msec;
    uiScreen_t *screen = UI_GetCurrentScreen();
    if (screen && screen->refresh) {
        screen->refresh((int)msec);
    }
    /* Overlay visibility and text updates — DLL owns this, client owns draw */
    if (ui_state.game_mode) {
        LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
        if (loading_screen.Loading) UI_SetHidden(loading_screen.Loading, !UI_LoadingActive(ps));
        if (UI_LoadingActive(ps)) {
            UI_UpdateLoadingMapInfo();
            FLOAT lp = ui_state.loading_progress;
            if (loading_screen.LoadingBackground) {
                snprintf(loading_screen.LoadingBackground->TextStorage, sizeof(loading_screen.LoadingBackground->TextStorage), "#!%u", (unsigned)loading_state.background_sequence);
                loading_screen.LoadingBackground->base.text = loading_screen.LoadingBackground->TextStorage;
                loading_screen.LoadingBackground->Portrait.model = loading_state.background_model;
            }
            if (loading_screen.LoadingBar) {
                snprintf(loading_screen.LoadingBar->TextStorage, sizeof(loading_screen.LoadingBar->TextStorage), "#0@%.4f", lp);
                loading_screen.LoadingBar->base.text = loading_screen.LoadingBar->TextStorage;
                loading_screen.LoadingBar->Portrait.model = loading_state.progress_model;
            }
            if (loading_screen.LoadingTitleText) UI_SetTextPointer(loading_screen.LoadingTitleText, loading_state.title);
            if (loading_screen.LoadingSubtitleText) UI_SetTextPointer(loading_screen.LoadingSubtitleText, loading_state.subtitle);
            if (loading_screen.LoadingText) UI_SetTextPointer(loading_screen.LoadingText, loading_state.text);
        }
        if (cinematic_panel.CinematicPanel) UI_SetHidden(cinematic_panel.CinematicPanel, !UI_CinematicActive(ps));
        if (UI_CinematicActive(ps) && ps) {
            if (cinematic_panel.CinematicSpeakerText) UI_SetTextPointer(cinematic_panel.CinematicSpeakerText, ps->texts[PLAYERTEXT_SPEAKER] ? ps->texts[PLAYERTEXT_SPEAKER] : "");
            if (cinematic_panel.CinematicDialogueText) UI_SetTextPointer(cinematic_panel.CinematicDialogueText, ps->texts[PLAYERTEXT_DIALOGUE] ? ps->texts[PLAYERTEXT_DIALOGUE] : "");
        }
        if (resource_bar.ResourceBarFrame) {
            UI_SetHidden(resource_bar.ResourceBarFrame, false);
            if (ps) {
                if (resource_bar.ResourceBarGoldText) UI_SetText(resource_bar.ResourceBarGoldText, "%u", (unsigned)ps->stats[PLAYERSTATE_RESOURCE_GOLD]);
                if (resource_bar.ResourceBarLumberText) UI_SetText(resource_bar.ResourceBarLumberText, "%u", (unsigned)ps->stats[PLAYERSTATE_RESOURCE_LUMBER]);
                if (resource_bar.ResourceBarSupplyText) UI_SetText(resource_bar.ResourceBarSupplyText, "%u/%u", (unsigned)ps->stats[PLAYERSTATE_RESOURCE_FOOD_USED], (unsigned)ps->stats[PLAYERSTATE_RESOURCE_FOOD_CAP]);
                if (resource_bar.ResourceBarUpkeepText) UI_SetText(resource_bar.ResourceBarUpkeepText, "UPKEEP_NONE");
            }
        }
    } else {
        if (loading_screen.Loading) UI_SetHidden(loading_screen.Loading, true);
        if (cinematic_panel.CinematicPanel) UI_SetHidden(cinematic_panel.CinematicPanel, true);
        if (resource_bar.ResourceBarFrame) UI_SetHidden(resource_bar.ResourceBarFrame, true);
    }
}

void UI_DrawFrameLocal(void) {
    if (!ui_state.active) {
        return;
    }
    
    /* Call current screen draw */
    if (ui_state.game_mode) {
        LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

        if (UI_LoadingActive(ps)) {
            UI_DrawLoadingScreen();
        } else if (UI_CinematicActive(ps)) {
            UI_DrawCinematicPanel(ps);
        } else {
            UI_DrawConsoleBackdropOnly();
            UI_DrawConsoleMinimap();
            UI_DrawResourceBar();
        }
    } else {
        uiScreen_t *screen = UI_GetCurrentScreen();
        if (screen && screen->draw) {
            screen->draw();
        }
    }
}

void UI_KeyEventLocal(int key, BOOL down, DWORD time) {
    (void)time;

    if (!ui_state.active) {
        return;
    }

    if (down && UI_EditKey(key)) {
        return;
    }
    
    /* Delegate to current screen */
    uiScreen_t *screen = UI_GetCurrentScreen();
    if (screen && screen->key_event) {
        screen->key_event(key, down);
    }
}

/* Convert pixel coordinates to FDF/UI space for hit testing */
static VECTOR2 UI_PixelToFdf(int px, int py) {
    LPRENDERER renderer = uiimport.GetRenderer ? uiimport.GetRenderer() : NULL;
    size2_t window = renderer && renderer->GetWindowSize ? renderer->GetWindowSize() : MAKE(size2_t, 0, 0);
    FLOAT window_aspect = UI_MIN_ASPECT;
    FLOAT x_scale = 1.0f;
    FLOAT y_scale = 1.0f;
    RECT scene;
    FLOAT nx = 0;
    FLOAT ny = 0;

    if (window.width > 0 && window.height > 0) {
        window_aspect = (FLOAT)window.width / (FLOAT)window.height;
        nx = (FLOAT)px / (FLOAT)window.width;
        ny = (FLOAT)py / (FLOAT)window.height;
    }
    if (window_aspect > UI_MIN_ASPECT) {
        x_scale = window_aspect / UI_MIN_ASPECT;
    } else if (window_aspect < UI_MIN_ASPECT) {
        y_scale = UI_MIN_ASPECT / window_aspect;
    }
    scene.w = UI_BASE_WIDTH * x_scale;
    scene.h = UI_BASE_HEIGHT * y_scale;
    scene.x = (UI_BASE_WIDTH - scene.w) * 0.5f;
    scene.y = (UI_BASE_HEIGHT - scene.h) * 0.5f;
    return MAKE(VECTOR2, scene.x + nx * scene.w, scene.y + ny * scene.h);
}

void UI_MouseEventLocal(int x, int y, int button, BOOL down) {
    if (!ui_state.active) {
        return;
    }
    VECTOR2 fdf = UI_PixelToFdf(x, y);
    ui_last_mouse_fdf = fdf;
    /* Track mouse position for layout system */
    UI_LayoutSetMouseState(fdf, down && button == 1);

    LPCFRAMEDEF hit = UI_HitTest(fdf.x, fdf.y);

    /* Dispatch to per-type event handler */
    if (hit && hit->base.on_event) {
        hit->base.on_event((struct uiBaseFrame_s *)hit, fdf.x, fdf.y, button, down);
    }

    /* Global: editbox clear focus on miss (LEFT_DOWN outside any editbox) */
    if (down && button == 1) {
        BOOL hit_editbox = hit && (hit->base.type == FT_EDITBOX || hit->base.type == FT_GLUEEDITBOX ||
                                   hit->base.type == FT_SLASHCHATBOX);
        if (!hit_editbox) {
            UI_EditboxClearFocusOnMiss();
        }
    }

    /* Global: slider drag tracking (motion when no frame hit) */
    if (UI_SliderIsDragging() && button == 0 && !down) {
        UI_SliderUpdateDrag(UI_SliderActiveFrame(), fdf.x, fdf.y);
    }
    if (!down && button == 1) {
        UI_SliderEndDrag(NULL);
    }

    /* Global: popup close on outside click */
    if (down && button == 1 && UI_HasActivePopup() && !UI_PopupPointInside(fdf.x, fdf.y)) {
        UI_PopupCloseOnMiss();
    }

    /* Global: popup menu wheel scroll */
    if (UI_HasActivePopup() && button == 4) {
        UI_PopupMenuScroll(true);
    }
    if (UI_HasActivePopup() && button == 5) {
        UI_PopupMenuScroll(false);
    }
    if (UI_HasActivePopup() && !down && button == 1) {
        UI_PopupSelectItem(fdf.x, fdf.y);
    }

    /* Delegate to current screen */
    uiScreen_t *screen = UI_GetCurrentScreen();
    if (screen && screen->mouse_event) {
        screen->mouse_event(x, y, button);
    }
}

void UI_MenuCommandLocal(LPCSTR command) {
    DWORD index;
    DWORD slot;
    DWORD value;
    char map_path[MAX_PATHLEN];

    uiimport.Printf("UI_MenuCommandLocal: %s\n", command);

    if (!command || !*command) {
        return;
    }

    if (!strcmp(command, "menu_main")) {
        UI_MenuMain_f();
        return;
    }
    if (!strcmp(command, "menu_game")) {
        UI_MenuGame_f();
        return;
    }
    if (!strcmp(command, "menu_multiplayer")) {
        UI_MenuMultiplayer_f();
        return;
    }
    if (!strcmp(command, "menu_startserver")) {
        UI_MenuStartServer_f();
        return;
    }
    if (!strcmp(command, "menu_joinserver")) {
        UI_MenuJoinServer_f();
        return;
    }
    if (!strcmp(command, "menu_options")) {
        UI_MenuOptions_f();
        return;
    }
    if (!strcmp(command, "menu_video")) {
        UI_MenuVideo_f();
        return;
    }
    if (!strcmp(command, "menu_keys")) {
        UI_MenuKeys_f();
        return;
    }
    if (!strcmp(command, "menu_credits")) {
        UI_MenuCredits_f();
        return;
    }
    if (!strcmp(command, "menu_quit")) {
        UI_MenuQuit_f();
        return;
    }
    if (!strcmp(command, "menu_realm_select")) {
        UI_MenuRealmSelect_f();
        return;
    }
    if (!strcmp(command, "menu_options_gameplay")) {
        UI_MenuOptionsGameplay_f();
        return;
    }
    if (!strcmp(command, "menu_options_sound")) {
        UI_MenuOptionsSound_f();
        return;
    }
    if (!strcmp(command, "menu_options_apply")) {
        UI_MenuOptionsApply_f();
        return;
    }
    if (!strcmp(command, "menu_single_player_campaign")) {
        UI_MenuSinglePlayerCampaign_f();
        return;
    }
    if (!strcmp(command, "menu_single_player_campaign_human")) {
        SinglePlayerMenu_LaunchCampaign("human");
        return;
    }
    if (!strcmp(command, "menu_single_player_campaign_orc")) {
        SinglePlayerMenu_LaunchCampaign("orc");
        return;
    }
    if (!strcmp(command, "menu_single_player_campaign_undead")) {
        SinglePlayerMenu_LaunchCampaign("undead");
        return;
    }
    if (!strcmp(command, "menu_single_player_campaign_night_elf")) {
        SinglePlayerMenu_LaunchCampaign("night-elf");
        return;
    }
    if (!strcmp(command, "menu_single_player_campaign_tutorial")) {
        SinglePlayerMenu_LaunchCampaign("tutorial");
        return;
    }
    if (sscanf(command, "menu_single_player_campaign_select %u", &value) == 1) {
        SinglePlayerMenu_LaunchCampaignIndex(value);
        return;
    }
    if (sscanf(command, "menu_single_player_difficulty %u", &value) == 1) {
        return;
    }
    if (!strcmp(command, "menu_lan_refresh")) {
        UI_MenuLANRefresh_f();
        return;
    }
    if (!strcmp(command, "menu_lan_start")) {
        UI_MenuLANStart_f();
        return;
    }
    if (!strcmp(command, "menu_lan_join")) {
        UI_MenuLANJoin_f();
        return;
    }
    if (sscanf(command, "menu_lan_select %u", &index) == 1) {
        LAN_SelectMapIndex(index);
        return;
    }
    if (!strcmp(command, "menu_game_setup_start")) {
        UI_MenuGameSetupStart_f();
        return;
    }
    if (!strcmp(command, "menu_ingame")) {
        UI_MenuInGame_f();
        return;
    }
    if (sscanf(command, "menu_game_setup_slot_type %u %u", &slot, &value) == 2) {
        GameSetup_SetSlotType(slot, value);
        return;
    }
    if (sscanf(command, "menu_game_setup_slot_race %u %u", &slot, &value) == 2) {
        GameSetup_SetSlotRace(slot, value);
        return;
    }
    if (sscanf(command, "menu_game_setup_slot_team_next %u", &slot) == 1) {
        GameSetup_CycleSlotTeam(slot);
        return;
    }
    if (sscanf(command, "menu_game_setup_slot_color_next %u", &slot) == 1) {
        GameSetup_CycleSlotColor(slot);
        return;
    }
    if (sscanf(command, "menu_game_setup_map %255[^\n]", map_path) == 1) {
        UI_SetScreen(&gameSetupScreen);
        GameSetup_LoadMap(map_path);
        return;
    }
    if (!strncmp(command, "menu_game_setup_chat ", 21)) {
        DWORD own = 0;
        LPCSTR text = command + 21;

        if (sscanf(text, "%u", &own) == 1) {
            while (*text && *text != ' ' && *text != '\t') {
                text++;
            }
            while (*text == ' ' || *text == '\t') {
                text++;
            }
        }
        GameSetup_AddChatMessage(text, own != 0);
        return;
    }

    if (UI_IsMapCommand(command)) {
        UI_EnterGameMode();
    }
    uiimport.Cmd_ExecuteText(command);
}

/* Stub callbacks for server data updates */
/* Forward unit UI data to active screen (Phase 8) */
void UI_UpdateUnitUILocal(DWORD num_units, uiUnitData_t *units) {
    uiimport.Printf("UI_UpdateUnitUI: %d units\n", (int)num_units);
    
    /* Forward to current screen if it implements unit UI handling */
    uiScreen_t *screen = UI_GetCurrentScreen();
    if (screen && screen->update_unit_ui) {
        screen->update_unit_ui(num_units, units);
    }
}

static void UI_UpdateLobbySetupLocal(lobbyState_t const *state) {
    if (ui_state.game_mode) {
        return;
    }
    UI_SetScreen(&gameSetupScreen);
    GameSetup_UpdateLobbySetup(state);
}

/* Export function table */
uiExport_t UI_GetAPI(uiImport_t import) {
    uiimport = import;
    
    uiExport_t exp;
    memset(&exp, 0, sizeof(exp));
    
    exp.Init = UI_InitLocal;
    exp.Shutdown = UI_ShutdownLocal;
    exp.Refresh = UI_RefreshLocal;
    exp.SetLoadingState = UI_SetLoadingState;
    exp.KeyEvent = UI_KeyEventLocal;
    exp.TextInput = UI_TextInputLocal;
    exp.MouseEvent = UI_MouseEventLocal;
    exp.UpdateUnitUI = UI_UpdateUnitUILocal;
    exp.UpdateLobbySetup = UI_UpdateLobbySetupLocal;
    exp.SetLayoutLayer = UI_LayoutSetLayer;
    exp.ClearLayoutLayer = UI_LayoutClearLayer;
    exp.HitTestLayout = UI_LayoutHitTest;
    exp.frame_size = sizeof(FRAMEDEF);
    exp.frames = frames;
    exp.num_frames = MAX_UI_CLASSES;
    
    return exp;
}
