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

#include "../ui_local.h"
#include "../ui_dialog.h"
#include "../ui_screen.h"
#include "../generated/main_menu.h"

/* Generated FDF frame references */
static MainMenu_t main_menu;
static uiDialogWar3_t quit_dialog;

/* State */
static BOOL show_realm_select = false;

static BOOL MainMenu_LoadScreen(void) {
    return MainMenu_Load(&main_menu);
}

static void MainMenu_RootDraw(struct uiBaseFrame_s *base, LPCRECT rect) {
    (void)base; (void)rect;
    UI_DrawGlueScene(show_realm_select ? "RealmSelection Stand" : "MainMenu Stand");
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
        uiimport.Printf("ERROR: MainMenuFrame not found\n");
        return;
    }
    main_menu.MainMenuFrame->base.on_draw = MainMenu_RootDraw;
    UI_SetHidden(main_menu.MainMenuFrame, false);

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
    MainMenu_InitQuitDialog();

    /* Realm select buttons */
    if (main_menu.RealmSelect) {
        UI_SetOnClick(main_menu.RealmSelectOKButton, "menu_main");
        UI_SetOnClick(main_menu.RealmSelectCancelButton, "menu_main");
    }
}

static void MainMenu_Init(void) {
    uiimport.Printf("MainMenu_Init\n");
    UI_PreloadGlueSceneModels();
    MainMenu_InitFrames();
    MainMenu_ShowMainPanel();
}

static void MainMenu_Shutdown(void) {
    UI_SetHidden(main_menu.MainMenuFrame, true);
    UI_DialogWar3Hide(&quit_dialog);
}

static void MainMenu_Refresh(int msec) {
    (void)msec;
    /* Update animations, etc. */
}

static void MainMenu_Draw(void) {
    LPCFRAMEDEF roots[2];
    DWORD num_roots = 0;

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

static void MainMenu_MouseEvent(int x, int y, int buttons) {
    /* Handle mouse events */
    (void)x;
    (void)y;
    (void)buttons;
}

void MainMenu_ShowMainPanel(void) {
    show_realm_select = false;
    UI_DialogWar3Hide(&quit_dialog);
    UI_SetHidden(main_menu.MainMenuFrame, false);
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
    UI_SetHidden(main_menu.MainMenuFrame, false);
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
    UI_SetHidden(main_menu.MainMenuFrame, false);
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
    UI_SetHidden(main_menu.MainMenuFrame, false);
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
    .mouse_event = MainMenu_MouseEvent,
};
