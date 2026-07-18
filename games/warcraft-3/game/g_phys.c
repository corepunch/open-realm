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

/* Hero per-attribute regen bonuses (WC3 Units\MiscGame.txt):
 * StrRegenBonus=0.05 HP/sec per Strength, IntRegenBonus=0.05 mana/sec per
 * Intelligence.  hero.str/intel are 0 on non-heroes, so this only affects heroes. */
#define STR_REGEN_BONUS 0.05f
#define INT_REGEN_BONUS 0.05f

/* IS_HOLLOW is shared and lives in g_local.h. */
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

/* Whether a unit's hit points regenerate right now, per its WC3 regenType
 * ("uhrt": always / night / blight / none).  Unknown/missing defaults to
 * always, which is the most common case. */
static BOOL G_UnitRegeneratesHP(LPCEDICT ent) {
    LPCSTR const type = UNIT_HIT_POINTS_REGENERATION_TYPE_NAME(ent->class_id);
    if (!type || !*type) {
        return true;
    }
    if (!strcmp(type, "none")) {
        return false;
    }
    if (!strcmp(type, "night")) {
        return G_IsNight();
    }
    if (!strcmp(type, "blight")) {
        return false; /* no blight system yet: treat as off-blight (no regen) */
    }
    return true; /* "always" */
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
    /* Mana regeneration (WC3 'umpr', mana/second), plus a hero's Intelligence
     * regen bonus (MiscGame IntRegenBonus = 0.05 mana/sec per Intelligence;
     * hero.intel is 0 for non-heroes). */
    if (ent->mana.max_value > 0 && ent->mana.value < ent->mana.max_value) {
        FLOAT const rate = UNIT_MANA_REGENERATION(ent->class_id)
                         + (FLOAT)ent->hero.intel * INT_REGEN_BONUS;
        ent->mana.value = MIN(ent->mana.max_value, ent->mana.value + rate * (FRAMETIME / 1000.0f));
    }
    /* Hit-point regeneration (WC3 'uhpr', HP/second), plus a hero's Strength
     * regen bonus (MiscGame StrRegenBonus = 0.05 HP/sec per Strength).  Gated by
     * the unit's 'uhrt' regenType: "always" any time, "night" only at night
     * (night elves), "blight" only on blight (no blight system yet -> off-blight
     * = no regen), "none" never.  Living, wounded units only. */
    if (ent->health.max_value > 0 && ent->health.value > 0 &&
        ent->health.value < ent->health.max_value && G_UnitRegeneratesHP(ent)) {
        FLOAT const rate = UNIT_HIT_POINTS_REGENERATION_RATE(ent->class_id)
                         + (FLOAT)ent->hero.str * STR_REGEN_BONUS;
        ent->health.value = MIN(ent->health.max_value, ent->health.value + rate * (FRAMETIME / 1000.0f));
    }
    ent->s.stats[ENT_HEALTH] = compress_stat(&ent->health);
    ent->s.stats[ENT_MANA] = compress_stat(&ent->mana);
    if (ent->currentmove) {
        ent->s.ability = GetAbilityIndex(ent->currentmove->ability);
    } else {
        ent->s.ability = 255;
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

/* Collision is now enforced at move time: unit_trymove() (g_ai.c) only commits
 * a step into a position free of terrain and other units, so units never end up
 * overlapping through normal movement and there is nothing left to push apart
 * here.  The old penalty solver shoved idle bystanders out of the way — which
 * never happens in WC3 — so it is retired.  G_SolveCollisions is kept as a
 * documented no-op so the frame loop (g_main.c) and the test suite retain a
 * stable symbol; spawn/teleport overlaps are resolved at the source by
 * nearest-free-space placement (SP_FindEmptySpaceAround) and by the
 * "don't deepen penetration" rule in move_is_valid().
 *
 * The query helpers it used are retired with it; left commented in case a
 * future global de-overlap pass is ever needed.  (Linux -Wall would warn that
 * these are unused.) */
// static LPCEDICT phys_current_entity = NULL;
// static BOOL FilterColliders(LPCEDICT ent) {
//     return ent != phys_current_entity && !IS_HOLLOW(ent);
// }
// #define MAX_COLLIDERS 256
// static LPEDICT sv_colliders[MAX_COLLIDERS];

void G_SolveCollisions(void) {
}
