/*
 * ui/screens/main_menu.c — Main menu screen controller.
 *
 * This screen displays the main menu with buttons for:
 * - Realm Select (Battle.net)
 * - Single Player
 * - Battle.net
 * - Local Area Network
 * - Options
 * - Credits
 * - Exit
 *
 * The screen loads MainMenu.fdf frame definitions and sends menu commands
 * to UI_MenuCommand() for navigation.
 */

#include "../menu_local.h"
#include "../menu_dialog.h"
#include "../menu_screen.h"
#include "../generated/main_menu.h"

/* Generated FDF frame references */
static MainMenu_t main_menu;
static uiDialogWar3_t quit_dialog;

/* State */
static BOOL show_realm_select = false;
static LPFRAMEDEF edition_button;
static BOOL edition_switch_pending;
static BOOL edition_switch_requested;
static BOOL edition_switch_target_expansion;
static BOOL single_player_transition_pending;

static BOOL MainMenu_LoadScreen(void) {
    return MainMenu_Load(&main_menu);
}

static void MainMenu_InitQuitDialog(void) {
    uiDialogWar3Init_t init = {
        .modal_name = "MainMenuQuitModal",
        .template_name = "DialogWar3",
    };

    UI_DialogWar3Init(&quit_dialog, main_menu.MainMenuFrame, &init);
}

static void MainMenu_ShowQuitDialog(void) {
    uiDialogWar3Config_t config = {
        .message = "Do you want to Quit?",
        .icon = UI_DIALOG_WAR3_ICON_QUESTION,
        .buttons = UI_DIALOG_WAR3_BUTTONS_YES_NO,
        .no_command = "menu_main",
        .yes_command = "quit",
    };

    UI_DialogWar3Show(&quit_dialog, &config);
}

static void MainMenu_InitFrames(void) {
    if (!main_menu.MainMenuFrame) {
        menuimport.Printf("ERROR: MainMenuFrame not found\n");
        return;
    }

    if (main_menu.RealmSelect) {
        UI_SetHidden(main_menu.RealmSelect, true);
    }
    if (main_menu.ControlLayer) {
        UI_SetHidden(main_menu.ControlLayer, false);
    }
    if (main_menu.WarCraftIIILogo) {
        DWORD logo_model = UI_LoadModel("MainMenuLogo", true);
        if (logo_model) {
            main_menu.WarCraftIIILogo->Portrait.model = logo_model;
        }
        UI_SetPoint(main_menu.WarCraftIIILogo,
                    FRAMEPOINT_TOPLEFT,
                    main_menu.MainMenuFrame,
                    FRAMEPOINT_TOPLEFT,
                    0.13f,
                    -0.08f);
    }

    UI_SetOnClick(main_menu.RealmButton, "menu_realm_select");
    UI_SetOnClick(main_menu.SinglePlayerButton, "menu_game");
    UI_SetOnClick(main_menu.BattleNetButton, "menu_multiplayer");
    UI_SetOnClick(main_menu.LocalAreaNetworkButton, "menu_multiplayer");
    UI_SetOnClick(main_menu.OptionsButton, "menu_options");
    UI_SetOnClick(main_menu.CreditsButton, "menu_credits");
    UI_SetOnClick(main_menu.ExitButton, "menu_quit");
    /* EditionButton exists in Blizzard's native MainMenu FDF but is optional
     * for reduced/test FDFs, so bind it without making generated-load success
     * depend on the frame being present. */
    edition_button = UI_FindChildFrame(main_menu.MainMenuFrame, "EditionButton");
    if (edition_button) UI_SetOnClick(edition_button, "menu_edition");
    MainMenu_InitQuitDialog();

    /* Realm select buttons */
    if (main_menu.RealmSelect) {
        UI_SetOnClick(main_menu.RealmSelectOKButton, "menu_main");
        UI_SetOnClick(main_menu.RealmSelectCancelButton, "menu_main");
    }
}

static void MainMenu_Init(void) {
    menuimport.Printf("MainMenu_Init\n");
    edition_button = NULL;
    edition_switch_pending = false;
    edition_switch_requested = false;
    single_player_transition_pending = false;
    UI_PreloadGlueSceneModels();
    MainMenu_InitFrames();
    MainMenu_ShowMainPanel();
}

static void MainMenu_Shutdown(void) {
    edition_button = NULL;
    edition_switch_pending = false;
    edition_switch_requested = false;
    single_player_transition_pending = false;
}

static void MainMenu_ApplyEdition(BOOL expansion) {
    HANDLE data = NULL;
    int size;

    menuimport.Cvar_Set("fs_expansion", expansion ? "1" : "0");
    if (!expansion) return;

    size = menuimport.FS_ReadFile("UI\\CampaignStrings_exp.txt", &data);
    if (data) menuimport.FS_FreeFile(data);
    if (size > 0) return;

    menuimport.Cvar_Set("fs_expansion", "0");
    menuimport.Printf("The Frozen Throne data is unavailable.\n");
}

static void MainMenu_Refresh(int msec) {
    (void)msec;
}

static void MainMenu_Draw(void) {
    LPCFRAMEDEF roots[2];
    DWORD num_roots = 0;

    if (edition_switch_pending) {
        UI_DrawGlueScene("MainMenu Death");
        if (UI_GlueSceneAnimationComplete() && !edition_switch_requested) {
            edition_switch_requested = true;
            MainMenu_ApplyEdition(edition_switch_target_expansion);
            menuimport.Cmd_ExecuteText("menu_restart\n");
        }
        return;
    }

    if (single_player_transition_pending) {
        UI_DrawGlueScene("MainMenu Death");
        if (UI_GlueSceneAnimationComplete()) {
            single_player_transition_pending = false;
            M_ShowSinglePlayerMenu();
        }
        return;
    }

    UI_DrawGlueScene(show_realm_select ? "RealmSelection Stand" : "MainMenu Stand");

    if (main_menu.MainMenuFrame) {
        roots[num_roots++] = main_menu.MainMenuFrame;
    }
    if (UI_DialogWar3Visible(&quit_dialog)) {
        roots[num_roots++] = quit_dialog.modal;
    }
    if (num_roots > 0) {
        UI_DrawFrames(roots, num_roots);
    }
}

static void MainMenu_KeyEvent(int key, BOOL down) {
    /* Handle key presses */
    (void)key;
    (void)down;
}

void MainMenu_BeginSinglePlayer(void) {
    if (single_player_transition_pending || edition_switch_pending || !main_menu.MainMenuFrame) return;
    single_player_transition_pending = true;
    show_realm_select = false;
    UI_DialogWar3Hide(&quit_dialog);
    UI_SetHidden(main_menu.MainMenuFrame, true);
}

void MainMenu_BeginEditionSwitch(void) {
    LPCSTR expansion;

    if (edition_switch_pending || !main_menu.MainMenuFrame) return;
    expansion = menuimport.Cvar_String("fs_expansion", "0");
    edition_switch_target_expansion = !(expansion && atoi(expansion) != 0);
    edition_switch_pending = true;
    edition_switch_requested = false;
    show_realm_select = false;
    UI_DialogWar3Hide(&quit_dialog);
    UI_SetHidden(main_menu.MainMenuFrame, true);
}

void MainMenu_ShowMainPanel(void) {
    show_realm_select = false;
    UI_DialogWar3Hide(&quit_dialog);
    if (main_menu.MainMenuFrame) {
        UI_SetHidden(main_menu.MainMenuFrame, false);
    }
    if (main_menu.RealmSelect) {
        UI_SetHidden(main_menu.RealmSelect, true);
    }
    if (main_menu.ControlLayer) {
        UI_SetHidden(main_menu.ControlLayer, false);
    }
    if (main_menu.WarCraftIIILogo) {
        UI_SetHidden(main_menu.WarCraftIIILogo, false);
    }
}

void MainMenu_ShowRealmSelect(void) {
    UI_DialogWar3Hide(&quit_dialog);
    show_realm_select = true;
    if (main_menu.RealmSelect) {
        UI_SetHidden(main_menu.RealmSelect, false);
    }
    if (main_menu.ControlLayer) {
        UI_SetHidden(main_menu.ControlLayer, true);
    }
    if (main_menu.WarCraftIIILogo) {
        UI_SetHidden(main_menu.WarCraftIIILogo, true);
    }
}

void MainMenu_ShowQuitConfirm(void) {
    MainMenu_ShowQuitDialog();
}

void MainMenu_ShowDisconnected(void) {
    uiDialogWar3Config_t config = {
        .message = "You were disconnected from the game.",
        .icon = UI_DIALOG_WAR3_ICON_MESSAGE,
        .buttons = UI_DIALOG_WAR3_BUTTONS_OK,
        .ok_command = "menu_main",
    };

    show_realm_select = false;
    if (main_menu.RealmSelect) {
        UI_SetHidden(main_menu.RealmSelect, true);
    }
    if (main_menu.ControlLayer) {
        UI_SetHidden(main_menu.ControlLayer, false);
    }
    if (main_menu.WarCraftIIILogo) {
        UI_SetHidden(main_menu.WarCraftIIILogo, false);
    }
    UI_DialogWar3Show(&quit_dialog, &config);
}

uiScreen_t mainMenuScreen = {
    .name = "main",
    .load = MainMenu_LoadScreen,
    .init = MainMenu_Init,
    .shutdown = MainMenu_Shutdown,
    .refresh = MainMenu_Refresh,
    .draw = MainMenu_Draw,
    .key_event = MainMenu_KeyEvent,
};
