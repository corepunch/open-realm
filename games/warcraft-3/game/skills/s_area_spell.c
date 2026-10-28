#include "s_skills.h"

#define BLIZZARD_DAMAGE_PHASE 0x80000000u
#define BLIZZARD_LEVEL_MASK   0x7fffffffu
#define BLIZZARD_SHARD_DELAY_MS 800u

static uint32_t blizzard_level(edict_t const *ent) {
    uint32_t const level = ent ? ent->variation & BLIZZARD_LEVEL_MASK : 0;
    return level ? level : 1;
}

typedef struct {
    edict_t *caster;
    uint32_t code;
    vec2_t direction;
    float length, width;
} shockwaveContext_t;

/* Deal ent->damage to every enemy within ent->collision of ent.  maxtotal > 0
 * caps the combined damage of this burst (WC3 "Max Damage" / "Maximum Damage per
 * Wave"): when damage*targets would exceed it, the per-target damage is scaled
 * down so the total lands on the cap. */
static void area_spell_damage(edict_t *ent, float maxtotal) {
    edict_t *caster = ent->owner;
    float radius = ent->collision;
    float damage = (float)ent->damage;
    uint32_t ntargets = 0;

#define AREA_HITS(t) ((t)->inuse && (t) != caster && S_SpellIsAliveTarget(t) && \
                      S_SpellIsEnemy(caster, t) &&                              \
                      (G_UnitTargetType(t) == TARG_GROUND || G_UnitTargetType(t) == TARG_AIR || G_UnitIsStructure(t)) && \
                      Vector2_distance(&(t)->s.origin2, &ent->s.origin2) <= radius)

    if (maxtotal > 0.0f) {
        FILTER_EDICTS(target, AREA_HITS(target)) {
            ntargets++;
        }
        if (ntargets > 0 && damage * (float)ntargets > maxtotal) {
            damage = MAX(1.0f, maxtotal / (float)ntargets);
        }
    }
    FILTER_EDICTS(target, AREA_HITS(target)) {
        S_SpellDamage(target, caster, (uint32_t)damage);
    }
#undef AREA_HITS
}

static bool blizzard_hits(edict_t *ent, edict_t *target) {
    edict_t *caster = ent->owner;
    return target->inuse && target != caster && S_SpellIsAliveTarget(target) &&
           S_SpellAllowsTarget(ent->class_id, caster, target) &&
           (G_UnitTargetType(target) == TARG_GROUND || G_UnitTargetType(target) == TARG_AIR || G_UnitIsStructure(target)) &&
           Vector2_distance(&target->s.origin2, &ent->s.origin2) <= ent->collision;
}

/* Blizzard counts every eligible target against DataF's per-wave cap, then
 * applies DataD only to structures.  Keep this separate from generic area
 * spells because the building multiplier is specific to Blizzard's fields. */
static void blizzard_wave_damage(edict_t *ent) {
    edict_t *caster = ent->owner;
    uint32_t level = blizzard_level(ent), ntargets = 0;
    float damage = (float)ent->damage;
    float maxtotal = ent->velocity;
    float building_scale = S_SpellData(ent->class_id, level, 4);

    FILTER_EDICTS(target, blizzard_hits(ent, target)) ntargets++;
    if (maxtotal > 0.0f && ntargets && damage * (float)ntargets > maxtotal)
        damage = MAX(1.0f, maxtotal / (float)ntargets);

    FILTER_EDICTS(target, blizzard_hits(ent, target)) {
        float amount = damage;
        if (G_UnitIsStructure(target))
            amount *= building_scale;
        if (amount > 0.0f) S_SpellDamage(target, caster, (uint32_t)amount);
    }
}

/* Shard art is presentation only.  Use the authored DataC count and the
 * server RNG so visual placement cannot change which units receive damage. */
static void blizzard_spawn_shards(edict_t *ent) {
    uint32_t level = blizzard_level(ent);
    uint32_t shards = (uint32_t)MAX(0.0f, S_SpellData(ent->class_id, level, 3));
    cstring_t effect_id = G_AbilityLevel(ent->class_id, level)->efctID;
    uint32_t effect_code = effect_id && strlen(effect_id) >= 4 ? FS_SLKKey(effect_id) : ent->class_id;
#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 keeps Blizzard's shard art and sounds under XHbz in
     * HumanAbilityFunc.txt, before AHbz gained the AbilityData EfctID field. */
    if ((!effect_id || strlen(effect_id) < 4) && ent->class_id == MAKEFOURCC('A','H','b','z'))
        effect_code = MAKEFOURCC('X','H','b','z');
#endif

    FOR_LOOP(i, shards) {
        float angle = ((float)rand() / (float)RAND_MAX) * 2.0f * (float)M_PI;
        float distance = ((float)rand() / (float)RAND_MAX) * ent->collision;
        vec2_t point = ent->s.origin2;
        point.x += cosf(angle) * distance;
        point.y += sinf(angle) * distance;
        /* Blizzard's shard EffectArt belongs to its authored EfctID object
         * (XHbz in stock data), not to the casting ability alias itself. */
        G_SpawnAbilityEffectAtPoint(effect_code, WC3_EFFECT_EFFECT, 0, &point, true);
        /* Warsmash emits the EfctID object's Effectsound once per shard.
         * Keep audio presentation on the same authored object as EffectArt. */
        G_PlayAbilityEffectSound(effect_code, &point);
    }
}

void blizzard_think(edict_t *ent) {
    uint32_t now = G_Time();

    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime)
        return;

    /* Warsmash splits each Blizzard wave into presentation and impact: the
     * authored shards appear first, then their gameplay damage lands 0.8s
     * later.  Keep the phase on the already-serialized variation field so a
     * live channel needs no new save-field or callback-table entry. */
    if (!(ent->variation & BLIZZARD_DAMAGE_PHASE)) {
        blizzard_spawn_shards(ent);
        ent->variation |= BLIZZARD_DAMAGE_PHASE;
        ent->freetime = now + BLIZZARD_SHARD_DELAY_MS;
        return;
    }

    blizzard_wave_damage(ent);
    ent->variation &= BLIZZARD_LEVEL_MASK;
    if (ent->resources > 0)
        ent->resources--;
    if (ent->resources == 0) {
        S_SpellEndChannel(ent);
        return;
    }
    ent->freetime = now + (uint32_t)MAX(FRAMETIME, ent->wait * 1000.0f);
}

static bool shockwave_hits(edict_t *target, shockwaveContext_t const *ctx) {
    vec2_t offset;
    float along, across;

    if (!target->inuse || target == ctx->caster || !S_SpellIsAliveTarget(target) ||
        !S_SpellIsEnemy(ctx->caster, target) ||
        !S_SpellAllowsAreaTarget(ctx->code, ctx->caster, target)) return false;
    offset = Vector2_sub(&target->s.origin2, &ctx->caster->s.origin2);
    along = Vector2_dot(&offset, &ctx->direction);
    across = offset.x * ctx->direction.y - offset.y * ctx->direction.x;
    return along >= 0.0f && along <= ctx->length && fabsf(across) <= ctx->width;
}

static umove_t shockwave_visual_move = {
    .animation = "Stand",
    .endfunc = G_FreeEdict
};

/* Shockwave's line damage resolves immediately, while its authored missile
 * travels along that same corridor as a presentation-only projectile. */
static void shockwave_spawn_missile(edict_t *caster, uint32_t code, vec2_t const *direction,
                                   float length, float speed) {
    cstring_t art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    edict_t *missile;
    channel_t *cast;

    if (!art || !*art || length <= 0.0f || speed <= 0.0f) return;
    missile = G_Spawn();
    if (!missile) return;
    missile->class_id = code;
    missile->owner = caster;
    missile->s.player = caster->s.player;
    missile->s.origin = caster->s.origin;
    missile->s.origin2 = caster->s.origin2;
    missile->s.angle = atan2f(direction->y, direction->x);
    missile->s.model = G_RegisterModel(art);
    if (!missile->s.model) { G_FreeEdict(missile); return; }
    G_InheritUnitTeamColor(missile, caster);
    cast = G_AddChannel(missile);
    cast->owner_spawn_time = caster->spawn_time;
    cast->origin = Vector2_mad(&caster->s.origin2, length, direction);
    missile->velocity = speed / 1000.0f;
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    missile->currentmove = &shockwave_visual_move;
    G_StartProjectilePresentation(missile);
}

void rain_of_fire_think(edict_t *ent) {
    uint32_t now = G_Time();

    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    area_spell_damage(ent, 0.0f);
    if (!--ent->resources) {
        S_SpellEndChannel(ent);
        return;
    }
    ent->freetime = now + (uint32_t)(MAX(0.1f, ent->velocity) * 1000.0f);
}

/* Starfall: self-centered periodic area damage.  AbilityData stores the
 * authored damage in DataA, wave interval in DataB, area in Area, and the
 * channel lifetime in Dur. */
void starfall_think(edict_t *ent) {
    uint32_t now = G_Time();

    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime)
        return;
    area_spell_damage(ent, 0.0f);
    if (ent->spawn_time && now >= ent->spawn_time) {
        S_SpellEndChannel(ent);
        return;
    }
    ent->freetime = now + (uint32_t)MAX(1.0f, ent->velocity * 1000.0f);
}

/* Name=Blizzard
 * Ubertip="Calls down an icy storm that damages enemy units in a target area."
 */
/* Blizzard: channeled point-target AoE.  AB_CHANNEL flag causes the unified
 * pipeline to lock the caster via channel_code/cast_origin; spell_run_frame()
 * enforces movement-cancel.  The thinker entity runs the per-wave damage. */
BZ_SIMPLE_SPELL_PROC(AbilityBlizzard) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t waves = (uint32_t)S_SpellData(spell->code, level, 1);
    uint32_t damage = (uint32_t)S_SpellData(spell->code, level, 2);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    float cast_delay = S_SpellNumber(spell->code, ABILITY_NUMBER_CAST, level);
    float wave_interval = cast_delay;
    edict_t *thinker;

#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 AHbz authors its one-second wave interval in Dur and Cast=0.
     * Later data moved that interval to Cast and set Dur=0. Keep the first
     * wave immediate, but use the legacy Dur between subsequent waves. */
    if (spell->code == MAKEFOURCC('A','H','b','z') && wave_interval <= 0.0f) {
        float const legacy_interval = S_SpellDuration(spell->code, level, false);
        if (legacy_interval > 0.0f) wave_interval = legacy_interval;
    }
#endif

    thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = area > 0 ? area : 200.0f;
    thinker->damage = damage ? damage : 1;
    thinker->resources = waves ? waves : 1;
    thinker->variation = level;
    thinker->velocity = S_SpellData(spell->code, level, 6); /* DataF = Max Damage per Wave */
    thinker->wait = wave_interval;
    thinker->think = blizzard_think;
    /* Cast is Blizzard's authored delay before the first shard wave.  A zero
     * delay still begins the shard phase immediately, but damage never lands
     * until the fixed shard-to-impact delay has elapsed. */
    thinker->freetime = G_Time() + (uint32_t)MAX(0.0f, cast_delay * 1000.0f);
    blizzard_think(thinker);
}

/* Name=Carrion Swarm
 * Ubertip="Sends a wave of bats that damages enemy units in a line."
 */
/* Carrion Swarm: instant point-target AoE blast. */
BZ_SIMPLE_SPELL_PROC(AbilityCarrionSwarm) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *blast;

    blast = G_Spawn();
    blast->owner = caster;
    blast->s.origin2 = st.point;
    blast->collision = MAX(96.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level));
    blast->damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    area_spell_damage(blast, S_SpellData(spell->code, level, 2)); /* DataB = Max Damage */
    G_FreeEdict(blast);
}

/* Name=Shockwave
 * Ubertip="A wave of force that ripples outward, causing <AOsh,DataA1> damage to land units in a line."
 */
/* Shockwave uses the authored damage, cap, travel distance, and corridor width
 * rather than treating the line spell as a circular point-target burst. */
void S_ShockwaveExecute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code), ntargets = 0;
    shockwaveContext_t ctx = { .caster = caster, .code = spell->code };
    vec2_t offset = Vector2_sub(&st.point, &caster->s.origin2);
    float distance = Vector2_distance(&caster->s.origin2, &st.point);
    float damage = MAX(1.0f, S_SpellData(spell->code, level, 1));
    float maxtotal = S_SpellData(spell->code, level, 2);
    uint32_t base_code = G_AbilityCode(spell->code);
    cstring_t speed_text = S_SpellString(base_code, "Missilespeed", 0);
    float speed = speed_text && *speed_text ? (float)atof(speed_text) : 1050.0f;

    if (distance <= 0.0f) return;
    ctx.direction = Vector2_scale(&offset, 1.0f / distance);
    ctx.length = MIN(distance, S_SpellData(spell->code, level, 3));
    ctx.width = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    shockwave_spawn_missile(caster, spell->code, &ctx.direction, ctx.length, speed);
    FILTER_EDICTS(target, shockwave_hits(target, &ctx)) ntargets++;
    if (maxtotal > 0.0f && ntargets && damage * ntargets > maxtotal)
        damage = MAX(1.0f, maxtotal / (float)ntargets);
    FILTER_EDICTS(target, shockwave_hits(target, &ctx)) S_SpellDamage(target, caster, (uint32_t)damage);
}

BZ_SIMPLE_SPELL_PROC(AbilityShockwave) {
    S_ShockwaveExecute(caster, st, spell);
}

/* Name=Rain of Fire
 * Ubertip="Calls down waves of fire that damage enemy units in a target area."
 */
BZ_SIMPLE_SPELL_PROC(AbilityRainOfFire) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 2));
    thinker->resources = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    thinker->velocity = MAX(0.1f, S_SpellDuration(spell->code, level, false));
    thinker->think = rain_of_fire_think;
    rain_of_fire_think(thinker);
}

/* Death and Decay deals the authored percentage of each enemy's maximum life
 * on every pulse; unlike Rain of Fire, DataA is not a fixed damage amount. */
void death_and_decay_think(edict_t *ent) {
    uint32_t now = G_Time();
    edict_t *caster = ent->owner;

    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    FILTER_EDICTS(target, target->inuse && S_SpellIsAliveTarget(target) &&
                  S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &ent->s.origin2) <= ent->collision) {
        S_SpellDamage(target, caster, (uint32_t)MAX(1.0f, target->health.max_value * ent->wait));
    }
    if (ent->spawn_time && now >= ent->spawn_time) {
        S_SpellEndChannel(ent);
        return;
    }
    ent->freetime = now + (uint32_t)(MAX(0.1f, ent->velocity) * 1000.0f);
}

/* Name=Death and Decay
 * Ubertip="Damages enemy units in a target area over time."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDeathAndDecay) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->wait = S_SpellData(spell->code, level, 1);
    thinker->velocity = MAX(0.1f, S_SpellData(spell->code, level, 2));
    thinker->spawn_time = G_Time() + (uint32_t)(MAX(0.1f, S_SpellDuration(spell->code, level, true)) * 1000.0f);
    thinker->think = death_and_decay_think;
    death_and_decay_think(thinker);
}

static void area_damage_status_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    float duration = S_SpellDuration(spell->code, level, false);
    uint32_t damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    cstring_t buff = S_SpellBuffId(spell->code, level);

#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 predates Thunder Clap's BuffID column; stock AHtc still applies
     * BHtc with its authored duration and DataC/DataD movement/attack slows. */
    if ((!buff || strlen(buff) < 4) && spell->code == MAKEFOURCC('A','H','t','c')) buff = "BHtc";
#endif

    FILTER_EDICTS(target, target->inuse && target != caster && S_SpellIsAliveTarget(target) &&
                  S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= radius) {
        if (S_SpellDamage(target, caster, damage) && !M_IsDead(target) && buff && strlen(buff) >= 4 && duration > 0.0f)
            S_SpellApplyTimedStatusFromAbility(target, spell->code, level, buff,
                                               S_SpellResistantDuration(spell->code, level, target));
    }
}

/* Name=Thunder Clap
 * Ubertip="Slams the ground, damaging and slowing nearby enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityThunderClap) {
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
    area_damage_status_execute(caster, st, spell);
}
/* Name=Frost Nova
 * Ubertip="Blasts nearby enemy units with frost, damaging and slowing them."
 */
BZ_SIMPLE_SPELL_PROC(AbilityFrostNova) {
    uint32_t rank = S_SpellLevel(caster, spell->code);
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, rank);
    cstring_t buff = S_SpellBuffId(spell->code, rank);
    vec2_t center = st.entity->s.origin2;
    FILTER_EDICTS(target, S_SpellIsEnemy(caster, target) && S_SpellAllowsTarget(spell->code, caster, target) &&
                  Vector2_distance(&target->s.origin2, &center) <= radius) {
        float damage = S_SpellData(spell->code, rank, 1);
        if (target == st.entity) damage += S_SpellData(spell->code, rank, 2);
        if (S_SpellDamage(target, caster, (int)damage) && !M_IsDead(target) && buff && strlen(buff) >= 4)
            S_SpellApplyTimedStatus(target, buff, rank, S_SpellHeroDuration(spell->code, rank, target));
    }
}

void tranquility_think(edict_t *ent) {
    uint32_t now = G_Time();
    edict_t *caster = ent->owner;

    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    FILTER_EDICTS(target, target->inuse && S_SpellIsAliveTarget(target) &&
                  S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &ent->s.origin2) <= ent->collision)
        S_SpellHeal(target, ent->damage);
    if (ent->spawn_time && now >= ent->spawn_time) {
        S_SpellEndChannel(ent);
        return;
    }
    ent->freetime = now + (uint32_t)(MAX(0.1f, ent->velocity) * 1000.0f);
}

/* Name=Tranquility
 * Ubertip="Heals nearby friendly units over time."
 */
BZ_SIMPLE_SPELL_PROC(AbilityTranquility) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->s.origin2 = caster->s.origin2;
    thinker->s.origin.x = caster->s.origin.x;
    thinker->s.origin.y = caster->s.origin.y;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    thinker->velocity = MAX(0.1f, S_SpellData(spell->code, level, 2));
    thinker->spawn_time = G_Time() + (uint32_t)(MAX(0.1f, S_SpellDuration(spell->code, level, true)) * 1000.0f);
    thinker->think = tranquility_think;
    tranquility_think(thinker);
}

/* Name=Starfall
 * Ubertip="Calls down falling stars that damage nearby enemy units over time."
 */
BZ_SIMPLE_SPELL_PROC(AbilityStarfall) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->s.origin2 = caster->s.origin2;
    thinker->s.origin.x = caster->s.origin.x;
    thinker->s.origin.y = caster->s.origin.y;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    thinker->velocity = MAX(1.0f, S_SpellData(spell->code, level, 2));
    thinker->spawn_time = G_Time() + (uint32_t)(MAX(1.0f, S_SpellDuration(spell->code, level, true)) * 1000.0f);
    thinker->think = starfall_think;
    starfall_think(thinker);
}

/* CAbilityChannel remains a non-spell ability for ad-hoc testing. */
BZ_COMMAND_PROC(AbilityChannel) {
    UI_AddCancelButton(clent);
    S_SpellCursorSplat(clent, 200.0f);
}
