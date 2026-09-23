#include "s_skills.h"

LPCSTR const stone_form_orders[] = { "stoneform", "unstoneform", NULL };

/* Gargoyle Stone Form is authored as an alternate unit row: ugrm is the
 * immobile stone presentation and ugar is the ordinary flying unit.  Keep the
 * same edict/selection/script handle and let the shared transform path clear
 * AI_IMMOBILE, restore movement data, and rebuild the unit's authored combat. */
static BOOL stone_form_order(LPEDICT unit, LPCSTR order) {
    DWORD const stone = MAKEFOURCC('u', 'g', 'r', 'm');
    DWORD const gargoyle = MAKEFOURCC('u', 'g', 'a', 'r');
    DWORD target;

    if (!unit || !order) return false;
    if (!strcmp(order, "unstoneform")) {
        if (unit->class_id != stone) return false;
        target = gargoyle;
    } else if (!strcmp(order, "stoneform")) {
        if (unit->class_id != gargoyle) return false;
        target = stone;
    } else {
        return false;
    }
    if (!G_TransformUnitType(unit, target)) return false;
    unit->goalentity = NULL;
    unit->secondarygoal = NULL;
    move_reset_progress(unit);
    unit_stand(unit);
    return true;
}

BZ_ABILITY_PROC(CAbilityStoneForm) {
    if (msg != A_ORDER || !call || !call->order) return false;
    return stone_form_order(ent, call->order);
}
