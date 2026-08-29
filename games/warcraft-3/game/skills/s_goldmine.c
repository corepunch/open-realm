#include "s_skills.h"

extern FLOAT HARVEST_GOLD_CAPACITY;

void harvestgold_walkback(LPEDICT ent);
void harvestgold_walk(LPEDICT ent);
void harvestgold_wait(LPEDICT ent);
void harvestgold_minegold(LPEDICT ent);
static umove_t harvestgold_move_wait;

static AbilityData_t const *goldmine_ability_data(LPCEDICT mine) {
    LPCSTR abilities;

    if (!mine || !mine->UnitAbilities || !(abilities = mine->UnitAbilities->abilList))
        return NULL;

    PARSE_LIST(abilities, abil, parse_segment) {
        if (G_AbilityCodeName(abil) == MAKEFOURCC('A', 'g', 'l', 'd'))
            return G_AbilityDataName(abil);
    }
    return NULL;
}

BOOL S_GoldMineIsMine(LPCEDICT mine) {
    return goldmine_ability_data(mine) != NULL;
}

DWORD S_GoldMineMaximumGold(LPCEDICT mine) {
    AbilityData_t const *data = goldmine_ability_data(mine);
    if (!data || data->data[0][0] <= 0)
        return 0;
    return (DWORD)data->data[0][0];
}

FLOAT S_GoldMineMiningDuration(LPCEDICT mine) {
    AbilityData_t const *data = goldmine_ability_data(mine);
    return data ? MAX(0.0f, data->data[0][1]) : 0.0f;
}

DWORD S_GoldMineCapacity(LPCEDICT mine) {
    AbilityData_t const *data = goldmine_ability_data(mine);
    if (!data || data->data[0][2] <= 0)
        return 0;
    return (DWORD)data->data[0][2];
}

BOOL S_GoldMineCanHarvest(LPCEDICT mine) {
    return mine && mine->inuse && mine->health.value > 0 && S_GoldMineIsMine(mine) && mine->resources > 0;
}

BOOL S_GoldMineWorkerIsInside(LPCEDICT worker) {
    return worker && worker->goldmine.mine != NULL;
}

void S_GoldMineInitUnit(LPEDICT mine) {
    DWORD maximum;

    if (!S_GoldMineIsMine(mine))
        return;
    maximum = S_GoldMineMaximumGold(mine);
    if (mine->resources == 0 && maximum > 0)
        mine->resources = maximum;
}

static BOOL goldmine_membership_valid(LPCEDICT worker, LPCEDICT mine) {
    return worker && mine && worker->goldmine.mine == mine && mine->inuse &&
        worker->goldmine.mine_spawn_time == mine->spawn_time;
}

static void goldmine_register_miner(LPEDICT worker, LPEDICT mine) {
    worker->goldmine.mine = mine;
    worker->goldmine.mine_spawn_time = mine->spawn_time;
    worker->goldmine.restore_invulnerable = worker->invulnerable;
    worker->invulnerable = true;
    worker->s.renderfx |= RF_HIDDEN;
    mine->peonsinside++;
}

static LPEDICT goldmine_unregister_miner(LPEDICT worker) {
    LPEDICT mine;

    if (!worker || !(mine = worker->goldmine.mine))
        return NULL;
    if (goldmine_membership_valid(worker, mine) && mine->peonsinside > 0)
        mine->peonsinside--;
    worker->goldmine.mine = NULL;
    worker->goldmine.mine_spawn_time = 0;
    worker->invulnerable = worker->goldmine.restore_invulnerable;
    worker->goldmine.restore_invulnerable = false;
    worker->s.renderfx &= ~RF_HIDDEN;
    return mine;
}

static void goldmine_deplete(LPEDICT mine) {
    if (!mine || !mine->inuse || mine->resources > 0 || M_IsDead(mine))
        return;
    mine->health.value = 0;
    if (mine->die)
        mine->die(mine, NULL);
    else
        mine->svflags |= SVF_DEADMONSTER;
}

static void goldmine_wake_waiters(LPEDICT mine) {
    /* Called immediately after goldmine_deplete; checks mine->inuse so a
     * synchronous G_FreeEdict in die() does not iterate freed memory. */
    if (!mine || !mine->inuse)
        return;
    FILTER_EDICTS(other, other->goalentity == mine &&
                  other->currentmove == &harvestgold_move_wait)
    {
        harvestgold_minegold(other);
    }
}

void S_GoldMineReleaseWorker(LPEDICT worker) {
    LPEDICT mine;

    if (!S_GoldMineWorkerIsInside(worker))
        return;
    mine = goldmine_unregister_miner(worker);
    goldmine_wake_waiters(mine);
}


static void ai_walkmine(LPEDICT ent) {
    if (!S_GoldMineCanHarvest(ent->goalentity)) {
        ent->stand(ent);
        return;
    }
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
        if (S_GoldMineCanHarvest(ent->goalentity)) {
            G_PublishMessage(ent, GAME_MSG_HARVEST_RESUME_GOLD, ent->goalentity);
            harvestgold_walk(ent);
        } else {
            ent->stand(ent);
        }
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
    LPEDICT mine = ent ? ent->goalentity : NULL;
    DWORD capacity;

    if (!ent || !S_GoldMineCanHarvest(mine)) {
        if (ent) ent->stand(ent);
        return;
    }
    if (S_GoldMineWorkerIsInside(ent))
        return;

    capacity = S_GoldMineCapacity(mine);
    if (capacity == 0) {
        ent->stand(ent);
        return;
    }
    if (mine->peonsinside < capacity) {
        G_PublishMessage(ent, GAME_MSG_HARVEST_ENTER_MINE, mine);
        unit_setmove(ent, &harvestgold_move_minegold);
        ent->wait = S_GoldMineMiningDuration(mine);
        goldmine_register_miner(ent, mine);
    } else {
        harvestgold_wait(ent);
    }
}

void harvestgold_walkback(LPEDICT ent) {
    LPEDICT mine;
    DWORD amount = 0;
    DWORD carry_capacity;

    if (!ent || !(mine = ent->goldmine.mine)) {
        if (ent) ent->stand(ent);
        return;
    }

    if (goldmine_membership_valid(ent, mine) && !M_IsDead(mine) && S_GoldMineIsMine(mine)) {
        carry_capacity = HARVEST_GOLD_CAPACITY > 0 ? (DWORD)HARVEST_GOLD_CAPACITY : 0;
        amount = MIN(mine->resources, carry_capacity);
        mine->resources -= amount;
    }

    goldmine_unregister_miner(ent);
    if (mine->inuse && mine->resources == 0)
        goldmine_deplete(mine);
    goldmine_wake_waiters(mine);

    if (amount == 0) {
        ent->stand(ent);
        return;
    }

    ent->s.renderfx |= RF_HAS_GOLD;
    ent->harvested_gold += amount;
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

ability_t a_goldmine = {0};

/* ---- Overlayed Gold Mine (Agl2): same as basic mine with overlay -------- */
ability_t a_goldmine_overlayed = {0};

/* ---- Entangle Gold Mine (Aent): NE transforms ownership of a mine ------- */
static BOOL entangle_goldmine_selecttarget(LPEDICT clent, LPEDICT target) {
    if (!target || !S_GoldMineIsMine(target)) {
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
