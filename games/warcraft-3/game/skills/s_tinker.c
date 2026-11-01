#include "s_skills.h"

/* Goblin Tinker: the ANeg ability's DataC..F author the chains below.
 * The paired rawcodes in those fields are SLK strings, not scalar ids; keep
 * the stock pairs together here and use the concrete variants' authored
 * numeric data for every spell, stat and attack effect. */
#define TINKER_ENGINEERING MAKEFOURCC('A','N','e','g')
#define TINKER_NORMAL MAKEFOURCC('N','t','i','n')
#define TINKER_ROBOT MAKEFOURCC('N','r','o','b')
#define TINKER_ROBO_FORM MAKEFOURCC('A','N','r','g')
#define TINKER_DEMOLISH MAKEFOURCC('A','N','d','e')
#define TINKER_ROCKET_PULSES_LIMIT 8u
#define TINKER_ROCKET_MODELS_PER_PULSE 24u

static uint32_t const tinker_variants[4][4] = {
    { MAKEFOURCC('A','N','s','y'), MAKEFOURCC('A','N','c','s'), MAKEFOURCC('A','N','r','g'), MAKEFOURCC('A','N','d','e') },
    { MAKEFOURCC('A','N','s','1'), MAKEFOURCC('A','N','c','1'), MAKEFOURCC('A','N','g','1'), MAKEFOURCC('A','N','d','1') },
    { MAKEFOURCC('A','N','s','2'), MAKEFOURCC('A','N','c','2'), MAKEFOURCC('A','N','g','2'), MAKEFOURCC('A','N','d','2') },
    { MAKEFOURCC('A','N','s','3'), MAKEFOURCC('A','N','c','3'), MAKEFOURCC('A','N','g','3'), MAKEFOURCC('A','N','d','3') }
};

uint32_t S_TinkerEngineeringRank(edict_t const *unit) {
    uint32_t rank;
    if (!unit || !unit->unit_state) return 0;
    /* Unlearned hero skills can occur in the candidate ability list. Only
     * learned hero ranks activate Engineering's numerical modifiers. */
    rank = 0;
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *ability = &unit->unit_state->ranks[i];
        if (ability->level && G_AbilityCode(ability->code) == TINKER_ENGINEERING)
            rank = MAX(rank, ability->level);
    }
    return MIN(rank, 3u);
}

uint32_t S_TinkerVariant(edict_t const *unit, uint32_t code) {
    uint32_t const base = G_AbilityCode(code);
    uint32_t const rank = S_TinkerEngineeringRank(unit);
    /* Directly-authored variant rawcodes (e.g. a custom-map ANc2) remain
     * authoritative rather than being silently replaced by the hero rank. */
    if (code != base) return code;
    FOR_LOOP(i, 4) {
        if (base == tinker_variants[0][i]) {
            uint32_t const variant = tinker_variants[rank][i];
            return G_AbilityData(variant)->id ? variant : code;
        }
    }
    return code;
}

float S_TinkerEngineeringMoveBonus(edict_t const *unit) {
    uint32_t const rank = S_TinkerEngineeringRank(unit);
    return rank ? MAX(0.0f, S_SpellData(TINKER_ENGINEERING, rank, 1)) : 0.0f;
}

float S_TinkerEngineeringDamageBonus(edict_t const *unit) {
    uint32_t const rank = S_TinkerEngineeringRank(unit);
    return rank ? S_SpellData(TINKER_ENGINEERING, rank, 2) : 0.0f;
}

static float tinker_form_stat(uint32_t engineering_rank, uint32_t data) {
    uint32_t const variant = tinker_variants[MIN(engineering_rank, 3u)][2];
    return G_AbilityData(variant)->id ? S_SpellData(variant, 1, data) : 0.0f;
}

float S_TinkerRoboArmor(edict_t const *unit) {
    return unit && unit->class_id == TINKER_ROBOT ?
        tinker_form_stat(S_TinkerEngineeringRank(unit), 6) : 0.0f;
}

float S_TinkerDemolishMultiplier(edict_t const *attacker, edict_t const *target) {
    uint32_t code;
    if (!attacker || !target || attacker->class_id != TINKER_ROBOT ||
        !G_UnitIsStructure(target) || !G_UnitAbilityLevel(attacker, TINKER_DEMOLISH)) return 1.0f;
    code = S_TinkerVariant(attacker, TINKER_DEMOLISH);
    return MAX(1.0f, S_SpellData(code, 1, 2));
}

/* Ranks live in hero.ranks and survive save/load and morphing unchanged. Do
 * not append per-rank bonuses to temporary ledgers: rebuilding Hero stats
 * from current ability data prevents duplicate stacking after level changes. */
void S_TinkerOnEngineeringLearned(edict_t *unit, uint32_t previous_rank) {
    uint32_t const rank = S_TinkerEngineeringRank(unit);
    if (!unit || !unit->unit_state || rank == previous_rank) return;
    if (unit->class_id == TINKER_ROBOT) {
        float const before = tinker_form_stat(previous_rank, 5);
        float const after = tinker_form_stat(rank, 5);
        unit->unit_state->hero.str = (uint32_t)MAX(0.0f,
            (float)unit->unit_state->hero.str + after - before);
    }
    G_RecomputeHeroStats(unit);
    G_InvalidateUnitInfoPanel(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
    { gameClient_t *owner = G_GetPlayerClientByNumber(unit->s.player);
      if (owner && owner->ps.number == unit->s.player) G_InvalidateCommands(owner); }
}

static void tinker_robo_morph_finished(edict_t *unit) {
    if (!unit) return;
    G_AddUnitAnimationProperties(unit, "alternate", unit->class_id == TINKER_ROBOT);
    unit_stand(unit);
}
static umove_t tinker_robo_morph = {
    "morph", NULL, tinker_robo_morph_finished, CAbilityRoboGoblin
};
static umove_t tinker_normal_morph = {
    "morph alternate", NULL, tinker_robo_morph_finished, CAbilityRoboGoblin
};

BZ_ABILITY_PROC(CAbilityRoboGoblin) {
    switch (msg) {
    case A_ORDER:
        if (!ent || !call || !call->order) return false;
        if (!strcmp(call->order, "robogoblin") && ent->class_id != TINKER_NORMAL) return false;
        if (!strcmp(call->order, "unrobogoblin") && ent->class_id != TINKER_ROBOT) return false;
        if (strcmp(call->order, "robogoblin") && strcmp(call->order, "unrobogoblin")) return false;
        return S_CastNoTargetSpell(ent, TINKER_ROBO_FORM);
    case A_TOGGLE_ON: return ent && ent->class_id == TINKER_ROBOT;
    case A_VALIDATE:
        return ent && (ent->class_id == TINKER_NORMAL || ent->class_id == TINKER_ROBOT) &&
            G_UnitUI(ent->class_id == TINKER_NORMAL ? TINKER_ROBOT : TINKER_NORMAL)->modelFile != NULL;
    case A_EXECUTE: {
        uint32_t const target = ent->class_id == TINKER_NORMAL ? TINKER_ROBOT : TINKER_NORMAL;
        bool const entering = target == TINKER_ROBOT;
        float const str = tinker_form_stat(S_TinkerEngineeringRank(ent), 5);
        if (!G_TransformUnitType(ent, target)) return false;
        ent->unit_state->hero.str = (uint32_t)MAX(0.0f,
            (float)ent->unit_state->hero.str + (entering ? str : -str));
        G_RecomputeHeroStats(ent);
        ent->goalentity = NULL;
        if (ent->orders) ent->orders->resource_goal = NULL;
        move_reset_progress(ent);
        /* The forward Morph clip is untagged; reverse Morph Alternate uses
         * the alternate tag. Keep the selected transition clip when switching
         * to the destination form's normal idle property. */
        G_AddUnitAnimationProperties(ent, "alternate", !entering);
        unit_setmove(ent, entering ? &tinker_robo_morph : &tinker_normal_morph);
        { animation_t const *animation = ent->animation;
          G_AddUnitAnimationProperties(ent, "alternate", entering);
          if (animation) {
              ent->animation = animation;
              ent->s.frame = animation->interval[0];
          } else tinker_robo_morph_finished(ent);
        }
        G_PlayAbilityEffectSound(TINKER_ROBO_FORM, &ent->s.origin2);
        return true;
    }
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* Cluster Rockets' DataC is *visual missiles*, not damage multiplicity.
 * DataA damage is applied every DataB seconds, bounded by DataD's shared
 * interval budget. DataF is the total damage-pulse lifetime (four pulses
 * in stock TFT 1.29). Presentation rockets have no damage callbacks. */
static uint32_t tinker_random(uint32_t *seed) {
    uint32_t x = *seed ? *seed : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *seed = x;
}

static void tinker_rocket_impact(edict_t *rocket) {
    vec2_t const point = rocket->s.origin2;
    G_SpawnAbilityEffectAtPoint(rocket->class_id, WC3_EFFECT_SPECIAL, 0, &point, true);
    if (!G_StartProjectileDeathPresentation(rocket)) G_FreeEdict(rocket);
}
static umove_t tinker_rocket_move = { "birth", NULL, tinker_rocket_impact };

static void tinker_spawn_rocket(edict_t *pulse, edict_t *caster, uint32_t code) {
    cstring_t art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    if (!art || !*art) art = G_AbilityEffectArt(G_AbilityCode(code), WC3_EFFECT_MISSILE, 0);
    cstring_t speed_text = S_SpellString(code, "Missilespeed", 0);
    if (!speed_text || !*speed_text) speed_text = S_SpellString(G_AbilityCode(code), "Missilespeed", 0);
    float const speed = speed_text ? (float)atof(speed_text) : 0.0f;
    float const radius = MAX(0.0f, pulse->collision);
    float const angle = (float)tinker_random(&pulse->variation) / (float)UINT32_MAX * 2.0f * (float)M_PI;
    float const distance = sqrtf((float)tinker_random(&pulse->variation) / (float)UINT32_MAX) * radius;
    vec2_t const destination = {
        pulse->s.origin2.x + cosf(angle) * distance,
        pulse->s.origin2.y + sinf(angle) * distance
    };
    edict_t *rocket;
    channel_t *flight;
    if (!art || !*art || speed <= 0.0f) return;
    rocket = S_SpellIdentityThinker(caster, code, NULL);
    if (!rocket) return;
    rocket->s.player = caster->s.player;
    rocket->s.model = G_RegisterModel(art);
    if (!rocket->s.model) { G_FreeEdict(rocket); return; }
    rocket->s.origin = caster->s.origin;
    rocket->s.origin2 = caster->s.origin2;
    rocket->s.angle = atan2f(destination.y - caster->s.origin2.y,
                             destination.x - caster->s.origin2.x);
    flight = G_GetChannel(rocket);
    flight->origin = destination;
    G_InheritUnitTeamColor(rocket, caster);
    rocket->velocity = speed / 1000.0f;
    rocket->movetype = MOVETYPE_FLYMISSILE;
    rocket->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    rocket->currentmove = &tinker_rocket_move;
    G_StartProjectilePresentation(rocket);
}

static bool tinker_rocket_target(edict_t *caster, edict_t *target, uint32_t code,
                                 vec2_t const *point, float radius) {
    return target && caster && S_SpellIsAliveTarget(target) &&
        S_SpellAllowsAreaTarget(code, caster, target) &&
        Vector2_distance(&target->s.origin2, point) <= radius + MAX(0.0f, target->collision);
}

void tinker_rockets_think(edict_t *pulse) {
    edict_t *caster = S_SpellChannelOwner(pulse);
    uint32_t const now = G_Time();
    uint32_t count = 0;
    if (!caster || !pulse->resources) { G_FreeEdict(pulse); return; }
    while (pulse->resources && now >= pulse->freetime && count++ < TINKER_ROCKET_PULSES_LIMIT) {
        uint32_t const code = pulse->class_id;
        uint32_t const numeric_code = G_AbilityCode(code);
        uint32_t const level = pulse->damage;
        float const amount = MAX(0.0f, S_SpellData(numeric_code, level, 1));
        float const max_damage = MAX(0.0f, S_SpellData(numeric_code, level, 4));
        float const building_factor = MAX(0.0f, S_SpellData(numeric_code, level, 5));
        float total = 0.0f;
        uint32_t const visual = (uint32_t)MAX(0.0f, S_SpellData(numeric_code, level, 3));
        uint32_t const pulses = (uint32_t)MAX(1.0f, S_SpellData(numeric_code, level, 6) /
                                                   MAX(0.001f, pulse->wait * 0.001f));
        /* One cast-wide missile count, spread across the independent damage pulses. */
        uint32_t const index = pulses > pulse->resources ? pulses - pulse->resources : 0;
        uint32_t const start = (visual * index) / pulses;
        uint32_t const end = (visual * (index + 1)) / pulses;
        for (uint32_t i = start; i < MIN(end, start + TINKER_ROCKET_MODELS_PER_PULSE); i++)
            tinker_spawn_rocket(pulse, caster, code);
        FILTER_EDICTS(target, tinker_rocket_target(caster, target, code, &pulse->s.origin2, pulse->collision))
            total += amount * (G_UnitIsStructure(target) ? building_factor : 1.0f);
        { float const scale = max_damage > 0.0f && total > max_damage ? max_damage / total : 1.0f;
          uint32_t budget = max_damage > 0.0f ? (uint32_t)floorf(max_damage + 0.5f) : UINT32_MAX;
          FILTER_EDICTS(target, tinker_rocket_target(caster, target, code, &pulse->s.origin2, pulse->collision)) {
              /* Preserve per-cast fractional damage. Four 11.25 pulses must
               * total 45, not 44 after truncating each pulse separately. */
              float const cumulative = floorf(amount * (float)(index + 1) + 0.5f) -
                                       floorf(amount * (float)index + 0.5f);
              float const raw = cumulative *
                  (G_UnitIsStructure(target) ? building_factor : 1.0f) * scale;
              if (raw > 0.0f && budget) {
                  uint32_t const hit = MIN(budget, (uint32_t)MAX(1.0f, floorf(raw + 0.5f)));
                  S_SpellDamage(target, caster, (int)hit);
                  budget -= hit;
              }
              if (!M_IsDead(target) && G_UnitTargetType(target) != TARG_AIR) {
                  cstring_t buff = S_SpellBuffId(code, level);
                  if (!buff || strlen(buff) < 4) buff = "BNcs";
                  unit_applystatus(target, &(status_application_t){
                      .buff = buff, .level = level,
                      .duration = S_SpellHeroDuration(code, level, target),
                      .source_ability = code, .state_mask = WC3_STATUS_STATE_STUNNED
                  });
              }
          }
        }
        pulse->resources--;
        pulse->freetime += (uint32_t)pulse->wait;
    }
    if (!pulse->resources) G_FreeEdict(pulse);
}

static bool tinker_rockets_validate(edict_t *caster, spellTarget_t target, abilityitem_t const *item) {
    if (!caster || !item || target.type != SPELL_TARGET_POINT) return false;
    uint32_t const code = S_TinkerVariant(caster, item->code);
    uint32_t const level = S_SpellLevel(caster, item->code);
    return S_SpellData(G_AbilityCode(code), level, 1) > 0.0f &&
           S_SpellData(G_AbilityCode(code), level, 2) > 0.0f &&
           S_SpellData(G_AbilityCode(code), level, 6) > 0.0f;
}

static void tinker_rockets_execute(edict_t *caster, spellTarget_t target, abilityitem_t const *item) {
    uint32_t const code = S_TinkerVariant(caster, item->code);
    uint32_t const rank = S_SpellLevel(caster, item->code);
    float const interval = S_SpellData(G_AbilityCode(code), rank, 2);
    float const duration = S_SpellData(G_AbilityCode(code), rank, 6);
    uint32_t const pulses = interval > 0.0f ? (uint32_t)floorf(duration / interval) : 0;
    edict_t *thinker;
    if (!pulses || interval <= 0.0f || duration <= 0.0f) return;
    thinker = S_SpellIdentityThinker(caster, code, NULL);
    if (!thinker) return;
    thinker->s.origin2 = target.point;
    thinker->s.origin = (vec3_t){ target.point.x, target.point.y,
                                  CM_GetHeightAtPoint(target.point.x, target.point.y) };
    thinker->resources = MIN(pulses, 1024u);
    thinker->damage = rank;
    thinker->collision = S_SpellNumber(code, ABILITY_NUMBER_AREA, 1);
    thinker->wait = MAX(1.0f, interval * 1000.0f);
    thinker->freetime = G_Time() + (uint32_t)thinker->wait;
    thinker->variation = (uint32_t)(caster->s.number * 2654435761u) ^ G_Time();
    thinker->svflags |= SVF_NOCLIENT;
    thinker->think = tinker_rockets_think;
    { uint32_t const effect = G_AbilityEffectCode(code, rank);
      if (effect)
          G_SpawnAbilityEffectAtPoint(effect, WC3_EFFECT_EFFECT, 0, &target.point, true); }
    G_PlayAbilityEffectSound(code, &caster->s.origin2);
}

BZ_VALIDATED_SPELL_PROC(AbilityClusterRockets, tinker_rockets_validate, tinker_rockets_execute)
