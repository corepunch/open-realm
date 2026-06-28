/*
 * ui/screens/quit_confirm.c — Main-menu quit confirmation screen.
 *
 * This reuses the existing Esc menu confirm-quit panel so the quit flow is
 * modal and consistent with the in-game confirm dialog styling.
 */

#include "../ui_local.h"
#include "../ui_screen.h"
#include "../generated/esc_menu_main_panel.h"

static EscMenuMainPanel_t quit_confirm;

static BOOL QuitConfirm_LoadScreen(void) {
    return EscMenuMainPanel_Load(&quit_confirm);
}

static void QuitConfirm_UpdateVisibility(void) {
    if (quit_confirm.EscMenuMainPanel) {
        UI_SetHidden(quit_confirm.EscMenuMainPanel, false);
    }
    if (quit_confirm.MainPanel) {
        UI_SetHidden(quit_confirm.MainPanel, true);
    }
    if (quit_confirm.EndGamePanel) {
        UI_SetHidden(quit_confirm.EndGamePanel, true);
    }
    if (quit_confirm.HelpPanel) {
        UI_SetHidden(quit_confirm.HelpPanel, true);
    }
    if (quit_confirm.TipsPanel) {
        UI_SetHidden(quit_confirm.TipsPanel, true);
    }
    if (quit_confirm.ConfirmQuitPanel) {
        UI_SetHidden(quit_confirm.ConfirmQuitPanel, false);
    }
}

static void QuitConfirm_Init(void) {
    uiimport.Printf("QuitConfirm_Init\n");
    UI_PreloadGlueSceneModels();

    if (!quit_confirm.EscMenuMainPanel) {
        uiimport.Printf("ERROR: EscMenuMainPanel not found\n");
        return;
    }
    if (!quit_confirm.ConfirmQuitPanel) {
        uiimport.Printf("ERROR: ConfirmQuitPanel not found\n");
        return;
    }

    if (quit_confirm.ConfirmQuitQuitButton) {
        UI_SetOnClick(quit_confirm.ConfirmQuitQuitButton, "quit");
    }
    if (quit_confirm.ConfirmQuitCancelButton) {
        UI_SetOnClick(quit_confirm.ConfirmQuitCancelButton, "menu_main");
    }

    QuitConfirm_UpdateVisibility();
}

static void QuitConfirm_Shutdown(void) {
    if (quit_confirm.EscMenuMainPanel) {
        UI_SetHidden(quit_confirm.EscMenuMainPanel, true);
    }
    if (quit_confirm.ConfirmQuitPanel) {
        UI_SetHidden(quit_confirm.ConfirmQuitPanel, true);
    }
}

static void QuitConfirm_Refresh(int msec) {
    (void)msec;
}

static void QuitConfirm_Draw(void) {
    UI_DrawGlueScene("MainMenu Stand");
    if (quit_confirm.EscMenuMainPanel) {
        UI_DrawFrame(quit_confirm.EscMenuMainPanel);
    }
}

static void QuitConfirm_KeyEvent(int key, BOOL down) {
    if (down && key == 27) {
        UI_ShowMainMenu();
    }
}

uiScreen_t quitConfirmScreen = {
    .name = "quit-confirm",
    .load = QuitConfirm_LoadScreen,
    .init = QuitConfirm_Init,
    .shutdown = QuitConfirm_Shutdown,
    .refresh = QuitConfirm_Refresh,
    .draw = QuitConfirm_Draw,
    .key_event = QuitConfirm_KeyEvent,
};
