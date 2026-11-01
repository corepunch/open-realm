#include "s_skills.h"

/* ANst and Rexxar's Arsp are the same channelled projectile spell with
 * different authored level data. DataA..E: beasts/sec, collision radius,
 * blast damage, blast radius, and post-collision damage delay. The authored
 * Area is the width of the spawn line AND the distance a beast travels; the
 * point-target Range only limits where the player may start the stampede.
 * Source: TFT AbilityData/NeutralAbilityFunc and the WC3 ability-editor
 * behaviour notes. Exact retail random distribution remains unverified. */
#define STAMPEDE_MAX_SPAWNS_PER_TICK 4
#define STAMPEDE_DEFAULT_MISSILE_SPEED 500.0f

static uint32_t stampede_random(uint32_t *seed) {
    uint32_t n = *seed ? *seed : 0x6d2b79f5u;
    n ^= n << 13;
    n ^= n >> 17;
    n ^= n << 5;
    return *seed = n;
}

static bool stampede_can_hit(edict_t *caster, uint32_t code, edict_t *target) {
    if (!caster || !target || target == caster || !S_SpellIsAliveTarget(target)) return false;
    if (G_UnitTargetType(target) == TARG_AIR) return false;
    return S_SpellAllowsAreaTarget(code, caster, target);
}

/* A destructable is not a unit and cannot be sent through the normal unit
 * target predicate (which expects UnitBalance). The authored debris token is
 * required; tree targets are deliberately not silently added. */
static bool stampede_can_hit_debris(uint32_t code, uint32_t rank, edict_t *target) {
    cstring_t targets = G_AbilityLevel(code, rank)->targs;
    return target && target->inuse && target->targtype == TARG_DEBRIS &&
        targets && S_SpellTargetHasToken(targets, "debris", NULL) &&
        G_IsDestructable(target) && !G_GetDestructable(target)->dead;
}

/* Damage is resolved once per beast, after DataE, at its snapshotted impact
 * location. The caster identity is validated by the standard channel record;
 * a reused entity slot must never receive attribution or hit another player. */
void stampede_blast_think(edict_t *beast) {
    edict_t *caster;
    vec2_t point;
    if (G_Time() < beast->freetime) return;
    caster = S_SpellChannelOwner(beast);
    if (!caster) { G_FreeEdict(beast); return; }
    point = beast->s.origin2;
    G_SpawnAbilityEffectAtPoint(beast->class_id, WC3_EFFECT_SPECIAL, 0, &point, true);
    FILTER_EDICTS(target, stampede_can_hit(caster, beast->class_id, target) &&
                  Vector2_distance(&point, &target->s.origin2) <= beast->wait)
        S_SpellDamage(target, caster, (int)beast->damage);
    FILTER_EDICTS(target, stampede_can_hit_debris(beast->class_id, beast->resources, target) &&
                  Vector2_distance(&point, &target->s.origin2) <= beast->wait)
        G_DestructableApplyDamage(target, caster, (float)beast->damage);
    G_FreeEdict(beast);
}

static void stampede_queue_blast(edict_t *beast, vec2_t const *point) {
    uint32_t const delay = beast->variation; /* DataE milliseconds, stored at launch */
    beast->s.origin2 = *point;
    beast->s.origin.x = point->x;
    beast->s.origin.y = point->y;
    beast->s.origin.z = CM_GetHeightAtPoint(point->x, point->y);
    beast->movetype = MOVETYPE_NONE;
    beast->prethink = NULL;
    beast->currentmove = NULL;
    beast->s.renderfx |= RF_HIDDEN;
    beast->freetime = G_Time() + delay;
    beast->think = stampede_blast_think; /* zero-delay impacts run in this frame's normal think step */
}

/* Sweep the entire frame segment so even a fast lizard cannot tunnel through
 * a narrow target. Pick the first contact, not the first edict in the pool. */
void stampede_beast_prethink(edict_t *beast) {
    edict_t *caster = S_SpellChannelOwner(beast);
    channel_t *flight = G_GetChannel(beast);
    vec2_t from = beast->s.origin2, direction;
    float length, best = FLT_MAX;
    bool hit = false;
    if (!caster || !flight) {
        beast->prethink = NULL;
        beast->movetype = MOVETYPE_NONE;
        beast->think = G_FreeEdict;
        return;
    }
    direction = Vector2_sub(&flight->origin, &from);
    length = Vector2_len(&direction);
    if (length <= 0.001f) return;
    direction = Vector2_scale(&direction, 1.0f / length);
    length = MIN(length, beast->velocity * FRAMETIME);
    FILTER_EDICTS(target, stampede_can_hit(caster, beast->class_id, target) ||
                  stampede_can_hit_debris(beast->class_id, beast->resources, target)) {
        vec2_t offset = Vector2_sub(&target->s.origin2, &from);
        float along = MAX(0.0f, MIN(length, Vector2_dot(&offset, &direction)));
        vec2_t closest = Vector2_mad(&from, along, &direction);
        float radius = beast->collision + MAX(0.0f, target->collision);
        if (Vector2_distance(&target->s.origin2, &closest) <= radius && along < best) {
            best = along;
            hit = true;
        }
    }
    if (hit) {
        vec2_t impact = Vector2_mad(&from, best, &direction);
        stampede_queue_blast(beast, &impact);
    } else {
        /* The ordinary fixed-point missile physics drives motion and model
         * frame playback. Keep the lizard at terrain height during travel. */
        beast->s.origin.z = CM_GetHeightAtPoint(from.x, from.y);
    }
}

/* Without an enemy contact the lizard simply reaches the end of its authored
 * Area travel distance and vanishes, rather than exploding at every endpoint. */
static void stampede_beast_end(edict_t *beast) { G_FreeEdict(beast); }
static umove_t stampede_beast_move = { "stand", NULL, stampede_beast_end };

static void stampede_spawn_beast(edict_t *emitter, edict_t *caster) {
    uint32_t const rank = emitter->resources;
    uint32_t const code = emitter->class_id;
    cstring_t art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    cstring_t speed_text = S_SpellString(code, "Missilespeed", 0);
    float speed = speed_text ? (float)atof(speed_text) : 0.0f;
    float const area = MAX(0.0f, emitter->collision);
    float const lateral = ((float)stampede_random(&emitter->variation) / (float)UINT32_MAX - 0.5f) * area;
    vec2_t forward = { cosf(emitter->s.angle), sinf(emitter->s.angle) };
    vec2_t side = { -forward.y, forward.x };
    vec2_t start = Vector2_mad(&emitter->s.origin2, lateral, &side);
    vec2_t end = Vector2_mad(&start, area, &forward);
    edict_t *beast;
    channel_t *flight;
    if (!art || !*art || area <= 0.0f) return;
    if (!(speed > 0.0f)) {
        if (G_AbilityCode(code) != code) {
            speed_text = S_SpellString(G_AbilityCode(code), "Missilespeed", 0);
            speed = speed_text ? (float)atof(speed_text) : 0.0f;
        }
        if (!(speed > 0.0f)) speed = STAMPEDE_DEFAULT_MISSILE_SPEED;
    }
    beast = S_SpellIdentityThinker(caster, code, NULL);
    if (!beast) return;
    beast->s.player = caster->s.player;
    beast->s.model = G_RegisterModel(art);
    if (!beast->s.model) { G_FreeEdict(beast); return; }
    beast->s.origin2 = start;
    beast->s.origin = (vec3_t){ start.x, start.y, CM_GetHeightAtPoint(start.x, start.y) };
    beast->s.angle = emitter->s.angle;
    G_InheritUnitTeamColor(beast, caster);
    beast->velocity = speed / 1000.0f;
    beast->collision = MAX(0.0f, S_SpellData(code, rank, 2)); /* DataB */
    beast->damage = (uint32_t)MAX(0.0f, S_SpellData(code, rank, 3)); /* DataC */
    beast->wait = MAX(0.0f, S_SpellData(code, rank, 4)); /* DataD */
    beast->variation = (uint32_t)(MAX(0.0f, S_SpellData(code, rank, 5)) * 1000.0f); /* DataE */
    beast->resources = rank;
    flight = G_GetChannel(beast);
    flight->origin = end;
    beast->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    beast->movetype = MOVETYPE_FLYMISSILE;
    beast->prethink = stampede_beast_prethink;
    beast->currentmove = &stampede_beast_move;
    G_StartProjectilePresentation(beast);
}

void stampede_channel_think(edict_t *emitter) {
    uint32_t const now = G_Time();
    uint32_t count = 0;
    edict_t *caster = S_SpellChannelOwner(emitter);
    if (!caster || !S_SpellChannelActive(emitter) || now >= emitter->spawn_time) {
        G_DestroyOwnedEffects(emitter);
        S_SpellEndChannel(emitter);
        return;
    }
    if (emitter->wait <= 0.0f) { S_SpellEndChannel(emitter); return; }
    while (now >= emitter->freetime && emitter->freetime < emitter->spawn_time &&
           count++ < STAMPEDE_MAX_SPAWNS_PER_TICK) {
        stampede_spawn_beast(emitter, caster);
        emitter->freetime += (uint32_t)emitter->wait;
    }
}

/* The spell pipeline has already committed mana, cooldown and the channel.
 * Preserve the cast point (relative to the caster) as the forward direction;
 * the actual lizard spawn line is centred on the clicked point. */
BZ_SIMPLE_SPELL_PROC(AbilityStampede) {
    uint32_t const rank = S_SpellLevel(caster, spell->code);
    float const rate = S_SpellData(spell->code, rank, 1);
    float const duration = S_SpellDuration(spell->code, rank, false);
    vec2_t facing = Vector2_sub(&st.point, &caster->s.origin2);
    edict_t *emitter;
    edict_t *sound;
    int loop;
    if (!(rate > 0.0f) || !(duration > 0.0f)) return;
    emitter = S_SpellChannelThinker(caster, spell->code);
    if (!emitter) return;
    emitter->s.origin2 = st.point;
    emitter->s.angle = Vector2_len(&facing) > 0.001f ? atan2f(facing.y, facing.x) : caster->s.angle;
    emitter->resources = rank;
    emitter->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, rank);
    emitter->wait = MAX(1.0f, 1000.0f / rate); /* milliseconds between beasts */
    emitter->freetime = G_Time();
    emitter->spawn_time = G_Time() + (uint32_t)(duration * 1000.0f);
    emitter->variation = (uint32_t)(caster->s.number * 2654435761u) ^ G_Time() ^
                         (uint32_t)st.point.x ^ ((uint32_t)st.point.y << 16);
    emitter->think = stampede_channel_think;
    G_PlayAbilityEffectSound(spell->code, &caster->s.origin2);
    loop = G_AbilityEffectSoundIndex(spell->code, true);
    if (loop) {
        sound = G_Spawn();
        if (sound) {
            sound->owner = emitter;
            sound->s.flags |= EF_NOT_SELECTABLE;
            sound->s.origin2 = st.point;
            sound->s.origin = (vec3_t){ st.point.x, st.point.y, CM_GetHeightAtPoint(st.point.x, st.point.y) };
            sound->s.sound = (uint16_t)loop;
        }
    }
    stampede_channel_think(emitter);
}
