/*
 * g_phys.c — Server-side physics and collision resolution.
 *
 * G_RunEntity() is called every game frame for each live entity.  It
 * dispatches on the entity's movetype:
 *   MOVETYPE_STEP        — ground-hugging units; snaps Z to terrain height.
 *   MOVETYPE_FLYMISSILE  — projectiles; moves toward goalentity and deals
 *                          damage on arrival (SV_Physics_Toss).
 *   MOVETYPE_LINK        — entities locked to another entity's position.
 *
 * After all entities have moved, G_SolveCollisions() resolves overlapping
 * entity pairs by pushing them apart.  Moving units share the separation
 * proportionally based on their remaining distance to their goal, which
 * prevents deadlocks when many units converge on the same destination.
 */
#include "g_local.h"

#define IS_HOLLOW(ent) ((ent->svflags & SVF_DEADMONSTER) || (ent->s.renderfx & RF_HIDDEN) || !ent->s.model || !ent->inuse)
#define IS_STATIC(ent) (ent->movetype == MOVETYPE_NONE)
#define IS_MOVING(ent) (ent->currentmove && ent->currentmove->ability == &a_move)

extern ability_t a_move;

void G_PushEntity(LPEDICT ent, FLOAT distance, LPCVECTOR2 direction) {
    ent->s.origin2 = Vector2_mad(&ent->s.origin2, distance, direction);
    gi.LinkEntity(ent);
}

void G_PushEntity3(LPEDICT ent, FLOAT distance, LPCVECTOR3 direction) {
    ent->s.origin = Vector3_mad(&ent->s.origin, distance, direction);
    gi.LinkEntity(ent);
}

void SV_Physics_Step(LPEDICT ent) {
    M_CheckGround(ent);
}

/* Move a projectile (MOVETYPE_FLYMISSILE) one frame toward its target.
 * If the distance remaining is less than the per-frame travel distance the
 * projectile hits, deals damage via T_Damage(), and is freed. */
void SV_Physics_Toss(LPEDICT ent) {
    FLOAT distance = ent->velocity * FRAMETIME;
    VECTOR3 dir = Vector3_sub(&ent->goalentity->s.origin, &ent->s.origin);
    if (Vector3_len(&dir) < distance) {
        if (ent->currentmove && ent->currentmove->endfunc) {
            ent->currentmove->endfunc(ent);
        } else {
            T_Damage(ent->goalentity, ent->owner, ent->damage);
            G_FreeEdict(ent);
        }
    } else {
        Vector3_normalize(&dir);
        G_PushEntity3(ent, distance, &dir);
    }
}

void SV_Physics_Link(LPEDICT ent) {
    ent->s.origin = ent->goalentity->s.origin;
    ent->s.angle = ent->goalentity->s.angle;
}

/* Per-entity update called every game frame.  Runs physics based on movetype,
 * then calls the entity's think function, and finally compresses health/mana
 * into the 8-bit stat fields that are sent to clients. */
void G_RunEntity(LPEDICT ent) {
    unit_updatestatuses(ent);
    SAFE_CALL(ent->prethink, ent);
    switch (ent->movetype) {
        case MOVETYPE_STEP:
            SV_Physics_Step(ent);
            break;
        case MOVETYPE_FLYMISSILE:
            SV_Physics_Toss(ent);
            break;
        case MOVETYPE_LINK:
            SV_Physics_Link(ent);
            break;
        default:
//            gi.error("SV_Physics: bad movetype %d", edict->movetype);
            break;
    }
    SAFE_CALL(ent->think, ent);
    /* Mana regeneration (WC3 'umpr', mana/second). Casters were stuck empty
     * once drained. */
    if (ent->mana.max_value > 0 && ent->mana.value < ent->mana.max_value) {
        FLOAT const regen = UNIT_MANA_REGENERATION(ent->class_id) * (FRAMETIME / 1000.0f);
        ent->mana.value = MIN(ent->mana.max_value, ent->mana.value + regen);
    }
    ent->s.stats[ENT_HEALTH] = compress_stat(&ent->health);
    ent->s.stats[ENT_MANA] = compress_stat(&ent->mana);
    if (ent->currentmove) {
        ent->s.ability = GetAbilityIndex(ent->currentmove->ability);
    } else {
        ent->s.ability = 0;
    }
    ent->s.class_id = ent->class_id;
}

inline BOOL M_CheckCollision(LPCVECTOR2 origin, FLOAT radius) {
    for (LPEDICT a = globals.edicts; a - globals.edicts < globals.num_edicts; a++) {
        VECTOR2 d = Vector2_sub(&a->s.origin2, origin);
        if (IS_HOLLOW(a))
            continue;
        if (IS_STATIC(a))
            continue;
        if (Vector2_len(&d) < radius + a->collision)
            return true;
    }
    return false;
}

static LPCEDICT phys_current_entity = NULL;

static BOOL FilterColliders(LPCEDICT ent) {
    return ent != phys_current_entity && !IS_HOLLOW(ent);
}

#define MAX_COLLIDERS 256

static LPEDICT sv_colliders[MAX_COLLIDERS];

/* Resolve all entity-entity overlaps for one game frame.
 * For each entity, nearby candidates are fetched with a bounding-box query.
 * Overlapping pairs are pushed apart:
 *   - If one side is static, only the dynamic entity moves.
 *   - If both are actively walking, separation is split by distance-to-goal
 *     so that the unit closer to its destination yields more ground.
 *   - Otherwise the separation is split evenly. */
void G_SolveCollisions(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT a = g_edicts+i;
        if (IS_HOLLOW(a) || IS_STATIC(a))
            continue;
        phys_current_entity = a;
        DWORD num_colliders = gi.BoxEdicts(&a->bounds, sv_colliders, MAX_COLLIDERS, FilterColliders);
        FOR_LOOP(j, num_colliders) {
            LPEDICT b = sv_colliders[j];
            FLOAT const radius = (a->collision + b->collision);
            FLOAT const distance = Vector2_distance(&a->s.origin2, &b->s.origin2);
            if (distance >= radius)
                continue;
            VECTOR2 d = Vector2_sub(&a->s.origin2, &b->s.origin2);
            Vector2_normalize(&d);
            FLOAT const diff = distance - radius;
            if (IS_STATIC(b)) {
                G_PushEntity(a, -diff, &d);
            } else if (IS_MOVING(a) && IS_MOVING(b)) {
                FLOAT const ad = M_DistanceToGoal(a);
                FLOAT const bd = M_DistanceToGoal(b);
                G_PushEntity(a, -diff * ad / (ad + bd), &d);
                G_PushEntity(b, diff * bd / (ad + bd), &d);
            } else {
                G_PushEntity(a, -diff * 0.5f, &d);
                G_PushEntity(b, diff * 0.5f, &d);
                /* Transitive bumping: a stopped unit that has reached its
                 * goal passes arrival to any overlapping unit heading for
                 * the same goal.  This prevents the jitter that occurs when
                 * collision nudges keep a nearly-arrived unit oscillating
                 * around the destination. */
                if (a->goalentity && a->goalentity == b->goalentity) {
                    if (!IS_MOVING(a) && IS_MOVING(b)) b->stand(b);
                    if (!IS_MOVING(b) && IS_MOVING(a)) a->stand(a);
                }
            }
        }
    }
}
