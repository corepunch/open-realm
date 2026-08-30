#include "s_skills.h"

static FLOAT repair_cost_factor;
static FLOAT power_build_cost_ratio;
static FLOAT power_build_time_ratio;

void repair_build(LPEDICT ent, LPEDICT building);
void repair_build_primary(LPEDICT ent, LPEDICT building);

static BOOL repair_primary_active(LPEDICT building) {
    LPEDICT worker;
    if (!building) return false;
    worker = building->construction.primary_builder;
    return worker && worker->inuse && !(worker->svflags & SVF_DEADMONSTER) && worker->build == building &&
           worker->currentmove && worker->currentmove->ability == &a_repair;
}

static BOOL repair_charge_power_cost(LPEDICT ent, LPEDICT building) {
    LPGAMECLIENT client;
    UnitBalance_t const *b;
    FLOAT seconds;
    LONG gold_due, lumber_due;

    if (!ent || !building || ent->buildwork.primary || G_BuildAllEnabled()) return true;
    client = G_GetPlayerClientByNumber(ent->s.player);
    b = building->UnitBalance;
    if (!client || b->buildTime <= 0 || power_build_cost_ratio <= 0.0f) return true;

    seconds = (FLOAT)FRAMETIME / 1000.0f;
    ent->buildwork.gold_accum += ((FLOAT)b->goldRep / (FLOAT)b->buildTime) * power_build_cost_ratio * seconds;
    ent->buildwork.lumber_accum += ((FLOAT)b->lumberRep / (FLOAT)b->buildTime) * power_build_cost_ratio * seconds;
    gold_due = (LONG)floorf(ent->buildwork.gold_accum);
    lumber_due = (LONG)floorf(ent->buildwork.lumber_accum);
    if (gold_due > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] ||
        lumber_due > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]) {
        return false;
    }
    if (gold_due > 0) {
        client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] -= gold_due;
        ent->buildwork.gold_accum -= gold_due;
    }
    if (lumber_due > 0) {
        client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] -= lumber_due;
        ent->buildwork.lumber_accum -= lumber_due;
    }
    if (gold_due || lumber_due) G_RefreshResourceBar(G_GetPlayerEntityByNumber(ent->s.player));
    return true;
}

static void ai_repair(LPEDICT ent) {
    LPEDICT building = ent ? ent->build : NULL;
    EDICTSTAT *hp;

    if (!building || !building->inuse) {
        if (ent) ent->stand(ent);
        return;
    }
    hp = &building->health;

    if (building->construction.active) {
        FLOAT ratio;
        FLOAT duration;
        FLOAT start_hp;
        FLOAT fraction;

        if (ent->buildwork.primary) {
            if (building->construction.primary_builder != ent) {
                ent->stand(ent);
                return;
            }
            ratio = 1.0f;
        } else {
            if (power_build_time_ratio <= 0.0f) {
                ent->stand(ent);
                return;
            }
            ratio = power_build_time_ratio;
        }
        if (!repair_charge_power_cost(ent, building)) {
            ent->stand(ent);
            return;
        }

        duration = MAX(1.0f, (FLOAT)building->UnitBalance->buildTime * 1000.0f);
        building->construction.progress += (FLOAT)FRAMETIME * ratio;
        fraction = MIN(1.0f, building->construction.progress / duration);
        start_hp = MAX(1.0f, hp->max_value * 0.10f);
        hp->value = start_hp + (hp->max_value - start_hp) * fraction;
        if (fraction >= 1.0f) {
            G_CompleteConstruction(building);
            ent->stand(ent);
        }
        return;
    }

    /* Completed-structure repair keeps the existing simple rate until the
     * normal Repair DataA/DataB resource/time model is implemented. */
    if (building->UnitBalance->buildTime <= 0) {
        ent->stand(ent);
        return;
    }
    hp->value += hp->max_value * (FLOAT)FRAMETIME /
                 ((FLOAT)building->UnitBalance->buildTime * 1000.0f);
    if (hp->value >= hp->max_value) {
        hp->value = hp->max_value;
        building->stand(building);
        ent->stand(ent);
    }
}

static umove_t repair_move_build = { "stand work", ai_repair, NULL, &a_repair };

static BOOL repair_begin(LPEDICT ent, LPEDICT building, BOOL primary) {
    VECTOR2 origin;
    FLOAT angle;

    if (!ent || !building) return false;
    /* Human builders must leave the building's authored pathing footprint, not
     * merely its selection/collision circle. The static footprint is baked by
     * build_build() before this search. */
    if (!SP_FindUnitExitPosition(building, ent, &origin, &angle)) {
        if (primary && building->construction.primary_builder == ent) {
            building->construction.primary_builder = NULL;
        }
        if (ent->stand) ent->stand(ent);
        return false;
    }
    unit_setmove(ent, &repair_move_build);
    ent->s.origin2 = origin;
    ent->s.angle = angle - M_PI;
    gi.LinkEntity(ent);
    ent->build = building;
    ent->buildwork.primary = primary;
    ent->buildwork.gold_accum = 0.0f;
    ent->buildwork.lumber_accum = 0.0f;
    if (primary) building->construction.primary_builder = ent;
    return true;
}

void repair_build_primary(LPEDICT ent, LPEDICT building) {
    repair_begin(ent, building, true);
}

void repair_build(LPEDICT ent, LPEDICT building) {
    BOOL primary = false;
    if (building && building->construction.active) {
        if (!repair_primary_active(building)) {
            building->construction.primary_builder = NULL;
            primary = true;
        }
    }
    repair_begin(ent, building, primary);
}


BOOL G_UnitHasHumanRepair(LPEDICT ent) {
    LPCSTR abilities;

    if (!ent || !ent->UnitAbilities) return false;
    abilities = ent->UnitAbilities->abilList;
    if (!abilities) return false;
    PARSE_LIST(abilities, ability_name, parse_segment) {
        if (FindAbilityForCommand(ability_name) == &a_repair) return true;
    }
    return false;
}

static BOOL repair_selecttarget(LPEDICT clent, LPEDICT target) {
    if (!target || !G_UnitIsBuilding(target->class_id)) {
        return false;
    }
    if (target->s.player != clent->client->ps.number) {
        return false;
    }
    if (target->health.value >= target->health.max_value && !target->construction.active) {
        return false;
    }
    if (target->construction.active && power_build_time_ratio <= 0.0f && repair_primary_active(target)) {
        return false;
    }
    FOR_SELECTED_UNITS(clent->client, ent) {
        if (G_UnitHasHumanRepair(ent)) repair_build(ent, target);
    }
    return true;
}

static void repair_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = repair_selecttarget;
}

void SP_ability_repair(LPCSTR classname, ability_t *self) {
    AbilityData_t const *data = G_AbilityDataName(classname);
    (void)self;
    repair_cost_factor = data->data[0][0];
    power_build_cost_ratio = data->data[0][2];
    power_build_time_ratio = data->data[0][3];
}

ability_t a_repair = {
    .init = SP_ability_repair,
    .cmd = repair_command,
};
