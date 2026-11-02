#include "s_skills.h"

static void flame_strike_projectile_impact(edict_t *missile) {
    channel_t *projectile = G_GetChannel(missile);
    if (projectile)
        G_SpawnAbilityEffectAtPoint(missile->class_id, WC3_EFFECT_EFFECT, 0,
                                    &projectile->origin, true);
    G_FreeEdict(missile);
}

static umove_t flame_strike_projectile_move = {
    .animation = "Stand",
    .endfunc = flame_strike_projectile_impact
};

/* The point spell has no MissileArt field. Blood Mage's HumanUnitFunc row
 * authors its normal BloodElfMissile and speed, which are the projectile used
 * to deliver the ground warning. */
static bool flame_strike_launch_projectile(edict_t *caster, uint32_t code, vec2_t const *point) {
    unitAttack_t const *attack;
    mat4_t matrix;
    vec3_t origin;
    vec2_t direction;
    edict_t *missile;
    channel_t *projectile;

    if (!caster || !caster->unit_state || !point) return false;
    attack = &caster->unit_state->attack1;
    if (!attack->projectile.model || attack->projectile.speed <= 0.0f) return false;
    M_GetEntityMatrix(&caster->s, &matrix);
    origin = Matrix4_multiply_vector3(&matrix, &attack->origin);
    direction = Vector2_sub(point, &caster->s.origin2);
    missile = G_Spawn();
    if (!missile) return false;
    missile->class_id = code;
    missile->owner = caster;
    missile->s.player = caster->s.player;
    missile->s.origin = origin;
    missile->s.origin2 = (vec2_t){ origin.x, origin.y };
    missile->s.angle = atan2f(direction.y, direction.x);
    missile->s.model = attack->projectile.model;
    G_InheritUnitTeamColor(missile, caster);
    projectile = G_AddChannel(missile);
    projectile->owner_spawn_time = caster->spawn_time;
    projectile->origin = *point;
    missile->velocity = attack->projectile.speed / 1000.0f;
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    missile->currentmove = &flame_strike_projectile_move;
    G_StartProjectilePresentation(missile);
    return true;
}

/* Flame Strike stores its cast rank and start time; Hfs1/Hfs3 are pulse damage,
 * Hfs2/Hfs4 are pulse intervals, Cast is the delay and HeroDur is the full-damage phase. */
void flame_strike_tick(edict_t *ent) {
    uint32_t now = G_Time(), rank = ent->resources, code = ent->class_id;
    float delay = S_SpellNumber(code, ABILITY_NUMBER_CAST, rank);
    float age = (now - ent->spawn_time) / 1000.0f - delay;
    float duration = S_SpellDuration(code, rank, false);
    float limit = S_SpellData(code, rank, 6);

    if (age > duration) { G_DestroyOwnedEffects(ent); G_FreeEdict(ent); return; }
    /* AHfs owns the warning circle and rank-specific impact art. XHfs owns
     * the persistent ember field and its looped sound. Keep the latter tied
     * to this damage thinker so expiry releases both together. */
    if (age >= 0.0f && ent->wait == 0.0f) {
        uint32_t effect_code = G_AbilityEffectCode(code, rank);
        G_SpawnAbilityEffectAtPoint(code, WC3_EFFECT_SPECIAL, rank ? rank - 1 : 0,
                                    &ent->s.origin2, true);
        if (effect_code && effect_code != code)
            (void)S_SpellBeginAreaPresentation(ent, effect_code, rank, &ent->s.origin2);
        ent->wait = 1.0f;
    }
    while (ent->freetime <= now) {
        bool full = (ent->freetime - ent->spawn_time) / 1000.0f - delay <= S_SpellDuration(code, rank, true);
        float damage = S_SpellData(code, rank, full ? 1 : 3), total = 0;
        uint32_t step = (uint32_t)(S_SpellData(code, rank, full ? 2 : 4) * 1000.0f);
        if (!step) {
            fprintf(stderr, "WC3 Flame Strike: damage interval became zero for %.4s\n", (cstring_t)&code);
            G_DestroyOwnedEffects(ent); G_FreeEdict(ent); return;
        }
#define BZ_FLAME_HITS(t) (S_SpellAllowsTarget(code, ent->owner, t) && \
    Vector2_distance(&(t)->s.origin2, &ent->s.origin2) <= ent->collision)
        FILTER_EDICTS(target, BZ_FLAME_HITS(target))
            total += damage * (G_UnitIsStructure(target) ? S_SpellData(code, rank, 5) : 1);
        FILTER_EDICTS(target, BZ_FLAME_HITS(target)) {
            float hit = damage * (G_UnitIsStructure(target) ? S_SpellData(code, rank, 5) : 1);
            if (limit > 0 && total > limit) hit *= limit / total;
            S_SpellDamage(target, ent->owner, (int)hit);
            {
                cstring_t buff = S_SpellBuffId(code, rank);
                if (buff && *buff)
                    G_SpawnAbilityEffectTarget(FS_SLKKey(buff), WC3_EFFECT_TARGET, 0,
                                               target, NULL, true);
            }
        }
#undef BZ_FLAME_HITS
        ent->freetime += step;
        if (full && (ent->freetime - ent->spawn_time) / 1000.0f - delay > S_SpellDuration(code, rank, true))
            ent->freetime = ent->spawn_time + (uint32_t)((delay + S_SpellDuration(code, rank, true) + S_SpellData(code, rank, 4)) * 1000.0f);
    }
}

/* Zero damage intervals cannot schedule a fire patch; reject malformed rows rather than substituting guessed values. */
static bool flame_strike_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t rank = S_SpellLevel(caster, spell->code);
    if (S_SpellData(spell->code, rank, 2) >= .001f && S_SpellData(spell->code, rank, 4) >= .001f) return true;
    fprintf(stderr, "WC3 Flame Strike: invalid damage intervals for %.4s rank %u\n", (cstring_t)&spell->code, rank);
    return false;
}

/* The authored delay and intervals schedule persistent ground damage independently of the caster's next order. */
static void flame_strike_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t rank = S_SpellLevel(caster, spell->code);
    edict_t *ent = G_Spawn();
    ent->owner = caster; ent->class_id = spell->code; ent->resources = rank;
    ent->s.origin2 = st.point; ent->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, rank);
    ent->spawn_time = G_Time();
    ent->freetime = G_Time() + (uint32_t)(S_SpellNumber(spell->code, ABILITY_NUMBER_CAST, rank) * 1000.0f);
    ent->think = flame_strike_tick;
    /* The missile arrives before the cast deadline and starts the warning
     * art. Keep an immediate warning fallback for casters without missile data. */
    if (!flame_strike_launch_projectile(caster, spell->code, &st.point))
        G_SpawnAbilityEffectAtPoint(spell->code, WC3_EFFECT_EFFECT, 0, &st.point, true);
}

BZ_VALIDATED_SPELL_PROC(AbilityFlameStrikeNeutral, flame_strike_validate, flame_strike_execute)
BZ_VALIDATED_SPELL_PROC(AbilityFlameStrike, flame_strike_validate, flame_strike_execute)
