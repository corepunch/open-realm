#include "s_skills.h"

#define LOCUST_SEEKING 1u // state ID; member is flying toward an eligible target
#define LOCUST_ATTACKING 2u // state ID; member is applying swarm attacks to its target
#define LOCUST_RETURNING 3u // state ID; member has left combat and is returning to its host
#define LOCUST_REACQUIRE_MS 250u // milliseconds; bounds idle target scans while allowing prompt retargeting
#define LOCUST_MAX_ATTACKERS_PER_TARGET 256u // members; avoids assigning the full swarm to one target
#define LOCUST_RECALL_GRACE_MS 8000u // milliseconds; lets idle swarm members disperse before retirement
/* Idle flight is a loose, continually changing cloud, not a fixed ring.
 * Fractions are presentation approximations; Area remains the authored limit. */
#define LOCUST_IDLE_RADIUS_FRACTION 0.78f
/* The locust's real model has Walk and Death but no Attack sequence. */
static umove_t locust_walk_move = { .animation = "walk" };

static void locust_begin_return(edict_t *member, locustMember_t *state, edict_t *caster);

static bool locust_reference_valid(edict_t *ent, uint32_t spawn_time) {
    return ent && ent->inuse && ent->spawn_time == spawn_time;
}

static bool locust_member_in_swarm(edict_t *member, edict_t *controller) {
    locustMember_t *state = G_GetLocustMember(member);
    return state && state->controller == controller &&
           state->controller_spawn_time == controller->spawn_time;
}

/* Swarm expiry and recall are RemoveUnit-like, not KillUnit: ordinary unit
 * Death/corpse handling needs monster_think, which members replace. Queue
 * removal after entity iteration to avoid stale reads in the current tick. */
static void locust_member_retire(edict_t *member) {
    if (member && member->inuse) G_DeferFreeEdict(member);
}

static void locust_swarm_end(edict_t *controller) {
    locustSwarm_t *swarm = G_GetLocustSwarm(controller);
    if (swarm) swarm->active = false;
    FILTER_EDICTS(member, locust_member_in_swarm(member, controller))
        locust_member_retire(member);
    G_FreeEdict(controller);
}

static void locust_swarm_begin_recall(edict_t *controller, locustSwarm_t *swarm) {
    swarm->active = false;
    swarm->recalling = true;
    swarm->recall_deadline_ms = G_Time() + LOCUST_RECALL_GRACE_MS;
    FILTER_EDICTS(member, locust_member_in_swarm(member, controller)) {
        locustMember_t *state = G_GetLocustMember(member);
        if (state->damage_credit > 0.0f && !M_IsDead(member) && !G_IsDeferredFree(member))
            locust_begin_return(member, state, controller->owner);
        else locust_member_retire(member);
    }
}

void S_LocustSwarmCancelForCaster(edict_t *caster) {
    uint32_t const count = globals.num_edicts;
    FOR_LOOP(i, count) {
        edict_t *controller = &globals.edicts[i];
        if (controller->inuse && controller->think == locust_swarm_think && controller->owner == caster)
            locust_swarm_end(controller);
    }
}

static void locust_spawn_one(edict_t *controller, locustSwarm_t *swarm) {
    edict_t *caster = controller->owner;
    float const angle = (float)swarm->released * 2.39996323f;
    vec2_t point = caster->s.origin2;
    edict_t *locust;
    locustMember_t *member;
    point.x += cosf(angle) * 48.0f;
    point.y += sinf(angle) * 48.0f;
    /* Do not install BTLF: its status expiry would kill a locust separately
     * from its controller, leaving a frozen corpse and losing return credit.
     * The shared swarm lifecycle owns the entire member lifetime. */
    locust = S_SummonAbilityAt(caster, swarm->ability, swarm->unit_id, &point, 0.0f);
    if (!locust) return;
    member = G_AddLocustMember(locust);
    if (!member) { locust_member_retire(locust); return; }
    member->controller = controller;
    member->controller_spawn_time = controller->spawn_time;
    member->release_index = swarm->released;
    member->state = LOCUST_SEEKING;
    member->next_acquire_ms = G_Time();
    locust->owner = caster;
    locust->summon_ability = swarm->ability;
    locust->think = locust_member_think;
    locust->s.player = caster->s.player;
    /* Replacing monster_think also replaces its animation advancement.
     * Play the authored Walk loop explicitly; custom summons with no Walk
     * sequence still move, but do not acquire an invented animation. */
    if (G_GetUnitAnimation(locust, "Walk")) {
        locust->currentmove = &locust_walk_move;
        G_SetUnitAnimation(locust, "walk");
        if (locust->animation) locust->s.frame = locust->animation->interval[0];
    }
    swarm->released++;
}

void locust_swarm_think(edict_t *controller) {
    locustSwarm_t *swarm = G_GetLocustSwarm(controller);
    edict_t *caster = controller->owner;
    uint32_t now = G_Time();
    if (!swarm || !locust_reference_valid(caster, swarm->caster_spawn_time) || M_IsDead(caster)) {
        locust_swarm_end(controller);
        return;
    }
    if (swarm->active && now >= swarm->end_ms) locust_swarm_begin_recall(controller, swarm);
    if (swarm->recalling) {
        bool any_alive = false;
        FILTER_EDICTS(member, locust_member_in_swarm(member, controller) &&
                      !M_IsDead(member) && !G_IsDeferredFree(member)) any_alive = true;
        if (!any_alive || now >= swarm->recall_deadline_ms) {
            locust_swarm_end(controller);
            return;
        }
    }
    if (!swarm->active) return;
    controller->s.origin = caster->s.origin;
    controller->s.origin2 = caster->s.origin2;
    gi.LinkEntity(controller);
    while (swarm->released < swarm->total && now >= swarm->next_release_ms) {
        locust_spawn_one(controller, swarm);
        if (swarm->release_interval_ms == 0) break;
        swarm->next_release_ms += swarm->release_interval_ms;
    }
}

/* A snapshot of current reservations costs one pass per acquisition, instead
 * of rescanning every locust for every potential target in the world. No
 * persistent cache: recalls, death and entity-slot reuse are always current. */
static uint16_t *locust_reservations(edict_t *controller, uint32_t count) {
    uint16_t *occupied = calloc(count, sizeof(*occupied));
    if (!occupied) return NULL;
    FILTER_EDICTS(member, locust_member_in_swarm(member, controller) &&
                  !G_IsDeferredFree(member) && S_SpellIsAliveTarget(member)) {
        locustMember_t const *state = G_GetLocustMember(member);
        if (state->state == LOCUST_ATTACKING && state->target &&
            state->target >= globals.edicts &&
            state->target < globals.edicts + count &&
            state->target->inuse &&
            state->target_spawn_time == state->target->spawn_time) {
            uint32_t const index = (uint32_t)(state->target - globals.edicts);
            if (occupied[index] != UINT16_MAX) occupied[index]++;
        }
    }
    return occupied;
}

static edict_t *locust_find_target(edict_t *member, locustSwarm_t *swarm) {
    edict_t *caster = member->owner, *best = NULL;
    edict_t *controller = G_GetLocustMember(member)->controller;
    uint32_t const count = globals.num_edicts;
    uint16_t *occupied = locust_reservations(controller, count);
    uint32_t const cap = (uint32_t)MAX(1.0f, MIN((float)LOCUST_MAX_ATTACKERS_PER_TARGET,
                                                swarm->target_cap));
    float best_distance = FLT_MAX;
    /* On allocation failure, count reservations on demand rather than
     * silently allowing more attackers than the authored target cap. */
    for (uint32_t i = 0; i < count; i++) {
        edict_t *target = &globals.edicts[i];
        uint32_t reservations = 0;
        float distance;
        if (target == caster || !S_SpellIsAliveTarget(target) ||
            !S_SpellIsEnemy(caster, target) ||
            G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE) ||
            unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE) ||
            !S_SpellAllowsAreaTarget(swarm->ability, caster, target) ||
            !S_AttackCanTarget(member, target) || !S_LocustAttackWeapon(member, target)) continue;
        distance = Vector2_distance(&target->s.origin2, &caster->s.origin2);
        if (distance > swarm->area) continue;
        if (occupied) reservations = occupied[i];
        else FILTER_EDICTS(other, locust_member_in_swarm(other, controller) &&
                           !G_IsDeferredFree(other) && S_SpellIsAliveTarget(other)) {
            locustMember_t const *state = G_GetLocustMember(other);
            if (state->state == LOCUST_ATTACKING && state->target == target &&
                state->target_spawn_time == target->spawn_time) reservations++;
        }
        if (reservations >= cap) continue;
        if (!best || distance < best_distance ||
            (distance == best_distance && target->s.number < best->s.number)) {
            best = target;
            best_distance = distance;
        }
    }
    free(occupied);
    return best;
}

/* Swarm members own their think callback, so conventional Move/Attack orders
 * cannot translate them: those orders are stepped by monster_think, which a
 * locust no longer runs. Steer each flying member in simulation coordinates,
 * and publish both network position fields to the universal client. */
static float locust_steer(edict_t *locust, vec2_t const *destination) {
    vec2_t delta = Vector2_sub(destination, &locust->s.origin2);
    float const distance = Vector2_len(&delta);
    float speed = locust->unitinfo.MoveSpeed;
    float step;
    if (speed <= 0.0f && locust->data.UnitBalance)
        speed = locust->data.UnitBalance->speed;
    if (!isfinite(speed) || speed <= 0.0f || distance <= 0.001f) return distance;
    step = MIN(distance, speed * (float)FRAMETIME / 1000.0f);
    locust->s.origin2.x += delta.x * (step / distance);
    locust->s.origin2.y += delta.y * (step / distance);
    locust->s.origin.x = locust->s.origin2.x;
    locust->s.origin.y = locust->s.origin2.y;
    locust->s.angle = atan2f(delta.y, delta.x);
    M_CheckGround(locust);
    gi.LinkEntity(locust);
    return distance - step;
}

static void locust_begin_return(edict_t *member, locustMember_t *state, edict_t *caster) {
    (void)member;
    (void)caster;
    state->state = LOCUST_RETURNING;
    state->target = NULL;
    state->target_spawn_time = 0;
}

void locust_member_think(edict_t *member) {
    locustMember_t *state = G_GetLocustMember(member);
    edict_t *controller, *caster, *target;
    locustSwarm_t *swarm;
    uint32_t now = G_Time();
    float distance, reach;
    if (!member || !member->inuse || G_IsDeferredFree(member)) return;
    /* A locust killed in combat must not retain a frozen Death pose: its
     * dedicated thinker has replaced the normal corpse lifecycle. The death
     * event has already been dispatched by unit_die. */
    if (M_IsDead(member)) { locust_member_retire(member); return; }
    if (!state || !(controller = state->controller) ||
        !locust_reference_valid(controller, state->controller_spawn_time) ||
        !(swarm = G_GetLocustSwarm(controller)) || (!swarm->active && !swarm->recalling)) {
        locust_member_retire(member);
        return;
    }
    caster = controller->owner;
    if (!locust_reference_valid(caster, swarm->caster_spawn_time) || M_IsDead(caster)) {
        locust_member_retire(member);
        return;
    }
    /* This member's think replaces monster_think, including its animation
     * clock. Do not depend on normal unit order/move callbacks. */
    if (member->currentmove == &locust_walk_move && member->animation)
        M_MoveFrame(member);

    if (swarm->recalling && state->state != LOCUST_RETURNING) {
        if (state->damage_credit <= 0.0f) { locust_member_retire(member); return; }
        locust_begin_return(member, state, caster);
    }
    if (state->state == LOCUST_RETURNING) {
        float const arrival = MAX(48.0f, member->collision + caster->collision);
        distance = locust_steer(member, &caster->s.origin2);
        if (distance <= arrival) {
            if (state->damage_credit > 0.0f)
                S_SpellHeal(caster, state->damage_credit * swarm->heal_factor);
            state->damage_credit = 0.0f;
            if (swarm->recalling) { locust_member_retire(member); return; }
            state->state = LOCUST_SEEKING;
            state->next_acquire_ms = now;
        }
        return;
    }

    target = state->target;
    /* Only check the attacker cap when ACQUIRING a new target. A member
     * already holding the final allowed reservation is not over capacity. */
    if (state->state == LOCUST_ATTACKING &&
        (!locust_reference_valid(target, state->target_spawn_time) ||
         !S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(caster, target) ||
         !S_AttackCanTarget(member, target) ||
         Vector2_distance(&target->s.origin2, &caster->s.origin2) > swarm->area)) {
        state->target = NULL;
        state->target_spawn_time = 0;
        state->state = LOCUST_SEEKING;
        if (state->damage_credit >= swarm->return_threshold && state->damage_credit > 0.0f) {
            locust_begin_return(member, state, caster);
            return;
        }
    }
    if (state->state == LOCUST_SEEKING && now >= state->next_acquire_ms) {
        target = locust_find_target(member, swarm);
        state->next_acquire_ms = now + LOCUST_REACQUIRE_MS;
        if (target && S_AttackCanTarget(member, target) && S_LocustAttackWeapon(member, target)) {
            state->state = LOCUST_ATTACKING;
            state->target = target;
            state->target_spawn_time = target->spawn_time;
            state->next_attack_ms = now;
        }
    }
    if (state->state != LOCUST_ATTACKING || !state->target) {
        /* Retail locusts continuously range around the caster rather than
         * occupying equally spaced points on a small, fixed-radius ring.
         * Two incommensurate oscillations provide independent, deterministic
         * heading/range changes per release index, with no new save state. */
        float const phase = (float)state->release_index * 2.39996323f;
        float const seconds = (float)(now - swarm->start_ms) * 0.001f;
        float const max_radius = MAX(0.0f, swarm->area * LOCUST_IDLE_RADIUS_FRACTION);
        float const min_radius = MIN(max_radius, MAX(48.0f, caster->collision + 40.0f));
        float const range_fraction = 0.1f + 0.75f *
            (0.5f + 0.5f * sinf(seconds * 0.43f + phase * 0.73f));
        float const radius = min_radius + (max_radius - min_radius) * range_fraction;
        float const angle = phase + seconds * (0.50f + 0.12f * sinf(phase * 1.31f)) +
            0.5f * sinf(seconds * 0.27f + phase * 1.7f);
        vec2_t const roaming = { caster->s.origin2.x + cosf(angle) * radius,
                                 caster->s.origin2.y + sinf(angle) * radius };
        locust_steer(member, &roaming);
        return;
    }

    target = state->target;
    if (!locust_reference_valid(target, state->target_spawn_time)) return;
    unitAttack_t const *weapon = S_LocustAttackWeapon(member, target);
    if (!weapon || !S_AttackCanTarget(member, target)) {
        state->target = NULL;
        state->target_spawn_time = 0;
        state->state = LOCUST_SEEKING;
        return;
    }
    reach = MAX(32.0f, weapon->range + target->collision);
    distance = Vector2_distance(&member->s.origin2, &target->s.origin2);
    if (distance > reach) {
        locust_steer(member, &target->s.origin2);
        return;
    }
    if (now >= state->next_attack_ms) {
        uint32_t const member_generation = member->spawn_time;
        uint32_t const target_generation = target->spawn_time;
        float const cooldown = MAX(0.1f, weapon->cooldown);
        float actual = S_LocustAttackHit(member, target);
        /* Combat callbacks may synchronously retire an attacker or victim. */
        if (!member->inuse || member->spawn_time != member_generation || M_IsDead(member)) return;
        state = G_GetLocustMember(member);
        if (!state) return;
        state->next_attack_ms = now + (uint32_t)MAX(100.0f, cooldown * 1000.0f);
        state->damage_credit += actual;
        if (state->damage_credit >= swarm->return_threshold && state->damage_credit > 0.0f)
            locust_begin_return(member, state, caster);
        else if (!target->inuse || target->spawn_time != target_generation || M_IsDead(target)) {
            state->target = NULL;
            state->target_spawn_time = 0;
            state->state = LOCUST_SEEKING;
        }
    }
}

void S_LocustSwarmAttackLanded(edict_t *attacker, edict_t *target, float actual_damage) {
    locustMember_t *member = G_GetLocustMember(attacker);
    locustSwarm_t *swarm;
    if (!member || member->state != LOCUST_ATTACKING || actual_damage <= 0.0f ||
        !locust_reference_valid(member->target, member->target_spawn_time) || member->target != target ||
        !locust_reference_valid(member->controller, member->controller_spawn_time) ||
        !(swarm = G_GetLocustSwarm(member->controller)) || !swarm->active) return;
    member->damage_credit += actual_damage;
    if (member->damage_credit >= swarm->return_threshold)
        locust_begin_return(attacker, member, attacker->owner);
}

static bool locust_swarm_validate(edict_t *caster, abilityitem_t const *spell) {
    uint32_t level;
    return caster && spell && S_SpellIsAliveTarget(caster) &&
           (level = S_SpellLevel(caster, spell->code)) > 0 &&
           S_SpellUnitId(spell->code, level) && S_SpellDuration(spell->code, level, false) > 0.0f;
}

static void locust_swarm_cast(edict_t *caster, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float duration = S_SpellDuration(spell->code, level, false);
    edict_t *controller = G_Spawn();
    locustSwarm_t *swarm;
    if (!controller) return;
    controller->owner = caster;
    controller->class_id = spell->code;
    controller->s.player = caster->s.player;
    controller->s.origin = caster->s.origin;
    controller->s.origin2 = caster->s.origin2;
    swarm = G_AddLocustSwarm(controller);
    if (!swarm) { G_FreeEdict(controller); return; }
    swarm->caster_spawn_time = caster->spawn_time;
    swarm->ability = spell->code;
    swarm->level = level;
    swarm->start_ms = G_Time();
    swarm->end_ms = G_Time() + (uint32_t)(duration * 1000.0f);
    swarm->release_interval_ms = (uint32_t)MAX(FRAMETIME, S_SpellData(spell->code, level, 2) * 1000.0f);
    swarm->next_release_ms = G_Time();
    swarm->total = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    swarm->unit_id = S_SpellUnitId(spell->code, level);
    swarm->area = MAX(0.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level));
    swarm->target_cap = MAX(1.0f, S_SpellData(spell->code, level, 3));
    swarm->heal_factor = MAX(0.0f, S_SpellData(spell->code, level, 4));
    swarm->return_threshold = MAX(0.0f, S_SpellData(spell->code, level, 5));
    swarm->active = true;
    controller->s.sound = (uint16_t)G_AbilityEffectSoundIndex(spell->code, true);
    controller->think = locust_swarm_think;
}

BZ_ABILITY_PROC(CAbilityLocustSwarm) {
    abilityitem_t const *spell = call ? call->item : NULL;
    if (msg == A_VALIDATE) return locust_swarm_validate(ent, spell);
    if (msg == A_EXECUTE && locust_swarm_validate(ent, spell)) {
        S_LocustSwarmCancelForCaster(ent);
        locust_swarm_cast(ent, spell);
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}
