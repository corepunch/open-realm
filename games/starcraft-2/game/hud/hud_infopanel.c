/*
 * hud_infopanel.c — SC2 info panel (selected unit name, wireframe, stats).
 *
 * Sends the InfoPanel subtree. Dynamic unit data (name, HP, shields) is
 * written by stamping .text / .stat on the relevant label frames before
 * calling SC2_HUD_WriteLayout.
 */

#include "hud.h"

static sc2BaseFrame_t *infopanel_root;
static BOOL infopanel_root_found;

static void infopanel_find_root(void) {
    if (infopanel_root_found) return;
    infopanel_root_found = true;
    infopanel_root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_INFO_PANEL);
}

void SC2_HUD_WriteInfoPanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;
    infopanel_find_root();
    if (!infopanel_root) return;
    SC2_HUD_WriteLayout(ent, frames, count, infopanel_root, LAYER_INFOPANEL);
}
