#include "s_skills.h"

FLOAT HARVEST_LUMBER_CAPACITY;
FLOAT HARVEST_GOLD_CAPACITY;
FLOAT HARVEST_TREE_DAMAGE;
FLOAT HARVEST_RANGE;
FLOAT HARVEST_COOLDOWN;
FLOAT HARVEST_SEARCH_RANGE;

void harvest_cooldown(LPEDICT ent);
void harvest_swing(LPEDICT ent);
void harvest_walkback(LPEDICT ent);
void harvest_walk(LPEDICT ent);

void harvest_start(LPEDICT self, LPEDICT target);
void harvest_gold_start(LPEDICT self, LPEDICT target);
LPEDICT find_townhall(LPEDICT unit);
    
static LPEDICT find_another_tree(LPEDICT ent) {
    FLOAT min_dist = HARVEST_SEARCH_RANGE;
    LPEDICT other = NULL;
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT tree = globals.edicts+i;
        if (tree->targtype != TARG_TREE || M_IsDead(tree))
            continue;
        FLOAT dist = Vector2_distance(&ent->s.origin2, &tree->s.origin2);
        if (dist < min_dist) {
            other = tree;
            min_dist = dist;
        }
    }
    return other;
}

static void look_for_another_tree(LPEDICT ent) {
    LPEDICT other = find_another_tree(ent);
    if (other) {
        harvest_start(ent, other);
    } else {
        ent->stand(ent);
    }
}

BOOL G_ActorHasSkill(LPEDICT ent, LPCSTR id) {
    LPCSTR abilities = UNIT_ABILITIES_NORMAL(ent->class_id);
    if (abilities) {
        PARSE_LIST(abilities, abil, parse_segment) {
            if (!strcmp(abil, id))
                return true;
        }
    }
    return false;
}

static void ai_walktree(LPEDICT ent) {
    if (M_DistanceToGoal(ent) > HARVEST_RANGE) {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    } else if (M_IsDead(ent->goalentity)) {
        look_for_another_tree(ent);
    } else {
        harvest_swing(ent);
    }
}

static void ai_harvest_walkback(LPEDICT ent) {
    if (M_DistanceToGoal(ent) < (ent->collision + ent->goalentity->collision + 5)) {
        ent->goalentity = ent->secondarygoal;
        LPPLAYER player = G_GetPlayerByNumber(ent->s.player);
        if (player) {
            player->stats[PLAYERSTATE_RESOURCE_LUMBER] += ent->harvested_lumber;
        }
        ent->s.renderfx &= ~RF_HAS_LUMBER;
        ent->harvested_lumber = 0;
        harvest_walk(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static void ai_chop(LPEDICT ent) {
    LPEDICT tree = ent->secondarygoal;
    if (!M_IsDead(tree)) {
        ent->harvested_lumber += HARVEST_TREE_DAMAGE;
        ent->s.renderfx |= RF_HAS_LUMBER;
    }
    if (tree->health.value < HARVEST_TREE_DAMAGE) {
        tree->health.value = 0;
        tree->die(tree, ent);
    } else if (tree->pain) {
        tree->health.value -= HARVEST_TREE_DAMAGE;
        tree->pain(tree);
    }
}

static void ai_swing(LPEDICT ent) {
    unit_runwait(ent, ai_chop);
}

static void ai_cooldown(LPEDICT ent) {
    unit_runwait(ent, harvest_swing);
}

static umove_t harvest_move_walk = { "walk", ai_walktree, NULL, &a_harvest };
static umove_t harvest_move_walkback = { "walk", ai_harvest_walkback, NULL, &a_harvest };
static umove_t harvest_move_swing = { "attack", ai_swing, harvest_cooldown, &a_harvest };
static umove_t harvest_move_cooldown = { "stand ready", ai_cooldown, NULL, &a_harvest };

void harvest_cooldown(LPEDICT ent) {
    if (ent->harvested_lumber >= HARVEST_LUMBER_CAPACITY) {
        harvest_walkback(ent);
    } else if (M_IsDead(ent->goalentity)) {
        look_for_another_tree(ent);
    } else {
        unit_setmove(ent, &harvest_move_cooldown);
        ent->wait = HARVEST_COOLDOWN;
    }
}

void harvest_walk(LPEDICT ent) {
    unit_setmove(ent, &harvest_move_walk);
}

void harvest_swing(LPEDICT ent) {
    unit_setmove(ent, &harvest_move_swing);
    ent->wait = UNIT_ATTACK1_DAMAGE_POINT(ent->class_id);
}

void harvest_walkback(LPEDICT ent) {
    LPEDICT townhall = find_townhall(ent);
    if (townhall) {
        ent->goalentity = townhall;
        unit_setmove(ent, &harvest_move_walkback);
    } else {
        ent->stand(ent);
    }
}

void CMD_Harvest(LPEDICT ent);

void harvest_start(LPEDICT self, LPEDICT target) {
    self->goalentity = target;
    self->secondarygoal = target;
    harvest_walk(self);
}

BOOL harvest_menu_selecttarget(LPEDICT clent, LPEDICT target) {
    if (G_ActorHasSkill(target, "Agld")) {
        FOR_SELECTED_UNITS(clent->client, ent) {
            harvest_gold_start(ent, target);
        }
    } else if (target->targtype == TARG_TREE) {
        FOR_SELECTED_UNITS(clent->client, ent) {
            harvest_start(ent, target);
        }
    }
    return true;
}

void harvest_command(LPEDICT ent) {
    UI_AddCancelButton(ent);
    ent->client->menu.on_entity_selected = harvest_menu_selecttarget;
}

void SP_ability_harvest(LPCSTR classname, ability_t *self) {
    HARVEST_LUMBER_CAPACITY = AB_Number(classname, "DataB1"); /* Lumber Capacity */
    HARVEST_GOLD_CAPACITY = AB_Number(classname, "DataC1");   /* Gold Capacity */
    HARVEST_TREE_DAMAGE = AB_Number(classname, "DataA1");     /* Damage to Tree */
    HARVEST_RANGE = AB_Number(classname, "Rng1");
    HARVEST_COOLDOWN = AB_Number(classname, "Dur1");
    HARVEST_SEARCH_RANGE = AB_Number(classname, "Area1");
}

ability_t a_harvest = {
    .init = SP_ability_harvest,
    .cmd = harvest_command,
};
