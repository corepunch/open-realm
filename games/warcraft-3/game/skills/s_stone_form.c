#include "s_skills.h"

cstring_t const stone_form_orders[] = { "stoneform", "unstoneform", NULL };

static bool stone_form_types(uint32_t code, uint32_t *base, uint32_t *stone) {
    AbilityData_t const *ability = G_AbilityData(code);
    if (!ability || !ability->id || !ability->level[0].data[0].id || !ability->level[0].unitID) return false;
    *base = ability->level[0].data[0].id;
    *stone = ability->level[0].unitID;
    return true;
}

static bool stone_form_order(edict_t *unit, cstring_t order, uint32_t code) {
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
    /* Gargoyle Stone Form keeps the Gargoyle model and selects its authored
     * Alternate animation set for the statue presentation.  The alternate
     * unit type supplies gameplay stats; the model tag supplies the visible
     * stone form, matching Warcraft's Required Animation Names behavior. */
    G_AddUnitAnimationProperties(unit, "alternate", target == stone);
    S_SetMoveGoal(unit, &unit->goalentity, NULL);
    S_SetMoveGoal(unit, &unit->secondarygoal, NULL);
    move_reset_progress(unit);
    unit_stand(unit);
    return true;
}

static bool stone_form_can_transform(edict_t const *unit, uint32_t code) {
    uint32_t base, stone;
    return unit && stone_form_types(code, &base, &stone) &&
           (unit->class_id == base || unit->class_id == stone);
}

static bool stone_form_execute(edict_t *unit, uint32_t code) {
    uint32_t base, stone;
    if (!unit || !stone_form_types(code, &base, &stone)) return false;
    return stone_form_order(unit, unit->class_id == base ? "stoneform" : "unstoneform", code);
}

BZ_ABILITY_PROC(CAbilityStoneForm) {
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_COMMAND: {
        gameClient_t *client;
        uint32_t subgroup_class;
        bool executed = false;

        if (!ent || !call || !call->client || !(client = call->client->client)) return false;
        subgroup_class = ent->class_id;
        /* Stone Form is an immediate subgroup command in Warcraft. Capture the
         * focused type before transforming the first Gargoyle because the same
         * edict changes class_id during G_TransformUnitType(). */
        FOR_CONTROLLABLE_SELECTED_UNITS(client, unit) {
            if (unit->class_id != subgroup_class || !G_UnitAbilityLevel(unit, code)) continue;
            if (S_CastNoTargetSpell(unit, code)) executed = true;
        }
        if (executed) Get_Commands_f(call->client);
        return executed;
    }
    case A_ORDER: {
        abilityAliasRef_t ref;
        uint32_t base, stone;
        if (!ent || !call || !call->order) return false;
        ref = S_ResolveAbilityAlias(ent, MAKEFOURCC('A','s','t','n'));
        if (!ref.alias || !stone_form_types(ref.alias, &base, &stone)) return false;
        if (!strcmp(call->order, "stoneform")) {
            if (ent->class_id != base) return false;
        } else if (!strcmp(call->order, "unstoneform")) {
            if (ent->class_id != stone) return false;
        } else return false;
        /* Registered immediate owners also own rejection. Validate direction
         * before the shared cast commits mana, cooldown and order cleanup. */
        return S_CastNoTargetSpell(ent, ref.alias);
    }
    case A_VALIDATE:
        return stone_form_can_transform(ent, code);
    case A_EXECUTE:
        return call && call->item && stone_form_execute(ent, code);
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}
