/*
 * hud_resource.c — SC2 resource panel (minerals, vespene, supply).
 *
 * Loads the SC2Layout frame tree once, binds dynamic stat indices to the
 * label frames, then writes a minimal ordered subtree each frame.
 * Mineral → PLAYERSTATE_RESOURCE_GOLD
 * Vespene → PLAYERSTATE_RESOURCE_LUMBER  (SC2 vespene maps to lumber slot)
 * Supply   → PLAYERSTATE_RESOURCE_FOOD_USED
 *
 * Wire order: parent ancestry of ResourcePanel, then the full right-edge
 * anchor chain (CharacterSheetButton → AllianceButton → TeamResourceButton
 * → CashPanel), then ResourcePanel + children.  All wire numbers stay well
 * below 255 so the 8-bit relativeTo field in uiFramePoint_t never overflows.
 */

#include "hud.h"

static sc2BaseFrame_t *resource_find(void) {
    sc2BaseFrame_t *root = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
    if (root) {
        /* ResourcePanel layout (right-to-left after SupplyLabel):
         *   ResourceLabel3 = primary mineral display  → GOLD
         *   ResourceLabel2 = high-yield mineral slot  → HERO_TOKENS (maps to 0)
         *   ResourceLabel1 = gas (vespene)            → LUMBER
         *   ResourceLabel0 = secondary mineral slot   → GOLD
         * ResourceLabel3 was previously unbound, causing "text N" fallback. */
        static struct { LPCSTR name; DWORD stat; } const bindings[] = {
            { "ResourceLabel0", PLAYERSTATE_RESOURCE_GOLD },
            { "ResourceLabel1", PLAYERSTATE_RESOURCE_LUMBER },
            { "ResourceLabel2", PLAYERSTATE_RESOURCE_HERO_TOKENS },
            { "ResourceLabel3", PLAYERSTATE_RESOURCE_GOLD },
            { "SupplyLabel", PLAYERSTATE_RESOURCE_FOOD_USED },
        };
        FOR_LOOP(i, sizeof(bindings) / sizeof(*bindings)) {
            sc2BaseFrame_t *label = SC2_LayoutFindFrameByName(bindings[i].name);
            if (!label) continue;
            label->stat = bindings[i].stat;
        }
        return root;
    }
    return SC2_HUD_FindFallbackFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
}

static void write_one(sc2BaseFrame_t *f) {
    if (f && !(f->ui_flags & SC2_UIFLAG_HIDDEN)) SC2_HUD_WriteFrame(f);
}

void SC2_HUD_WriteResourcePanel(LPEDICT ent) {
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_HUD_EnsureLayout(&count);
    if (!frames) return;

    sc2BaseFrame_t *res  = resource_find();
    if (!res) return;

    /* Anchor chain for CashPanel's right edge (from SC2Layout):
     *   CharacterSheetButton → AllianceButton → TeamResourceButton → CashPanel
     * All must be written BEFORE ResourcePanel so their wire numbers fit in uint8.
     * ResourcePanel.right → CashPanel.left: CashPanel must be in the wire set. */
    sc2BaseFrame_t *sheet = SC2_LayoutFindFrameByName("CharacterSheetButton");
    sc2BaseFrame_t *ally  = SC2_LayoutFindFrameByName("AllianceButton");
    sc2BaseFrame_t *team  = SC2_LayoutFindFrameByName("TeamResourceButton");
    sc2BaseFrame_t *cash  = SC2_LayoutFindFrameByName("CashPanel");

    SC2_HUD_WriteStart(LAYER_CONSOLE);

    /* 1. Ancestors of ResourcePanel (GameUI → UIContainer → FullscreenUpperContainer) */
    SC2_HUD_WriteAncestors(frames, count, res);

    /* 2. Anchor chain — left-to-right so each references an already-written predecessor */
    write_one(sheet);
    write_one(ally);
    write_one(team);
    write_one(cash);

    /* 3. ResourcePanel and all its children (labels, icons, supply frame) */
    SC2_HUD_WriteFrameWithChildren(frames, count, res);

    SC2_HUD_WriteEnd(ent);
}
