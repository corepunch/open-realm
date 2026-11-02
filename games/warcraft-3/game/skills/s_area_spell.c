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

typedef struct {
    edict_t *caster;
    vec2_t direction;
    float length, width;
} carrionSwarmContext_t;

static bool carrion_swarm_hits(edict_t *target, carrionSwarmContext_t const *ctx) {
    vec2_t offset;
    float along, across;
    if (!target->inuse || target == ctx->caster || !S_SpellIsAliveTarget(target) ||
        !S_SpellIsEnemy(ctx->caster, target) ||
        (G_UnitTargetType(target) != TARG_GROUND && G_UnitTargetType(target) != TARG_AIR)) return false;
    offset = Vector2_sub(&target->s.origin2, &ctx->caster->s.origin2);
    along = Vector2_dot(&offset, &ctx->direction);
    across = offset.x * ctx->direction.y - offset.y * ctx->direction.x;
    /* Area1 is the cone's maximum half-width at its endpoint. The wave opens
     * linearly from the caster and stops at the selected target point. */
    return along >= 0.0f && along <= ctx->length &&
           fabsf(across) <= ctx->width * (along / ctx->length);
}

static void carrion_swarm_damage(edict_t *caster, vec2_t const *point, float width,
                                 uint32_t base_damage, float max_total) {
    vec2_t offset = Vector2_sub(point, &caster->s.origin2);
    float length = Vector2_len(&offset);
    carrionSwarmContext_t ctx;
    float damage = (float)base_damage;
    uint32_t count = 0;
    if (length <= 0.0f) return;
    ctx = (carrionSwarmContext_t){ .caster = caster,
        .direction = Vector2_scale(&offset, 1.0f / length), .length = length, .width = width };
    if (max_total > 0.0f) {
        FILTER_EDICTS(target, carrion_swarm_hits(target, &ctx)) count++;
        if (count && damage * (float)count > max_total)
            damage = MAX(1.0f, max_total / (float)count);
    }
    FILTER_EDICTS(target, carrion_swarm_hits(target, &ctx))
        S_SpellDamage(target, caster, (uint32_t)damage);
}

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

/* Carrion Swarm resolves its damage at the selected point, while this
 * presentation-only missile travels from the caster to that point. */
static void carrion_swarm_spawn_missile(edict_t *caster, uint32_t code, vec2_t const *point) {
    cstring_t art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    cstring_t speed_text = S_SpellString(code, "Missilespeed", 0);
    float speed = speed_text ? (float)atof(speed_text) : 0.0f;
    vec2_t offset = Vector2_sub(point, &caster->s.origin2);
    float distance = Vector2_len(&offset);
    vec2_t direction;
    edict_t *missile;
    channel_t *cast;

#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 omits AUcs MissileArt and Missilespeed; the 1.29 row authors
     * these same retail values. */
    if (!art) art = "Abilities\\Spells\\Undead\\CarrionSwarm\\CarrionSwarmMissile.mdl";
    if (speed <= 0.0f) speed = 1100.0f;
#endif
    if (!art || !*art || distance <= 0.0f || speed <= 0.0f) return;
    direction = Vector2_scale(&offset, 1.0f / distance);
    missile = G_Spawn();
    if (!missile) return;
    missile->class_id = code;
    missile->owner = caster;
    missile->s.player = caster->s.player;
    missile->s.origin = caster->s.origin;
    missile->s.origin2 = caster->s.origin2;
    missile->s.angle = atan2f(direction.y, direction.x);
    missile->s.model = G_RegisterModel(art);
    if (!missile->s.model) { G_FreeEdict(missile); return; }
    G_InheritUnitTeamColor(missile, caster);
    cast = (missile->channel ? missile->channel : (missile->channel=G_AllocChannel()));
    cast->owner_spawn_time = caster->spawn_time;
    cast->origin = *point;
    missile->velocity = speed / 1000.0f;
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    missile->currentmove = &shockwave_visual_move;
    G_StartProjectilePresentation(missile);
}

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
    cast = (missile->channel ? missile->channel : (missile->channel=G_AllocChannel()));
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
static cstring_t starfall_target_art_code(uint32_t code, uint32_t level) {
    cstring_t buff = S_SpellBuffId(code, level);
#if BZ_COMPAT_WORKAROUNDS
    if (!buff && G_AbilityCode(code) == MAKEFOURCC('A','E','s','f')) {
        return "AEsd";
    }
#endif
    return buff;
}

void starfall_think(edict_t *ent) {
    uint32_t now = G_Time();
    edict_t *caster = ent->owner;
    uint32_t level = S_SpellLevel(caster, ent->class_id);
    cstring_t target_art_code = starfall_target_art_code(ent->class_id, level);

    if (!S_SpellChannelActive(ent)) {
        S_SpellEndAreaPresentation(ent);
        S_SpellEndChannel(ent);
        return;
    }
    if (ent->freetime && now < ent->freetime)
        return;
    FILTER_EDICTS(target, target->inuse && target != caster && S_SpellIsAliveTarget(target) &&
                  S_SpellIsEnemy(caster, target) &&
                  (G_UnitTargetType(target) == TARG_GROUND || G_UnitTargetType(target) == TARG_AIR ||
                   G_UnitIsStructure(target)) &&
                  Vector2_distance(&target->s.origin2, &ent->s.origin2) <= ent->collision) {
        S_SpellDamage(target, caster, ent->damage);
        if (target_art_code && strlen(target_art_code) >= 4)
            G_SpawnAbilityEffectTarget(FS_SLKKey(target_art_code), WC3_EFFECT_TARGET, 0,
                                       target, "origin", true);
    }
    if (ent->spawn_time && now >= ent->spawn_time) {
        S_SpellEndAreaPresentation(ent);
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
/* Carrion Swarm damages enemy units in a widening cone from the caster to the
 * selected point. */
BZ_SIMPLE_SPELL_PROC(AbilityCarrionSwarm) {
    uint32_t level = S_SpellLevel(caster, spell->code);

    carrion_swarm_spawn_missile(caster, spell->code, &st.point);
    G_SpawnAbilityEffectAtPoint(spell->code, WC3_EFFECT_SPECIAL, 0, &st.point, true);
    carrion_swarm_damage(caster, &st.point,
        MAX(0.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level)),
        (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1)),
        S_SpellData(spell->code, level, 2)); /* DataB = Max Damage */
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
#define DEATH_AND_DECAY_AREA_EFFECT_COUNT 10

static umove_t death_and_decay_area_effect_move = { "stand", NULL, G_FreeEdict, NULL };

static bool death_and_decay_hits_tree(edict_t const *target, vec2_t const *origin, float radius) {
    return target && target->inuse && target->targtype == TARG_TREE && G_IsDestructable(target) &&
           !target->destructable->dead && !(target && target->invulnerable) &&
           Vector2_distance(&target->s.origin2, origin) <= radius;
}

static uint32_t death_and_decay_random(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float death_and_decay_random_unit(uint32_t *state) {
    return (float)(death_and_decay_random(state) >> 8) * (1.0f / 16777216.0f);
}

static edict_t *death_and_decay_spawn_area_effect(edict_t *owner, uint32_t code,
                                                   uint32_t effect_code, vec2_t const *point) {
    edict_t *effect = G_SpawnOwnedAbilityEffectAtPoint(owner, code, WC3_EFFECT_AREA_EFFECT, 0, point);
    if (!effect) effect = G_SpawnOwnedAbilityEffectAtPoint(owner, code, WC3_EFFECT_EFFECT, 0, point);
    if (!effect && effect_code != code)
        effect = G_SpawnOwnedAbilityEffectAtPoint(owner, effect_code, WC3_EFFECT_AREA_EFFECT, 0, point);
    if (!effect && effect_code != code)
        effect = G_SpawnOwnedAbilityEffectAtPoint(owner, effect_code, WC3_EFFECT_EFFECT, 0, point);
    if (!effect) return NULL;
    effect->s.scale = 1.0f;
    unit_setmove(effect, &death_and_decay_area_effect_move);
    if (effect->animation) effect->s.frame = effect->animation->interval[0];
    return effect;
}

static void death_and_decay_spawn_area_effects(edict_t *thinker, uint32_t code,
                                                uint32_t level, vec2_t const *center,
                                                float radius) {
    uint32_t effect_code = G_AbilityEffectCode(code, level);
    uint32_t seed = (thinker->spawn_time ^ thinker->s.number * 2654435761u ^ G_Time());
    if (!seed) seed = 1;

    FOR_LOOP(i, DEATH_AND_DECAY_AREA_EFFECT_COUNT) {
        float const angle = death_and_decay_random_unit(&seed) * (2.0f * (float)M_PI);
        float const distance = sqrtf(death_and_decay_random_unit(&seed)) * radius;
        vec2_t point = { center->x + cosf(angle) * distance,
                         center->y + sinf(angle) * distance };
        edict_t *effect = death_and_decay_spawn_area_effect(thinker, code, effect_code, &point);
        if (!effect) break;
    }
}

void death_and_decay_think(edict_t *ent) {
    uint32_t now = G_Time();
    edict_t *caster = ent->owner;

    if (!S_SpellChannelActive(ent)) { S_SpellEndAreaPresentation(ent); S_SpellEndChannel(ent); return; }
    if (ent->spawn_time && now >= ent->spawn_time) {
        S_SpellEndAreaPresentation(ent);
        S_SpellEndChannel(ent);
        return;
    }
    if (ent->freetime && now < ent->freetime) return;
    death_and_decay_spawn_area_effects(ent, ent->class_id, S_SpellLevel(caster, ent->class_id),
                                      &ent->s.origin2, ent->collision);
    FILTER_EDICTS(target, target->inuse && S_SpellIsAliveTarget(target) &&
                  S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &ent->s.origin2) <= ent->collision) {
        S_SpellDamage(target, caster, (uint32_t)MAX(1.0f, target->health.max_value * ent->wait));
    }
    FILTER_EDICTS(target, death_and_decay_hits_tree(target, &ent->s.origin2, ent->collision))
        G_DestructableApplyDamage(target, caster, MAX(1.0f, target->health.max_value * ent->wait));
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
    {
        uint32_t effect_code = G_AbilityEffectCode(spell->code, level);
        if (G_AbilityEffectSoundIndex(spell->code, false))
            G_PlayAbilityEffectSound(spell->code, &st.point);
        else if (effect_code != spell->code)
            G_PlayAbilityEffectSound(effect_code, &st.point);
        int loop_sound = G_AbilityEffectSoundIndex(spell->code, true);
        if (!loop_sound && effect_code != spell->code)
            loop_sound = G_AbilityEffectSoundIndex(effect_code, true);
        if (loop_sound) thinker->s.sound = (uint16_t)loop_sound;
    }
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
        if (S_SpellDamage(target, caster, (int)damage) && !M_IsDead(target)) {
            if (target == st.entity)
                G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_EFFECT, 0, target, NULL, true);
            if (buff && strlen(buff) >= 4) {
                heroabilitystatus_t *status = S_SpellApplyTimedStatus(
                    target, buff, rank, S_SpellHeroDuration(spell->code, rank, target));
                if (status) unit_status_enabletargetart(target, status, "origin");
            }
        }
    }
}

void tranquility_think(edict_t *ent) {
    uint32_t now = G_Time();
    edict_t *caster = ent->owner;
    static uint32_t const heal_art_code = MAKEFOURCC('A','E','t','r');

    if (!S_SpellChannelActive(ent)) {
        S_SpellEndAreaPresentation(ent);
        S_SpellEndChannel(ent);
        return;
    }
    if (ent->freetime && now < ent->freetime) return;
    FILTER_EDICTS(target, target->inuse && S_SpellIsAliveTarget(target) &&
                  S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &ent->s.origin2) <= ent->collision) {
        edict_t *effect;
        S_SpellHeal(target, ent->damage);
        effect = G_SpawnAbilityEffectTarget(heal_art_code, WC3_EFFECT_TARGET, 0, target, NULL, true);
        if (!effect)
            G_SpawnAbilityEffectTarget(heal_art_code, WC3_EFFECT_EFFECT, 0, target, NULL, true);
    }
    if (ent->spawn_time && now >= ent->spawn_time) {
        S_SpellEndAreaPresentation(ent);
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
    S_SpellBeginAreaPresentation(thinker, spell->code, level, &thinker->s.origin2);
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
    S_SpellBeginAreaPresentation(thinker, spell->code, level, &thinker->s.origin2);
    thinker->think = starfall_think;
    starfall_think(thinker);
}

/* ANcl has distinct ability-disable and channel-work lifetimes. The ordered
 * cast owns its deadline and saved continuation; DataA replaces follow-through. */
BZ_ABILITY_PROC(CAbilityChannel) {
    uint32_t code=call && call->item ? call->item->code : 0;
    switch(msg) {
    case A_UNIT_TYPE_UPDATE:return ent ? 0 : UNIT_UPDATE_POINTER(channel);
    case A_SPELL_COMMAND_PROFILE: {
        abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(ent,code));
        uint32_t type=wc3_int_bits(wc3_float_bits(data->data[1].number));
        /*4da0d0 fixes the base order from DataF1, with an explicit native
         * channel default for absent/unregistered names. Rank affects DataB. */
        uint32_t order=G_RegisteredOrderId(G_AbilityLevel(code,1)->data[5].text);
        *call->spell_command=(spellCommandProfile_t){
            .order_id=order ? order : G_OrderId("channel"),
            .target_type=type<=3 ? (spellTargetType_t)type : SPELL_TARGET_NONE};
        return true;
    }
    case A_CAST_PROFILE:
        S_DefaultSpellCastProfile(ent,code,call->cast_profile);
        call->cast_profile->backswing=S_SpellData(code,S_SpellLevel(ent,code),1);
        call->cast_profile->intercept_before_work=true;
        return true;
    case A_CAST_BEGIN:
        if(S_SpellData(code,S_SpellLevel(ent,code),5)!=0) {
            S_AcquireExclusiveAbilityDisables(ent,code);
            S_AcquireUnitSeparationSuppression(ent);
        }
        return true;
    case A_EXECUTE:
        if(!ent->channel)ent->channel=G_AllocChannel();
        assert(ent->channel);
        ent->channel->serial++;ent->channel->code=code;
        ent->channel->origin=ent->s.origin2;
        S_UnitAbilityEvent(ent,A_CHANNEL_STATE_CHANGED);
        return true;
    case A_CAST_END:
        if(S_ReleaseExclusiveAbilityDisables(ent,code))
            S_ReleaseUnitSeparationSuppression(ent);
        if(ent->channel && ent->channel->code==code) {
            ent->channel->code=0;
            S_UnitAbilityEvent(ent,A_CHANNEL_STATE_CHANGED);
        }
        return true;
    case A_UNIT_SUSPEND:
    case A_DISABLE:
        S_CancelSpellOwner(ent,code);return true;
    default:return CAbilitySimpleSpell(ent,msg,call);
    }
}
