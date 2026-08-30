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

static DWORD return_resources_mask(LPCSTR ability) {
    static struct { LPCSTR name; DWORD mask; } const artn_aliases[] = {
        { "Argd", RETURN_RESOURCE_GOLD },
        { "Arlm", RETURN_RESOURCE_LUMBER },
        { "Argl", RETURN_RESOURCE_GOLD | RETURN_RESOURCE_LUMBER },
    };
    AbilityData_t const *data;
    DWORD mask = 0;
    int i;

    /* Stock aliases map directly without a full AbilityData table. */
    for (i = 0; i < (int)(sizeof(artn_aliases) / sizeof(artn_aliases[0])); i++) {
        if (!strcmp(ability, artn_aliases[i].name))
            return artn_aliases[i].mask;
    }

    if (G_AbilityCodeName(ability) != MAKEFOURCC('A', 'r', 't', 'n'))
        return 0;

    data = G_AbilityDataName(ability);
    if (!data)
        return 0;
    if (data->data[0][0]) mask |= RETURN_RESOURCE_GOLD;
    if (data->data[0][1]) mask |= RETURN_RESOURCE_LUMBER;
    return mask;
}

BOOL S_CanReturnResourceAt(LPEDICT unit, LPEDICT building, returnResource_t resource) {
    LPCSTR abilities;

    if (!unit || !building || !building->inuse || building->s.player != unit->s.player || M_IsDead(building))
        return false;
    if (!building->UnitAbilities || !(abilities = building->UnitAbilities->abilList))
        return false;

    PARSE_LIST(abilities, abil, parse_segment) {
        if (return_resources_mask(abil) & resource)
            return true;
    }
    return false;
}

LPEDICT S_FindNearestResourceDropoff(LPEDICT unit, returnResource_t resource) {
    LPEDICT best = NULL;
    FLOAT best_dist = 0;

    /* TODO: use pathfinding distance; geometric distance misjudges across impassable terrain */
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT building = &globals.edicts[i];
        FLOAT dist;
        if (!S_CanReturnResourceAt(unit, building, resource))
            continue;
        dist = Vector2_distance(&unit->s.origin2, &building->s.origin2);
        if (!best || dist < best_dist) {
            best = building;
            best_dist = dist;
        }
    }
    return best;
}

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
    LPCSTR abilities = ent->UnitAbilities->abilList;
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
        G_PublishMessage(ent, GAME_MSG_HARVEST_START_CHOP, ent->goalentity);
        harvest_swing(ent);
    }
}

static void ai_harvest_walkback(LPEDICT ent) {
    if (!S_CanReturnResourceAt(ent, ent->goalentity, RETURN_RESOURCE_LUMBER)) {
        LPEDICT dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_LUMBER);
        if (!dropoff) {
            ent->stand(ent);
            return;
        }
        G_PublishMessage(ent, GAME_MSG_HARVEST_RETURN_LUMBER, dropoff);
        ent->goalentity = dropoff;
    }

    FLOAT const dist = M_DistanceToGoal(ent);
    FLOAT const contact = ent->collision + ent->goalentity->collision;
    FLOAT const step = unit_movedistance(ent);

    /* Building pathing can block the next movement step before the worker
     * reaches physical contact. Deposit once that next step would cross the
     * contact boundary, matching the resource interaction rule. */
    if (dist <= contact + step) {
        LPEDICT dropoff = ent->goalentity;
        G_PublishMessage(ent, GAME_MSG_HARVEST_DEPOSIT_LUMBER, dropoff);
        LPPLAYER player = G_GetPlayerByNumber(ent->s.player);
        if (player) {
            player->stats[PLAYERSTATE_RESOURCE_LUMBER] += ent->harvested_lumber;
        }
        ent->s.renderfx &= ~RF_HAS_LUMBER;
        ent->harvested_lumber = 0;
        /* Resolve the next live tree at the deposit boundary.  Resuming with
         * the felled tree left it as the worker's active goal for another tick. */
        LPEDICT tree = ent->secondarygoal;
        if (!tree || M_IsDead(tree))
            tree = find_another_tree(ent);
        ent->goalentity = ent->secondarygoal = tree;
        if (tree) {
            G_PublishMessage(ent, GAME_MSG_HARVEST_RESUME_LUMBER, tree);
            harvest_walk(ent);
        } else {
            ent->stand(ent);
        }
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static void ai_chop(LPEDICT ent) {
    LPEDICT tree = ent->secondarygoal;

    G_PublishMessage(ent, GAME_MSG_HARVEST_CHOP, tree);
    if (tree && !M_IsDead(tree)) {
        ent->harvested_lumber += HARVEST_TREE_DAMAGE;
        ent->s.renderfx |= RF_HAS_LUMBER;
    }
    BOOL felled = G_DestructableApplyDamage(tree, ent, HARVEST_TREE_DAMAGE);
    /* Tree-fall supersedes chop: play one-shot EV_ATTACK sound for all clients. */
    if (felled && g_numTreeFallSounds) {
        G_PublishMessage(ent, GAME_MSG_HARVEST_TREE_FELLED, tree);
        ent->s.event = EV_ATTACK;
        ent->s.sound = g_treeFallSounds[rand() % g_numTreeFallSounds];
    } else if (ent->sound.num_chop) {
        ent->s.event = EV_ATTACK;
        ent->s.sound = ent->sound.chop[rand() % ent->sound.num_chop];
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
    ent->wait = ent->UnitWeapons->attack1.damagePoint;
}

void harvest_walkback(LPEDICT ent) {
    LPEDICT dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_LUMBER);
    if (dropoff) {
        G_PublishMessage(ent, GAME_MSG_HARVEST_RETURN_LUMBER, dropoff);
        ent->goalentity = dropoff;
        unit_setmove(ent, &harvest_move_walkback);
    } else {
        ent->stand(ent);
    }
}

void CMD_Harvest(LPEDICT ent);

void harvest_start(LPEDICT self, LPEDICT target) {
    self->goalentity = target;
    self->secondarygoal = target;
    G_PublishMessage(self, GAME_MSG_HARVEST_MOVE_LUMBER, target);
    harvest_walk(self);
}

/* ---- Wisp harvest: walk to tree, gather lumber, wisp dies ---------------- */
static FLOAT wisp_lumber_per_interval;
static DWORD wisp_interval_count;

static void ai_wisp_mine(LPEDICT ent) {
    unit_runwait(ent, NULL);
    /* Wisp gathers lumber and is consumed. */
    LPPLAYER player = G_GetPlayerByNumber(ent->s.player);
    if (player) {
        player->stats[PLAYERSTATE_RESOURCE_LUMBER] += (DWORD)wisp_lumber_per_interval;
    }
    ent->health.value = 0;
    if (ent->die) {
        ent->die(ent, ent);
    }
}

static umove_t wisp_harvest_mine = { "stand", ai_wisp_mine, NULL, &a_wisp_harvest };

static void ai_wisp_walktree(LPEDICT ent) {
    if (M_DistanceToGoal(ent) > HARVEST_RANGE) {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    } else {
        unit_setmove(ent, &wisp_harvest_mine);
        ent->wait = 1.0f;
    }
}

static umove_t wisp_harvest_walk = { "walk", ai_wisp_walktree, NULL, &a_wisp_harvest };

void wisp_harvest_start(LPEDICT self, LPEDICT target) {
    self->goalentity = target;
    unit_setmove(self, &wisp_harvest_walk);
}

static BOOL wisp_harvest_selecttarget(LPEDICT clent, LPEDICT target) {
    if (!target || target->targtype != TARG_TREE || M_IsDead(target)) {
        return false;
    }
    FOR_SELECTED_UNITS(clent->client, ent) {
        wisp_harvest_start(ent, target);
    }
    return true;
}

static void wisp_harvest_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = wisp_harvest_selecttarget;
}

static void SP_ability_wisp_harvest(LPCSTR classname, ability_t *self) {
    wisp_lumber_per_interval = G_AbilityDataName(classname)->data[0][0];
    wisp_interval_count = (DWORD)G_AbilityDataName(classname)->data[0][1];
}

ability_t a_wisp_harvest = {
    .init = SP_ability_wisp_harvest,
    .cmd = wisp_harvest_command,
};

/* ---- Acolyte harvest: target blighted gold mine ------------------------- */
static BOOL acolyte_harvest_selecttarget(LPEDICT clent, LPEDICT target) {
    if (!target || !G_ActorHasSkill(target, "Abgm")) {
        return false;
    }
    FOR_SELECTED_UNITS(clent->client, ent) {
        harvest_gold_start(ent, target);
    }
    return true;
}

static void acolyte_harvest_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = acolyte_harvest_selecttarget;
}

ability_t a_acolyte_harvest = {
    .cmd = acolyte_harvest_command,
};

/* ---- Return Resources: standalone command to deposit carried resources --- */
static void return_resources_command(LPEDICT clent) {
    FOR_SELECTED_UNITS(clent->client, ent) {
        if (ent->harvested_lumber > 0 || ent->harvested_gold > 0) {
            harvest_walkback(ent);
        }
    }
}

ability_t a_return_resources = {
    .cmd = return_resources_command,
};

/* ---- Harvest menu dispatch (extended for wisp/acolyte) ------------------ */
BOOL harvest_menu_selecttarget(LPEDICT clent, LPEDICT target) {
    if (G_ActorHasSkill(target, "Agld")) {
        FOR_SELECTED_UNITS(clent->client, ent) {
            harvest_gold_start(ent, target);
        }
    } else if (G_ActorHasSkill(target, "Abgm")) {
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
    HARVEST_TREE_DAMAGE = AB_Data(classname, 1, 1);     /* lumber/tree-HP per swing */
    HARVEST_LUMBER_CAPACITY = AB_Data(classname, 1, 2); /* max lumber to carry */
    HARVEST_GOLD_CAPACITY = AB_Data(classname, 1, 3);
    HARVEST_RANGE = G_AbilityDataName(classname)->range[0];
    HARVEST_COOLDOWN = G_AbilityDataName(classname)->dur[0];
    HARVEST_SEARCH_RANGE = G_AbilityDataName(classname)->area[0];
}

ability_t a_harvest = {
    .init = SP_ability_harvest,
    .cmd = harvest_command,
};
