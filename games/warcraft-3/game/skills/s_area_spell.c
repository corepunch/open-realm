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
        if (target->targtype == TARG_STRUCTURE || G_UnitIsBuilding(target->class_id))
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
        !S_SpellIsEnemy(ctx->caster, target)) return false;
    offset = Vector2_sub(&target->s.origin2, &ctx->caster->s.origin2);
    along = Vector2_dot(&offset, &ctx->direction);
    across = offset.x * ctx->direction.y - offset.y * ctx->direction.x;
    return along >= 0.0f && along <= ctx->length && fabsf(across) <= ctx->width;
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
    edict_t *thinker;

    thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = area > 0 ? area : 200.0f;
    thinker->damage = damage ? damage : 1;
    thinker->resources = waves ? waves : 1;
    thinker->variation = level;
    thinker->velocity = S_SpellData(spell->code, level, 6); /* DataF = Max Damage per Wave */
    thinker->wait = S_SpellNumber(spell->code, ABILITY_NUMBER_CAST, level);
    thinker->think = blizzard_think;
    /* Cast is Blizzard's authored delay before the first shard wave.  A zero
     * delay still begins the shard phase immediately, but damage never lands
     * until the fixed shard-to-impact delay has elapsed. */
    thinker->freetime = G_Time() + (uint32_t)MAX(0.0f, thinker->wait * 1000.0f);
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
BZ_SIMPLE_SPELL_PROC(AbilityShockwave) {
    uint32_t level = S_SpellLevel(caster, spell->code), ntargets = 0;
    shockwaveContext_t ctx = { .caster = caster };
    vec2_t offset = Vector2_sub(&st.point, &caster->s.origin2);
    float distance = Vector2_distance(&caster->s.origin2, &st.point);
    float damage = MAX(1.0f, S_SpellData(spell->code, level, 1));
    float maxtotal = S_SpellData(spell->code, level, 2);

    if (distance <= 0.0f) return;
    ctx.direction = Vector2_scale(&offset, 1.0f / distance);
    ctx.length = MIN(distance, S_SpellData(spell->code, level, 3));
    ctx.width = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FILTER_EDICTS(target, shockwave_hits(target, &ctx)) ntargets++;
    if (maxtotal > 0.0f && ntargets && damage * ntargets > maxtotal)
        damage = MAX(1.0f, maxtotal / (float)ntargets);
    FILTER_EDICTS(target, shockwave_hits(target, &ctx)) S_SpellDamage(target, caster, (uint32_t)damage);
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
    cstring_t buff = G_AbilityLevel(spell->code, level)->buffID;

    FILTER_EDICTS(target, target->inuse && target != caster && S_SpellIsAliveTarget(target) &&
                  S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= radius) {
        if (S_SpellDamage(target, caster, damage) && !M_IsDead(target) && buff && strlen(buff) >= 4 && duration > 0.0f)
            unit_addtimedstatus(target, buff, level, duration);
    }
}

/* Name=Thunder Clap
 * Ubertip="Slams the ground, damaging and slowing nearby enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityThunderClap) { area_damage_status_execute(caster, st, spell); }
/* Name=Frost Nova
 * Ubertip="Blasts nearby enemy units with frost, damaging and slowing them."
 */
BZ_SIMPLE_SPELL_PROC(AbilityFrostNova) {
    uint32_t rank = S_SpellLevel(caster, spell->code);
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, rank);
    cstring_t buff = G_AbilityLevel(spell->code, rank)->buffID;
    vec2_t center = st.entity->s.origin2;
    FILTER_EDICTS(target, S_SpellIsEnemy(caster, target) && S_SpellAllowsTarget(spell->code, caster, target) &&
                  Vector2_distance(&target->s.origin2, &center) <= radius) {
        float damage = S_SpellData(spell->code, rank, 1);
        if (target == st.entity) damage += S_SpellData(spell->code, rank, 2);
        if (S_SpellDamage(target, caster, (int)damage) && !M_IsDead(target) && buff && strlen(buff) >= 4)
            unit_addtimedstatus(target, buff, rank, S_SpellDuration(spell->code, rank, G_UnitIsHero(target)));
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
