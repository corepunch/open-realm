/*
 * hud_resource.c — SC2 resource panel (minerals, vespene, supply).
 *
 * Loads the SC2Layout frame tree once, binds dynamic stat indices to the
 * label frames, then writes the full subtree each frame.
 * Mineral → PLAYERSTATE_RESOURCE_GOLD
 * Vespene → PLAYERSTATE_RESOURCE_LUMBER  (SC2 vespene maps to lumber slot)
 * Supply   → PLAYERSTATE_RESOURCE_FOOD_USED
 */

#include "hud.h"

static sc2BaseFrame_t *resource_root;
static BOOL resource_root_found;

static void resource_find_root(void) {
    if (resource_root_found) return;
    resource_root_found = true;
    resource_root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
}

void SC2_HUD_WriteResourcePanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;
    resource_find_root();
    if (!resource_root) return;
    SC2_HUD_WriteLayout(ent, frames, count, resource_root, LAYER_CONSOLE);
}
