#include "s_skills.h"

FLOAT HARVEST_LUMBER_CAPACITY;
FLOAT HARVEST_GOLD_CAPACITY;
FLOAT HARVEST_TREE_DAMAGE;
FLOAT HARVEST_RANGE;
FLOAT HARVEST_COOLDOWN;
FLOAT HARVEST_SEARCH_RANGE;

typedef struct harvestLumberTuning_s {
    FLOAT tree_damage;
    FLOAT lumber_capacity;
    FLOAT range;
    FLOAT cooldown;
    FLOAT search_range;
} harvestLumberTuning_t;

/* Resolve the worker's authored harvest alias, including runtime-added aliases. */
static DWORD harvest_actor_ability_alias(LPCEDICT ent, DWORD base_code) {
    char alias_name[5] = {0};

    if (!ent) return 0;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            DWORD alias = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&alias, token, 4);
            if (alias == base_code || G_AbilityCode(alias) == base_code) return alias;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        DWORD const alias = ent->abilities.added[i];
        if (!alias) continue;
        memcpy(alias_name, &alias, 4);
        alias_name[4] = '\0';
        if (G_ActorHasSkill(ent, alias_name) &&
            (alias == base_code || G_AbilityCode(alias) == base_code)) return alias;
    }
    return 0;
}

/* Prefer the lumber-only ability and otherwise use the shared Harvest ability. */
static DWORD harvest_lumber_alias(LPCEDICT ent) {
    DWORD alias = harvest_actor_ability_alias(ent, MAKEFOURCC('A','h','r','l'));
    return alias ? alias : harvest_actor_ability_alias(ent, MAKEFOURCC('A','h','a','r'));
}

/* Confirm that the worker has authoritative lumber-harvest data with capacity. */
BOOL S_HarvestCanLumber(LPCEDICT ent) {
    DWORD const alias = harvest_lumber_alias(ent);
    AbilityData_t const *data;

    if (!alias) return false;
    data = G_AbilityData(alias);
    if (data->id == alias) return data->level[0].data[1].number > 0.0f;
    /* Ahar predates per-worker tuning; legacy order callers may only need its
     * authored presence because their harvesting profile is initialized later. */
    return G_AbilityCode(alias) == MAKEFOURCC('A','h','a','r');
}

/* Confirm that the worker has authoritative gold-harvest data with capacity. */
BOOL S_HarvestCanGold(LPCEDICT ent) {
    DWORD const alias = harvest_actor_ability_alias(ent, MAKEFOURCC('A','h','a','r'));
    AbilityData_t const *data;

    if (!alias) return false;
    data = G_AbilityData(alias);
    if (data->id == alias) return data->level[0].data[2].number > 0.0f;
    /* Ahar predates per-worker tuning; legacy order callers may only need its
     * authored presence because their harvesting profile is initialized later. */
    return G_AbilityCode(alias) == MAKEFOURCC('A','h','a','r');
}

/* Collect per-worker lumber tuning from the resolved ability instead of globals. */
static harvestLumberTuning_t harvest_lumber_tuning(LPCEDICT ent) {
    harvestLumberTuning_t tuning = {
        .tree_damage = HARVEST_TREE_DAMAGE,
        .lumber_capacity = HARVEST_LUMBER_CAPACITY,
        .range = HARVEST_RANGE,
        .cooldown = HARVEST_COOLDOWN,
        .search_range = HARVEST_SEARCH_RANGE,
    };
    DWORD const alias = harvest_lumber_alias(ent);
    AbilityData_t const *data;
    DWORD base;

    if (!alias || !(data = G_AbilityData(alias)) || data->id != alias) return tuning;
    base = G_AbilityCode(alias);
    tuning.tree_damage = data->level[0].data[0].number;
    tuning.lumber_capacity = data->level[0].data[1].number;
    tuning.range = data->level[0].range;
    tuning.cooldown = data->level[0].dur;
    if (base == MAKEFOURCC('A','h','a','r'))
        tuning.search_range = data->level[0].area;
    else if (base == MAKEFOURCC('A','h','r','l'))
        tuning.search_range = FLT_MAX;
    return tuning;
}

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
    if (data->level[0].data[0].number) mask |= RETURN_RESOURCE_GOLD;
    if (data->level[0].data[1].number) mask |= RETURN_RESOURCE_LUMBER;
    return mask;
}

BOOL S_UnitTypeReturnsGold(DWORD unit_id) {
    UnitAbilities_t const *abilities = G_UnitAbil(unit_id);

    if (!abilities || !abilities->abilList) return false;
    PARSE_LIST(abilities->abilList, abil, parse_segment) {
        if (return_resources_mask(abil) & RETURN_RESOURCE_GOLD)
            return true;
    }
    return false;
}

BOOL S_CanReturnResourceAt(LPEDICT unit, LPEDICT building, returnResource_t resource) {
    LPCSTR abilities;

    /* Unit data exposes Return Resources before construction completes, but
     * Warcraft keeps that capability unavailable until the structure is finished. */
    if (!unit || !building || !building->inuse || building->s.player != unit->s.player ||
        M_IsDead(building) || building->construction.active)
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

static LPEDICT find_another_tree_near(LPCEDICT worker, LPCVECTOR2 origin) {
    FLOAT min_dist = harvest_lumber_tuning(worker).search_range;
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
    return ent ? find_another_tree_near(ent, &ent->s.origin2) : NULL;
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
    FLOAT const range = harvest_lumber_tuning(ent).range;

    if (distance <= range)
        return true;
    return CM_FindDirectApproachPointForRadius(&ent->s.origin2, &tree->s.origin2,
                                               range, ent->collision, &approach);
}

static LPEDICT find_reachable_replacement_tree(LPEDICT ent, LPEDICT exclude) {
    FLOAT min_dist = harvest_lumber_tuning(ent).search_range;
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

static BOOL actor_has_skill(LPCEDICT ent, DWORD code) {
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

BOOL G_ActorHasSkill(LPCEDICT ent, LPCSTR id) {
    DWORD code = 0;
    if (!id || strlen(id) != 4) return false;
    memcpy(&code, id, sizeof(code));
    return actor_has_skill(ent, code);
}

static BOOL harvest_auto_start(LPEDICT self, returnResource_t resource) {
    LPEDICT target;

    /* These are worker-internal immediate orders, not substitutes for giving
     * Harvest to arbitrary units. Ahrl is lumber-only while Ahar can harvest
     * both resources, matching Warsmash's shared CAbilityHarvest behavior. */
    if (!self || (self->aiflags & AI_IMMOBILE)) return false;
    if (resource == RETURN_RESOURCE_GOLD && !S_HarvestCanGold(self)) return false;
    if (resource == RETURN_RESOURCE_LUMBER && !S_HarvestCanLumber(self)) return false;

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
    S_EnableAbility(ent, code);
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
    S_DisableAbility(ent, code);
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
    FLOAT const range = harvest_lumber_tuning(ent).range;

    if (!ent->goalentity || M_IsDead(ent->goalentity)) {
        HARVEST_PATH_LOG(1, "invalid worker=%d target=%d reason=dead_or_missing\n",
                         ent->s.number, ent->goalentity ? ent->goalentity->s.number : -1);
        look_for_another_tree(ent);
    } else if (distance > range) {
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
            ent->s.number, ent->goalentity->s.number, distance, range);
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
        tree = find_another_tree_near(ent, &tree->s.origin2);
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
    harvestLumberTuning_t const tuning = harvest_lumber_tuning(ent);
    BOOL const valid_hit = tree && G_IsDestructable(tree) && !M_IsDead(tree) &&
                           !tree->invulnerable && tuning.tree_damage > 0.0f;
    BOOL felled = false;

    G_PublishMessage(ent, GAME_MSG_HARVEST_CHOP, tree);
    if (valid_hit) {
        FLOAT const carried = MIN((FLOAT)ent->harvested_lumber + tuning.tree_damage,
                                  tuning.lumber_capacity);

        felled = G_DestructableApplyDamage(tree, ent, tuning.tree_damage);
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

static umove_t harvest_move_walk = { "walk", ai_walktree, NULL, CAbilityHarvest };
static umove_t harvest_move_walkback = { "walk", ai_harvest_walkback, NULL, CAbilityHarvest };
static umove_t harvest_move_swing = { "attack", ai_swing, harvest_cooldown, CAbilityHarvest };
static umove_t harvest_move_cooldown = { "stand ready", ai_cooldown, NULL, CAbilityHarvest };

void harvest_cooldown(LPEDICT ent) {
    harvestLumberTuning_t const tuning = harvest_lumber_tuning(ent);

    if (ent->harvested_lumber >= tuning.lumber_capacity) {
        harvest_walkback(ent);
    } else if (M_IsDead(ent->goalentity)) {
        look_for_another_tree(ent);
    } else {
        unit_setmove(ent, &harvest_move_cooldown);
        ent->wait = tuning.cooldown;
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
    harvestLumberTuning_t const tuning = harvest_lumber_tuning(self);

    self->secondarygoal = target;
    if (self->harvested_lumber >= tuning.lumber_capacity && self->harvested_lumber > 0) {
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
    G_SetHealth(ent, 0);
    if (ent->die) {
        ent->die(ent, ent);
    }
}

static umove_t wisp_harvest_mine = { "stand", ai_wisp_mine, NULL, CAbilityWispHarvest };

static void ai_wisp_walktree(LPEDICT ent) {
    if (M_DistanceToGoal(ent) > HARVEST_RANGE) {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    } else {
        unit_setmove(ent, &wisp_harvest_mine);
        ent->wait = 1.0f;
    }
}

static umove_t wisp_harvest_walk = { "walk", ai_wisp_walktree, NULL, CAbilityWispHarvest };

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

BZ_ABILITY_PROC(CAbilityWispHarvest) {
    switch (msg) {
    case A_INIT:
        if (!call || !call->classname) return false;
        wisp_lumber_per_interval = G_AbilityDataName(call->classname)->level[0].data[0].number;
        wisp_interval_count = (DWORD)G_AbilityDataName(call->classname)->level[0].data[1].number;
        return true;
    case A_COMMAND: wisp_harvest_command(call && call->client ? call->client : ent); return true;
    default: return false;
    }
}

/* ---- Acolyte harvest: target blighted gold mine ------------------------- */
static BOOL acolyte_harvest_selecttarget(LPEDICT clent, LPEDICT target) {
    BOOL issued = false;

    if (!clent || !clent->client) return false;
    if (!target || !G_ActorHasSkill(target, "Abgm")) {
        G_ShowCommandErrorKey(clent, "Targetblightedmine", "Must target a Haunted Gold Mine.");
        return false;
    }
    if (target->s.player != clent->client->ps.number) {
        G_ShowCommandErrorKey(clent, "Nototherplayersmine",
                              "Unable to use a mine controlled by another player.");
        return false;
    }
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        if (S_AcolyteHarvestOrder(ent, target)) issued = true;
    }
    return issued;
}

BZ_COMMAND_PROC(AbilityAcolyteHarvest) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = acolyte_harvest_selecttarget;
}

/* ---- Return Resources: standalone command to deposit carried resources --- */
BZ_COMMAND_PROC(AbilityReturn) {
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

/* ---- Harvest menu dispatch (extended for wisp/acolyte) ------------------ */
BOOL harvest_menu_selecttarget(LPEDICT clent, LPEDICT target) {
    if (target && G_ActorHasSkill(target, "Abgm")) {
        BOOL has_acolyte = false;
        if (target->s.player != clent->client->ps.number) {
            G_ShowCommandErrorKey(clent, "Nototherplayersmine",
                                  "Unable to use a mine controlled by another player.");
            return false;
        }
        FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
            if (G_ActorHasSkill(ent, "Aaha")) {
                has_acolyte = true;
                S_AcolyteHarvestOrder(ent, target);
            }
        }
        if (!has_acolyte) return false;
    } else if (S_GoldMineCanHarvest(target)) {
        FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
            if (S_HarvestCanGold(ent)) harvest_gold_order(ent, target);
        }
    } else if (target && target->targtype == TARG_TREE) {
        FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
            if (S_HarvestCanLumber(ent)) harvest_start(ent, target);
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
        AbilityReturn_Command(ent);
        return;
    }

    UI_AddCancelButton(ent);
    ent->client->menu.on_entity_selected = harvest_menu_selecttarget;
}

/* Issue a lumber target to the selected workers while preserving ability ownership. */
static BOOL harvest_lumber_selecttarget(LPEDICT clent, LPEDICT target) {
    BOOL issued = false;

    if (!clent || !clent->client || !target || target->targtype != TARG_TREE || M_IsDead(target))
        return false;
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        if (!S_HarvestCanLumber(ent)) continue;
        harvest_start(ent, target);
        issued = true;
    }
    return issued;
}

/* Enter lumber target mode or return carried lumber through the owning ability. */
static void harvest_lumber_command(LPEDICT clent) {
    LPEDICT selected;

    if (!clent || !clent->client) return;
    selected = G_GetMainSelectedUnit(clent->client);
    if (selected && selected->harvested_lumber > 0) {
        AbilityReturn_Command(clent);
        return;
    }
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = harvest_lumber_selecttarget;
}

BZ_ABILITY_PROC(CAbilityHarvestLumber) {
    switch (msg) {
    case A_INIT: return true;
    case A_COMMAND: harvest_lumber_command(call && call->client ? call->client : ent); return true;
    case A_TOGGLE_ON: return harvest_is_toggle_on(ent);
    default: return false;
    }
}

BZ_ABILITY_PROC(CAbilityHarvest) {
    switch (msg) {
    case A_INIT:
        if (!call || !call->classname) return false;
        HARVEST_TREE_DAMAGE = AB_Data(call->classname, 1, 1);     /* lumber/tree-HP per swing */
        HARVEST_LUMBER_CAPACITY = AB_Data(call->classname, 1, 2); /* max lumber to carry */
        HARVEST_GOLD_CAPACITY = AB_Data(call->classname, 1, 3);
        HARVEST_RANGE = G_AbilityDataName(call->classname)->level[0].range;
        HARVEST_COOLDOWN = G_AbilityDataName(call->classname)->level[0].dur;
        HARVEST_SEARCH_RANGE = G_AbilityDataName(call->classname)->level[0].area;
        return true;
    case A_COMMAND: harvest_command(call && call->client ? call->client : ent); return true;
    case A_TOGGLE_ON: return harvest_is_toggle_on(ent);
    default: return false;
    }
}
