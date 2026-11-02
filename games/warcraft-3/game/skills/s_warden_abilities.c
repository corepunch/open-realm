#include "s_skills.h"

/* Night Elf Warden (Maiev) hero abilities: Blink, Fan of Knives, Shadow Strike. */

#define ID_BLINK        MAKEFOURCC('A', 'E', 'b', 'l')

/* ---- Blink (AEbl): instant teleport to a target point within range -------- */

static bool blink_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float maxrange = S_SpellData(spell->code, level, 1);
    float minrange = S_SpellData(spell->code, level, 2);
    float dist = Vector2_distance(&caster->s.origin2, &st.point);

    if (maxrange > 0 && dist > maxrange) return false;
    if (minrange > 0 && dist < minrange) return false;
    return true;
}

static void blink_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_SPECIAL, 0, caster, NULL, true);
    vec2_t dest = st.point;
    CM_ClosestPathablePointForRadiusFlags(&st.point, caster->collision, M_UnitStaticPathingFlags(caster), &dest);
    S_SpellCommitRelocation(caster, &dest);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_AREA_EFFECT, 0, caster, NULL, true);
}

/* ---- Registration -------------------------------------------------------- */

BZ_VALIDATED_SPELL_PROC(AbilityBlink, blink_validate, blink_execute)

/* ---- Fan of Knives (AEfk): capped enhanced-physical area damage ------- */

/* Fan of Knives is a magical *ability* with enhanced-physical *damage*.
 * It uses the Spells attack/defense multiplier (including Hero defense), but
 * neither numeric armor nor the magical spell-damage/Anti-Magic Shell path.
 * It cannot hit Ethereal targets and remains blocked by spell immunity. */
static bool fan_of_knives_can_damage(edict_t *target) {
    return !S_UnitSpellImmune(target) &&
           !unit_hasstatusstate(target, WC3_STATUS_STATE_ETHEREAL) &&
           !unit_hasstatusstate(target, WC3_STATUS_STATE_INVULNERABLE) &&
           !G_UnitHasFlag(target, UNIT_FLAG_INVULNERABLE);
}

static void fan_of_knives_damage(edict_t *target, edict_t *caster, int damage) {
    if (damage <= 0 || !fan_of_knives_can_damage(target)) return;
    uint32_t const defense = target->unit_state ? target->unit_state->defense_type : 0;
    float const multiplier = G_AttackTypeDamageMultiplier(ATK_SPELLS, defense);
    int const dealt = (int)((float)damage * multiplier);
    if (dealt > 0) T_Damage(target, caster, dealt);
}

/* The original models both carry Birth - 1/2/3 and Death. The numbered
 * Birth variants are selected by learned rank here (clamped to the three
 * shipped variants), pending a retail selection trace. No gameplay timing is
 * derived from their 1600 model-time-unit sequences. A custom model with
 * only a plain Birth clip continues to work through the generic effect path. */
static void fan_of_knives_set_visual_rank(edict_t *effect, uint32_t rank) {
    char clip[24];
    animation_t const *anim;
    if (!effect || !effect->inuse || !effect->s.model) return;
    snprintf(clip, sizeof(clip), "Birth - %u", (unsigned)MIN(MAX(rank, 1u), 3u));
    anim = G_GetAnimation(effect->s.model, clip);
    if (anim && G_AnimationHasPrimary(anim, "birth")) {
        effect->animation = anim;
        effect->s.frame = anim->interval[0];
    }
}

/* Fan of Knives throws a *visual* circle of knives whether enemies are
 * nearby or not. The stock MissileArt is one animated knife/ribbon model;
 * placing a single copy at the caster produced a stationary, one-sided fan.
 * Spawn fixed-point, non-damaging projectiles on evenly spaced bearings.
 * Their radial destinations are NOT the selected gameplay victims. */
static uint32_t fan_of_knives_visual_count(uint32_t rank) {
    /* Provisional visual density pending a controlled level 1/2/3 retail
     * comparison.  Do not use DataB's gameplay target cap: empty casts still
     * throw knives. Bounded to the three authored Birth variants. */
    return 4u + 4u * MIN(MAX(rank, 1u), 3u); /* 8 / 12 / 16 */
}

static void fan_of_knives_spawn_knife(edict_t *caster, cstring_t art,
                                      vec2_t const *origin, float angle,
                                      float travel, uint32_t rank,
                                      uint32_t knife_index) {
    vec2_t const direction = { cosf(angle), sinf(angle) };
    vec2_t const destination = Vector2_mad(origin, travel, &direction);
    edict_t *effect = G_SpawnModelEffect(art, origin, NULL, NULL, true);
    channel_t *cast;

    if (!effect) return;
    cast = G_AddChannel(effect);
    /* This fixed destination is purely presentation. Its impact handler is
     * the temporary effect's ordinary G_FreeEdict endfunc, never combat. */
    cast->origin = destination;
    cast->owner_spawn_time = caster->spawn_time;
    effect->owner = caster;
    effect->s.player = caster->s.player;
    G_InheritUnitTeamColor(effect, caster);
    effect->s.angle = angle;
    effect->s.origin.z = caster->s.origin.z + 24.0f;
    effect->movetype = MOVETYPE_FLYMISSILE;
    effect->aiflags |= AI_PROJECTILE_FIXED_TARGET;
    /* Stagger flight speeds slightly so the trails occupy the whole radius,
     * rather than a single geometrically perfect ring.  Both 1600-ms Birth
     * animations and the radial flight end independently without gameplay
     * damage or waiting for a target. */
    effect->velocity = (travel / 1050.0f) *
                       (0.85f + 0.15f * (float)(knife_index % 3u));
    G_StartProjectilePresentation(effect);
    fan_of_knives_set_visual_rank(effect, rank);
}

static void fan_of_knives_play_visuals(edict_t *caster, uint32_t code,
                                       uint32_t rank, float radius) {
    vec2_t const origin = caster->s.origin2;
    cstring_t const caster_art = G_AbilityEffectArt(code, WC3_EFFECT_CASTER, 0);
    cstring_t const knife_art = G_AbilityEffectArt(code, WC3_EFFECT_MISSILE, 0);
    uint32_t const knife_count = fan_of_knives_visual_count(rank);
    float const distance = radius > 0.0f ? radius : 400.0f;
    edict_t *effect;

    if (caster_art && *caster_art) {
        effect = G_SpawnModelEffect(caster_art, &origin, NULL, NULL, true);
        fan_of_knives_set_visual_rank(effect, rank);
    }
    /* A map using the same CasterArt and MissileArt is intentionally shown
     * just once; do not multiply a custom caster-only model into 16 objects. */
    if (knife_art && *knife_art && (!caster_art || strcasecmp(knife_art, caster_art))) {
        for (uint32_t i = 0; i < knife_count; i++) {
            float const angle = caster->s.angle + (2.0f * (float)M_PI * (float)i) /
                                                     (float)knife_count;
            fan_of_knives_spawn_knife(caster, knife_art, &origin, angle,
                                      distance, rank, i);
        }
    }
    /* The inspected Hit1/2/3 WAVs do not prove per-target or per-knife sound
     * playback. The authored one-shot ability sound is played exactly once. */
    G_PlayAbilityEffectSound(code, &origin);
}

BZ_SIMPLE_SPELL_PROC(AbilityFanOfKnives) {
    if (!caster) return;
    uint32_t const level = S_SpellLevel(caster, spell->code);
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    float const per_target = MAX(0.0f, S_SpellData(spell->code, level, 1));
    int const damage = (int)per_target;
    float const maxtotal = S_SpellData(spell->code, level, 2);
    float remaining = maxtotal;
    if (radius <= 0.0f) radius = 400.0f;
    /* An empty or zero-damage cast still has its normal spell presentation. */
    fan_of_knives_play_visuals(caster, spell->code, level, radius);
    if (damage <= 0) return;

    /* Maximum damage is a budget for full-strength victims, NOT an amount to
     * distribute over every unit inside the area. DataB <= 0 is uncapped.
     * Traverse stable edict order for deterministic casts; exact retail victim
     * selection among >cap candidates still needs a controlled comparison. */
#define FOK_HITS(t) ((t)->inuse && (t) != caster && S_SpellIsEnemy(caster, t) && \
                     S_SpellAllowsTarget(spell->code, caster, t) &&               \
                     fan_of_knives_can_damage(t) &&                              \
                     Vector2_distance(&(t)->s.origin2, &caster->s.origin2) <= radius)

    FILTER_EDICTS(target, FOK_HITS(target)) {
        if (maxtotal > 0.0f && remaining < per_target) break;
        fan_of_knives_damage(target, caster, damage);
        if (maxtotal > 0.0f) remaining -= per_target;
    }
#undef FOK_HITS
}

/* ---- Shadow Strike (AEsh): homing missile and initial damage -------------- */

static void shadow_strike_projectile_hit(edict_t *missile) {
    edict_t *target = S_SpellProjectileTarget(missile);
    edict_t *caster = S_SpellProjectileOwner(missile);

    if (caster && S_SpellIsAliveTarget(target) &&
        S_SpellAllowsTarget(missile->class_id, caster, target) &&
        !S_TryBlockSpellShield(caster, missile->class_id, target)) {
        S_SpellDamage(target, caster, missile->damage);
        /* TODO(1:1): Shadow Strike also applies a movement slow and a decaying
         * poison DoT via the BEsh buff. The status system needs periodic damage
         * and movement-speed modifiers first. */
    }
    G_FreeEdict(missile);
}

static umove_t shadow_strike_projectile_move = {
    "stand", NULL, shadow_strike_projectile_hit, CAbilityShadowStrike
};

BZ_ABILITY_PROC(CAbilityShadowStrike) {
    if (msg == A_EXECUTE && call && call->item && call->target && call->target->entity) {
        cstring_t speed_text = S_SpellString(call->item->code, "Missilespeed", 0);
        float speed = speed_text ? (float)atof(speed_text) : 1200.0f;
        uint32_t level = S_SpellLevel(ent, call->item->code);
        edict_t *missile = S_SpawnUnitTargetSpellMissile(ent, call->item->code,
            call->target->entity, speed, &shadow_strike_projectile_move);

        if (missile)
            missile->damage = (uint32_t)MAX(1.0f, S_SpellData(call->item->code, level, 5)); /* DataE */
        else
            S_SpellDamage(call->target->entity, ent,
                (int)MAX(1.0f, S_SpellData(call->item->code, level, 5)));
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}
