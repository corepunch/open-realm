#include "s_skills.h"

static BOOL ShowTrainedUnit(LPEDICT townhall, LPEDICT unit) {
    VECTOR2 origin;
    FLOAT angle;

    if (!SP_FindUnitExitPosition(townhall, unit, &origin, &angle)) {
        return false;
    }
    unit->s.origin2 = origin;
    unit->s.angle = angle;
    unit->s.renderfx &= ~RF_HIDDEN;
    unit->stand(unit);
    return true;
}

void ai_train_build(LPEDICT ent) {
    FLOAT const k = (FLOAT)FRAMETIME / ((FLOAT)ent->build->UnitBalance->buildTime * 1000.0f);
    EDICTSTAT *hp = &ent->build->health;
    hp->value += hp->max_value * k;
    if (hp->value >= hp->max_value) {
        LPEDICT clent = G_GetPlayerEntityByNumber(ent->s.player);

        hp->value = hp->max_value; /* clamp; placement retries every tick until space clears */
        if (!ShowTrainedUnit(ent, ent->build)) {
            return;
        }
        G_PublishEvent(ent->build, EVENT_PLAYER_UNIT_TRAIN_FINISH);
        if (!(ent->build = ent->build->build)) {
            ent->stand(ent);
        }
        Get_Portrait_f(clent);
    }
}

static umove_t train_move_train = { "stand", ai_train_build, NULL, &a_train };

void unit_add_build_queue(LPEDICT self, LPEDICT item) {
    if (!self->build) {
        self->build = item;
    } else {
        LPEDICT last = self->build;
        while (last->build) last = last->build;
        last->build = item;
    }
}

void unit_build(LPEDICT self, DWORD class_id) {
    LPEDICT ent = SP_SpawnAtLocation(class_id, self->s.player, &self->s.origin2);
    ent->health.value = 0;
    ent->birth(ent);
    ent->s.renderfx |= RF_HIDDEN;
    unit_add_build_queue(self, ent);
    unit_setmove(self, &train_move_train);
}

void SP_TrainUnit(LPEDICT townhall, DWORD class_id) {
    LPEDICT clent = g_edicts+townhall->s.player;
    LPPLAYER player = G_GetPlayerByNumber(townhall->s.player);
    if (player_pay(player, class_id)) {
        unit_build(townhall, class_id);
        Get_Portrait_f(clent);
        Get_Commands_f(clent);
    } else {
        fprintf(stdout, "Not enough resources\n");
    }
}

ability_t a_train = {
    
};
