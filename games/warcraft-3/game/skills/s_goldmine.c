#include "s_skills.h"

FLOAT MAX_GOLD;
FLOAT MINING_DURATION;
FLOAT MINING_CAPACITY;
extern FLOAT HARVEST_GOLD_CAPACITY;

void harvestgold_walkback(LPEDICT ent);
void harvestgold_walk(LPEDICT ent);
void harvestgold_wait(LPEDICT ent);
void harvestgold_minegold(LPEDICT ent);

LPEDICT find_townhall(LPEDICT unit) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (ent->class_id == MAKEFOURCC('h', 't', 'o', 'w') &&
            ent->s.player == unit->s.player)
        {
            return ent;
        }
    }
    return NULL;
}

static void ai_walkmine(LPEDICT ent) {
    if (M_DistanceToGoal(ent) <  ent->collision + 180) {
        harvestgold_minegold(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static void ai_goldmine_walkback(LPEDICT ent) {
    if (M_DistanceToGoal(ent) < (ent->collision + ent->goalentity->collision + 5)) {
        ent->goalentity = ent->secondarygoal;
        LPPLAYER player = G_GetPlayerByNumber(ent->s.player);
        if (player) {
            player->stats[PLAYERSTATE_RESOURCE_GOLD] += ent->harvested_gold;
        }
        ent->s.renderfx &= ~RF_HAS_GOLD;
        ent->harvested_gold = 0;
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
    LPEDICT townhall = find_townhall(ent);
    if (townhall) {
        ent->goalentity = townhall;
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
    harvestgold_walk(self);
}

void SP_ability_goldmine(LPCSTR classname, ability_t *self) {
    MAX_GOLD = AB_Number(classname, "DataA1");        /* Max Gold */
    MINING_DURATION = AB_Number(classname, "DataB1");  /* Mining Duration */
    MINING_CAPACITY = AB_Number(classname, "DataC1");  /* Mining Capacity */
}

ability_t a_goldmine = {
    .init = SP_ability_goldmine,
};
