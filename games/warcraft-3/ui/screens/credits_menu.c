/*
 * ui/screens/credits_menu.c — Main-menu credits screen controller.
 */

#include "../ui_local.h"
#include "../ui_dialog.h"
#include "../ui_screen.h"

static LPFRAMEDEF credits_root;
static LPFRAMEDEF credits_bg, credits_left_panel, credits_right_panel;
static uiDialogWar3_t credits_dialog;

static BOOL CreditsMenu_LoadScreen(void) {
    credits_root = UI_FindFrame("CreditsMenu");
    if (!credits_root) {
        credits_root = UI_Spawn(FT_FRAME, NULL);
        if (!credits_root) return false;
        snprintf(credits_root->Name, sizeof(credits_root->Name), "CreditsMenu");
    }
    UI_SetAllPoints(credits_root);
    return credits_root != NULL;
}

static void CreditsMenu_Init(void) {
    uiDialogWar3Init_t init = {
        .modal_name = "CreditsDialogModal",
        .template_name = "BattleNetDialogTemplate",
    };
    uiDialogWar3Config_t config = {
        .message = "OpenWarcraft3\n\nA Quake-style RTS runtime for Warcraft III data.\n\nThe project began with a Warcraft III map renderer I wrote around 2006. In 2023 I decided to restore that old work and push it further: not just a viewer, but a playable game engine.\n\nThere is still a lot to build, fix, and polish. Contributors are always welcome.\n\nOriginal Warcraft III assets and names belong to Blizzard Entertainment.",
        .icon = UI_DIALOG_WAR3_ICON_MESSAGE,
        .buttons = UI_DIALOG_WAR3_BUTTONS_OK,
        .ok_command = "menu_main",
    };

    uiimport.Printf("CreditsMenu_Init\n");
    UI_PreloadGlueSceneModels();
    UI_SpawnGlueSceneFrames(credits_root, "MainMenu Stand", "MainMenu Stand",
                            &credits_bg, &credits_left_panel, &credits_right_panel);
    UI_SetHidden(credits_root, false);
    if (UI_DialogWar3Init(&credits_dialog, credits_root, &init)) {
        UI_DialogWar3Show(&credits_dialog, &config);
    }
}

static void CreditsMenu_Shutdown(void) {
    UI_SetHidden(credits_root, true);
    UI_DialogWar3Hide(&credits_dialog);
}

static void CreditsMenu_Refresh(int msec) {
    (void)msec;
}

static void CreditsMenu_Draw(void) {
    if (credits_dialog.modal) {
        UI_DrawFrame(credits_dialog.modal);
    }
}

static void CreditsMenu_KeyEvent(int key, BOOL down) {
    (void)key;
    (void)down;
}

uiScreen_t creditsMenuScreen = {
    .name = "credits",
    .load = CreditsMenu_LoadScreen,
    .init = CreditsMenu_Init,
    .shutdown = CreditsMenu_Shutdown,
    .refresh = CreditsMenu_Refresh,
    .draw = CreditsMenu_Draw,
    .key_event = CreditsMenu_KeyEvent,
};
