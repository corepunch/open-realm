#include "s_skills.h"

cstring_t const stone_form_orders[] = { "stoneform", "unstoneform", NULL };

static bool stone_form_types(uint32_t code, uint32_t *base, uint32_t *stone) {
    AbilityData_t const *ability = G_AbilityData(code);
    if (!ability || !ability->id || !ability->level[0].data[0].id || !ability->level[0].unitID) return false;
    *base = ability->level[0].data[0].id;
    *stone = ability->level[0].unitID;
    return true;
}

static void stone_form_animation_end(edict_t *unit) {
    if(!S_HoldMorphAnimation(unit))unit_stand(unit);
}
static umove_t stone_form_forward={"morph",NULL,stone_form_animation_end,CAbilityStoneForm};
static umove_t stone_form_reverse={"morph alternate",NULL,stone_form_animation_end,CAbilityStoneForm};
static umove_t avenger_form_forward={"morph",NULL,stone_form_animation_end,CAbilityMetamorphosis};

static bool flight_form_begin(edict_t *unit,uint32_t code,uint32_t base,uint32_t target,
                              bool reverse,umove_t *move,cstring_t order) {
    abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(unit,code));
    bool takeoff,landing;
    order_stop_cleanup(unit);
    if(!S_PrepareMorphFlight(unit,target,data,&takeoff,&landing))return false;
    unit_setmove(unit,move);
    unit->current_order_id=G_OrderId(order);
    S_BeginMorphTransition(unit,code,base,target,reverse,!takeoff,
        wc3_add(data->dur,landing ? data->data[2].number : 0));
    return true;
}

static bool stone_form_can_transform(edict_t const *unit, uint32_t code) {
    uint32_t base, stone;
    return unit && !(unit->morph && unit->morph->transition.active) && stone_form_types(code, &base, &stone) &&
           (unit->class_id == base || unit->class_id == stone);
}

static bool stone_form_execute(edict_t *unit, uint32_t code) {
    uint32_t base, stone;
    if (!unit || !stone_form_types(code, &base, &stone)) return false;
    bool reverse=unit->class_id==stone;
    return flight_form_begin(unit,code,base,reverse ? base : stone,reverse,
        reverse ? &stone_form_reverse : &stone_form_forward,stone_form_orders[reverse ? 1 : 0]);
}

BZ_ABILITY_PROC(CAbilityStoneForm) {
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_DISABLE:S_RemoveMorphAbility(ent,code);return true;
    case A_MORPH_COMPLETE:
        G_AddUnitAnimationProperties(ent,"alternate",!call->enabled);
        S_BeginMorphRise(ent);unit_stand(ent);return true;
    case A_COMMAND: {
        gameClient_t *client;
        uint32_t subgroup_class;
        bool executed = false;

        if (!ent || !call || !call->client || !(client = call->client->client)) return false;
        subgroup_class = ent->class_id;
        /* Capture the focused subgroup before casting: takeoff rebinds the
         * first member immediately while landing keeps its source type. */
        FOR_CONTROLLABLE_SELECTED_UNITS(client, unit) {
            if (unit->class_id != subgroup_class || !G_UnitAbilityClassLevel(unit, code)) continue;
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

/* Destroyer Form is a one-way conversion, retaining the AI conversion owner. */
BZ_ABILITY_PROC(CAbilityAvengerForm) {
    if(!call || !call->item)return false;
    uint32_t code=call->item->code;
    abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(ent,code));
    switch(msg) {
    case A_ORDER: {
        if(!ent || !call->order || strcmp(call->order,"avengerform"))return false;
        abilityAliasRef_t ref=S_ResolveAbilityAlias(ent,MAKEFOURCC('A','a','v','e'));
        return ref.alias && S_CastNoTargetSpell(ent,ref.alias);
    }
    case A_VALIDATE:
        return ent && data->unitID && ent->class_id!=data->unitID &&
            !(ent->morph && ent->morph->transition.active);
    case A_EXECUTE:
        return flight_form_begin(ent,code,ent->class_id,data->unitID,false,
            &avenger_form_forward,"avengerform");
    case A_MORPH_COMPLETE:S_BeginMorphRise(ent);unit_stand(ent);return true;
    case A_DISABLE:S_RemoveMorphAbility(ent,code);return true;
    default:return CAbilitySimpleSpell(ent,msg,call);
    }
}
