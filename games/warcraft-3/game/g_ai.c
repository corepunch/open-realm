#include "g_local.h"
#include "skills/s_skills.h"

void unit_setanimation(edict_t * self, cstring_t anim) {
    /* Walk is requested every movement tick. Keep the selected numbered walk
     * sequence until a move transition selects a fresh animation. */
    if (self && anim && !strcmp(anim, "walk") && G_AnimationHasPrimary(self->animation, "walk")) return;
    G_SetUnitAnimation(self, anim);
}

static bool unit_is_active_repair_move(edict_t * self) {
    char rawcode[5];
    ability_t const *handler;

    if (!self || !self->currentmove || !self->buildwork.ability) return false;
    memcpy(rawcode, &self->buildwork.ability, 4);
    rawcode[4] = '\0';
    handler = FindAbilityForCommand(rawcode);
    return handler && self->currentmove->proc == handler->proc;
}

void unit_setmove(edict_t * self, umove_t *move) {
    bool was_idle = G_UnitIsIdleWorker(self);

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
        S_UnitAbilityEvent(self, A_MOVE_LEAVE);
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
}

void unit_runwait(edict_t * self, void (*callback)(edict_t * )) {
    if (self->wait <= 0)
        return;
    if (self->wait > FRAMETIME / 1000.f) {
        self->wait -= FRAMETIME / 1000.f;
    } else {
        self->wait = 0;
        callback(self);
    }
}

void ai_idle(edict_t * self) {
}

void order_attack(edict_t * self, edict_t * target);

#define MAX_SIGHT_ENTITIES 256

static edict_t * ai_current_entity = NULL;
static edict_t * sight_entities[MAX_SIGHT_ENTITIES];

static bool unit_has_attack(edict_t const * self);

static bool filter_sight(edict_t const * ent) {
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
    if (S_UnitIsInvisibleToPlayer(ent, ai_current_entity->s.player))
        return false;
    /* Warsmash excludes invulnerable units from automatic attack acquisition;
     * explicit orders still perform their own target validation. */
    if (ent->invulnerable)
        return false;
    if (S_UnitAbilityEvent((edict_t *)ent, A_NO_ACQUIRE))
        return false;
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
static bool unit_has_attack(edict_t const * self) {
    return S_CargoAttacksEnabled(self) &&
           ((S_UnitAttackSlotEnabled(self, 0) && self->attack1.cooldown > 0.0f && (self->attack1.damageBase > 0 || self->attack1.numberOfDice > 0)) ||
            (S_UnitAttackSlotEnabled(self, 1) && self->attack2.cooldown > 0.0f && (self->attack2.damageBase > 0 || self->attack2.numberOfDice > 0)));
}

/* Throttle target re-acquisition: units scan only a few times per second,
 * staggered by entity index, instead of every sim tick. */
#define AI_ACQUIRE_INTERVAL 300 /* ms */

bool G_ShouldAcquireThisFrame(edict_t const * self) {
    uint32_t const stagger = (uint32_t)(self - g_edicts) % AI_ACQUIRE_INTERVAL;
    return ((level.time + stagger) % AI_ACQUIRE_INTERVAL) < (uint32_t)FRAMETIME;
}

/* Return the spawn-cached range; repeated SLK walks dominated large acquisition scans. */
float G_AcquisitionRange(edict_t const * self) {
    return self->runtime.acquisition_range;
}

edict_t * G_FindNearestEnemy(edict_t * self, float radius) {
    ai_current_entity = self;
    box2_t const sightbox = {
        { self->s.origin2.x - radius, self->s.origin2.y - radius },
        { self->s.origin2.x + radius, self->s.origin2.y + radius },
    };
    uint32_t numents = gi.BoxEdicts(&sightbox, sight_entities, MAX_SIGHT_ENTITIES, filter_sight);
    edict_t * best = NULL;
    float best_dist = radius;
    FOR_LOOP(i, numents) {
        edict_t * ent = sight_entities[i];
        float const d = Vector2_distance(&ent->s.origin2, &self->s.origin2);
        if (d < best_dist) {
            best_dist = d;
            best = ent;
        }
    }
    return best;
}

void ai_stand(edict_t * self) {
    if (!(self->svflags & SVF_MONSTER))
        return;
    /* Upgrading structures keep their world entity but their ordinary
     * abilities/orders are construction-disabled in Warcraft/Warsmash. */
    if (G_BuildingUpgradeActive(self))
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

    edict_t * best = G_FindNearestEnemy(self, G_AcquisitionRange(self));
    if (best) {
        order_attack(self, best);
    }
}

void ai_birth(edict_t * self) {
}

void ai_pain(edict_t * self) {
}
