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

static bool ReserveTrainingFood(LPEDICT producer, LPEDICT unit) {
    LPGAMECLIENT client;
    LPEDICT clent;
    int32_t cost;
    bool was_waiting;

    if (!unit || !unit->data.UnitBalance) return false;
    /* Warsmash sacrifice queues do not reserve additional food while the
     * consumed worker still exists; the resulting Shade inherits the slot at
     * completion. The exception policy lives in s_sacrifice.c. */
    if (S_SacrificeSkipsFoodReservation(unit)) return true;
    was_waiting = unit->training_food_wait_notified;
    if (G_ReserveTrainingFood(unit)) {
        if (was_waiting) {
            unit->training_food_wait_notified = false;
            RefreshTrainingQueue(producer);
        }
        return true;
    }

    cost = MAX(0, unit->data.UnitBalance->foodUsed);
    client = G_GetPlayerClientByNumber(unit->s.player);
    if (cost <= 0 || !client || client->ps.number != unit->s.player ||
        G_PlayerHasFoodFor(client, cost) || unit->training_food_wait_notified) {
        return false;
    }

    unit->training_food_wait_notified = true;
    clent = G_GetPlayerEntityByNumber(unit->s.player);
    if (clent && client->connected) {
        G_ShowCommandErrorText(clent, "Not enough food");
    }
    return false;
}

static LPEDICT ProductionNext(LPEDICT item) {
    if (!item) return NULL;
    return item->revival.reviving ? item->revival.queue_next : item->build;
}

static void ProductionSetNext(LPEDICT item, LPEDICT next) {
    if (!item) return;
    if (item->revival.reviving) item->revival.queue_next = next;
    else item->build = next;
}

/* Queue lifecycle ownership dispatch. Special queue types validate, complete
 * and cancel through their owning ability procedure; Train keeps allocation,
 * progress, ordering, placement and resource mechanisms. */
static intptr_t SacrificeQueueMessage(LPEDICT producer, LPEDICT item, abilityMsg_t msg) {
    abilityitem_t ab = S_AbilityItem(S_SacrificeAbilityCode());
    abilityCall_t call = MAKE(abilityCall_t, .item = &ab);
    if (!ab.ability || !ab.ability->proc) return false;
    call.queue.producer = producer; call.queue.item = item;
    return S_AbilityMessage(producer, msg, &call);
}

static void RefundTrainingCost(LPEDICT item) {
    LPPLAYER player;
    UnitBalance_t const *balance;
    int32_t gold, lumber;

    if (!item || !item->data.UnitBalance) return;
    player = G_GetPlayerByNumber(item->s.player);
    if (!player) return;
    balance = item->data.UnitBalance;
    gold = (int32_t)player->stats[PLAYERSTATE_RESOURCE_GOLD] + MAX(0, balance->goldCost);
    lumber = (int32_t)player->stats[PLAYERSTATE_RESOURCE_LUMBER] + MAX(0, balance->lumberCost);
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = (uint16_t)MIN(gold, USHRT_MAX);
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = (uint16_t)MIN(lumber, USHRT_MAX);
}


static void RefundResearchCost(LPEDICT item) {
    LPPLAYER player;
    int32_t gold, lumber;

    if (!item || !item->research.upgrade) return;
    player = G_GetPlayerByNumber(item->s.player);
    if (!player) return;
    gold = (int32_t)player->stats[PLAYERSTATE_RESOURCE_GOLD] + MAX(0, item->research.gold);
    lumber = (int32_t)player->stats[PLAYERSTATE_RESOURCE_LUMBER] + MAX(0, item->research.lumber);
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = (uint16_t)MIN(gold, USHRT_MAX);
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = (uint16_t)MIN(lumber, USHRT_MAX);
}

static void ShowResearchComplete(LPEDICT producer, uint32_t upgrade_id, int32_t level_value) {
    LPGAMECLIENT client;
    LPEDICT clent;
    gameCommandButton_t button;
    char text[512];
    cstring_t completed;
    cstring_t sound;

    if (!producer) return;
    client = G_GetPlayerClientByNumber(producer->s.player);
    clent = G_GetPlayerEntityByNumber(producer->s.player);
    if (!client || !clent || !client->connected || client->ps.number != producer->s.player) return;

    if (G_BuildCommandButton(producer, GetClassName(upgrade_id), true, (uint32_t)level_value, &button)) {
        completed = UI_GetString("COLON_COMPLETED");
        snprintf(text, sizeof(text), "%s%s",
                 completed && strcmp(completed, "COLON_COMPLETED") ? completed : "Completed: ",
                 button.tooltip[0] ? button.tooltip : GetClassName(upgrade_id));
        UI_ShowText(clent, &MAKE(VECTOR2, 0, 0), text, 2.0f);
    }
    sound = Theme_PlayerString(client, "ResearchComplete", NULL);
    if (sound && *sound) G_PlayUISoundForPlayer(clent, sound);
    G_SendOwnerMinimapAlert(producer);
}

static bool CancelTrainingQueueItem(LPEDICT producer, uint32_t index, bool refund, bool activate_next) {
    LPEDICT prev = NULL;
    LPEDICT item;
    LPEDICT next;
    LPGAMECLIENT client;

    if (!producer) return false;
    item = producer->build;
    for (uint32_t i = 0; item && i < index; i++) {
        prev = item;
        item = ProductionNext(item);
    }
    if (!item) return false;
    if (item->revival.reviving) return G_CancelHeroRevive(producer, item);
    if (!item->training) return false;

    next = ProductionNext(item);
    if (prev) ProductionSetNext(prev, next);
    else producer->build = next;
    ProductionSetNext(item, NULL);

    if (item->sacrifice.active) {
        /* The input Acolyte was not charged and the owner restores it; Train
         * refunds only the resulting unit's authored cost, matching Warsmash. */
        SacrificeQueueMessage(producer, item, A_QUEUE_CANCEL);
        if (refund) RefundTrainingCost(item);
        G_ClearUnitFood(item);
    } else if (item->research.upgrade) {
        uint32_t const upgrade_id = item->research.upgrade;
        if (refund) RefundResearchCost(item);
        G_AddPlayerTechInProgress(G_GetPlayerClientByNumber(item->s.player),
                                  upgrade_id, -1);
        /* The queue entity is freed immediately below, so carry the researched
         * rawcode as scalar event context instead of retaining its edict. */
        G_PublishEventWithValue(producer, EVENT_PLAYER_UNIT_RESEARCH_CANCEL, NULL, (int32_t)upgrade_id);
        G_PublishEventWithValue(producer, EVENT_UNIT_RESEARCH_CANCEL, NULL, (int32_t)upgrade_id);
    } else {
        /* Publish while the cancelled queue entity still carries its unit and
         * owner metadata; clearing it first made train-cancel triggers impossible. */
        G_PublishEvent(item, EVENT_PLAYER_UNIT_TRAIN_CANCEL);
        G_PublishEvent(item, EVENT_UNIT_TRAIN_CANCEL);
        if (refund) RefundTrainingCost(item);
        G_ClearUnitFood(item);
    }
    G_FreeEdict(item);

    client = G_GetPlayerClientByNumber(producer->s.player);
    if (client && client->ps.number == producer->s.player) G_InvalidateCommands(client);

    if (!producer->build) {
        if (activate_next && producer->stand) producer->stand(producer);
    } else if (!prev && activate_next && producer->build->training &&
               !producer->build->research.upgrade && !producer->build->revival.reviving) {
        /* A new ordinary-training head becomes active immediately. Research
         * and revival do not reserve Food Used. */
        ReserveTrainingFood(producer, producer->build);
    }
    return true;
}

bool G_CancelTrainingQueueItem(LPEDICT producer, uint32_t index, bool refund) {
    return CancelTrainingQueueItem(producer, index, refund, true);
}

void G_CancelTrainingQueue(LPEDICT producer, bool refund) {
    if (!producer || !producer->build || !producer->build->training) return;
    while (producer->build && producer->build->training) {
        if (!CancelTrainingQueueItem(producer, 0, refund, false)) break;
    }
}

static bool HeroReviveMisc(cstring_t key, float *out) {
    cstring_t value;
    if (!key || !out) return false;
    value = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    if (!value || !*value) {
        fprintf(stderr, "Hero revival: missing Misc.%s\n", key);
        return false;
    }
    *out = (float)atof(value);
    return true;
}

bool G_UnitCanReviveHeroes(LPCEDICT altar) {
    cstring_t revive = altar && altar->data.UnitProfile ? altar->data.UnitProfile->revive : NULL;
    return revive && *revive && atoi(revive) != 0;
}

bool G_HeroCanBeRevivedAt(LPCEDICT altar, LPCEDICT hero) {
    return altar && hero && altar->inuse && hero->inuse &&
        !(altar->svflags & SVF_DEADMONSTER) && G_UnitCanReviveHeroes(altar) &&
        altar->s.player == hero->s.player && hero->data.UnitBalance &&
        G_UnitIsHero(hero) && (hero->svflags & SVF_DEADMONSTER) &&
        hero->revival.awaiting && !hero->revival.reviving;
}

static bool HeroReviveValues(LPCEDICT hero, uint32_t *gold, uint32_t *lumber, float *seconds) {
    float goldBase, goldLevel, lumberBase, lumberLevel, maxFactor;
    float timeFactor, maxTimeFactor, factor;
    uint32_t level;
    if (!hero || !hero->data.UnitBalance || !G_UnitIsHero(hero)) return false;
    if (!HeroReviveMisc("ReviveBaseFactor", &goldBase) ||
        !HeroReviveMisc("ReviveLevelFactor", &goldLevel) ||
        !HeroReviveMisc("ReviveBaseLumberFactor", &lumberBase) ||
        !HeroReviveMisc("ReviveLumberLevelFactor", &lumberLevel) ||
        !HeroReviveMisc("ReviveMaxFactor", &maxFactor) ||
        !HeroReviveMisc("ReviveTimeFactor", &timeFactor) ||
        !HeroReviveMisc("ReviveMaxTimeFactor", &maxTimeFactor)) return false;

    level = MAX(1, hero->hero.level);
    factor = goldBase + goldLevel * (float)(level - 1);
    if (maxFactor > 0.0f) factor = MIN(factor, maxFactor);
    if (gold) *gold = (uint32_t)MAX(0.0f, (float)MAX(0, hero->data.UnitBalance->goldCost) * factor);

    factor = lumberBase + lumberLevel * (float)(level - 1);
    if (maxFactor > 0.0f) factor = MIN(factor, maxFactor);
    if (lumber) *lumber = (uint32_t)MAX(0.0f, (float)MAX(0, hero->data.UnitBalance->lumberCost) * factor);

    factor = (float)MAX(0, hero->data.UnitBalance->buildTime) * (float)level * timeFactor;
    if (maxTimeFactor > 0.0f) {
        float const maximum = (float)MAX(0, hero->data.UnitBalance->buildTime) * maxTimeFactor;
        factor = MIN(factor, maximum);
    }
    if (seconds) *seconds = MAX(0.0f, factor);
    return true;
}

uint32_t G_HeroReviveGoldCost(LPCEDICT hero) {
    uint32_t value = 0;
    HeroReviveValues(hero, &value, NULL, NULL);
    return value;
}

uint32_t G_HeroReviveLumberCost(LPCEDICT hero) {
    uint32_t value = 0;
    HeroReviveValues(hero, NULL, &value, NULL);
    return value;
}

float G_HeroReviveTime(LPCEDICT hero) {
    float value = 0.0f;
    HeroReviveValues(hero, NULL, NULL, &value);
    return value;
}

static uint32_t ProductionQueueCount(LPEDICT producer) {
    uint32_t count = 0;
    for (LPEDICT item = producer ? producer->build : NULL; item && count < MAX_BUILD_QUEUE; item = ProductionNext(item)) {
        count++;
        if (ProductionNext(item) == item) break;
    }
    return count;
}

static void RefreshReviveUI(LPEDICT altar) {
    LPEDICT clent;
    LPGAMECLIENT client;
    if (!altar) return;
    client = G_GetPlayerClientByNumber(altar->s.player);
    clent = G_GetPlayerEntityByNumber(altar->s.player);
    if (client) G_InvalidateCommands(client);
    if (clent) {
        G_RefreshResourceBar(clent);
        Get_Commands_f(clent);
        Get_Portrait_f(clent);
    }
}

static void RefundHeroRevive(LPEDICT altar, LPEDICT hero) {
    LPGAMECLIENT client;
    if (!altar || !hero) return;
    client = G_GetPlayerClientByNumber(hero->revival.player);
    if (!client || client->ps.number != hero->revival.player) return;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] += MAX(0, hero->revival.gold);
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] += MAX(0, hero->revival.lumber);
}

static bool ShowTrainedUnit(LPEDICT townhall, LPEDICT unit) {
    VECTOR2 origin;
    float angle;

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
    G_SetUnitFoodMade(unit, unit->data.UnitBalance->foodMade);
    unit->stand(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
    return true;
}

static bool CompleteHeroRevive(LPEDICT altar, LPEDICT hero) {
    VECTOR2 origin;
    float angle;
    LPEDICT next;

    if (!altar || !hero || !hero->inuse || !hero->revival.reviving ||
        !hero->revival.awaiting || !(hero->svflags & SVF_DEADMONSTER)) return false;
    if (!SP_FindUnitExitPosition(altar, hero, &origin, &angle)) return false;

    next = hero->revival.queue_next;
    altar->build = next;
    hero->revival.reviving = false;
    hero->revival.producer = NULL;
    hero->revival.queue_next = NULL;
    hero->revival.player = 0;
    hero->s.angle = angle;
    G_ReviveHero(hero, origin.x, origin.y);
    G_PublishEventWithSource(hero, EVENT_PLAYER_HERO_REVIVE_FINISH, altar);
    G_PublishEventWithSource(hero, EVENT_UNIT_HERO_REVIVE_FINISH, altar);
    G_ApplyRallyOrder(altar, hero);
    RefreshReviveUI(altar);
    if (!altar->build) altar->stand(altar);
    return true;
}

static bool CompleteResearch(LPEDICT producer, LPEDICT item) {
    LPGAMECLIENT client;
    LPEDICT next;
    uint32_t upgrade_id;
    int32_t level_value;

    if (!producer || !item || !item->research.upgrade) return false;
    client = G_GetPlayerClientByNumber(item->s.player);
    if (!client || client->ps.number != item->s.player) return false;

    upgrade_id = item->research.upgrade;
    level_value = item->research.level;
    next = item->build;
    producer->build = next;
    item->build = NULL;
    G_AddPlayerTechInProgress(client, upgrade_id, -1);
    G_SetPlayerTechResearched(client, upgrade_id, level_value);
    G_PublishEventWithValue(producer, EVENT_PLAYER_UNIT_RESEARCH_FINISH, NULL, (int32_t)upgrade_id);
    G_PublishEventWithValue(producer, EVENT_UNIT_RESEARCH_FINISH, NULL, (int32_t)upgrade_id);
    ShowResearchComplete(producer, upgrade_id, level_value);
    G_FreeEdict(item);

    if (producer->build && producer->build->training &&
        !producer->build->research.upgrade && !producer->build->revival.reviving) {
        ReserveTrainingFood(producer, producer->build);
    }
    if (!producer->build && producer->stand) producer->stand(producer);
    RefreshTrainingQueue(producer);
    return true;
}

void ai_train_build(LPEDICT ent) {
    if (G_BuildingIsUnsummoning(ent)) return;
    if (!ent || !ent->build) {
        if (ent && ent->stand) ent->stand(ent);
        return;
    }
    if (ent->build->revival.reviving) {
        LPEDICT hero = ent->build;
        float required;

        if (!hero->inuse || !hero->revival.awaiting ||
            !(hero->svflags & SVF_DEADMONSTER)) {
            G_CancelHeroRevive(ent, hero);
            return;
        }
        required = G_HeroReviveTime(hero);
        if (required <= 0.0f) return;
        hero->revival.progress += (float)FRAMETIME / 1000.0f;
        if (hero->revival.progress >= required) CompleteHeroRevive(ent, hero);
        return;
    }

    if (ent->build->research.upgrade) {
        LPEDICT research = ent->build;

        if (research->research.duration <= 0.0f || G_PlayerInstantBuild(ent->s.player)) {
            CompleteResearch(ent, research);
            return;
        }
        research->research.progress += (float)FRAMETIME / 1000.0f;
        if (research->research.progress >= research->research.duration) {
            CompleteResearch(ent, research);
        }
        return;
    }

    /* If the sacrificed worker disappears, the owner invalidates the result;
     * cancel and refund its authored cost instead of completing from stale. */
    if (ent->build->sacrifice.active && !SacrificeQueueMessage(ent, ent->build, A_QUEUE_VALIDATE)) {
        CancelTrainingQueueItem(ent, 0, true, true);
        return;
    }
    if (!ReserveTrainingFood(ent, ent->build)) return;
    {
        float const duration = MAX(1.0f, (float)ent->build->data.UnitBalance->buildTime * 1000.0f);
        float const k = (float)FRAMETIME / duration;
        edictStat_s *hp = &ent->build->health;
        if (G_PlayerInstantBuild(ent->s.player)) hp->value = hp->max_value;
        else hp->value += hp->max_value * k;
        if (hp->value >= hp->max_value) {
            LPEDICT clent = G_GetPlayerEntityByNumber(ent->s.player);
            LPEDICT completed = ent->build;
            LPEDICT next = completed->build;

            hp->value = hp->max_value; /* clamp; placement retries every tick until space clears */
            if (!ShowTrainedUnit(ent, completed)) {
                return;
            }
            if (completed->sacrifice.active)
                SacrificeQueueMessage(ent, completed, A_QUEUE_COMPLETE);
            /* Queued units use build as the next-item link, while unit_stand()
             * clears build for the completed unit. Preserve the producer's queue
             * link before revealing/standing the completed unit. */
            ent->build = next;
            if (ent->build && ent->build->training && !ent->build->research.upgrade)
                ReserveTrainingFood(ent, ent->build);
            G_InvalidateCommands(G_GetPlayerClientByNumber(ent->s.player));
            G_QueueReadySound(completed);
            G_SendOwnerMinimapAlert(completed);
            G_PublishEvent(completed, EVENT_PLAYER_UNIT_TRAIN_FINISH);
            G_ApplyRallyOrder(ent, completed);
#ifdef WC3_DEBUG_AI
            fprintf(stderr, "WC3_DEBUG_AI training complete producer=%ld unit=%ld id=%.4s player=%u\n",
                (long)(ent - g_edicts), (long)(completed - g_edicts), (cstring_t)&completed->class_id, completed->s.player);
#endif
            if (!ent->build) {
                ent->stand(ent);
            }
            if (clent) Get_Portrait_f(clent);
        }
    }
}

static umove_t train_move_train = { "stand", ai_train_build, NULL, CAbilityTrain };

/* Train-owned queue mechanisms shared with sacrifice queue creation. */
void TrainSetBuildMove(LPEDICT producer) {
    if (producer) unit_setmove(producer, &train_move_train);
}

void G_RefreshTrainingQueue(LPEDICT producer) { RefreshTrainingQueue(producer); }

void unit_add_build_queue(LPEDICT self, LPEDICT item) {
    LPEDICT last;

    /* Queued units must not run stand/birth callbacks, which clear build and used to sever the queue behind them. */
    item->currentmove = NULL;
    item->animation = NULL;
    if (!self->build) {
        self->build = item;
    } else {
        last = self->build;
        while (ProductionNext(last)) last = ProductionNext(last);
        ProductionSetNext(last, item);
    }
}

bool G_QueueHeroRevive(LPEDICT altar, LPEDICT hero) {
    LPGAMECLIENT client;
    uint32_t gold, lumber;
    float seconds;

    if (!G_HeroCanBeRevivedAt(altar, hero) || ProductionQueueCount(altar) >= MAX_BUILD_QUEUE) return false;
    client = G_GetPlayerClientByNumber(altar->s.player);
    if (!client || client->ps.number != altar->s.player ||
        !HeroReviveValues(hero, &gold, &lumber, &seconds) || seconds <= 0.0f) return false;
    if (gold > client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] ||
        lumber > client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]) return false;

    hero->revival.reviving = true;
    hero->revival.producer = altar;
    hero->revival.queue_next = NULL;
    hero->revival.player = altar->s.player;
    hero->revival.gold = (int32_t)gold;
    hero->revival.lumber = (int32_t)lumber;
    hero->revival.progress = 0.0f;
    unit_add_build_queue(altar, hero);
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] -= gold;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] -= lumber;
    unit_setmove(altar, &train_move_train);
    G_PublishEventWithSource(hero, EVENT_PLAYER_HERO_REVIVE_START, altar);
    G_PublishEventWithSource(hero, EVENT_UNIT_HERO_REVIVE_START, altar);
    RefreshReviveUI(altar);
    return true;
}

bool G_CancelHeroRevive(LPEDICT altar, LPEDICT hero) {
    LPEDICT prev = NULL;
    LPEDICT item;
    LPEDICT next;

    if (!altar || !hero || !hero->revival.reviving || hero->revival.producer != altar) return false;
    for (item = altar->build; item; prev = item, item = ProductionNext(item)) {
        if (item == hero) break;
    }
    if (!item) return false;
    next = ProductionNext(item);
    if (prev) ProductionSetNext(prev, next);
    else altar->build = next;
    RefundHeroRevive(altar, hero);
    hero->revival.reviving = false;
    hero->revival.producer = NULL;
    hero->revival.queue_next = NULL;
    hero->revival.player = 0;
    hero->revival.gold = hero->revival.lumber = 0;
    hero->revival.progress = 0.0f;
    G_PublishEventWithSource(hero, EVENT_PLAYER_HERO_REVIVE_CANCEL, altar);
    G_PublishEventWithSource(hero, EVENT_UNIT_HERO_REVIVE_CANCEL, altar);
    RefreshReviveUI(altar);
    if (!altar->build && !M_IsDead(altar) && altar->stand) altar->stand(altar);
    return true;
}

void G_CancelHeroRevives(LPEDICT altar) {
    LPEDICT visited[MAX_BUILD_QUEUE];
    LPEDICT item;
    LPEDICT next;
    uint32_t visited_count = 0;

    /* This cleanup is also called for ordinary units on death/removal. Their
     * build pointer can name a construction target, whose self-link is not a
     * production queue. Construction itself cannot own an active revive queue. */
    if (!altar || !G_UnitCanReviveHeroes(altar) || altar->construction.active || altar->build == altar) return;
    item = altar->build;
    while (item) {
        for (uint32_t i = 0; i < visited_count; i++) {
            if (visited[i] == item) {
                fprintf(stderr, "WC3: cyclic Hero-revive queue at producer %ld (item %ld)\n",
                        (long)(altar - g_edicts), (long)(item - g_edicts));
                return;
            }
        }
        if (visited_count >= MAX_BUILD_QUEUE) {
            fprintf(stderr, "WC3: Hero-revive queue exceeds %d entries at producer %ld\n",
                    MAX_BUILD_QUEUE, (long)(altar - g_edicts));
            return;
        }
        visited[visited_count++] = item;
        next = ProductionNext(item);
        if (item->revival.reviving) G_CancelHeroRevive(altar, item);
        item = next;
    }
}

void unit_build(LPEDICT self, uint32_t class_id) {
    bool was_empty;
    LPEDICT ent;

    was_empty = self->build == NULL;
    ent = SP_SpawnAtLocation(class_id, self->s.player, &self->s.origin2);
    ent->training = true;
    ent->training_food_wait_notified = false;
    G_SetHealth(ent, 0);
    /* SP_SpawnAtLocation already ran birth; calling it twice reset the trained unit and crashed sparse fixtures. */
    ent->s.renderfx |= RF_HIDDEN;
    unit_add_build_queue(self, ent);
    /* Warcraft publishes TRAIN_START when an accepted trainee enters the
     * producer queue.  The producer is the triggering unit; the hidden queued
     * trainee is carried as event source so GetTrainedUnitType/GetTrainedUnit
     * can expose the trainee without changing GetTriggerUnit semantics. */
    G_PublishEventWithSource(self, EVENT_PLAYER_UNIT_TRAIN_START, ent);
    G_PublishEventWithSource(self, EVENT_UNIT_TRAIN_START, ent);
    if (was_empty) {
        /* Queue insertion makes this item active immediately. Food reservation
         * must therefore happen before a later Train command performs its
         * command-time food check. */
        ReserveTrainingFood(self, ent);
    }
    unit_setmove(self, &train_move_train);
}

bool G_QueueResearch(LPEDICT producer, uint32_t upgrade_id) {
    LPGAMECLIENT client;
    LPEDICT clent;
    LPEDICT item;
    buildCommandState_t state;
    int32_t level_value = 0;
    int32_t gold, lumber;
    float duration;
    char reason[128];

    if (!producer || !upgrade_id || ProductionQueueCount(producer) >= MAX_BUILD_QUEUE) return false;
    client = G_GetPlayerClientByNumber(producer->s.player);
    if (!client || client->ps.number != producer->s.player) return false;
    clent = G_GetPlayerEntityByNumber(producer->s.player);
    state = G_GetResearchCommandState(client, producer, upgrade_id, &level_value, reason, sizeof(reason));
    if (state != BUILD_COMMAND_AVAILABLE) {
        if (clent && client->connected && reason[0]) G_ShowCommandErrorText(clent, reason);
        return false;
    }

    gold = G_UpgradeGoldCost(upgrade_id, level_value);
    lumber = G_UpgradeLumberCost(upgrade_id, level_value);
    duration = G_UpgradeResearchTime(upgrade_id, level_value);
    if (gold > (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] ||
        lumber > (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]) return false;

    item = G_Spawn();
    if (!item) return false;
    /* This is queue state, not a world unit/tech entity. Keep class_id zero so
     * generic entity-count queries never mistake in-progress research for a
     * completed technology or owned unit of the same rawcode. */
    item->class_id = 0;
    item->s.player = producer->s.player;
    item->training = true;
    item->s.renderfx |= RF_HIDDEN;
    item->research.upgrade = upgrade_id;
    item->research.level = level_value;
    item->research.gold = gold;
    item->research.lumber = lumber;
    item->research.duration = duration;
    item->research.progress = 0.0f;
    unit_add_build_queue(producer, item);

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] -= gold;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] -= lumber;
    G_AddPlayerTechInProgress(client, upgrade_id, 1);
    /* Warcraft research callbacks identify the producer through
     * GetResearchingUnit() and the upgrade through GetResearched().  Publish
     * at command acceptance, matching the existing TRAIN_START queue contract. */
    G_PublishEventWithValue(producer, EVENT_PLAYER_UNIT_RESEARCH_START, NULL, (int32_t)upgrade_id);
    G_PublishEventWithValue(producer, EVENT_UNIT_RESEARCH_START, NULL, (int32_t)upgrade_id);
    unit_setmove(producer, &train_move_train);
    if (clent && client->connected) {
        G_RefreshResourceBar(clent);
        Get_Commands_f(clent);
        Get_Portrait_f(clent);
    }
    return true;
}

bool SP_TrainUnit(LPEDICT townhall, uint32_t class_id) {
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
        if (clent && client->connected && reason[0]) G_ShowCommandErrorText(clent, reason);
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
        G_ShowCommandErrorText(clent, "Not enough resources");
    }
    return false;
}

BZ_ABILITY_PROC(CAbilityTrain) {
    return CAbilityNoop(ent, msg, call);
}
