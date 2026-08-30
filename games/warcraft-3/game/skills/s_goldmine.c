#include "s_skills.h"

FLOAT MAX_GOLD;
FLOAT MINING_DURATION;
FLOAT MINING_CAPACITY;
extern FLOAT HARVEST_GOLD_CAPACITY;

void harvestgold_walkback(LPEDICT ent);
void harvestgold_walk(LPEDICT ent);
void harvestgold_wait(LPEDICT ent);
void harvestgold_minegold(LPEDICT ent);

static void ai_walkmine(LPEDICT ent) {
    /* Building collision became footprint-authored in 55724517; using the old
     * fixed 180u mine radius stranded workers outside larger mine footprints. */
    FLOAT const contact = ent->collision + ent->goalentity->collision;
    if (M_DistanceToGoal(ent) <= contact + unit_movedistance(ent)) {
        harvestgold_minegold(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static void ai_goldmine_walkback(LPEDICT ent) {
    if (!S_CanReturnResourceAt(ent, ent->goalentity, RETURN_RESOURCE_GOLD)) {
        LPEDICT dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_GOLD);
        if (!dropoff) {
            ent->stand(ent);
            return;
        }
        G_PublishMessage(ent, GAME_MSG_HARVEST_RETURN_GOLD, dropoff);
        ent->goalentity = dropoff;
    }

    FLOAT const dist = M_DistanceToGoal(ent);
    FLOAT const contact = ent->collision + ent->goalentity->collision;
    FLOAT const step = unit_movedistance(ent);

    /* Building pathing can block the next movement step before the worker
     * reaches physical contact. Deposit once that next step would cross the
     * contact boundary, matching the gold-mine entry interaction rule. */
    if (dist <= contact + step) {
        LPEDICT dropoff = ent->goalentity;
        G_PublishMessage(ent, GAME_MSG_HARVEST_DEPOSIT_GOLD, dropoff);
        ent->goalentity = ent->secondarygoal;
        LPPLAYER player = G_GetPlayerByNumber(ent->s.player);
        if (player) {
            player->stats[PLAYERSTATE_RESOURCE_GOLD] += ent->harvested_gold;
        }
        ent->s.renderfx &= ~RF_HAS_GOLD;
        ent->harvested_gold = 0;
        G_PublishMessage(ent, GAME_MSG_HARVEST_RESUME_GOLD, ent->goalentity);
        harvestgold_walk(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static void ai_minegold(LPEDICT ent) {
    unit_runwait(ent, harvestgold_walkback);
}

static void ai_waittoenter(LPEDICT ent) {
}

static umove_t harvestgold_move_walk = { "walk", ai_walkmine, NULL, &a_goldmine };
static umove_t harvestgold_move_walkback = { "walk", ai_goldmine_walkback, NULL, &a_goldmine };
static umove_t harvestgold_move_minegold = { "attack", ai_minegold, NULL, &a_goldmine };
static umove_t harvestgold_move_wait = { "stand", ai_waittoenter, NULL, &a_goldmine };

void harvestgold_walk(LPEDICT ent) {
    unit_setmove(ent, &harvestgold_move_walk);
}

void harvestgold_minegold(LPEDICT ent) {
    if (ent->goalentity->peonsinside < MINING_CAPACITY) {
        G_PublishMessage(ent, GAME_MSG_HARVEST_ENTER_MINE, ent->goalentity);
        unit_setmove(ent, &harvestgold_move_minegold);
        ent->wait = MINING_DURATION;
        ent->s.renderfx |= RF_HIDDEN;
        ent->goalentity->peonsinside++;
    } else {
        harvestgold_wait(ent);
    }
}

void harvestgold_walkback(LPEDICT ent) {
    ent->goalentity->peonsinside--;
    ent->s.renderfx |= RF_HAS_GOLD;
    ent->s.renderfx &= ~RF_HIDDEN;
    ent->harvested_gold += HARVEST_GOLD_CAPACITY;
    FILTER_EDICTS(other, other->goalentity == ent->goalentity &&
                  other->currentmove == &harvestgold_move_wait)
    {
        harvestgold_minegold(other);
    }
    LPEDICT dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_GOLD);
    if (dropoff) {
        G_PublishMessage(ent, GAME_MSG_HARVEST_RETURN_GOLD, dropoff);
        ent->goalentity = dropoff;
        unit_setmove(ent, &harvestgold_move_walkback);
    } else {
        ent->stand(ent);
    }
}

void harvestgold_wait(LPEDICT ent) {
    unit_setmove(ent, &harvestgold_move_wait);
}

void harvest_gold_start(LPEDICT self, LPEDICT target) {
    self->goalentity = target;
    self->secondarygoal = target;
    G_PublishMessage(self, GAME_MSG_HARVEST_MOVE_GOLD, target);
    harvestgold_walk(self);
}

void SP_ability_goldmine(LPCSTR classname, ability_t *self) {
    MAX_GOLD = AB_Data(classname, 1, 1);        /* Max Gold */
    MINING_DURATION = AB_Data(classname, 1, 2); /* Mining Duration (s) */
    MINING_CAPACITY = AB_Data(classname, 1, 3); /* Max Peons Inside */
}

ability_t a_goldmine = {
    .init = SP_ability_goldmine,
};

/* ---- Overlayed Gold Mine (Agl2): same as basic mine with overlay -------- */
ability_t a_goldmine_overlayed = {
    /* Agl2 has no AbilityData row; reusing Agld's initializer reset the shared
     * mining capacity to zero after Agld initialized and stranded every worker. */
};

/* ---- Entangle Gold Mine (Aent): NE transforms ownership of a mine ------- */
static BOOL entangle_goldmine_selecttarget(LPEDICT clent, LPEDICT target) {
    if (!target || !G_ActorHasSkill(target, "Agld")) {
        return false;
    }
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    if (!caster) {
        return false;
    }
    /* Transfer mine ownership to the caster's player. */
    target->s.player = caster->s.player;
    return true;
}

static void entangle_goldmine_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = entangle_goldmine_selecttarget;
}

ability_t a_entangle_goldmine = {
    .cmd = entangle_goldmine_command,
};

/* ---- Entangled Mine (Aegm): passive marker on the mine unit ------------- */
ability_t a_entangled_mine = {0};

/* ---- Blighted Gold Mine (Abgm): interval-based income for Undead -------- */
static FLOAT blight_gold_per_interval;
static FLOAT blight_interval_duration;

void blight_mine_think(LPEDICT ent) {
    monster_think(ent);
    DWORD now = gi.GetTime();
    if (ent->freetime && now < ent->freetime)
        return;
    LPPLAYER player = G_GetPlayerByNumber(ent->s.player);

    if (!player || ent->health.value <= 0) {
        return;
    }
    /* Add gold income directly to the player. */
    player->stats[PLAYERSTATE_RESOURCE_GOLD] += (DWORD)blight_gold_per_interval;
    ent->freetime = now + (DWORD)(blight_interval_duration * 1000.0f);
}

static void SP_ability_blighted_goldmine(LPCSTR classname, ability_t *self) {
    blight_gold_per_interval = G_AbilityDataName(classname)->data[0][0];
    blight_interval_duration = G_AbilityDataName(classname)->data[0][1];
}

ability_t a_blighted_goldmine = {
    .init = SP_ability_blighted_goldmine,
};
