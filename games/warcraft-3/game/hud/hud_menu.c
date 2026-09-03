/* hud_menu.c — Server-authored in-game pause menu using Blizzard's Esc FDF. */

#include "hud_local.h"

typedef enum {
    MENU_PANEL_MAIN,
    MENU_PANEL_END_GAME,
    MENU_PANEL_CONFIRM_QUIT,
} menuPanel_t;

static LPFRAMEDEF MenuPanel(menuPanel_t panel) {
    switch (panel) {
        case MENU_PANEL_END_GAME: return hud.menu.EndGamePanel;
        case MENU_PANEL_CONFIRM_QUIT: return hud.menu.ConfirmQuitPanel;
        case MENU_PANEL_MAIN:
        default: return hud.menu.MainPanel;
    }
}

void UI_LoadHudMenu(void) {
    if (hud.menu.EscMenuBackdrop) return;
    if (!EscMenuMainPanelGame_Load(&hud.menu)) return;
    if (!hud.menu.EscMenuBackdrop) {
        fprintf(stderr, "WC3 menu: missing EscMenuBackdrop\n");
        return;
    }

    UI_SetParent(hud.menu.EscMenuBackdrop, hud.menu.EscMenuMainPanel);
    /* The separate backdrop root loads after the panels. Nest all Esc-menu
     * panel subtrees below it so decoration serializes/draws before controls;
     * explicit anchors still target the bounded controller. */
    UI_SetParent(hud.menu.MainPanel, hud.menu.EscMenuBackdrop);
    UI_SetParent(hud.menu.EndGamePanel, hud.menu.EscMenuBackdrop);
    UI_SetParent(hud.menu.ConfirmQuitPanel, hud.menu.EscMenuBackdrop);
    UI_SetParent(hud.menu.HelpPanel, hud.menu.EscMenuBackdrop);
    UI_SetParent(hud.menu.TipsPanel, hud.menu.EscMenuBackdrop);

    /* Current Warsmash leaves these authored controls present but disabled.
     * OpenRealm's newer pause lifecycle intentionally reuses PauseButton as a
     * second Resume action, so only features that remain unimplemented stay
     * disabled here. */
    UI_SetEnabled(hud.menu.SaveGameButton, false);
    UI_SetEnabled(hud.menu.LoadGameButton, false);
    UI_SetEnabled(hud.menu.OptionsButton, false);
    UI_SetEnabled(hud.menu.HelpButton, false);
    UI_SetEnabled(hud.menu.TipsButton, false);
    UI_SetEnabled(hud.menu.RestartButton, false);

    UI_SetText(hud.menu.PauseButtonText, "Resume Game");
    UI_SetText(hud.menu.ReturnButtonText, "Return to Game");
    UI_SetOnClick(hud.menu.PauseButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(hud.menu.ReturnButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(hud.menu.EndGameButton, "menu_endgame");
    UI_SetOnClick(hud.menu.PreviousButton, "menu");
    UI_SetOnClick(hud.menu.QuitButton, UI_WINDOW_DISCONNECT_ACTION);
    UI_SetOnClick(hud.menu.ExitButton, "menu_confirm_exit");
    UI_SetOnClick(hud.menu.ConfirmQuitCancelButton, "menu_endgame");
    UI_SetOnClick(hud.menu.ConfirmQuitQuitButton, UI_WINDOW_QUIT_ACTION);
}

static void MenuSelectPanel(menuPanel_t panel) {
    LPFRAMEDEF active = MenuPanel(panel);

    UI_SetHidden(hud.menu.EscMenuMainPanel, false);
    UI_SetHidden(hud.menu.EscMenuBackdrop, false);
    UI_SetHidden(hud.menu.MainPanel, panel != MENU_PANEL_MAIN);
    UI_SetHidden(hud.menu.EndGamePanel, panel != MENU_PANEL_END_GAME);
    UI_SetHidden(hud.menu.ConfirmQuitPanel, panel != MENU_PANEL_CONFIRM_QUIT);
    UI_SetHidden(hud.menu.HelpPanel, true);
    UI_SetHidden(hud.menu.TipsPanel, true);

    /* Warsmash sizes both the wrapper and backdrop from the active authored
     * panel. Keep the latest OpenRealm centering policy while updating those
     * dimensions for EndGame/ConfirmQuit instead of retaining MainPanel size. */
    if (active && active->Width > 0.0f && active->Height > 0.0f) {
        UI_SetSize(hud.menu.EscMenuMainPanel, active->Width, active->Height);
        UI_SetSize(hud.menu.EscMenuBackdrop, active->Width, active->Height);
    }
    UI_CenterFrame(hud.menu.EscMenuMainPanel);
    UI_CenterFrame(hud.menu.EscMenuBackdrop);
}

static void MenuWrite(LPEDICT ent, menuPanel_t panel) {
    if (!ent || !ent->client || !ent->client->connected) return;
    /* Decorated Esc-menu art is resolved while the FDF is first loaded, so
     * establish the recipient's race skin before entering the template cache. */
    UI_SetCurrentClient(ent->client);
    if (!hud.menu.EscMenuBackdrop) {
        UI_SetCurrentClient(NULL);
        return;
    }
    MenuSelectPanel(panel);
    UI_WriteWindow(ent, hud.menu.EscMenuMainPanel, &MAKE(uiWindowDef_t,
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
