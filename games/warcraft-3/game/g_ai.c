#include <float.h>
#include "g_local.h"
#include "skills/s_skills.h"

void unit_setanimation(edict_t *self, cstring_t anim) {
    /* Walk is requested every movement tick. Keep the selected numbered walk
     * sequence until a move transition selects a fresh animation. */
    if (self && anim && !strcmp(anim, "walk") && G_AnimationHasPrimary(self->animation, "walk")) return;
    G_SetUnitAnimation(self, anim);
}

static bool unit_is_active_repair_move(edict_t *self) {
    char rawcode[5];
    ability_t const *handler;

    if (!self || !self->currentmove || !self->buildwork || !self->buildwork->ability) return false;
    memcpy(rawcode, &self->buildwork->ability, 4);
    rawcode[4] = '\0';
    handler = FindAbilityForCommand(rawcode);
    return handler && self->currentmove->proc == handler->proc;
}

void unit_setmove(edict_t *self, umove_t *move) {
    bool was_idle = G_UnitIsIdleWorker(self);
    bool const was_standing = self->currentmove && self->currentmove->think == ai_stand;

    if (self->currentmove != move) move_cancel_displacement(self);
    self->animation_override = false;

    /* buildwork.ability is staged before Repair switches from the worker's
     * existing stand/move behavior. Only an OLD Repair move means this
     * transition is actually leaving Repair; otherwise cancelling here erases
     * the new target before the Repair walk can begin. */
    if (self->currentmove && self->currentmove->proc != move->proc &&
        unit_is_active_repair_move(self)) {
        S_CancelRepair(self);
    }
    if (self->currentmove && self->currentmove->proc == CAbilityMilitia &&
        move->proc != CAbilityMilitia) {
        S_CancelMilitiaPairing(self);
    }
    /* Acolyte mine slots belong to the harvesting order and must be released
     * when any other movement ability replaces it. */
    if (self->currentmove && self->currentmove->proc == CAbilityAcolyteHarvest &&
        move->proc != CAbilityAcolyteHarvest) {
        S_AcolyteHarvestRelease(self);
    }
    /* A point-drop keeps the exact carried item separately from its waypoint.
     * Replacing that behavior must abandon the pending drop just like replacing
     * any other unit order; otherwise a stale item pointer would survive while
     * an unrelated move/attack is active. */
    if (self->item_drop && self->currentmove && self->currentmove != move) {
        self->item_drop = NULL;
    }
    /* A replaced pre-spawn Build order used to leave build_project set after
     * Stop/Move, so later code could mistake an idle worker for an active build. */
    if (self->currentmove && self->currentmove->proc == CAbilityBuild &&
        move->proc != CAbilityBuild) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD order-replaced worker=%ld old=%s new=%s project=%.4s goal=%ld preview=%ld origin=(%.1f,%.1f)\n",
                (long)(self - g_edicts),
                self->currentmove->animation ? self->currentmove->animation : "<none>",
                move->animation ? move->animation : "<none>",
                self->build_project ? (cstring_t)&self->build_project : "----",
                self->goalentity ? (long)(self->goalentity - g_edicts) : -1L,
                self->build_preview ? (long)(self->build_preview - g_edicts) : -1L,
                self->s.origin2.x, self->s.origin2.y);
#endif
        G_ClearBuildPreview(self);
        self->build_project = 0;
    }
    if (self->currentmove != move)
        S_UnitAbilityMoveLeave(self, move->proc);
    self->currentmove = move;
    G_SetUnitAnimation(self, move->animation);
    if (self->animation) {
        // skip
    } else if (strstr(move->animation, "run")) {
        G_SetUnitAnimation(self, "walk");
    } else if (strstr(move->animation, "stand ")) {
        G_SetUnitAnimation(self, "stand");
    } else if (strstr(move->animation, "attack ")) {
        G_SetUnitAnimation(self, "attack");
    }
    if (was_idle != G_UnitIsIdleWorker(self)) {
        G_InvalidateUnitShortcutsForUnit(self);
    }
    /* Stop's engaged glow is derived from the idle stand move; moves and
     * auto-acquired combat change it without any command-card click. */
    if (was_standing != (move->think == ai_stand)) G_InvalidateUnitCommands(self);
}

void unit_runwait(edict_t *self, void (*callback)(edict_t * )) {
    if (self->wait <= 0)
        return;
    if (self->wait > FRAMETIME / 1000.f) {
        self->wait -= FRAMETIME / 1000.f;
    } else {
        self->wait = 0;
        callback(self);
    }
}

void ai_idle(edict_t *self) {
}

void order_attack(edict_t *self, edict_t *target);

#define MAX_SIGHT_ENTITIES 256

static edict_t *ai_current_entity = NULL;
static edict_t *sight_entities[MAX_SIGHT_ENTITIES];

static bool unit_has_attack(edict_t const *self);

static bool filter_sight(edict_t const *ent) {
    if (!(ent->svflags & SVF_MONSTER) || !ai_current_entity ||
        ai_current_entity->s.player >= MAX_PLAYERS || ent->s.player >= MAX_PLAYERS ||
        ent->s.player == ai_current_entity->s.player)
        return false;
    /* Friend/enemy is the acquiring player's directional PASSIVE alliance.
     * Shared vision/control/XP alone must never suppress hostile acquisition. */
    if (G_PlayerTreatsPlayerAsAlly(ai_current_entity->s.player, ent->s.player))
        return false;
    if (ent->svflags & SVF_DEADMONSTER)
        return false;
    if (S_UnitIsHiddenFromPlayer(ent, ai_current_entity->s.player))
        return false;
    /* Warsmash excludes invulnerable units from automatic attack acquisition;
     * explicit orders still perform their own target validation. */
    if (ent->invulnerable || unit_hasstatusstate(ent, WC3_STATUS_STATE_INVULNERABLE))
        return false;
    if (S_UnitAbilityEvent((edict_t *)ent, A_NO_ACQUIRE))
        return false;
    /* Retail creep idle-acquisition exceptions apply only to autonomous
     * Neutral Hostile guarding. Retaliation and explicit scripted orders use
     * the ordinary attack-target checks instead. A grounded (Ensnared) flyer
     * is no longer a flyover. */
    if (ai_current_entity->movement.creep_guard_enabled &&
        ai_current_entity->s.player == PLAYER_NEUTRAL_AGGRESSIVE) {
        if (ent->aiflags & AI_FLYING) return false;
        if (ent->runtime.flags & UNIT_BALANCE_BUILDING) return false;
    }
    /* Attack-capable units filter acquisition through the Attack ability's
     * authored target mask.  Structures are ordinary unit targets here; the
     * attack data decides whether they are legal instead of AI excluding every
     * building globally. */
    if (unit_has_attack(ai_current_entity)) {
        if (!S_AttackCanAutoAcquire(ai_current_entity, ent))
            return false;
    } else if (ent->runtime.flags & UNIT_BALANCE_BUILDING) {
        /* Preserve the old non-combat sight behavior: callers without an
         * ordinary weapon do not gain building candidates merely because
         * armed units may now attack structures. */
        return false;
    }
    return true;
}

/* Does this unit have an attack to acquire targets with? */
static bool unit_has_attack(edict_t const *self) {
    return S_CargoAttacksEnabled(self) &&
           ((S_UnitAttackSlotEnabled(self, 0) && self->attack1.cooldown > 0.0f && (self->attack1.damageBase > 0 || self->attack1.numberOfDice > 0)) ||
            (S_UnitAttackSlotEnabled(self, 1) && self->attack2.cooldown > 0.0f && (self->attack2.damageBase > 0 || self->attack2.numberOfDice > 0)));
}

/* Throttle target re-acquisition: units scan only a few times per second,
 * staggered by entity index, instead of every sim tick. */
#define AI_ACQUIRE_INTERVAL 300 /* ms */

bool G_ShouldAcquireThisFrame(edict_t const *self) {
    uint32_t const stagger = (uint32_t)(self - g_edicts) % AI_ACQUIRE_INTERVAL;
    return ((level.time + stagger) % AI_ACQUIRE_INTERVAL) < (uint32_t)FRAMETIME;
}

/* Return the spawn-cached range; repeated SLK walks dominated large acquisition scans. */
float G_AcquisitionRange(edict_t const *self) {
    return self->runtime.acquisition_range;
}

static bool ai_has_siege_attack(edict_t const *self) {
    return self && ((S_UnitAttackSlotEnabled(self, 0) && self->attack1.type == ATK_SIEGE) ||
                    (S_UnitAttackSlotEnabled(self, 1) && self->attack2.type == ATK_SIEGE));
}

/* Melee AI policy setters affect automatic target acquisition, not explicit player/script
 * attack orders. Target Heroes gives legal Heroes priority over ordinary targets. Smart
 * Artillery gives siege-capable AI units structures priority; distance still chooses within
 * a category. */
/* Retail reports a distinct injured-unit/Hero preference for level 7+
 * creeps. Exact weights are undocumented; rank these two documented traits
 * ahead of distance, without affecting lower-level creeps or bot policy. */
static uint32_t ai_creep_target_priority(edict_t const *self, edict_t const *target) {
    bool injured, hero;
    if (!self || !target || !self->movement.creep_guard_enabled ||
        self->s.player != PLAYER_NEUTRAL_AGGRESSIVE || !self->data.UnitBalance ||
        self->data.UnitBalance->level < 7) return 2;
    injured = target->health.max_value > 0 && target->health.value < target->health.max_value;
    hero = G_UnitIsHero(target);
    return injured && hero ? 0 : (injured || hero ? 1 : 2);
}

static uint32_t ai_bot_target_priority(edict_t const *self, edict_t const *target) {
    bot_t const *bot;
    if (!self || !target || self->s.player >= MAX_PLAYERS) return 0;
    bot = &level.bots[self->s.player];
    if (!bot->vm) return 0;
    if ((bot->flags & BOT_SMART_ARTILLERY) && ai_has_siege_attack(self))
        return G_UnitIsBuilding(target->class_id) ? 0 : 1;
    if (bot->flags & BOT_TARGET_HEROES)
        return G_UnitIsHero(target) ? 0 : 1;
    return 0;
}

edict_t *G_FindNearestEnemy(edict_t *self, float radius) {
    ai_current_entity = self;
    box2_t const sightbox = {
        { self->s.origin2.x - radius, self->s.origin2.y - radius },
        { self->s.origin2.x + radius, self->s.origin2.y + radius },
    };
    uint32_t numents = gi.BoxEdicts(&sightbox, sight_entities, MAX_SIGHT_ENTITIES, filter_sight);
    edict_t *best = NULL;
    float best_dist = radius;
    uint32_t best_priority = 2;
    FOR_LOOP(i, numents) {
        edict_t *ent = sight_entities[i];
        float const d = Vector2_distance(&ent->s.origin2, &self->s.origin2);
        uint32_t const priority = self->movement.creep_guard_enabled &&
            self->s.player == PLAYER_NEUTRAL_AGGRESSIVE ?
            ai_creep_target_priority(self, ent) : ai_bot_target_priority(self, ent);
        if (d >= radius) continue;
        if (priority < best_priority || (priority == best_priority && d < best_dist)) {
            best_priority = priority;
            best_dist = d;
            best = ent;
        }
    }
    return best;
}

void ai_stand(edict_t *self) {
    if (!(self->svflags & SVF_MONSTER))
        return;
    /* Upgrading structures keep their world entity but their ordinary
     * abilities/orders are construction-disabled in Warcraft/Warsmash. */
    if (G_BuildingUpgradeActive(self))
        return;
    if (G_UnitQueuedOrderCount(self) && G_UnitStartNextQueuedOrder(self))
        return;
    if (S_UnitAbilityEvent(self, A_IDLE))
        return;
    /* Neutral creeps sleep until an enemy enters acquisition range, then wake
     * permanently and fight normally.  Campaign defenders that were made hostile
     * by script have already had AI_SLEEPING cleared and use regular acquisition. */
    if (level.mapinfo->players[self->s.player].playerType == kPlayerTypeNeutral) {
        if (self->aiflags & AI_SLEEPING) {
            if (!G_ShouldAcquireThisFrame(self)) return;
            if (!G_FindNearestEnemy(self, G_AcquisitionRange(self))) return;
            self->aiflags &= ~AI_SLEEPING;
        }
    }
    if (!G_ShouldAcquireThisFrame(self))
        return;

    /* A_NO_ACQUIRE applies both to this unit as an acquisition candidate and
     * to its own voluntary acquisition. Explicit Hide must hold fire after the
     * stop order leaves the unit in its ordinary idle stand behavior. */
    if (S_UnitAbilityEvent(self, A_NO_ACQUIRE))
        return;

    /* Autocast gets the first acquisition opportunity. Its ability owns target
     * policy and emits an ordinary order; only if no autocast action starts do
     * we fall through to the existing automatic attack scan. */
    if (G_TryUnitAutocast(self))
        return;

    /* Idle units auto-engage the nearest enemy within acquisition range — for
     * the player's own units too. Units with no attack (workers/critters) and
     * units already chasing/attacking stay as they are. */
    if (!unit_has_attack(self))
        return;

    edict_t *best = G_FindNearestEnemy(self, G_AcquisitionRange(self));
    if (best) {
        S_UnitAbilityEvent(self, A_AUTO_COMBAT_START);
        order_attack(self, best);
        if (self->goalentity == best && self->currentmove && self->currentmove->proc == CAbilityAttack)
            G_CreepGuardAutoCombat(self);
    }
}

void ai_birth(edict_t *self) {
}

void ai_pain(edict_t *self) {
}

/* Stage A: only automatically acquired/retaliatory Neutral Hostile attacks
 * participate. Scripted commands are authoritative and suspend the policy. */
static float creep_guard_misc(cstring_t key, float fallback) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    char *end;
    double parsed;
    if (!value || !*value) return fallback;
    parsed = strtod(value, &end);
    return end == value || *end || !isfinite(parsed) || parsed < 0.0 || parsed > FLT_MAX ? fallback : (float)parsed;
}

void G_CreepGuardInit(edict_t *unit) {
    if (!unit) return;
    unit->movement.creep_guard_enabled = unit->s.player == PLAYER_NEUTRAL_AGGRESSIVE &&
        !(unit->runtime.flags & UNIT_BALANCE_BUILDING);
    unit->movement.creep_guard_origin = unit->s.origin2;
    unit->movement.creep_guard_last_hit_ms = level.time;
    unit->movement.creep_guard_outside_ms = 0;
    unit->movement.creep_guard_return_retries = 0;
    unit->movement.creep_guard_retry_at_ms = 0;
    unit->movement.creep_guard_phase = CREEP_GUARD_IDLE;
}

/* Script controls the auto-guard policy, never the player's Stop anchor.
 * Existing explicit orders remain in place. Re-enabling uses the current
 * position as the anchor only if this unit never had a creep anchor. */
void G_CreepGuardSetEnabled(edict_t *unit, bool enabled) {
    if (!unit) return;
    enabled = enabled && unit->s.player == PLAYER_NEUTRAL_AGGRESSIVE &&
        !(unit->runtime.flags & UNIT_BALANCE_BUILDING);
    if (!enabled) {
        /* The return Move is owned by creep AI. Disabling that policy must
         * cancel its active order, but leave any explicitly issued Move and
         * the independent Stop guard anchor alone. */
        bool const returning = unit->movement.creep_guard_phase == CREEP_GUARD_RETURNING;
        unit->movement.creep_guard_enabled = false;
        G_CreepGuardExplicitOrder(unit);
        if (returning && unit->currentmove && unit->currentmove->proc == CAbilityMove)
            order_stop_cleanup(unit);
        return;
    }
    if (!unit->movement.creep_guard_enabled) {
        unit->movement.creep_guard_last_hit_ms = level.time;
        unit->movement.creep_guard_outside_ms = 0;
        unit->movement.creep_guard_phase = CREEP_GUARD_IDLE;
        unit->movement.creep_guard_return_retries = 0;
        unit->movement.creep_guard_retry_at_ms = 0;
    }
    unit->movement.creep_guard_enabled = true;
}

void G_CreepGuardAutoCombat(edict_t *unit) {
    if (!unit || !unit->inuse || M_IsDead(unit) || !unit->movement.creep_guard_enabled ||
        unit->s.player != PLAYER_NEUTRAL_AGGRESSIVE ||
        unit->movement.creep_guard_phase == CREEP_GUARD_RETURNING) return;
    if (unit->movement.creep_guard_phase != CREEP_GUARD_COMBAT)
        unit->movement.creep_guard_last_hit_ms = level.time;
    unit->movement.creep_guard_phase = CREEP_GUARD_COMBAT;
}

void G_CreepGuardDamaged(edict_t *unit) {
    if (unit && unit->inuse && !M_IsDead(unit) &&
        unit->movement.creep_guard_enabled &&
        unit->s.player == PLAYER_NEUTRAL_AGGRESSIVE)
        unit->movement.creep_guard_last_hit_ms = level.time;
}

/* Stage B: assistance is triggered by a concrete surviving unit's damage, not
 * by each AI think. Compare permanent guard anchors so pursuing creeps cannot
 * merge adjacent camps into an unbounded alert chain. The exact retail camp
 * clustering algorithm remains unresolved; this is a bounded local fallback. */
void G_CreepGuardCallForHelp(edict_t *victim, edict_t *attacker) {
    float radius;
    if (!victim || !attacker || !victim->movement.creep_guard_enabled ||
        victim->s.player != PLAYER_NEUTRAL_AGGRESSIVE || M_IsDead(victim) ||
        !attacker->inuse || M_IsDead(attacker) ||
        !S_SpellIsEnemy(victim, attacker)) return;
    radius = creep_guard_misc("CreepCallForHelp", 600.0f);
    if (radius <= 0.0f) return;
    /* One event fan-out, never recursive: each responder receives an ordinary
     * automatic combat order, not a second camp notification. */
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ally = g_edicts + i;
        if (ally == victim || !ally->inuse || !ally->movement.creep_guard_enabled ||
            ally->s.player != victim->s.player || M_IsDead(ally) ||
            ally->movement.creep_guard_phase == CREEP_GUARD_RETURNING ||
            (ally->aiflags & AI_IMMOBILE) ||
            G_UnitQueuedOrderCount(ally) ||
            !ally->currentmove ||
            (ally->currentmove->think != ai_stand && !G_UnitIsSleeping(ally)) ||
            !unit_has_attack(ally) ||
            Vector2_distance(&ally->movement.creep_guard_origin,
                             &victim->movement.creep_guard_origin) > radius ||
            Vector2_distance(&ally->s.origin2, &victim->s.origin2) > radius ||
            !S_SpellIsEnemy(ally, attacker) ||
            !S_AttackCanTarget(ally, attacker)) continue;
        /* Wake only natural ACsp sleepers. Dreadlord Sleep and other disabling
         * effects remain governed by their own attack eligibility checks. */
        if (G_UnitIsSleeping(ally)) G_UnitWakeUp(ally);
        if (S_UnitAbilityEvent(ally, A_NO_RETALIATE) ||
            S_UnitAbilityEvent(ally, A_NO_ACQUIRE)) continue;
        S_UnitAbilityEvent(ally, A_AUTO_COMBAT_START);
        order_attack(ally, attacker);
        if (ally->goalentity == attacker && ally->currentmove &&
            ally->currentmove->proc == CAbilityAttack)
            G_CreepGuardAutoCombat(ally);
    }
}

/* Construction is a discrete provocation, not a perpetual building
 * acquisition target. Only actual starts (never placement previews or progress
 * ticks) notify guarding creeps. Keep the anchored camp bounded and do not
 * override a script-owned or queued order. */
void G_CreepGuardConstructionStarted(edict_t *building) {
    float radius;
    if (!building || !building->inuse ||
        !(building->runtime.flags & UNIT_BALANCE_BUILDING)) return;
    radius = creep_guard_misc("BuildingPlacementNotifyRadius", 600.0f);
    if (radius <= 0.0f) return;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *creep = g_edicts + i;
        if (!creep->inuse || !creep->movement.creep_guard_enabled ||
            creep->s.player != PLAYER_NEUTRAL_AGGRESSIVE || M_IsDead(creep) ||
            (creep->aiflags & AI_IMMOBILE) ||
            creep->movement.creep_guard_phase == CREEP_GUARD_RETURNING || G_UnitQueuedOrderCount(creep) ||
            !creep->currentmove ||
            (creep->currentmove->think != ai_stand && !G_UnitIsSleeping(creep)) ||
            !unit_has_attack(creep) ||
            Vector2_distance(&creep->movement.creep_guard_origin, &building->s.origin2) > radius ||
            Vector2_distance(&creep->s.origin2, &building->s.origin2) > radius ||
            !S_SpellIsEnemy(creep, building) ||
            !S_AttackCanTarget(creep, building) ||
            S_UnitAbilityEvent(creep, A_NO_RETALIATE) ||
            S_UnitAbilityEvent(creep, A_NO_ACQUIRE)) continue;
        if (G_UnitIsSleeping(creep)) G_UnitWakeUp(creep);
        S_UnitAbilityEvent(creep, A_AUTO_COMBAT_START);
        order_attack(creep, building);
        if (creep->goalentity == building && creep->currentmove &&
            creep->currentmove->proc == CAbilityAttack)
            G_CreepGuardAutoCombat(creep);
    }
}

void G_CreepGuardExplicitOrder(edict_t *unit) {
    if (!unit) return;
    unit->movement.creep_guard_phase = CREEP_GUARD_IDLE;
    unit->movement.creep_guard_outside_ms = 0;
    unit->movement.creep_guard_return_retries = 0;
    unit->movement.creep_guard_retry_at_ms = 0;
}

/* Retry a failed homeward Move at most three times, once per simulation
 * second. Never force-teleport a blocked creep or override script orders. */
#define CREEP_GUARD_RETURN_RETRIES 3u
#define CREEP_GUARD_RETRY_DELAY_MS 1000u

static bool creep_guard_begin_return(edict_t *unit) {
    edict_t *point;
    if ((unit->aiflags & AI_IMMOBILE) || G_UnitQueuedOrderCount(unit)) return false;
    unit->movement.creep_guard_outside_ms = 0;
    unit->movement.creep_guard_phase = CREEP_GUARD_RETURNING;
    unit->movement.creep_guard_return_retries = 0;
    unit->movement.creep_guard_retry_at_ms = 0;
    unit_leavecombat(unit);
    unit->goalentity = NULL;
    unit->attack_target_spawn_time = 0;
    point = Waypoint_add(&unit->movement.creep_guard_origin);
    if (point) order_move(unit, point);
    /* order_move can reject movement (root, Cyclone, etc). Keep the return
     * policy active and let the tick retry when movement is possible. */
    if (!point || !unit->currentmove || unit->currentmove->proc != CAbilityMove) {
        unit->movement.creep_guard_retry_at_ms = level.time + CREEP_GUARD_RETRY_DELAY_MS;
        if (unit->stand) unit->stand(unit);
    }
    return true;
}

bool G_CreepGuardCombatEnd(edict_t *unit) {
    if (!unit || !unit->movement.creep_guard_enabled ||
        unit->movement.creep_guard_phase != CREEP_GUARD_COMBAT ||
        unit->s.player != PLAYER_NEUTRAL_AGGRESSIVE) return false;
    if (Vector2_distance(&unit->s.origin2, &unit->movement.creep_guard_origin) <= 4.0f) {
        if (unit->movement.creep_guard_phase == CREEP_GUARD_COMBAT) unit->movement.creep_guard_phase = CREEP_GUARD_IDLE;
        return false;
    }
    return creep_guard_begin_return(unit);
}

void G_CreepGuardTick(edict_t *unit) {
    float distance, soft, hard, seconds;
    uint32_t now;
    if (!unit || !unit->inuse || !unit->movement.creep_guard_enabled ||
        unit->s.player != PLAYER_NEUTRAL_AGGRESSIVE || M_IsDead(unit)) return;
    distance = Vector2_distance(&unit->s.origin2, &unit->movement.creep_guard_origin);
    if (unit->movement.creep_guard_phase == CREEP_GUARD_RETURNING) {
        if (distance <= 4.0f) {
            if (unit->movement.creep_guard_phase == CREEP_GUARD_RETURNING) unit->movement.creep_guard_phase = CREEP_GUARD_IDLE;
            unit->movement.creep_guard_outside_ms = 0;
            unit->movement.creep_guard_return_retries = 0;
            unit->movement.creep_guard_retry_at_ms = 0;
            return;
        }
        /* Retry only when the return Move has stopped. A blocked or rooted
         * unit cannot consume infinite waypoints each frame. */
        if (unit->currentmove && unit->currentmove->proc == CAbilityMove &&
            unit->currentmove->think != ai_stand)
            return;
        if (unit->movement.creep_guard_return_retries >= CREEP_GUARD_RETURN_RETRIES) {
            if (unit->movement.creep_guard_phase == CREEP_GUARD_RETURNING) unit->movement.creep_guard_phase = CREEP_GUARD_IDLE;
            return;
        }
        if (!unit->movement.creep_guard_retry_at_ms) {
            unit->movement.creep_guard_retry_at_ms = level.time + CREEP_GUARD_RETRY_DELAY_MS;
            return;
        }
        if ((int32_t)(level.time - unit->movement.creep_guard_retry_at_ms) < 0)
            return;
        /* Temporary disables postpone the attempt, but do not spend its
         * bounded failure budget. A three-second root is not three path
         * failures. Use the simulation clock for the next eligible check. */
        if ((unit->aiflags & AI_IMMOBILE) || unit->paused || unit->stunned ||
            S_UnitIsCycloned(unit) || unit_hasstatusstate(unit, WC3_STATUS_STATE_ROOTED) ||
            S_PurgeIsImmobilized(unit)) {
            unit->movement.creep_guard_retry_at_ms = level.time + CREEP_GUARD_RETRY_DELAY_MS;
            return;
        }
        unit->movement.creep_guard_return_retries++;
        unit->movement.creep_guard_retry_at_ms = level.time + CREEP_GUARD_RETRY_DELAY_MS;
        edict_t *point = Waypoint_add(&unit->movement.creep_guard_origin);
        if (point) order_move(unit, point);
        return;
    }
    if (unit->movement.creep_guard_phase != CREEP_GUARD_COMBAT) return;
    /* An automatically acquired attack can be terminated by an ability or
     * target removal without passing the normal attack-end callback. Recover
     * from an idle transition, but never override another active movement. */
    if (!unit->currentmove || unit->currentmove->think == ai_stand) {
        G_CreepGuardCombatEnd(unit);
        return;
    }
    if (unit->currentmove->proc != CAbilityAttack) return;
    soft = creep_guard_misc("GuardDistance", 600.0f);
    hard = creep_guard_misc("MaxGuardDistance", 1000.0f);
    if (hard < soft) hard = soft;
    seconds = creep_guard_misc("GuardReturnTime", 5.0f);
    /* The map value is in seconds but the simulation clock uses uint32
     * milliseconds. Clamp before conversion to avoid undefined float-to-int
     * overflow on malformed or extreme custom-map Misc values. */
    if (seconds > (float)(UINT32_MAX / 1000u))
        seconds = (float)(UINT32_MAX / 1000u);
    now = level.time;
    if (distance <= soft) {
        unit->movement.creep_guard_outside_ms = 0;
        return;
    }
    if (!unit->movement.creep_guard_outside_ms)
        unit->movement.creep_guard_outside_ms = now ? now : 1;
    if (distance > hard ||
        (now - unit->movement.creep_guard_outside_ms >= (uint32_t)(seconds * 1000.0f) &&
         now - unit->movement.creep_guard_last_hit_ms >= (uint32_t)(seconds * 1000.0f)))
        creep_guard_begin_return(unit);
}
