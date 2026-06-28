/*
 * g_ui_stub.c — Empty stubs for game-UI functions whose callers remain
 * in skill files, g_main.c, and api headers. The real HUD is now drawn by
 * ui/screens/console_ui.c via FDF frames. Manual UIFRAME creation
 * is fully removed.
 */

#include "g_local.h"

/* Called by g_commands.c, g_main.c, skill files */
void Get_Commands_f(LPEDICT ent) { (void)ent; }
void Get_Portrait_f(LPEDICT ent) { (void)ent; }

/* Called by g_main.c per-frame (now no-op: console_ui.c draws client-side) */
void G_UpdateClientInfoPanels(void) {}
void G_UpdateClientResourceBars(void) {}
void G_RefreshResourceBar(LPEDICT ent) { (void)ent; }

/* Called by g_spawn.c */
void UI_Init(void) {}

/* Called by g_commands.c */
void UI_ShowQuests(LPEDICT ent) { (void)ent; }
void UI_HideQuests(LPEDICT ent) { (void)ent; }

/* Called by g_main.c, api_misc.h */
void UI_ShowGameInterface(LPEDICT ent) { (void)ent; }

/* Called by api_misc.h (Jass ShowInterface) */
void UI_ShowInterface(LPEDICT ent, BOOL flag, FLOAT duration) {
    (void)ent; (void)flag; (void)duration;
}

/* Called by api_misc.h (Jass SetCinematicScene) */
void UI_WriteCinematicLayer(LPEDICT ent) { (void)ent; }

/* Called by api_player.h (Jass DisplayTextToPlayer) */
void UI_ShowText(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration) {
    (void)ent; (void)pos; (void)text; (void)duration;
}

/* Called by api_cinefilter.h (Jass CreateCineFilter) */
DWORD UI_LoadTexture(LPCSTR path, BOOL mipmap) {
    (void)path; (void)mipmap;
    return 0;
}

/* Called by skill files (16 active sites) */
void UI_AddCancelButton(LPEDICT ent) { (void)ent; }
void UI_AddCommandButton(LPCSTR code) { (void)code; }
void UI_AddCommandButtonExtended(LPCSTR code, BOOL research, DWORD level) {
    (void)code; (void)research; (void)level;
}

/* Called by g_commands.c */
void UI_ShowQuest(LPEDICT ent, LPCQUEST quest) { (void)ent; (void)quest; }

/* Called by g_spawn.c, g_unit_ui.c */
LPCSTR Theme_String(LPCSTR key, LPCSTR def) { return def; }
FLOAT Theme_Float(LPCSTR key, LPCSTR def) { return def ? (FLOAT)atof(def) : 0.0f; }
