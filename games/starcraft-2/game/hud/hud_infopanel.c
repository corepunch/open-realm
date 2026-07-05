/*
 * hud_infopanel.c — SC2 info panel (selected unit name, wireframe, stats).
 *
 * Sends the InfoPanel subtree. Dynamic unit data (name, HP, shields) is
 * written by stamping .text / .stat on the relevant label frames before
 * calling SC2_HUD_WriteLayout.
 */

#include "hud.h"

static sc2BaseFrame_t *infopanel_find(void) {
    sc2BaseFrame_t *r = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_INFO_PANEL);
    if (r) return r;
    return SC2_HUD_FindFallbackFrameByType(SC2_FRAMETYPE_INFO_PANEL);
}

void SC2_HUD_WriteInfoPanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;
    sc2BaseFrame_t *root = infopanel_find();
    if (!root) return;
    SC2_HUD_WriteLayout(ent, frames, count, root, LAYER_INFOPANEL);
}
