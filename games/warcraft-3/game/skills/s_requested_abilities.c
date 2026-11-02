#include "s_skills.h"

#define ID_EARTHQUAKE MAKEFOURCC('A', 'O', 'e', 'q')
#define ID_EARTHQUAKE_BUFF MAKEFOURCC('B', 'O', 'e', 'q')
/* HACK: retail Earthquake's ground shake has not been traced, so these are visual guesses chosen only to make
 * the deformation path observable in game. Replace them once the retail effect is measured. */
#define EARTHQUAKE_DEFORM_DURATION_MS 1000 // ms; one pulse spans the 1 s damage tick so pulses join without a gap
#define EARTHQUAKE_DEFORM_UPDATE_MS 200 // ms; five random height fields per pulse, slow enough to read as ground motion
#define EARTHQUAKE_DEFORM_MIN_DELTA -48.0f // world units; lowest random vertex offset, large enough to see at game zoom
#define EARTHQUAKE_DEFORM_MAX_DELTA 48.0f // world units; highest random vertex offset, symmetric with the minimum
#define ID_CHAIN_LIGHTNING_VISIT MAKEFOURCC('C', 'L', 'v', 's')
#define CHAIN_LIGHTNING_JUMP_MS 250
#define CHAIN_LIGHTNING_BOLT_MS 2000

void whirlwind_think(edict_t *ent);
static umove_t whirlwind_blade_storm_move = { "attack walk stand spin", NULL, NULL };

bool S_UnitHasStatus(edict_t const *unit, uint32_t code) {
    if (!unit) return false;
    FOR_LOOP(i, unit->unit_state ? MAX_UNIT_STATUSES : 0)
        if (unit->unit_state->statuses[i].level && unit->unit_state->statuses[i].code == code &&
            (!unit->unit_state->statuses[i].timestamp || unit->unit_state->statuses[i].timestamp > G_Time())) return true;
    return false;
}

static cstring_t spell_buff_fallback(uint32_t code) {
    /* ROC omits BuffID; ACsl/AUsl share the TFT token. */
    if (G_AbilityCode(code) == MAKEFOURCC('A', 'U', 's', 'l')) return "BUsl";
    return NULL;
}

static cstring_t spell_buff(abilityitem_t const *spell, uint32_t level) {
    cstring_t buff = S_SpellBuffId(spell->code, level);
    return buff ? buff : spell_buff_fallback(spell->code);
}

static void target_status_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = spell_buff(spell, level);
    if (!st.entity || !buff) return;
    S_SpellApplyTimedTargetStatus(st.entity, spell->code, level, buff,
                                  S_SpellHeroDuration(spell->code, level, st.entity));
}

static void toggle_status_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)st;
    S_ToggleUnitAbilityStatus(caster, spell->code, S_SpellLevel(caster, spell->code));
}

static void radial_damage_status(edict_t *caster, vec2_t point, abilityitem_t const *spell,
                                 uint32_t data, bool exclude_air) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = spell_buff(spell, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  (!exclude_air || G_UnitTargetType(target) != TARG_AIR) &&
                  Vector2_distance(&target->s.origin2, &point) <= area) {
        S_SpellDamage(target, caster, (int)MAX(1.0f, S_SpellData(spell->code, level, data)));
        if (buff && !M_IsDead(target))
            S_SpellApplyTimedStatus(target, buff, level, S_SpellHeroDuration(spell->code, level, target));
    }
}

/* Retail AOeq/SNeq masks author no relationship token, and the generic mask rule reads that as "any
 * player". That is the original release behaviour: Earthquake shook every building in the area, the
 * caster's own included. A later Blizzard patch stopped it hurting the caster's side without touching the
 * mask, so the patched rule lives here: with no authored relationship, only enemies are hit.
 * Build with WC3_EARTHQUAKE_FRIENDLY_FIRE=1 for the pre-patch behaviour, the authored mask as written. */
static bool earthquake_allows_unit(uint32_t code, cstring_t targets, edict_t *caster, edict_t *target) {
    if (!S_SpellIsAliveTarget(target) || !S_SpellAllowsAreaTarget(code, caster, target)) return false;
#ifdef WC3_EARTHQUAKE_FRIENDLY_FIRE
    (void)targets;
    return true;
#else
    return S_SpellTargetHasToken(targets, "friend", NULL) || S_SpellTargetHasToken(targets, "enemy", NULL) ||
        S_SpellTargetHasToken(targets, "neutral", NULL) || S_SpellIsEnemy(caster, target);
#endif
}

static bool earthquake_hits_destructable(edict_t *target, cstring_t targets, float radius, vec2_t const *origin) {
    if (!target || !target->inuse || (target->targtype != TARG_TREE && target->targtype != TARG_DEBRIS)) return false;
    if (!G_IsDestructable(target) || G_GetDestructable(target)->dead) return false;
    if (targets && *targets) {
        if (target->targtype == TARG_TREE && !S_SpellTargetHasToken(targets, "tree", NULL)) return false;
        if (target->targtype == TARG_DEBRIS && !S_SpellTargetHasToken(targets, "debris", NULL)) return false;
    }
    return Vector2_distance(&target->s.origin2, origin) <= radius;
}

float S_EarthquakeMoveReduction(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, ID_EARTHQUAKE_BUFF);
    if (!level) return 0.0f;
    return MIN(1.0f, MAX(0.0f, S_SpellData(ID_EARTHQUAKE, level, 3)));
}

edict_t *S_SpellBeginAreaPresentation(edict_t *owner, uint32_t code, uint32_t level,
                                     vec2_t const *point) {
    edict_t *effect;
    uint32_t effect_code = G_AbilityEffectCode(code, level);
    int loop_sound;
    if (G_AbilityEffectSoundIndex(code, false)) G_PlayAbilityEffectSound(code, point);
    else if (effect_code != code) G_PlayAbilityEffectSound(effect_code, point);
    effect = G_SpawnOwnedAbilityEffectAtPoint(owner, code, WC3_EFFECT_AREA_EFFECT, 0, point);
    if (!effect) effect = G_SpawnOwnedAbilityEffectAtPoint(owner, code, WC3_EFFECT_EFFECT, 0, point);
    if (!effect && effect_code != code)
        effect = G_SpawnOwnedAbilityEffectAtPoint(owner, effect_code, WC3_EFFECT_AREA_EFFECT, 0, point);
    if (!effect && effect_code != code)
        effect = G_SpawnOwnedAbilityEffectAtPoint(owner, effect_code, WC3_EFFECT_EFFECT, 0, point);
    loop_sound = G_AbilityEffectSoundIndex(code, true);
    if (!loop_sound && effect_code != code)
        loop_sound = G_AbilityEffectSoundIndex(effect_code, true);
    if (effect && loop_sound) effect->s.sound = (uint16_t)loop_sound;
    return effect;
}

void S_SpellEndAreaPresentation(edict_t *owner) {
    G_DestroyOwnedEffects(owner);
}

void earthquake_think(edict_t *ent) {
    if (!S_SpellChannelActive(ent)) { S_SpellEndAreaPresentation(ent); S_SpellEndChannel(ent); return; }
    uint32_t level = S_SpellLevel(ent->owner, ent->class_id), now = G_Time();
    abilityitem_t item = S_AbilityItem(ent->class_id);
    abilityitem_t const *spell = &item;
    cstring_t buff = spell_buff(spell, level);
    cstring_t targets = G_AbilityLevel(ent->class_id, level)->targs;
    float radius = S_SpellNumber(ent->class_id, ABILITY_NUMBER_AREA, level);
    float damage = S_SpellData(ent->class_id, level, 2);
    if (now >= ent->spawn_time) { S_SpellEndAreaPresentation(ent); S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    G_SendTerrainDeformation(&(terrainDeform_t){ .type = TERRAIN_DEFORM_RANDOM,
        .random = { .origin = ent->s.origin2, .radius = radius,
                    .min_delta = EARTHQUAKE_DEFORM_MIN_DELTA, .max_delta = EARTHQUAKE_DEFORM_MAX_DELTA },
        .duration_ms = EARTHQUAKE_DEFORM_DURATION_MS,
        .update_ms = EARTHQUAKE_DEFORM_UPDATE_MS });
    FILTER_EDICTS(target, earthquake_allows_unit(ent->class_id, targets, ent->owner, target) &&
                  Vector2_distance(&target->s.origin2, &ent->s.origin2) <= radius) {
        if (G_UnitIsStructure(target)) {
            S_SpellDamage(target, ent->owner, (int)damage);
        } else if (G_UnitTargetType(target) == TARG_GROUND && buff) {
            (void)S_SpellApplyTimedStatus(target, buff, level, 1.5f);
        }
    }
    FILTER_EDICTS(target, earthquake_hits_destructable(target, targets, radius, &ent->s.origin2))
        G_DestructableApplyDamage(target, ent->owner, damage);
    ent->freetime = now + 1000;
}

void far_sight_think(edict_t *thinker) {
    if (G_Time() >= thinker->spawn_time || thinker->s.player >= MAX_PLAYERS) {
        S_SpellEndAreaPresentation(thinker);
        G_FreeEdict(thinker);
        return;
    }
    G_FowSetStateRadius(&(fogWrite_t){ thinker->s.player, WC3_FOG_STATE_VISIBLE, true },
                        &thinker->s.origin2, thinker->collision);
}

void whirlwind_think(edict_t *ent) {
    if (!S_SpellChannelActive(ent)) {
        edict_t *caster = S_SpellChannelOwner(ent);
        if (caster && !M_IsDead(caster) && ent->class_id == MAKEFOURCC('A','O','w','w') &&
            caster->currentmove == &whirlwind_blade_storm_move && caster->stand)
            caster->stand(caster);
        S_SpellEndChannel(ent);
        return;
    }
    abilityitem_t item = S_AbilityItem(ent->class_id);
    uint32_t data = item.ability && item.ability->proc == CAbilityStampede ? 2 : 1;
    if (G_Time() >= ent->spawn_time) {
        edict_t *caster = S_SpellChannelOwner(ent);
        if (caster && !M_IsDead(caster) && ent->class_id == MAKEFOURCC('A','O','w','w') &&
            caster->currentmove == &whirlwind_blade_storm_move && caster->stand)
            caster->stand(caster);
        S_SpellEndChannel(ent);
        return;
    }
    if (ent->freetime && G_Time() < ent->freetime) return;
    if (item.ability && item.ability->proc == CAbilityWhirlwind)
        ent->s.origin2 = ent->owner->s.origin2;
    radial_damage_status(ent->owner, ent->s.origin2, &item, data,
                         item.ability && item.ability->proc == CAbilityWhirlwind);
    ent->freetime = G_Time() + 1000;
}

static void whirlwind_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code);
    if (!thinker) return;
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(spell->code, level, true) * 1000.0f);
    if (spell->code == MAKEFOURCC('A','O','w','w')) {
        animation_t const *spin = G_GetUnitAnimation(caster, "attack walk stand spin");
        if (spin) {
        /* This is presentation inside the active channel, not a new order:
         * unit_setmove would dispatch A_MOVE_LEAVE and cancel Bladestorm. */
        caster->currentmove = &whirlwind_blade_storm_move;
        G_SetUnitAnimation(caster, "attack walk stand spin");
        }
    }
    thinker->think = whirlwind_think; whirlwind_think(thinker);
}

void morph_end(edict_t *thinker) {
    edict_t *owner = S_SpellChannelOwner(thinker);
    if (G_Time() < thinker->spawn_time) return;
    if (owner && G_AbilityCode(thinker->class_id) == MAKEFOURCC('A','E','m','e')) {
        heroabilitystatus_t *status = unit_findstatus(owner, MAKEFOURCC('B','E','m','e'));
        if (status) unit_removestatus(owner, status, STATUS_REMOVE_EXPIRE);
    }
    if (owner && G_TransformUnitType(owner, thinker->resources) &&
        G_AbilityCode(thinker->class_id) == MAKEFOURCC('A','E','m','e') && thinker->velocity > 0.0f)
        G_ApplyTemporaryMaxHealthBonus(owner, -thinker->velocity);
    G_FreeEdict(thinker);
}

/* Black Arrow is a per-hit attack modifier. Never consult a mutable
 * autocast toggle at impact: the shot's ability and rank are selected and
 * paid for at its weapon damage point, then carried on the missile. */
static bool black_arrow_ability(uint32_t code) {
    abilityitem_t const item = S_AbilityItem(code);
    return item.ability && item.ability->proc == CAbilityBlackArrow;
}

bool S_BlackArrowIssueOrder(edict_t *attacker, edict_t *target, uint32_t ability) {
    heroabilitystatus_t *status;
    uint32_t level;
    if (!attacker || !target || !attacker->unit_state || !black_arrow_ability(ability) ||
        !G_UnitAbilityLevel(attacker, ability) || !S_AttackCanTarget(attacker, target) ||
        !S_SpellIsEnemy(attacker, target) ||
        !S_SpellAllowsTarget(ability, attacker, target)) return false;
    level = G_UnitAbilityLevel(attacker, ability);
    status = unit_applystatus(attacker, &(status_application_t){
        .buff = GetClassName(ability), .level = level, .source_ability = ability,
        .rank = level, .buff_flags = WC3_STATUS_BUFF_ONE_SHOT_ATTACK
    });
    if (!status) return false;
    if (S_OrderAttack(attacker, target)) return true;
    unit_removestatus(attacker, status, STATUS_REMOVE_SCRIPT);
    return false;
}

bool S_BlackArrowPrepareAttack(edict_t *attacker, edict_t *target,
                               uint32_t *ability, uint32_t *rank) {
    heroabilitystatus_t *manual = NULL;
    uint32_t code = 0, level = 0;
    if (ability) *ability = 0;
    if (rank) *rank = 0;
    if (!attacker || !target || !attacker->unit_state || !ability || !rank) return false;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = attacker->unit_state->statuses + i;
        uint32_t const source = status->source_ability ? status->source_ability : status->code;
        if (!status->level || !(status->buff_flags & WC3_STATUS_BUFF_ONE_SHOT_ATTACK) ||
            (status->timestamp && status->timestamp <= G_Time()) ||
            !black_arrow_ability(source)) continue;
        manual = status;
        code = source;
        level = status->rank ? status->rank : status->level;
        break;
    }
    if (!code && attacker->unit_state->autocast &&
        black_arrow_ability(attacker->unit_state->autocast) &&
        G_UnitAutocastIsOn(attacker, attacker->unit_state->autocast)) {
        code = attacker->unit_state->autocast;
        level = G_UnitAbilityLevel(attacker, code);
    }
    /* An armed manual attack is one attempt even if the target has become
     * ineligible, mana is empty or the weapon swing was evaded. */
    if (manual) unit_removestatus(attacker, manual, STATUS_REMOVE_SCRIPT);
    if (!code || !level || !G_UnitAbilityLevel(attacker, code) ||
        !S_AttackCanTarget(attacker, target) || !S_SpellIsEnemy(attacker, target) ||
        !S_SpellAllowsAreaTarget(code, attacker, target) ||
        !S_SpellSpendMana(attacker, code, level)) return false;
    *ability = code;
    *rank = level;
    return true;
}

int S_BlackArrowDamageForShot(uint32_t ability, uint32_t level, int damage) {
    return ability && level && black_arrow_ability(ability) ?
        damage + (int)S_SpellData(ability, level, 1) : damage;
}

/* Black Arrow raises a Dark Minion only from a corpse-producing unit. The
 * shared UnitData deathType bit 0 marks raisable remains; units with no corpse,
 * structures, Heroes, illusions and already-unraisable victims are excluded.
 * A legal hit still inflicts its ordinary damage when conversion is ineligible. */
static bool black_arrow_corpse_eligible(edict_t const *victim) {
    UnitData_t const *data;
    if (!victim || !victim->inuse || !(victim->svflags & SVF_MONSTER) ||
        G_UnitIsHero(victim) || G_UnitIsStructure(victim) ||
        (victim->aiflags & (AI_CORPSE_UNRAISABLE | AI_SCRIPT_EXPLODED | AI_ILLUSION)))
        return false;
    data = victim->data.UnitData ? victim->data.UnitData : G_UnitData(victim->class_id);
    return data && (data->deathType & 1u) != 0;
}

/* This is called after the authoritative attack shields, Spirit Link and
 * immunities accept positive damage, but before the damage can kill its unit.
 * The status uses existing serialized source/alias/rank/timestamp fields, so
 * save/load preserves the mark without adding a new entity/save contract. */
void S_BlackArrowMarkHit(edict_t *attacker, edict_t *victim,
                         uint32_t ability, uint32_t level) {
    cstring_t buff;
    float duration;
    heroabilitystatus_t *mark;
    if (!attacker || !attacker->inuse || !victim || M_IsDead(victim) ||
        !ability || !level || !black_arrow_ability(ability) ||
        !black_arrow_corpse_eligible(victim)) return;
    buff = S_SpellBuffId(ability, level);
    /* RoC/custom aliases can omit a BuffID. Use the stock Black Arrow marker
     * identity rather than making such projectiles lose conversion entirely. */
    if (!buff || strlen(buff) < 4) buff = "BNba";
    duration = S_SpellDuration(ability, level, false);
    /* Some old ability tables omit Dur for an attack modifier. The documented
     * original Black Arrow mark lasts two seconds. */
    if (duration <= 0.0f) duration = 2.0f;
    mark = unit_applystatus(victim, &(status_application_t){
        .buff = buff, .level = level, .duration = duration,
        .source_ability = ability, .rank = level, .source = attacker,
        .buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL
    });
    if (mark) {
        mark->source_ability = ability;
        mark->rank = level;
        mark->source = attacker;
        mark->source_spawn_time = attacker->spawn_time;
    }
}

/* A_STATUS_DEATH runs during the ordinary unit_die lifecycle, including when
 * an unrelated attacker delivers the finishing blow during the mark's life.
 * Consume first so nested death events can never create a second skeleton. */
static bool black_arrow_summon_on_death(edict_t *victim, heroabilitystatus_t *mark) {
    edict_t *source;
    uint32_t ability, rank, unit_id;
    float duration;
    if (!victim || !mark || !mark->level ||
        (mark->timestamp && mark->timestamp <= G_Time())) return false;
    ability = mark->source_ability;
    rank = mark->rank ? mark->rank : mark->level;
    source = S_SpellStatusSource(mark);
    /* If multiple custom Black Arrow buffs coexist, the most recent landed
     * mark is authoritative; earlier marks cannot produce another summon. */
    FOR_LOOP(i, victim->unit_state ? MAX_UNIT_STATUSES : 0) {
        heroabilitystatus_t const *other = victim->unit_state->statuses + i;
        if (other != mark && other->level && other->instance_id > mark->instance_id &&
            (!other->timestamp || other->timestamp > G_Time()) &&
            black_arrow_ability(other->source_ability)) {
            unit_removestatus_deferred(victim, mark, STATUS_REMOVE_DEATH);
            return true;
        }
    }
    unit_removestatus_deferred(victim, mark, STATUS_REMOVE_DEATH);
    if (!source || !black_arrow_ability(ability) || !black_arrow_corpse_eligible(victim)) return true;
    unit_id = S_SpellUnitId(ability, rank);
    duration = S_SpellData(ability, rank, 3);
    /* Some creep/custom aliases omit their UnitID/lifetime and inherit the
     * base Black Arrow row. Keep the concrete alias for summon ownership. */
    if (!unit_id && G_AbilityCode(ability) != ability) {
        uint32_t const base = G_AbilityCode(ability);
        unit_id = S_SpellUnitId(base, rank);
        if (duration <= 0.0f) duration = S_SpellData(base, rank, 3);
    }
    if (unit_id && S_SummonAbilityAt(source, ability, unit_id, &victim->s.origin2,
                                     duration > 0.0f ? duration : 0.0f)) {
        /* Successful conversion consumes raisable remains. The unit's normal
         * Death animation still plays; its corpse is removed afterward. */
        victim->aiflags |= AI_CORPSE_UNRAISABLE | AI_CORPSE_NO_DECAY;
    }
    return true;
}

static void death_coil_projectile_hit(edict_t *missile);
static umove_t death_coil_projectile_move = { "stand", NULL, death_coil_projectile_hit, CAbilityDeathCoil };

static bool death_coil_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    cstring_t race = st.entity && st.entity->data.UnitData ? st.entity->data.UnitData->race : NULL;
    (void)spell;
    if (!S_SpellIsAliveTarget(st.entity) || st.entity == caster || !race) return false;
    if (!strcmp(race, STR_UNDEAD) && S_SpellIsFriend(caster, st.entity))
        return st.entity->health.value < st.entity->health.max_value;
    return strcmp(race, STR_UNDEAD) && S_SpellIsEnemy(caster, st.entity);
}

static float death_coil_missile_speed(uint32_t code) {
    cstring_t value = S_SpellString(code, "Missilespeed", 0);
    float speed;

    if (!value && G_AbilityCode(code) != code) value = S_SpellString(G_AbilityCode(code), "Missilespeed", 0);
    speed = value ? atof(value) : 0.0f;
    return speed > 0.0f ? speed : 1000.0f;
}

static void death_coil_projectile_hit(edict_t *missile) {
    edict_t *target = S_SpellProjectileTarget(missile), *caster = S_SpellProjectileOwner(missile);
    cstring_t race = target && target->data.UnitData ? target->data.UnitData->race : NULL;
    bool applied = false;

    if (caster && caster->inuse && S_SpellIsAliveTarget(target) && race &&
        !S_TryBlockSpellShield(caster, missile->class_id, target)) {
        if (!strcmp(race, STR_UNDEAD) && S_SpellIsFriend(caster, target)) {
            S_SpellHeal(target, missile->damage);
            applied = true;
        } else if (strcmp(race, STR_UNDEAD) && S_SpellIsEnemy(caster, target)) {
            applied = S_SpellDamage(target, caster, (int)(missile->damage * 0.5f));
        }
        if (applied) G_SpawnAbilityEffectTarget(missile->class_id, WC3_EFFECT_SPECIAL, 0, target, NULL, true);
    }
    G_FreeEdict(missile);
}

static void death_coil_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *missile = S_SpawnUnitTargetSpellMissile(caster, spell->code, st.entity,
                                                     death_coil_missile_speed(spell->code),
                                                     &death_coil_projectile_move);

    if (!missile) return;
    missile->damage = (uint32_t)MAX(0.0f, S_SpellData(spell->code, level, 1));
}

/* Chain Lightning is asynchronous in Warcraft/Warsmash: the first target is
 * struck immediately and each subsequent jump occurs 0.25 seconds later.
 * Keep the delayed cast entirely in ordinary save-safe edicts. The main
 * thinker owns the next damage/radius/jump count, while small no-client marker
 * edicts remember target identity (pointer + spawn generation) so simultaneous
 * or delayed jumps cannot revisit an earlier unit. */
static bool chain_lightning_visited(edict_t *thinker, edict_t const *target) {
    FILTER_EDICTS(marker, marker->class_id == ID_CHAIN_LIGHTNING_VISIT &&
                  S_SpellChannelOwner(marker) == thinker && marker->goalentity == target &&
                  marker->resources == target->spawn_time)
        return true;
    return false;
}

static void chain_lightning_mark_visited(edict_t *thinker, edict_t *target) {
    edict_t *marker = G_Spawn();
    if (!marker) return;
    marker->class_id = ID_CHAIN_LIGHTNING_VISIT;
    marker->svflags |= SVF_NOCLIENT;
    marker->owner = thinker;
    G_AddChannel(marker);
    G_GetChannel(marker)->owner_spawn_time = thinker->spawn_time;
    marker->goalentity = target;
    marker->resources = target->spawn_time;
}

static void chain_lightning_finish(edict_t *thinker) {
    edict_t *markers[32];
    uint32_t count = 0;
    FILTER_EDICTS(marker, marker->class_id == ID_CHAIN_LIGHTNING_VISIT &&
                  S_SpellChannelOwner(marker) == thinker)
        if (count < 32) markers[count++] = marker;
    FOR_LOOP(i, count) G_FreeEdict(markers[i]);
    G_FreeEdict(thinker);
}

void chain_lightning_think(edict_t *thinker) {
    edict_t *caster, *next = NULL;
    float nearest_distance = 0.0f;

    if (!thinker || !thinker->inuse) return;
    caster = S_SpellChannelOwner(thinker);
    if (!caster || !thinker->resources) {
        chain_lightning_finish(thinker);
        return;
    }
    if (G_Time() < thinker->freetime) return;

    FILTER_EDICTS(target, S_SpellBounceTargetAllowed(caster, thinker->class_id, thinker, target,
                                                    thinker->collision, false)) {
        float distance;
        if (chain_lightning_visited(thinker, target)) continue;
        distance = Vector2_distance(&target->s.origin2, &thinker->s.origin2);
        /* Warcraft Chain Lightning follows the nearest eligible unvisited unit.
         * FILTER_EDICTS' stable scan order is the deterministic tie breaker. */
        if (!next || distance < nearest_distance) {
            next = target;
            nearest_distance = distance;
        }
    }
    if (!next) {
        chain_lightning_finish(thinker);
        return;
    }

    if (thinker->goalentity && thinker->goalentity->inuse && thinker->goalentity->spawn_time == thinker->damage) {
        G_SpawnAbilityLightning(&(abilityLightningParams_t){
            .ability_id = thinker->class_id, .index = 1,
            .source = thinker->goalentity, .target = next,
            .duration_ms = CHAIN_LIGHTNING_BOLT_MS,
        });
    } else {
        vec3_t from = { thinker->s.origin2.x, thinker->s.origin2.y,
            CM_GetHeightAtPoint(thinker->s.origin2.x, thinker->s.origin2.y) + next->s.radius * 0.5f };
        vec3_t to = next->s.origin;
        uint32_t lightning = G_AbilityLightningId(thinker->class_id, 1);
        to.z += next->s.radius * 0.5f;
        if (lightning) G_LightningAdd(&(lightningAddParams_t){
            .effect_id = lightning, .source = &from, .target = &to,
            .color = COLOR32_WHITE, .duration_ms = CHAIN_LIGHTNING_BOLT_MS,
        });
    }
    S_SpellDamage(next, caster, (int)MAX(1.0f, thinker->wait));
    G_SpawnAbilityEffectTarget(thinker->class_id, WC3_EFFECT_TARGET, 0, next, NULL, true);
    chain_lightning_mark_visited(thinker, next);
    thinker->goalentity = next;
    thinker->damage = next->spawn_time;
    thinker->s.origin2 = next->s.origin2;
    thinker->wait *= thinker->velocity;
    thinker->resources--;
    if (!thinker->resources) {
        chain_lightning_finish(thinker);
        return;
    }
    thinker->freetime = G_Time() + CHAIN_LIGHTNING_JUMP_MS;
}

static void chain_lightning_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t hits = MIN(32, (uint32_t)MAX(0.0f, S_SpellData(spell->code, level, 2)));
    float damage = S_SpellData(spell->code, level, 1);
    edict_t *thinker;

    if (!st.entity || !hits) return;
    G_PlayAbilityEffectSound(spell->code, &st.entity->s.origin2);
    G_SpawnAbilityLightning(&(abilityLightningParams_t){
        .ability_id = spell->code, .source = caster, .target = st.entity,
        .duration_ms = CHAIN_LIGHTNING_BOLT_MS,
    });
    S_SpellDamage(st.entity, caster, (int)MAX(1.0f, damage));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
    if (hits <= 1) return;

    thinker = G_Spawn();
    if (!thinker) return;
    thinker->class_id = spell->code;
    thinker->svflags |= SVF_NOCLIENT;
    thinker->owner = caster;
    G_AddChannel(thinker);
    G_GetChannel(thinker)->owner_spawn_time = caster->spawn_time;
    thinker->s.origin2 = st.entity->s.origin2;
    thinker->goalentity = st.entity;
    thinker->damage = st.entity->spawn_time;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->wait = damage * (1.0f - S_SpellData(spell->code, level, 3));
    thinker->velocity = 1.0f - S_SpellData(spell->code, level, 3);
    thinker->resources = hits - 1;
    thinker->freetime = G_Time() + CHAIN_LIGHTNING_JUMP_MS;
    thinker->think = chain_lightning_think;
    chain_lightning_mark_visited(thinker, st.entity);
}

static edict_t *reincarnation_spawn_tombstone(vec2_t const *point) {
    edict_t *tombstone;
    uint32_t model;

    if (!point) return NULL;
    model = G_RegisterModel("Doodads\\Northrend\\Props\\NorthrendTombstone\\NorthrendTombStone0.mdx");
    if (!model) return NULL;
    tombstone = G_Spawn();
    if (!tombstone) return NULL;
    tombstone->s.model = model;
    tombstone->s.flags |= EF_NOT_SELECTABLE;
    tombstone->s.origin2 = *point;
    tombstone->s.origin.x = point->x;
    tombstone->s.origin.y = point->y;
    tombstone->s.origin.z = CM_GetHeightAtPoint(point->x, point->y);
    tombstone->movetype = MOVETYPE_NONE;
    gi.LinkEntity(tombstone);
    return tombstone;
}

static umove_t reincarnation_return_effect_move = { "death", NULL, G_FreeEdict };

static void reincarnation_spawn_return_effect(edict_t *hero) {
    edict_t *effect;

    if (!hero) return;
    effect = G_SpawnModelEffect("Abilities\\Spells\\Orc\\Reincarnation\\ReincarnationTarget.mdx",
                                NULL, hero, "origin", false);
    if (!effect) return;
    /* The authored Birth/Stand sequences are only a thin marker beam; the
     * bright gold return flare is the model's Death sequence. */
    unit_setmove(effect, &reincarnation_return_effect_move);
    if (!effect->animation) G_FreeEdict(effect);
}

void reincarnation_think(edict_t *thinker) {
    edict_t *owner = S_SpellChannelOwner(thinker);
    if (!owner) {
        if (thinker->goalentity && thinker->goalentity->inuse) G_FreeEdict(thinker->goalentity);
        G_FreeEdict(thinker);
        return;
    }
    if (G_Time() < thinker->spawn_time) return;
    if (thinker->goalentity && thinker->goalentity->inuse) G_FreeEdict(thinker->goalentity);
    thinker->goalentity = NULL;
    if (M_IsDead(owner)) {
        G_ReviveHero(owner, thinker->s.origin2.x, thinker->s.origin2.y);
        reincarnation_spawn_return_effect(owner);
    }
    G_FreeEdict(thinker);
}

void S_ReincarnationOnDeath(edict_t *unit) {
    static uint32_t const codes[] = { MAKEFOURCC('A','O','r','e'), MAKEFOURCC('A','C','r','n'), MAKEFOURCC('A','N','r','n') };
    uint32_t code = 0, level = 0;
    edict_t *thinker;
    FOR_LOOP(i, sizeof(codes) / sizeof(*codes)) if ((level = G_UnitAbilityLevel(unit, codes[i]))) { code = codes[i]; break; }
    if (!level || !S_SpellCooldownReady(unit, code)) return;
    thinker = G_Spawn(); thinker->owner = unit; thinker->class_id = code; thinker->s.origin2 = unit->s.origin2;
    thinker->goalentity = reincarnation_spawn_tombstone(&unit->s.origin2);
    G_AddChannel(thinker);
    G_GetChannel(thinker)->owner_spawn_time = unit->spawn_time;
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellData(code, level, 1) * 1000.0f);
    thinker->think = reincarnation_think; S_SpellStartCooldown(unit, code, level);
}

void acid_bomb_think(edict_t *thinker) {
    edict_t *owner = S_SpellChannelOwner(thinker);
    edict_t *target = S_SpellChannelTarget(thinker);
    if (G_Time() >= thinker->spawn_time || !owner || !target || M_IsDead(target)) { G_FreeEdict(thinker); return; }
    if (!thinker->freetime || G_Time() >= thinker->freetime) {
        S_SpellDamage(target, owner, thinker->damage); thinker->freetime = G_Time() + 1000;
    }
}

/* Mass Teleport keeps its channel-owned area effects on the thinker so cancel,
 * target death, save/load continuation and successful completion all use the
 * same cleanup path. */
static void mass_teleport_cleanup(edict_t *thinker) {
    edict_t *target;
    bool newer_pause_owner = false;

    if (!thinker) return;
    target = S_SpellChannelTarget(thinker);
    /* A_CANCEL can clean this thinker before its next scheduled tick. When a
     * new Mass Teleport is cast on the same destination, the retired thinker's
     * later cleanup must not release the new channel's pause. Check the live
     * channel identity without invoking S_SpellChannelActive() (which can
     * itself cancel channels while iterating thinkers). */
    if (target && thinker->wait < 0.5f) {
        FILTER_EDICTS(other, other != thinker && other->think == mass_teleport_think && other->goalentity == target) {
            channel_t const *other_cast = G_GetChannel(other);
            edict_t *owner;
            channel_t const *owner_cast;
            if (!other_cast || other_cast->target_spawn_time != target->spawn_time) continue;
            owner = S_SpellChannelOwner(other);
            owner_cast = G_GetChannel(owner);
            if (owner && owner_cast && owner_cast->code == other->class_id &&
                owner_cast->serial == other_cast->serial && !M_IsDead(owner)) {
                newer_pause_owner = true;
                break;
            }
        }
        if (!newer_pause_owner) G_SetUnitFlag(target, UNIT_FLAG_PAUSED, false);
    }
    /* Owned effects are keyed to the thinker and may be retired repeatedly. */
    G_DestroyOwnedEffects(thinker);
}

static void mass_teleport_move_unit(edict_t *unit, uint32_t code, vec2_t const *requested) {
    vec2_t position;

    if (!unit || !requested) return;
    /* SetUnitPosition keeps the requested point as a fallback when no spiral
     * candidate is open.  Mass Teleport uses the same relocation contract. */
    (void)G_FindUnitUnstuckPosition(unit, requested, &position);
    S_SpellRelocateUnit(unit, code, &position);
}

void mass_teleport_think(edict_t *thinker) {
    edict_t *caster = thinker ? thinker->owner : NULL;
    edict_t *target = S_SpellChannelTarget(thinker);
    uint32_t now = G_Time(), level, limit, count = 1;
    float area;
    bool cluster;
    vec2_t src, dst;

    if (!thinker) return;
    if (!S_SpellChannelActive(thinker)) {
        mass_teleport_cleanup(thinker);
        S_SpellEndChannel(thinker);
        return;
    }
    if (!target || !S_SpellAllowsTarget(thinker->class_id, caster, target)) {
        mass_teleport_cleanup(thinker);
        S_SpellEndChannel(thinker);
        return;
    }
    if (thinker->freetime && now < thinker->freetime) return;

    level = thinker->variation ? thinker->variation : 1;
    area = S_SpellNumber(thinker->class_id, ABILITY_NUMBER_AREA, level);
    limit = (uint32_t)S_SpellData(thinker->class_id, level, 1);
    cluster = S_SpellData(thinker->class_id, level, 3) != 0.0f;
    src = caster->s.origin2;
    dst = target->s.origin2;

    /* The caster relocates first, matching SetUnitPosition-based Mass
     * Teleport behavior; payload units then find collision-safe positions
     * around that authoritative destination. */
    mass_teleport_move_unit(caster, thinker->class_id, &dst);

    /* The gameplay description owns nearby units of the caster's player. Do
     * not drag allied-player armies or structures merely because the target
     * relation for the destination is friendly. */
    FILTER_EDICTS(unit, count < limit && unit != caster && S_SpellIsAliveTarget(unit) &&
                  unit->s.player == caster->s.player && !G_UnitIsStructure(unit) &&
                  Vector2_distance(&unit->s.origin2, &src) <= area) {
        vec2_t offset = Vector2_sub(&unit->s.origin2, &src);
        vec2_t requested = cluster ? dst : Vector2_add(&dst, &offset);
        mass_teleport_move_unit(unit, thinker->class_id, &requested);
        count++;
    }
    mass_teleport_cleanup(thinker);
    S_SpellEndChannel(thinker);
}

/* Name=Mass Teleport
 * Ubertip="Teleports the caster and nearby friendly units to a target location."
 */
BZ_ABILITY_PROC(CAbilityMassTeleport) {
    abilityitem_t const *spell = call ? call->item : NULL;

    if (msg == A_VALIDATE) {
        edict_t *target = call && call->target ? call->target->entity : NULL;
        if (!target || !S_SpellIsAliveTarget(target) || target == ent ||
            !S_SpellAllowsAreaTarget(spell ? spell->code : 0, ent, target)) return false;
#if BZ_COMPAT_WORKAROUNDS
        /* RoC AHmt has no player/ally relationship tokens in targs, so retain
         * its retail friendly-destination rule explicitly. */
        if (spell && spell->code == MAKEFOURCC('A','H','m','t')) {
            abilityLevel_t const *row = G_AbilityLevel(spell->code, S_SpellLevel(ent, spell->code));
            if (row && row->targs && !strstr(row->targs, "player") && !strstr(row->targs, "ally") &&
                !S_SpellIsFriend(ent, target)) return false;
        }
#endif
        return true;
    }
    if (msg == A_CANCEL) {
        uint32_t code = spell ? spell->code : 0;
        FILTER_EDICTS(thinker, thinker->inuse && S_SpellChannelOwner(thinker) == ent &&
                      thinker->class_id == code && thinker->think == mass_teleport_think)
            mass_teleport_cleanup(thinker);
        return true;
    }
    if (msg == A_EXECUTE) {
        spellTarget_t st = call && call->target ? *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
        edict_t *target = st.entity, *thinker;
        uint32_t level;
        float delay;

        if (!spell || !target) return false;
        level = S_SpellLevel(ent, spell->code);
        delay = MAX(0.0f, S_SpellData(spell->code, level, 2));
        thinker = S_SpellChannelTargetThinker(ent, spell->code, target);
        thinker->variation = level;
        thinker->wait = G_UnitHasFlag(target, UNIT_FLAG_PAUSED) ? 1.0f : 0.0f;
        thinker->freetime = G_Time() + (uint32_t)(delay * 1000.0f);
        thinker->think = mass_teleport_think;

        G_SpawnOwnedAbilityEffectAtPoint(thinker, spell->code, WC3_EFFECT_AREA_EFFECT, 0, &ent->s.origin2);
        G_SpawnOwnedAbilityEffectAtPoint(thinker, spell->code, WC3_EFFECT_AREA_EFFECT, 0, &target->s.origin2);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, ent, NULL, true);
        G_SetUnitFlag(target, UNIT_FLAG_PAUSED, true);
        mass_teleport_think(thinker);
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}
/* Name=Stampede
 * Ubertip="Calls down hordes of rampaging thunder lizards to explode upon the Beastmaster's enemies."
 */
BZ_SIMPLE_SPELL_PROC(AbilityStampede) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code); thinker->s.origin2 = st.point;
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(spell->code, level, false) * 1000.0f);
    thinker->freetime = G_Time(); thinker->think = whirlwind_think;
}
/* Name=Bladestorm
 * Ubertip="Causes a Blademaster to spin violently, damaging nearby enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityWhirlwind) { whirlwind_execute(caster, st, spell); }
/* Name=Tornado
 * Ubertip="Creates a tornado that damages and disables enemy units."
 */
/* Name=Banish
 * Ubertip="Turns a target unit ethereal, making it unable to attack or be attacked by physical attacks."
 */
BZ_SIMPLE_SPELL_PROC(AbilityBanish) { target_status_execute(caster, st, spell); }
/* Name=Phoenix
 * Ubertip="Summons a Phoenix to fight for the caster."
 */
BZ_SIMPLE_SPELL_PROC(AbilitySummonPhoenix) { S_SummonPhoenix(caster, spell->code); }
/* Carrion Beetles consume one targeted corpse, spawn authored DataC beetles,
 * and cap living beetles per Crypt Lord using DataE. DataA is beetles per cast;
 * Dur=0 makes each beetle permanent until killed. */
static uint32_t carrion_scarab_count(edict_t *caster, uint32_t code) {
    uint32_t count = 0;
    if (!caster) return 0;
    FILTER_EDICTS(beetle, beetle->inuse && !M_IsDead(beetle) && beetle->owner == caster &&
                  beetle->summon_ability && G_AbilityCode(beetle->summon_ability) == G_AbilityCode(code))
        count++;
    return count;
}

static bool carrion_scarab_corpse_valid(edict_t *caster, edict_t *corpse, abilityitem_t const *spell) {
    if (!caster || !corpse || !spell || !G_UnitIsRaisableCorpse(corpse) ||
        G_UnitIsHero(corpse) || G_UnitIsStructure(corpse) ||
        !S_SpellAllowsTarget(spell->code, caster, corpse)) return false;
    return carrion_scarab_count(caster, spell->code) <
        (uint32_t)MAX(0.0f, S_SpellData(spell->code, S_SpellLevel(caster, spell->code), 5));
}

static bool carrion_scarab_validate(edict_t *caster, spellTarget_t target, abilityitem_t const *spell) {
    return target.type == SPELL_TARGET_UNIT &&
        carrion_scarab_corpse_valid(caster, target.entity, spell);
}

static void carrion_scarab_execute(edict_t *caster, spellTarget_t target, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t beetle_id = S_SpellDataId(spell->code, level, 3); /* DataC */
    uint32_t limit = (uint32_t)MAX(0.0f, S_SpellData(spell->code, level, 5)); /* DataE */
    uint32_t remaining = limit > carrion_scarab_count(caster, spell->code) ?
        limit - carrion_scarab_count(caster, spell->code) : 0;
    uint32_t count = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1)); /* DataA */
    edict_t *corpse = target.entity;
    vec2_t position;
    uint32_t spawned = 0;

    if (!carrion_scarab_corpse_valid(caster, corpse, spell) || !beetle_id || !remaining) return;
    position = corpse->s.origin2;
    count = MIN(count, remaining);
    FOR_LOOP(i, count) {
        edict_t *beetle = S_SummonAbilityAt(caster, spell->code, beetle_id, &position,
                                             S_SpellDuration(spell->code, level, false));
        if (beetle) {
            spawned++;
        }
    }
    if (spawned) G_FreeEdict(corpse);
}

static bool carrion_scarab_autocast_acquire(edict_t *caster, uint32_t code) {
    abilityitem_t spell = S_AbilityItem(code);
    edict_t *nearest = NULL;
    float best = FLT_MAX;
    float range;

    if (!caster || !spell.ability || carrion_scarab_count(caster, code) >=
        (uint32_t)MAX(0.0f, S_SpellData(code, S_SpellLevel(caster, code), 5))) return false;
    range = S_SpellRange(code, S_SpellLevel(caster, code));
    FILTER_EDICTS(corpse, G_UnitIsRaisableCorpse(corpse) && !G_UnitIsHero(corpse) &&
                  !G_UnitIsStructure(corpse) && S_SpellAllowsTarget(code, caster, corpse)) {
        float distance = Vector2_distance(&corpse->s.origin2, &caster->s.origin2);
        if (distance <= range && distance < best) {
            nearest = corpse;
            best = distance;
        }
    }
    return nearest && S_CastUnitTargetSpell(caster, code, nearest);
}

BZ_ABILITY_PROC(CAbilityCarrionScarabs) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    uint32_t code = call && call->item ? call->item->code : MAKEFOURCC('A','U','c','b');
    switch (msg) {
    case A_VALIDATE: return carrion_scarab_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: carrion_scarab_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->unit_state && ent->unit_state->autocast == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return carrion_scarab_autocast_acquire(ent, code);
    default: return CAbilityModalSpell(ent, msg, call);
    }
}
/* Name=Black Arrow
 * Ubertip="Adds bonus damage to attacks and summons a skeleton when an attacked unit dies."
 * Untip="Right-click to activate auto-casting."
 * Unubertip="Right-click to deactivate auto-casting."
 */
BZ_ABILITY_PROC(CAbilityBlackArrow) {
    uint32_t const code = call && call->item ? call->item->code : 0;
    bool const active = ent && ent->unit_state && ent->unit_state->autocast == code;
    switch (msg) {
    case A_TOGGLE_ON: return active;
    case A_AUTOCAST_ON: return active;
    case A_AUTOCAST_SET: return true;
    case A_STATUS_DEATH:
        return black_arrow_summon_on_death(ent, call ? call->status.slot : NULL);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}
/* Name=Poison Arrows
 * Ubertip="Adds <AHfa,DataA1> bonus fire damage to an attack against enemies, but drains mana with each shot fired."
 * Untip="Right-click to activate auto-casting."
 * Unubertip="Right-click to deactivate auto-casting."
 */
BZ_SIMPLE_SPELL_PROC(AbilityPoisonArrows) { toggle_status_execute(caster, st, spell); }
/* Name=Silence
 * Ubertip="Stops enemy units in an area from casting spells."
 */
BZ_SIMPLE_SPELL_PROC(AbilitySilence) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = spell_buff(spell, level);

    /* The ability's EffectArt is a one-shot ground burst; the buff's TargetArt
     * is persistent overhead art and belongs to the timed BNsi status. */
    G_SpawnAbilityEffectAtPoint(spell->code, WC3_EFFECT_EFFECT, 0, &st.point, true);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  S_SpellAllowsAreaTarget(spell->code, caster, target) &&
                  Vector2_distance(&target->s.origin2, &st.point) <= area) {
        heroabilitystatus_t *status = buff ? S_SpellApplyTimedStatusFromAbility(
            target, spell->code, level, buff, S_SpellHeroDuration(spell->code, level, target)) : NULL;
        if (status) {
            status->buff_flags |= WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL;
            unit_status_enabletargetart(target, status, "overhead");
        }
    }
}
/* Corpse ultimates prefer higher-level units before lower-level ones. Equal-level
 * ties retain the stable entity-enumeration order until a stricter retail tie-break
 * is established. */
static bool corpse_preferred(edict_t const *candidate, edict_t const *current, edict_t const *caster, bool nearest_tie) {
    int32_t candidate_level, current_level;

    if (!current) return true;
    candidate_level = G_CorpseUnitLevel(candidate); current_level = G_CorpseUnitLevel(current);
    if (candidate_level != current_level) return candidate_level > current_level;
    return nearest_tie && caster &&
        Vector2_distance(&candidate->s.origin2, &caster->s.origin2) <
        Vector2_distance(&current->s.origin2, &caster->s.origin2);
}

/* Name=Animate Dead
 * Ubertip="Raises a number of corpses to serve the caster for a limited time."
 */
static bool animate_dead_target(edict_t *caster, edict_t *unit, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    return G_UnitIsRaisableCorpse(unit) && !G_UnitIsHero(unit) && !G_UnitIsStructure(unit) &&
        Vector2_distance(&unit->s.origin2, &caster->s.origin2) <= area;
}

static bool animate_dead_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)st;
    FILTER_EDICTS(unit, animate_dead_target(caster, unit, spell)) return true;
    return false;
}

static void animate_dead_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code), count = 0;
    uint32_t limit = (uint32_t)S_SpellData(spell->code, level, 1);
    bool raised_invulnerable = S_SpellData(spell->code, level, 2) != 0.0f;
    float duration = S_SpellDuration(spell->code, level, false);
    (void)st;

    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
    while (count < limit) {
        edict_t *selected = NULL;
        FILTER_EDICTS(unit, animate_dead_target(caster, unit, spell))
            if (corpse_preferred(unit, selected, caster, false)) selected = unit;
        if (!selected) break;

        /* Animated Dead reuses the corpse handle but does not reactivate food.
         * The shared temporary-revival primitive retires decay/order ownership
         * and consumes the original corpse so the timed summon cannot be raised
         * again when it later dies or expires. */
        G_ReviveCorpseAsSummon(selected, 1.0f);
        /* Hre2 / ABILITY_BLF_RAISED_UNITS_ARE_INVULNERABLE is authored
         * per Resurrection-family ability. Grant it when requested without
         * forcibly clearing other invulnerability sources when it is false. */
        if (raised_invulnerable) G_SetUnitFlag(selected, UNIT_FLAG_INVULNERABLE, true);
        selected->s.player = caster->s.player; selected->owner = caster;
        selected->summon_ability = spell->code;
        S_SpellApplyTimedLife(selected, level, duration);
        selected->vertex_color = MAKE(color32_t, 255, 0, 0, 255);
        G_SetUnitFlag(selected, UNIT_FLAG_VERTEX_COLOR_SET, true);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_SPECIAL, 0, selected, NULL, true);
        count++;
    }
}
BZ_VALIDATED_SPELL_PROC(AbilityAnimateDead, animate_dead_validate, animate_dead_execute)
BZ_VALIDATED_SPELL_PROC(AbilityDeathCoil, death_coil_validate, death_coil_execute)
/* Name=Death Pact
 * Ubertip="Sacrifices a friendly undead unit to restore the Death Knight's life and mana."
 */
static bool death_pact_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t race = st.entity && st.entity->data.UnitData ? st.entity->data.UnitData->race : NULL;
    float mana_value = S_SpellData(spell->code, level, 1);
    float life_value = S_SpellData(spell->code, level, 2);
    bool mana_as_value = S_SpellData(spell->code, level, 3) != 0.0f;
    bool leave_target_alive = S_SpellData(spell->code, level, 5) != 0.0f;
    bool full_mana = caster->mana.value >= caster->mana.max_value;
    bool full_health = caster->health.value >= caster->health.max_value;

    if (!S_SpellIsAliveTarget(st.entity) || st.entity == caster || !race || strcmp(race, STR_UNDEAD) ||
        !S_SpellIsFriend(caster, st.entity) || G_UnitIsHero(st.entity)) return false;

    /* Warcraft's DataC value-mode has a deliberately odd target requirement:
     * a nonzero mana conversion may only target an Undead unit that currently
     * has mana. Preserve it so custom Death Pact-derived abilities match the
     * authored field contract rather than treating DataC as presentation-only. */
    if (mana_as_value && mana_value != 0.0f && st.entity->mana.value <= 0.0f) return false;

    /* Mirror the stock activation/resource gate. DataC/DataD change how the
     * conversion executes below, but the authored DataA/DataB fields still
     * decide which caster resources make the cast useful. */
    if (leave_target_alive) {
        if (mana_value != 0.0f && full_mana) {
            if (life_value != 0.0f) return !full_health;
            return false;
        }
        return mana_value != 0.0f || life_value != 0.0f;
    }
    if (life_value != 0.0f) {
        if (mana_value != 0.0f) return !(full_mana && full_health);
        return !full_health;
    }
    return mana_value != 0.0f && !full_mana;
}

static void death_pact_lose_life(edict_t *unit, float amount) {
    if (!unit || amount <= 0.0f || M_IsDead(unit)) return;
    G_SetHealth(unit, MAX(0.0f, unit->health.value - amount));
    /* Warsmash's negative-heal/value path goes through setLife(), which kills
     * a unit that reaches zero without attributing ordinary combat damage. */
    if (M_IsDead(unit) && !(unit->svflags & SVF_DEADMONSTER)) {
        if (unit->die) unit->die(unit, NULL);
        else unit_die(unit, NULL);
    }
}

static void death_pact_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float target_life = st.entity->health.value;
    float mana_value = S_SpellData(spell->code, level, 1);
    float life_value = S_SpellData(spell->code, level, 2);
    bool mana_as_value = S_SpellData(spell->code, level, 3) != 0.0f;
    bool life_as_value = S_SpellData(spell->code, level, 4) != 0.0f;
    bool leave_target_alive = S_SpellData(spell->code, level, 5) != 0.0f;
    float target_life_loss = 0.0f;

    if (life_value != 0.0f) {
        if (life_as_value) {
            target_life_loss += life_value;
            death_pact_lose_life(caster, life_value);
        } else {
            S_SpellHeal(caster, life_value * target_life);
        }
    }
    if (mana_value != 0.0f) {
        if (mana_as_value) {
            target_life_loss += mana_value;
            caster->mana.value = MAX(0.0f, caster->mana.value - mana_value);
        } else {
            caster->mana.value = MIN(caster->mana.max_value, caster->mana.value + mana_value * target_life);
        }
    }
    /* AUdp's CasterArt is the receive-health presentation. Play it after the
     * Death Knight's life/mana restoration, as Dark Ritual does for its caster. */
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);

    /* A non-leave-target-alive Death Pact is a sacrifice even when a fixed
     * DataC/DataD drain happens to reduce the victim to zero first. Mark the
     * corpse policy before applying that life loss so the death callback
     * cannot briefly create a raisable/decaying corpse. */
    if (!leave_target_alive) {
        st.entity->aiflags |= AI_CORPSE_UNRAISABLE | AI_CORPSE_NO_DECAY;
    }
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
    death_pact_lose_life(st.entity, target_life_loss);
    if (leave_target_alive || (st.entity->svflags & SVF_DEADMONSTER)) return;

    /* Death Pact is a sacrifice, not damage. It therefore bypasses damage
     * immunity/invulnerability and consumes the victim without leaving a corpse
     * for Resurrection, Animate Dead, Raise Dead, Cannibalize, etc. */
    if (st.entity->die) st.entity->die(st.entity, caster);
    else unit_die(st.entity, caster);
}
BZ_ABILITY_PROC(CAbilityDeathPact) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    if (msg == A_COMMAND) {
        edict_t *clent = call && call->entity ? call->entity : NULL;
        edict_t *caster = clent && clent->client ? G_GetMainSelectedUnit(clent->client) : ent;
        if (caster && caster->health.value >= caster->health.max_value) {
            G_ShowCommandErrorKey(clent, "HPmaxed", NULL);
            return true;
        }
    }
    switch (msg) {
    case A_VALIDATE: return death_pact_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: death_pact_execute(ent, target, call ? call->item : NULL); return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}
/* Name=Metamorphosis
 * Ubertip="Transforms the Demon Hunter into a powerful demon."
 */
#define ID_BEAR_FORM_ABILITY MAKEFOURCC('A','b','r','f')
#define ID_BEAR_FORM_NIGHT_ELF MAKEFOURCC('e','d','o','c')
#define ID_BEAR_FORM_BEAR MAKEFOURCC('e','d','c','m')
#define ID_DESTROYER_FORM MAKEFOURCC('A','a','v','e')
static void bear_form_morph_end(edict_t *unit) { unit_stand(unit); }
static umove_t bear_form_morph = { "morph", NULL, bear_form_morph_end, CAbilityMetamorphosis };
static umove_t bear_form_morph_alternate = { "morph alternate", NULL, bear_form_morph_end, CAbilityMetamorphosis };

static bool metamorphosis_ability(uint32_t code) {
    return G_AbilityCode(code) == MAKEFOURCC('A','E','m','e');
}

/* Destroyer Form is an irreversible, purchased upgrade of the existing unit.
 * The spell's DataA and UnitID describe the source/destination: unlike
 * Metamorphosis it must never enqueue a morph_end reversion thinker. */
static bool destroyer_form_cost(edict_t *caster, uint32_t code, uint32_t *target,
                                int32_t *gold, int32_t *lumber, int32_t *food) {
    AbilityData_t const *ability;
    UnitBalance_t const *source, *destination;
    gameClient_t *owner;
    uint32_t form;
    if (!caster || !caster->unit_state ||
        !(ability = G_AbilityData(code)) || !ability->id ||
        caster->class_id != ability->level[0].data[0].id ||
        !(form = ability->level[0].unitID) ||
        !(source = caster->data.UnitBalance) ||
        !(destination = G_UnitBalance(form)) ||
        !(owner = G_GetPlayerClientByNumber(caster->s.player)) ||
        owner->ps.number != caster->s.player ||
        !G_UnitAbilityResearchAvailable(caster, code)) return false;
    *target = form;
    *gold = MAX(0, destination->goldCost - source->goldCost);
    *lumber = MAX(0, destination->lumberCost - source->lumberCost);
    *food = MAX(0, destination->foodUsed - source->foodUsed);
    return owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] >= (uint32_t)*gold &&
           owner->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] >= (uint32_t)*lumber &&
           G_PlayerHasFoodFor(owner, *food);
}

/* The stock Obsidian Statue Morph sequence spawns Crumble2 and hands the
 * statue geoset over to the Destroyer at 24.3s/24.333s of its 23.333-25.333s
 * sequence. Keep the model grounded through that handoff, then rise for the
 * remainder of the animation. These are normalized sequence times so custom
 * model frame rates remain supported. */
#define DESTROYER_MORPH_GROUND_PROGRESS 0.4835f
static float destroyer_morph_progress(edict_t const *unit) {
    uint32_t const start = unit && unit->animation ? unit->animation->interval[0] : 0;
    uint32_t const end = unit && unit->animation ? unit->animation->interval[1] : 0;
    if (end <= start) return 1.0f;
    return MAX(0.0f, MIN(1.0f,
        ((float)unit->s.frame - (float)start) / (float)(end - start)));
}

static void destroyer_morph_end(edict_t *unit) {
    if (!unit) return;
    G_AddUnitAnimationProperties(unit, "alternate", true);
    unit->unitinfo.FlyHeight = unit->data.UnitData ? unit->data.UnitData->moveHeight : 0.0f;
    unit_stand(unit);
}

static void destroyer_morph_think(edict_t *unit) {
    float const progress = destroyer_morph_progress(unit);
    float const rise = MAX(0.0f, MIN(1.0f,
        (progress - DESTROYER_MORPH_GROUND_PROGRESS) /
        (1.0f - DESTROYER_MORPH_GROUND_PROGRESS)));
    float const height = unit->data.UnitData ? unit->data.UnitData->moveHeight : 0.0f;
    if (progress >= DESTROYER_MORPH_GROUND_PROGRESS) {
        /* Adding an animation tag re-resolves the current request. Preserve
         * the Morph clip and frame so the geoset transition does not restart
         * at the beginning or jump to Morph Alternate. */
        animation_t const *animation = unit->animation;
        uint32_t const frame = unit->s.frame;
        G_AddUnitAnimationProperties(unit, "alternate", true);
        unit->animation = animation;
        unit->s.frame = frame;
        unit->unitinfo.FlyHeight = height * rise;
    } else {
        unit->unitinfo.FlyHeight = 0.0f;
    }
}

static umove_t destroyer_form_morph = {
    "morph", destroyer_morph_think, destroyer_morph_end, CAbilityMetamorphosis
};

/* The Statue and Destroyer resolve to one model. Lock the untagged Morph
 * sequence while selecting alternate geosets for the destination's stand,
 * walk and attack. The animation-end owner restores normal unit orders. */
static void destroyer_form_play_morph(edict_t *unit) {
    animation_t const *morph;
    if (!unit) return;
    G_AddUnitAnimationProperties(unit, "alternate", false);
    unit->unitinfo.FlyHeight = 0.0f;
    unit_setmove(unit, &destroyer_form_morph);
    morph = unit->animation;
    if (!morph) {
        G_AddUnitAnimationProperties(unit, "alternate", true);
        unit->unitinfo.FlyHeight = unit->data.UnitData ? unit->data.UnitData->moveHeight : 0.0f;
        unit_stand(unit);
        return;
    }
}

static bool destroyer_form_execute(edict_t *caster, uint32_t code) {
    uint32_t target;
    int32_t gold, lumber, food;
    gameClient_t *owner;
    if (!destroyer_form_cost(caster, code, &target, &gold, &lumber, &food)) return false;
    owner = G_GetPlayerClientByNumber(caster->s.player);
    if (!G_TransformUnitType(caster, target)) return false;
    owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] -= (uint16_t)gold;
    owner->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] -= (uint16_t)lumber;
    caster->mana.value = 0.0f;
    destroyer_form_play_morph(caster);
    G_RefreshResourceBar(G_GetPlayerEntityByNumber(caster->s.player));
    return true;
}

static void AbilityMetamorphosis_Execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)st;
    if (spell->code == ID_DESTROYER_FORM) {
        destroyer_form_execute(caster, spell->code);
        return;
    }
    if (spell->code == ID_BEAR_FORM_ABILITY) {
        bool const entering_bear_form = caster->class_id != ID_BEAR_FORM_BEAR;
        uint32_t const target_form = entering_bear_form ? ID_BEAR_FORM_BEAR : ID_BEAR_FORM_NIGHT_ELF;
        if (!G_TransformUnitType(caster, target_form)) return;
        /* Both authored unit forms share the Morph/Morph Alternate sequences.
         * Rebind first so the transition runs on the newly selected form. */
        unit_stand(caster);
        unit_setmove(caster, entering_bear_form ? &bear_form_morph : &bear_form_morph_alternate);
        return;
    }
    uint32_t level = S_SpellLevel(caster, spell->code), form = S_SpellUnitId(spell->code, level), original = caster->class_id;
    float duration = S_SpellDuration(spell->code, level, true);
    float bonus_health = 0.0f;
    heroability_t saved_rank = { .code = spell->code, .level = level };
    if (!form || duration <= 0.0f) return;
    if (metamorphosis_ability(spell->code)) {
        G_InitUnitState(caster);
        FOR_LOOP(i, MAX_HERO_ABILITIES) {
            if (caster->unit_state->ranks[i].code == spell->code && caster->unit_state->ranks[i].level) {
                saved_rank = caster->unit_state->ranks[i];
                level = saved_rank.level;
                break;
            }
        }
        bonus_health = S_SpellData(spell->code, level, 5);
    }
    if (!G_TransformUnitType(caster, form)) return;
    if (metamorphosis_ability(spell->code)) {
        cstring_t buff = S_SpellBuffId(spell->code, level);
        if (!buff || strlen(buff) < 4) buff = "BEme";
        if (!S_SpellApplyTimedStatus(caster, buff, level, duration)) {
            /* A timer status is part of the visible morph lifecycle. Avoid
             * transforming or spending mana when its status slot is full. */
            G_TransformUnitType(caster, original);
            return;
        }
        static uint32_t const metamorphosis_abilities[] = {
            MAKEFOURCC('A','E','m','b'), MAKEFOURCC('A','E','i','m'), MAKEFOURCC('A','E','e','v')
        };
        uint32_t rank_slot = MAX_HERO_ABILITIES;
        FOR_LOOP(i, MAX_HERO_ABILITIES) {
            if (caster->unit_state->ranks[i].code == spell->code || !caster->unit_state->ranks[i].level) {
                rank_slot = i;
                break;
            }
        }
        if (rank_slot < MAX_HERO_ABILITIES) caster->unit_state->ranks[rank_slot] = saved_rank;
        FOR_LOOP(i, sizeof(metamorphosis_abilities) / sizeof(metamorphosis_abilities[0])) {
            if (!G_ActorHasSkill(caster, GetClassName(metamorphosis_abilities[i])))
                G_ActorAddSkill(caster, metamorphosis_abilities[i]);
        }
        /* Edmm reuses the Demon Hunter model; its alternate geosets and
         * attack/walk sequences are selected by the Required Animation Name. */
        G_AddUnitAnimationProperties(caster, "alternate", true);
    }
    if (bonus_health > 0.0f) G_ApplyTemporaryMaxHealthBonus(caster, bonus_health);
    edict_t *thinker = G_Spawn();
    thinker->owner = caster; thinker->resources = original;
    if (bonus_health > 0.0f) {
        thinker->class_id = G_AbilityCode(spell->code);
        thinker->velocity = bonus_health;
    }
    G_AddChannel(thinker);
    G_GetChannel(thinker)->owner_spawn_time = caster->spawn_time;
    thinker->spawn_time = G_Time() + (uint32_t)(duration * 1000.0f); thinker->think = morph_end;
}

BZ_ABILITY_PROC(CAbilityMetamorphosis) {
    if (msg == A_VALIDATE && call && call->item && call->item->code == ID_DESTROYER_FORM) {
        uint32_t target;
        int32_t gold, lumber, food;
        return CAbilitySimpleSpell(ent, msg, call) &&
            destroyer_form_cost(ent, call->item->code, &target, &gold, &lumber, &food);
    }
    if (msg == A_TOGGLE_ON && call && call->item && call->item->code == ID_BEAR_FORM_ABILITY)
        return ent && ent->class_id == ID_BEAR_FORM_BEAR;
    if (msg == A_VALIDATE && call && call->item && metamorphosis_ability(call->item->code)) {
        uint32_t level = S_SpellLevel(ent, call->item->code);
        cstring_t buff = S_SpellBuffId(call->item->code, level);
        wc3_status_apply_check_t check;
        if (!buff || strlen(buff) < 4) buff = "BEme";
        check = unit_status_checkapplication(ent, &(status_application_t){ .buff = buff, .level = level });
        return CAbilitySimpleSpell(ent, msg, call) &&
               (check == WC3_STATUS_APPLY_REUSE || check == WC3_STATUS_APPLY_FREE_SLOT);
    }
    if (msg == A_EXECUTE) {
        if (!call || !call->item) return false;
        AbilityMetamorphosis_Execute(ent, call->target ? *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE), call->item);
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}
/* Name=Sleep
 * Ubertip="Puts a target enemy unit to sleep."
 */
BZ_SIMPLE_SPELL_PROC(AbilitySleep) { target_status_execute(caster, st, spell); }

/* Neutral direct-damage bolt; unlike Fire Bolt it has no authored stun. */
BZ_ABILITY_PROC(CAbilityFingerOfDeath) {
    if (msg == A_VALIDATE)
        return call && call->target && call->target->entity &&
            S_SpellIsAliveTarget(call->target->entity) &&
            S_SpellIsEnemy(ent, call->target->entity);
    if (msg == A_EXECUTE && call && call->item && call->target && call->target->entity) {
        uint32_t level = S_SpellLevel(ent, call->item->code);
        S_SpellDamage(call->target->entity, ent, (int)S_SpellData(call->item->code, level, 1));
        G_SpawnAbilityEffectTarget(call->item->code, WC3_EFFECT_TARGET, 0, call->target->entity, NULL, true);
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}

/* Name=Inferno
 * Ubertip="Calls down an infernal that damages nearby enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDreadLordInferno) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    radial_damage_status(caster, st.point, spell, 1, false);
    S_SummonAbilityAt(caster, spell->code, S_SpellUnitId(spell->code, level), &st.point,
                      S_SpellData(spell->code, level, 2));
}
/* Name=Chain Lightning
 * Ubertip="Hurls a bolt of lightning that jumps between enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityChainLightning) {
    chain_lightning_execute(caster, st, spell);
}
/* Name=Forked Lightning
 * Ubertip="Strikes multiple enemy units with lightning."
 */
BZ_SIMPLE_SPELL_PROC(AbilityForkedLightning) {
    uint32_t const level = S_SpellLevel(caster, spell->code);
    uint32_t const max_hits = MIN(MAX_GROUP_SIZE,
        (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 2)));
    float const range = S_SpellRange(spell->code, level);
    float const width = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    if (!caster || !st.entity || range <= 0.0f) return;
    vec2_t const aim = Vector2_sub(&st.entity->s.origin2, &caster->s.origin2);
    float const aim_length = Vector2_len(&aim);
    if (aim_length <= 0.0f) return;
    vec2_t const direction = Vector2_scale(&aim, 1.0f / aim_length);
    edict_t *targets[MAX_GROUP_SIZE];
    float distances[MAX_GROUP_SIZE];
    uint32_t count = 0;

    /* Forked Lightning spreads from the caster toward the selected target.
     * Every struck unit must independently fit the authored cast range; the
     * old bounce handler instead allowed each hop to extend that range. */
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) &&
                  S_SpellIsEnemy(caster, target) &&
                  S_SpellAllowsTarget(spell->code, caster, target)) {
        vec2_t const offset = Vector2_sub(&target->s.origin2, &caster->s.origin2);
        float const along = Vector2_dot(&offset, &direction);
        float const across = fabsf(offset.x * direction.y - offset.y * direction.x);
        float const distance = Vector2_len(&offset);
        if (target == caster || along < 0.0f || distance > range ||
            along > range || across > width * (along / range)) continue;

        /* Keep the closest eligible units in the cone, up to the authored
         * target count, so edict iteration order does not decide the victims. */
        uint32_t insert = count;
        while (insert > 0 && distances[insert - 1] > distance) {
            if (insert < max_hits && insert < MAX_GROUP_SIZE) {
                distances[insert] = distances[insert - 1];
                targets[insert] = targets[insert - 1];
            }
            insert--;
        }
        if (insert < max_hits && insert < MAX_GROUP_SIZE) {
            distances[insert] = distance;
            targets[insert] = target;
            if (count < max_hits && count < MAX_GROUP_SIZE) count++;
        }
    }

    FOR_LOOP(i, count) {
        edict_t *target = targets[i];
        G_SpawnAbilityLightning(&(abilityLightningParams_t){
            .ability_id = spell->code, .index = 0,
            .source = caster, .target = target,
            .duration_ms = CHAIN_LIGHTNING_BOLT_MS,
        });
        S_SpellDamage(target, caster, (int)MAX(1.0f, S_SpellData(spell->code, level, 1)));
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_SPECIAL, 0, target, NULL, true);
    }
}
/* Name=Earthquake
 * Ubertip="Causes the earth to shake, damaging enemy buildings and slowing enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityEarthquake) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code); thinker->s.origin2 = st.point;
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(spell->code, level, false) * 1000.0f);
    thinker->freetime = G_Time() + (uint32_t)(MAX(0.0f, S_SpellData(spell->code, level, 1)) * 1000.0f);
    thinker->think = earthquake_think;
    S_SpellBeginAreaPresentation(thinker, spell->code, level, &st.point);
    earthquake_think(thinker);
}
/* Name=Far Sight
 * Ubertip="Reveals a specified area of the map."
 */
BZ_SIMPLE_SPELL_PROC(AbilityFarSight) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = G_Spawn();
    thinker->class_id = spell->code;
    thinker->s.player = caster->s.player;
    thinker->s.origin2 = st.point;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(spell->code, level, false) * 1000.0f);
    thinker->think = far_sight_think;
    S_SpellBeginAreaPresentation(thinker, spell->code, level, &st.point);
    far_sight_think(thinker);
}
/* Resurrection operates on nearby ordinary corpses; Heroes retain their separate altar revival lifecycle. */
static bool resurrection_target(edict_t *caster, edict_t *target, abilityitem_t const *spell) {
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, S_SpellLevel(caster, spell->code));
    return G_UnitIsRaisableCorpse(target) && !G_UnitIsHero(target) &&
        !G_UnitIsStructure(target) && S_SpellIsFriend(caster, target) &&
        Vector2_distance(&target->s.origin2, &caster->s.origin2) <= radius;
}

/* Reject empty casts before the shared pipeline commits mana and cooldown. */
static bool resurrection_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    FILTER_EDICTS(target, resurrection_target(caster, target, spell)) return true;
    return false;
}

/* Reuse each corpse's edict and retire its death animation/timer before restoring ordinary unit activity. */
static void resurrection_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t rank = S_SpellLevel(caster, spell->code), count = 0;
    uint32_t limit = (uint32_t)S_SpellData(spell->code, rank, 1);
    bool raised_invulnerable = S_SpellData(spell->code, rank, 2) != 0.0f;
    (void)st;
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
    while (count < limit) {
        edict_t *selected = NULL;
        FILTER_EDICTS(target, resurrection_target(caster, target, spell))
            if (corpse_preferred(target, selected, caster, true)) selected = target;
        if (!selected) break;
        G_ReviveCorpse(selected, 1.0f);
        if (raised_invulnerable) G_SetUnitFlag(selected, UNIT_FLAG_INVULNERABLE, true);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, selected, NULL, true);
        count++;
    }
}

BZ_VALIDATED_SPELL_PROC(AbilityResurrection, resurrection_validate, resurrection_execute)
/* Name=Breath of Fire
 * Ubertip="Breathes a cone of fire at enemy units, dealing <ANcf,DataA1> initial damage."
 */
BZ_SIMPLE_SPELL_PROC(AbilityBreathOfFire) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    S_SpellDamageEnemiesInRadius(caster, &st.point, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level),
                                 (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1)));
}
/* Name=Howl of Terror
 * Ubertip="Reduces the attack damage of nearby enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityHowlOfTerror) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = S_SpellBuffId(spell->code, level);
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  S_SpellAllowsAreaTarget(spell->code, caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= radius) {
        /* BNht owns the persistent HowlTarget model, not ANht's one-shot
         * caster burst. Let the status remove its effect on dispel, death,
         * replacement or expiry, and reuse it on refresh/recast. */
        if (buff && strlen(buff) >= 4)
            S_SpellApplyTimedTargetStatus(target, spell->code, level, buff,
                                          S_SpellHeroDuration(spell->code, level, target));
    }
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
}
/* Name=Drunken Haze
 * Ubertip="Slows enemy units and gives them a chance to miss on attacks."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDrunkenHaze) { target_status_execute(caster, st, spell); }
/* Doom is permanent until the victim dies. A_STATUS_TICK advances an authored
 * one-second damage clock even when status->timestamp == 0; this keeps damage,
 * spell locking, targeting and art owned by the victim's serialized status.
 * DataA = damage per second, DataC = summon timed life, DataD = creep level cap. */
#define DOOM_TICK_MS 1000u

static cstring_t doom_first_buff(uint32_t code, uint32_t rank) {
    cstring_t id = S_SpellBuffId(code, rank);
    return id && strlen(id) >= 4 ? S_SpellBuffToken(id, 0) : "BNdo";
}

static bool doom_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    UnitBalance_t const *balance;
    uint32_t rank;
    float max_creep;
    cstring_t buff;
    if (!caster || !spell || !target || target == caster ||
        !S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(caster, target) ||
        !S_SpellAllowsTarget(spell->code, caster, target) ||
        S_UnitIsResistant(target) || G_UnitIsStructure(target) ||
        target->targtype == TARG_MECHANICAL || (target->aiflags & AI_ILLUSION)) return false;
    rank = S_SpellLevel(caster, spell->code);
    max_creep = S_SpellData(spell->code, rank, 4);
    balance = target->data.UnitBalance;
    if (target->s.player == PLAYER_NEUTRAL_AGGRESSIVE && max_creep > 0.0f &&
        (!balance || balance->level > max_creep)) return false;
    buff = doom_first_buff(spell->code, rank);
    if (buff) {
        wc3_status_apply_check_t const check = unit_status_checkapplication(target, &(status_application_t){
            .buff = buff, .level = rank
        });
        return check != WC3_STATUS_APPLY_INVALID && check != WC3_STATUS_APPLY_FULL;
    }
    return false;
}

static void doom_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t rank = S_SpellLevel(caster, spell->code);
    heroabilitystatus_t *slot;
    cstring_t buff = doom_first_buff(spell->code, rank);
    if (!doom_validate(caster, st, spell)) return;
    slot = unit_applystatus(st.entity, &(status_application_t){
        .buff = buff, .level = rank, .source_ability = spell->code,
        .source = caster, .rank = rank,
        /* data stores owner player for caster-loss fallback; spell code is
         * already in source_ability and level in rank. */
        .data = caster->s.player,
        .state_mask = WC3_STATUS_STATE_SILENCED,
        .buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL |
                      WC3_STATUS_BUFF_UNDISPELLABLE | WC3_STATUS_BUFF_TARGET_ART
    });
    if (!slot) return;
    slot->next_tick = G_Time() + DOOM_TICK_MS;
    unit_status_invalidate_update_mask(st.entity);
    S_SpellCancelChannel(st.entity);
}

static void doom_tick(edict_t *victim, heroabilitystatus_t *slot) {
    edict_t *caster;
    uint32_t code, rank;
    int damage;
    if (!victim || !slot || !slot->level) return;
    code = slot->source_ability;
    rank = slot->rank;
    caster = S_SpellStatusSource(slot);
    damage = (int)MAX(0.0f, S_SpellData(code, rank, 1));
    while (unit_status_take_due_tick(slot, DOOM_TICK_MS, G_Time())) {
        if (damage > 0) {
            /* Doom persists if its original caster has died or been removed.
             * Fall back to ordinary damage without dereferencing stale owner. */
            if (caster) S_SpellDamage(victim, caster, damage);
            else T_Damage(victim, victim, damage);
        }
        if (M_IsDead(victim) || !slot->level) break;
    }
}

static void doom_death(edict_t *victim, heroabilitystatus_t *slot) {
    edict_t *caster = S_SpellStatusSource(slot), *summon = NULL;
    uint32_t const code = slot->source_ability;
    uint32_t const rank = slot->rank;
    uint32_t const player = slot->data;
    uint32_t const summon_id = S_SpellUnitId(code, rank);
    float const duration = MAX(0.0f, S_SpellData(code, rank, 3));
    cstring_t const buffs = S_SpellBuffId(code, rank);
    cstring_t const death_buff = buffs ? S_SpellBuffToken(buffs, 1) : NULL;
    vec2_t const point = victim->s.origin2;
    /* Invalidate before invoking spawn events or any other callbacks. */
    unit_removestatus_deferred(victim, slot, STATUS_REMOVE_DEATH);
    if (death_buff && strlen(death_buff) >= 4)
        G_SpawnAbilityEffectAtPoint(FS_SLKKey(death_buff), WC3_EFFECT_EFFECT, 0, &point, true);
    if (!summon_id) return;
    if (caster) summon = S_SummonAbilityAt(caster, code, summon_id, &point, duration);
    else if (player < MAX_PLAYERS) {
        /* A summoned Doom Guard retains its owner's player even when the
         * caster edict was freed and its slot reused. */
        summon = SP_SpawnAtLocation(summon_id, player, &point);
        if (summon) {
            summon->summon_ability = code;
            G_ActivateUnitFood(summon);
            if (summon->stand) summon->stand(summon);
            if (duration > 0.0f) S_SpellApplyTimedLife(summon, 1, duration);
        }
    }
}

/* Name=Doom
 * Ubertip="Curses a target enemy unit, preventing it from casting spells and damaging it over time."
 */
BZ_ABILITY_PROC(CAbilityDoom) {
    if (msg == A_STATUS_TICK && call && call->status.slot) {
        doom_tick(ent, call->status.slot);
        return true;
    }
    if (msg == A_STATUS_DEATH && call && call->status.slot) {
        doom_death(ent, call->status.slot);
        return true;
    }
    if (msg == A_VALIDATE || msg == A_EXECUTE) {
        spellTarget_t st = call && call->target ? *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
        if (msg == A_VALIDATE) return doom_validate(ent, st, call ? call->item : NULL);
        if (call && call->item) doom_execute(ent, st, call->item);
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}
/* Name=Healing Wave
 * Ubertip="Heals a target friendly unit and bounces to nearby friendlies, healing less each jump."
 */
BZ_SIMPLE_SPELL_PROC(AbilityHealingWave) {
    uint32_t level = S_SpellLevel(caster, spell->code), count = (uint32_t)S_SpellData(spell->code, level, 2);
    float amount = S_SpellData(spell->code, level, 1), loss = S_SpellData(spell->code, level, 3);
    edict_t *source = caster, *current = st.entity, *visited[32] = {0};
    FOR_LOOP(i, MIN(count ? count : 1, 32)) {
        if (!current || !S_SpellIsAliveTarget(current) || !S_SpellIsFriend(caster, current)) break;
        G_SpawnAbilityLightning(&(abilityLightningParams_t){
            .ability_id = spell->code, .index = i ? 1 : 0,
            .source = source, .target = current,
            .duration_ms = CHAIN_LIGHTNING_BOLT_MS,
        });
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, current, NULL, true);
        S_SpellHeal(current, amount); visited[i] = current; amount *= 1.0f - loss;
        current = NULL;
        FILTER_EDICTS(target, S_SpellBounceTargetAllowed(caster, spell->code, visited[i], target,
                                                        S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level), true)) {
            if (!S_SpellTargetVisited(visited, i + 1, target)) { current = target; break; }
        }
        source = visited[i];
    }
}
/* Name=Hex
 * Ubertip="Transforms an enemy unit into a random critter for <ANhx,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityHex) { target_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySpiritOfVengeance) { S_SummonAbilityUnits(caster, spell->code, NULL); }

/* Spirit of Vengeance (Avng) raises a temporary invulnerable spirit from a
 * corpse. The Avatar of Vengeance (AEsv) summon enables this spell's autocast
 * on creation, while the same ability remains manually targetable. */
static bool vengeance_corpse_valid(edict_t *caster, edict_t *corpse, abilityitem_t const *spell) {
    float range;
    if (!caster || !corpse || !spell || !G_UnitIsRaisableCorpse(corpse) ||
        G_UnitIsHero(corpse) || G_UnitIsStructure(corpse) ||
        !S_SpellAllowsCorpseTarget(spell->code, caster, corpse)) return false;
    range = S_SpellRange(spell->code, S_SpellLevel(caster, spell->code));
    return range <= 0.0f || Vector2_distance(&corpse->s.origin2, &caster->s.origin2) <= range;
}

static bool vengeance_validate(edict_t *caster, spellTarget_t target, abilityitem_t const *spell) {
    return target.type == SPELL_TARGET_UNIT &&
        vengeance_corpse_valid(caster, target.entity, spell);
}

static void vengeance_execute(edict_t *caster, spellTarget_t target, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *corpse = target.entity;
    uint32_t spirit_id = S_SpellDataId(spell->code, level, 3); /* DataC */
    edict_t *spirit;
    vec2_t position;
    if (!vengeance_corpse_valid(caster, corpse, spell) || !spirit_id) return;
    position = corpse->s.origin2;
    spirit = S_SummonAbilityAt(caster, spell->code, spirit_id, &position,
                               S_SpellDuration(spell->code, level, false));
    if (!spirit) return;
    if (S_SpellData(spell->code, level, 6) != 0.0f) /* DataF: invulnerable */
        G_SetUnitFlag(spirit, UNIT_FLAG_INVULNERABLE, true);
    /* The corpse is the source of the summon, not the summoned unit. */
    G_FreeEdict(corpse);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_SPECIAL, 0, spirit, NULL, true);
}

static bool vengeance_autocast_acquire(edict_t *caster, uint32_t code) {
    abilityitem_t spell = S_AbilityItem(code);
    edict_t *nearest = NULL;
    float best = FLT_MAX;
    if (!caster || !spell.ability) return false;
    FILTER_EDICTS(corpse, G_UnitIsRaisableCorpse(corpse) && !G_UnitIsHero(corpse) &&
                  !G_UnitIsStructure(corpse)) {
        float distance;
        if (!S_SpellAllowsCorpseTarget(code, caster, corpse)) continue;
        distance = Vector2_distance(&corpse->s.origin2, &caster->s.origin2);
        if (distance <= S_SpellRange(code, S_SpellLevel(caster, code)) && distance < best) {
            nearest = corpse;
            best = distance;
        }
    }
    return nearest && S_CastUnitTargetSpell(caster, code, nearest);
}

BZ_ABILITY_PROC(CAbilityVengeance) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    uint32_t code = call && call->item ? call->item->code : MAKEFOURCC('A','v','n','g');
    switch (msg) {
    case A_VALIDATE: return vengeance_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: vengeance_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->unit_state && ent->unit_state->autocast == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return vengeance_autocast_acquire(ent, code);
    default: return CAbilityModalSpell(ent, msg, call);
    }
}
BZ_SIMPLE_SPELL_PROC(AbilityVoodoo) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = spell_buff(spell, level);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    if (!buff) return;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        S_SpellApplyTimedStatus(target, buff, level, S_SpellDuration(spell->code, level, false));
}
BZ_SIMPLE_SPELL_PROC(AbilityAcidBomb) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = spell_buff(spell, level);
    edict_t *thinker;
    if (!st.entity || !S_SpellIsAliveTarget(st.entity)) return;
    if (buff) S_SpellApplyTimedStatus(st.entity, buff, level, S_SpellDuration(spell->code, level, false));
    thinker = G_Spawn(); thinker->owner = caster; thinker->goalentity = st.entity; thinker->class_id = spell->code;
    channel_t *cast = G_AddChannel(thinker);
    cast->owner_spawn_time = caster->spawn_time;
    cast->target_spawn_time = st.entity->spawn_time;
    thinker->damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 3));
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(spell->code, level, false) * 1000.0f); thinker->think = acid_bomb_think;
}

BZ_ABILITY_PROC(CAbilityFlamingArrows) {
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_AUTOCAST_ON: return ent && ent->unit_state && ent->unit_state->autocast == code;
    case A_AUTOCAST_SET: return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}
