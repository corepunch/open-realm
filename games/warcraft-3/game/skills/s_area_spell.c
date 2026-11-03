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

#define ID_CARRION_WAVE_HIT MAKEFOURCC('C','W','h','t')
static uint32_t carrion_wave_identity;
static float carrion_clamp(float value, float low, float high) { return MIN(high, MAX(low, value)); }

static bool carrion_wave_visited(edict_t *wave, edict_t *target) {
    carrionWave_t *state = G_GetCarrionWave(wave);
    if (!state) return false;
    FILTER_EDICTS(marker, marker->class_id == ID_CARRION_WAVE_HIT &&
                  marker->owner == wave && marker->goalentity == target &&
                  marker->resources == target->spawn_time &&
                  marker->damage == wave->spawn_time && marker->variation == state->identity)
        return true;
    return false;
}

static bool carrion_wave_mark_visited(edict_t *wave, edict_t *target) {
    edict_t *marker = G_Spawn();
    carrionWave_t *wave_state = G_GetCarrionWave(wave);
    if (!marker) return false;
    if (!wave_state) { G_FreeEdict(marker); return false; }
    marker->class_id = ID_CARRION_WAVE_HIT;
    marker->svflags |= SVF_NOCLIENT;
    marker->owner = wave;
    marker->damage = wave->spawn_time;
    marker->variation = wave_state->identity;
    marker->goalentity = target;
    marker->resources = target->spawn_time;
    return true;
}

static void carrion_wave_finish(edict_t *wave) {
    carrionWave_t *state = G_GetCarrionWave(wave);
    uint32_t i;
    for (i = 0; i < globals.num_edicts; i++) {
        edict_t *marker = &globals.edicts[i];
        if (!marker->inuse || marker->class_id != ID_CARRION_WAVE_HIT || marker->owner != wave) continue;
        if (marker->damage == wave->spawn_time && state && marker->variation == state->identity)
            G_FreeEdict(marker);
    }
    G_FreeEdict(wave);
}

void carrion_wave_think(edict_t *wave);

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

/* Return the distance along *this frame's finite sweep*. A target outside
 * either end cannot be damaged merely because it lies close to the infinite
 * line through the projectile. The authored width expands with distance. */
static bool carrion_wave_candidate(edict_t *wave, edict_t *caster, carrionWave_t *state,
                                   edict_t *target, vec2_t const *start, float step,
                                   float *out_along, bool visited_cached) {
    vec2_t offset;
    float along, across, width;

    if (!S_SpellIsAliveTarget(target) || target == caster ||
        !S_SpellAllowsAreaTarget(state->ability, caster, target) ||
        !S_SpellIsEnemy(caster, target) || G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE) ||
        (!visited_cached && carrion_wave_visited(wave, target))) return false;

    offset = Vector2_sub(&target->s.origin2, start);
    along = Vector2_dot(&offset, &state->direction);
    /* Test the bounded swept interval before the lateral corridor. A wide
     * wave must not hit units behind its current position or past its end. */
    if (along < 0.0f || along > step) return false;
    across = offset.x * state->direction.y - offset.y * state->direction.x;
    width = state->initial_width + (state->final_width - state->initial_width) *
        carrion_clamp((state->travelled + along) / MAX(0.001f, state->travel_distance), 0.0f, 1.0f);
    if (fabsf(across) > width + MAX(0.0f, target->collision)) return false;
    *out_along = along;
    return true;
}

static void carrion_wave_hit(edict_t *wave, edict_t *caster, carrionWave_t *state,
                             edict_t *target) {
    float damage;
    int applied;
    if (state->capped && state->remaining_damage <= 0.0f) return;
    damage = state->capped ? MIN(state->damage_per_target, state->remaining_damage) : state->damage_per_target;
    applied = (int)damage;
    if (applied <= 0) return;
    if (!carrion_wave_mark_visited(wave, target)) return;
    if (S_SpellDamage(target, caster, applied) && state->capped)
        state->remaining_damage = MAX(0.0f, state->remaining_damage - (float)applied);
}

typedef struct {
    uint32_t index;
    uint32_t spawn_time;
    float along;
} carrionCandidate_t;

static int carrion_candidate_compare(void const *a, void const *b) {
    carrionCandidate_t const *left = a, *right = b;
    if (left->along < right->along) return -1;
    if (left->along > right->along) return 1;
    return (left->index > right->index) - (left->index < right->index);
}

void carrion_wave_think(edict_t *wave) {
    carrionWave_t *state = G_GetCarrionWave(wave);
    edict_t *caster;
    vec2_t start;
    float step;
    uint32_t count, eligible = 0;
    carrionCandidate_t *candidates;
    uint8_t *visited;
    if (!state || !(caster = wave->owner) || !caster->inuse || caster->spawn_time != state->caster_spawn_time) {
        carrion_wave_finish(wave); return;
    }
    if (state->travelled >= state->travel_distance || state->speed <= 0.0f) {
        carrion_wave_finish(wave); return;
    }
    start = state->previous_position;
    step = MIN(Vector2_distance(&wave->s.origin2, &start), state->travel_distance - state->travelled);
    if (step <= 0.001f) return;
    count = globals.num_edicts;
    candidates = malloc((size_t)count * sizeof(*candidates));
    visited = calloc(count, sizeof(*visited));
    if (candidates && visited) {
        /* Reconstruct saved per-wave hit identities in a single pass. Markers
         * allocated during impact cannot affect this frame's candidate set. */
        FOR_LOOP(i, count) {
            edict_t const *mark = &globals.edicts[i];
            if (mark->inuse && mark->class_id == ID_CARRION_WAVE_HIT &&
                mark->owner == wave && mark->damage == wave->spawn_time &&
                mark->variation == state->identity && mark->goalentity &&
                mark->goalentity >= globals.edicts && mark->goalentity < globals.edicts + count &&
                mark->resources == mark->goalentity->spawn_time)
                visited[mark->goalentity - globals.edicts] = 1;
        }
        FOR_LOOP(i, count) {
            edict_t *target = &globals.edicts[i];
            float along;
            if (visited[i] || !carrion_wave_candidate(wave, caster, state, target,
                                                       &start, step, &along, true)) continue;
            candidates[eligible++] = (carrionCandidate_t){ i, target->spawn_time, along };
        }
        qsort(candidates, eligible, sizeof(*candidates), carrion_candidate_compare);
        FOR_LOOP(i, eligible) {
            edict_t *target = &globals.edicts[candidates[i].index];
            float along;
            if (state->capped && state->remaining_damage <= 0.0f) break;
            if (!target->inuse || target->spawn_time != candidates[i].spawn_time ||
                !carrion_wave_candidate(wave, caster, state, target, &start, step, &along, true)) continue;
            carrion_wave_hit(wave, caster, state, target);
        }
    } else {
        /* Preserve spell delivery on allocation failure. */
        uint32_t previous_index = 0;
        float previous_along = 0.0f;
        bool have_previous = false;
        for (;;) {
            edict_t *nearest = NULL;
            uint32_t nearest_index = 0;
            float nearest_along = 0.0f;
            if (state->capped && state->remaining_damage <= 0.0f) break;
            FOR_LOOP(i, count) {
                edict_t *target = &globals.edicts[i];
                float along;
                if (!carrion_wave_candidate(wave, caster, state, target, &start, step, &along, false)) continue;
                if (have_previous && (along < previous_along ||
                    (along == previous_along && i <= previous_index))) continue;
                if (!nearest || along < nearest_along ||
                    (along == nearest_along && i < nearest_index)) {
                    nearest = target;
                    nearest_along = along;
                    nearest_index = i;
                }
            }
            if (!nearest) break;
            carrion_wave_hit(wave, caster, state, nearest);
            previous_along = nearest_along;
            previous_index = nearest_index;
            have_previous = true;
        }
    }
    free(candidates);
    free(visited);
    state->travelled += step;
    state->previous_position = wave->s.origin2;
    wave->think = carrion_wave_think;
    if (state->travelled >= state->travel_distance) carrion_wave_finish(wave);
}

/* The shared physics projectile path keeps Crushing Wave presentation moving
 * and interpolated like other Warcraft missiles. Land exactly at the authored
 * DataC endpoint on the final physics step; carrion_wave_think then processes
 * the final swept segment before retiring the projectile. */
static void carrion_wave_arrive(edict_t *wave) {
    channel_t *cast = G_GetChannel(wave);
    if (!cast) return;
    wave->s.origin2 = cast->origin;
    wave->s.origin.x = cast->origin.x;
    wave->s.origin.y = cast->origin.y;
    wave->s.origin.z = CM_GetHeightAtPoint(cast->origin.x, cast->origin.y);
    gi.LinkEntity(wave);
}

static umove_t carrion_wave_visual_move = {
    .animation = "Stand",
    .endfunc = carrion_wave_arrive
};

static umove_t shockwave_visual_move = {
    .animation = "Stand",
    .endfunc = G_FreeEdict
};

/* The selected point establishes direction; authored DataC controls travel. */
static void carrion_swarm_spawn_missile(edict_t *caster, uint32_t code, uint32_t level,
                                       vec2_t const *point) {
    cstring_t art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    cstring_t speed_text = S_SpellString(code, "Missilespeed", 0);
    float speed = speed_text ? (float)atof(speed_text) : 0.0f;
    vec2_t offset = Vector2_sub(point, &caster->s.origin2);
    float distance = Vector2_len(&offset);
    float travel = MAX(0.0f, S_SpellData(code, level, 3));
    float initial_width = MAX(0.0f, S_SpellNumber(code, ABILITY_NUMBER_AREA, level));
    float final_width = MAX(0.0f, S_SpellData(code, level, 4));
    vec2_t direction;
    edict_t *missile;
    carrionWave_t *state;
    channel_t *cast;
    vec3_t endpoint;
    float horizontal_distance;
    float spatial_distance;

#if BZ_COMPAT_WORKAROUNDS
    /* RoC 1.0 omits AUcs MissileArt and Missilespeed; the 1.29 row authors
     * these same retail values. */
    if ((!art || !*art) && code == MAKEFOURCC('A','U','c','s'))
        art = "Abilities\\Spells\\Undead\\CarrionSwarm\\CarrionSwarmMissile.mdl";
    if (speed <= 0.0f && code == MAKEFOURCC('A','U','c','s')) speed = 1100.0f;
#endif
    if (distance <= 0.0f || travel <= 0.0f) return;
    /* The damage wave is authoritative gameplay. Missing/invalid visual data
     * may hide its model, but must not cancel the paid spell. Use the retail
     * Carrion Swarm travel rate when custom missile speed is unavailable. */
    if (!isfinite(speed) || speed <= 0.0f) speed = 1100.0f;
    direction = Vector2_scale(&offset, 1.0f / distance);
    missile = G_Spawn();
    if (!missile) return;
    missile->class_id = code;
    missile->owner = caster;
    missile->s.player = caster->s.player;
    missile->s.origin = caster->s.origin;
    missile->s.origin2 = caster->s.origin2;
    missile->s.angle = atan2f(direction.y, direction.x);
    /* A zero model is an invisible wave, not a gameplay failure. */
    missile->s.model = (art && *art) ? G_RegisterModel(art) : 0;
    /* The authored Crushing Wave missile needs about 3x its default size in
     * game. Scale proportionally from stock DataD=300 so customized aliases
     * keep the same visual relationship to their final wave area. */
    if (code == MAKEFOURCC('A','C','c','v') || code == MAKEFOURCC('A','C','c','2') ||
        code == MAKEFOURCC('A','C','c','3'))
        missile->s.scale = MAX(1.0f, final_width / 100.0f);
    G_InheritUnitTeamColor(missile, caster);
    state = G_AddCarrionWave(missile);
    if (!state) { G_FreeEdict(missile); return; }
    state->caster_spawn_time = caster->spawn_time;
    if (++carrion_wave_identity == 0) ++carrion_wave_identity;
    state->identity = carrion_wave_identity;
    state->ability = code;
    state->level = level;
    state->direction = direction;
    state->previous_position = caster->s.origin2;
    state->travel_distance = travel;
    state->speed = speed;
    state->initial_width = initial_width;
    state->final_width = final_width;
    state->damage_per_target = MAX(0.0f, S_SpellData(code, level, 1));
    state->remaining_damage = MAX(0.0f, S_SpellData(code, level, 2));
    state->capped = state->remaining_damage > 0.0f;
    cast = G_AddChannel(missile);
    if (!cast) { G_FreeEdict(missile); return; }
    cast->owner_spawn_time = caster->spawn_time;
    cast->origin = Vector2_mad(&caster->s.origin2, travel, &direction);
    endpoint = MAKE(vec3_t, cast->origin.x, cast->origin.y,
                    CM_GetHeightAtPoint(cast->origin.x, cast->origin.y));
    horizontal_distance = Vector2_distance(&caster->s.origin2, &cast->origin);
    spatial_distance = Vector3_distance(&caster->s.origin, &endpoint);
    /* The shared missile mover travels in 3D toward the support surface. Scale
     * its velocity so authored Missilespeed remains the horizontal ground
     * speed even when caster and destination terrain elevations differ. */
    missile->velocity = speed / 1000.0f *
        (horizontal_distance > 0.0f ? spatial_distance / horizontal_distance : 1.0f);
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    missile->currentmove = &carrion_wave_visual_move;
    missile->think = carrion_wave_think;
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
    cast = G_AddChannel(missile);
    cast->owner_spawn_time = caster->spawn_time;
    cast->origin = Vector2_mad(&caster->s.origin2, length, direction);
    missile->velocity = speed / 1000.0f;
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    missile->currentmove = &shockwave_visual_move;
    G_StartProjectilePresentation(missile);
}

/* Rain of Fire has a separate three-second burn for *each landed wave*.
 * These small save-safe thinkers do not belong to the caster's channel. A
 * canceled cast stops future waves, but completed hits continue burning.
 * Identity storage prevents slot reuse from redirecting burn damage. */
#define RAIN_FIRE_BURN_TICK_MS 1000u // milliseconds; one-second cadence for each landed Rain of Fire burn
#define RAIN_FIRE_BURN_TICKS   3u // ticks; three pulses preserve the authored three-second burn duration

void rain_of_fire_burn_think(edict_t *burn) {
    edict_t *target = S_SpellChannelTarget(burn);
    edict_t *source = S_SpellChannelOwner(burn);
    uint32_t const now = G_Time();
    if (!target || !S_SpellIsAliveTarget(target) || !burn->resources) {
        G_FreeEdict(burn);
        return;
    }
    while (burn->resources && now >= burn->freetime) {
        /* Advance before damage: death callbacks can retire this target. */
        burn->resources--;
        burn->freetime += RAIN_FIRE_BURN_TICK_MS;
        if (burn->damage > 0) S_SpellDamage(target, source, (int)burn->damage);
        if (!burn->inuse) return;
        if (!S_SpellIsAliveTarget(target) ||
            target->spawn_time != G_GetChannel(burn)->target_spawn_time) break;
    }
    if (!burn->resources || !S_SpellIsAliveTarget(target)) G_FreeEdict(burn);
}

static bool rain_of_fire_hits(edict_t const *wave, edict_t *target) {
    cstring_t mask;
    bool structure;
    uint32_t type;
    uint32_t const rank = MAX(1u, wave->variation);
    if (!S_SpellIsAliveTarget(target) ||
        (G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE) ||
         unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE)) ||
        S_UnitIsCycloned(target) || S_UnitSpellImmune(target) ||
        Vector2_distance(&target->s.origin2, &wave->s.origin2) > wave->collision) return false;
    structure = G_UnitIsStructure(target);
    type = G_UnitTargetType(target);
    /* Stock Rain of Fire deliberately deals friendly fire even if the ability
     * row names enemies. Preserve authored unit-type restrictions, but do not
     * route this special-case through the normal hostile-area relationship
     * filter. Caster, allies, enemies and neutrals can all be hit. */
    mask = G_AbilityLevel(wave->class_id, rank)->targs;
    if (mask && (S_SpellTargetHasToken(mask, "air", NULL) ||
                 S_SpellTargetHasToken(mask, "ground", NULL) ||
                 S_SpellTargetHasToken(mask, "structure", NULL))) {
        if (!(S_SpellTargetHasToken(mask, "air", NULL) && type == TARG_AIR) &&
            !(S_SpellTargetHasToken(mask, "ground", NULL) && type == TARG_GROUND) &&
            !(S_SpellTargetHasToken(mask, "structure", NULL) && structure)) return false;
    }
    if (mask && S_SpellTargetHasToken(mask, "organic", NULL) && target->targtype == TARG_MECHANICAL)
        return false;
    if (mask && S_SpellTargetHasToken(mask, "mechanical", NULL) && target->targtype != TARG_MECHANICAL)
        return false;
    return true;
}

static void rain_of_fire_spawn_burn(edict_t *wave, edict_t *target, uint32_t rank) {
    float const dps = S_SpellData(wave->class_id, rank, 5);
    edict_t *burn;
    if (dps < 1.0f || !S_SpellIsAliveTarget(target)) return;
    burn = S_SpellIdentityThinker(wave->owner, wave->class_id, target);
    if (!burn) return;
    burn->svflags |= SVF_NOCLIENT;
    burn->damage = (uint32_t)dps;
    burn->resources = RAIN_FIRE_BURN_TICKS;
    burn->freetime = G_Time() + RAIN_FIRE_BURN_TICK_MS;
    burn->think = rain_of_fire_burn_think;
}

/* Rain of Fire's XErf EffectArt is an impact with a finite Birth sequence.
 * DataC is the number of visible shards per wave; presentation must not
 * consume the simulation RNG or determine which targets take damage. A
 * local integer sequence gives stable, save-friendly spatial distribution.
 * Clamp malformed custom map counts to avoid exhausting the edict pool. */
#define RAIN_FIRE_MAX_VISUAL_SHARDS 64u

static uint32_t rain_of_fire_visual_random(uint32_t *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}

static void rain_of_fire_wave_presentation(edict_t *wave) {
    uint32_t const rank = MAX(1u, wave->variation);
    uint32_t const effect_code = G_AbilityEffectCode(wave->class_id, rank);
    uint32_t const art_code = G_AbilityEffectArt(wave->class_id, WC3_EFFECT_EFFECT, 0)
        ? wave->class_id : effect_code;
    float const authored = S_SpellData(wave->class_id, rank, 3);
    uint32_t const shards = !isfinite(authored) || authored <= 0.0f ? 0 :
        (uint32_t)MIN(authored, (float)RAIN_FIRE_MAX_VISUAL_SHARDS);
    uint32_t seed = wave->spawn_time ^ ((uint32_t)wave->s.number * 2654435761u) ^
        (wave->resources * 2246822519u);
    cstring_t art = G_AbilityEffectArt(art_code, WC3_EFFECT_EFFECT, 0);

    /* RainOfFireWave belongs to the effect object, not to ANrf's caster.
     * One wave sound is preferable to replaying a full sample for every
     * simultaneous shard. Ability-specific overrides retain precedence. */
    if (G_AbilityEffectSoundIndex(wave->class_id, false))
        G_PlayAbilityEffectSound(wave->class_id, &wave->s.origin2);
    else if (effect_code != wave->class_id)
        G_PlayAbilityEffectSound(effect_code, &wave->s.origin2);

    if (!art || !*art || wave->collision <= 0.0f) return;
    FOR_LOOP(i, shards) {
        float const angle = (float)((double)rain_of_fire_visual_random(&seed) / 4294967296.0) *
            (2.0f * (float)M_PI);
        float const radius = sqrtf((float)((double)rain_of_fire_visual_random(&seed) / 4294967296.0)) *
            wave->collision;
        vec2_t point = wave->s.origin2;
        point.x += cosf(angle) * radius;
        point.y += sinf(angle) * radius;
        /* A one-shot Birth should finish even if the channel is cancelled.
         * Never give the impact entity the channel thinker as its owner. */
        G_SpawnAbilityEffectAtPoint(art_code, WC3_EFFECT_EFFECT, 0, &point, true);
    }
}

static void rain_of_fire_wave_damage(edict_t *wave) {
    uint32_t const rank = MAX(1u, wave->variation);
    float amount = (float)wave->damage;
    float const limit = wave->velocity;
    float const building_multiplier = MAX(0.0f, S_SpellData(wave->class_id, rank, 4));
    uint32_t count = 0;

    FILTER_EDICTS(target, rain_of_fire_hits(wave, target)) count++;
    if (!count) return;
    /* DataF caps the pre-building-reduction damage across this wave. Stock
     * caps take effect above five targets; do not round each hit *up* and
     * exceed the cap after integer damage conversion. */
    if (limit > 0.0f && amount * (float)count > limit)
        amount = limit / (float)count;
    FILTER_EDICTS(target, rain_of_fire_hits(wave, target)) {
        float const scaled = amount * (G_UnitIsStructure(target) ? building_multiplier : 1.0f);
        if (scaled >= 1.0f && S_SpellDamage(target, wave->owner, (int)scaled) &&
            !M_IsDead(target) && !G_UnitIsStructure(target))
            rain_of_fire_spawn_burn(wave, target, rank);
    }
}

void rain_of_fire_think(edict_t *ent) {
    uint32_t const now = G_Time();
    uint32_t const interval = (uint32_t)MAX(1.0f, ent->wait * 1000.0f);
    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    /* The final wave still occupies one interval of the authored channel.
     * Absolute deadlines keep frame jitter from slowing subsequent waves. */
    while (ent->freetime <= now) {
        if (!ent->resources) { S_SpellEndChannel(ent); return; }
        /* Authoritative damage/burn thinkers take allocation precedence over
         * optional model entities if the shared edict pool is tight. */
        rain_of_fire_wave_damage(ent);
        rain_of_fire_wave_presentation(ent);
        ent->resources--;
        ent->freetime += interval;
        if (!ent->resources) break;
        if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    }
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

    carrion_swarm_spawn_missile(caster, spell->code, level, &st.point);
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
    uint32_t const level = S_SpellLevel(caster, spell->code);
    uint32_t const waves = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    float const duration = S_SpellDuration(spell->code, level, false);
    float const cast_interval = S_SpellNumber(spell->code, ABILITY_NUMBER_CAST, level);
    /* The Pit Lord's three-second Dur spans every wave, not the interval
     * between waves. Creep aliases may omit Dur and author a Cast interval. */
    float const interval = duration > 0.0f ? duration / (float)waves :
        (cast_interval > 0.0f ? cast_interval : 1.0f);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code);
    if (!thinker) return;
    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->s.origin.z = CM_GetHeightAtPoint(st.point.x, st.point.y);
    thinker->s.sound = (uint16_t)G_AbilityEffectSoundIndex(spell->code, true);
    if (!thinker->s.sound) {
        uint32_t const effect_code = G_AbilityEffectCode(spell->code, level);
        if (effect_code != spell->code)
            thinker->s.sound = (uint16_t)G_AbilityEffectSoundIndex(effect_code, true);
    }
    thinker->collision = MAX(0.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level));
    thinker->damage = (uint32_t)MAX(0.0f, S_SpellData(spell->code, level, 2));
    thinker->resources = waves;
    thinker->variation = level;
    thinker->velocity = MAX(0.0f, S_SpellData(spell->code, level, 6));
    thinker->wait = MAX(0.001f, interval);
    thinker->freetime = G_Time();
    thinker->think = rain_of_fire_think;
    rain_of_fire_think(thinker);
}

/* Death and Decay deals the authored percentage of each enemy's maximum life
 * on every pulse; unlike Rain of Fire, DataA is not a fixed damage amount. */
#define DEATH_AND_DECAY_AREA_EFFECT_COUNT 10

static umove_t death_and_decay_area_effect_move = { "stand", NULL, G_FreeEdict, NULL };

static bool death_and_decay_hits_tree(edict_t const *target, vec2_t const *origin, float radius) {
    return target && target->inuse && target->targtype == TARG_TREE && G_IsDestructable(target) &&
           !G_GetDestructable(target)->dead && !G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE) &&
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
#if BZ_COMPAT_WORKAROUNDS
    /* RoC AbilityData omits BuffID; stock Frost Nova uses Bfro. */
    if ((!buff || strlen(buff) < 4) && G_AbilityCode(spell->code) == MAKEFOURCC('A','U','f','n')) buff = "Bfro";
#endif
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

/* CAbilityChannel remains a non-spell ability for ad-hoc testing. */
BZ_COMMAND_PROC(AbilityChannel) {
    UI_AddCancelButton(clent);
    S_SpellCursorSplat(clent, 200.0f);
}
