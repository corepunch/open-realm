#include "s_skills.h"

extern FLOAT HARVEST_GOLD_CAPACITY;

#define WC3_MINE_OVERLAY_MATCH_RADIUS 8.0f // world units; map placement tolerance for pairing an overlay with its parent mine

void harvestgold_walkback(LPEDICT ent);
void harvestgold_walk(LPEDICT ent);
void harvestgold_wait(LPEDICT ent);
void harvestgold_minegold(LPEDICT ent);
static umove_t harvestgold_move_wait;

static int goldmine_path_debug_level(void) {
    LPCSTR value;
    value = gi.CvarString("wc3_harvest_path_debug", "0");
    return value ? atoi(value) : 0;
}

static void goldmine_debug_dump_geometry(LPCEDICT mine) {
    pathTex_t const *pathtex;
    UnitUI_t const *ui;
    UnitData_t const *data;
    DWORD blocked = 0;
    int min_x = INT_MAX, min_y = INT_MAX, max_x = -1, max_y = -1;
    int const debug = goldmine_path_debug_level();

    if (debug < 2 || !mine)
        return;

    pathtex = mine->pathtex;
    ui = mine->data.UnitUI;
    data = mine->data.UnitData;
    if (pathtex) {
        FOR_LOOP(y, pathtex->height) {
            FOR_LOOP(x, pathtex->width) {
                if (!pathtex->map[x + y * pathtex->width].b)
                    continue;
                blocked++;
                min_x = MIN(min_x, (int)x);
                min_y = MIN(min_y, (int)y);
                max_x = MAX(max_x, (int)x);
                max_y = MAX(max_y, (int)y);
            }
        }
    }

    {
        FLOAT const cell = CM_PathCellWorldSize();
        FLOAT const local_min_x = blocked ? ((FLOAT)min_x - pathtex->width * 0.5f) * cell : 0.0f;
        FLOAT const local_min_y = blocked ? ((FLOAT)min_y - pathtex->height * 0.5f) * cell : 0.0f;
        FLOAT const local_max_x = blocked ? ((FLOAT)(max_x + 1) - pathtex->width * 0.5f) * cell : 0.0f;
        FLOAT const local_max_y = blocked ? ((FLOAT)(max_y + 1) - pathtex->height * 0.5f) * cell : 0.0f;

        fprintf(stderr,
                "WC3_GOLD_GEOMETRY mine=%d rawcode=%.4s origin=(%.1f,%.1f,%.1f) angle=%.1f "
                "model=\"%s\" scale=%.3f selection_radius=%.1f collision=%.1f "
                "pathing=\"%s\" path=%ux%u blocked=%u blocked_bbox=[%d,%d]-[%d,%d] "
                "blocked_local=[%.1f,%.1f]-[%.1f,%.1f] capacity=%u resources=%u\n",
                mine->s.number, (LPCSTR)&mine->s.class_id,
                mine->s.origin.x, mine->s.origin.y, mine->s.origin.z, mine->s.angle,
                ui && ui->modelFile ? ui->modelFile : "", mine->s.scale, mine->s.radius,
                mine->collision, data && data->pathingTexture ? data->pathingTexture : "",
                pathtex ? pathtex->width : 0, pathtex ? pathtex->height : 0, blocked,
                blocked ? min_x : -1, blocked ? min_y : -1,
                blocked ? max_x : -1, blocked ? max_y : -1,
                local_min_x, local_min_y, local_max_x, local_max_y,
                S_GoldMineCapacity(mine), mine->resources);
    }

    if (debug < 3 || !pathtex)
        return;
    if (pathtex->width > 120) {
        fprintf(stderr,
                "WC3_GOLD_FOOTPRINT mine=%d rawcode=%.4s omitted reason=width width=%u\n",
                mine->s.number, (LPCSTR)&mine->s.class_id, pathtex->width);
        return;
    }
    FOR_LOOP(y, pathtex->height) {
        char row[121];
        FOR_LOOP(x, pathtex->width)
            row[x] = pathtex->map[x + y * pathtex->width].b ? '#' : '.';
        row[pathtex->width] = '\0';
        fprintf(stderr,
                "WC3_GOLD_FOOTPRINT mine=%d row=%02u %s\n",
                mine->s.number, (unsigned)y, row);
    }
}

static void goldmine_debug_log_approach(LPEDICT ent, LPEDICT mine,
                                        FLOAT dist, FLOAT footprint_dist,
                                        FLOAT contact, FLOAT step) {
    static DWORD last_log[MAX_ENTITIES];
    DWORD number;
    DWORD now;

    if (goldmine_path_debug_level() < 2 || !ent || !mine)
        return;
    number = ent->s.number;
    if (number >= MAX_ENTITIES)
        return;
    now = G_Time();
    if (last_log[number] && (DWORD)(now - last_log[number]) < 250)
        return;
    last_log[number] = now;

    fprintf(stderr,
            "WC3_GOLD_PATH approach worker=%d rawcode=%.4s pos=(%.1f,%.1f) collision=%.1f "
            "mine=%d mine_rawcode=%.4s mine_pos=(%.1f,%.1f) mine_angle=%.1f mine_collision=%.1f "
            "distance=%.1f contact=%.1f step=%.1f footprint=%.1f "
            "heading=%.1f direct=%d path_valid=%d waypoint=(%.1f,%.1f) "
            "flow=%u flow_goal=%d unreachable=%d\n",
            ent->s.number, (LPCSTR)&ent->s.class_id, ent->s.origin.x, ent->s.origin.y, ent->collision,
            mine->s.number, (LPCSTR)&mine->s.class_id, mine->s.origin.x, mine->s.origin.y,
            mine->s.angle, mine->collision, dist, contact, step, footprint_dist,
            ent->movement.heading, ent->movement.flow_direct, ent->movement.path.valid,
            ent->movement.path.waypoint.x, ent->movement.path.waypoint.y,
            ent->movement.flow_generation, ent->movement.flow_goal_reached,
            ent->movement.flow_unreachable);
}

/* A resumable route miss leaves direct, accelerator, and flow states clear.
 * Gold movement must hold then: using the previous facing would send workers
 * in an unrelated direction while the shared route is still being built. */
static BOOL gold_route_pending(LPCEDICT worker) {
    return worker && !worker->movement.flow_direct &&
           !worker->movement.path.valid &&
           worker->movement.flow_generation == 0;
}

/* Warsmash-style return movement still needs a concrete static endpoint for a
 * blocked drop-off building.  Pick the innermost collision-safe pathing-cell
 * ring around the authored footprint, then choose the point on that ring
 * closest to this worker.  Unlike centre-rooted flow routing, this preserves
 * the worker's approach side (mine on the left -> left edge of the Town Hall). */
static BOOL gold_find_nearest_footprint_approach(LPEDICT worker, LPEDICT target,
                                                  LPVECTOR2 out) {
    FLOAT const route_band = worker ?
        worker->collision + CM_PathCellWorldSize() * 1.41421356237f : 0.0f;

    if (!worker || !target || !target->pathtex || !out)
        return false;
    return CM_FindInnerApproachPointToFootprintForRadius(
        target, &worker->s.origin2, route_band, worker->collision, out);
}

/* Resolve an authored or runtime ability alias while preserving race-specific aliases. */
static DWORD goldmine_actor_ability_alias(LPCEDICT ent, DWORD base_code) {
    char token_code[5] = {0};

    if (!ent) return 0;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            DWORD alias = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&alias, token, 4);
            if (G_AbilityCode(alias) == base_code || alias == base_code) return alias;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        DWORD const alias = ent->abilities.added[i];
        if (!alias) continue;
        memcpy(token_code, &alias, 4);
        token_code[4] = '\0';
        if (G_ActorHasSkill(ent, token_code) &&
            (G_AbilityCode(alias) == base_code || alias == base_code)) return alias;
    }
    return 0;
}

/* Classify overlay mines separately so they cannot be harvested as neutral mines. */
static BOOL goldmine_is_overlay_type(LPCEDICT mine) {
    return mine && (mine->mineoverlay.parent || G_ActorHasSkill(mine, "Agl2") ||
                    G_ActorHasSkill(mine, "Abgm") || G_ActorHasSkill(mine, "Aegm"));
}

static AbilityData_t const *goldmine_ability_data(LPCEDICT mine) {
    LPCSTR abilities;

    if (!mine || !mine->data.UnitAbilities || !(abilities = mine->data.UnitAbilities->abilList))
        return NULL;

    PARSE_LIST(abilities, abil, parse_segment) {
        if (G_AbilityCodeName(abil) == MAKEFOURCC('A', 'g', 'l', 'd'))
            return G_AbilityDataName(abil);
    }
    return NULL;
}

BOOL S_UnitTypeIsGoldMine(DWORD unit_id) {
    UnitAbilities_t const *unit_abilities = G_UnitAbil(unit_id);

    if (!unit_abilities || !unit_abilities->abilList) return false;
    PARSE_LIST(unit_abilities->abilList, abil, parse_segment) {
        if (G_AbilityCodeName(abil) == MAKEFOURCC('A', 'g', 'l', 'd'))
            return true;
    }
    return false;
}

BOOL S_GoldMineIsMine(LPCEDICT mine) {
    return goldmine_ability_data(mine) != NULL;
}

BOOL S_GoldMineIsOverlay(LPCEDICT mine) { return goldmine_is_overlay_type(mine); }

DWORD S_GoldMineMaximumGold(LPCEDICT mine) {
    AbilityData_t const *data = goldmine_ability_data(mine);
    if (!data || data->level[0].data[0].number <= 0)
        return 0;
    return (DWORD)data->level[0].data[0].number;
}

FLOAT S_GoldMineMiningDuration(LPCEDICT mine) {
    AbilityData_t const *data = goldmine_ability_data(mine);
    return data ? MAX(0.0f, data->level[0].data[1].number) : 0.0f;
}

DWORD S_GoldMineCapacity(LPCEDICT mine) {
    AbilityData_t const *data = goldmine_ability_data(mine);
    if (!data || data->level[0].data[2].number <= 0)
        return 0;
    return (DWORD)data->level[0].data[2].number;
}

BOOL S_GoldMineCanHarvest(LPCEDICT mine) {
    return mine && mine->inuse && mine->health.value > 0 && S_GoldMineIsMine(mine) &&
           !goldmine_is_overlay_type(mine) && mine->resources > 0;
}

BOOL S_GoldMineWorkerIsInside(LPCEDICT worker) {
    return worker && worker->goldmine.mine != NULL;
}

void S_GoldMineInitUnit(LPEDICT mine) {
    DWORD maximum;

    if (!S_GoldMineIsMine(mine) || goldmine_is_overlay_type(mine))
        return;
    maximum = S_GoldMineMaximumGold(mine);
    if (mine->resources == 0 && maximum > 0)
        mine->resources = maximum;
    goldmine_debug_dump_geometry(mine);
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
    if (mine->peonsinside == 1) G_AddUnitAnimationProperties(mine, "work", true);
}

static LPEDICT goldmine_unregister_miner(LPEDICT worker) {
    LPEDICT mine;

    if (!worker || !(mine = worker->goldmine.mine))
        return NULL;
    if (goldmine_membership_valid(worker, mine) && mine->peonsinside > 0) {
        mine->peonsinside--;
        if (mine->peonsinside == 0) G_AddUnitAnimationProperties(mine, "work", false);
    }
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
    G_SetHealth(mine, 0);
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
    LPEDICT mine = ent ? ent->goalentity : NULL;
    FLOAT dist, contact, step, footprint_dist;
    BOOL footprint_entry, circle_entry;
    int const debug = goldmine_path_debug_level();

    if (!S_GoldMineCanHarvest(mine)) {
        if (debug >= 1 && ent) {
            fprintf(stderr,
                    "WC3_GOLD_PATH stop worker=%d mine=%d reason=not_harvestable resources=%u\n",
                    ent->s.number, mine ? mine->s.number : -1,
                    mine ? mine->resources : 0);
        }
        ent->stand(ent);
        return;
    }

    dist = M_DistanceToGoal(ent);
    contact = ent->collision + mine->collision;
    step = unit_movedistance(ent);
    footprint_dist = CM_DistanceToPathingFootprint(mine, &ent->s.origin2);
    footprint_entry = footprint_dist < FLT_MAX && footprint_dist <= ent->collision + step;
    circle_entry = dist <= contact + step;

    /* Mine entry is an interaction with the authored building footprint, not
     * necessarily its centre collision circle.  A worker approaching a square
     * mine at a corner can be stopped by pathing while its centre-to-centre
     * distance is still larger than collision+step.  Complete the interaction
     * when the next step would touch the mine's own no-walk footprint.  Keep
     * the historical collision-circle rule as a fallback for mines with no
     * path texture. */
    if (footprint_entry || circle_entry) {
        if (debug >= 1) {
            fprintf(stderr,
                    "WC3_GOLD_PATH enter_range worker=%d mine=%d distance=%.1f contact=%.1f step=%.1f footprint=%.1f via=%s peons=%u capacity=%u resources=%u\n",
                    ent->s.number, mine->s.number, dist, contact, step,
                    footprint_dist, footprint_entry ? "footprint" : "circle",
                    mine->peonsinside, S_GoldMineCapacity(mine), mine->resources);
        }
        harvestgold_minegold(ent);
    } else {
        /* Warsmash's CBehaviorHarvest disables live-unit collision when its
         * target is a unit. Gold Mines are units, so retain authored/static
         * pathing while allowing miners to share the same approach space. */
        unit_changeangle_interaction_ignore_units(ent);
        goldmine_debug_log_approach(ent, mine, dist, footprint_dist, contact, step);
        if (gold_route_pending(ent))
            return;
        /* The collision-sized route ends outside the mine footprint. The
         * behavior-owned range check above remains authoritative for entry. */
        unit_moveindirection_ignore_units(ent);
    }
}

static void goldmine_finish_deposit(LPEDICT ent, LPEDICT dropoff, int debug) {
    LPPLAYER player;

    G_PublishMessage(ent, GAME_MSG_HARVEST_DEPOSIT_GOLD, dropoff);
    ent->goalentity = ent->secondarygoal;
    player = G_GetPlayerByNumber(ent->s.player);
    if (player) {
        G_CreditResourceIncome(player, ent, PLAYERSTATE_RESOURCE_GOLD,
                               (LONG)ent->harvested_gold);
    }
    if (debug >= 1)
        fprintf(stderr,
                "WC3_GOLD_RETURN deposit worker=%d dropoff=%d resume_mine=%d gold=%u\n",
                ent->s.number, dropoff->s.number,
                ent->goalentity ? ent->goalentity->s.number : -1,
                ent->harvested_gold);
    S_SetCarriedResource(ent, RETURN_RESOURCE_GOLD, 0);
    if (S_GoldMineCanHarvest(ent->goalentity)) {
        G_PublishMessage(ent, GAME_MSG_HARVEST_RESUME_GOLD, ent->goalentity);
        harvestgold_walk(ent);
    } else {
        ent->stand(ent);
    }
}

static void ai_goldmine_walkback(LPEDICT ent) {
    LPEDICT dropoff;
    FLOAT dist, contact, step, footprint_dist;
    BOOL footprint_deposit, circle_deposit;
    int const debug = goldmine_path_debug_level();

    if (!S_CanReturnResourceAt(ent, ent->goalentity, RETURN_RESOURCE_GOLD)) {
        dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_GOLD);
        if (!dropoff) {
            if (debug >= 1)
                fprintf(stderr,
                        "WC3_GOLD_RETURN stop worker=%d reason=no_dropoff gold=%u\n",
                        ent->s.number, ent->harvested_gold);
            ent->stand(ent);
            return;
        }
        G_PublishMessage(ent, GAME_MSG_HARVEST_RETURN_GOLD, dropoff);
        ent->goalentity = dropoff;
        move_reset_progress(ent);
        if (debug >= 1)
            fprintf(stderr,
                    "WC3_GOLD_RETURN retarget worker=%d dropoff=%d gold=%u\n",
                    ent->s.number, dropoff->s.number, ent->harvested_gold);
    }

    dropoff = ent->goalentity;
    dist = M_DistanceToGoal(ent);
    contact = ent->collision + dropoff->collision;
    step = unit_movedistance(ent);
    footprint_dist = CM_DistanceToPathingFootprint(dropoff, &ent->s.origin2);
    footprint_deposit = footprint_dist < FLT_MAX &&
                        footprint_dist <= ent->collision + step;
    circle_deposit = dist <= contact + step;

    /* Return-resource range is footprint-aware for the same reason mine entry
     * is: a Town Hall's authored no-walk cells can stop the worker before a
     * centre-circle approximation reaches contact.  Deposit when the worker's
     * radius plus one simulation step reaches the actual building footprint.
     * Keep collision+step as the fallback for buildings with no path texture. */
    if (footprint_deposit || circle_deposit) {
        if (debug >= 1)
            fprintf(stderr,
                    "WC3_GOLD_RETURN deposit_range worker=%d dropoff=%d distance=%.1f contact=%.1f step=%.1f footprint=%.1f via=%s gold=%u\n",
                    ent->s.number, dropoff->s.number, dist, contact, step,
                    footprint_dist, footprint_deposit ? "footprint" : "circle",
                    ent->harvested_gold);
        goldmine_finish_deposit(ent, dropoff, debug);
    } else {
        VECTOR2 approach;

        /* Return Resources targets the building interaction boundary, not an
         * arbitrary legal cell around its blocked centre. Pick the innermost
         * collision-safe ring and then the worker's current side. */
        if (gold_find_nearest_footprint_approach(ent, dropoff, &approach)) {
            if (unit_snap_to_point_ignore_units(ent, &approach)) {
                goldmine_finish_deposit(ent, dropoff, debug);
                return;
            }
            if (unit_changeangle_towards_point_ignore_units(ent, &approach)) {
                unit_moveindirection_ignore_units(ent);
                return;
            }
        }

        /* Longer detours retain the shared collision-sized fallback while
         * live units remain non-blocking on resource-return legs. */
        unit_changeangle_interaction_ignore_units(ent);
        if (gold_route_pending(ent))
            return;
        unit_moveindirection_ignore_units(ent);
    }
}

static void ai_minegold(LPEDICT ent) {
    unit_runwait(ent, harvestgold_walkback);
}

static void ai_waittoenter(LPEDICT ent) {
}

static umove_t harvestgold_move_walk = { "walk", ai_walkmine, NULL, CAbilityGoldMine };
static umove_t harvestgold_move_walkback = { "walk", ai_goldmine_walkback, NULL, CAbilityGoldMine };
static umove_t harvestgold_move_minegold = { "attack", ai_minegold, NULL, CAbilityGoldMine };
static umove_t harvestgold_move_wait = { "stand", ai_waittoenter, NULL, CAbilityGoldMine };

BOOL harvest_gold_return_to(LPEDICT ent, LPEDICT dropoff) {
    if (!ent || !dropoff || !ent->harvested_gold ||
        !S_CanReturnResourceAt(ent, dropoff, RETURN_RESOURCE_GOLD)) {
        return false;
    }

    G_PublishMessage(ent, GAME_MSG_HARVEST_RETURN_GOLD, dropoff);
    ent->goalentity = dropoff;
    move_reset_progress(ent);
    unit_setmove(ent, &harvestgold_move_walkback);
    return true;
}

void harvestgold_walk(LPEDICT ent) {
    move_reset_progress(ent);
    unit_setmove(ent, &harvestgold_move_walk);
}

void harvestgold_minegold(LPEDICT ent) {
    LPEDICT mine = ent ? ent->goalentity : NULL;
    LPEDICT dropoff;
    DWORD capacity;

    if (!ent || !S_GoldMineCanHarvest(mine)) {
        if (ent) ent->stand(ent);
        return;
    }
    if (S_GoldMineWorkerIsInside(ent))
        return;

    /* Retail honors the clicked mine first even when the worker is already
     * carrying gold.  Once the worker reaches the mine interaction boundary,
     * do not mine another load: return the existing gold to the nearest valid
     * drop-off, then resume this clicked mine after the deposit completes. */
    if (ent->harvested_gold > 0) {
        dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_GOLD);
        if (!dropoff || !harvest_gold_return_to(ent, dropoff)) {
            ent->stand(ent);
        }
        return;
    }

    capacity = S_GoldMineCapacity(mine);
    if (capacity == 0) {
        if (goldmine_path_debug_level() >= 1)
            fprintf(stderr, "WC3_GOLD_PATH stop worker=%d mine=%d reason=zero_capacity\n",
                    ent->s.number, mine->s.number);
        ent->stand(ent);
        return;
    }
    if (mine->peonsinside < capacity) {
        if (goldmine_path_debug_level() >= 1)
            fprintf(stderr,
                    "WC3_GOLD_PATH enter worker=%d mine=%d peons=%u capacity=%u duration=%.3f resources=%u\n",
                    ent->s.number, mine->s.number, mine->peonsinside, capacity,
                    S_GoldMineMiningDuration(mine), mine->resources);
        G_PublishMessage(ent, GAME_MSG_HARVEST_ENTER_MINE, mine);
        unit_setmove(ent, &harvestgold_move_minegold);
        ent->wait = S_GoldMineMiningDuration(mine);
        goldmine_register_miner(ent, mine);
    } else {
        if (goldmine_path_debug_level() >= 1)
            fprintf(stderr,
                    "WC3_GOLD_PATH wait worker=%d mine=%d peons=%u capacity=%u resources=%u\n",
                    ent->s.number, mine->s.number, mine->peonsinside, capacity, mine->resources);
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

    S_SetCarriedResource(ent, RETURN_RESOURCE_GOLD, ent->harvested_gold + amount);
    LPEDICT dropoff = S_FindNearestResourceDropoff(ent, RETURN_RESOURCE_GOLD);
    if (dropoff) {
        if (goldmine_path_debug_level() >= 1)
            fprintf(stderr,
                    "WC3_GOLD_RETURN start worker=%d mine=%d dropoff=%d gold=%u\n",
                    ent->s.number, mine->s.number, dropoff->s.number,
                    ent->harvested_gold);
        G_PublishMessage(ent, GAME_MSG_HARVEST_RETURN_GOLD, dropoff);
        ent->goalentity = dropoff;
        move_reset_progress(ent);
        unit_setmove(ent, &harvestgold_move_walkback);
    } else {
        ent->stand(ent);
    }
}

void harvestgold_wait(LPEDICT ent) {
    unit_setmove(ent, &harvestgold_move_wait);
}

void harvest_gold_start(LPEDICT self, LPEDICT target) {
    if (goldmine_path_debug_level() >= 1) {
        fprintf(stderr,
                "WC3_GOLD_PATH start worker=%d rawcode=%.4s pos=(%.1f,%.1f) "
                "mine=%d mine_rawcode=%.4s mine_pos=(%.1f,%.1f) mine_angle=%.1f\n",
                self->s.number, (LPCSTR)&self->s.class_id, self->s.origin.x, self->s.origin.y,
                target ? target->s.number : -1, target ? (LPCSTR)&target->s.class_id : "----",
                target ? target->s.origin.x : 0.0f, target ? target->s.origin.y : 0.0f,
                target ? target->s.angle : 0.0f);
    }
    self->goalentity = target;
    self->secondarygoal = target;
    G_PublishMessage(self, GAME_MSG_HARVEST_MOVE_GOLD, target);
    harvestgold_walk(self);
}

BOOL harvest_gold_order(LPEDICT self, LPEDICT target) {
    if (!self || !target || !S_GoldMineCanHarvest(target))
        return false;

    /* Keep the clicked mine as the immediate goal regardless of carried
     * resources.  Gold already in hand is handled only after the worker
     * reaches this mine, matching retail's visible order transition. */
    harvest_gold_start(self, target);
    return true;
}

BZ_ABILITY_PROC(CAbilityGoldMine) {
    return CAbilityNoop(ent, msg, call);
}

/* ---- Racial Gold Mine overlays ------------------------------------------ */

static void haunted_mine_remove_effects(LPEDICT mine);

/* Validate and return a mine's live parent, clearing stale saved relationships. */
static LPEDICT mineoverlay_parent(LPEDICT overlay) {
    LPEDICT parent;

    if (!overlay || !(parent = overlay->mineoverlay.parent)) return NULL;
    if (!parent->inuse || parent->spawn_time != overlay->mineoverlay.parent_spawn_time ||
        M_IsDead(parent) || !S_GoldMineIsMine(parent)) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING parent-invalid overlay=%ld parent=%ld parent_inuse=%d "
                "spawn=%u/%u dead=%d goldmine=%d hidden=%d noclient=%d model=%d gold=%u\n",
                (long)(overlay - globals.edicts), (long)(parent - globals.edicts), parent->inuse,
                (unsigned)parent->spawn_time, (unsigned)overlay->mineoverlay.parent_spawn_time,
                M_IsDead(parent), S_GoldMineIsMine(parent), !!(parent->s.renderfx & RF_HIDDEN),
                !!(parent->svflags & SVF_NOCLIENT), !!parent->s.model, (unsigned)parent->resources);
#endif
        overlay->mineoverlay.parent = NULL;
        overlay->mineoverlay.parent_spawn_time = 0;
        return NULL;
    }
    return parent;
}

/* Resolve a parent for teardown without requiring it to remain a valid gold mine. */
static LPEDICT mineoverlay_release_parent(LPEDICT overlay) {
    LPEDICT parent;

    if (!overlay || !(parent = overlay->mineoverlay.parent) || !parent->inuse ||
        parent->spawn_time != overlay->mineoverlay.parent_spawn_time) return NULL;
    return parent;
}

/* Check whether another overlay already owns the candidate parent mine. */
static BOOL mineoverlay_parent_in_use(LPEDICT parent, LPEDICT except) {
    if (!parent) return false;
    FILTER_EDICTS(ent, ent != except && ent->inuse && ent->mineoverlay.parent == parent &&
                  ent->mineoverlay.parent_spawn_time == parent->spawn_time) {
        return true;
    }
    return false;
}

BOOL S_MineOverlayBind(LPEDICT overlay, LPEDICT parent) {
    if (!overlay || !parent || overlay == parent || !overlay->inuse || !parent->inuse ||
        M_IsDead(overlay) || M_IsDead(parent) || !S_GoldMineIsMine(parent) ||
        goldmine_is_overlay_type(parent) || mineoverlay_parent_in_use(parent, overlay)) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING bind-rejected overlay=%ld parent=%ld overlay_ok=%d parent_ok=%d "
                "overlay_dead=%d parent_dead=%d parent_goldmine=%d parent_overlay=%d parent_bound=%d\n",
                overlay ? (long)(overlay - globals.edicts) : -1L,
                parent ? (long)(parent - globals.edicts) : -1L,
                overlay && overlay->inuse, parent && parent->inuse,
                overlay && M_IsDead(overlay), parent && M_IsDead(parent),
                parent && S_GoldMineIsMine(parent), parent && goldmine_is_overlay_type(parent),
                parent && mineoverlay_parent_in_use(parent, overlay));
#endif
        return false;
    }

    if (overlay->mineoverlay.parent) S_MineOverlayRelease(overlay);
    overlay->mineoverlay.parent = parent;
    overlay->mineoverlay.parent_spawn_time = parent->spawn_time;
    overlay->mineoverlay.income_time = 0;
    overlay->mineoverlay.active_interval_index = 0;
    parent->s.renderfx |= RF_HIDDEN;
    parent->paused = true;
    G_InvalidateUnitShortcutsForUnit(parent);
    if (parent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.LinkEntity(parent);
#ifdef WC3_DEBUG_MINING
    fprintf(stderr, "WC3_MINING bound overlay=%ld id=%.4s parent=%ld id=%.4s spawn=%u "
            "parent_spawn=%u hidden=%d paused=%d noclient=%d model=%d gold=%u\n",
            (long)(overlay - globals.edicts), (LPCSTR)&overlay->class_id,
            (long)(parent - globals.edicts), (LPCSTR)&parent->class_id,
            (unsigned)overlay->spawn_time, (unsigned)parent->spawn_time,
            !!(parent->s.renderfx & RF_HIDDEN), parent->paused,
            !!(parent->svflags & SVF_NOCLIENT), !!parent->s.model, (unsigned)parent->resources);
#endif
    return true;
}

/* Pair map-placed overlay units with the neutral mine loaded at the same
 * location; construction performs this binding itself, but map units have no
 * builder callback to establish the relationship. */
void S_MineOverlayBindPreplaced(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT overlay = &globals.edicts[i];
        LPEDICT best = NULL;
        FLOAT best_distance = FLT_MAX;

        if (!overlay->inuse || !goldmine_is_overlay_type(overlay) || overlay->mineoverlay.parent) continue;
        FOR_LOOP(j, globals.num_edicts) {
            LPEDICT parent = &globals.edicts[j];
            FLOAT distance;
            if (parent == overlay || !parent->inuse || !S_GoldMineIsMine(parent) ||
                goldmine_is_overlay_type(parent) || parent->s.player != PLAYER_NEUTRAL_PASSIVE) continue;
            distance = Vector2_distance(&overlay->s.origin2, &parent->s.origin2);
            if (distance > WC3_MINE_OVERLAY_MATCH_RADIUS || distance >= best_distance) continue;
            best = parent;
            best_distance = distance;
        }
        if (best) {
            S_MineOverlayBind(overlay, best);
            continue;
        }
        /* Retail's pre-placed parent is already in its stand state; creating the
         * missing map parent must not replay its birth animation. */
        best = SP_SpawnAtLocationNoBirth(MAKEFOURCC('n','g','o','l'), PLAYER_NEUTRAL_PASSIVE, &overlay->s.origin2);
        if (best) {
            best->resources = overlay->resources ? overlay->resources : S_GoldMineMaximumGold(best);
            S_MineOverlayBind(overlay, best);
#ifdef WC3_DEBUG_MINING
            fprintf(stderr, "WC3_MINING preplaced-created-parent overlay=%ld parent=%ld gold=%u\n",
                    (long)(overlay - globals.edicts), (long)(best - globals.edicts),
                    (unsigned)best->resources);
#endif
            continue;
        }
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING preplaced-unbound overlay=%ld id=%.4s origin=(%.1f,%.1f)\n",
                (long)(overlay - globals.edicts), (LPCSTR)&overlay->class_id,
                overlay->s.origin2.x, overlay->s.origin2.y);
#endif
    }
}

/* CreateBlightedGoldmine is a script-visible overlay constructor; bind it
 * immediately so subsequent scripted harvest orders receive a live target. */
LPEDICT S_CreateBlightedGoldmine(DWORD player, LPCVECTOR2 origin, FLOAT facing) {
    LPEDICT overlay;

    if (!origin) return NULL;
    FILTER_EDICTS(existing, existing->inuse && existing->s.player == player &&
                  S_GoldMineIsOverlay(existing) && Vector2_distance(&existing->s.origin2, origin) <= 1.0f) {
        existing->s.angle = facing;
        return existing;
    }
    overlay = SP_SpawnAtLocationNoBirth(MAKEFOURCC('u','g','o','l'), player, origin);
    if (!overlay) return NULL;
    overlay->s.angle = facing;
    S_MineOverlayBindPreplaced();
    if (!overlay->mineoverlay.parent) {
        G_FreeEdict(overlay);
        return NULL;
    }
    return overlay;
}

void S_GoldMineSetResourceAmount(LPEDICT mine, DWORD amount) {
    LPEDICT parent = mineoverlay_parent(mine);
    if (parent) parent->resources = amount;
    else if (mine) mine->resources = amount;
}

BOOL S_AcolyteHarvestIsActive(LPCEDICT worker) {
    LPCEDICT mine;
    if (!worker || !(mine = worker->acolyte_mine.mine)) return false;
    return mine->inuse && mine->spawn_time == worker->acolyte_mine.mine_spawn_time &&
           worker->acolyte_mine.slot >= 0;
}

void S_AcolyteHarvestRelease(LPEDICT worker) {
    LPEDICT mine;
    if (!worker) return;
    mine = worker->acolyte_mine.mine;
    worker->acolyte_mine.mine = NULL;
    worker->acolyte_mine.mine_spawn_time = 0;
    worker->acolyte_mine.slot = -1;
    if (worker->goalentity == mine) worker->goalentity = NULL;
    if (worker->secondarygoal == mine) worker->secondarygoal = NULL;
}

void S_MineOverlayRelease(LPEDICT overlay) {
    LPEDICT parent;

    if (!overlay) return;
    /* Warsmash removes the persistent Abgm EffectArt ring on both ability
     * removal and mine death. Do this before clearing the overlay identity so
     * the effect-owner markers remain available to the cleanup scan. */
    haunted_mine_remove_effects(overlay);
    /* A Haunted Mine owns fixed Acolyte relationships. Retiring the mine must
     * free those slots before its edict can be reused. */
    FILTER_EDICTS(worker, worker->inuse && worker->acolyte_mine.mine == overlay &&
                  worker->acolyte_mine.mine_spawn_time == overlay->spawn_time) {
        S_AcolyteHarvestRelease(worker);
        if (worker->currentmove && worker->currentmove->proc == CAbilityAcolyteHarvest)
            unit_stand(worker);
    }

    /* Teardown restores the bound edict by identity. Runtime mining validation
     * above must not discard the parent before its hidden/paused state is undone. */
    parent = mineoverlay_release_parent(overlay);
#ifdef WC3_DEBUG_MINING
    fprintf(stderr, "WC3_MINING release overlay=%ld id=%.4s parent=%ld result=%ld "
            "parent_spawn=%u/%u\n", (long)(overlay - globals.edicts), (LPCSTR)&overlay->class_id,
            overlay->mineoverlay.parent ? (long)(overlay->mineoverlay.parent - globals.edicts) : -1L,
            parent ? (long)(parent - globals.edicts) : -1L,
            parent ? (unsigned)parent->spawn_time : 0u,
            (unsigned)overlay->mineoverlay.parent_spawn_time);
#endif
    overlay->mineoverlay.parent = NULL;
    overlay->mineoverlay.parent_spawn_time = 0;
    overlay->mineoverlay.income_time = 0;
    overlay->mineoverlay.active_interval_index = 0;
    if (!parent) return;
    parent->s.renderfx &= ~RF_HIDDEN;
    parent->paused = false;
    G_InvalidateUnitShortcutsForUnit(parent);
    if (parent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.LinkEntity(parent);
#ifdef WC3_DEBUG_MINING
    fprintf(stderr, "WC3_MINING restored parent=%ld id=%.4s hidden=%d paused=%d noclient=%d "
            "model=%d dead=%d gold=%u\n", (long)(parent - globals.edicts), (LPCSTR)&parent->class_id,
            !!(parent->s.renderfx & RF_HIDDEN), parent->paused,
            !!(parent->svflags & SVF_NOCLIENT), !!parent->s.model, M_IsDead(parent),
            (unsigned)parent->resources);
#endif
}

/* Agl2 owns only the shared overlay relationship. The racial mine abilities
 * below own their worker and income state. */
/* ---- Haunted Gold Mine / Acolyte Harvest -------------------------------- */

/* Return the authored Haunted Mine ability alias active on this unit. */
static DWORD haunted_mine_alias(LPEDICT mine) {
    return goldmine_actor_ability_alias(mine, MAKEFOURCC('A','b','g','m'));
}

/* Read the authored maximum number of Acolytes supported by the Haunted Mine. */
static DWORD haunted_mine_max_miners(LPEDICT mine) {
    DWORD alias = haunted_mine_alias(mine);
    FLOAT value = alias ? G_AbilityLevel(alias, 1)->data[2].number : 0.0f;
    if (value <= 0.0f) return 0;
    return (DWORD)value;
}

/* Read the authored radius used to position the Acolyte mining ring. */
static FLOAT haunted_mine_ring_radius(LPEDICT mine) {
    DWORD alias = haunted_mine_alias(mine);
    return alias ? MAX(0.0f, G_AbilityLevel(alias, 1)->data[3].number) : 0.0f;
}

/* Convert a fixed mining-ring slot into the world position used by the worker. */
static void haunted_mine_slot_position(LPEDICT mine, DWORD slot, DWORD capacity, LPVECTOR2 out) {
    double angle;
    FLOAT radius;
    if (!out || !mine || !capacity) return;
    angle = ((M_PI * 2.0) / (double)capacity) * (double)slot + (M_PI / 2.0);
    radius = haunted_mine_ring_radius(mine);
    out->x = mine->s.origin2.x + (FLOAT)cos(angle) * radius;
    out->y = mine->s.origin2.y + (FLOAT)sin(angle) * radius;
}

/* CAbilityBlightedGoldMine.onAdd() creates one persistent EFFECT render
 * component at every authored Acolyte ring slot and faces it radially. Point
 * effects are ordinary edicts in OpenRealm, so tag them with their owning mine,
 * ability alias, and one-based slot number. That gives teardown/save-load a
 * stable identity without adding another serialized pointer array. */
static BOOL haunted_ring_effect_matches(LPCEDICT effect, LPCEDICT mine, DWORD alias, DWORD slot) {
    return effect && effect->inuse && effect->owner == mine &&
           effect->summon_ability == alias && effect->resources == slot + 1 &&
           (effect->s.flags & EF_NOT_SELECTABLE);
}

static LPEDICT haunted_ring_effect(LPEDICT mine, DWORD alias, DWORD slot) {
    FILTER_EDICTS(effect, haunted_ring_effect_matches(effect, mine, alias, slot)) {
        return effect;
    }
    return NULL;
}

static void haunted_mine_ensure_effects(LPEDICT mine) {
    DWORD alias, capacity;

    if (!mine || !mine->inuse || M_IsDead(mine) ||
        !(alias = haunted_mine_alias(mine)) ||
        !(capacity = haunted_mine_max_miners(mine))) return;

    FOR_LOOP(i, capacity) {
        VECTOR2 point;
        LPEDICT effect;
        double angle;

        if (haunted_ring_effect(mine, alias, i)) continue;
        haunted_mine_slot_position(mine, i, capacity, &point);
        effect = G_SpawnAbilityEffectAtPoint(alias, WC3_EFFECT_EFFECT, 0, &point, false);
        if (!effect) continue;

        angle = ((M_PI * 2.0) / (double)capacity) * (double)i + (M_PI / 2.0);
        effect->owner = mine;
        effect->summon_ability = alias;
        effect->resources = i + 1;
        effect->s.angle = (FLOAT)(angle * (180.0 / M_PI));
        gi.LinkEntity(effect);
    }
}

static void haunted_mine_remove_effects(LPEDICT mine) {
    DWORD const base = MAKEFOURCC('A','b','g','m');

    if (!mine) return;
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT effect = g_edicts + i;
        if (effect->inuse && effect->owner == mine && effect->summon_ability &&
            (effect->summon_ability == base || G_AbilityCode(effect->summon_ability) == base) &&
            effect->resources > 0 && (effect->s.flags & EF_NOT_SELECTABLE)) {
            /* A render component may keep playing its Death sequence after the
             * mine edict is released; do not leave a serialized owner pointer
             * aimed at an edict slot that can be reused meanwhile. */
            effect->owner = NULL;
            effect->summon_ability = 0;
            effect->resources = 0;
            G_DestroyEffect(effect);
        }
    }
}

/* Test whether an Acolyte already owns a particular mining-ring slot. */
static BOOL haunted_slot_occupied(LPEDICT mine, LONG slot) {
    FILTER_EDICTS(worker, worker->inuse && worker->acolyte_mine.mine == mine &&
                  worker->acolyte_mine.mine_spawn_time == mine->spawn_time &&
                  worker->acolyte_mine.slot == slot) {
        return true;
    }
    return false;
}

/* Count live Acolytes currently assigned to this Haunted Mine. */
static DWORD haunted_active_miners(LPEDICT mine) {
    DWORD count = 0;
    if (!mine) return 0;
    FILTER_EDICTS(worker, worker->inuse && worker->acolyte_mine.mine == mine &&
                  worker->acolyte_mine.mine_spawn_time == mine->spawn_time &&
                  worker->acolyte_mine.slot >= 0) {
        count++;
    }
    return count;
}

/* Validate ownership, construction state, parent lifetime, and remaining gold for a worker order. */
static BOOL haunted_mine_valid_for(LPEDICT worker, LPEDICT mine) {
    LPEDICT parent;
    if (!worker || !mine || !mine->inuse || M_IsDead(mine) || mine->construction.active ||
        worker->s.player != mine->s.player || !haunted_mine_alias(mine)) return false;
    parent = mineoverlay_parent(mine);
    return parent && parent->resources > 0;
}

/* Determine whether the worker has reached the mine's authored interaction range. */
static BOOL acolyte_in_harvest_range(LPEDICT worker, LPEDICT mine) {
    DWORD alias = goldmine_actor_ability_alias(worker, MAKEFOURCC('A','a','h','a'));
    FLOAT const range = alias ? MAX(0.0f, G_AbilityLevel(alias, 1)->range) : 0.0f;
    FLOAT footprint;
    if (!worker || !mine) return false;
    footprint = CM_DistanceToPathingFootprint(mine, &worker->s.origin2);
    if (footprint < FLT_MAX) return footprint <= worker->collision + range;
    return Vector2_distance(&worker->s.origin2, &mine->s.origin2) <=
           worker->collision + mine->collision + range;
}

/* Claim the nearest unoccupied authored ring slot for an Acolyte. */
static BOOL acolyte_claim_slot(LPEDICT worker, LPEDICT mine) {
    DWORD const capacity = haunted_mine_max_miners(mine);
    LONG best = -1;
    FLOAT best_distance = FLT_MAX;

    if (!capacity) return false;
    FOR_LOOP(i, capacity) {
        VECTOR2 point;
        FLOAT dx, dy, distance;
        if (haunted_slot_occupied(mine, (LONG)i)) continue;
        haunted_mine_slot_position(mine, i, capacity, &point);
        dx = point.x - worker->s.origin2.x;
        dy = point.y - worker->s.origin2.y;
        distance = dx * dx + dy * dy;
        if (distance < best_distance) {
            best_distance = distance;
            best = (LONG)i;
        }
    }
    if (best < 0) return false;

    worker->acolyte_mine.mine = mine;
    worker->acolyte_mine.mine_spawn_time = mine->spawn_time;
    worker->acolyte_mine.slot = best;
    return true;
}

/* Snap an active Acolyte to its persistent ring slot and terrain height. */
static void acolyte_snap_to_slot(LPEDICT worker) {
    LPEDICT mine;
    DWORD capacity;
    VECTOR2 point;
    if (!S_AcolyteHarvestIsActive(worker) || !(mine = worker->acolyte_mine.mine)) return;
    capacity = haunted_mine_max_miners(mine);
    if (!capacity || worker->acolyte_mine.slot < 0 || (DWORD)worker->acolyte_mine.slot >= capacity) return;
    haunted_mine_slot_position(mine, (DWORD)worker->acolyte_mine.slot, capacity, &point);
    worker->s.origin2 = point;
    worker->s.origin.x = point.x;
    worker->s.origin.y = point.y;
    worker->s.origin.z = CM_GetHeightAtPoint(point.x, point.y);
    gi.LinkEntity(worker);
}

static void ai_acolyte_harvest_walk(LPEDICT worker);
static void ai_acolyte_harvest_work(LPEDICT worker);
static umove_t acolyte_harvest_move_walk = { "walk", ai_acolyte_harvest_walk, NULL, CAbilityAcolyteHarvest };
static umove_t acolyte_harvest_move_work = { "stand work", ai_acolyte_harvest_work, NULL, CAbilityAcolyteHarvest };

/* Advance an Acolyte toward its assigned Haunted Mine and claim its slot on arrival. */
static void ai_acolyte_harvest_walk(LPEDICT worker) {
    LPEDICT mine = worker ? worker->goalentity : NULL;
    if (!haunted_mine_valid_for(worker, mine)) {
        if (worker) unit_stand(worker);
        return;
    }
    if (acolyte_in_harvest_range(worker, mine)) {
        if (!acolyte_claim_slot(worker, mine)) {
            LPEDICT clent = G_GetPlayerEntityByNumber(worker->s.player);
            if (clent && clent->client)
                G_ShowCommandErrorKey(clent, "Blightringfull",
                                      "That gold mine can't support any more Acolytes.");
            unit_stand(worker);
            return;
        }
        acolyte_snap_to_slot(worker);
        unit_setmove(worker, &acolyte_harvest_move_work);
        return;
    }
    unit_changeangle_interaction_ignore_units(worker);
    if (worker->movement.flow_unreachable) {
        unit_stand(worker);
        return;
    }
    unit_moveindirection_ignore_units(worker);
}

/* Maintain the Acolyte's position while it is working in the Haunted Mine ring. */
static void ai_acolyte_harvest_work(LPEDICT worker) {
    LPEDICT mine = worker ? worker->acolyte_mine.mine : NULL;
    if (!haunted_mine_valid_for(worker, mine) || !S_AcolyteHarvestIsActive(worker)) {
        if (worker) unit_stand(worker);
        return;
    }
    acolyte_snap_to_slot(worker);
}

BOOL S_AcolyteHarvestOrder(LPEDICT worker, LPEDICT mine) {
    if (!worker || !mine || !goldmine_actor_ability_alias(worker, MAKEFOURCC('A','a','h','a')) ||
        !haunted_mine_valid_for(worker, mine)) {
        return false;
    }
    if (S_GoldMineWorkerIsInside(worker) || S_CargoTransportForUnit(worker)) {
        return false;
    }

    G_ClearUnitOrderQueue(worker);
    S_AcolyteHarvestRelease(worker);
    worker->movement.follow_target = NULL;
    worker->movement.attackmove_waypoint = NULL;
    worker->movement.patrol_a = NULL;
    worker->movement.patrol_b = NULL;
    worker->movement.patrol_target = NULL;
    worker->movement.holding_position = false;
    worker->goalentity = mine;
    worker->secondarygoal = mine;
    move_reset_progress(worker);
    unit_setmove(worker, &acolyte_harvest_move_walk);
    return true;
}

void blight_mine_think(LPEDICT mine) {
    DWORD alias, maximum, active, multiplier, interval_ms, now;
    LONG gold_per_interval, gold;
    LPEDICT parent;
    LPPLAYER player;

    monster_think(mine);
    if (!mine || !mine->inuse || M_IsDead(mine)) return;
    /* Warsmash creates the Abgm ring render components in onAdd(), so they are
     * visible during construction as well as after completion. Lazy ensure on
     * the authoritative mine thinker gives preplaced, constructed, and loaded
     * mines the same presentation without a second unit-lifecycle hook. */
    haunted_mine_ensure_effects(mine);
    if (mine->construction.active || !(alias = haunted_mine_alias(mine))) return;
    parent = mineoverlay_parent(mine);
    player = G_GetPlayerByNumber(mine->s.player);
    maximum = haunted_mine_max_miners(mine);
    active = haunted_active_miners(mine);
    if (!parent || !player || !maximum || !active || parent->resources == 0) return;

    /* Match Warsmash's Java integer division here: 5/4 and 5/3 both produce
     * multiplier 1, 5/2 produces 2, and one Acolyte produces 5. */
    active = MIN(active, maximum);
    multiplier = MAX(1u, maximum / active);
    interval_ms = (DWORD)(MAX(0.0f, G_AbilityLevel(alias, 1)->data[1].number) * 1000.0f);
    interval_ms = MAX(1u, interval_ms * multiplier);
    now = G_Time();
    if (now < mine->mineoverlay.income_time + interval_ms) return;

    gold_per_interval = (LONG)MAX(0.0f, G_AbilityLevel(alias, 1)->data[0].number);
    gold = MIN((LONG)parent->resources, gold_per_interval);
    mine->mineoverlay.income_time = now;
    if (gold <= 0) return;
    parent->resources -= (DWORD)gold;
    G_CreditResourceIncome(player, mine, PLAYERSTATE_RESOURCE_GOLD, gold);
}

BZ_ABILITY_PROC(CAbilityBlightedGoldMine) {
    return CAbilityPassive(ent, msg, call);
}

/* ---- Entangle Gold Mine / Entangled Mine -------------------------------- */

static BOOL entangle_goldmine_selecttarget(LPEDICT clent, LPEDICT target) {
    LPEDICT caster, entangled;
    DWORD alias, resulting_type;
    BOOL bound, started;

    if (!clent || !clent->client) {
        return false;
    }
    if (!target || !target->inuse || M_IsDead(target)) {
        return false;
    }
    if (!S_GoldMineIsMine(target)) {
        return false;
    }
    if (goldmine_is_overlay_type(target) || (target->s.renderfx & RF_HIDDEN) ||
        mineoverlay_parent_in_use(target, NULL)) {
        return false;
    }
    caster = G_GetMainSelectedUnit(clent->client);
    if (!caster || !(alias = goldmine_actor_ability_alias(caster, MAKEFOURCC('A','e','n','t')))) {
        return false;
    }
    resulting_type = G_AbilityLevel(alias, 1)->unitID;
    if (!resulting_type || !G_UnitIsBuilding(resulting_type)) {
        return false;
    }

    entangled = SP_SpawnAtLocation(resulting_type, caster->s.player, &target->s.origin2);
    if (!entangled) {
        return false;
    }
    bound = S_MineOverlayBind(entangled, target);
    started = bound && G_StartNightElfOverlayConstruction(entangled);
    if (!started) {
        S_MineOverlayRelease(entangled);
        G_FreeEdict(entangled);
        return false;
    }
    G_SetUnitFoodUsed(entangled, entangled->data.UnitBalance ? entangled->data.UnitBalance->foodUsed : 0);
    entangled->build = entangled;
    CM_BakeStaticObstacles();
    G_PublishEvent(entangled, EVENT_PLAYER_UNIT_CONSTRUCT_START);
    return true;
}

static void entangle_goldmine_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = entangle_goldmine_selecttarget;
}

BZ_ABILITY_PROC(CAbilityEntangle) {
    if (msg != A_COMMAND) return false;
    entangle_goldmine_command(call && call->client ? call->client : ent);
    return true;
}

void S_EntangledMineTick(LPEDICT mine) {
    DWORD alias, capacity, interval_ms, now, index;
    LONG gold_per_interval, gold;
    LPEDICT parent;
    LPPLAYER player;

    if (!mine || !mine->inuse || M_IsDead(mine) || mine->construction.active ||
        !(alias = goldmine_actor_ability_alias(mine, MAKEFOURCC('A','e','g','m')))) return;
    parent = mineoverlay_parent(mine);
    player = G_GetPlayerByNumber(mine->s.player);
    capacity = S_CargoCapacity(mine);
    if (!parent || !player || !capacity) return;

    now = G_Time();
    if (now < mine->mineoverlay.income_time) return;
    interval_ms = (DWORD)(MAX(0.0f, G_AbilityLevel(alias, 1)->data[1].number) * 1000.0f);
    mine->mineoverlay.income_time = now + MAX(1u, interval_ms);

    if (parent->resources == 0) {
        unit_die(mine, NULL);
        return;
    }
    index = (mine->mineoverlay.active_interval_index + 1) % capacity;
    mine->mineoverlay.active_interval_index = index;
    if (index >= mine->cargo.count) return;

    gold_per_interval = (LONG)MAX(0.0f, G_AbilityLevel(alias, 1)->data[0].number);
    gold = MIN((LONG)parent->resources, gold_per_interval);
    if (gold > 0) {
        parent->resources -= (DWORD)gold;
        G_CreditResourceIncome(player, mine, PLAYERSTATE_RESOURCE_GOLD, gold);
    }
    if (parent->resources == 0 && mine->inuse && !M_IsDead(mine))
        unit_die(mine, NULL);
}
