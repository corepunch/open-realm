#include "s_skills.h"

static void RefreshTrainingQueue(LPEDICT producer) {
    LPGAMECLIENT client;
    LPEDICT clent;

    if (!producer) return;
    client = G_GetPlayerClientByNumber(producer->s.player);
    if (!client || client->ps.number != producer->s.player || !G_IsEntitySelected(client, producer)) return;
    clent = G_GetPlayerEntityByNumber(producer->s.player);
    if (clent) Get_Portrait_f(clent);
}

static BOOL ReserveTrainingFood(LPEDICT producer, LPEDICT unit) {
    LPGAMECLIENT client;
    LPEDICT clent;
    LONG cost;
    BOOL was_waiting;

    if (!unit || !unit->UnitBalance) return false;
    was_waiting = unit->training_food_wait_notified;
    if (G_ReserveTrainingFood(unit)) {
        if (was_waiting) {
            unit->training_food_wait_notified = false;
            RefreshTrainingQueue(producer);
        }
        return true;
    }

    cost = MAX(0, unit->UnitBalance->foodUsed);
    client = G_GetPlayerClientByNumber(unit->s.player);
    if (cost <= 0 || !client || client->ps.number != unit->s.player ||
        G_PlayerHasFoodFor(client, cost) || unit->training_food_wait_notified) {
        return false;
    }

    unit->training_food_wait_notified = true;
    clent = G_GetPlayerEntityByNumber(unit->s.player);
    if (clent && client->connected) {
        UI_ShowText(clent, &MAKE(VECTOR2, 0, 0), "Not enough food", 2.0f);
    }
    return false;
}

static void RefundTrainingCost(LPEDICT item) {
    LPPLAYER player;
    UnitBalance_t const *balance;
    LONG gold, lumber;

    if (!item || !item->UnitBalance) return;
    player = G_GetPlayerByNumber(item->s.player);
    if (!player) return;
    balance = item->UnitBalance;
    gold = (LONG)player->stats[PLAYERSTATE_RESOURCE_GOLD] + MAX(0, balance->goldCost);
    lumber = (LONG)player->stats[PLAYERSTATE_RESOURCE_LUMBER] + MAX(0, balance->lumberCost);
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = (USHORT)MIN(gold, USHRT_MAX);
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = (USHORT)MIN(lumber, USHRT_MAX);
}

static BOOL CancelTrainingQueueItem(LPEDICT producer, DWORD index, BOOL refund, BOOL activate_next) {
    LPEDICT prev = NULL;
    LPEDICT item;
    LPEDICT next;
    LPGAMECLIENT client;

    if (!producer) return false;
    item = producer->build;
    for (DWORD i = 0; item && item->training && i < index; i++) {
        prev = item;
        item = item->build;
    }
    if (!item || !item->training) return false;

    next = item->build;
    if (prev) prev->build = next;
    else producer->build = next;
    item->build = NULL;

    /* Publish while the cancelled queue entity still carries its unit and
     * owner metadata; clearing it first made train-cancel triggers impossible. */
    G_PublishEvent(item, EVENT_PLAYER_UNIT_TRAIN_CANCEL);
    G_PublishEvent(item, EVENT_UNIT_TRAIN_CANCEL);
    if (refund) RefundTrainingCost(item);
    G_ClearUnitFood(item);
    G_FreeEdict(item);

    client = G_GetPlayerClientByNumber(producer->s.player);
    if (client && client->ps.number == producer->s.player) G_InvalidateCommands(client);

    if (!producer->build) {
        if (activate_next && producer->stand) producer->stand(producer);
    } else if (!prev && activate_next) {
        /* A new queue head becomes active immediately. Reserve now rather than
         * waiting one simulation tick so command-time food checks observe the
         * authoritative active reservation. */
        ReserveTrainingFood(producer, producer->build);
    }
    return true;
}

BOOL G_CancelTrainingQueueItem(LPEDICT producer, DWORD index, BOOL refund) {
    return CancelTrainingQueueItem(producer, index, refund, true);
}

void G_CancelTrainingQueue(LPEDICT producer, BOOL refund) {
    if (!producer || !producer->build || !producer->build->training) return;
    while (producer->build && producer->build->training) {
        if (!CancelTrainingQueueItem(producer, 0, refund, false)) break;
    }
}

static BOOL ShowTrainedUnit(LPEDICT townhall, LPEDICT unit) {
    VECTOR2 origin;
    FLOAT angle;

    if (!SP_FindUnitExitPosition(townhall, unit, &origin, &angle)) {
        return false;
    }
    unit->s.origin2 = origin;
    unit->s.angle = angle;
    unit->training = false;
    unit->training_food_wait_notified = false;
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
    if (!ReserveTrainingFood(ent, ent->build)) {
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
        if (ent->build) {
            /* The next item is active as soon as the previous item completes.
             * Reserving here closes the one-frame gap where another Train click
             * could otherwise see stale Food Used. */
            ReserveTrainingFood(ent, ent->build);
        }
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
    BOOL was_empty;
    LPEDICT ent;

    was_empty = self->build == NULL;
    ent = SP_SpawnAtLocation(class_id, self->s.player, &self->s.origin2);
    ent->training = true;
    ent->training_food_wait_notified = false;
    ent->health.value = 0;
    /* SP_SpawnAtLocation already ran birth; calling it twice reset the trained unit and crashed sparse fixtures. */
    ent->s.renderfx |= RF_HIDDEN;
    unit_add_build_queue(self, ent);
    if (was_empty) {
        /* Queue insertion makes this item active immediately. Food reservation
         * must therefore happen before a later Train command performs its
         * command-time food check. */
        ReserveTrainingFood(self, ent);
    }
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
        if (clent && client->connected && reason[0]) UI_ShowText(clent, &MAKE(VECTOR2, 0, 0), reason, 2.0f);
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
    } else if (clent && client->connected) {
        UI_ShowText(clent, &MAKE(VECTOR2, 0, 0), "Not enough resources", 2.0f);
    }
    return false;
}

ability_t a_train = {
    
};
