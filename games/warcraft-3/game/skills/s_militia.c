#include "s_skills.h"

#define MILITIA_BUFF "Bmil"

static DWORD militia_actor_ability_alias(LPEDICT ent, DWORD base_code) {
    char alias[5] = {0};

    if (!ent) return 0;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            DWORD code = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&code, token, 4);
            if (G_AbilityCode(code) == base_code) return code;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        DWORD const code = ent->abilities.added[i];
        if (!code) continue;
        memcpy(alias, &code, 4);
        if (G_ActorHasSkill(ent, alias) && G_AbilityCode(code) == base_code) return code;
    }
    return 0;
}

static BOOL militia_actor_ability_removed(LPEDICT ent, DWORD base_code) {
    if (!ent) return false;
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.removed)) {
        DWORD const code = ent->abilities.removed[i];
        if (code && G_AbilityCode(code) == base_code) return true;
    }
    return false;
}

static DWORD militia_hall_base_type(LPEDICT hall) {
    return hall && hall->data.UnitAbilities && hall->data.UnitAbilities->id
        ? hall->data.UnitAbilities->id : (hall ? hall->class_id : 0);
}

static BOOL militia_is_first_tier_one_hall(LPEDICT hall) {
    LPEDICT first = NULL;

    if (!hall || militia_hall_base_type(hall) != MAKEFOURCC('h','t','o','w')) return false;
    FILTER_EDICTS(candidate, candidate->inuse && (candidate->svflags & SVF_MONSTER) &&
                  candidate->s.player == hall->s.player &&
                  militia_hall_base_type(candidate) == MAKEFOURCC('h','t','o','w')) {
        if (!first || candidate->spawn_time < first->spawn_time ||
            (candidate->spawn_time == first->spawn_time && candidate->s.number < first->s.number)) {
            first = candidate;
        }
    }
    return first == hall;
}

static BOOL militia_can_recover_hall_ability(LPEDICT hall) {
    DWORD const type = militia_hall_base_type(hall);

    if (!hall || militia_actor_ability_removed(hall, MAKEFOURCC('A','m','i','c'))) return false;
    if (type == MAKEFOURCC('h','k','e','e') || type == MAKEFOURCC('h','c','a','s')) return true;
    return type == MAKEFOURCC('h','t','o','w') && militia_is_first_tier_one_hall(hall);
}

static DWORD militia_hall_ability_alias(LPEDICT hall, BOOL recover) {
    DWORD ability = militia_actor_ability_alias(hall, MAKEFOURCC('A','m','i','c'));

    if (ability || !recover || !militia_can_recover_hall_ability(hall)) return ability;

    /* TFT melee normally injects Amic into the first Human Town Hall from
     * Blizzard.j, while Keep/Castle carry the pairing ability through their
     * authored data.  Recover only those canonical cases when the runtime
     * ability is unexpectedly absent; preserve a static removed-ability
     * marker and never grant it to later tier-one expansion halls. */
    G_ActorAddSkill(hall, MAKEFOURCC('A','m','i','c'));
    ability = militia_actor_ability_alias(hall, MAKEFOURCC('A','m','i','c'));
    return ability;
}

BOOL S_MilitiaEnsureHallAbility(LPEDICT hall) {
    return militia_hall_ability_alias(hall, true) != 0;
}

static void militia_sync_form(LPEDICT unit, DWORD ability) {
    DWORD normal_type, militia_type;

    if (!unit || !ability || unit->militia.ability) return;
    normal_type = S_SpellDataId(ability, 1, 1);
    militia_type = S_SpellDataId(ability, 1, 2);
    if (!normal_type || !militia_type || unit->class_id != militia_type) return;
    unit->militia.ability = ability;
    unit->militia.normal_type = normal_type;
    unit->militia.militia_type = militia_type;
    unit->militia.active = true;
}

static BOOL militia_partner_valid(LPEDICT worker, LPEDICT hall) {
    if (!worker || !hall || !hall->inuse || M_IsDead(worker) || M_IsDead(hall)) return false;
    if (worker->s.player != hall->s.player || worker->paused || hall->paused) return false;
    if (hall != worker->militia.partner || hall->spawn_time != worker->militia.partner_spawn_time) return false;
    if (!militia_actor_ability_alias(worker, MAKEFOURCC('A','m','i','l'))) return false;
    return militia_hall_ability_alias(hall, false) != 0;
}

static BOOL militia_in_range(LPEDICT worker, LPEDICT hall) {
    DWORD const hall_ability = militia_hall_ability_alias(hall, false);
    FLOAT const range = hall_ability ? MAX(0.0f, S_SpellRange(hall_ability, 1)) : 0.0f;
    FLOAT const footprint = CM_DistanceToPathingFootprint(hall, &worker->s.origin2);

    if (footprint < FLT_MAX) return footprint <= worker->collision + range;
    return Vector2_distance(&worker->s.origin2, &hall->s.origin2) <=
        worker->collision + hall->collision + range;
}

static BOOL militia_prepare_approach(LPEDICT worker, LPEDICT hall) {
    DWORD const hall_ability = militia_hall_ability_alias(hall, false);
    VECTOR2 approach;
    FLOAT range;

    if (!worker || !hall || !hall_ability) return false;
    range = worker->collision + MAX(0.0f, S_SpellRange(hall_ability, 1));
    if (CM_FindApproachPointToFootprintForRadius(
            hall, &worker->s.origin2, range, worker->collision, &approach)) {
        worker->goalentity = Waypoint_add(&approach);
        move_reset_progress(worker);
        return worker->goalentity != NULL;
    }
    if (!hall->pathtex) {
        worker->goalentity = hall;
        move_reset_progress(worker);
        return true;
    }
    return false;
}

static returnResource_t militia_previous_resource(LPEDICT worker) {
    if (!worker) return 0;
    if (worker->harvested_gold || (worker->currentmove && worker->currentmove->ability == &a_goldmine))
        return RETURN_RESOURCE_GOLD;
    if (worker->harvested_lumber || (worker->currentmove && worker->currentmove->ability == &a_harvest))
        return RETURN_RESOURCE_LUMBER;
    return 0;
}

static void militia_return_carried_resources(LPEDICT worker) {
    LPPLAYER player;

    if (!worker || !(player = G_GetPlayerByNumber(worker->s.player))) return;
    if (worker->harvested_gold) {
        G_CreditResourceIncome(player, worker, PLAYERSTATE_RESOURCE_GOLD,
                               (LONG)worker->harvested_gold);
        S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 0);
    }
    if (worker->harvested_lumber) {
        G_CreditResourceIncome(player, worker, PLAYERSTATE_RESOURCE_LUMBER,
                               (LONG)worker->harvested_lumber);
        S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 0);
    }
}

static void militia_remove_buff(LPEDICT unit) {
    DWORD const code = MAKEFOURCC('B','m','i','l');
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == code)
            memset(unit->abilstatus + i, 0, sizeof(unit->abilstatus[i]));
    }
}

static BOOL militia_transform_type(LPEDICT unit, DWORD type) {
    LPGAMECLIENT client;
    FLOAT health_ratio, mana_ratio, temporary_armor;
    FLOAT temporary_attack1, temporary_attack2;
    DWORD old_flags;

    if (!unit || !type || !G_UnitUI(type)->modelFile || G_UnitIsBuilding(type)) return false;
    health_ratio = unit->health.max_value > 0.0f ? unit->health.value / unit->health.max_value : 1.0f;
    mana_ratio = unit->mana.max_value > 0.0f ? unit->mana.value / unit->mana.max_value : 0.0f;
    temporary_armor = unit->temporary_armor_bonus;
    temporary_attack1 = unit->attack1.temporaryDamageBonus;
    temporary_attack2 = unit->attack2.temporaryDamageBonus;
    old_flags = unit->s.flags;

    G_ClearUnitFood(unit);
    if (old_flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    unit->class_id = unit->s.class_id = type;
    G_BindEntityData(unit);
    unit->s.flags &= ~(EF_BUILDING | EF_FOW_BLOCKER | EF_FOW_REVEALER);
    unit->aiflags &= ~(AI_FLYING | AI_IMMOBILE);
    unit->s.shadow = 0;
    memset(&unit->attack1, 0, sizeof(unit->attack1));
    memset(&unit->attack2, 0, sizeof(unit->attack2));
    unit->permanent_armor_bonus = 0.0f;
    unit->temporary_armor_bonus = 0.0f;
    SP_SpawnUnit(unit);
    unit->health.value = MIN(unit->health.max_value, MAX(0.0f, unit->health.max_value * health_ratio));
    unit->mana.value = MIN(unit->mana.max_value, MAX(0.0f, unit->mana.max_value * mana_ratio));
    unit->temporary_armor_bonus = temporary_armor;
    unit->armor_value += temporary_armor;
    unit->attack1.temporaryDamageBonus = temporary_attack1;
    unit->attack2.temporaryDamageBonus = temporary_attack2;
    G_ActivateUnitFood(unit);
    unit->animation = NULL;
    gi.LinkEntity(unit);
    client = G_GetPlayerClientByNumber(unit->s.player);
    if (client && client->ps.number == unit->s.player) G_InvalidateCommands(client);
    G_InvalidateUnitInfoPanel(unit);
    G_InvalidateUnitPortrait(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
    return true;
}

static void militia_clear_pairing(LPEDICT unit) {
    unit->militia.partner = NULL;
    unit->militia.partner_spawn_time = 0;
    unit->militia.returning = false;
}

static void militia_back_to_work(LPEDICT unit, returnResource_t resource) {
    if (resource == RETURN_RESOURCE_GOLD && unit_issueimmediateorder(unit, "autoharvestgold")) return;
    if (resource == RETURN_RESOURCE_LUMBER && unit_issueimmediateorder(unit, "autoharvestlumber")) return;
    unit_stand(unit);
}

static BOOL militia_transform_forward(LPEDICT worker) {
    DWORD const ability = worker->militia.ability;
    DWORD const normal_type = S_SpellDataId(ability, 1, 1);
    DWORD const militia_type = S_SpellDataId(ability, 1, 2);
    FLOAT const duration = S_SpellDuration(ability, 1, false);

    if (!normal_type || !militia_type || worker->class_id != normal_type ||
        !G_UnitUI(militia_type)->modelFile) return false;
    worker->militia.normal_type = normal_type;
    worker->militia.militia_type = militia_type;
    militia_return_carried_resources(worker);
    if (!militia_transform_type(worker, militia_type)) return false;
    worker->militia.active = true;
    militia_clear_pairing(worker);
    worker->goalentity = NULL;
    move_reset_progress(worker);
    if (duration > 0.0f) unit_addtimedstatus(worker, MILITIA_BUFF, 1, duration);
    unit_stand(worker);
    return true;
}

static BOOL militia_transform_back(LPEDICT militia, BOOL resume_work) {
    returnResource_t const resource = (returnResource_t)militia->militia.previous_resource;
    DWORD const normal_type = militia->militia.normal_type;

    if (!normal_type || militia->class_id != militia->militia.militia_type) return false;
    militia_remove_buff(militia);
    if (!militia_transform_type(militia, normal_type)) return false;
    militia->militia.ability = 0;
    militia->militia.active = false;
    militia->militia.normal_type = 0;
    militia->militia.militia_type = 0;
    militia->militia.previous_resource = 0;
    militia_clear_pairing(militia);
    if (resume_work) {
        militia->goalentity = NULL;
        move_reset_progress(militia);
        militia_back_to_work(militia, resource);
    }
    return true;
}

static void ai_militia_pair_walk(LPEDICT worker) {
    LPEDICT hall = worker ? worker->militia.partner : NULL;
    FLOAT distance, step;

    if (!militia_partner_valid(worker, hall)) {
        militia_clear_pairing(worker);
        worker->goalentity = NULL;
        move_reset_progress(worker);
        unit_stand(worker);
        return;
    }
    if (militia_in_range(worker, hall)) {
        BOOL const transformed = worker->militia.returning
            ? militia_transform_back(worker, true)
            : militia_transform_forward(worker);
        if (!transformed) {
            S_CancelMilitiaPairing(worker);
            worker->goalentity = NULL;
            move_reset_progress(worker);
            unit_stand(worker);
        }
        return;
    }
    distance = M_DistanceToGoal(worker);
    step = unit_movedistance(worker);
    if (move_is_blocked(worker, distance, step) || worker->movement.flow_unreachable) {
        militia_clear_pairing(worker);
        worker->goalentity = NULL;
        move_reset_progress(worker);
        unit_stand(worker);
        return;
    }
    unit_changeangle_for_radius(worker, worker->collision);
    if (worker->movement.flow_goal_reached && !militia_in_range(worker, hall)) {
        militia_clear_pairing(worker);
        worker->goalentity = NULL;
        move_reset_progress(worker);
        unit_stand(worker);
        return;
    }
    unit_moveindirection(worker);
}

static umove_t militia_move_walk = { "walk", ai_militia_pair_walk, NULL, &a_militia };

BOOL S_MilitiaTargetOrder(LPEDICT worker, LPCSTR order, LPEDICT hall) {
    DWORD worker_ability;
    BOOL returning;

    if (!worker || !order || !hall) {
        return false;
    }
    if (M_IsDead(worker) || M_IsDead(hall)) {
        return false;
    }
    if (worker->s.player != hall->s.player) {
        return false;
    }
    if (worker->paused || hall->paused) {
        return false;
    }
    if (!militia_hall_ability_alias(hall, true)) {
        return false;
    }
    returning = !strcmp(order, "militiaoff");
    if (!returning && strcmp(order, "militia")) {
        return false;
    }
    worker_ability = militia_actor_ability_alias(worker, MAKEFOURCC('A','m','i','l'));
    if (!worker_ability) {
        return false;
    }
    militia_sync_form(worker, worker_ability);
    if (returning != worker->militia.active) {
        return false;
    }

    if (!returning && !worker->militia.ability)
        worker->militia.previous_resource = militia_previous_resource(worker);
    if (!worker->militia.ability) worker->militia.ability = worker_ability;
    worker->militia.partner = hall;
    worker->militia.partner_spawn_time = hall->spawn_time;
    worker->militia.returning = returning;
    worker->movement.holding_position = false;
    if (militia_in_range(worker, hall)) {
        BOOL const transformed = returning ? militia_transform_back(worker, true) : militia_transform_forward(worker);
        if (!transformed) S_CancelMilitiaPairing(worker);
        return transformed;
    }
    if (!militia_prepare_approach(worker, hall)) {
        militia_clear_pairing(worker);
        worker->goalentity = NULL;
        move_reset_progress(worker);
        if (!returning) worker->militia.ability = 0;
        return false;
    }
    unit_setmove(worker, &militia_move_walk);
    return true;
}

FLOAT S_MilitiaPairSearchRadius(DWORD ability) {
    FLOAT const radius = MAX(0.0f, S_SpellNumber(ability, ABILITY_NUMBER_AREA, 1));

    /* Warsmash's generic pairing ability treats an authored zero search
     * radius as unbounded. Amil uses that sentinel, so interpreting zero as
     * a literal radius prevents a Peasant from finding any ordinary Hall. */
    return radius == 0.0f ? FLT_MAX : radius;
}

static LPEDICT militia_find_partner(LPEDICT worker, DWORD worker_ability) {
    LPEDICT best = NULL;
    FLOAT best_distance = FLT_MAX;
    FLOAT const radius = S_MilitiaPairSearchRadius(worker_ability);

    FILTER_EDICTS(hall, hall->inuse && (hall->svflags & SVF_MONSTER)) {
        DWORD hall_ability;
        FLOAT distance;

        if (hall->s.player != worker->s.player) continue;
        if (!G_UnitIsBuilding(hall->class_id)) continue;
        if (M_IsDead(hall) || hall->paused) continue;
        hall_ability = militia_hall_ability_alias(hall, true);
        if (!hall_ability) continue;
        distance = Vector2_distance(&worker->s.origin2, &hall->s.origin2);
        if (distance > radius) continue;
        if (distance < best_distance) {
            best = hall;
            best_distance = distance;
        }
    }
    return best;
}

static BOOL militia_toggle_on(LPEDICT unit) {
    DWORD ability, militia_type;

    if (!unit) return false;
    if (unit->militia.active) return true;
    ability = militia_actor_ability_alias(unit, MAKEFOURCC('A','m','i','l'));
    militia_type = ability ? S_SpellDataId(ability, 1, 2) : 0;
    return militia_type && unit->class_id == militia_type;
}

static void militia_cmd(LPEDICT clent) {
    BOOL issued = false;

    if (!clent || !clent->client) return;
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, worker) {
        DWORD const ability = militia_actor_ability_alias(worker, MAKEFOURCC('A','m','i','l'));
        LPEDICT hall;
        LPCSTR order;
        if (!ability || worker->paused || worker->militia.partner || S_GoldMineWorkerIsInside(worker)) {
            continue;
        }
        militia_sync_form(worker, ability);
        hall = militia_find_partner(worker, ability);
        if (!hall) {
            continue;
        }
        order = worker->militia.active ? "militiaoff" : "militia";
        if (G_IssueUnitTargetOrder(worker, order, hall, false, clent->client->ps.number))
            issued = true;
    }
    if (!issued) G_ShowCommandErrorText(clent, "No suitable Town Hall could be found.");
}

static void call_to_arms_cmd(LPEDICT clent) {
    LPGAMECLIENT client;
    BOOL off;
    BOOL issued = false;

    if (!clent || !(client = clent->client)) return;
    off = client->menu.ability_off;
    FOR_CONTROLLABLE_SELECTED_UNITS(client, hall) {
        DWORD const hall_ability = militia_hall_ability_alias(hall, true);
        FLOAT radius;
        if (!hall_ability || hall->paused) continue;
        radius = S_MilitiaPairSearchRadius(hall_ability);
        FILTER_EDICTS(worker, worker->inuse && (worker->svflags & SVF_MONSTER) && !M_IsDead(worker) &&
                      !worker->paused && worker->s.player == hall->s.player) {
            DWORD const worker_ability = militia_actor_ability_alias(worker, MAKEFOURCC('A','m','i','l'));
            FLOAT const distance = Vector2_distance(&worker->s.origin2, &hall->s.origin2);
            if (!worker_ability || worker->militia.partner || S_GoldMineWorkerIsInside(worker)) continue;
            militia_sync_form(worker, worker_ability);
            if (off != worker->militia.active) continue;
            if (distance > radius) continue;
            if (G_IssueUnitTargetOrder(worker, off ? "militiaoff" : "militia", hall, false, client->ps.number))
                issued = true;
        }
    }
    if (!issued) G_ShowCommandErrorText(clent,
        off ? "No Militia could be found." : "No Peasants could be found.");
}

void S_CancelMilitiaPairing(LPEDICT unit) {
    if (!unit || !unit->militia.ability) return;
    militia_clear_pairing(unit);
    if (!unit->militia.active) {
        unit->militia.ability = 0;
        unit->militia.normal_type = 0;
        unit->militia.militia_type = 0;
        unit->militia.previous_resource = 0;
    }
}

void S_MilitiaExpire(LPEDICT unit) {
    if (!unit || !unit->militia.active || M_IsDead(unit)) return;
    if (!militia_transform_back(unit, false)) return;
    /* Natural expiration is not Back to Work. Preserve a combat/move behavior
     * when one exists; only retire an in-flight militia pairing walk. */
    if (unit->currentmove == &militia_move_walk) unit_stand(unit);
    else if (unit->currentmove) unit_setanimation(unit, unit->currentmove->animation);
}

ability_t a_call_to_arms = {
    .cmd = call_to_arms_cmd,
    .flags = ABILITY_SEPARATE_OFF,
};

ability_t a_militia = {
    .cmd = militia_cmd,
    .is_toggle_on = militia_toggle_on,
};
