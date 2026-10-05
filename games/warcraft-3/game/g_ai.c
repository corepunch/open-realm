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
    if (self->currentmove != move) {
        if (self->currentmove) SAFE_CALL(self->currentmove->leave, self);
        S_UnitAbilityMoveLeave(self, move->proc);
    }
    M_SetMove(self,move);
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
static bool unit_has_attack(edict_t const *self) {
    return S_CargoAttacksEnabled(self) &&
           ((S_UnitAttackSlotEnabled(self, 0) && S_AttackProfileRead(self, 0)->cooldown > 0.0f && (S_AttackProfileRead(self, 0)->damageBase > 0 || S_AttackProfileRead(self, 0)->numberOfDice > 0)) ||
            (S_UnitAttackSlotEnabled(self, 1) && S_AttackProfileRead(self, 1)->cooldown > 0.0f && (S_AttackProfileRead(self, 1)->damageBase > 0 || S_AttackProfileRead(self, 1)->numberOfDice > 0)));
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
    return self && ((S_UnitAttackSlotEnabled(self, 0) && S_AttackProfileRead(self, 0)->type == ATK_SIEGE) ||
                    (S_UnitAttackSlotEnabled(self, 1) && S_AttackProfileRead(self, 1)->type == ATK_SIEGE));
}

/* Melee AI policy setters affect automatic target acquisition, not explicit player/script
 * attack orders. Target Heroes gives legal Heroes priority over ordinary targets. Smart
 * Artillery gives siege-capable AI units structures priority; distance still chooses within
 * a category. */
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

/* This grid only proves that the existing filtered broad phase is empty.
 * It never selects or reorders candidates. Presence expands between frames;
 * removal, death and movement may leave conservative positives until rebuild. */
#define AI_PRESENCE_CELL_SIZE 512 // world units; coarse rejection only; local queries visit a few owner-mask cells
static struct {
    uint32_t *cells, width, height, overflow, unbounded;
    box2_t overflow_bounds[MAX_PLAYERS];
    box2_t bounds;
    bool ready;
} ai_presence;
#ifdef BZ_TESTS
static bool ai_force_broadphase;
static uint32_t ai_broadphase_queries;
#endif

void G_ResetAcquisitionPresence(void) {
    free(ai_presence.cells);
    memset(&ai_presence, 0, sizeof(ai_presence));
#ifdef BZ_TESTS
    ai_force_broadphase = false;
    ai_broadphase_queries = 0;
#endif
}

void G_AcquisitionEntityLinked(edict_t const *ent) {
    if (!ai_presence.ready || !ent->inuse || !ent->area.prev ||
        !(ent->svflags & SVF_MONSTER) || ent->s.player >= MAX_PLAYERS) return;
    uint32_t mask = 1u << ent->s.player;
    box2_t const *box = &ent->bounds, *world = &ai_presence.bounds;
    /* Out-of-map actors still have finite geometry. Retain conservative owner
     * bounds instead of making one edge actor defeat every local query on the
     * entire map. Malformed geometry alone requires an unbounded presence. */
    if (!isfinite(box->min.x) || !isfinite(box->min.y) || !isfinite(box->max.x) || !isfinite(box->max.y) ||
        box->min.x > box->max.x || box->min.y > box->max.y) {
        ai_presence.unbounded |= mask;
        return;
    }
    if (box->min.x < world->min.x || box->min.y < world->min.y ||
        box->max.x > world->max.x || box->max.y > world->max.y) {
        box2_t *bounds = ai_presence.overflow_bounds + ent->s.player;
        if (!(ai_presence.overflow & mask)) *bounds = *box;
        else {
            bounds->min.x = MIN(bounds->min.x, box->min.x);
            bounds->min.y = MIN(bounds->min.y, box->min.y);
            bounds->max.x = MAX(bounds->max.x, box->max.x);
            bounds->max.y = MAX(bounds->max.y, box->max.y);
        }
        ai_presence.overflow |= mask;
        return;
    }
    uint32_t x0 = MIN(ai_presence.width - 1, (uint32_t)(((double)box->min.x - world->min.x) / AI_PRESENCE_CELL_SIZE));
    uint32_t y0 = MIN(ai_presence.height - 1, (uint32_t)(((double)box->min.y - world->min.y) / AI_PRESENCE_CELL_SIZE));
    uint32_t x1 = MIN(ai_presence.width - 1, (uint32_t)(((double)box->max.x - world->min.x) / AI_PRESENCE_CELL_SIZE));
    uint32_t y1 = MIN(ai_presence.height - 1, (uint32_t)(((double)box->max.y - world->min.y) / AI_PRESENCE_CELL_SIZE));
    for (uint32_t y = y0; y <= y1; y++)
        for (uint32_t x = x0; x <= x1; x++) ai_presence.cells[y * ai_presence.width + x] |= mask;
}

void G_BeginAcquisitionFrame(void) {
    box2_t bounds = CM_GetWorldBounds();
    double width = ceil(((double)bounds.max.x - bounds.min.x) / AI_PRESENCE_CELL_SIZE);
    double height = ceil(((double)bounds.max.y - bounds.min.y) / AI_PRESENCE_CELL_SIZE);
    if (!isfinite(width) || !isfinite(height) || width < 1 || height < 1 ||
        width > UINT32_MAX || height > UINT32_MAX || width * height > UINT32_MAX ||
        width * height > SIZE_MAX / sizeof(uint32_t)) {
        gi.error("Acquisition presence: invalid world bounds"); abort();
    }
    if (!ai_presence.cells || memcmp(&bounds, &ai_presence.bounds, sizeof(bounds))) {
        free(ai_presence.cells);
        ai_presence.width = (uint32_t)width; ai_presence.height = (uint32_t)height;
        ai_presence.bounds = bounds;
        ai_presence.cells = calloc((size_t)ai_presence.width * ai_presence.height, sizeof(uint32_t));
        if (!ai_presence.cells) { gi.error("Acquisition presence: allocation failed"); abort(); }
    } else memset(ai_presence.cells, 0, (size_t)ai_presence.width * ai_presence.height * sizeof(uint32_t));
    ai_presence.overflow = ai_presence.unbounded = 0;
    ai_presence.ready = true;
}

static bool ai_enemy_presence(edict_t const *self, box2_t const *box) {
#ifdef BZ_TESTS
    if (ai_force_broadphase) return true;
#endif
    if (!ai_presence.ready) {
        G_BeginAcquisitionFrame();
        FOR_LOOP(i, globals.num_edicts) G_AcquisitionEntityLinked(g_edicts + i);
    }
    if (self->s.player >= MAX_PLAYERS) return false;
    uint32_t enemies = 0;
    FOR_LOOP(player, MAX_PLAYERS)
        if (!G_PlayerTreatsPlayerAsAlly(self->s.player, player)) enemies |= 1u << player;
    if (ai_presence.unbounded & enemies) return true;
    for (uint32_t owners = ai_presence.overflow & enemies; owners; owners &= owners - 1) {
        box2_t const *outside = ai_presence.overflow_bounds + __builtin_ctz(owners);
        if (!(box->min.x > outside->max.x || box->min.y > outside->max.y ||
              box->max.x < outside->min.x || box->max.y < outside->min.y)) return true;
    }
    if (!isfinite(box->min.x) || !isfinite(box->min.y) || !isfinite(box->max.x) || !isfinite(box->max.y)) return true;
    box2_t const *world = &ai_presence.bounds;
    if (box->min.x > world->max.x || box->min.y > world->max.y ||
        box->max.x < world->min.x || box->max.y < world->min.y) return false;
    uint32_t x0 = MIN(ai_presence.width - 1, (uint32_t)((MAX((double)box->min.x, world->min.x) - world->min.x) / AI_PRESENCE_CELL_SIZE));
    uint32_t y0 = MIN(ai_presence.height - 1, (uint32_t)((MAX((double)box->min.y, world->min.y) - world->min.y) / AI_PRESENCE_CELL_SIZE));
    uint32_t x1 = MIN(ai_presence.width - 1, (uint32_t)((MIN((double)box->max.x, world->max.x) - world->min.x) / AI_PRESENCE_CELL_SIZE));
    uint32_t y1 = MIN(ai_presence.height - 1, (uint32_t)((MIN((double)box->max.y, world->max.y) - world->min.y) / AI_PRESENCE_CELL_SIZE));
    for (uint32_t y = y0; y <= y1; y++)
        for (uint32_t x = x0; x <= x1; x++)
            if (ai_presence.cells[y * ai_presence.width + x] & enemies) return true;
    return false;
}

edict_t *G_FindNearestEnemy(edict_t *self, float radius) {
    ai_current_entity = self;
    box2_t const sightbox = {
        { self->s.origin2.x - radius, self->s.origin2.y - radius },
        { self->s.origin2.x + radius, self->s.origin2.y + radius },
    };
    if (!ai_enemy_presence(self, &sightbox)) return NULL;
#ifdef BZ_TESTS
    ai_broadphase_queries++;
#endif
    uint32_t numents = gi.BoxEdicts(&sightbox, sight_entities, MAX_SIGHT_ENTITIES, filter_sight);
    edict_t *best = NULL;
    float best_dist = radius;
    uint32_t best_priority = 2;
    FOR_LOOP(i, numents) {
        edict_t *ent = sight_entities[i];
        float const d = Vector2_distance(&ent->s.origin2, &self->s.origin2);
        uint32_t const priority = ai_bot_target_priority(self, ent);
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
    }
}

void ai_birth(edict_t *self) {
}

void ai_pain(edict_t *self) {
}
