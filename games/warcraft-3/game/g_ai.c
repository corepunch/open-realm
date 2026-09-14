#include "g_local.h"
#include "skills/s_skills.h"

void unit_setanimation(LPEDICT self, LPCSTR anim) {
    G_SetUnitAnimation(self, anim);
}

static BOOL unit_is_active_repair_move(LPEDICT self) {
    char rawcode[5];
    ability_t const *handler;

    if (!self || !self->currentmove || !self->buildwork.ability) return false;
    memcpy(rawcode, &self->buildwork.ability, 4);
    rawcode[4] = '\0';
    handler = FindAbilityForCommand(rawcode);
    return handler && self->currentmove->proc == handler->proc;
}

void unit_setmove(LPEDICT self, umove_t *move) {
    BOOL was_idle = G_UnitIsIdleWorker(self);


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

void unit_runwait(LPEDICT self, void (*callback)(LPEDICT )) {
    if (self->wait <= 0)
        return;
    if (self->wait > FRAMETIME / 1000.f) {
        self->wait -= FRAMETIME / 1000.f;
    } else {
        self->wait = 0;
        callback(self);
    }
}

void ai_idle(LPEDICT self) {
}

void order_attack(LPEDICT self, LPEDICT target);

#define MAX_SIGHT_ENTITIES 256

static LPEDICT ai_current_entity = NULL;
static LPEDICT sight_entities[MAX_SIGHT_ENTITIES];

static BOOL filter_sight(LPCEDICT ent) {
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
    /* Warsmash excludes invulnerable units from automatic attack acquisition;
     * explicit orders still perform their own target validation. */
    if (ent->invulnerable)
        return false;
    if (S_UnitAbilityEvent((LPEDICT)ent, A_NO_ACQUIRE))
        return false;
    if (ent->runtime.flags & UNIT_BALANCE_BUILDING)
        return false;
    return true;
}

/* Does this unit have an attack to acquire targets with? */
static BOOL unit_has_attack(LPCEDICT self) {
    return S_CargoAttacksEnabled(self) && self->attack1.cooldown > 0.0f &&
           (self->attack1.damageBase > 0 || self->attack1.numberOfDice > 0);
}

/* Throttle target re-acquisition: units scan only a few times per second,
 * staggered by entity index, instead of every sim tick. */
#define AI_ACQUIRE_INTERVAL 300 /* ms */

BOOL G_ShouldAcquireThisFrame(LPCEDICT self) {
    DWORD const stagger = (DWORD)(self - g_edicts) % AI_ACQUIRE_INTERVAL;
    return ((level.time + stagger) % AI_ACQUIRE_INTERVAL) < (DWORD)FRAMETIME;
}

/* Return the spawn-cached range; repeated SLK walks dominated large acquisition scans. */
FLOAT G_AcquisitionRange(LPCEDICT self) {
    return self->runtime.acquisition_range;
}

LPEDICT G_FindNearestEnemy(LPEDICT self, FLOAT radius) {
    ai_current_entity = self;
    BOX2 const sightbox = {
        { self->s.origin2.x - radius, self->s.origin2.y - radius },
        { self->s.origin2.x + radius, self->s.origin2.y + radius },
    };
    DWORD numents = gi.BoxEdicts(&sightbox, sight_entities, MAX_SIGHT_ENTITIES, filter_sight);
    LPEDICT best = NULL;
    FLOAT best_dist = radius;
    FOR_LOOP(i, numents) {
        LPEDICT ent = sight_entities[i];
        FLOAT const d = Vector2_distance(&ent->s.origin2, &self->s.origin2);
        if (d < best_dist) {
            best_dist = d;
            best = ent;
        }
    }
    return best;
}

void ai_stand(LPEDICT self) {
    if (!(self->svflags & SVF_MONSTER))
        return;
    if (S_UnitAbilityEvent(self, A_IDLE))
        return;
    /* Neutral/creep units do not initiate (avoids map-wide neutral-vs-neutral
     * aggression); campaign defenders may be rescuable until script takeover
     * and still use normal unit auto-acquisition while hostile. */
    if (level.mapinfo->players[self->s.player].playerType == kPlayerTypeNeutral)
        return;
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

    LPEDICT best = G_FindNearestEnemy(self, G_AcquisitionRange(self));
    if (best) {
        order_attack(self, best);
    }
}

void ai_birth(LPEDICT self) {
}

void ai_pain(LPEDICT self) {
}
