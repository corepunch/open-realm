/*
 * menu_main.c — UI library entry point and lifecycle management.
 */

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include "menu_local.h"
#include "common/video_modes.h"
#include "menu_screen.h"

/* Global import table filled by M_GetAPI */
menuImport_t mi;


/* Internal state */
typedef struct {
    BOOL initialized;
    BOOL active;
    DWORD time;
    VECTOR2 mouse_fdf;
    uiScreen_t *transition_screen;
    void (*configure)(void);
    void (*transition_action)(void);
} uiState_t;

static uiState_t ui_state;
static uiScreen_t *ui_current_screen = NULL;
static BOOL ui_menu_commands_registered;

static void UI_ClearScreen(void);
static void M_ShowSinglePlayerSkirmishMenu(void);

typedef struct { LPCSTR name; void (*func)(void); } MENUCOMMAND;

static void UI_MenuMain_f(void);
static void UI_MenuGame_f(void);
static void UI_MenuVideo_f(void);
static void UI_MenuKeys_f(void);
static void UI_MenuLoadGame_f(void);
static void UI_MenuSaveGame_f(void);
static void UI_MenuPlayerConfig_f(void);
static void UI_MenuStartServer_f(void);
static void UI_MenuQuit_f(void);
static void UI_MenuDisconnected_f(void);
static void UI_MenuRealmSelect_f(void);
static void UI_MenuOptionsGameplay_f(void);
static void UI_MenuOptionsSound_f(void);
static void UI_MenuOptionsApply_f(void);
static void UI_MenuSinglePlayerCampaign_f(void);
static void UI_MenuGameSetupStart_f(void);
static void UI_MenuCampaignHuman_f(void);
static void UI_MenuCampaignOrc_f(void);
static void UI_MenuCampaignUndead_f(void);
static void UI_MenuCampaignNightElf_f(void);
static void UI_MenuCampaignTutorial_f(void);
static void UI_MenuVideoMode_f(void);
static void UI_MenuCampaignSelect_f(void);
static void UI_MenuMissionSelect_f(void);
static void UI_MenuDifficulty_f(void);
static void UI_MenuLANSelect_f(void);
static void UI_MenuSlotTeam_f(void);
static void UI_MenuSlotColor_f(void);
static void UI_MenuSlotType_f(void);
static void UI_MenuSlotRace_f(void);
static void UI_MenuSetupMap_f(void);
static void UI_MenuSetupChat_f(void);

/* All menu entry points are ordinary console commands; clicks only enqueue text. */
static const MENUCOMMAND menu_commands[] = {
    { "menu_main", UI_MenuMain_f },
    { "menu_game", UI_MenuGame_f },
    { "menu_multiplayer", M_ShowLanBrowserMenu },
    { "menu_options", M_ShowOptionsMenu },
    { "menu_video", UI_MenuVideo_f },
    { "menu_keys", UI_MenuKeys_f },
    { "menu_loadgame", UI_MenuLoadGame_f },
    { "menu_savegame", UI_MenuSaveGame_f },
    { "menu_playerconfig", UI_MenuPlayerConfig_f },
    { "menu_startserver", UI_MenuStartServer_f },
    { "menu_joinserver", M_ShowLanBrowserMenu },
    { "menu_credits", M_ShowCreditsMenu },
    { "menu_quit", UI_MenuQuit_f },
    { "menu_disconnected", UI_MenuDisconnected_f },
    { "menu_realm_select", UI_MenuRealmSelect_f },
    { "menu_options_gameplay", UI_MenuOptionsGameplay_f },
    { "menu_options_sound", UI_MenuOptionsSound_f },
    { "menu_options_apply", UI_MenuOptionsApply_f },
    { "menu_single_player_campaign", UI_MenuSinglePlayerCampaign_f },
    { "menu_single_player_skirmish", M_ShowSinglePlayerSkirmishMenu },
    { "menu_lan_refresh", LAN_RefreshMaps },
    { "menu_lan_start", LAN_StartSelectedMap },
    { "menu_lan_join", LAN_JoinSelectedGame },
    { "menu_game_setup_start", UI_MenuGameSetupStart_f },
    { "menu_ingame", UI_ClearScreen },
    { "menu_edition", MainMenu_BeginEditionSwitch },
    { "menu_single_player_campaign_back", SinglePlayerMenu_BackCampaign },
    { "menu_single_player_campaign_human", UI_MenuCampaignHuman_f },
    { "menu_single_player_campaign_orc", UI_MenuCampaignOrc_f },
    { "menu_single_player_campaign_undead", UI_MenuCampaignUndead_f },
    { "menu_single_player_campaign_night_elf", UI_MenuCampaignNightElf_f },
    { "menu_single_player_campaign_tutorial", UI_MenuCampaignTutorial_f },
    { "menu_video_mode", UI_MenuVideoMode_f },
    { "menu_single_player_campaign_select", UI_MenuCampaignSelect_f },
    { "menu_single_player_mission_select", UI_MenuMissionSelect_f },
    { "menu_single_player_difficulty", UI_MenuDifficulty_f },
    { "menu_lan_select", UI_MenuLANSelect_f },
    { "menu_game_setup_slot_team_next", UI_MenuSlotTeam_f },
    { "menu_game_setup_slot_color_next", UI_MenuSlotColor_f },
    { "menu_game_setup_slot_type", UI_MenuSlotType_f },
    { "menu_game_setup_slot_race", UI_MenuSlotRace_f },
    { "menu_game_setup_map", UI_MenuSetupMap_f },
    { "menu_game_setup_chat", UI_MenuSetupChat_f },
};

/* Some classic/pre-widescreen skin tables expose only one of the paired
 * ConsoleTexture05/06 fields even though both extension tiles are installed.
 * When one symbolic key is absent, derive it from the resolved sibling path so
 * the active race/custom skin remains authoritative. */
/* Resolve resources before changing presentation; failure leaves the old screen intact. */
static BOOL UI_LoadScreen(uiScreen_t *screen) {
    if (!screen || screen == ui_current_screen || screen == ui_state.transition_screen || !screen->load || screen->load()) return true;
    fprintf(stderr, "UI: failed to load screen '%s'\n", screen->name);
    return false;
}

static void UI_UpdateMenuMusic(uiScreen_t *screen) {
    LPCSTR key, music;

    if (!screen) {
        mi.StopMusic();
        return;
    }
    key = screen == &gameSetupScreen ? "ChatMusic" : "GlueMusic";
    music = Theme_String(key, "Default");
    if (!music || !*music || !strcmp(music, key)) {
        key = "GlueMusic";
        music = Theme_String(key, "Default");
    }
    if (music && *music && strcmp(music, key)) mi.PlayMusic(music);
    else mi.StopMusic();
}

/* Installation never requests chrome: it also runs from animation callbacks. */
static void UI_InstallScreen(uiScreen_t *screen) {
    if (ui_current_screen == screen) return;
    fprintf(stderr, "UI_SetScreen: %s -> %s\n", ui_current_screen ? ui_current_screen->name : "(null)", screen ? screen->name : "(null)");
    if (ui_current_screen && ui_current_screen->shutdown) ui_current_screen->shutdown();
    ui_current_screen = screen;
    UI_UpdateMenuMusic(screen);
}

static void UI_FinishScreenTransition(void) { ui_state.transition_screen = NULL; }

/* Content selection runs only at the outgoing animation boundary. */
static void UI_BeginScreenTransition(void) {
    void (*configure)(void) = ui_state.configure;
    ui_state.configure = NULL;
    UI_InstallScreen(ui_state.transition_screen);
    if (configure) configure();
}

static void UI_FinishActionTransition(void) {
    void (*action)(void) = ui_state.transition_action;

    ui_state.transition_action = NULL;
    action();
}

/* Every menu route uses the same handoff, including commands issued at startup.
 * A loaded destination may prepare resources, but cannot replace outgoing content. */
static BOOL UI_RequestScreen(uiScreen_t *screen, GLUEDEST glue, void (*configure)(void)) {
    if (ui_state.transition_action || !UI_LoadScreen(screen)) return false;
    /* Prepare inactive data before returning to lobby/map command callers. Only
     * installation grants draw ownership, so preparation cannot flash controls. */
    if (screen && screen != ui_current_screen && screen != ui_state.transition_screen && screen->init) screen->init();
    ui_state.transition_screen = screen;
    ui_state.configure = configure;
    UI_GotoGluePanel(glue, UI_BeginScreenTransition, UI_FinishScreenTransition);
    return true;
}

static void UI_SetScreen(uiScreen_t *screen) {
    UI_RequestScreen(screen, screen ? screen->glue : (GLUEDEST){0}, NULL);
}

void M_TransitionToAction(void (*action)(void)) {
    if (ui_state.transition_screen || ui_state.transition_action) return;
    ui_state.transition_action = action;
    UI_CloseGluePanel(UI_FinishActionTransition);
}

uiScreen_t *UI_GetCurrentScreen(void) {
    return ui_current_screen;
}

__attribute__((visibility("hidden"))) void M_ShowMainMenu(void) {
    UI_RequestScreen(&mainMenuScreen, mainMenuScreen.glue, MainMenu_ShowMainPanel);
}

void M_ShowSinglePlayerMenu(void) {
    UI_RequestScreen(&singlePlayerMenuScreen, singlePlayerMenuScreen.glue, SinglePlayerMenu_ShowMain);
}

void M_ShowOptionsMenu(void) { UI_MenuOptionsGameplay_f(); }
void M_ShowCreditsMenu(void) { UI_SetScreen(&creditsMenuScreen); }

void M_ShowLanCreateMenu(void) {
    UI_RequestScreen(&lanJoinScreen, (GLUEDEST){ .panel = UI_GLUE_BATTLENET_CUSTOM, .tab = 1 }, LAN_ShowCreate);
}

static void M_ShowSinglePlayerSkirmishMenu(void) {
    UI_RequestScreen(&lanJoinScreen, (GLUEDEST){ .panel = UI_GLUE_SINGLE_PLAYER, .tab = 1 }, LAN_ShowSinglePlayerCreate);
}

void M_ShowLanBrowserMenu(void) {
    UI_RequestScreen(&lanJoinScreen, (GLUEDEST){ .panel = UI_GLUE_BATTLENET_CUSTOM }, LAN_ShowBrowser);
}

static BOOL UI_RequestGameSetup(void) {
    GLUEDEST glue = LAN_IsSinglePlayerCreate() ? (GLUEDEST){ .panel = UI_GLUE_SINGLE_PLAYER, .tab = 1 } : gameSetupScreen.glue;
    return UI_RequestScreen(&gameSetupScreen, glue, NULL);
}

void M_ShowGameSetupMenu(void) { UI_RequestGameSetup(); }

static void UI_MenuMain_f(void) { M_ShowMainMenu(); }
static void UI_MenuGame_f(void) { M_ShowSinglePlayerMenu(); }

static void UI_MenuVideo_f(void) {
    UI_RequestScreen(&optionsMenuScreen, (GLUEDEST){ .panel = UI_GLUE_OPTIONS, .tab = 1, .page = 1 }, OptionsMenu_ShowVideo);
}

static void UI_MenuKeys_f(void) { UI_MenuOptionsGameplay_f(); }

static void UI_MenuLoadGame_f(void) {
    mi.Cmd_ExecuteText("load quick\n");
}

static void UI_MenuSaveGame_f(void) {
    mi.Cmd_ExecuteText("save quick\n");
}

static void UI_MenuPlayerConfig_f(void) {
    /* TODO: bind the native profile manager before exposing profile editing. */
    fprintf(stderr, "UI: player profile configuration is not implemented\n");
}

static void UI_MenuStartServer_f(void) {
    LAN_ApplyPlayerName();
    M_ShowLanCreateMenu();
}

static void UI_MenuQuit_f(void) {
    UI_RequestScreen(&mainMenuScreen, mainMenuScreen.glue, MainMenu_ShowQuitConfirm);
}

static void UI_MenuDisconnected_f(void) {
    UI_RequestScreen(&mainMenuScreen, mainMenuScreen.glue, MainMenu_ShowDisconnected);
}

static void UI_MenuRealmSelect_f(void) {
    UI_RequestScreen(&mainMenuScreen, (GLUEDEST){ .panel = UI_GLUE_REALM_SELECTION }, MainMenu_ShowRealmSelect);
}

static void UI_MenuOptionsGameplay_f(void) {
    UI_RequestScreen(&optionsMenuScreen, optionsMenuScreen.glue, OptionsMenu_ShowGameplay);
}

static void UI_MenuOptionsSound_f(void) {
    UI_RequestScreen(&optionsMenuScreen, (GLUEDEST){ .panel = UI_GLUE_OPTIONS, .tab = 1, .page = 2 }, OptionsMenu_ShowSound);
}

static void UI_MenuOptionsApply_f(void) {
    OptionsMenu_Apply();
    UI_MenuMain_f();
}

static void UI_MenuSinglePlayerCampaign_f(void) {
    UI_RequestScreen(&singlePlayerMenuScreen, (GLUEDEST){0}, SinglePlayerMenu_ShowCampaign);
}

static void UI_MenuGameSetupStart_f(void) {
    if (GameSetup_StartGame()) {
        UI_ClearScreen();
    }
}

static void UI_RegisterMenuCommands(void) {
    if (ui_menu_commands_registered) return;
    FOR_LOOP(i, sizeof(menu_commands) / sizeof(menu_commands[0]))
        mi.Cmd_AddCommand(menu_commands[i].name, menu_commands[i].func);
    ui_menu_commands_registered = true;
}

/* Gameplay and shutdown cancel presentation outright; they are not menu navigation. */
static void UI_ClearScreen(void) {
    ui_state.transition_screen = NULL;
    ui_state.transition_action = NULL;
    ui_state.configure = NULL;
    UI_ResetGlueTransitions();
    UI_InstallScreen(NULL);
}

/* Refresh frame state flags before dispatch so draw never asks for mouse position. */
static void UI_UpdateMouseFrameFlags(LPCFRAMEDEF hit, BOOL clear_pressed) {
    FOR_LOOP(i, MAX_UI_CLASSES) {
        LPFRAMEDEF frame = &frames[i];
        if (!frame->inuse) {
            continue;
        }
        frame->ui_flags &= ~(UIFLAG_HOVERED | UIFLAG_ACTIVE);
        if (clear_pressed) {
            frame->ui_flags &= ~UIFLAG_PRESSED;
        }
        if (frame->hidden) frame->ui_flags &= ~UIFLAG_VISIBLE;
        else frame->ui_flags |= UIFLAG_VISIBLE;
        if (frame->disabled) frame->ui_flags |= UIFLAG_DISABLED;
        else frame->ui_flags &= ~UIFLAG_DISABLED;
    }
    if (hit) {
        ((LPFRAMEDEF)hit)->ui_flags |= UIFLAG_HOVERED | UIFLAG_ACTIVE;
    }
}

void M_Init(void) {
    memset(&ui_state, 0, sizeof(ui_state));
    UI_ResetGlueSceneModels();
    UI_RegisterMenuCommands();
    
    mi.Printf("M_Init: loading FDF assets\n");

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
    
    ui_state.initialized = true;
    ui_state.active = true;
    
    /*
     * Map launches use the server-authored in-game HUD via svc_layout.  Leave
     * the client-side menu screen idle there so no glue screen covers the game.
     */
    LPCSTR map = mi.Cvar_String
        ? mi.Cvar_String("map", "")
        : "";
    if (map && *map) {
        UI_ClearScreen();
        return;
    }

}

void M_Shutdown(void) {
    UI_ClearScreen();
    UI_ReleaseGlueSceneModels();
    UI_ReleaseAssets();
    UI_ClearTemplates();
    memset(&ui_state, 0, sizeof(ui_state));
}

void M_SetActive(BOOL active) {
    ui_state.active = active;
}

DWORD M_Time(void) {
    return ui_state.time;
}

BOOL M_IsTransitioning(void) {
    return ui_state.transition_screen || ui_state.transition_action || UI_GlueIsTransitioning();
}

/* Left subtree declarations split native FDF without changing its layout. All
 * remaining controls belong to the right side, including screen-level dialogs. */
FLOAT UI_ScreenFrameOffset(LPCFRAMEDEF frame) {
    uiScreen_t *screen = UI_GetCurrentScreen();
    if (!screen) return 0;
    for (LPCFRAMEDEF cur = frame; cur; cur = cur->Parent)
        for (LPCSTR const *name = screen->left; name && *name; name++)
            if (!strcmp(cur->Name, *name)) return UI_GlueSideOffset(UI_GLUE_LEFT);
    return UI_GlueSideOffset(UI_GLUE_RIGHT);
}

void M_Refresh(DWORD time) {
    if (!ui_state.active) {
        return;
    }

    ui_state.time = time;
    /* Call current screen refresh */
    uiScreen_t *screen = UI_GetCurrentScreen();
    if (screen && screen->refresh) {
        screen->refresh((int)time);
    }

    UI_DrawGlueScene();
    screen = UI_GetCurrentScreen();
    if (screen && screen->draw)
        screen->draw();
}

/* SDL text events bypass KeyEvent; apply the same ownership/transition gate before editing. */
void M_TextInput(LPCSTR text) {
    if (!ui_state.active || M_IsTransitioning() || (ui_state.initialized && !UI_GetCurrentScreen())) return;
    UI_EditTextInput(text);
}

void M_KeyEvent(int key, BOOL down, DWORD time) {
    (void)time;

    if (!ui_state.active || M_IsTransitioning()) {
        return;
    }

    if (down && M_EditKey(key)) {
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
    LPRENDERER renderer = mi.GetRenderer();
    size2_t window = renderer && renderer->GetWindowSize ? renderer->GetWindowSize() : MAKE(size2_t, 0, 0);
    RECT scene = UI_GetSceneRect();
    FLOAT nx = 0;
    FLOAT ny = 0;

    if (window.width > 0 && window.height > 0) {
        nx = (FLOAT)px / (FLOAT)window.width;
        ny = (FLOAT)py / (FLOAT)window.height;
    }
    return MAKE(VECTOR2, scene.x + nx * scene.w, scene.y + ny * scene.h);
}

/* All UI mouse work starts here so draw code only consumes event-updated state. */
BOOL M_MouseEvent(menuMouseEvent_t event, int x, int y, int32_t param) {
    BOOL const down = event == MENU_MOUSE_DOWN;
    BOOL const up = event == MENU_MOUSE_UP;
    BOOL const left = param == 1;
    int const wheel_y = event == MENU_MOUSE_SCROLL ? MENU_MOUSE_PARAM_Y(param) : 0;
    /* In the initialized runtime, a current screen is the ownership token for
     * standalone FDF input. Gameplay clears the screen, but menu_render.c keeps
     * the previous layout cache; never hit-test those stale invisible frames.
     * Uninitialized unit tests may exercise the low-level FDF event path
     * directly without installing a screen controller. */
    if (!ui_state.active || M_IsTransitioning() ||
        (ui_state.initialized && !UI_GetCurrentScreen())) {
        return false;
    }

    VECTOR2 fdf = UI_PixelToFdf(x, y);
    ui_state.mouse_fdf = fdf;
    LPCFRAMEDEF hit = UI_HitTest(fdf.x, fdf.y);
    UI_UpdateMouseFrameFlags(hit, up && left);

    /* Dispatch to per-type event handler */
    if (hit && hit->event_handler) {
        hit->event_handler((LPFRAMEDEF)hit, event, fdf.x, fdf.y, param);
    }

    /* Global: editbox clear focus on miss (LEFT_DOWN outside any editbox) */
    if (down && left) {
        BOOL hit_editbox = hit && (hit->Type == FT_EDITBOX || hit->Type == FT_GLUEEDITBOX ||
                                   hit->Type == FT_SLASHCHATBOX);
        if (!hit_editbox) {
            UI_EditboxClearFocusOnMiss();
        }
    }

    /* Global: slider drag tracking (motion when no frame hit) */
    if (UI_SliderIsDragging() && event == MENU_MOUSE_MOVE) {
        UI_SliderUpdateDrag(UI_SliderActiveFrame(), fdf.x, fdf.y);
    }
    if (up && left) {
        UI_SliderEndDrag(NULL);
    }

    /* Global: popup close on outside click */
    if (down && left && UI_HasActivePopup() && !UI_PopupPointInside(fdf.x, fdf.y)) {
        UI_PopupCloseOnMiss();
    }

    /* Global: popup menu wheel scroll */
    if (UI_HasActivePopup() && wheel_y > 0) {
        UI_PopupMenuScroll(true);
    }
    if (UI_HasActivePopup() && wheel_y < 0) {
        UI_PopupMenuScroll(false);
    }
    if (UI_HasActivePopup() && up && left) {
        UI_PopupSelectItem(fdf.x, fdf.y);
    }

    UI_PopupMenuHover(fdf.x, fdf.y);

    return hit != NULL;
}

/* Keep click handling deferred until the client executes its command buffer. */
void UI_QueueCommand(LPCSTR command) {
    mi.Cmd_ExecuteText(command);
    mi.Cmd_ExecuteText("\n");
}

/* IDs must be complete unsigned DWORD tokens, not negative or overflowing scanf conversions. */
static BOOL UI_MenuNumber(LPCSTR text, LPDWORD value) {
    char *end;
    errno = 0;
    if (*text < '0' || *text > '9') return false;
    unsigned long num = strtoul(text, &end, 10);
    if (errno == ERANGE || num > UINT32_MAX || *end) return false;
    *value = (DWORD)num;
    return true;
}

/* Console tokenization owns quoting/whitespace; these callbacks validate only their argument shape. */
static BOOL UI_MenuNumbers(LPDWORD nums, int count) {
    if (mi.Cmd_Argc() == count + 1) {
        int i;
        for (i = 0; i < count && UI_MenuNumber(mi.Cmd_Argv(i + 1), &nums[i]); i++) {}
        if (i == count) return true;
    }
    fprintf(stderr, "UI: %s expects %d unsigned integer argument(s)\n", mi.Cmd_Argv(0), count);
    return false;
}

static void UI_MenuIndex(void (*func)(DWORD)) {
    DWORD num;
    if (UI_MenuNumbers(&num, 1)) func(num);
}
static void UI_MenuPair(void (*func)(DWORD, DWORD)) {
    DWORD nums[2];
    if (UI_MenuNumbers(nums, 2)) func(nums[0], nums[1]);
}

static void UI_MenuVideoMode(DWORD value) {
    char command[96];
    if (value == video_mode_count()) mi.Cmd_ExecuteText("seta vid_native 1\nseta vid_fullscreen 1\n");
    else if (value < video_mode_count()) {
        snprintf(command, sizeof(command), "seta vid_native 0\nseta vid_mode %u\n", (unsigned)value);
        mi.Cmd_ExecuteText(command);
    } else fprintf(stderr, "UI: invalid video mode %u\n", (unsigned)value);
}

static void UI_MenuVideoMode_f(void) { UI_MenuIndex(UI_MenuVideoMode); }
static void UI_MenuCampaignSelect_f(void) { UI_MenuIndex(SinglePlayerMenu_LaunchCampaignIndex); }
static void UI_MenuMissionSelect_f(void) { UI_MenuIndex(SinglePlayerMenu_LaunchMissionIndex); }
static void UI_MenuDifficulty_f(void) { UI_MenuIndex(SinglePlayerMenu_SetDifficulty); }
static void UI_MenuLANSelect_f(void) { UI_MenuIndex(LAN_SelectMapIndex); }
static void UI_MenuSlotTeam_f(void) { UI_MenuIndex(GameSetup_CycleSlotTeam); }
static void UI_MenuSlotColor_f(void) { UI_MenuIndex(GameSetup_CycleSlotColor); }
static void UI_MenuSlotType_f(void) { UI_MenuPair(GameSetup_SetSlotType); }
static void UI_MenuSlotRace_f(void) { UI_MenuPair(GameSetup_SetSlotRace); }
static void UI_MenuCampaignHuman_f(void) { SinglePlayerMenu_LaunchCampaign("human"); }
static void UI_MenuCampaignOrc_f(void) { SinglePlayerMenu_LaunchCampaign("orc"); }
static void UI_MenuCampaignUndead_f(void) { SinglePlayerMenu_LaunchCampaign("undead"); }
/* CampaignStrings uses NightElf; the old hyphenated shortcut never resolved a campaign. */
static void UI_MenuCampaignNightElf_f(void) { SinglePlayerMenu_LaunchCampaign("NightElf"); }
static void UI_MenuCampaignTutorial_f(void) { SinglePlayerMenu_LaunchCampaign("tutorial"); }

/* A quoted map path is one console argument, including spaces and archive separators. */
static void UI_MenuSetupMap_f(void) {
    if (mi.Cmd_Argc() != 2 || !*mi.Cmd_Argv(1)) {
        fprintf(stderr, "UI: %s expects a map path\n", mi.Cmd_Argv(0));
        return;
    }
    if (UI_RequestGameSetup()) GameSetup_LoadMap(mi.Cmd_Argv(1));
}

/* Chat retains the optional legacy numeric ownership prefix and the complete message. */
static void UI_MenuSetupChat_f(void) {
    DWORD own = 0;
    int first = UI_MenuNumber(mi.Cmd_Argv(1), &own) ? 2 : 1;
    LPCSTR text = mi.Cmd_ArgsFrom(first);
    if (*text) GameSetup_AddChatMessage(text, own);
    else fprintf(stderr, "UI: %s expects a chat message\n", mi.Cmd_Argv(0));
}

static void M_UpdateLobbySetup(lobbyState_t const *state) {
    /* No current standalone screen means loading/gameplay owns presentation;
     * late lobby packets must not resurrect the game-setup glue screen. */
    if (!UI_GetCurrentScreen()) {
        return;
    }
    if (UI_RequestGameSetup()) GameSetup_UpdateLobbySetup(state);
}

/* Export function table */
menuExport_t M_GetAPI(menuImport_t import) {
    mi = import;
    
    menuExport_t exp;
    memset(&exp, 0, sizeof(exp));
    
    exp.Init = M_Init;
    exp.Shutdown = M_Shutdown;
    exp.Refresh = M_Refresh;
    exp.KeyEvent = M_KeyEvent;
    exp.TextInput = M_TextInput;
    exp.MouseEvent = M_MouseEvent;
    exp.UpdateLobbySetup = M_UpdateLobbySetup;
    
    return exp;
}
