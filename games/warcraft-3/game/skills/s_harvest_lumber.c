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

static int harvest_path_debug_level(void) {
    LPCSTR value;
    if (!gi.CvarString)
        return 0;
    value = gi.CvarString("wc3_harvest_path_debug", "0");
    return value ? atoi(value) : 0;
}

#define HARVEST_PATH_LOG(LEVEL, ...) do { \
    if (harvest_path_debug_level() >= (LEVEL)) { \
        fprintf(stderr, "WC3_HARVEST_PATH " __VA_ARGS__); \
    } \
} while (0)

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

/* Retail WC3 continues lumber work when the explicitly clicked tree is alive
 * but cannot be reached.  Keep target selection in Harvest: routing reports
 * failure/exhaustion, then Harvest chooses a replacement tree.  Prefer a tree
 * already in chop range; otherwise require a static, collision-sized straight
 * route to a legal approach point that is itself within HARVEST_RANGE.  This
 * avoids full flow-field builds for every candidate while still rejecting the
 * buried interior trees that caused the original orbit. */
static BOOL tree_has_reachable_harvest_approach(LPEDICT ent, LPEDICT tree) {
    VECTOR2 approach;
    FLOAT const distance = Vector2_distance(&ent->s.origin2, &tree->s.origin2);

    if (distance <= HARVEST_RANGE)
        return true;
    if (!CM_ClosestPathablePointForRadius(&tree->s.origin2, ent->collision, &approach))
        return false;
    if (Vector2_distance(&approach, &tree->s.origin2) > HARVEST_RANGE)
        return false;
    return CM_LineIsWalkableForRadius(&ent->s.origin2, &approach, ent->collision);
}

static LPEDICT find_reachable_replacement_tree(LPEDICT ent, LPEDICT exclude) {
    FLOAT min_dist = HARVEST_SEARCH_RANGE;
    LPEDICT other = NULL;

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT tree = globals.edicts + i;
        FLOAT dist;
        BOOL reachable;

        if (tree == exclude || tree->targtype != TARG_TREE || M_IsDead(tree))
            continue;
        dist = Vector2_distance(&ent->s.origin2, &tree->s.origin2);
        if (dist >= min_dist)
            continue;
        reachable = tree_has_reachable_harvest_approach(ent, tree);
        HARVEST_PATH_LOG(2,
            "candidate worker=%d failed_target=%d candidate=%d distance=%.1f reachable=%d\n",
            ent->s.number, exclude ? exclude->s.number : -1, tree->s.number,
            dist, reachable);
        if (!reachable)
            continue;
        other = tree;
        min_dist = dist;
    }
    return other;
}

static void harvest_route_failed(LPEDICT ent, LPCSTR reason) {
    LPEDICT failed = ent->goalentity;
    LPEDICT other = find_reachable_replacement_tree(ent, failed);

    if (other) {
        HARVEST_PATH_LOG(1,
            "fallback worker=%d old_target=%d new_target=%d reason=%s worker_pos=(%.1f,%.1f)\n",
            ent->s.number, failed ? failed->s.number : -1, other->s.number, reason,
            ent->s.origin2.x, ent->s.origin2.y);
        harvest_start(ent, other);
        return;
    }

    HARVEST_PATH_LOG(1,
        "stop worker=%d target=%d reason=%s no_reachable_tree=1 worker_pos=(%.1f,%.1f)\n",
        ent->s.number, failed ? failed->s.number : -1, reason,
        ent->s.origin2.x, ent->s.origin2.y);
    ent->stand(ent);
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
    FLOAT const distance = M_DistanceToGoal(ent);
    FLOAT const step = unit_movedistance(ent);

    if (!ent->goalentity || M_IsDead(ent->goalentity)) {
        HARVEST_PATH_LOG(1, "invalid worker=%d target=%d reason=dead_or_missing\n",
                         ent->s.number, ent->goalentity ? ent->goalentity->s.number : -1);
        look_for_another_tree(ent);
    } else if (distance > HARVEST_RANGE) {
        if (move_is_blocked(ent, distance, step)) {
            harvest_route_failed(ent, "movement_blocked");
            return;
        }
        unit_changeangle_for_radius(ent, ent->collision);
        HARVEST_PATH_LOG(2,
            "approach worker=%d target=%d worker_pos=(%.1f,%.1f) target_pos=(%.1f,%.1f) "
            "distance=%.1f range=%.1f step=%.1f blocked_frames=%u direct=%d flow=%u flow_goal=%d flow_unreachable=%d\n",
            ent->s.number, ent->goalentity->s.number,
            ent->s.origin2.x, ent->s.origin2.y,
            ent->goalentity->s.origin2.x, ent->goalentity->s.origin2.y,
            distance, HARVEST_RANGE, step, ent->movement.blocked_frames,
            ent->movement.flow_direct, ent->movement.flow_generation,
            ent->movement.flow_goal_reached, ent->movement.flow_unreachable);
        if (ent->movement.flow_goal_reached) {
            harvest_route_failed(ent, "route_goal_out_of_range");
            return;
        }
        if (ent->movement.flow_unreachable) {
            harvest_route_failed(ent, "route_unreachable");
            return;
        }
        unit_moveindirection(ent);
    } else {
        HARVEST_PATH_LOG(1,
            "reached worker=%d target=%d distance=%.1f range=%.1f\n",
            ent->s.number, ent->goalentity->s.number, distance, HARVEST_RANGE);
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
    move_reset_progress(self);
    HARVEST_PATH_LOG(1,
        "start worker=%d target=%d worker_pos=(%.1f,%.1f) target_pos=(%.1f,%.1f)\n",
        self->s.number, target ? target->s.number : -1,
        self->s.origin2.x, self->s.origin2.y,
        target ? target->s.origin2.x : 0.0f,
        target ? target->s.origin2.y : 0.0f);
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
    if (S_GoldMineIsMine(target)) {
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
