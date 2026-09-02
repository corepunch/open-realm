/* hud_menu.c — Server-authored in-game pause menu using Blizzard's Esc FDF. */

#include "hud_local.h"
#include "../generated/esc_menu_main_panel.h"

static EscMenuMainPanelGame_t menu;
static BOOL menu_loaded;

static BOOL MenuEnsureLoaded(void) {
    if (menu_loaded) return menu.EscMenuMainPanel != NULL;
    menu_loaded = true;
    if (!EscMenuMainPanelGame_Load(&menu)) return false;
    UI_SetHidden(menu.MainPanel, false);
    UI_SetHidden(menu.EndGamePanel, true);
    UI_SetHidden(menu.HelpPanel, true);
    UI_SetHidden(menu.TipsPanel, true);
    return true;
}

void UI_ShowMainMenu(LPEDICT ent) {
    if (!ent || !ent->client || !ent->client->connected || !MenuEnsureLoaded()) return;
    UI_SetText(menu.PauseButtonText, "Resume Game");
    UI_SetText(menu.ReturnButtonText, "Return to Game");
    UI_SetOnClick(menu.PauseButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(menu.ReturnButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_WriteWindow(ent, menu.EscMenuMainPanel, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_MENU, .class_id = BZ_WC3_WINDOW_MENU,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
}
