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
        static struct { cstring_t name; uint32_t stat; } const bindings[] = {
            { "ResourceLabel0", PLAYERSTATE_RESOURCE_GOLD },
            { "ResourceLabel1", PLAYERSTATE_RESOURCE_LUMBER },
        };
        FOR_LOOP(i, sizeof(bindings) / sizeof(*bindings)) {
            sc2BaseFrame_t *label=SC2_HUD_Find(root,bindings[i].name);
            if (label) label->stat=bindings[i].stat;
        }
        /* Liberty's active resource types are minerals and vespene. Energy and
         * life slots are unused here; preserve the stock collapsed anchor chain. */
        cstring_t hidden[]={"ResourceLabel2","ResourceLabel3","ResourceIcon2","ResourceIcon3","PlayerImage"};
        FOR_LOOP(i,sizeof(hidden)/sizeof(*hidden)) {
            sc2BaseFrame_t *f=SC2_HUD_Find(root,hidden[i]);
            if (f) { f->ui_flags|=SC2_UIFLAG_HIDDEN; f->size.width=0; }
        }
        return root;
    }
    return NULL;
}

static void write_one(sc2BaseFrame_t *f) {
    if (f) SC2_HUD_WriteFrame(f);
}

void SC2_HUD_WriteResourcePanel(edict_t *ent) {
    uint32_t count = 0;
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

    sc2BaseFrame_t *chain[]={sheet,ally,team,cash};
    FOR_LOOP(i,4) if (chain[i]) { chain[i]->ui_flags|=SC2_UIFLAG_HIDDEN; chain[i]->size.width=0; }
    static char supply[48];
    snprintf(supply,sizeof(supply),"%u / %u",ent->client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED],ent->client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP]);
    sc2BaseFrame_t *label=SC2_HUD_Find(res,"SupplyLabel");
    if (label) { label->stat=0; label->text=supply; }
    SC2_HUD_WriteStart(LAYER_CONSOLE);
    SC2_HUD_ReserveAncestors(frames,res);
    FOR_LOOP(i,4) if (chain[i]) SC2_HUD_ReserveTree(frames,count,chain[i]);
    SC2_HUD_ReserveTree(frames,count,res);

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
