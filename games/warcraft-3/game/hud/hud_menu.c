/* hud_menu.c — Server-authored in-game pause menu using Blizzard's Esc FDF. */

#include "hud_local.h"
#include "../generated/esc_menu_main_panel.h"

static EscMenuMainPanelGame_t menu;
static BOOL menu_loaded;

static BOOL MenuEnsureLoaded(void) {
    if (menu_loaded) return menu.EscMenuMainPanel != NULL;
    menu_loaded = true;
    if (!EscMenuMainPanelGame_Load(&menu)) return false;
    if (!menu.EscMenuBackdrop) {
        fprintf(stderr, "WC3 menu: missing EscMenuBackdrop\n");
        return false;
    }
    /* Blizzard sizes the all-points controller from the active panel; leaving
     * it full-screen made its TOP/BOTTOM content anchors span the canvas. */
    UI_SetSize(menu.EscMenuMainPanel, menu.MainPanel->Width, menu.MainPanel->Height);
    UI_CenterFrame(menu.EscMenuMainPanel);
    UI_SetParent(menu.EscMenuBackdrop, menu.EscMenuMainPanel);
    /* The separate backdrop root loads after MainPanel; nesting controls wire
     * order so decoration draws first while explicit anchors still target the controller. */
    UI_SetParent(menu.MainPanel, menu.EscMenuBackdrop);
    UI_CenterFrame(menu.EscMenuBackdrop);
    UI_SetHidden(menu.MainPanel, false);
    UI_SetHidden(menu.EndGamePanel, true);
    UI_SetHidden(menu.ConfirmQuitPanel, true);
    UI_SetHidden(menu.HelpPanel, true);
    UI_SetHidden(menu.TipsPanel, true);
    return true;
}

void UI_ShowMainMenu(LPEDICT ent) {
    if (!ent || !ent->client || !ent->client->connected) return;
    /* Decorated Esc-menu art is resolved while the FDF is first loaded, so
     * establish the recipient's race skin before entering the template cache. */
    UI_SetCurrentClient(ent->client);
    if (!MenuEnsureLoaded()) { UI_SetCurrentClient(NULL); return; }
    UI_SetText(menu.PauseButtonText, "Resume Game");
    UI_SetText(menu.ReturnButtonText, "Return to Game");
    UI_SetOnClick(menu.PauseButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_SetOnClick(menu.ReturnButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_WriteWindow(ent, menu.EscMenuMainPanel, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_MENU, .class_id = BZ_WC3_WINDOW_MENU,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
    UI_SetCurrentClient(NULL);
}
