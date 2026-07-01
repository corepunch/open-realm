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

static sc2BaseFrame_t *resource_find(void) {
    sc2BaseFrame_t *root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
    if (root) {
        static struct { LPCSTR name; DWORD stat; } const bindings[] = {
            { "ResourceLabel0", PLAYERSTATE_RESOURCE_GOLD },
            { "ResourceLabel1", PLAYERSTATE_RESOURCE_LUMBER },
            { "ResourceLabel2", PLAYERSTATE_RESOURCE_HERO_TOKENS },
            { "SupplyLabel", PLAYERSTATE_RESOURCE_FOOD_USED },
        };
        FOR_LOOP(i, sizeof(bindings) / sizeof(*bindings)) {
            sc2BaseFrame_t *label = SC2_LayoutFindFrameByName(bindings[i].name);
            if (!label) {
                fprintf(stderr, "SC2_HUD: missing resource label '%s'\n", bindings[i].name);
                continue;
            }
            label->stat = bindings[i].stat;
        }
        return root;
    }
    return SC2_HUD_FindFallbackFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
}

void SC2_HUD_WriteResourcePanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;
    sc2BaseFrame_t *root = resource_find();
    if (!root) return;
    SC2_HUD_WriteLayout(ent, frames, count, root, LAYER_CONSOLE);
}
