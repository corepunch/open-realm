#include "s_skills.h"

cstring_t const stone_form_orders[] = { "stoneform", "unstoneform", NULL };

static bool stone_form_types(uint32_t code, uint32_t *base, uint32_t *stone) {
    AbilityData_t const *ability = G_AbilityData(code);
    if (!ability || !ability->id || !ability->level[0].data[0].id || !ability->level[0].unitID) return false;
    *base = ability->level[0].data[0].id;
    *stone = ability->level[0].unitID;
    return true;
}

static bool stone_form_order(LPEDICT unit, cstring_t order, uint32_t code) {
    uint32_t base, stone, target;
    if (!unit || !order || !stone_form_types(code, &base, &stone)) return false;
    if (!strcmp(order, "unstoneform")) {
        if (unit->class_id != stone) return false;
        target = base;
    } else if (!strcmp(order, "stoneform")) {
        if (unit->class_id != base) return false;
        target = stone;
    } else return false;
    if (!G_TransformUnitType(unit, target)) return false;
    unit->goalentity = NULL;
    unit->secondarygoal = NULL;
    move_reset_progress(unit);
    unit_stand(unit);
    return true;
}

static bool stone_form_can_transform(LPCEDICT unit, uint32_t code) {
    uint32_t base, stone;
    return unit && stone_form_types(code, &base, &stone) &&
           (unit->class_id == base || unit->class_id == stone);
}

static bool stone_form_execute(LPEDICT unit, uint32_t code) {
    uint32_t base, stone;
    if (!unit || !stone_form_types(code, &base, &stone)) return false;
    return stone_form_order(unit, unit->class_id == base ? "stoneform" : "unstoneform", code);
}

BZ_ABILITY_PROC(CAbilityStoneForm) {
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_ORDER:
        /* Immediate orders use shared cast validation, preserving ownership and cooldown checks. */
        return false;
    case A_VALIDATE:
        return stone_form_can_transform(ent, code);
    case A_EXECUTE:
        return call && call->item && stone_form_execute(ent, code);
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}
