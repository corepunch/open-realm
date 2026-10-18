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

typedef struct {
    edict_t *caster;
    spellTarget_t target;
    abilityitem_t const *spell;
    float scale;
    bool random_jumps;
} bounceParams_t;

bool S_UnitHasStatus(edict_t const *unit, uint32_t code) {
    if (!unit) return false;
    FOR_LOOP(i, G_UnitStatusSlotCount(unit))
        if (unit->abilstatus[i].level && G_UnitStatusClassCode(unit->abilstatus+i) == code &&
            (!unit->abilstatus[i].timestamp || unit->abilstatus[i].timestamp > G_Time())) return true;
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

static void radial_damage_status(edict_t *caster, vec2_t point, abilityitem_t const *spell, uint32_t data) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = spell_buff(spell, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
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
    if (!G_IsDestructable(target) || target->destructable->dead) return false;
    if (targets && *targets) {
        if (target->targtype == TARG_TREE && !S_SpellTargetHasToken(targets, "tree", NULL)) return false;
        if (target->targtype == TARG_DEBRIS && !S_SpellTargetHasToken(targets, "debris", NULL)) return false;
    }
    return Vector2_distance(&target->s.origin2, origin) <= radius;
}

float S_EarthquakeMoveReduction(edict_t const *unit) {
    uint32_t level = G_QueryUnitStatusClassLevel(unit, ID_EARTHQUAKE_BUFF);
    if (!level) return 0.0f;
    return MIN(1.0f, MAX(0.0f, S_SpellData(ID_EARTHQUAKE, level, 3)));
}

static uint32_t spell_effect_code(uint32_t code, uint32_t level) {
    cstring_t effect_id = G_AbilityLevel(code, level)->efctID;
    return effect_id && strlen(effect_id) >= 4 ? FS_SLKKey(effect_id) : code;
}

static edict_t *spell_begin_area_presentation(edict_t *owner, uint32_t code, uint32_t level,
                                               vec2_t const *point) {
    edict_t *effect;
    uint32_t effect_code = spell_effect_code(code, level);
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

static void spell_end_area_presentation(edict_t *owner) {
    G_DestroyOwnedEffects(owner);
}

void earthquake_think(edict_t *ent) {
    if (!S_SpellChannelActive(ent)) { spell_end_area_presentation(ent); S_SpellEndChannel(ent); return; }
    uint32_t level = S_SpellLevel(ent->owner, ent->class_id), now = G_Time();
    abilityitem_t item = S_AbilityItem(ent->class_id);
    abilityitem_t const *spell = &item;
    cstring_t buff = spell_buff(spell, level);
    cstring_t targets = G_AbilityLevel(ent->class_id, level)->targs;
    float radius = S_SpellNumber(ent->class_id, ABILITY_NUMBER_AREA, level);
    float damage = S_SpellData(ent->class_id, level, 2);
    if (now >= ent->spawn_time) { spell_end_area_presentation(ent); S_SpellEndChannel(ent); return; }
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
        spell_end_area_presentation(thinker);
        G_FreeEdict(thinker);
        return;
    }
    G_FowSetStateRadius(&(fogWrite_t){ thinker->s.player, WC3_FOG_STATE_VISIBLE, true },
                        &thinker->s.origin2, thinker->collision);
}

void whirlwind_think(edict_t *ent) {
    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    abilityitem_t item = S_AbilityItem(ent->class_id);
    uint32_t data = item.ability && item.ability->proc == CAbilityStampede ? 2 : 1;
    if (G_Time() >= ent->spawn_time) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && G_Time() < ent->freetime) return;
    if (item.ability && (item.ability->proc == CAbilityWhirlwind || item.ability->proc == CAbilityTornado))
        ent->s.origin2 = ent->owner->s.origin2;
    radial_damage_status(ent->owner, ent->s.origin2, &item, data);
    ent->freetime = G_Time() + 1000;
}

static void whirlwind_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(spell->code, level, true) * 1000.0f);
    thinker->think = whirlwind_think; whirlwind_think(thinker);
}

void morph_end(edict_t *thinker) {
    edict_t *owner = S_SpellChannelOwner(thinker);
    if (G_Time() < thinker->spawn_time) return;
    if (owner) G_TransformUnitType(owner, thinker->resources);
    G_FreeEdict(thinker);
}

int S_BlackArrowDamage(edict_t *attacker, int damage) {
    uint32_t level = G_UnitStatusClassLevel(attacker, MAKEFOURCC('A','N','b','a'));
    return level ? damage + (int)S_SpellData(MAKEFOURCC('A','N','b','a'), level, 1) : damage;
}

void S_BlackArrowDeath(edict_t *attacker, edict_t *target) {
    uint32_t level = G_UnitStatusClassLevel(attacker, MAKEFOURCC('A','N','b','a'));
    if (level && target && M_IsDead(target))
        S_SummonAbilityAt(attacker, MAKEFOURCC('A','N','b','a'),
                          S_SpellUnitId(MAKEFOURCC('A','N','b','a'), level), &target->s.origin2,
                          S_SpellData(MAKEFOURCC('A','N','b','a'), level, 3));
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

/* Resolve chained damage jumps while keeping target selection separate from spell metadata. */
static void bounce_execute(bounceParams_t const *params) {
    edict_t *caster = params->caster;
    spellTarget_t st = params->target;
    abilityitem_t const *spell = params->spell;
    float scale = params->scale;
    bool random_jumps = params->random_jumps;
    uint32_t level = S_SpellLevel(caster, spell->code), hits = (uint32_t)S_SpellData(spell->code, level, 2);
    float damage = S_SpellData(spell->code, level, 1);
    edict_t *current = st.entity, *visited[32] = {0};
    uint32_t nvisited = 0;
    FOR_LOOP(i, MIN(hits, 32)) {
        edict_t *candidates[MAX_GROUP_SIZE];
        uint32_t candidate_count = 0;
        if (!current) break;
        S_SpellDamage(current, caster, (int)MAX(1.0f, damage));
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, current, NULL, true);
        visited[nvisited++] = current; damage *= scale;
        FILTER_EDICTS(target, S_SpellBounceTargetAllowed(caster, spell->code, current, target,
                                                        S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level), false)) {
            if (!S_SpellTargetVisited(visited, nvisited, target) && candidate_count < MAX_GROUP_SIZE)
                candidates[candidate_count++] = target;
        }
        current = candidate_count ? candidates[random_jumps ? rand() % candidate_count : 0] : NULL;
    }
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
    if (!marker->channel) marker->channel = G_AllocChannel();
    assert(marker->channel);
    marker->channel->owner_spawn_time = thinker->spawn_time;
    S_SetMoveGoal(marker, &marker->goalentity, target);
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
    S_SetMoveGoal(thinker, &thinker->goalentity, next);
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
    if (!thinker->channel) thinker->channel = G_AllocChannel();
    assert(thinker->channel);
    thinker->channel->owner_spawn_time = caster->spawn_time;
    thinker->s.origin2 = st.entity->s.origin2;
    S_SetMoveGoal(thinker, &thinker->goalentity, st.entity);
    thinker->damage = st.entity->spawn_time;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->wait = damage * (1.0f - S_SpellData(spell->code, level, 3));
    thinker->velocity = 1.0f - S_SpellData(spell->code, level, 3);
    thinker->resources = hits - 1;
    thinker->freetime = G_Time() + CHAIN_LIGHTNING_JUMP_MS;
    thinker->think = chain_lightning_think;
    chain_lightning_mark_visited(thinker, st.entity);
}

void reincarnation_think(edict_t *thinker) {
    edict_t *owner = S_SpellChannelOwner(thinker);
    if (!owner) { G_FreeEdict(thinker); return; }
    if (G_Time() < thinker->spawn_time) return;
    if (M_IsDead(owner)) G_ReviveHero(owner, thinker->s.origin2.x, thinker->s.origin2.y);
    G_FreeEdict(thinker);
}

void S_ReincarnationOnDeath(edict_t *unit) {
    static uint32_t const codes[] = { MAKEFOURCC('A','O','r','e'), MAKEFOURCC('A','C','r','n'), MAKEFOURCC('A','N','r','n') };
    uint32_t code = 0, level = 0;
    edict_t *thinker;
    FOR_LOOP(i, sizeof(codes) / sizeof(*codes)) if ((level = G_UnitAbilityClassLevel(unit, codes[i]))) { code = codes[i]; break; }
    if (!level || !S_SpellCooldownReady(unit, code)) return;
    thinker = G_Spawn(); thinker->owner = unit; thinker->class_id = code; thinker->s.origin2 = unit->s.origin2;
    thinker->channel = G_AllocChannel();
    assert(thinker->channel);
    thinker->channel->owner_spawn_time = unit->spawn_time;
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
        FILTER_EDICTS(other, other != thinker && other->think == mass_teleport_think &&
                      other->goalentity == target && other->channel &&
                      other->channel->target_spawn_time == target->spawn_time) {
            edict_t *owner = S_SpellChannelOwner(other);
            if (owner && owner->channel && owner->channel->code == other->class_id &&
                owner->channel->serial == other->channel->serial && !M_IsDead(owner)) {
                newer_pause_owner = true;
                break;
            }
        }
        if (!newer_pause_owner) S_SetUnitPauseFlag(target,false);
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
        thinker->wait = target->paused ? 1.0f : 0.0f;
        thinker->freetime = G_Time() + (uint32_t)(delay * 1000.0f);
        thinker->think = mass_teleport_think;

        G_SpawnOwnedAbilityEffectAtPoint(thinker, spell->code, WC3_EFFECT_AREA_EFFECT, 0, &ent->s.origin2);
        G_SpawnOwnedAbilityEffectAtPoint(thinker, spell->code, WC3_EFFECT_AREA_EFFECT, 0, &target->s.origin2);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, ent, NULL, true);
        S_SetUnitPauseFlag(target,true);
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
BZ_SIMPLE_SPELL_PROC(AbilityTornado) { whirlwind_execute(caster, st, spell); }
/* Name=Banish
 * Ubertip="Turns a target unit ethereal, making it unable to attack or be attacked by physical attacks."
 */
BZ_SIMPLE_SPELL_PROC(AbilityBanish) { target_status_execute(caster, st, spell); }
/* Name=Phoenix
 * Ubertip="Summons a Phoenix to fight for the caster."
 */
BZ_SIMPLE_SPELL_PROC(AbilitySummonPhoenix) { S_SummonAbilityUnits(caster, spell->code, NULL); }
/* Name=Carrion Beetles
 * Ubertip="Raises carrion beetles from a nearby corpse."
 */
BZ_SIMPLE_SPELL_PROC(AbilityCarrionScarabs) {
    uint32_t level = S_SpellLevel(caster, spell->code), count = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    float range = S_SpellRange(spell->code, level);
    edict_t *corpse = NULL;
    FILTER_EDICTS(unit, G_UnitIsRaisableCorpse(unit) && !G_UnitIsHero(unit) &&
                  Vector2_distance(&unit->s.origin2, &caster->s.origin2) <= range) { corpse = unit; break; }
    if (!corpse) return;
    FOR_LOOP(i, count) S_SummonAbilityAt(caster, spell->code, S_SpellDataId(spell->code, level, 3),
                                          &corpse->s.origin2, S_SpellDuration(spell->code, level, false));
    G_FreeEdict(corpse);
}
/* Name=Impale
 * Ubertip="Slams the ground, impaling enemy units in a line and stunning them."
 */
BZ_SIMPLE_SPELL_PROC(AbilityImpale) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    vec2_t offset = Vector2_sub(&st.point, &caster->s.origin2);
    float distance = Vector2_distance(&caster->s.origin2, &st.point);
    vec2_t direction;
    cstring_t buff = spell_buff(spell, level);
    if (distance <= 0.0f) return;
    direction = Vector2_scale(&offset, 1.0f / distance);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target)) {
        vec2_t delta = Vector2_sub(&target->s.origin2, &caster->s.origin2);
        float along = Vector2_dot(&delta, &direction);
        float across = delta.x * direction.y - delta.y * direction.x;
        if (along < 0.0f || along > S_SpellData(spell->code, level, 1) ||
            fabsf(across) > S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level)) continue;
        S_SpellDamage(target, caster, (int)S_SpellData(spell->code, level, 3));
        if (!M_IsDead(target) && buff)
            S_SpellApplyTimedStatus(target, buff, level, S_SpellHeroDuration(spell->code, level, target));
    }
}
/* Name=Locust Swarm
 * Ubertip="Summons a swarm of locusts that damages enemy units and returns life to the caster."
 */
BZ_SIMPLE_SPELL_PROC(AbilityLocustSwarm) { S_SummonAbilityUnits(caster, spell->code, NULL); }
/* Name=Black Arrow
 * Ubertip="Adds bonus damage to attacks and summons a skeleton when an attacked unit dies."
 * Untip="Right-click to activate auto-casting."
 * Unubertip="Right-click to deactivate auto-casting."
 */
BZ_SIMPLE_SPELL_PROC(AbilityBlackArrow) { toggle_status_execute(caster, st, spell); }
/* Name=Poison Arrows
 * Ubertip="Adds <AHfa,DataA1> bonus fire damage to an attack against enemies, but drains mana with each shot fired."
 * Untip="Right-click to activate auto-casting."
 * Unubertip="Right-click to deactivate auto-casting."
 */
BZ_SIMPLE_SPELL_PROC(AbilityPoisonArrows) { toggle_status_execute(caster, st, spell); }
/* Name=Silence
 * Ubertip="Stops enemy units in an area from casting spells."
 */
static void silence_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = spell_buff(spell, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &st.point) <= area) {
        if (buff) S_ApplyAttackPrevention(caster, target, spell, buff,
            S_SpellHeroDuration(spell->code, level, target));
    }
}

BZ_ABILITY_PROC(CAbilitySilence) {
    if (msg == A_EXECUTE) {
        if (call && call->item && call->target) silence_execute(ent, *call->target, call->item);
        return true;
    }
    return CAbilityAttackPrevention(ent, msg, call);
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
        if (raised_invulnerable) selected->invulnerable = true;
        selected->s.player = caster->s.player; selected->owner = caster;
        selected->summon_ability = spell->code;
        S_SpellApplyTimedLife(selected, level, duration);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, selected, NULL, true);
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

    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
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
BZ_VALIDATED_SPELL_PROC(AbilityDeathPact, death_pact_validate, death_pact_execute)
/* Name=Metamorphosis
 * Ubertip="Transforms the Demon Hunter into a powerful demon."
 */
/* Ground timed forms use the common primary transition owner. Forward and
 * inverse animations are presentation; neither can release the counted lock. */
static bool metamorphosis_physical_held(edict_t *unit,uint32_t code) {
    abilityitem_t item=S_AbilityItem(code);
    return item.ability && !strcmp(item.ability->classname,"AEme") && S_UnitExecutionPaused(unit);
}
static void metamorphosis_animation_end(edict_t *unit) {
    if(!S_HoldMorphAnimation(unit) && !metamorphosis_physical_held(unit,unit->morph ? unit->morph->code : 0))unit_stand(unit);
}
static umove_t metamorphosis_forward={"morph",NULL,metamorphosis_animation_end,CAbilityMetamorphosis};
static umove_t metamorphosis_reverse={"morph alternate",NULL,metamorphosis_animation_end,CAbilityMetamorphosis};

static bool metamorphosis_timed_ground(abilityitem_t const *item) {
    return item && item->ability && (!strcmp(item->ability->classname,"AEme") ||
                                     !strcmp(item->ability->classname,"ANcr"));
}
static uint32_t const metamorphosis_buff=MAKEFOURCC('B','E','m','e');
static bool metamorphosis_is_chemical(abilityCall_t const *call) {
    return !strcmp(call->item->ability->classname,"ANcr");
}
static bool metamorphosis_owned_buff(edict_t *unit,heroabilitystatus_t const *status,uint32_t code) {
    return status && status->level && status->class_code==metamorphosis_buff &&
        status->source_ability==code && status->source==unit && status->source_spawn_time==unit->spawn_time;
}
static edict_t *metamorphosis_timer_heap[MAX_ENTITIES];
static uint32_t metamorphosis_timer_positions[MAX_ENTITIES],metamorphosis_timer_count;

static heroabilitystatus_t *metamorphosis_timer_next(edict_t const *unit) {
    heroabilitystatus_t *best=NULL;
    FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *s=unit->abilstatus+i;
        if(!s->level || s->class_code!=metamorphosis_buff || !s->class_timer.active)continue;
        abilityPrimaryTimer_t *a=&s->class_timer,*b=best ? &best->class_timer : NULL;
        if(!b || a->deadline.time<b->deadline.time ||
           (a->deadline.time==b->deadline.time && a->sequence<b->sequence))best=s;
    }
    return best;
}
static bool metamorphosis_timer_less(edict_t const *a,edict_t const *b) {
    abilityPrimaryTimer_t *x=&metamorphosis_timer_next(a)->class_timer,*y=&metamorphosis_timer_next(b)->class_timer;
    return x->deadline.time==y->deadline.time ? x->sequence<y->sequence : x->deadline.time<y->deadline.time;
}
static void metamorphosis_timer_put(uint32_t pos,edict_t *unit) {
    metamorphosis_timer_heap[pos]=unit;metamorphosis_timer_positions[unit-g_edicts]=pos+1;
}
void S_ForgetMetamorphosisOwner(edict_t *unit) {
    uint32_t pos=metamorphosis_timer_positions[unit-g_edicts];
    if(!pos)return;
    metamorphosis_timer_positions[unit-g_edicts]=0;pos--;
    edict_t *last=metamorphosis_timer_heap[--metamorphosis_timer_count];
    if(pos==metamorphosis_timer_count)return;
    while(pos && metamorphosis_timer_less(last,metamorphosis_timer_heap[(pos-1)/2])) {
        metamorphosis_timer_put(pos,metamorphosis_timer_heap[(pos-1)/2]);pos=(pos-1)/2;
    }
    while(pos*2+1<metamorphosis_timer_count) {
        uint32_t child=pos*2+1;
        if(child+1<metamorphosis_timer_count && metamorphosis_timer_less(metamorphosis_timer_heap[child+1],metamorphosis_timer_heap[child]))child++;
        if(!metamorphosis_timer_less(metamorphosis_timer_heap[child],last))break;
        metamorphosis_timer_put(pos,metamorphosis_timer_heap[child]);pos=child;
    }
    metamorphosis_timer_put(pos,last);
}
static void metamorphosis_timer_insert(edict_t *unit) {
    if(!unit || !unit->inuse)return;
    if(!metamorphosis_timer_next(unit) || metamorphosis_timer_positions[unit-g_edicts])return;
    uint32_t pos=metamorphosis_timer_count++;
    while(pos && metamorphosis_timer_less(unit,metamorphosis_timer_heap[(pos-1)/2])) {
        metamorphosis_timer_put(pos,metamorphosis_timer_heap[(pos-1)/2]);pos=(pos-1)/2;
    }
    metamorphosis_timer_put(pos,unit);
}
static void metamorphosis_timer_schedule(heroabilitystatus_t *slot,wc3Clock_t now,float duration) {
    slot->class_timer=(abilityPrimaryTimer_t){.deadline=now,.sequence=++level.timer_sequence,.active=true};
    slot->class_timer.deadline.time=wc3_add(now.time,MAX(duration,G_ClockMinimumDelay()));
}

/* Public Pause owns a separate Ability48 contribution. Native382W proves
 * Unpause resumes these clocks while physicalSleep Unit54 remains positive;
 * neither ordinary3c/40 disabling nor internal suspension owns this clock. */
void S_SetMetamorphosisClocksPaused(edict_t *unit,bool paused) {
    if(!unit)return;
    S_ForgetMetamorphosisOwner(unit);
    wc3Clock_t now=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
    FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *status=unit->abilstatus+i;
        if(!status->level || status->class_code!=metamorphosis_buff)continue;
        if(paused && !status->class_clock_depth) {
            status->class_clock_depth=1;
            status->class_timer_remaining=status->class_timer.active ? MAX(0,wc3_elapsed(&status->class_timer.deadline,&now)) : 0;
            status->class_timer.active=false;
        } else if(!paused && status->class_clock_depth) {
            status->class_clock_depth=0;
            if(status->class_timer_remaining>0)metamorphosis_timer_schedule(status,now,status->class_timer_remaining);
            status->class_timer_remaining=0;
        }
    }
    metamorphosis_timer_insert(unit);
    abilityitem_t source=S_AbilityItem(unit->morph ? unit->morph->code : 0);
    if(source.ability && !strcmp(source.ability->classname,"AEme") && G_ActorHasAbilityCode(unit,source.code))
        S_SetMorphExpirationPaused(unit,paused);
}
static void metamorphosis_remove_buff(edict_t *unit,uint32_t code) {
    FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *status=unit->abilstatus+i;
        if(metamorphosis_owned_buff(unit,status,code))unit_removestatus(unit,status,STATUS_REMOVE_SCRIPT);
    }
}
static edict_t *metamorphosis_creating; /* Synchronous concrete forward callback. */
static bool metamorphosis_create_buff(edict_t *unit,uint32_t code,bool initial) {
    uint32_t rank=S_SpellLevel(unit,code);
    abilityLevel_t const *data=G_AbilityLevel(code,rank);
    cstring_t token=S_SpellBuffToken(data->buffID,0);
    uint32_t buff=token ? FS_SLKKey(token) : metamorphosis_buff;
    if(!buff)buff=metamorphosis_buff;
    char name[5]={0};memcpy(name,&buff,4);
    status_application_t app={.buff=name,.class_code=metamorphosis_buff,.level=1,
        .source_ability=code,.source=unit,.rank=rank};
    if(data->heroDur>0 && unit_status_checkapplication(unit,&app)==WC3_STATUS_APPLY_FULL) {
        bool replaceable=false;
        FOR_LOOP(i,G_UnitStatusSlotCount(unit))
            if(unit->abilstatus[i].level && unit->abilstatus[i].class_code==metamorphosis_buff)replaceable=true;
        if(!replaceable)return false;
    }
    /*62dff0 retires any prior concreteBEme before attaching its successor.
     * BuffData.code and raw public BuffID cannot choose the concrete class. */
    edict_t *previous_creation=metamorphosis_creating;metamorphosis_creating=unit;
    FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *existing=unit->abilstatus+i;
        if(existing->level && existing->class_code==metamorphosis_buff)
            unit_removestatus(unit,existing,STATUS_REMOVE_REPLACED);
    }
    metamorphosis_creating=previous_creation;
    if(data->heroDur<=0)return true;
    heroabilitystatus_t *status=unit_applystatus(unit,&app);
    if(!status)return false;
    S_ForgetMetamorphosisOwner(unit);
    float duration=wc3_add(data->heroDur,.01f);
    status->duration_ms=(uint32_t)(duration*1000);
    metamorphosis_timer_schedule(status,G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL),duration);
    metamorphosis_timer_insert(unit);
    S_AttachStatusAbilityAvailability(unit,buff,metamorphosis_buff);
    if(initial)for(unitAbilityDisable_t const *owner=unit->ability_disables;owner;owner=owner->next)
        if(!owner->class_code && owner->code==code && owner->morph_owned) {
            S_AdjustAbilityAvailability(unit,buff,metamorphosis_buff,false,true);break;
        }
    if(unit->paused)S_SetMetamorphosisClocksPaused(unit,true);
    return true;
}
static void metamorphosis_timer_fire(void) {
    edict_t *unit=metamorphosis_timer_heap[0];
    heroabilitystatus_t *status=metamorphosis_timer_next(unit);
    S_ForgetMetamorphosisOwner(unit);status->class_timer.active=false;
    unit_removestatus(unit,status,STATUS_REMOVE_EXPIRE);
    metamorphosis_timer_insert(unit);
}
BZ_ABILITY_PROC(CBuffMetamorphosis) {
    switch(msg) {
    case A_UNIT_EVENT_MASK:return UNIT_MESSAGE_SUBSCRIPTIONS(A_UNIT_REMOVING,A_UNIT_REMOVE);
    case A_UNIT_REMOVING:case A_UNIT_REMOVE:
        if(ent)S_ForgetMetamorphosisOwner(ent);
        return true;
    case A_TIMERS_RESET:
        metamorphosis_timer_count=0;memset(metamorphosis_timer_positions,0,sizeof(metamorphosis_timer_positions));return true;
    case A_TIMERS_REBUILD:
        metamorphosis_timer_count=0;memset(metamorphosis_timer_positions,0,sizeof(metamorphosis_timer_positions));
        FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse)metamorphosis_timer_insert(g_edicts+i);
        return true;
    case A_PRIMARY_TIMER_NEXT:
        if(!metamorphosis_timer_count || !call || !call->primary_timer)return false;
        *call->primary_timer=(abilityTimerRequest_t){metamorphosis_timer_next(metamorphosis_timer_heap[0])->class_timer.deadline,
            metamorphosis_timer_next(metamorphosis_timer_heap[0])->class_timer.sequence,CBuffMetamorphosis};return true;
    case A_PRIMARY_TIMER_FIRE:
        if(metamorphosis_timer_count)metamorphosis_timer_fire();
        return true;
    case A_PRIMARY_TIMER_REBASE:
        FOR_LOOP(i,metamorphosis_timer_count)FOR_LOOP(j,G_UnitStatusSlotCount(metamorphosis_timer_heap[i])) {
            heroabilitystatus_t *status=metamorphosis_timer_heap[i]->abilstatus+j;
            abilityPrimaryTimer_t *t=&status->class_timer;
            if(status->class_code==metamorphosis_buff && t->active) {t->deadline.time=wc3_sub(t->deadline.time,call->clock_span);t->deadline.epoch++;}
        }
        return true;
    case A_PRIMARY_TIMER:
        if(!level.scheduled_frame)while(metamorphosis_timer_count) {
            wc3Clock_t now=G_TimerQueryClock(NULL);
            if(metamorphosis_timer_next(metamorphosis_timer_heap[0])->class_timer.deadline.time>now.time)break;
            metamorphosis_timer_fire();
        }
        return true;
    default:return false;
    }
}
/* Pools and source F_EDICT fixups must be restored before this cross-check.
 * Concreteclass is explicit; BuffData.code does not participate. */
bool S_ValidateMetamorphosisOwners(void) {
    FOR_LOOP(n,globals.num_edicts) {
        edict_t *unit=g_edicts+n;if(!unit->inuse)continue;
        FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
            heroabilitystatus_t const *status=unit->abilstatus+i;
            if(!status->class_code) {
                if(status->class_timer.active || status->class_timer_remaining || status->class_clock_depth)return false;
                continue;
            }
            if(status->class_code!=metamorphosis_buff || !status->level || !status->code ||
               status->source!=unit || status->source_spawn_time!=unit->spawn_time ||
               !G_ActorHasAbilityCode(unit,status->source_ability))return false;
            abilityitem_t item=S_AbilityItem(status->source_ability);
            if(!item.ability || strcmp(item.ability->classname,"AEme"))return false;
            if(!isfinite(status->class_timer_remaining) || status->class_timer_remaining<0 ||
               status->class_clock_depth<0 || status->class_clock_depth>1 ||
               (status->class_clock_depth && status->class_timer.active) ||
               (!status->class_clock_depth && status->class_timer_remaining))return false;
            if(status->class_timer.active && (!isfinite(status->class_timer.deadline.time) ||
               !status->class_timer.sequence || status->class_timer.sequence>level.timer_sequence))return false;
            unsigned owners=0;
            for(unitAbilityDisable_t const *owner=unit->ability_disables;owner;owner=owner->next)
                if(owner->code==status->code && owner->class_code==status->class_code)owners++;
            if(owners!=1)return false;
        }
        for(unitAbilityDisable_t const *owner=unit->ability_disables;owner;owner=owner->next) {
            if(!owner->class_code)continue;
            if(owner->class_code!=metamorphosis_buff || owner->morph_owned || owner->exclusive_owned ||
               owner->exclusive_inventory || owner->ethereal_initialized || owner->ethereal_owned ||
               owner->burrow_initialized || owner->burrow_owned)return false;
            unsigned statuses=0;
            FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
                heroabilitystatus_t const *status=unit->abilstatus+i;
                if(status->level && status->code==owner->code && status->class_code==owner->class_code)statuses++;
            }
            if(statuses!=1)return false;
        }
        if(unit->morph && (unit->morph->expiration_remaining || unit->morph->expiration_paused)) {
            abilityitem_t item=S_AbilityItem(unit->morph->code);
            if(!item.ability || strcmp(item.ability->classname,"AEme") ||
               !G_ActorHasAbilityCode(unit,unit->morph->code) ||
               !isfinite(unit->morph->expiration_remaining) || unit->morph->expiration_remaining<0 ||
               !unit->morph->expiration_paused || unit->morph->expiration.active)return false;
        }
    }
    return true;
}
static bool metamorphosis_begin(edict_t *unit,uint32_t code,bool reverse) {
    abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(unit,code));
    uint32_t base=reverse && unit->morph ? unit->morph->base_type : unit->class_id;
    uint32_t target=reverse ? base : data->unitID;
    if(!target || target==unit->class_id)return false;
    bool physical_held=metamorphosis_physical_held(unit,code);
    if(!physical_held)order_stop_cleanup(unit);
    unit_setmove(unit,reverse ? &metamorphosis_reverse : &metamorphosis_forward);
    if(!physical_held)unit->current_order_id=S_SpellOrderId(unit,code);
    S_BeginMorphTransition(unit,code,base,target,reverse,true,data->dur);
    return true;
}
/* ChemicalRage55d380 enters552120 synchronously when attached to its authored
 * alternate form. The concrete producer starts its authored lifetime here,
 * without a transition hold. Existing state distinguishes a type refresh. */
static void timed_ground_initialize_lifetime(edict_t *unit,uint32_t code) {
    if(!unit)return;
    abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(unit,code));
    if(!data->unitID || unit->class_id!=data->unitID || (unit->morph && unit->morph->code==code))return;
    if(!unit->morph)unit->morph=G_AllocMorph();
    assert(unit->morph);
    unit->morph->code=code;unit->morph->base_type=data->data[0].id;unit->morph->target_type=data->unitID;
    abilityitem_t item=S_AbilityItem(code);
    bool chemical=item.ability && !strcmp(item.ability->classname,"ANcr");
    if(!chemical) {
        /* The attaching walk contributes execution without orders before the
         * final inherited1,1 store: initial configured AEme ends at1,0. */
        for(unitAbilityDisable_t const *owner=unit->ability_disables;owner;owner=owner->next)
            if(!owner->class_code && owner->code==code && owner->morph_owned) {
                S_AdjustAbilityAvailability(unit,code,0,true,false);break;
            }
        if(!metamorphosis_create_buff(unit,code,true))return;
    }
    S_ScheduleMorphExpiration(unit,data->heroDur);
}
BZ_ABILITY_PROC(CAbilityMetamorphosis) {
    if(!call || !call->item)return false;
    /* Preserve the conversion-owner identity used by AI while sharing the
     * reversible ground transition and public-order dispatch. */
    if(call->item->ability && !strcmp(call->item->ability->classname,"ANrg"))
        return CAbilityGroundMorph(ent,msg,call);
    uint32_t code=call->item->code;
    if(!metamorphosis_timed_ground(call->item))return CAbilityAvengerForm(ent,msg,call);
    bool chemical=metamorphosis_is_chemical(call);
    switch(msg) {
    case A_UNIT_EVENT_MASK:
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_COMMAND,A_VALIDATE,A_DISABLE,A_MOVE_LEAVE,A_TARGET_MODE_EXIT,
            A_ENABLE,A_UNIT_INIT,A_UNIT_TYPE_CHANGED);
    case A_ENABLE:case A_UNIT_INIT:case A_UNIT_TYPE_CHANGED:
        S_InitializeConfiguredMorphAvailability(ent,call);
        timed_ground_initialize_lifetime(ent,code);return true;
    case A_VALIDATE:
        return ent && !(ent->morph && ent->morph->transition.active) &&
            G_AbilityLevel(code,S_SpellLevel(ent,code))->unitID &&
            G_AbilityLevel(code,S_SpellLevel(ent,code))->unitID!=ent->class_id;
    case A_EXECUTE:return metamorphosis_begin(ent,code,false);
    case A_MORPH_COMPLETE:
        S_CompleteConfiguredMorphAvailability(ent,call);
        if(!chemical && !call->enabled && !metamorphosis_create_buff(ent,code,false))return false;
        if(!call->enabled)S_ScheduleMorphExpiration(ent,S_SpellDuration(code,S_SpellLevel(ent,code),true));
        if(chemical || !S_UnitExecutionPaused(ent))unit_stand(ent);
        return true;
    case A_MORPH_EXPIRE:
        if(!chemical) {
            /* Native sets10000 before removing BEme; that notification must
             * not turn natural expiry into the public immediate inverse. */
            if(ent->morph && ent->morph->code==code)ent->morph->reverse=true;
            metamorphosis_remove_buff(ent,code);
        }
        return metamorphosis_begin(ent,code,chemical || ent->class_id!=ent->morph->base_type);
    case A_STATUS_REMOVE:case A_STATUS_REPLACE:
        if(!chemical && metamorphosis_owned_buff(ent,call->status.slot,code)) {
            S_ForgetMetamorphosisOwner(ent);
            call->status.slot->class_timer.active=false;
            S_DetachStatusAbilityAvailability(ent,call->status.slot->code,metamorphosis_buff);
            metamorphosis_timer_insert(ent);
        }
        return true;
    case A_STATUS_DEATH:
        if(!chemical && metamorphosis_owned_buff(ent,call->status.slot,code))
            unit_removestatus(ent,call->status.slot,STATUS_REMOVE_DEATH);
        return true;
    case A_STATUS_REMOVED:
        if(!chemical && ent && !M_IsDead(ent) && !G_IsDeferredFree(ent) &&
           metamorphosis_creating!=ent && metamorphosis_owned_buff(ent,call->status.removed,code) &&
           G_ActorHasAbilityCode(ent,code) && ent->morph && ent->morph->code==code &&
           !ent->morph->reverse && !ent->morph->transition.active) {
            /*647a00/5698a0 inverse cancels the live request, but does not
             * erase194/198 captured by an earlier private-clock suspension. */
            float remaining=ent->morph->expiration_remaining;
            bool paused=ent->morph->expiration_paused;
            if(!S_UnitExecutionPaused(ent))order_stop_cleanup(ent);
            S_DetachConfiguredMorphAvailability(ent,call);
            if(!S_UnitExecutionPaused(ent))unit_stand(ent);
            ent->morph->expiration_remaining=remaining;ent->morph->expiration_paused=paused;
        }
        return true;
    case A_DISABLE:
        if(!chemical && ent && !M_IsDead(ent) && !G_IsDeferredFree(ent)) {
            S_CompleteMorphDetachTransition(ent,code);
            if(ent->morph && ent->morph->code==code)ent->morph->reverse=true;
            metamorphosis_remove_buff(ent,code);
        }
        S_DetachConfiguredMorphAvailability(ent,call);
        return true;
    default:return CAbilitySimpleSpell(ent,msg,call);
    }
}
/* Name=Sleep
 * Ubertip="Puts a target enemy unit to sleep."
 */
/* Concrete Sleep and initial-guard owners are implemented in s_sleep.c. */

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
    radial_damage_status(caster, st.point, spell, 1);
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
    bounceParams_t params = { .caster = caster, .target = st, .spell = spell,
        .scale = 1.0f, .random_jumps = false };
    bounce_execute(&params);
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
    spell_begin_area_presentation(thinker, spell->code, level, &st.point);
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
    spell_begin_area_presentation(thinker, spell->code, level, &st.point);
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
        if (raised_invulnerable) selected->invulnerable = true;
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
    float duration = S_SpellDuration(spell->code, level, false);
    cstring_t buff = S_SpellBuffId(spell->code, level);
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= radius)
        if (buff && strlen(buff) >= 4) S_SpellApplyTimedStatus(target, buff, level, duration);
}
/* Name=Drunken Haze
 * Ubertip="Slows enemy units and gives them a chance to miss on attacks."
 */
BZ_ABILITY_PROC(CAbilityDrunkenHaze) {
    if (msg == A_EXECUTE) {
        if (call && call->item && call->target && call->target->entity) {
            uint32_t rank = S_SpellLevel(ent, call->item->code);
            cstring_t buff = spell_buff(call->item, rank);
            S_ApplyAttackPrevention(ent, call->target->entity, call->item, buff,
                S_SpellHeroDuration(call->item->code, rank, call->target->entity));
        }
        return true;
    }
    return CAbilityAttackPrevention(ent, msg, call);
}
/* Name=Doom
 * Ubertip="Curses a target enemy unit, preventing it from casting spells and damaging it over time."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDoom) { target_status_execute(caster, st, spell); }
/* Name=Healing Wave
 * Ubertip="Heals a target friendly unit and bounces to nearby friendlies, healing less each jump."
 */
BZ_SIMPLE_SPELL_PROC(AbilityHealingWave) {
    uint32_t level = S_SpellLevel(caster, spell->code), count = (uint32_t)S_SpellData(spell->code, level, 2);
    float amount = S_SpellData(spell->code, level, 1), loss = S_SpellData(spell->code, level, 3);
    edict_t *current = st.entity, *visited[32] = {0};
    FOR_LOOP(i, MIN(count ? count : 1, 32)) {
        if (!current || !S_SpellIsAliveTarget(current) || !S_SpellIsFriend(caster, current)) break;
        S_SpellHeal(current, amount); visited[i] = current; amount *= 1.0f - loss;
        current = NULL;
        FILTER_EDICTS(target, S_SpellBounceTargetAllowed(caster, spell->code, visited[i], target,
                                                        S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level), true)) {
            if (!S_SpellTargetVisited(visited, i + 1, target)) { current = target; break; }
        }
    }
}
/* Name=Hex
 * Ubertip="Transforms an enemy unit into a random critter for <ANhx,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityHex) { target_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySpiritOfVengeance) { S_SummonAbilityUnits(caster, spell->code, NULL); }
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
    thinker = G_Spawn(); thinker->owner = caster; S_SetMoveGoal(thinker, &thinker->goalentity, st.entity); thinker->class_id = spell->code;
    thinker->channel = G_AllocChannel();
    assert(thinker->channel);
    thinker->channel->owner_spawn_time = caster->spawn_time;
    thinker->channel->target_spawn_time = st.entity->spawn_time;
    thinker->damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 3));
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(spell->code, level, false) * 1000.0f); thinker->think = acid_bomb_think;
}

BZ_SIMPLE_SPELL_PROC(AbilityFlamingArrows) {
    toggle_status_execute(caster, st, spell);
}
