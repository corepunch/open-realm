/*
 * ui/screens/credits_menu.c — Main-menu credits screen controller.
 */

#include "../menu_local.h"
#include "../menu_dialog.h"
#include "../menu_screen.h"
#include "../generated/contributors.h"

static frameDef_t *credits_root;
static uiDialogWar3_t credits_dialog;

static bool CreditsMenu_LoadScreen(void) {
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
        .message = "OpenWarcraft3\n\nA Quake-style RTS runtime for Warcraft III data.\n\nThe project began with a Warcraft III map renderer I wrote around 2006. In 2023 I decided to restore that old work and push it further: not just a viewer, but a playable game engine.\n\nThere is still a lot to build, fix, and polish. Contributors are always welcome.\n\nContributors: " WC3_CREDITS_CONTRIBUTORS "\n\nOriginal Warcraft III assets and names belong to Blizzard Entertainment.",
        .icon = UI_DIALOG_WAR3_ICON_MESSAGE,
        .buttons = UI_DIALOG_WAR3_BUTTONS_OK,
        .ok_command = "menu_main",
    };

    mi.Printf("CreditsMenu_Init\n");
    if (UI_DialogWar3Init(&credits_dialog, credits_root, &init)) {
        UI_DialogWar3Show(&credits_dialog, &config);
    }
}

static void CreditsMenu_Shutdown(void) {
}

static void CreditsMenu_Refresh(int msec) {
    (void)msec;
}

static void CreditsMenu_Draw(void) {
    if (credits_dialog.modal) {
        UI_DrawFrame(credits_dialog.modal);
    }
}

static void CreditsMenu_KeyEvent(int key, bool down) {
    (void)key;
    (void)down;
}

uiScreen_t creditsMenuScreen = {
    .name = "credits",
    .glue = { .panel = UI_GLUE_MAIN_MENU },
    .load = CreditsMenu_LoadScreen,
    .init = CreditsMenu_Init,
    .shutdown = CreditsMenu_Shutdown,
    .refresh = CreditsMenu_Refresh,
    .draw = CreditsMenu_Draw,
    .key_event = CreditsMenu_KeyEvent,
};
