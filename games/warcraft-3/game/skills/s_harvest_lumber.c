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
    if (!building->data.UnitAbilities || !(abilities = building->data.UnitAbilities->abilList))
        return false;

    PARSE_LIST(abilities, abil, parse_segment) {
        if (return_resources_mask(abil) & resource)
            return true;
    }
    return false;
}

void S_SetCarriedResource(LPEDICT unit, returnResource_t resource, DWORD amount) {
    BOOL const was_carrying = unit && (unit->harvested_gold > 0 || unit->harvested_lumber > 0);
    BOOL is_carrying;

    if (!unit)
        return;

    /* A worker carries exactly one visible resource type.  Keep the gameplay
     * counters and renderer flags in one transition so stale lumber/gold tags
     * cannot survive a resource switch or completed deposit. */
    unit->harvested_gold = 0;
    unit->harvested_lumber = 0;
    unit->s.renderfx &= ~(RF_HAS_GOLD | RF_HAS_LUMBER);

    if (amount && resource == RETURN_RESOURCE_GOLD) {
        unit->harvested_gold = amount;
        unit->s.renderfx |= RF_HAS_GOLD;
    } else if (amount && resource == RETURN_RESOURCE_LUMBER) {
        unit->harvested_lumber = amount;
        unit->s.renderfx |= RF_HAS_LUMBER;
    }

    /* Warsmash exposes Harvest as one toggled ability: empty workers use the
     * normal Gather UI, while any positive carried amount uses the Un* Return
     * Resources UI.  Refresh only when that boolean presentation state flips. */
    is_carrying = unit->harvested_gold > 0 || unit->harvested_lumber > 0;
    if (was_carrying != is_carrying) {
        FOR_LOOP(i, game.max_clients) {
            LPGAMECLIENT client = game.clients + i;
            if (G_IsEntitySelected(client, unit))
                G_InvalidateCommands(client);
        }
    }
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

static LPEDICT find_another_tree_near(LPCVECTOR2 origin) {
    FLOAT min_dist = HARVEST_SEARCH_RANGE;
    LPEDICT other = NULL;

    if (!origin)
        return NULL;

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT tree = globals.edicts + i;
        FLOAT dist;

        if (tree->targtype != TARG_TREE || M_IsDead(tree))
            continue;
        dist = Vector2_distance(origin, &tree->s.origin2);
        if (dist < min_dist) {
            other = tree;
            min_dist = dist;
        }
    }
    return other;
}

static LPEDICT find_another_tree(LPEDICT ent) {
    return ent ? find_another_tree_near(&ent->s.origin2) : NULL;
}

/* Automatic harvest orders have no explicit target.  Retail resolves them
 * from the worker's current position, then continues through the ordinary
 * targeted Harvest state machine.  Keep target discovery here so JASS/order
 * dispatch does not need to know what counts as a live resource. */
static LPEDICT harvest_find_nearest_resource(LPEDICT worker, returnResource_t resource) {
    LPEDICT best = NULL;
    FLOAT best_dist = 0.0f;

    if (!worker)
        return NULL;

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT target = globals.edicts + i;
        FLOAT dist;

        if (resource == RETURN_RESOURCE_GOLD) {
            if (!S_GoldMineCanHarvest(target))
                continue;
        } else if (resource == RETURN_RESOURCE_LUMBER) {
            if (!target->inuse || target->targtype != TARG_TREE || M_IsDead(target))
                continue;
        } else {
            return NULL;
        }

        dist = Vector2_distance(&worker->s.origin2, &target->s.origin2);
        if (!best || dist < best_dist) {
            best = target;
            best_dist = dist;
        }
    }
    return best;
}

/* Return routing needs a stable interaction-side endpoint rather than
 * the drop-off centre.  Restrict the search to the innermost collision-safe
 * pathing-cell ring so the closest candidate is the nearest edge of the Town
 * Hall/Lumber Mill from the worker's current side. */
static BOOL harvest_find_nearest_dropoff_approach(LPEDICT ent, LPEDICT dropoff,
                                                   LPVECTOR2 out) {
    FLOAT const route_band = ent ?
        ent->collision + CM_PathCellWorldSize() * 1.41421356237f : 0.0f;

    if (!ent || !dropoff || !dropoff->pathtex || !out)
        return false;
    return CM_FindInnerApproachPointToFootprintForRadius(
        dropoff, &ent->s.origin2, route_band, ent->collision, out);
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
    return CM_FindDirectApproachPointForRadius(&ent->s.origin2, &tree->s.origin2,
                                               HARVEST_RANGE, ent->collision, &approach);
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

static LONG skill_index(DWORD const *skills, DWORD count, DWORD code) {
    FOR_LOOP(i, count) if (skills[i] == code) return i;
    return -1;
}

static BOOL skill_add(DWORD *skills, DWORD *count, DWORD code) {
    if (*count >= MAX_ABILITIES) {
        fprintf(stderr, "WC3: unit ability list full while adding %08x\n", code); return false;
    }
    skills[(*count)++] = code; return true;
}

static void skill_remove(DWORD *skills, DWORD *count, DWORD index) {
    memmove(skills + index, skills + index + 1, (--*count - index) * sizeof(*skills));
}

static BOOL actor_has_skill(LPEDICT ent, DWORD code) {
    LPCSTR abilities;
    if (!ent || !code) return false;
    if (skill_index(ent->abilities.removed, ARRAY_COUNT(ent->abilities.removed), code) >= 0) return false;
    if (skill_index(ent->abilities.added, ARRAY_COUNT(ent->abilities.added), code) >= 0) return true;
    if (!ent->data.UnitAbilities) return false;
    abilities = ent->data.UnitAbilities->abilList;
    if (abilities) {
        PARSE_LIST(abilities, abil, parse_segment) {
            DWORD static_code = 0;
            if (strlen(abil) == 4) memcpy(&static_code, abil, sizeof(static_code));
            if (static_code == code) return true;
        }
    }
    return false;
}

BOOL G_ActorHasSkill(LPEDICT ent, LPCSTR id) {
    DWORD code = 0;
    if (!id || strlen(id) != 4) return false;
    memcpy(&code, id, sizeof(code));
    return actor_has_skill(ent, code);
}

static BOOL harvest_auto_start(LPEDICT self, returnResource_t resource) {
    LPEDICT target;

    /* autoharvestgold/autoharvestlumber are worker-internal immediate orders,
     * not substitutes for giving Harvest to arbitrary units.  OpenRealm's
     * normal Smart resource path already keys worker authority off Ahar. */
    if (!self || !G_ActorHasSkill(self, "Ahar") || (self->aiflags & AI_IMMOBILE))
        return false;

    target = harvest_find_nearest_resource(self, resource);
    if (!target)
        return false;

    if (resource == RETURN_RESOURCE_GOLD)
        return harvest_gold_order(self, target);
    if (resource == RETURN_RESOURCE_LUMBER) {
        harvest_start(self, target);
        return true;
    }
    return false;
}

BOOL harvest_auto_start_gold(LPEDICT self) {
    return harvest_auto_start(self, RETURN_RESOURCE_GOLD);
}

BOOL harvest_auto_start_lumber(LPEDICT self) {
    return harvest_auto_start(self, RETURN_RESOURCE_LUMBER);
}

BOOL G_ActorAddSkill(LPEDICT ent, DWORD code) {
    LONG index;
    if (!ent || !code || actor_has_skill(ent, code)) return false;
    index = skill_index(ent->abilities.removed, ARRAY_COUNT(ent->abilities.removed), code);
    if (index >= 0) skill_remove(ent->abilities.removed, &ARRAY_COUNT(ent->abilities.removed), index);
    else {
        if (G_AbilityData(code)->id != code) return false;
        if (!skill_add(ent->abilities.added, &ARRAY_COUNT(ent->abilities.added), code)) return false;
    }
    if (code == MAKEFOURCC('A', 'h', 'a', 'r')) G_InvalidateUnitShortcutsForUnit(ent);
    { LPGAMECLIENT client = G_GetPlayerClientByNumber(ent->s.player); if (client) G_InvalidateCommands(client); }
    return true;
}

BOOL G_ActorRemoveSkill(LPEDICT ent, DWORD code) {
    LONG index;
    if (!ent || !code || !actor_has_skill(ent, code)) return false;
    index = skill_index(ent->abilities.added, ARRAY_COUNT(ent->abilities.added), code);
    if (index < 0 && ARRAY_COUNT(ent->abilities.removed) >= MAX_ABILITIES) return false;
    /* Invalidate while Ahar is still present; the shortcut invalidation hook
     * deliberately ignores ordinary non-worker units for low CPU overhead. */
    if (code == MAKEFOURCC('A', 'h', 'a', 'r')) G_InvalidateUnitShortcutsForUnit(ent);
    if (index >= 0) skill_remove(ent->abilities.added, &ARRAY_COUNT(ent->abilities.added), index);
    else if (!skill_add(ent->abilities.removed, &ARRAY_COUNT(ent->abilities.removed), code)) return false;
    index = skill_index(ent->abilities.permanent, ARRAY_COUNT(ent->abilities.permanent), code);
    if (index >= 0) skill_remove(ent->abilities.permanent, &ARRAY_COUNT(ent->abilities.permanent), index);
    { LPGAMECLIENT client = G_GetPlayerClientByNumber(ent->s.player); if (client) G_InvalidateCommands(client); }
    return true;
}

BOOL G_ActorSetSkillPermanent(LPEDICT ent, DWORD code, BOOL permanent) {
    LONG index;
    if (!actor_has_skill(ent, code)) return false;
    index = skill_index(ent->abilities.permanent, ARRAY_COUNT(ent->abilities.permanent), code);
    if (permanent && index < 0 && !skill_add(ent->abilities.permanent, &ARRAY_COUNT(ent->abilities.permanent), code)) return false;
    else if (!permanent && index >= 0) skill_remove(ent->abilities.permanent, &ARRAY_COUNT(ent->abilities.permanent), index);
    return true;
}

BOOL G_ActorSkillPermanent(LPEDICT ent, DWORD code) {
    return ent && skill_index(ent->abilities.permanent, ARRAY_COUNT(ent->abilities.permanent), code) >= 0;
}

void G_FreeActorSkills(LPEDICT ent) {
    if (ent) memset(&ent->abilities, 0, sizeof(ent->abilities));
}

static void ai_walktree(LPEDICT ent) {
    FLOAT const distance = M_DistanceToGoal(ent);

    if (!ent->goalentity || M_IsDead(ent->goalentity)) {
        HARVEST_PATH_LOG(1, "invalid worker=%d target=%d reason=dead_or_missing\n",
                         ent->s.number, ent->goalentity ? ent->goalentity->s.number : -1);
        look_for_another_tree(ent);
    } else if (distance > HARVEST_RANGE) {
        /* Warsmash delegates destructable harvesting to ordinary generic move
         * collision. The Harvest state machine owns target/range semantics. */
        unit_changeangle_for_radius(ent, ent->collision);
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

static void harvest_finish_lumber_deposit(LPEDICT ent) {
    LPEDICT dropoff = ent->goalentity;
    LPEDICT tree;
    LPPLAYER player;

    G_PublishMessage(ent, GAME_MSG_HARVEST_DEPOSIT_LUMBER, dropoff);
    player = G_GetPlayerByNumber(ent->s.player);
    if (player) {
        G_CreditResourceIncome(player, ent, PLAYERSTATE_RESOURCE_LUMBER,
                               (LONG)ent->harvested_lumber);
    }
    S_SetCarriedResource(ent, RETURN_RESOURCE_LUMBER, 0);

    /* Resolve the next live tree at the deposit boundary.  Resuming with the
     * felled tree left it as the worker's active goal for another tick. */
    tree = ent->secondarygoal;
    if (tree && M_IsDead(tree))
        tree = find_another_tree_near(&tree->s.origin2);
    else if (!tree)
        tree = find_another_tree(ent);
    ent->goalentity = ent->secondarygoal = tree;
    if (tree) {
        G_PublishMessage(ent, GAME_MSG_HARVEST_RESUME_LUMBER, tree);
        move_reset_progress(ent);
        harvest_walk(ent);
    } else {
        ent->stand(ent);
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
        move_reset_progress(ent);
    }

    FLOAT const dist = M_DistanceToGoal(ent);
    FLOAT const contact = ent->collision + ent->goalentity->collision;
    FLOAT const step = unit_movedistance(ent);
    FLOAT const footprint_dist = CM_DistanceToPathingFootprint(
        ent->goalentity, &ent->s.origin2);
    BOOL const footprint_deposit = footprint_dist < FLT_MAX &&
                                   footprint_dist <= ent->collision + step;
    BOOL const circle_deposit = dist <= contact + step;

    /* Match gold return: authored building pathing is the authoritative
     * physical boundary when it exists.  A Lumber Mill/Town Hall can block the
     * worker before its scalar collision circle reaches contact, so complete
     * the deposit when one legal step reaches either the footprint or the
     * collision fallback. */
    if (footprint_deposit || circle_deposit) {
        harvest_finish_lumber_deposit(ent);
    } else {
        VECTOR2 approach;

        /* Return Resources owns a building interaction, not movement to its
         * centre. Pick the innermost collision-safe ring and the worker's
         * current side so lumber uses the nearest Town Hall/Lumber Mill edge. */
        if (harvest_find_nearest_dropoff_approach(
                ent, ent->goalentity, &approach)) {
            if (unit_snap_to_point_ignore_units(ent, &approach)) {
                harvest_finish_lumber_deposit(ent);
                return;
            }
            if (unit_changeangle_towards_point_ignore_units(ent, &approach)) {
                unit_moveindirection_ignore_units(ent);
                return;
            }
        }

        unit_changeangle_interaction_ignore_units(ent);
        unit_moveindirection_ignore_units(ent);
    }
}

static void ai_chop(LPEDICT ent) {
    LPEDICT tree = ent->secondarygoal;
    BOOL const valid_hit = tree && G_IsDestructable(tree) && !M_IsDead(tree) &&
                           !tree->invulnerable && HARVEST_TREE_DAMAGE > 0.0f;
    BOOL felled = false;

    G_PublishMessage(ent, GAME_MSG_HARVEST_CHOP, tree);
    if (valid_hit) {
        FLOAT const carried = MIN((FLOAT)ent->harvested_lumber + HARVEST_TREE_DAMAGE,
                                  HARVEST_LUMBER_CAPACITY);

        felled = G_DestructableApplyDamage(tree, ent, HARVEST_TREE_DAMAGE);
        if (carried > ent->harvested_lumber)
            S_SetCarriedResource(ent, RETURN_RESOURCE_LUMBER, (DWORD)carried);
    }
    /* Tree-fall supersedes chop: play one-shot world sound for all clients. */
    if (felled && g_numTreeFallSounds) {
        G_PublishMessage(ent, GAME_MSG_HARVEST_TREE_FELLED, tree);
        gi.Sound(ent, CHAN_BODY, g_treeFallSounds[rand() % g_numTreeFallSounds], 1.0f, 1.0f, 0.0f);
    } else if (ent->sound.num_chop) {
        gi.Sound(ent, CHAN_WEAPON, ent->sound.chop[rand() % ent->sound.num_chop], 1.0f, 1.0f, 0.0f);
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
    ent->wait = ent->data.UnitWeapons->attack1.damagePoint;
}

BOOL harvest_lumber_return_to(LPEDICT ent, LPEDICT dropoff) {
    if (!ent || !dropoff || !ent->harvested_lumber ||
        !S_CanReturnResourceAt(ent, dropoff, RETURN_RESOURCE_LUMBER)) {
        return false;
    }

    G_PublishMessage(ent, GAME_MSG_HARVEST_RETURN_LUMBER, dropoff);
    ent->goalentity = dropoff;
    move_reset_progress(ent);
    unit_setmove(ent, &harvest_move_walkback);
    return true;
}

void harvest_walkback(LPEDICT ent) {
    LPEDICT dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_LUMBER);
    if (!harvest_lumber_return_to(ent, dropoff))
        ent->stand(ent);
}

void CMD_Harvest(LPEDICT ent);

void harvest_start(LPEDICT self, LPEDICT target) {
    self->secondarygoal = target;
    if (self->harvested_lumber >= HARVEST_LUMBER_CAPACITY && self->harvested_lumber > 0) {
        harvest_walkback(self);
        return;
    }
    self->goalentity = target;
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
        G_CreditResourceIncome(player, ent, PLAYERSTATE_RESOURCE_LUMBER,
                               (LONG)wisp_lumber_per_interval);
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
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
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
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
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
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        if (ent->harvested_lumber > 0) {
            harvest_walkback(ent);
        } else if (ent->harvested_gold > 0) {
            LPEDICT dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_GOLD);
            if (!harvest_gold_return_to(ent, dropoff))
                ent->stand(ent);
        }
    }
}

ability_t a_return_resources = {
    .cmd = return_resources_command,
};

/* ---- Harvest menu dispatch (extended for wisp/acolyte) ------------------ */
BOOL harvest_menu_selecttarget(LPEDICT clent, LPEDICT target) {
    if (S_GoldMineIsMine(target)) {
        FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
            harvest_gold_order(ent, target);
        }
    } else if (G_ActorHasSkill(target, "Abgm")) {
        FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
            harvest_gold_start(ent, target);
        }
    } else if (target->targtype == TARG_TREE) {
        FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
            harvest_start(ent, target);
        }
    }
    return true;
}

static BOOL harvest_is_toggle_on(LPEDICT ent) {
    return ent && (ent->harvested_lumber > 0 || ent->harvested_gold > 0);
}

void harvest_command(LPEDICT ent) {
    LPEDICT selected = G_GetMainSelectedUnit(ent->client);

    /* Ahar is the worker's visible command in stock unit data. While the main
     * selected worker carries resources, activating it performs the same
     * no-target Return Resources behavior instead of entering target mode. */
    if (selected && (selected->harvested_lumber > 0 || selected->harvested_gold > 0)) {
        return_resources_command(ent);
        return;
    }

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
    .is_toggle_on = harvest_is_toggle_on,
};
