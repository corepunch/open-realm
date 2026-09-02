/* hud_menu.c — Server-authored in-game pause menu using Blizzard's Esc FDF. */

#include "hud_local.h"
#include "../generated/esc_menu_main_panel.h"

static EscMenuMainPanelGame_t menu;
static BOOL menu_loaded, menu_valid;

typedef enum {
    MENU_PANEL_MAIN,
    MENU_PANEL_END_GAME,
    MENU_PANEL_CONFIRM_QUIT,
} menuPanel_t;

static LPFRAMEDEF MenuPanel(menuPanel_t panel) {
    switch (panel) {
        case MENU_PANEL_END_GAME: return menu.EndGamePanel;
        case MENU_PANEL_CONFIRM_QUIT: return menu.ConfirmQuitPanel;
        case MENU_PANEL_MAIN:
        default: return menu.MainPanel;
    }
}

static BOOL MenuEnsureLoaded(void) {
    if (menu_loaded) return menu_valid;
    menu_loaded = true;
    if (!EscMenuMainPanelGame_Load(&menu)) return false;
    if (!menu.EscMenuBackdrop) {
        fprintf(stderr, "WC3 menu: missing EscMenuBackdrop\n");
        return false;
    }

    UI_SetParent(menu.EscMenuBackdrop, menu.EscMenuMainPanel);
    /* The separate backdrop root loads after the panels. Nest all Esc-menu
     * panel subtrees below it so decoration serializes/draws before controls;
     * explicit anchors still target the bounded controller. */
    UI_SetParent(menu.MainPanel, menu.EscMenuBackdrop);
    UI_SetParent(menu.EndGamePanel, menu.EscMenuBackdrop);
    UI_SetParent(menu.ConfirmQuitPanel, menu.EscMenuBackdrop);
    UI_SetParent(menu.HelpPanel, menu.EscMenuBackdrop);
    UI_SetParent(menu.TipsPanel, menu.EscMenuBackdrop);

    /* Current Warsmash leaves these authored controls present but disabled.
     * OpenRealm's newer pause lifecycle intentionally reuses PauseButton as a
     * second Resume action, so only features that remain unimplemented stay
     * disabled here. */
    UI_SetEnabled(menu.SaveGameButton, false);
    UI_SetEnabled(menu.LoadGameButton, false);
    UI_SetEnabled(menu.OptionsButton, false);
    UI_SetEnabled(menu.HelpButton, false);
    UI_SetEnabled(menu.TipsButton, false);
    UI_SetEnabled(menu.RestartButton, false);

    UI_SetText(menu.PauseButtonText, "Resume Game");
    UI_SetText(menu.ReturnButtonText, "Return to Game");
    UI_SetOnClick(menu.PauseButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(menu.ReturnButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(menu.EndGameButton, "menu_endgame");
    UI_SetOnClick(menu.PreviousButton, "menu");
    UI_SetOnClick(menu.QuitButton, UI_WINDOW_DISCONNECT_ACTION);
    UI_SetOnClick(menu.ExitButton, "menu_confirm_exit");
    UI_SetOnClick(menu.ConfirmQuitCancelButton, "menu_endgame");
    UI_SetOnClick(menu.ConfirmQuitQuitButton, UI_WINDOW_QUIT_ACTION);
    menu_valid = true;
    return true;
}

static void MenuSelectPanel(menuPanel_t panel) {
    LPFRAMEDEF active = MenuPanel(panel);

    UI_SetHidden(menu.EscMenuMainPanel, false);
    UI_SetHidden(menu.EscMenuBackdrop, false);
    UI_SetHidden(menu.MainPanel, panel != MENU_PANEL_MAIN);
    UI_SetHidden(menu.EndGamePanel, panel != MENU_PANEL_END_GAME);
    UI_SetHidden(menu.ConfirmQuitPanel, panel != MENU_PANEL_CONFIRM_QUIT);
    UI_SetHidden(menu.HelpPanel, true);
    UI_SetHidden(menu.TipsPanel, true);

    /* Warsmash sizes both the wrapper and backdrop from the active authored
     * panel. Keep the latest OpenRealm centering policy while updating those
     * dimensions for EndGame/ConfirmQuit instead of retaining MainPanel size. */
    if (active && active->Width > 0.0f && active->Height > 0.0f) {
        UI_SetSize(menu.EscMenuMainPanel, active->Width, active->Height);
        UI_SetSize(menu.EscMenuBackdrop, active->Width, active->Height);
    }
    UI_CenterFrame(menu.EscMenuMainPanel);
    UI_CenterFrame(menu.EscMenuBackdrop);
}

static void MenuWrite(LPEDICT ent, menuPanel_t panel) {
    if (!ent || !ent->client || !ent->client->connected) return;
    /* Decorated Esc-menu art is resolved while the FDF is first loaded, so
     * establish the recipient's race skin before entering the template cache. */
    UI_SetCurrentClient(ent->client);
    if (!MenuEnsureLoaded()) {
        UI_SetCurrentClient(NULL);
        return;
    }
    MenuSelectPanel(panel);
    UI_WriteWindow(ent, menu.EscMenuMainPanel, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_MENU, .class_id = BZ_WC3_WINDOW_MENU,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
    UI_SetCurrentClient(NULL);
}

void UI_ShowMainMenu(LPEDICT ent) {
    MenuWrite(ent, MENU_PANEL_MAIN);
}

void UI_ShowGameMenuEndGame(LPEDICT ent) {
    MenuWrite(ent, MENU_PANEL_END_GAME);
}

void UI_ShowGameMenuConfirmExit(LPEDICT ent) {
    MenuWrite(ent, MENU_PANEL_CONFIRM_QUIT);
}
