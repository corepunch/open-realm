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

/* The parsed ResourcePanel has a cross-panel x-anchor relative to CashPanel
 * (which we don't render). Override with a simple right-edge anchor so it
 * appears in the top-right corner regardless of absent sibling panels.
 * The -16px/+4px offsets match what SC2 displays when all panels are present. */
#define RES_OFFSET_X  (-16.0f / 1600.0f * 0.8f)
#define RES_OFFSET_Y  (  4.0f / 1200.0f * 0.6f)

static void resource_fix_anchor(sc2BaseFrame_t *root) {
    memset(&root->points.x, 0, sizeof(root->points.x));
    root->points.x[FPP_MAX].used = 1;
    root->points.x[FPP_MAX].targetPos = FPP_MAX;
    root->points.x[FPP_MAX].relative_index = (DWORD)-1; /* parent (scene) */
    root->points.x[FPP_MAX].offset = RES_OFFSET_X;

    memset(&root->points.y, 0, sizeof(root->points.y));
    root->points.y[FPP_MIN].used = 1;
    root->points.y[FPP_MIN].targetPos = FPP_MIN;
    root->points.y[FPP_MIN].relative_index = (DWORD)-1; /* parent (scene) */
    root->points.y[FPP_MIN].offset = RES_OFFSET_Y;
}

static sc2BaseFrame_t *resource_find(void) {
    sc2BaseFrame_t *root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
    if (root) {
        resource_fix_anchor(root);
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
