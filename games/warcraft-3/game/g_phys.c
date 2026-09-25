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
#include "skills/s_skills.h"

/* Hero per-attribute regen bonuses (WC3 Units\MiscGame.txt):
 * StrRegenBonus=0.05 HP/sec per Strength, IntRegenBonus=0.05 mana/sec per
 * Intelligence.  hero.str/intel are 0 on non-heroes, so this only affects heroes. */
#define STR_REGEN_BONUS 0.05f
#define INT_REGEN_BONUS 0.05f

/* IS_HOLLOW is shared and lives in g_local.h. */
#define IS_STATIC(ent) (ent->movetype == MOVETYPE_NONE)
#define IS_MOVING(ent) (ent->currentmove && ent->currentmove->proc == CAbilityMove)
extern void spell_run_frame(edict_t *ent);

void G_PushEntity(edict_t *ent, float distance, vector2_t const *direction) {
    ent->s.origin2 = Vector2_mad(&ent->s.origin2, distance, direction);
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.LinkEntity(ent);
}

void G_PushEntity3(edict_t *ent, float distance, vector3_t const *direction) {
    ent->s.origin = Vector3_mad(&ent->s.origin, distance, direction);
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.LinkEntity(ent);
}

void SV_Physics_Step(edict_t *ent) {
    M_CheckGround(ent);
}

/* Missile entities use the same snapshot/model path as ordinary units, but
 * they do not run monster_think() and therefore never select/advance an MDX
 * sequence on their own.  Start with the authored Stand sequence (the normal
 * travelling sequence for WC3 missiles), falling back to Birth for models
 * that only provide a one-shot sequence.  Keeping this state on the server
 * means the existing entity snapshot contract remains sufficient for every
 * client renderer. */
void G_StartProjectilePresentation(edict_t *ent) {
    animation_t const *anim;

    if (!ent) return;
    ent->s.flags |= EF_NOT_SELECTABLE;
    ent->s.renderfx |= RF_NO_SHADOW;
    if (!ent->s.model) return;

    anim = G_GetAnimation(ent->s.model, "Stand");
    /* TODO: Some authored missile models omit Stand; Birth is the only
     * available travel sequence until a data-driven sequence selector exists. */
    if (!anim) anim = G_GetAnimation(ent->s.model, "Birth");
    ent->animation = anim;
    if (anim) ent->s.frame = anim->interval[0];
}

static void G_AdvanceProjectilePresentation(edict_t *ent) {
    animation_t const *anim = ent->animation;
    uint32_t step;
    uint32_t next;

    /* Save/load does not need a new pointer contract: reacquire the sequence
     * from the already-networked model when a live missile has no cached
     * animation pointer. */
    if (!anim && ent->s.model) {
        G_StartProjectilePresentation(ent);
        anim = ent->animation;
    }
    if (!anim || anim->interval[1] <= anim->interval[0]) return;

    step = (uint32_t)MAX(1.0f, FRAMETIME);
    next = ent->s.frame + step;
    if (next >= anim->interval[1]) {
        if (anim->flags & 1)
            next = anim->interval[1] - 1;
        else
            next = anim->interval[0] + (next - anim->interval[0]) %
                   (anim->interval[1] - anim->interval[0]);
    }
    ent->s.frame = next;
}

/* Move a projectile (MOVETYPE_FLYMISSILE) one frame toward its target.
 * If the distance remaining is less than the per-frame travel distance the
 * projectile hits, deals damage via T_Damage(), and is freed. */
void SV_Physics_Toss(edict_t *ent) {
    float distance;
    vector3_t target, dir;
    bool const fixed_target = (ent->aiflags & AI_PROJECTILE_FIXED_TARGET) != 0;

    if (!fixed_target && (!ent->goalentity || !ent->goalentity->inuse)) { G_FreeEdict(ent); return; }
    distance = ent->velocity * FRAMETIME;
    if (fixed_target) {
        target = MAKE(vector3_t, ent->channel.origin.x, ent->channel.origin.y,
                      CM_GetHeightAtPoint(ent->channel.origin.x, ent->channel.origin.y));
    } else {
        target = ent->goalentity->s.origin;
        /* s.origin already contains support surface + current FlyHeight.  ImpactZ
         * is the model-local target point on top of that airborne/ground origin. */
        target.z += G_UnitImpactZ(ent->goalentity->class_id);
    }
    dir = Vector3_sub(&target, &ent->s.origin);
    if (Vector3_len(&dir) < distance) {
        if (ent->currentmove && ent->currentmove->endfunc) {
            ent->currentmove->endfunc(ent);
        } else {
            /* Abilities own projectile-impact reactions before the normal hit path. */
            if (!fixed_target && S_UnitProjectileHit(ent)) return;
            /* Basic attack missiles carry the launch-time raw roll. Resolve
             * target defense/armor on impact, matching Warsmash and allowing
             * in-flight armor/defense changes to affect the hit. Spell
             * missiles install currentmove/endfunc and bypass this branch. */
            if (fixed_target) {
                vector2_t impact = fixed_target ? ent->channel.origin : ent->goalentity->s.origin2;
                edict_t *primary = ent->goalentity;
                if (fixed_target && primary &&
                    (!primary->inuse || primary->spawn_time != ent->channel.target_spawn_time)) primary = NULL;
                S_ResolveArtilleryPointHit(ent->owner, primary, &impact, ent->damage, &ent->artillery);
            } else {
                int const damage = G_AttackDamageWithType(ent->owner, ent->goalentity, ent->damage,
                                                          ent->projectile_attack_type);
                S_ResolveAttackHit(ent->owner, ent->goalentity, damage);
            }
            G_FreeEdict(ent);
        }
    } else {
        Vector3_normalize(&dir);
        /* Homing projectiles must visually follow their changing trajectory;
         * the previous implementation kept only the launch-time yaw. */
        ent->s.angle = atan2f(dir.y, dir.x);
        G_AdvanceProjectilePresentation(ent);
        G_PushEntity3(ent, distance, &dir);
    }
}

void SV_Physics_Link(edict_t *ent) {
    vector3_t const old = ent->s.origin;
    ent->s.origin = ent->goalentity->s.origin;
    ent->s.angle = ent->goalentity->s.angle;
    if ((ent->s.flags & EF_FOW_BLOCKER) && memcmp(&old, &ent->s.origin, sizeof(old))) G_FowMarkBlockersDirty();
}

/* Whether a unit's hit points regenerate right now, per its WC3 regenType
 * ("uhrt": always / night / blight / none).  Unknown/missing defaults to
 * always, which is the most common case. */
static bool G_UnitRegeneratesHP(edict_t const *ent) {
    cstring_t const type = ent->data.UnitBalance->healthRegenType;
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
        return G_IsPointBlighted(&ent->s.origin2);
    }
    return true; /* "always" */
}

/* Per-entity update called every game frame.  Runs physics based on movetype,
 * then calls the entity's think function, and finally compresses health/mana
 * into the 8-bit stat fields that are sent to clients. */
void G_RunEntity(edict_t *ent) {
    if (!ent->inuse) return; /* defensive: freed edicts carry no simulation state */
    bool const soul_trapped = !!(ent->aiflags & AI_SOUL_TRAPPED);
    spell_run_frame(ent);
    unit_updatestatuses(ent);
    if (!soul_trapped) {
        SAFE_CALL(ent->prethink, ent);
        switch (ent->movetype) {
            case MOVETYPE_STEP: SV_Physics_Step(ent); break;
            case MOVETYPE_FLYMISSILE: SV_Physics_Toss(ent); break;
            case MOVETYPE_LINK: SV_Physics_Link(ent); break;
            default:
//            gi.error("SV_Physics: bad movetype %d", edict->movetype);
                break;
        }
    }
    G_RunConstructionFrame(ent);
    G_RunBuildingUpgradeFrame(ent);
    if (!soul_trapped) SAFE_CALL(ent->think, ent);
    /* Mana regeneration (WC3 'umpr', mana/second), plus a hero's Intelligence
     * regen bonus (MiscGame IntRegenBonus = 0.05 mana/sec per Intelligence;
     * hero.intel is 0 for non-heroes). */
    if (ent->mana.max_value > 0 && ent->mana.value < ent->mana.max_value) {
        float const rate = ent->data.UnitBalance->manaRegen + ent->mana_regen_bonus
                 + (float)ent->hero.intel * INT_REGEN_BONUS + S_BrillianceManaRegen(ent)
                 + S_RegenerationManaAura(ent);
        ent->mana.value = MIN(ent->mana.max_value, ent->mana.value + rate * (FRAMETIME / 1000.0f));
    }
    /* Hit-point regeneration (WC3 'uhpr', HP/second), plus a hero's Strength
     * regen bonus (MiscGame StrRegenBonus = 0.05 HP/sec per Strength).  Gated by
     * the unit's 'uhrt' regenType: "always" any time, "night" only at night
     * (night elves), "blight" only on the authoritative Blight pathing field,
     * "none" never.  Living, wounded units only. */
    if (ent->health.max_value > 0 && ent->health.value > 0 && ent->health.value < ent->health.max_value) {
        float const aura = S_RegenerationHealthAura(ent);
        float const rejuv = S_RejuvHealRate(ent);
        float rate = aura + rejuv;
        bool const natural = G_UnitRegeneratesHP(ent);
        if (natural)
            rate += ent->data.UnitBalance->healthRegen +
                    (float)ent->hero.str * STR_REGEN_BONUS + S_UnholyHealthRegen(ent);
        if (rate != 0.0f) G_AddHealth(ent, rate * (FRAMETIME / 1000.0f));
    }
    /* Unholy Frenzy drains HP at DataB HP/sec regardless of current health. */
    { float const drain = S_UnholyFrenzyLifeDrain(ent); if (drain > 0.0f && ent->health.value > 0) G_AddHealth(ent, -drain * (FRAMETIME / 1000.0f)); }
    /* Soul Burn deals DataA damage per second to the afflicted unit. */
    { float const rate = S_SoulBurnDamageRate(ent); if (rate > 0.0f && ent->health.value > 0) G_AddHealth(ent, -rate * (FRAMETIME / 1000.0f)); }
    ent->s.stats[ENT_HEALTH] = compress_stat(&ent->health);
    ent->s.stats[ENT_MANA] = compress_stat(&ent->mana);
    if (ent->currentmove) {
        ent->s.ability = GetAbilityIndex(ent->currentmove->proc);
    } else {
        ent->s.ability = 255;
    }
    ent->s.class_id = ent->class_id;
}

inline bool M_CheckCollision(vector2_t const *origin, float radius) {
    for (edict_t *a = globals.edicts; a - globals.edicts < globals.num_edicts; a++) {
        vector2_t d = Vector2_sub(&a->s.origin2, origin);
        if (IS_HOLLOW(a))
            continue;
        if (IS_STATIC(a))
            continue;
        if (Vector2_len(&d) < radius + a->collision)
            return true;
    }
    return false;
}

/* Collision is now enforced at move time: unit_trymove() (skills/s_move.c) only commits
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
// static edict_t const *phys_current_entity = NULL;
// static bool FilterColliders(edict_t const *ent) {
//     return ent != phys_current_entity && !IS_HOLLOW(ent);
// }
// #define MAX_COLLIDERS 256
// static edict_t *sv_colliders[MAX_COLLIDERS];

void G_SolveCollisions(void) {
}
