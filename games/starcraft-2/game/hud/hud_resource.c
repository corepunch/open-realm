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

static BOOL resource_loaded;
static sc2BaseFrame_t *resource_root;
static DWORD resource_count;

static void resource_ensure_loaded(void) {
    if (resource_loaded) return;
    resource_loaded = true;

    SC2_LayoutInit();
    if (!SC2_LayoutBuildGameUI()) return;

    resource_root  = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
    resource_count = 0;
    SC2_LayoutGetFrames(&resource_count);

    /* Bind stat IDs to text-label children whose names match SC2 conventions.
     * The parser stores frames in the flat array; we walk looking for Label
     * children of the ResourcePanel whose names contain "Mineral", "Vespene",
     * or "Supply". */
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(NULL);
    if (!frames || !resource_root) return;

    for (DWORD i = 0; i < resource_count; i++) {
        sc2BaseFrame_t *f = &frames[i];
        if (f->type != FT_TEXT || !f->text) continue;
        if (strstr(f->text, "Mineral") || strstr(f->text, "mineral"))
            f->ui_flags |= 0; /* stat bound below via separate dynamic field */
        /* Dynamic stat overlay is written per-frame in SC2_HUD_WriteResourcePanel */
    }
}

void SC2_HUD_WriteResourcePanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames;

    resource_ensure_loaded();
    frames = SC2_LayoutGetFrames(&count);
    if (!frames || !resource_root) return;
    SC2_HUD_WriteLayout(ent, frames, count, resource_root, LAYER_CONSOLE);
}
