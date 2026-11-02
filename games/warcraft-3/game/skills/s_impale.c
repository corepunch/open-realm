#include "s_skills.h"

#define IMPALE_HIT_ID MAKEFOURCC('I', 'm', 'p', 'H')

/* Impale's configured SpecialArt is a one-shot *miss* spike, while BUim
 * TargetArt is the one-shot *hit* spike. The art report establishes their
 * identities but not retail emission spacing. Keep this provisional spacing
 * local to presentation; the simulation sweep below remains authoritative. */
#define IMPALE_SPIKE_SPACING 64.0f
#define IMPALE_SPIKES_PER_TICK_LIMIT 32u

/* DataD is the airborne phase, independently timed from Dur/HeroDur stun.
 * The exact retail elevation curve is not observable from the available
 * game data. This deliberately conservative sinusoid is presentation only. */
#define IMPALE_LIFT_PEAK 120.0f

bool S_ImpaleIsAirborne(edict_t const *unit) {
    impaleLift_t const *lift = unit ? G_GetImpaleLift(unit) : NULL;
    return lift && unit->inuse && !M_IsDead((edict_t *)unit) &&
        lift->end_ms > lift->start_ms && G_Time() < lift->end_ms;
}

bool S_ImpaleAirborneVulnerable(edict_t const *unit) {
    impaleLift_t const *lift = unit ? G_GetImpaleLift(unit) : NULL;
    return lift && S_ImpaleIsAirborne(unit) && lift->vulnerable != 0;
}

float S_ImpaleLiftOffset(edict_t const *unit) {
    impaleLift_t const *lift = unit ? G_GetImpaleLift(unit) : NULL;
    uint32_t now;
    float t;
    if (!lift || !unit || !unit->inuse || M_IsDead(unit)) return 0.0f;
    now = G_Time();
    if (now >= lift->end_ms || lift->end_ms <= lift->start_ms) return 0.0f;
    if (now <= lift->start_ms) return MAX(0.0f, lift->start_offset);
    t = (float)(now - lift->start_ms) / (float)(lift->end_ms - lift->start_ms);
    /* The initial offset prevents a second hit from snapping an already
     * lifted unit to its ordinary flight height before starting a new rise. */
    return MAX(0.0f, lift->start_offset * (1.0f - t) +
                lift->peak_height * sinf((float)M_PI * t));
}

static void impale_begin_lift(edict_t *target, float air_seconds, bool vulnerable) {
    impaleLift_t *lift;
    uint32_t duration_ms;
    float previous;
    if (!target || !target->inuse || M_IsDead(target) ||
        !isfinite(air_seconds) || air_seconds <= 0.0f) return;
    /* Bound milliseconds and preserve a nonzero lifetime for short custom
     * values; never allow the end deadline to wrap past UINT32_MAX. */
    duration_ms = (uint32_t)MIN(air_seconds * 1000.0f, (float)(UINT32_MAX / 2));
    duration_ms = MAX(1u, duration_ms);
    previous = S_ImpaleLiftOffset(target);
    lift = G_GetImpaleLift(target);
    if (!lift) lift = G_AddImpaleLift(target);
    if (!lift) return;
    lift->start_ms = G_Time();
    lift->end_ms = G_Time() + MIN(duration_ms, UINT32_MAX - G_Time());
    lift->start_offset = previous;
    lift->peak_height = IMPALE_LIFT_PEAK;
    /* Each hit owns the new airborne state; preserve the old offset, not
     * the old vulnerability mode, when a second wave lands in mid-air. */
    lift->vulnerable = vulnerable;
    M_CheckGround(target);
    gi.LinkEntity(target);
}

void S_ImpaleLiftUpdate(edict_t *unit) {
    impaleLift_t *lift = unit ? G_GetImpaleLift(unit) : NULL;
    if (!lift) return;
    if (M_IsDead(unit) || G_Time() >= lift->end_ms ||
        lift->end_ms <= lift->start_ms) {
        G_FreeImpaleLift(unit);
    }
    M_CheckGround(unit);
    gi.LinkEntity(unit);
}


/* Target identities are attached to the current wave instead of to the victim:
 * a unit can be hit once by each distinct cast, including simultaneous waves.
 * Edicts are reused; target spawn identity protects those reuse boundaries. */
static bool impale_already_hit(edict_t const *wave, edict_t const *target) {
    FILTER_EDICTS(mark, mark->class_id == IMPALE_HIT_ID && mark->owner == wave &&
                  mark->damage == wave->spawn_time && mark->goalentity == target &&
                  mark->resources == target->spawn_time)
        return true;
    return false;
}

static void impale_end_wave(edict_t *wave) {
    /* Release markers before recycling the wave slot. */
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *mark = &globals.edicts[i];
        if (mark->inuse && mark->class_id == IMPALE_HIT_ID && mark->owner == wave &&
            mark->damage == wave->spawn_time) G_FreeEdict(mark);
    }
    G_FreeEdict(wave);
}

/* Resolve spike presentation from the already recorded authoritative hits.
 * Samples overlapping an impact use BUim.TargetArt; empty samples use the
 * ability's SpecialArt. Art capacity never changes gameplay hit processing. */
static bool impale_sample_hit(edict_t const *wave, impaleWave_t const *state, float distance) {
    FOR_LOOP(i, globals.num_edicts) {
        edict_t const *mark = &globals.edicts[i];
        vec2_t offset;
        float along;
        if (!mark->inuse || mark->class_id != IMPALE_HIT_ID || mark->owner != wave ||
            mark->damage != wave->spawn_time) continue;
        offset = Vector2_sub(&mark->s.origin2, &state->origin);
        along = Vector2_dot(&offset, &state->direction);
        if (fabsf(along - distance) < IMPALE_SPIKE_SPACING * 0.5f) return true;
    }
    return false;
}

static void impale_present_sweep(edict_t *wave, impaleWave_t const *state,
                                 float start, float end_distance) {
    float sample;
    if (!G_AbilityEffectArt(state->ability, WC3_EFFECT_SPECIAL, 0)) return;
    sample = (floorf(start / IMPALE_SPIKE_SPACING) + 1.0f) * IMPALE_SPIKE_SPACING;
    if (state->distance < IMPALE_SPIKE_SPACING && end_distance >= state->distance)
        sample = state->distance;
    FOR_LOOP(i, IMPALE_SPIKES_PER_TICK_LIMIT) {
        edict_t *effect;
        vec2_t point;
        if (!isfinite(sample) || sample > end_distance || sample > state->distance) break;
        if (!impale_sample_hit(wave, state, sample)) {
            point = Vector2_mad(&state->origin, sample, &state->direction);
            effect = G_SpawnAbilityEffectAtPoint(state->ability, WC3_EFFECT_SPECIAL, 0,
                                                 &point, true);
            if (effect) effect->s.angle = atan2f(state->direction.y, state->direction.x);
        }
        if (sample + IMPALE_SPIKE_SPACING <= sample) break;
        sample += IMPALE_SPIKE_SPACING;
    }
}

static bool impale_hit(edict_t *wave, impaleWave_t const *state, edict_t *target, bool visited_cached) {
    edict_t *mark;
    edict_t *caster = wave->owner;
    cstring_t buff;
    heroabilitystatus_t *status;
    uint32_t target_spawn_time = target->spawn_time;
    float stun_duration;
    if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(caster, target) ||
        !S_SpellAllowsAreaTarget(state->ability, caster, target) ||
        G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE) ||
        unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE) ||
        (!visited_cached && impale_already_hit(wave, target))) return false;

    /* Record the hit before damage. This prevents repeated hits when damage
     * causes immediate unit events, and protects status refresh callbacks. */
    mark = G_Spawn();
    if (!mark) return false;
    mark->class_id = IMPALE_HIT_ID;
    mark->svflags |= SVF_NOCLIENT;
    mark->owner = wave;
    mark->goalentity = target;
    mark->damage = wave->spawn_time;
    mark->resources = target->spawn_time;
    mark->s.origin2 = target->s.origin2; /* immutable presentation impact */
    buff = S_SpellBuffId(state->ability, state->level);
    if (!buff || strlen(buff) < 4) buff = "BUim"; /* RoC/custom missing BuffID */
    /* The tall hit spike is authored on BUim, not on AUim. Anchor its one-shot
     * Birth on the ground at impact; otherwise the spike follows a lifting
     * victim. Spawn before damage so a lethal hit still has its visual. */
    {
        uint32_t buff_code = FS_SLKKey(buff);
        edict_t *effect = G_SpawnAbilityEffectAtPoint(buff_code, WC3_EFFECT_TARGET, 0,
                                                      &target->s.origin2, true);
        if (effect) effect->s.angle = atan2f(state->direction.y, state->direction.x);
    }
    S_SpellDamage(target, caster, (int)state->damage);
    if (!target->inuse || target->spawn_time != target_spawn_time || M_IsDead(target)) return true;
    stun_duration = S_SpellHeroDuration(state->ability, state->level, target);
    /* The shared timed-status API uses zero duration for permanent status.
     * An authored zero stun must never create an indefinite BUim lock. */
    if (isfinite(stun_duration) && stun_duration > 0.0f) {
        status = S_SpellApplyTimedTargetStatus(target, state->ability, state->level,
            buff, stun_duration);
        if (status) status->state_mask |= WC3_STATUS_STATE_STUNNED;
    }
    {
        uint32_t const buff_code = FS_SLKKey(buff);
        (void)G_SpawnAbilityEffectTarget(buff_code, WC3_EFFECT_EFFECT, 0, target, "sprite", true);
    }
    /* The authored airborne motion is independent of the stun duration and
     * survives a failed status allocation (or a custom zero-duration stun). */
    impale_begin_lift(target, S_SpellData(state->ability, state->level, 4),
                      S_SpellData(state->ability, state->level, 6) > 0.0f);
    return true;
}

typedef struct {
    uint32_t index, spawn_time;
    float along;
} impaleCandidate_t;

static int impale_candidate_compare(void const *a, void const *b) {
    impaleCandidate_t const *left = a, *right = b;
    if (left->along < right->along) return -1;
    if (left->along > right->along) return 1;
    return (left->index > right->index) - (left->index < right->index);
}

void impale_wave_think(edict_t *wave) {
    impaleWave_t *state = G_GetImpaleWave(wave);
    edict_t *caster = wave->owner;
    float elapsed, end, start;
    if (!state || !caster || !caster->inuse || M_IsDead(caster) ||
        caster->spawn_time != state->caster_spawn_time) {
        impale_end_wave(wave);
        return;
    }
    /* Pre-1.32.6 Impale was interruptible: moving during its travelling
     * spike phase terminated the remaining sweep. DataE permits modern
     * or custom waves to keep travelling after the caster changes position.
     * The caster's cast-time/animation interruption is owned by the shared
     * spell pipeline, not this already-launched wave. */
    if (!state->uninterruptible &&
        Vector2_distance(&caster->s.origin2, &state->origin) > 0.5f) {
        impale_end_wave(wave);
        return;
    }
    elapsed = (float)(G_Time() - state->start_ms) / 1000.0f;
    end = state->duration > 0.0f
        ? state->distance * MIN(1.0f, MAX(0.0f, elapsed / state->duration))
        : state->distance;
    start = state->previous_distance;
    if (end < start) end = start;
    if (end > start) {
        float const step = end - start;
        uint32_t const count = globals.num_edicts;
        impaleCandidate_t *candidates = malloc((size_t)count * sizeof(*candidates));
        uint8_t *visited = calloc(count, sizeof(*visited));
        uint32_t eligible = 0;
        if (candidates && visited) {
            FOR_LOOP(i, count) {
                edict_t const *mark = &globals.edicts[i];
                if (mark->inuse && mark->class_id == IMPALE_HIT_ID &&
                    mark->owner == wave && mark->damage == wave->spawn_time &&
                    mark->goalentity && mark->goalentity >= globals.edicts &&
                    mark->goalentity < globals.edicts + count &&
                    mark->resources == mark->goalentity->spawn_time)
                    visited[mark->goalentity - globals.edicts] = 1;
            }
            FOR_LOOP(i, count) {
                edict_t *target = &globals.edicts[i];
                vec2_t delta;
                float along, across;
                if (visited[i] || !S_SpellIsAliveTarget(target) ||
                    !S_SpellIsEnemy(caster, target) ||
                    !S_SpellAllowsAreaTarget(state->ability, caster, target) ||
                    G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE) ||
                    unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE)) continue;
                delta = Vector2_sub(&target->s.origin2, &state->origin);
                along = Vector2_dot(&delta, &state->direction) - start;
                if (along < 0.0f || along > step) continue;
                across = delta.x * state->direction.y - delta.y * state->direction.x;
                if (fabsf(across) > state->half_width + MAX(0.0f, target->collision)) continue;
                candidates[eligible++] = (impaleCandidate_t){ i, target->spawn_time, along };
            }
            qsort(candidates, eligible, sizeof(*candidates), impale_candidate_compare);
            FOR_LOOP(i, eligible) {
                edict_t *target = &globals.edicts[candidates[i].index];
                if (!target->inuse || target->spawn_time != candidates[i].spawn_time) continue;
                /* Previous impacts can remove or move a candidate. Recheck
                 * its finite swept segment, not only its entity identity. */
                vec2_t const delta = Vector2_sub(&target->s.origin2, &state->origin);
                float const along = Vector2_dot(&delta, &state->direction) - start;
                float const across = delta.x * state->direction.y - delta.y * state->direction.x;
                if (along < 0.0f || along > step ||
                    fabsf(across) > state->half_width + MAX(0.0f, target->collision)) continue;
                impale_hit(wave, state, target, true);
            }
        } else {
            /* Allocation failure must not suppress paid spell damage. The
             * legacy nearest-first search remains the emergency fallback. */
            uint32_t previous_index = 0;
            float previous_along = 0.0f;
            bool have_previous = false;
            for (;;) {
                edict_t *best = NULL;
                uint32_t best_index = 0;
                float best_along = 0.0f;
                FOR_LOOP(i, count) {
                    edict_t *target = &globals.edicts[i];
                    vec2_t delta;
                    float along, across;
                    if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(caster, target) ||
                        !S_SpellAllowsAreaTarget(state->ability, caster, target) ||
                        G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE) ||
                        unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE) ||
                        impale_already_hit(wave, target)) continue;
                    delta = Vector2_sub(&target->s.origin2, &state->origin);
                    along = Vector2_dot(&delta, &state->direction) - start;
                    if (along < 0.0f || along > step ||
                        (have_previous && (along < previous_along ||
                         (along == previous_along && i <= previous_index)))) continue;
                    across = delta.x * state->direction.y - delta.y * state->direction.x;
                    if (fabsf(across) > state->half_width + MAX(0.0f, target->collision)) continue;
                    if (!best || along < best_along ||
                        (along == best_along && i < best_index)) {
                        best = target;
                        best_index = i;
                        best_along = along;
                    }
                }
                if (!best) break;
                if (!impale_hit(wave, state, best, false)) break;
                previous_along = best_along;
                previous_index = best_index;
                have_previous = true;
            }
        }
        free(candidates);
        free(visited);
    }
    if (end > start) impale_present_sweep(wave, state, start, end);
    state->previous_distance = end;
    wave->s.origin2 = Vector2_mad(&state->origin, end, &state->direction);
    if (elapsed >= state->duration || end >= state->distance)
        impale_end_wave(wave);
}

/* The timed BUim stun and DataD lift begin independently at wave impact;
 * neither persists on the wave once the wave finishes travelling. */
static bool impale_validate(edict_t *caster, spellTarget_t target, abilityitem_t const *spell) {
    uint32_t level;
    if (!caster || !spell || target.type != SPELL_TARGET_POINT) return false;
    level = S_SpellLevel(caster, spell->code);
    return Vector2_distance(&caster->s.origin2, &target.point) > 0.0f &&
           S_SpellData(spell->code, level, 1) > 0.0f &&
           S_SpellData(spell->code, level, 2) > 0.0f;
}

BZ_ABILITY_PROC(CAbilityImpale) {
    /* The point-target order path queries custom approach distance with a
     * NULL caster. Impale uses its authored Rng unchanged, so accept the
     * query without entering the generic spell handler. */
    if (msg == A_SPELL_APPROACH_RANGE) return true;
    spellTarget_t target = call && call->target ? *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    abilityitem_t const *spell = call ? call->item : NULL;
    impaleWave_t *state;
    edict_t *wave;
    vec2_t offset;
    float direction_len;
    uint32_t level;
    if (msg == A_VALIDATE) return impale_validate(ent, target, spell);
    if (msg != A_EXECUTE) return CAbilitySimpleSpell(ent, msg, call);
    if (!impale_validate(ent, target, spell)) return false;
    level = S_SpellLevel(ent, spell->code);
    wave = G_Spawn();
    if (!wave) return false;
    state = G_AddImpaleWave(wave);
    if (!state) { G_FreeEdict(wave); return false; }
    wave->class_id = spell->code;
    wave->owner = ent;
    wave->s.player = ent->s.player;
    wave->s.origin2 = ent->s.origin2;
    wave->svflags |= SVF_NOCLIENT;
    state->origin = ent->s.origin2;
    offset = Vector2_sub(&target.point, &state->origin);
    direction_len = Vector2_distance(&target.point, &state->origin);
    state->direction = Vector2_scale(&offset, 1.0f / direction_len);
    state->distance = S_SpellData(spell->code, level, 1);
    state->duration = S_SpellData(spell->code, level, 2);
    state->half_width = MAX(0.0f, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level) * 0.5f);
    state->damage = S_SpellData(spell->code, level, 3);
    state->start_ms = G_Time();
    state->caster_spawn_time = ent->spawn_time;
    state->ability = spell->code;
    state->level = level;
    state->uninterruptible = S_SpellData(spell->code, level, 5) > 0.0f;
    wave->think = impale_wave_think;
    /* The caster model owns its authored sound events. */
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, ent, NULL, true);
    return true;
}
