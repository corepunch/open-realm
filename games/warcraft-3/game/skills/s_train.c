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

    if (!unit || !unit->data.UnitBalance) return false;
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

static void RefundTrainingCost(LPEDICT item) {
    LPPLAYER player;
    UnitBalance_t const *balance;
    LONG gold, lumber;

    if (!item || !item->data.UnitBalance) return;
    player = G_GetPlayerByNumber(item->s.player);
    if (!player) return;
    balance = item->data.UnitBalance;
    gold = (LONG)player->stats[PLAYERSTATE_RESOURCE_GOLD] + MAX(0, balance->goldCost);
    lumber = (LONG)player->stats[PLAYERSTATE_RESOURCE_LUMBER] + MAX(0, balance->lumberCost);
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = (USHORT)MIN(gold, USHRT_MAX);
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = (USHORT)MIN(lumber, USHRT_MAX);
}

static void RefundResearchCost(LPEDICT item) {
    LPPLAYER player;
    LONG gold, lumber;

    if (!item || !item->research.upgrade) return;
    player = G_GetPlayerByNumber(item->s.player);
    if (!player) return;
    gold = (LONG)player->stats[PLAYERSTATE_RESOURCE_GOLD] + MAX(0, item->research.gold);
    lumber = (LONG)player->stats[PLAYERSTATE_RESOURCE_LUMBER] + MAX(0, item->research.lumber);
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = (USHORT)MIN(gold, USHRT_MAX);
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = (USHORT)MIN(lumber, USHRT_MAX);
}

static void ShowResearchComplete(LPEDICT producer, DWORD upgrade_id, LONG level_value) {
    LPGAMECLIENT client;
    LPEDICT clent;
    gameCommandButton_t button;
    char text[512];
    LPCSTR completed;
    LPCSTR sound;

    if (!producer) return;
    client = G_GetPlayerClientByNumber(producer->s.player);
    clent = G_GetPlayerEntityByNumber(producer->s.player);
    if (!client || !clent || !client->connected || client->ps.number != producer->s.player) return;

    if (G_BuildCommandButton(producer, GetClassName(upgrade_id), true, (DWORD)level_value, &button)) {
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

static BOOL CancelTrainingQueueItem(LPEDICT producer, DWORD index, BOOL refund, BOOL activate_next) {
    LPEDICT prev = NULL;
    LPEDICT item;
    LPEDICT next;
    LPGAMECLIENT client;

    if (!producer) return false;
    item = producer->build;
    for (DWORD i = 0; item && i < index; i++) {
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

    if (item->research.upgrade) {
        if (refund) RefundResearchCost(item);
        G_AddPlayerTechInProgress(G_GetPlayerClientByNumber(item->s.player),
                                  item->research.upgrade, -1);
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

BOOL G_CancelTrainingQueueItem(LPEDICT producer, DWORD index, BOOL refund) {
    return CancelTrainingQueueItem(producer, index, refund, true);
}

void G_CancelTrainingQueue(LPEDICT producer, BOOL refund) {
    if (!producer || !producer->build || !producer->build->training) return;
    while (producer->build && producer->build->training) {
        if (!CancelTrainingQueueItem(producer, 0, refund, false)) break;
    }
}

static BOOL HeroReviveMisc(LPCSTR key, FLOAT *out) {
    LPCSTR value;
    if (!key || !out) return false;
    value = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    if (!value || !*value) {
        fprintf(stderr, "Hero revival: missing Misc.%s\n", key);
        return false;
    }
    *out = (FLOAT)atof(value);
    return true;
}

BOOL G_UnitCanReviveHeroes(LPCEDICT altar) {
    LPCSTR revive = altar && altar->data.UnitProfile ? altar->data.UnitProfile->revive : NULL;
    return revive && *revive && atoi(revive) != 0;
}

BOOL G_HeroCanBeRevivedAt(LPCEDICT altar, LPCEDICT hero) {
    return altar && hero && altar->inuse && hero->inuse &&
        !(altar->svflags & SVF_DEADMONSTER) && G_UnitCanReviveHeroes(altar) &&
        altar->s.player == hero->s.player && hero->data.UnitBalance &&
        G_UnitIsHero(hero) && (hero->svflags & SVF_DEADMONSTER) &&
        hero->revival.awaiting && !hero->revival.reviving;
}

static BOOL HeroReviveValues(LPCEDICT hero, DWORD *gold, DWORD *lumber, FLOAT *seconds) {
    FLOAT goldBase, goldLevel, lumberBase, lumberLevel, maxFactor;
    FLOAT timeFactor, maxTimeFactor, factor;
    DWORD level;
    if (!hero || !hero->data.UnitBalance || !G_UnitIsHero(hero)) return false;
    if (!HeroReviveMisc("ReviveBaseFactor", &goldBase) ||
        !HeroReviveMisc("ReviveLevelFactor", &goldLevel) ||
        !HeroReviveMisc("ReviveBaseLumberFactor", &lumberBase) ||
        !HeroReviveMisc("ReviveLumberLevelFactor", &lumberLevel) ||
        !HeroReviveMisc("ReviveMaxFactor", &maxFactor) ||
        !HeroReviveMisc("ReviveTimeFactor", &timeFactor) ||
        !HeroReviveMisc("ReviveMaxTimeFactor", &maxTimeFactor)) return false;

    level = MAX(1, hero->hero.level);
    factor = goldBase + goldLevel * (FLOAT)(level - 1);
    if (maxFactor > 0.0f) factor = MIN(factor, maxFactor);
    if (gold) *gold = (DWORD)MAX(0.0f, (FLOAT)MAX(0, hero->data.UnitBalance->goldCost) * factor);

    factor = lumberBase + lumberLevel * (FLOAT)(level - 1);
    if (maxFactor > 0.0f) factor = MIN(factor, maxFactor);
    if (lumber) *lumber = (DWORD)MAX(0.0f, (FLOAT)MAX(0, hero->data.UnitBalance->lumberCost) * factor);

    factor = (FLOAT)MAX(0, hero->data.UnitBalance->buildTime) * (FLOAT)level * timeFactor;
    if (maxTimeFactor > 0.0f) {
        FLOAT const maximum = (FLOAT)MAX(0, hero->data.UnitBalance->buildTime) * maxTimeFactor;
        factor = MIN(factor, maximum);
    }
    if (seconds) *seconds = MAX(0.0f, factor);
    return true;
}

DWORD G_HeroReviveGoldCost(LPCEDICT hero) {
    DWORD value = 0;
    HeroReviveValues(hero, &value, NULL, NULL);
    return value;
}

DWORD G_HeroReviveLumberCost(LPCEDICT hero) {
    DWORD value = 0;
    HeroReviveValues(hero, NULL, &value, NULL);
    return value;
}

FLOAT G_HeroReviveTime(LPCEDICT hero) {
    FLOAT value = 0.0f;
    HeroReviveValues(hero, NULL, NULL, &value);
    return value;
}

static DWORD ProductionQueueCount(LPEDICT producer) {
    DWORD count = 0;
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
    G_SetUnitFoodMade(unit, unit->data.UnitBalance->foodMade);
    unit->stand(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
    return true;
}

static BOOL CompleteHeroRevive(LPEDICT altar, LPEDICT hero) {
    VECTOR2 origin;
    FLOAT angle;
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

static BOOL CompleteResearch(LPEDICT producer, LPEDICT item) {
    LPGAMECLIENT client;
    LPEDICT next;
    DWORD upgrade_id;
    LONG level_value;

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
    if (!ent || !ent->build) {
        if (ent && ent->stand) ent->stand(ent);
        return;
    }
    if (ent->build->revival.reviving) {
        LPEDICT hero = ent->build;
        FLOAT required;

        if (!hero->inuse || !hero->revival.awaiting ||
            !(hero->svflags & SVF_DEADMONSTER)) {
            G_CancelHeroRevive(ent, hero);
            return;
        }
        required = G_HeroReviveTime(hero);
        if (required <= 0.0f) return;
        hero->revival.progress += (FLOAT)FRAMETIME / 1000.0f;
        if (hero->revival.progress >= required) CompleteHeroRevive(ent, hero);
        return;
    }

    if (ent->build->research.upgrade) {
        LPEDICT research = ent->build;

        if (research->research.duration <= 0.0f) {
            CompleteResearch(ent, research);
            return;
        }
        research->research.progress += (FLOAT)FRAMETIME / 1000.0f;
        if (research->research.progress >= research->research.duration) {
            CompleteResearch(ent, research);
        }
        return;
    }

    /* Only the active ordinary-training head owns food. Revival and research
     * have no food reservation. */
    if (!ReserveTrainingFood(ent, ent->build)) return;
    {
        FLOAT const k = (FLOAT)FRAMETIME / ((FLOAT)ent->build->data.UnitBalance->buildTime * 1000.0f);
        edictStat_s *hp = &ent->build->health;
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
            if (ent->build && ent->build->training && !ent->build->research.upgrade)
                ReserveTrainingFood(ent, ent->build);
            G_InvalidateCommands(G_GetPlayerClientByNumber(ent->s.player));
            G_QueueReadySound(completed);
            G_SendOwnerMinimapAlert(completed);
            G_PublishEvent(completed, EVENT_PLAYER_UNIT_TRAIN_FINISH);
            G_ApplyRallyOrder(ent, completed);
#ifdef WC3_DEBUG_AI
            fprintf(stderr, "WC3_DEBUG_AI training complete producer=%ld unit=%ld id=%.4s player=%u\n",
                (long)(ent - g_edicts), (long)(completed - g_edicts), (LPCSTR)&completed->class_id, completed->s.player);
#endif
            if (!ent->build) {
                ent->stand(ent);
            }
            if (clent) Get_Portrait_f(clent);
        }
    }
}

static umove_t train_move_train = { "stand", ai_train_build, NULL, &a_train };

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

BOOL G_QueueHeroRevive(LPEDICT altar, LPEDICT hero) {
    LPGAMECLIENT client;
    DWORD gold, lumber;
    FLOAT seconds;

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
    hero->revival.gold = (LONG)gold;
    hero->revival.lumber = (LONG)lumber;
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

BOOL G_CancelHeroRevive(LPEDICT altar, LPEDICT hero) {
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
    LPEDICT item;
    LPEDICT next;

    if (!altar) return;
    item = altar->build;
    while (item) {
        next = ProductionNext(item);
        if (item->revival.reviving) G_CancelHeroRevive(altar, item);
        item = next;
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

BOOL G_QueueResearch(LPEDICT producer, DWORD upgrade_id) {
    LPGAMECLIENT client;
    LPEDICT clent;
    LPEDICT item;
    buildCommandState_t state;
    LONG level_value = 0;
    LONG gold, lumber;
    FLOAT duration;
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
    if (gold > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] ||
        lumber > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]) return false;

    item = G_Spawn();
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
    unit_setmove(producer, &train_move_train);
    if (clent && client->connected) {
        G_RefreshResourceBar(clent);
        Get_Commands_f(clent);
        Get_Portrait_f(clent);
    }
    return true;
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

ability_t a_train = {
    
};
