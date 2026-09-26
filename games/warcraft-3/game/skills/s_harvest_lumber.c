#include "s_skills.h"

float HARVEST_LUMBER_CAPACITY;
float HARVEST_GOLD_CAPACITY;
float HARVEST_TREE_DAMAGE;
float HARVEST_RANGE;
float HARVEST_COOLDOWN;
float HARVEST_SEARCH_RANGE;

typedef struct harvestLumberTuning_s {
    float tree_damage;
    float lumber_capacity;
    float range;
    float cooldown;
    float search_range;
} harvestLumberTuning_t;

/* Resolve the worker's authored harvest alias, including runtime-added aliases. */
static uint32_t harvest_actor_ability_alias(edict_t const *ent, uint32_t base_code) {
    char alias_name[5] = {0};

    if (!ent) return 0;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            uint32_t alias = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&alias, token, 4);
            if (alias == base_code || G_AbilityCode(alias) == base_code) return alias;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        uint32_t const alias = ent->abilities.added[i];
        if (!alias) continue;
        memcpy(alias_name, &alias, 4);
        alias_name[4] = '\0';
        if (G_ActorHasSkill(ent, alias_name) &&
            (alias == base_code || G_AbilityCode(alias) == base_code)) return alias;
    }
    return 0;
}

/* Prefer the lumber-only ability and otherwise use the shared Harvest ability. */
static uint32_t harvest_lumber_alias(edict_t const *ent) {
    uint32_t alias = harvest_actor_ability_alias(ent, MAKEFOURCC('A','h','r','l'));
    return alias ? alias : harvest_actor_ability_alias(ent, MAKEFOURCC('A','h','a','r'));
}

/* Confirm that the worker has authoritative lumber-harvest data with capacity. */
bool S_HarvestCanLumber(edict_t const *ent) {
    uint32_t const alias = harvest_lumber_alias(ent);
    AbilityData_t const *data;

    if (!alias) return false;
    data = G_AbilityData(alias);
    if (data->id == alias) return data->level[0].data[1].number > 0.0f;
    /* Ahar predates per-worker tuning; legacy order callers may only need its
     * authored presence because their harvesting profile is initialized later. */
    return G_AbilityCode(alias) == MAKEFOURCC('A','h','a','r');
}

/* Confirm that the worker has authoritative gold-harvest data with capacity. */
bool S_HarvestCanGold(edict_t const *ent) {
    uint32_t const alias = harvest_actor_ability_alias(ent, MAKEFOURCC('A','h','a','r'));
    AbilityData_t const *data;

    if (!alias) return false;
    data = G_AbilityData(alias);
    if (data->id == alias) return data->level[0].data[2].number > 0.0f;
    /* Ahar predates per-worker tuning; legacy order callers may only need its
     * authored presence because their harvesting profile is initialized later. */
    return G_AbilityCode(alias) == MAKEFOURCC('A','h','a','r');
}

/* Collect per-worker lumber tuning from the resolved ability instead of globals. */
static harvestLumberTuning_t harvest_lumber_tuning(edict_t const *ent) {
    harvestLumberTuning_t tuning = {
        .tree_damage = HARVEST_TREE_DAMAGE,
        .lumber_capacity = HARVEST_LUMBER_CAPACITY,
        .range = HARVEST_RANGE,
        .cooldown = HARVEST_COOLDOWN,
        .search_range = HARVEST_SEARCH_RANGE,
    };
    uint32_t const alias = harvest_lumber_alias(ent);
    AbilityData_t const *data;
    uint32_t base;

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

void harvest_cooldown(edict_t *ent);
void harvest_swing(edict_t *ent);
void harvest_walkback(edict_t *ent);
void harvest_walk(edict_t *ent);

void harvest_start(edict_t *self, edict_t *target);
void harvest_gold_start(edict_t *self, edict_t *target);

static int harvest_path_debug_level(void) {
    cstring_t value;
    value = gi.CvarString("wc3_harvest_path_debug", "0");
    return value ? atoi(value) : 0;
}

#define HARVEST_PATH_LOG(LEVEL, ...) do { \
    if (harvest_path_debug_level() >= (LEVEL)) { \
        fprintf(stderr, "WC3_HARVEST_PATH " __VA_ARGS__); \
    } \
} while (0)

static uint32_t return_resources_mask(cstring_t ability) {
    static struct { cstring_t name; uint32_t mask; } const artn_aliases[] = {
        { "Argd", RETURN_RESOURCE_GOLD },
        { "Arlm", RETURN_RESOURCE_LUMBER },
        { "Argl", RETURN_RESOURCE_GOLD | RETURN_RESOURCE_LUMBER },
    };
    AbilityData_t const *data;
    uint32_t mask = 0;
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

bool S_UnitTypeReturnsGold(uint32_t unit_id) {
    UnitAbilities_t const *abilities = G_UnitAbil(unit_id);

    if (!abilities || !abilities->abilList) return false;
    PARSE_LIST(abilities->abilList, abil, parse_segment) {
        if (return_resources_mask(abil) & RETURN_RESOURCE_GOLD)
            return true;
    }
    return false;
}

bool S_CanReturnResourceAt(edict_t *unit, edict_t *building, returnResource_t resource) {
    cstring_t abilities;

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

void S_SetCarriedResource(edict_t *unit, returnResource_t resource, uint32_t amount) {
    bool const was_carrying = unit && (unit->harvested_gold > 0 || unit->harvested_lumber > 0);
    bool is_carrying;

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
            gameClient_t *client = game.clients + i;
            if (G_IsEntitySelected(client, unit))
                G_InvalidateCommands(client);
        }
    }
}

edict_t *S_FindNearestResourceDropoff(edict_t *unit, returnResource_t resource) {
    edict_t *best = NULL;
    float best_dist = 0;

    /* TODO: use pathfinding distance; geometric distance misjudges across impassable terrain */
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *building = &globals.edicts[i];
        float dist;
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

static edict_t *find_another_tree_near(edict_t const *worker, vec2_t const *origin) {
    float min_dist = harvest_lumber_tuning(worker).search_range;
    edict_t *other = NULL;

    if (!origin)
        return NULL;

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *tree = globals.edicts + i;
        float dist;

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

static edict_t *find_another_tree(edict_t *ent) {
    return ent ? find_another_tree_near(ent, &ent->s.origin2) : NULL;
}

/* Automatic harvest orders have no explicit target.  Retail resolves them
 * from the worker's current position, then continues through the ordinary
 * targeted Harvest state machine.  Keep target discovery here so JASS/order
 * dispatch does not need to know what counts as a live resource. */
static edict_t *harvest_find_nearest_resource(edict_t *worker, returnResource_t resource) {
    edict_t *best = NULL;
    float best_dist = 0.0f;

    if (!worker)
        return NULL;

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *target = globals.edicts + i;
        float dist;

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
static bool harvest_find_nearest_dropoff_approach(edict_t *ent, edict_t *dropoff,
                                                   vec2_t *out) {
    float const route_band = ent ?
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
static bool tree_has_reachable_harvest_approach(edict_t *ent, edict_t *tree) {
    vec2_t approach;
    float const distance = Vector2_distance(&ent->s.origin2, &tree->s.origin2);
    float const range = harvest_lumber_tuning(ent).range;

    if (distance <= range)
        return true;
    return CM_FindDirectApproachPointForRadius(&ent->s.origin2, &tree->s.origin2,
                                               range, ent->collision, &approach);
}

static edict_t *find_reachable_replacement_tree(edict_t *ent, edict_t *exclude) {
    float min_dist = harvest_lumber_tuning(ent).search_range;
    edict_t *other = NULL;

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *tree = globals.edicts + i;
        float dist;
        bool reachable;

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

static void harvest_route_failed(edict_t *ent, cstring_t reason) {
    edict_t *failed = ent->goalentity;
    edict_t *other = find_reachable_replacement_tree(ent, failed);

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

static void look_for_another_tree(edict_t *ent) {
    edict_t *other = find_another_tree(ent);
    if (other) {
        harvest_start(ent, other);
    } else {
        ent->stand(ent);
    }
}

static int32_t skill_index(uint32_t const *skills, uint32_t count, uint32_t code) {
    FOR_LOOP(i, count) if (skills[i] == code) return i;
    return -1;
}

static bool skill_add(uint32_t *skills, uint32_t *count, uint32_t code) {
    if (*count >= MAX_ABILITIES) {
        fprintf(stderr, "WC3: unit ability list full while adding %08x\n", code); return false;
    }
    skills[(*count)++] = code; return true;
}

static void skill_remove(uint32_t *skills, uint32_t *count, uint32_t index) {
    memmove(skills + index, skills + index + 1, (--*count - index) * sizeof(*skills));
}

static bool actor_has_skill(edict_t const *ent, uint32_t code) {
    cstring_t abilities;
    if (!ent || !code) return false;
    if (skill_index(ent->abilities.removed, ARRAY_COUNT(ent->abilities.removed), code) >= 0) return false;
    if (skill_index(ent->abilities.added, ARRAY_COUNT(ent->abilities.added), code) >= 0) return true;
    if (!ent->data.UnitAbilities) return false;
    abilities = ent->data.UnitAbilities->abilList;
    if (abilities) {
        PARSE_LIST(abilities, abil, parse_segment) {
            uint32_t static_code = 0;
            if (strlen(abil) == 4) memcpy(&static_code, abil, sizeof(static_code));
            if (static_code == code) return true;
        }
    }
    return false;
}

bool G_ActorHasSkill(edict_t const *ent, cstring_t id) {
    uint32_t code = 0;
    if (!id || strlen(id) != 4) return false;
    memcpy(&code, id, sizeof(code));
    return actor_has_skill(ent, code);
}

static bool harvest_auto_start(edict_t *self, returnResource_t resource) {
    edict_t *target;

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

bool harvest_auto_start_gold(edict_t *self) {
    return harvest_auto_start(self, RETURN_RESOURCE_GOLD);
}

bool harvest_auto_start_lumber(edict_t *self) {
    return harvest_auto_start(self, RETURN_RESOURCE_LUMBER);
}

bool G_ActorAddSkill(edict_t *ent, uint32_t code) {
    int32_t index;
    if (!ent || !code || actor_has_skill(ent, code)) return false;
    index = skill_index(ent->abilities.removed, ARRAY_COUNT(ent->abilities.removed), code);
    if (index >= 0) skill_remove(ent->abilities.removed, &ARRAY_COUNT(ent->abilities.removed), index);
    else {
        if (G_AbilityData(code)->id != code) return false;
        if (!skill_add(ent->abilities.added, &ARRAY_COUNT(ent->abilities.added), code)) return false;
    }
    if (code == MAKEFOURCC('A', 'h', 'a', 'r')) G_InvalidateUnitShortcutsForUnit(ent);
    S_EnableAbility(ent, code);
    { gameClient_t *client = G_GetPlayerClientByNumber(ent->s.player); if (client) G_InvalidateCommands(client); }
    return true;
}

bool G_ActorRemoveSkill(edict_t *ent, uint32_t code) {
    int32_t index;
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
    { gameClient_t *client = G_GetPlayerClientByNumber(ent->s.player); if (client) G_InvalidateCommands(client); }
    return true;
}

bool G_ActorSetSkillPermanent(edict_t *ent, uint32_t code, bool permanent) {
    int32_t index;
    if (!actor_has_skill(ent, code)) return false;
    index = skill_index(ent->abilities.permanent, ARRAY_COUNT(ent->abilities.permanent), code);
    if (permanent && index < 0 && !skill_add(ent->abilities.permanent, &ARRAY_COUNT(ent->abilities.permanent), code)) return false;
    else if (!permanent && index >= 0) skill_remove(ent->abilities.permanent, &ARRAY_COUNT(ent->abilities.permanent), index);
    return true;
}

bool G_ActorSkillPermanent(edict_t *ent, uint32_t code) {
    return ent && skill_index(ent->abilities.permanent, ARRAY_COUNT(ent->abilities.permanent), code) >= 0;
}

void G_FreeActorSkills(edict_t *ent) {
    if (ent) memset(&ent->abilities, 0, sizeof(ent->abilities));
}

static void ai_walktree(edict_t *ent) {
    float const distance = M_DistanceToGoal(ent);
    float const range = harvest_lumber_tuning(ent).range;

    if (!ent->goalentity || M_IsDead(ent->goalentity)) {
        HARVEST_PATH_LOG(1, "invalid worker=%d target=%d reason=dead_or_missing\n",
                         ent->s.number, ent->goalentity ? ent->goalentity->s.number : -1);
        look_for_another_tree(ent);
    } else if (distance > range) {
        /* Warsmash keeps live-unit collision enabled for destructable harvest
         * movement.  Keep that collision contract, but use the worker crowd
         * policy so several Peasants converging on one tree do not repeatedly
         * choose competing generic slide sides.  Same-stream workers queue
         * briefly, then take the deterministic bounded pass if the worker in
         * front has stopped inside chop range. */
        unit_changeangle_for_radius_worker(ent, ent->collision);
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

static void harvest_finish_lumber_deposit(edict_t *ent) {
    edict_t *dropoff = ent->goalentity;
    edict_t *tree;
    player_t *player;

    G_PublishMessage(ent, GAME_MSG_HARVEST_DEPOSIT_LUMBER, dropoff);
    player = G_GetPlayerByNumber(ent->s.player);
    if (player) {
        G_CreditResourceIncome(player, ent, PLAYERSTATE_RESOURCE_LUMBER,
                               (int32_t)ent->harvested_lumber);
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

static void ai_harvest_walkback(edict_t *ent) {
    if (!S_CanReturnResourceAt(ent, ent->goalentity, RETURN_RESOURCE_LUMBER)) {
        edict_t *dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_LUMBER);
        if (!dropoff) {
            ent->stand(ent);
            return;
        }
        G_PublishMessage(ent, GAME_MSG_HARVEST_RETURN_LUMBER, dropoff);
        ent->goalentity = dropoff;
        move_reset_progress(ent);
    }

    float const dist = M_DistanceToGoal(ent);
    float const contact = ent->collision + ent->goalentity->collision;
    float const step = unit_movedistance(ent);
    float const footprint_dist = CM_DistanceToPathingFootprint(
        ent->goalentity, &ent->s.origin2);
    bool const footprint_deposit = footprint_dist < FLT_MAX &&
                                   footprint_dist <= ent->collision + step;
    bool const circle_deposit = dist <= contact + step;

    /* Match gold return: authored building pathing is the authoritative
     * physical boundary when it exists.  A Lumber Mill/Town Hall can block the
     * worker before its scalar collision circle reaches contact, so complete
     * the deposit when one legal step reaches either the footprint or the
     * collision fallback. */
    if (footprint_deposit || circle_deposit) {
        harvest_finish_lumber_deposit(ent);
    } else {
        vec2_t approach;

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

static void ai_chop(edict_t *ent) {
    edict_t *tree = ent->secondarygoal;
    harvestLumberTuning_t const tuning = harvest_lumber_tuning(ent);
    bool const valid_hit = tree && G_IsDestructable(tree) && !M_IsDead(tree) &&
                           !tree->invulnerable && tuning.tree_damage > 0.0f;
    bool felled = false;

    G_PublishMessage(ent, GAME_MSG_HARVEST_CHOP, tree);
    if (valid_hit) {
        if (ent->data.UnitData && WC3_RaceFromString(ent->data.UnitData->race) == RACE_UNDEAD)
            G_BlightMarkDestructable(tree);
        float const carried = MIN((float)ent->harvested_lumber + tuning.tree_damage,
                                  tuning.lumber_capacity);

        felled = G_DestructableApplyDamage(tree, ent, tuning.tree_damage);
        if (carried > ent->harvested_lumber)
            S_SetCarriedResource(ent, RETURN_RESOURCE_LUMBER, (uint32_t)carried);
    }
    /* Tree-fall supersedes chop: play one-shot world sound for all clients. */
    if (felled && g_numTreeFallSounds) {
        G_PublishMessage(ent, GAME_MSG_HARVEST_TREE_FELLED, tree);
        G_PlaySound(NULL, ent, CHAN_BODY, g_treeFallSounds[rand() % g_numTreeFallSounds], 1.0f, 1.0f, 0.0f);
    } else if (ent->sound.num_chop) {
        int sound = ent->sound.chop[rand() % ent->sound.num_chop];
        G_PlaySound(NULL, ent, CHAN_WEAPON, sound, G_SoundIndexVolume(sound), 1.0f, 0.0f);
    }
}

static void ai_swing(edict_t *ent) {
    unit_runwait(ent, ai_chop);
}

static void ai_cooldown(edict_t *ent) {
    unit_runwait(ent, harvest_swing);
}

static umove_t harvest_move_walk = { "walk", ai_walktree, NULL, CAbilityHarvest };
static umove_t harvest_move_walkback = { "walk", ai_harvest_walkback, NULL, CAbilityHarvest };
static umove_t harvest_move_swing = { "attack", ai_swing, harvest_cooldown, CAbilityHarvest };
static umove_t harvest_move_cooldown = { "stand ready", ai_cooldown, NULL, CAbilityHarvest };

void harvest_cooldown(edict_t *ent) {
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

void harvest_walk(edict_t *ent) {
    unit_setmove(ent, &harvest_move_walk);
}

void harvest_swing(edict_t *ent) {
    unit_setmove(ent, &harvest_move_swing);
    ent->wait = ent->data.UnitWeapons->attack1.damagePoint;
}

bool harvest_lumber_return_to(edict_t *ent, edict_t *dropoff) {
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

void harvest_walkback(edict_t *ent) {
    edict_t *dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_LUMBER);
    if (!harvest_lumber_return_to(ent, dropoff))
        ent->stand(ent);
}

void CMD_Harvest(edict_t *ent);

void harvest_start(edict_t *self, edict_t *target) {
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
static float wisp_lumber_per_interval;
static uint32_t wisp_interval_count;

static void ai_wisp_mine(edict_t *ent) {
    unit_runwait(ent, NULL);
    /* Wisp gathers lumber and is consumed. */
    player_t *player = G_GetPlayerByNumber(ent->s.player);
    if (player) {
        G_CreditResourceIncome(player, ent, PLAYERSTATE_RESOURCE_LUMBER,
                               (int32_t)wisp_lumber_per_interval);
    }
    G_SetHealth(ent, 0);
    if (ent->die) {
        ent->die(ent, ent);
    }
}

static umove_t wisp_harvest_mine = { "stand", ai_wisp_mine, NULL, CAbilityWispHarvest };

static void ai_wisp_walktree(edict_t *ent) {
    if (M_DistanceToGoal(ent) > HARVEST_RANGE) {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    } else {
        unit_setmove(ent, &wisp_harvest_mine);
        ent->wait = 1.0f;
    }
}

static umove_t wisp_harvest_walk = { "walk", ai_wisp_walktree, NULL, CAbilityWispHarvest };

void wisp_harvest_start(edict_t *self, edict_t *target) {
    self->goalentity = target;
    unit_setmove(self, &wisp_harvest_walk);
}

static bool wisp_harvest_selecttarget(edict_t *clent, edict_t *target) {
    if (!target || target->targtype != TARG_TREE || M_IsDead(target)) {
        return false;
    }
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        wisp_harvest_start(ent, target);
    }
    return true;
}

static void wisp_harvest_command(edict_t *clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = wisp_harvest_selecttarget;
}

BZ_ABILITY_PROC(CAbilityWispHarvest) {
    switch (msg) {
    case A_INIT:
        if (!call || !call->classname) return false;
        wisp_lumber_per_interval = G_AbilityDataName(call->classname)->level[0].data[0].number;
        wisp_interval_count = (uint32_t)G_AbilityDataName(call->classname)->level[0].data[1].number;
        return true;
    case A_COMMAND: wisp_harvest_command(call && call->client ? call->client : ent); return true;
    default: return false;
    }
}

/* ---- Acolyte harvest: target blighted gold mine ------------------------- */
static bool acolyte_harvest_selecttarget(edict_t *clent, edict_t *target) {
    bool issued = false;

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
            edict_t *dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_GOLD);
            if (!harvest_gold_return_to(ent, dropoff))
                ent->stand(ent);
        }
    }
}

/* ---- Harvest menu dispatch (extended for wisp/acolyte) ------------------ */
bool harvest_menu_selecttarget(edict_t *clent, edict_t *target) {
    if (target && G_ActorHasSkill(target, "Abgm")) {
        bool has_acolyte = false;
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

static bool harvest_is_toggle_on(edict_t *ent) {
    return ent && (ent->harvested_lumber > 0 || ent->harvested_gold > 0);
}

void harvest_command(edict_t *ent) {
    edict_t *selected = G_GetMainSelectedUnit(ent->client);

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
static bool harvest_lumber_selecttarget(edict_t *clent, edict_t *target) {
    bool issued = false;

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
static void harvest_lumber_command(edict_t *clent) {
    edict_t *selected;

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
