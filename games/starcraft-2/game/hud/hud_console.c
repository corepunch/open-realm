/*
 * hud_console.c — SC2 console panel (menu bar, chat bar, minimap frame).
 *
 * The ConsolePanel root frame anchors the entire bottom HUD chrome.
 * This file sends the static structure once per client per connect; it
 * does not carry dynamic data so no stat bindings are needed here.
 */

#include "hud.h"

static BOOL console_loaded;
static sc2BaseFrame_t *console_root;

static void console_ensure_loaded(void) {
    if (console_loaded) return;
    console_loaded = true;

    SC2_LayoutInit();
    if (!SC2_LayoutBuildGameUI()) return;
    console_root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_CONSOLE_PANEL);
}

void SC2_HUD_WriteConsolePanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames;

    console_ensure_loaded();
    frames = SC2_LayoutGetFrames(&count);
    if (!frames || !console_root) return;
    SC2_HUD_WriteLayout(ent, frames, count, console_root, LAYER_BACKGROUND);
}
