#include "s_skills.h"

static BOOL ShowTrainedUnit(LPEDICT townhall, LPEDICT unit) {
    VECTOR2 origin;
    FLOAT angle;

    if (!SP_FindUnitExitPosition(townhall, unit, &origin, &angle)) {
        return false;
    }
    unit->s.origin2 = origin;
    unit->s.angle = angle;
    unit->training = false;
    unit->s.renderfx &= ~RF_HIDDEN;
    /* Food Used was already reserved on this queue entity. Completion only
     * activates Food Made; it must not charge Food Used a second time. */
    G_SetUnitFoodMade(unit, unit->UnitBalance->foodMade);
    unit->stand(unit);
    return true;
}

void ai_train_build(LPEDICT ent) {
    FLOAT k;
    EDICTSTAT *hp;

    if (!ent || !ent->build) {
        if (ent && ent->stand) ent->stand(ent);
        return;
    }
    /* Only the active queue head owns a food reservation. Later queue entries
     * stay at food.used == 0 until they advance to the front. If supply falls
     * before reservation succeeds, production waits and retries next tick. */
    if (!G_ReserveTrainingFood(ent->build)) {
        return;
    }
    k = (FLOAT)FRAMETIME / ((FLOAT)ent->build->UnitBalance->buildTime * 1000.0f);
    hp = &ent->build->health;
    hp->value += hp->max_value * k;
    if (hp->value >= hp->max_value) {
        LPEDICT clent = G_GetPlayerEntityByNumber(ent->s.player);
        LPEDICT completed = ent->build;
        LPEDICT next = completed->build;

        hp->value = hp->max_value; /* clamp; placement retries every tick until space clears */
        if (!ShowTrainedUnit(ent, completed)) {
            return;
        }
        /* Queued units use build as the next-item link, while unit_stand()
         * clears build for the completed unit. Preserve the producer's queue
         * link before revealing/standing the completed unit. */
        ent->build = next;
        G_InvalidateCommands(G_GetPlayerClientByNumber(ent->s.player));
        G_PublishEvent(completed, EVENT_PLAYER_UNIT_TRAIN_FINISH);
    #ifdef WC3_DEBUG_AI
        fprintf(stderr, "WC3_DEBUG_AI training complete producer=%ld unit=%ld id=%.4s player=%u\n",
            (long)(ent - g_edicts), (long)(completed - g_edicts), (LPCSTR)&completed->class_id, completed->s.player);
    #endif
        if (!ent->build) {
            ent->stand(ent);
        }
        Get_Portrait_f(clent);
    }
}

static umove_t train_move_train = { "stand", ai_train_build, NULL, &a_train };

void unit_add_build_queue(LPEDICT self, LPEDICT item) {
    /* Queued units must not run stand/birth callbacks, which clear build and used to sever the queue behind them. */
    item->currentmove = NULL;
    item->animation = NULL;
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
    ent->training = true;
    ent->health.value = 0;
    /* SP_SpawnAtLocation already ran birth; calling it twice reset the trained unit and crashed sparse fixtures. */
    ent->s.renderfx |= RF_HIDDEN;
    unit_add_build_queue(self, ent);
    unit_setmove(self, &train_move_train);
}

BOOL SP_TrainUnit(LPEDICT townhall, DWORD class_id) {
    LPGAMECLIENT client;
    LPEDICT clent;
    LPPLAYER player;
    buildCommandState_t state;
    char reason[128];

    if (!townhall || !class_id) return false;
    client = G_GetPlayerClientByNumber(townhall->s.player);
    if (!client || client->ps.number != townhall->s.player) return false;
    clent = G_GetPlayerEntityByNumber(townhall->s.player);
    state = G_GetTrainCommandState(client, townhall, class_id, reason, sizeof(reason));
    if (state != BUILD_COMMAND_AVAILABLE) {
        if (clent && reason[0]) UI_ShowText(clent, &MAKE(VECTOR2, 0, 0), reason, 2.0f);
        return false;
    }
    player = G_GetPlayerByNumber(townhall->s.player);
    if (player_pay(player, class_id)) {
        unit_build(townhall, class_id);
        if (clent) {
            Get_Portrait_f(clent);
            Get_Commands_f(clent);
        }
        return true;
    } else if (clent) {
        UI_ShowText(clent, &MAKE(VECTOR2, 0, 0), "Not enough resources", 2.0f);
    }
    return false;
}

ability_t a_train = {
    
};
