#include "s_skills.h"
#include <float.h>

void repair_build_primary(LPEDICT ent, LPEDICT building);
void repair_build_legacy(LPEDICT ent, LPEDICT building);

static umove_t repair_move_walk;
static umove_t repair_move_work;
static umove_t repair_generic_move_walk;
static umove_t repair_generic_move_work;
static umove_t repair_legacy_move_work;

static BOOL repair_autocast_is_on(LPEDICT ent);

static void repair_code_string(DWORD code, char out[5]) {
    memcpy(out, &code, 4);
    out[4] = '\0';
}

static ability_t const *repair_handler(DWORD code) {
    char rawcode[5];
    if (!code) return NULL;
    repair_code_string(code, rawcode);
    return FindAbilityForCommand(rawcode);
}

static DWORD repair_find_code(LPEDICT ent, ability_t const *wanted, DWORD preferred) {
    DWORD fallback = 0;
    LPCSTR abilities;

    if (!ent || !ent->UnitAbilities) return 0;
    abilities = ent->UnitAbilities->abilList;
    if (!abilities) return 0;

    PARSE_LIST(abilities, ability_name, parse_segment) {
        ability_t const *handler = FindAbilityForCommand(ability_name);
        DWORD code;
        if (handler != &a_repair && handler != &a_repair_generic) continue;
        if (wanted && handler != wanted) continue;
        code = FS_SLKKey(ability_name);
        if (preferred && code == preferred) return code;
        if (!fallback) fallback = code;
    }
    return fallback;
}

static AbilityData_t const *repair_data(LPEDICT ent) {
    return ent && ent->buildwork.ability ? G_AbilityData(ent->buildwork.ability) : NULL;
}

static BOOL repair_primary_active(LPEDICT building) {
    LPEDICT worker;
    if (!building) return false;
    worker = building->construction.primary_builder;
    return worker && worker->inuse && !(worker->svflags & SVF_DEADMONSTER) && worker->build == building &&
           worker->currentmove && worker->currentmove->ability == &a_repair;
}

static void repair_release(LPEDICT ent) {
    LPEDICT building;
    if (!ent) return;
    building = ent->build;
    if (ent->buildwork.primary && building && building->construction.primary_builder == ent) {
        building->construction.primary_builder = NULL;
    }
    if (ent->build == building) ent->build = NULL;
    ent->buildwork.primary = false;
    ent->buildwork.ability = 0;
    ent->buildwork.gold_accum = 0.0f;
    ent->buildwork.lumber_accum = 0.0f;
}

void S_CancelRepair(LPEDICT ent) {
    ability_t const *ability;
    LPEDICT building;
    LPEDICT goal;

    if (!ent || !ent->buildwork.ability) return;
    ability = repair_handler(ent->buildwork.ability);
    if (ability != &a_repair && ability != &a_repair_generic) return;
    building = ent->build;
    goal = ent->goalentity;
    /* A replacement order installs its goal before unit_setmove() cancels the
     * old Repair move. Preserve that replacement goal, but still retire the
     * Repair-owned goal on an ordinary Stop/stand transition. unit_stand()
     * clears build before switching moves, so when no build pointer remains the
     * current Repair goal is the best surviving identity for the old target. */
    if (!building && ent->currentmove && ent->currentmove->ability == ability)
        building = goal;
#ifdef WC3_DEBUG_AUTOCAST
    if (G_AutocastDebugLevel() >= 1) {
        fprintf(stderr,
                "WC3_AUTOREPAIR cancel worker=%ld build=%ld goal=%ld move=%s autocast=%d\n",
                g_edicts ? (long)(ent - g_edicts) : -1L,
                building && g_edicts ? (long)(building - g_edicts) : -1L,
                goal && g_edicts ? (long)(goal - g_edicts) : -1L,
                ent->currentmove && ent->currentmove->animation ? ent->currentmove->animation : "<none>",
                repair_autocast_is_on(ent) ? 1 : 0);
    }
#endif
    repair_release(ent);
    if (ent->goalentity == building) ent->goalentity = NULL;
}

static void repair_stop_reason(LPEDICT ent, LPCSTR reason) {
#ifdef WC3_DEBUG_AUTOCAST
    LPEDICT building = ent ? ent->build : NULL;

    if (G_AutocastDebugLevel() >= 1 && ent) {
        fprintf(stderr,
                "WC3_AUTOREPAIR stop worker=%ld build=%ld reason=%s hp=%.1f/%.1f autocast=%d\n",
                g_edicts ? (long)(ent - g_edicts) : -1L,
                building && g_edicts ? (long)(building - g_edicts) : -1L,
                reason ? reason : "unknown",
                building ? building->health.value : 0.0f,
                building ? building->health.max_value : 0.0f,
                repair_autocast_is_on(ent) ? 1 : 0);
    }
#else
    (void)reason;
#endif
    if (ent) ent->goalentity = NULL;
    repair_release(ent);
    if (ent && ent->stand) ent->stand(ent);
}

static FLOAT repair_time(UnitBalance_t const *balance) {
    if (!balance) return 0.0f;
    if (balance->reptm > 0) return (FLOAT)balance->reptm;
    /* TODO: Current ROC/test rows can omit reptm. Preserve the old build-time
     * duration for those rows until the normalized unit-data import always
     * exposes the authoritative repair-time field. */
    return (FLOAT)balance->buildTime;
}

static BOOL repair_charge(LPEDICT ent, FLOAT gold_rate, FLOAT lumber_rate) {
    LPGAMECLIENT client;
    FLOAT seconds;
    LONG gold_due, lumber_due;

    if (!ent) return false;
    client = G_GetPlayerClientByNumber(ent->s.player);
    if (!client) return false;

    seconds = (FLOAT)FRAMETIME / 1000.0f;
    ent->buildwork.gold_accum += MAX(0.0f, gold_rate) * seconds;
    ent->buildwork.lumber_accum += MAX(0.0f, lumber_rate) * seconds;
    gold_due = (LONG)floorf(ent->buildwork.gold_accum);
    lumber_due = (LONG)floorf(ent->buildwork.lumber_accum);

    if (gold_due > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] ||
        lumber_due > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]) {
        return false;
    }
    if (gold_due > 0) {
        client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] -= gold_due;
        ent->buildwork.gold_accum -= gold_due;
    }
    if (lumber_due > 0) {
        client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] -= lumber_due;
        ent->buildwork.lumber_accum -= lumber_due;
    }
    if (gold_due || lumber_due) {
        G_RefreshResourceBar(G_GetPlayerEntityByNumber(ent->s.player));
    }
    return true;
}

static BOOL repair_charge_power_cost(LPEDICT ent, LPEDICT building, AbilityData_t const *data) {
    UnitBalance_t const *balance;
    FLOAT build_time;
    FLOAT cost_ratio;

    if (!ent || !building || ent->buildwork.primary || G_BuildAllEnabled()) return true;
    balance = building->UnitBalance;
    build_time = balance ? (FLOAT)balance->buildTime : 0.0f;
    cost_ratio = data ? data->data[0][2] : 0.0f;
    if (build_time <= 0.0f || cost_ratio <= 0.0f) return true;

    return repair_charge(ent,
        ((FLOAT)balance->goldRep / build_time) * cost_ratio,
        ((FLOAT)balance->lumberRep / build_time) * cost_ratio);
}

static BOOL repair_target_valid(LPEDICT ent, LPEDICT target, DWORD code, BOOL primary) {
    ability_t const *handler = repair_handler(code);
    AbilityData_t const *data = G_AbilityData(code);

    if (!ent || !target || !target->inuse || M_IsDead(target)) return false;
    if (!G_UnitIsBuilding(target->class_id) || !target->UnitBalance) return false;
    /* Keep the current ownership/building rule until Repair has a target-mask
     * evaluator that understands WC3's overlapping categories (for example a
     * structure can also be a ground target).  S_SpellAllowsTarget() models a
     * smaller spell subset and can reject otherwise-valid Repair structures. */
    if (target->s.player != ent->s.player) return false;

    if (target->construction.active) {
        /* DataD is the extra-worker power-build ratio. The primary Human
         * builder always contributes at 1.0 and must not be rejected merely
         * because DataD is zero/missing for additional workers. */
        return handler == &a_repair && data && target->construction.paused &&
               (primary || data->data[0][3] > 0.0f);
    }
    return target->health.value < target->health.max_value;
}

static FLOAT repair_range(LPEDICT ent) {
    AbilityData_t const *data = repair_data(ent);
    return data ? MAX(0.0f, data->range[0]) : 0.0f;
}

/* Work may begin only from the worker's current position.  Do not include the
 * next movement step here: doing so makes walk hand off early, then the work
 * state immediately fail the same range check before applying repair/build
 * progress, producing a walk/work oscillation with zero HP/progress change. */
static BOOL repair_in_range(LPEDICT ent, LPEDICT target) {
    FLOAT footprint;
    FLOAT range;

    if (!ent || !target) return false;
    range = repair_range(ent);
    footprint = CM_DistanceToPathingFootprint(target, &ent->s.origin2);
    if (footprint < FLT_MAX) {
        return footprint <= ent->collision + range;
    }
    return M_DistanceToGoal(ent) <= ent->collision + target->collision + range;
}

static void repair_set_work(LPEDICT ent) {
    LPEDICT building = ent ? ent->build : NULL;

    if (!ent || !building) return;
    ent->goalentity = building;
    move_reset_progress(ent);
    if (repair_handler(ent->buildwork.ability) == &a_repair_generic)
        unit_setmove(ent, &repair_generic_move_work);
    else
        unit_setmove(ent, &repair_move_work);
}

static BOOL repair_prepare_approach(LPEDICT ent) {
    LPEDICT building = ent ? ent->build : NULL;
    VECTOR2 approach;
    FLOAT interaction_range;
    BOOL found;

    if (!ent || !building) return false;
    interaction_range = ent->collision + repair_range(ent);
    found = CM_FindApproachPointToFootprintForRadius(
        building, &ent->s.origin2, interaction_range, ent->collision, &approach);
    if (found) {
        ent->goalentity = Waypoint_add(&approach);
        move_reset_progress(ent);
        return true;
    }

    /* Models without an authored footprint retain the legacy centre/collision
     * fallback, but still use collision-sized routing. */
    if (!building->pathtex) {
        ent->goalentity = building;
        move_reset_progress(ent);
        return true;
    }

    return false;
}

static BOOL repair_set_walk(LPEDICT ent) {
    if (!repair_prepare_approach(ent)) {
        repair_stop_reason(ent, "no_approach");
        return false;
    }
    if (repair_handler(ent->buildwork.ability) == &a_repair_generic)
        unit_setmove(ent, &repair_generic_move_walk);
    else
        unit_setmove(ent, &repair_move_walk);
    return true;
}

static void ai_repair_walk(LPEDICT ent) {
    LPEDICT building = ent ? ent->build : NULL;
    FLOAT distance, step;

    if (!building || !repair_target_valid(ent, building, ent->buildwork.ability,
                                           ent->buildwork.primary)) {
        repair_stop_reason(ent, "walk_target_invalid");
        return;
    }
    if (repair_in_range(ent, building)) {
        repair_set_work(ent);
        return;
    }

    distance = M_DistanceToGoal(ent);
    step = unit_movedistance(ent);
    if (move_is_blocked(ent, distance, step)) {
        repair_stop_reason(ent, "walk_blocked");
        return;
    }

    unit_changeangle_for_radius(ent, ent->collision);

    if (ent->movement.flow_goal_reached && !repair_in_range(ent, building)) {
        repair_stop_reason(ent, "walk_goal_reached_out_of_range");
        return;
    }
    if (ent->movement.flow_unreachable) {
        repair_stop_reason(ent, "walk_unreachable");
        return;
    }
    unit_moveindirection(ent);
}

static void ai_repair(LPEDICT ent) {
    LPEDICT building = ent ? ent->build : NULL;
    AbilityData_t const *data;
    EDICTSTAT *hp;

    if (!building || !repair_target_valid(ent, building, ent->buildwork.ability,
                                           ent->buildwork.primary)) {
        repair_stop_reason(ent, "work_target_invalid");
        return;
    }
    if (!repair_in_range(ent, building)) {
        repair_set_walk(ent);
        return;
    }

    data = repair_data(ent);
    hp = &building->health;
    unit_changeangle(ent);

    if (building->construction.active) {
        FLOAT ratio;
        FLOAT duration;
        FLOAT hp_gain;
        FLOAT start_hp;

        if (!building->construction.paused || !data) {
            repair_stop_reason(ent, "construction_not_paused_or_no_data");
            return;
        }
        if (ent->buildwork.primary) {
            if (building->construction.primary_builder != ent) {
                repair_stop_reason(ent, "lost_primary_builder");
                return;
            }
            ratio = 1.0f;
        } else {
            ratio = data->data[0][3];
            if (ratio <= 0.0f) {
                repair_stop_reason(ent, "invalid_power_build_ratio");
                return;
            }
        }
        if (!repair_charge_power_cost(ent, building, data)) {
            repair_stop_reason(ent, "power_build_unaffordable");
            return;
        }

        duration = MAX(1.0f, (FLOAT)building->UnitBalance->buildTime * 1000.0f);
        building->construction.progress += (FLOAT)FRAMETIME * ratio;
        G_UpdateConstructionAnimation(building);
        start_hp = MAX(1.0f, hp->max_value * 0.10f);
        hp_gain = (hp->max_value - start_hp) * ((FLOAT)FRAMETIME * ratio / duration);
        hp->value = MIN(hp->max_value, hp->value + hp_gain);
        if (building->construction.progress >= duration) {
            G_CompleteConstruction(building);
            repair_stop_reason(ent, "construction_complete");
        }
        return;
    }

    if (data) {
        UnitBalance_t const *balance = building->UnitBalance;
        FLOAT seconds = (FLOAT)FRAMETIME / 1000.0f;
        FLOAT duration = repair_time(balance);
        FLOAT cost_ratio = data->data[0][0];
        FLOAT time_ratio = data->data[0][1];
        FLOAT hp_rate;

        if (duration <= 0.0f || time_ratio <= 0.0f) {
            repair_stop_reason(ent, "invalid_repair_rates");
            return;
        }
        hp_rate = (hp->max_value / duration) * time_ratio;
        if (!repair_charge(ent,
                ((FLOAT)balance->goldRep / duration) * cost_ratio * time_ratio,
                ((FLOAT)balance->lumberRep / duration) * cost_ratio * time_ratio)) {
            repair_stop_reason(ent, "repair_unaffordable");
            return;
        }
        hp->value = MIN(hp->max_value, hp->value + hp_rate * seconds);
    }
    if (hp->value >= hp->max_value) {
        hp->value = hp->max_value;
        building->stand(building);
        repair_stop_reason(ent, "repair_complete");
    }
}

static void ai_repair_legacy(LPEDICT ent) {
    LPEDICT building = ent ? ent->build : NULL;
    EDICTSTAT *hp;

    if (!building || !building->inuse || M_IsDead(building) || building->UnitBalance->buildTime <= 0) {
        if (ent) ent->stand(ent);
        return;
    }
    hp = &building->health;
    hp->value += hp->max_value * (FLOAT)FRAMETIME /
                 ((FLOAT)building->UnitBalance->buildTime * 1000.0f);
    if (hp->value >= hp->max_value) {
        hp->value = hp->max_value;
        building->stand(building);
        ent->stand(ent);
    }
}

static umove_t repair_move_walk = { "walk", ai_repair_walk, NULL, &a_repair };
static umove_t repair_move_work = { "stand work", ai_repair, NULL, &a_repair };
static umove_t repair_generic_move_walk = { "walk", ai_repair_walk, NULL, &a_repair_generic };
static umove_t repair_generic_move_work = { "stand work", ai_repair, NULL, &a_repair_generic };
static umove_t repair_legacy_move_work = { "stand work", ai_repair_legacy, NULL, &a_repair };

static BOOL repair_begin(LPEDICT ent, LPEDICT building, DWORD code, BOOL primary) {
    VECTOR2 origin;
    FLOAT angle;

    if (!ent || !building || !code || !repair_target_valid(ent, building, code, primary)) return false;
    S_CancelRepair(ent);
    ent->build = building;
    ent->goalentity = building;
    ent->buildwork.primary = primary;
    ent->buildwork.ability = code;
    ent->buildwork.gold_accum = 0.0f;
    ent->buildwork.lumber_accum = 0.0f;
    move_reset_progress(ent);
#ifdef WC3_DEBUG_AUTOCAST
    if (G_AutocastDebugLevel() >= 1) {
        char rawcode[5];
        repair_code_string(code, rawcode);
        fprintf(stderr,
                "WC3_AUTOREPAIR begin worker=%ld target=%ld code=%s primary=%d in_range=%d autocast=%d\n",
                g_edicts ? (long)(ent - g_edicts) : -1L,
                g_edicts ? (long)(building - g_edicts) : -1L,
                rawcode, primary ? 1 : 0,
                repair_in_range(ent, building) ? 1 : 0,
                repair_autocast_is_on(ent) ? 1 : 0);
    }
#endif

    if (primary) {
        /* The initial Human builder starts inside the newly baked footprint.
         * Relocate only this construction transition; ordinary Repair orders
         * must approach through pathfinding instead of teleporting. */
        if (!SP_FindUnitExitPosition(building, ent, &origin, &angle)) {
            repair_release(ent);
            if (ent->stand) ent->stand(ent);
            return false;
        }
        ent->s.origin2 = origin;
        ent->s.angle = angle - M_PI;
        gi.LinkEntity(ent);
        building->construction.primary_builder = ent;
    }

    if (repair_in_range(ent, building)) repair_set_work(ent);
    else if (!repair_set_walk(ent)) return false;
    return true;
}

void repair_build_primary(LPEDICT ent, LPEDICT building) {
    DWORD code = repair_find_code(ent, &a_repair, 0);
    if (!code || !repair_begin(ent, building, code, true)) {
        if (building && building->construction.primary_builder == ent)
            building->construction.primary_builder = NULL;
    }
}

void repair_build_legacy(LPEDICT ent, LPEDICT building) {
    VECTOR2 origin;
    FLOAT angle;

    if (!ent || !building) return;
    if (!SP_FindUnitExitPosition(building, ent, &origin, &angle)) {
        if (ent->stand) ent->stand(ent);
        return;
    }
    ent->s.origin2 = origin;
    ent->s.angle = angle - M_PI;
    gi.LinkEntity(ent);
    ent->build = building;
    ent->goalentity = building;
    ent->buildwork.primary = false;
    ent->buildwork.ability = 0;
    ent->buildwork.gold_accum = 0.0f;
    ent->buildwork.lumber_accum = 0.0f;
    unit_setmove(ent, &repair_legacy_move_work);
}

BOOL G_UnitHasHumanRepair(LPEDICT ent) {
    return repair_find_code(ent, &a_repair, 0) != 0;
}

BOOL S_OrderRepair(LPEDICT ent, LPEDICT target, DWORD preferred) {
    ability_t const *wanted = NULL;
    DWORD code;
    BOOL primary = false;

    if (!ent || !target) return false;
    if (preferred) {
        wanted = repair_handler(preferred);
        if (wanted != &a_repair && wanted != &a_repair_generic) return false;
    }
    code = repair_find_code(ent, wanted, preferred);
    if (!code) return false;

    if (target->construction.active) {
        if (repair_handler(code) != &a_repair) return false;
        if (!repair_primary_active(target)) {
            target->construction.primary_builder = NULL;
            primary = true;
        }
    }
    if (!repair_target_valid(ent, target, code, primary)) return false;

    return repair_begin(ent, target, code, primary);
}

BOOL S_RepairSmart(LPEDICT ent, LPEDICT target) {
    return S_OrderRepair(ent, target, 0);
}

#define REPAIR_AUTOCAST_MAX_TARGETS 256 // entities; bounded candidates considered by one Auto Repair acquisition scan

static BOOL repair_autocast_is_on(LPEDICT ent) {
    return ent && (ent->aiflags & AI_AUTOCAST_REPAIR) != 0;
}

static void repair_autocast_set(LPEDICT ent, BOOL enabled) {
    if (!ent) return;
    if (enabled) ent->aiflags |= AI_AUTOCAST_REPAIR;
    else ent->aiflags &= ~AI_AUTOCAST_REPAIR;
}

static LPCSTR repair_autocast_reject_reason(LPEDICT ent, LPEDICT target, DWORD code) {
    ability_t const *handler;
    AbilityData_t const *data;
    BOOL primary = false;

    if (!ent) return "no_worker";
    if (!target) return "no_target";
    if (!code) return "no_repair_code";
    if (!target->inuse) return "unused";
    if (M_IsDead(target)) return "dead";
    if (!G_UnitIsBuilding(target->class_id)) return "not_building";
    if (!target->UnitBalance) return "no_balance";
    if (target->s.player != ent->s.player) return "wrong_owner";

    handler = repair_handler(code);
    data = G_AbilityData(code);
    if (target->construction.active) {
        if (handler != &a_repair) return "construction_requires_human_repair";
        if (!target->construction.paused) return "construction_not_paused";
        if (!repair_primary_active(target)) primary = true;
        if (!primary && (!data || data->data[0][3] <= 0.0f)) return "no_power_build_ratio";
    } else if (target->health.value >= target->health.max_value) {
        return "full_health";
    }
    return repair_target_valid(ent, target, code, primary) ? NULL : "repair_target_rules";
}

/* Warsmash CUnit.distance() compares unit edges by subtracting both collision
 * sizes, not just center-to-center distance. This matters most for large
 * buildings: a Peasant can be visibly close to the footprint while the
 * building center lies beyond uacq. */
static FLOAT repair_autocast_distance(LPCEDICT ent, LPCEDICT target) {
    FLOAT distance;

    if (!ent || !target) return FLT_MAX;
    distance = Vector2_distance(&target->s.origin2, &ent->s.origin2) -
               MAX(0.0f, ent->collision) - MAX(0.0f, target->collision);
    return MAX(0.0f, distance);
}

BOOL S_SetRepairAutocast(LPEDICT ent, BOOL enabled) {
    DWORD code;
    ability_t const *ability;

    if (!ent) return false;
    code = repair_find_code(ent, NULL, 0);
    ability = repair_handler(code);
    if (!ability) return false;
    return G_SetUnitAutocast(ent, ability, enabled);
}

/* Repair uses Warsmash's NEARESTVALID autocast policy: acquisition range only
 * bounds candidate discovery. Once chosen, the ordinary Repair order owns
 * pathing into Repair range and all subsequent validation/cost behavior. */
static BOOL repair_autocast_acquire(LPEDICT ent) {
    LPEDICT candidates[REPAIR_AUTOCAST_MAX_TARGETS];
    LPEDICT best = NULL;
    FLOAT radius;
    FLOAT best_distance;
    DWORD code;
    BOX2 area;
    DWORD count;

    if (!ent) return false;
    if (M_IsDead(ent) || S_GoldMineWorkerIsInside(ent)) {
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 1) {
            fprintf(stderr,
                    "WC3_AUTOREPAIR scan_skip worker=%ld reason=%s\n",
                    g_edicts ? (long)(ent - g_edicts) : -1L,
                    M_IsDead(ent) ? "worker_dead" : "worker_inside_goldmine");
        }
#endif
        return false;
    }
    code = repair_find_code(ent, NULL, 0);
    radius = G_AcquisitionRange(ent);
    if (!code || radius <= 0.0f) {
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 1) {
            fprintf(stderr,
                    "WC3_AUTOREPAIR scan_skip worker=%ld reason=%s radius=%.1f abilities=%s\n",
                    g_edicts ? (long)(ent - g_edicts) : -1L,
                    !code ? "no_repair_code" : "zero_acquisition_range", radius,
                    ent->UnitAbilities && ent->UnitAbilities->abilList ? ent->UnitAbilities->abilList : "<none>");
        }
#endif
        return false;
    }

    /* Warsmash expands from the source collision rectangle, then lets
     * NEARESTVALID compare collision-aware distances among enumerated units. */
    area = (BOX2){
        { ent->s.origin2.x - radius - ent->collision,
          ent->s.origin2.y - radius - ent->collision },
        { ent->s.origin2.x + radius + ent->collision,
          ent->s.origin2.y + radius + ent->collision },
    };
    count = gi.BoxEdicts(&area, candidates, REPAIR_AUTOCAST_MAX_TARGETS, NULL);
    best_distance = FLT_MAX;
#ifdef WC3_DEBUG_AUTOCAST
    if (G_AutocastDebugLevel() >= 1) {
        char rawcode[5];
        repair_code_string(code, rawcode);
        fprintf(stderr,
                "WC3_AUTOREPAIR scan worker=%ld code=%s pos=(%.1f,%.1f) radius=%.1f collision=%.1f candidates=%u\n",
                g_edicts ? (long)(ent - g_edicts) : -1L, rawcode,
                ent->s.origin2.x, ent->s.origin2.y, radius, ent->collision, count);
    }
#endif
    FOR_LOOP(i, count) {
        LPEDICT target = candidates[i];
        LPCSTR reject;
        FLOAT distance;

        reject = repair_autocast_reject_reason(ent, target, code);
        if (reject) {
#ifdef WC3_DEBUG_AUTOCAST
            if (G_AutocastDebugLevel() >= 2 && target) {
                fprintf(stderr,
                        "WC3_AUTOREPAIR candidate worker=%ld target=%ld class=%.4s owner=%u hp=%.1f/%.1f reject=%s\n",
                        g_edicts ? (long)(ent - g_edicts) : -1L,
                        g_edicts ? (long)(target - g_edicts) : -1L,
                        (LPCSTR)&target->class_id, target->s.player,
                        target->health.value, target->health.max_value, reject);
            }
#endif
            continue;
        }
        distance = repair_autocast_distance(ent, target);
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 2) {
            fprintf(stderr,
                    "WC3_AUTOREPAIR candidate worker=%ld target=%ld class=%.4s hp=%.1f/%.1f distance=%.1f valid=1\n",
                    g_edicts ? (long)(ent - g_edicts) : -1L,
                    g_edicts ? (long)(target - g_edicts) : -1L,
                    (LPCSTR)&target->class_id,
                    target->health.value, target->health.max_value, distance);
        }
#endif
        if (distance < best_distance) {
            best = target;
            best_distance = distance;
        }
    }
    if (!best) {
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 1) {
            fprintf(stderr, "WC3_AUTOREPAIR no_target worker=%ld\n",
                    g_edicts ? (long)(ent - g_edicts) : -1L);
        }
#endif
        return false;
    }
#ifdef WC3_DEBUG_AUTOCAST
    if (G_AutocastDebugLevel() >= 1) {
        fprintf(stderr,
                "WC3_AUTOREPAIR choose worker=%ld target=%ld class=%.4s distance=%.1f hp=%.1f/%.1f\n",
                g_edicts ? (long)(ent - g_edicts) : -1L,
                g_edicts ? (long)(best - g_edicts) : -1L,
                (LPCSTR)&best->class_id, best_distance,
                best->health.value, best->health.max_value);
    }
#endif
    if (!G_IssueUnitTargetOrder(ent, "repair", best, false, ent->s.player)) {
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 1) {
            fprintf(stderr, "WC3_AUTOREPAIR order_failed worker=%ld target=%ld\n",
                    g_edicts ? (long)(ent - g_edicts) : -1L,
                    g_edicts ? (long)(best - g_edicts) : -1L);
        }
#endif
        return false;
    }
    return true;
}

static BOOL repair_selecttarget(LPEDICT clent, LPEDICT target) {
    DWORD code;
    ability_t const *handler;
    BOOL issued = false;

    if (!clent || !clent->client || !target) return false;
    code = clent->client->menu.ability_code;
    handler = repair_handler(code);
    if (handler != &a_repair && handler != &a_repair_generic) return false;
    if (!target->inuse || M_IsDead(target) || !G_UnitIsBuilding(target->class_id) ||
        target->s.player != clent->client->ps.number) {
        return false;
    }

    if (!target->construction.active && target->health.value >= target->health.max_value) {
        G_ShowCommandErrorText(clent, "Target is not damaged.");
        return false;
    }
    if (target->construction.active &&
        (handler != &a_repair || !target->construction.paused)) {
        G_ShowCommandErrorText(clent, "That building is currently under construction.");
        return false;
    }

    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        if (G_IssueUnitTargetOrder(ent, "repair", target,
                                   clent->client->menu.order_queued,
                                   clent->client->ps.number)) {
            issued = true;
        }
    }
    return issued;
}

static void repair_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = repair_selecttarget;
    clent->client->menu.supports_order_queue = true;
}

ability_t a_repair = {
    .cmd = repair_command,
    .autocast_is_on = repair_autocast_is_on,
    .autocast_set = repair_autocast_set,
    .autocast_acquire = repair_autocast_acquire,
};

ability_t a_repair_generic = {
    .cmd = repair_command,
    .autocast_is_on = repair_autocast_is_on,
    .autocast_set = repair_autocast_set,
    .autocast_acquire = repair_autocast_acquire,
};
