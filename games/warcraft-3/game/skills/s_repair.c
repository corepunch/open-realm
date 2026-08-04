#include "s_skills.h"

static FLOAT repair_cost_factor;

void repair_build(LPEDICT ent, LPEDICT building);

static void ai_repair(LPEDICT ent) {
    FLOAT const k = (FLOAT)FRAMETIME / (FLOAT)UNIT_BUILD_TIME_MSEC(ent->build->class_id);
    EDICTSTAT *hp = &ent->build->health;
    hp->value += hp->max_value * k;
    if (hp->value >= hp->max_value) {
        hp->value = hp->max_value;
        ent->build->stand(ent->build);
        ent->stand(ent);
    }
}

static umove_t repair_move_build = { "stand work", ai_repair, NULL, &a_repair };

void repair_build(LPEDICT ent, LPEDICT building) {
    VECTOR2 origin;
    FLOAT angle;
    unit_setmove(ent, &repair_move_build);
    SP_FindEmptySpaceAround(building, ent->class_id, &origin, &angle);
    ent->s.origin2 = origin;
    ent->s.angle = angle - M_PI;
    ent->build = building;
}

static BOOL repair_selecttarget(LPEDICT clent, LPEDICT target) {
    if (!target || !UNIT_IS_BUILDING(target->class_id)) {
        return false;
    }
    if (target->s.player != clent->client->ps.number) {
        return false;
    }
    if (target->health.value >= target->health.max_value) {
        return false;
    }
    FOR_SELECTED_UNITS(clent->client, ent) {
        repair_build(ent, target);
    }
    return true;
}

static void repair_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = repair_selecttarget;
}

void SP_ability_repair(LPCSTR classname, ability_t *self) {
    repair_cost_factor = AB_Number(classname, "DataA1");
}

ability_t a_repair = {
    .init = SP_ability_repair,
    .cmd = repair_command,
};
