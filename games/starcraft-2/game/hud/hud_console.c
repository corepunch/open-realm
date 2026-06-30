/*
 * hud_console.c — SC2 console panel (menu bar, chat bar, minimap frame).
 *
 * The ConsolePanel root frame anchors the entire bottom HUD chrome.
 * This file sends the static structure once per client per connect; it
 * does not carry dynamic data so no stat bindings are needed here.
 */

#include "hud.h"

static sc2BaseFrame_t *console_root;
static BOOL console_root_found;

static void console_find_root(void) {
    if (console_root_found) return;
    console_root_found = true;
    console_root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_CONSOLE_PANEL);
}

void SC2_HUD_WriteConsolePanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;
    console_find_root();
    if (!console_root) return;
    SC2_HUD_WriteLayout(ent, frames, count, console_root, LAYER_BACKGROUND);
}
