/*
 * hud_infopanel.c — SC2 info panel (selected unit name, wireframe, stats).
 *
 * Sends the InfoPanel subtree. Dynamic unit data (name, HP, shields) is
 * written by stamping .text / .stat on the relevant label frames before
 * calling SC2_HUD_WriteLayout.
 */

#include "hud.h"

static BOOL infopanel_loaded;
static sc2BaseFrame_t *infopanel_root;

static void infopanel_ensure_loaded(void) {
    if (infopanel_loaded) return;
    infopanel_loaded = true;

    SC2_LayoutInit();
    if (!SC2_LayoutBuildGameUI()) return;
    infopanel_root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_INFO_PANEL);
}

void SC2_HUD_WriteInfoPanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames;

    infopanel_ensure_loaded();
    frames = SC2_LayoutGetFrames(&count);
    if (!frames || !infopanel_root) return;
    SC2_HUD_WriteLayout(ent, frames, count, infopanel_root, LAYER_INFOPANEL);
}
