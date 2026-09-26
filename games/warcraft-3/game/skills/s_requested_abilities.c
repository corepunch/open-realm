#include "s_skills.h"

#define ID_EARTHQUAKE MAKEFOURCC('A', 'O', 'e', 'q')
#define ID_EARTHQUAKE_BUFF MAKEFOURCC('B', 'O', 'e', 'q')
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
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == code &&
            (!unit->abilstatus[i].timestamp || unit->abilstatus[i].timestamp > G_Time())) return true;
    return false;
}

static cstring_t spell_buff_fallback(uint32_t code) {
    /* ROC omits BuffID; ACsl/AUsl share the TFT token. */
    if (G_AbilityCode(code) == MAKEFOURCC('A', 'U', 's', 'l')) return "BUsl";
    return NULL;
}

static cstring_t spell_buff(abilityitem_t const *spell, uint32_t level) {
    cstring_t buff = G_AbilityLevel(spell->code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : spell_buff_fallback(spell->code);
}

static void target_status_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = spell_buff(spell, level);
    if (!st.entity || !buff) return;
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

static void toggle_status_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = caster->abilstatus + i;
        if (status->level && status->code == spell->code) { memset(status, 0, sizeof(*status)); return; }
    }
    unit_addstatus(caster, GetClassName(spell->code), S_SpellLevel(caster, spell->code));
}

static void radial_damage_status(edict_t *caster, vec2_t point, abilityitem_t const *spell, uint32_t data) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = spell_buff(spell, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &point) <= area) {
        S_SpellDamage(target, caster, (int)MAX(1.0f, S_SpellData(spell->code, level, data)));
        if (buff && !M_IsDead(target))
            unit_addtimedstatus(target, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(target)));
    }
}

static bool earthquake_hits_destructable(edict_t *target, float radius, vec2_t const *origin) {
    if (!target || !target->inuse || (target->targtype != TARG_TREE && target->targtype != TARG_DEBRIS)) return false;
    if (!G_IsDestructable(target) || target->destructable.dead) return false;
    return Vector2_distance(&target->s.origin2, origin) <= radius;
}

float S_EarthquakeMoveReduction(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, ID_EARTHQUAKE_BUFF);
    if (!level) return 0.0f;
    return MIN(1.0f, MAX(0.0f, S_SpellData(ID_EARTHQUAKE, level, 3)));
}

static edict_t *spell_begin_area_presentation(edict_t *owner, uint32_t code, vec2_t const *point) {
    edict_t *effect;
    int loop_sound;
    G_PlayAbilityEffectSound(code, point);
    effect = G_SpawnOwnedAbilityEffectAtPoint(owner, code, WC3_EFFECT_AREA_EFFECT, 0, point);
    if (!effect) effect = G_SpawnOwnedAbilityEffectAtPoint(owner, code, WC3_EFFECT_EFFECT, 0, point);
    loop_sound = G_AbilityEffectSoundIndex(code, true);
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
    float radius = S_SpellNumber(ent->class_id, ABILITY_NUMBER_AREA, level);
    float damage = S_SpellData(ent->class_id, level, 2);
    if (now >= ent->spawn_time) { spell_end_area_presentation(ent); S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(ent->owner, target) &&
                  Vector2_distance(&target->s.origin2, &ent->s.origin2) <= radius) {
        if (G_UnitIsBuilding(target->class_id) || target->targtype == TARG_STRUCTURE) {
            S_SpellDamage(target, ent->owner, (int)damage);
        } else if (target->targtype == TARG_GROUND && buff) {
            unit_addtimedstatus(target, buff, level, 1.5f);
        }
    }
    FILTER_EDICTS(target, earthquake_hits_destructable(target, radius, &ent->s.origin2))
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

static void morph_end(edict_t *thinker) {
    if (G_Time() < thinker->spawn_time) return;
    if (thinker->owner && thinker->owner->inuse) G_TransformUnitType(thinker->owner, thinker->resources);
    G_FreeEdict(thinker);
}

static void summon_execute_requested(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    S_SummonUnits(caster, S_SpellUnitId(spell->code, level), (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1)),
                  S_SpellDuration(spell->code, level, false));
}

int S_BlackArrowDamage(edict_t *attacker, int damage) {
    uint32_t level = G_UnitStatusLevel(attacker, MAKEFOURCC('A','N','b','a'));
    return level ? damage + (int)S_SpellData(MAKEFOURCC('A','N','b','a'), level, 1) : damage;
}

void S_BlackArrowDeath(edict_t *attacker, edict_t *target) {
    uint32_t level = G_UnitStatusLevel(attacker, MAKEFOURCC('A','N','b','a'));
    if (level && target && M_IsDead(target))
        S_SummonAt(attacker, S_SpellUnitId(MAKEFOURCC('A','N','b','a'), level), &target->s.origin2,
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
    edict_t *target = missile->goalentity, *caster = missile->owner;
    cstring_t race = target && target->data.UnitData ? target->data.UnitData->race : NULL;
    bool applied = false;

    if (caster && caster->inuse && S_SpellIsAliveTarget(target) && race &&
        target->spawn_time == missile->channel.target_spawn_time) {
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
    cstring_t art = G_AbilityEffectArt(spell->code, WC3_EFFECT_MISSILE, 0);
    edict_t *missile = G_Spawn();

    missile->class_id = spell->code;
    missile->s.origin = caster->s.origin;
    missile->s.angle = caster->s.angle;
    missile->s.model = art ? G_RegisterModel(art) : 0;
    missile->goalentity = st.entity;
    missile->channel.target_spawn_time = st.entity->spawn_time;
    missile->owner = caster;
    missile->velocity = death_coil_missile_speed(spell->code) / 1000.0f;
    missile->damage = (uint32_t)MAX(0.0f, S_SpellData(spell->code, level, 1));
    missile->movetype = MOVETYPE_FLYMISSILE;
    missile->currentmove = &death_coil_projectile_move;
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
        FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                      S_SpellAllowsTarget(spell->code, caster, target) &&
                      Vector2_distance(&target->s.origin2, &current->s.origin2) <= S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level)) {
            bool seen = false;
            FOR_LOOP(j, nvisited) seen |= target == visited[j];
            if (!seen && candidate_count < MAX_GROUP_SIZE) candidates[candidate_count++] = target;
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
    FILTER_EDICTS(marker, marker->class_id == ID_CHAIN_LIGHTNING_VISIT && marker->owner == thinker &&
                  marker->channel.owner_spawn_time == thinker->spawn_time &&
                  marker->goalentity == target && marker->resources == target->spawn_time)
        return true;
    return false;
}

static void chain_lightning_mark_visited(edict_t *thinker, edict_t *target) {
    edict_t *marker = G_Spawn();
    if (!marker) return;
    marker->class_id = ID_CHAIN_LIGHTNING_VISIT;
    marker->svflags |= SVF_NOCLIENT;
    marker->owner = thinker;
    marker->channel.owner_spawn_time = thinker->spawn_time;
    marker->goalentity = target;
    marker->resources = target->spawn_time;
}

static void chain_lightning_finish(edict_t *thinker) {
    edict_t *markers[32];
    uint32_t count = 0;
    FILTER_EDICTS(marker, marker->class_id == ID_CHAIN_LIGHTNING_VISIT && marker->owner == thinker &&
                  marker->channel.owner_spawn_time == thinker->spawn_time)
        if (count < 32) markers[count++] = marker;
    FOR_LOOP(i, count) G_FreeEdict(markers[i]);
    G_FreeEdict(thinker);
}

void chain_lightning_think(edict_t *thinker) {
    edict_t *caster, *next = NULL;
    float nearest_distance = 0.0f;

    if (!thinker || !thinker->inuse) return;
    caster = thinker->owner;
    if (!caster || !caster->inuse || caster->spawn_time != thinker->channel.owner_spawn_time || !thinker->resources) {
        chain_lightning_finish(thinker);
        return;
    }
    if (G_Time() < thinker->freetime) return;

    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  S_SpellAllowsTarget(thinker->class_id, caster, target) &&
                  Vector2_distance(&target->s.origin2, &thinker->s.origin2) <= thinker->collision) {
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
    thinker->channel.owner_spawn_time = caster->spawn_time;
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

static void reincarnation_think(edict_t *thinker) {
    if (!thinker->owner || !thinker->owner->inuse) { G_FreeEdict(thinker); return; }
    if (G_Time() < thinker->spawn_time) return;
    if (M_IsDead(thinker->owner)) G_ReviveHero(thinker->owner, thinker->s.origin2.x, thinker->s.origin2.y);
    G_FreeEdict(thinker);
}

void S_ReincarnationOnDeath(edict_t *unit) {
    static uint32_t const codes[] = { MAKEFOURCC('A','O','r','e'), MAKEFOURCC('A','C','r','n'), MAKEFOURCC('A','N','r','n') };
    uint32_t code = 0, level = 0;
    edict_t *thinker;
    FOR_LOOP(i, sizeof(codes) / sizeof(*codes)) if ((level = G_UnitAbilityLevel(unit, codes[i]))) { code = codes[i]; break; }
    if (!level || !S_SpellCooldownReady(unit, code)) return;
    thinker = G_Spawn(); thinker->owner = unit; thinker->s.origin2 = unit->s.origin2;
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellData(code, level, 1) * 1000.0f);
    thinker->think = reincarnation_think; S_SpellStartCooldown(unit, code, level);
}

static void acid_bomb_think(edict_t *thinker) {
    edict_t *target = thinker->goalentity;
    if (G_Time() >= thinker->spawn_time || !target || !target->inuse || M_IsDead(target)) { G_FreeEdict(thinker); return; }
    if (!thinker->freetime || G_Time() >= thinker->freetime) {
        S_SpellDamage(target, thinker->owner, thinker->damage); thinker->freetime = G_Time() + 1000;
    }
}

/* Mass Teleport keeps its channel-owned area effects on the thinker so cancel,
 * target death, save/load continuation and successful completion all use the
 * same cleanup path. */
static void mass_teleport_cleanup(edict_t *thinker) {
    edict_t *target = thinker ? thinker->goalentity : NULL;

    if (!thinker) return;
    if (target && target->inuse && target->spawn_time == thinker->channel.target_spawn_time &&
        thinker->wait < 0.5f)
        target->paused = false;
    FILTER_EDICTS(effect, effect->inuse && effect->owner == thinker &&
                  effect->summon_ability == thinker->class_id) {
        effect->owner = NULL;
        effect->summon_ability = 0;
        G_DestroyEffect(effect);
    }
}

static void mass_teleport_track_effect(edict_t *thinker, edict_t *effect) {
    if (!thinker || !effect) return;
    effect->owner = thinker;
    effect->summon_ability = thinker->class_id;
}

static void mass_teleport_move_unit(edict_t *unit, uint32_t code, vec2_t const *requested) {
    vec2_t source, position;

    if (!unit || !requested) return;
    source = unit->s.origin2;
    G_SpawnAbilityEffectAtPoint(code, WC3_EFFECT_SPECIAL, 0, &source, true);
    /* SetUnitPosition keeps the requested point as a fallback when no spiral
     * candidate is open.  Mass Teleport uses the same relocation contract. */
    (void)G_FindUnitUnstuckPosition(unit, requested, &position);
    unit->s.origin2 = position;
    unit->s.origin.x = position.x;
    unit->s.origin.y = position.y;
    if (unit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.LinkEntity(unit);
    G_SpawnAbilityEffectAtPoint(code, WC3_EFFECT_SPECIAL, 0, &unit->s.origin2, true);
}

void mass_teleport_think(edict_t *thinker) {
    edict_t *caster = thinker ? thinker->owner : NULL;
    edict_t *target = thinker ? thinker->goalentity : NULL;
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
    if (!target || !target->inuse || target->spawn_time != thinker->channel.target_spawn_time ||
        !S_SpellAllowsTarget(thinker->class_id, caster, target)) {
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
                  unit->s.player == caster->s.player && unit->targtype != TARG_STRUCTURE &&
                  !G_UnitIsBuilding(unit->class_id) &&
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
        FILTER_EDICTS(thinker, thinker->inuse && thinker->owner == ent && thinker->class_id == code &&
                      thinker->think == mass_teleport_think &&
                      thinker->channel.owner_spawn_time == ent->spawn_time)
            mass_teleport_cleanup(thinker);
        return true;
    }
    if (msg == A_EXECUTE) {
        spellTarget_t st = call && call->target ? *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
        edict_t *target = st.entity, *thinker, *effect;
        uint32_t level;
        float delay;

        if (!spell || !target) return false;
        level = S_SpellLevel(ent, spell->code);
        delay = MAX(0.0f, S_SpellData(spell->code, level, 2));
        thinker = S_SpellChannelThinker(ent, spell->code);
        thinker->goalentity = target;
        thinker->channel.target_spawn_time = target->spawn_time;
        thinker->variation = level;
        thinker->wait = target->paused ? 1.0f : 0.0f;
        thinker->freetime = G_Time() + (uint32_t)(delay * 1000.0f);
        thinker->think = mass_teleport_think;

        effect = G_SpawnAbilityEffectAtPoint(spell->code, WC3_EFFECT_AREA_EFFECT, 0, &ent->s.origin2, false);
        mass_teleport_track_effect(thinker, effect);
        effect = G_SpawnAbilityEffectAtPoint(spell->code, WC3_EFFECT_AREA_EFFECT, 0, &target->s.origin2, false);
        mass_teleport_track_effect(thinker, effect);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, ent, NULL, true);
        target->paused = true;
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
BZ_SIMPLE_SPELL_PROC(AbilitySummonPhoenix) { summon_execute_requested(caster, st, spell); }
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
    FOR_LOOP(i, count) S_SummonAt(caster, S_SpellDataId(spell->code, level, 3), &corpse->s.origin2,
                                  S_SpellDuration(spell->code, level, false));
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
            unit_addtimedstatus(target, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(target)));
    }
}
/* Name=Locust Swarm
 * Ubertip="Summons a swarm of locusts that damages enemy units and returns life to the caster."
 */
BZ_SIMPLE_SPELL_PROC(AbilityLocustSwarm) { summon_execute_requested(caster, st, spell); }
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
BZ_SIMPLE_SPELL_PROC(AbilitySilence) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = spell_buff(spell, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &st.point) <= area) {
        if (buff) unit_addtimedstatus(target, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(target)));
    }
}
/* Corpse ultimates prefer higher-level units before lower-level ones. Equal-level
 * ties retain the stable entity-enumeration order until a stricter retail tie-break
 * is established. */
static int32_t corpse_unit_level(edict_t const *unit) {
    UnitBalance_t const *balance = unit ? unit->data.UnitBalance : NULL;
    if (!balance && unit) balance = G_UnitBalance(unit->class_id);
    return balance ? balance->level : 0;
}

static bool corpse_preferred(edict_t const *candidate, edict_t const *current, edict_t const *caster, bool nearest_tie) {
    int32_t candidate_level, current_level;

    if (!current) return true;
    candidate_level = corpse_unit_level(candidate); current_level = corpse_unit_level(current);
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
    return G_UnitIsRaisableCorpse(unit) && !G_UnitIsHero(unit) && !G_UnitIsBuilding(unit->class_id) &&
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
         * Retire the same death-state owners as ordinary resurrection first so
         * the old decay callback/order state cannot remove or drive the raised unit.
         * Its original corpse is consumed: when the temporary unit later dies or
         * times out it must not create another raisable corpse. */
        selected->svflags &= ~SVF_DEADMONSTER; selected->s.flags &= ~EF_NOT_SELECTABLE;
        selected->aiflags &= ~AI_HOLD_FRAME; selected->s.renderfx &= ~RF_HIDDEN;
        selected->combatentity = selected->goalentity = selected->secondarygoal = NULL;
        selected->wait = 0; G_ClearUnitOrderQueue(selected);
        selected->aiflags |= AI_CORPSE_UNRAISABLE | AI_CORPSE_NO_DECAY;
        G_SetHealth(selected, selected->health.max_value);
        /* Hre2 / ABILITY_BLF_RAISED_UNITS_ARE_INVULNERABLE is authored
         * per Resurrection-family ability. Grant it when requested without
         * forcibly clearing other invulnerability sources when it is false. */
        if (raised_invulnerable) selected->invulnerable = true;
        selected->s.player = caster->s.player; selected->owner = caster;
        selected->summon_ability = spell->code;
        unit_addtimedstatus(selected, "BTLF", level, duration);
        if (selected->stand) selected->stand(selected);
        gi.LinkEntity(selected);
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
BZ_SIMPLE_SPELL_PROC(AbilityMetamorphosis) {
    uint32_t level = S_SpellLevel(caster, spell->code), form = S_SpellUnitId(spell->code, level), original = caster->class_id;
    float duration = S_SpellDuration(spell->code, level, true);
    if (!form || !G_TransformUnitType(caster, form) || duration <= 0.0f) return;
    edict_t *thinker = G_Spawn();
    thinker->owner = caster; thinker->resources = original;
    thinker->spawn_time = G_Time() + (uint32_t)(duration * 1000.0f); thinker->think = morph_end;
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
    radial_damage_status(caster, st.point, spell, 1);
    S_SummonAt(caster, S_SpellUnitId(spell->code, level), &st.point, S_SpellData(spell->code, level, 2));
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
    spell_begin_area_presentation(thinker, spell->code, &st.point);
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
    spell_begin_area_presentation(thinker, spell->code, &st.point);
    far_sight_think(thinker);
}
/* Resurrection operates on nearby ordinary corpses; Heroes retain their separate altar revival lifecycle. */
static bool resurrection_target(edict_t *caster, edict_t *target, abilityitem_t const *spell) {
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, S_SpellLevel(caster, spell->code));
    return G_UnitIsRaisableCorpse(target) && !G_UnitIsHero(target) &&
        !G_UnitIsBuilding(target->class_id) && S_SpellIsFriend(caster, target) &&
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
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    uint32_t damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &st.point) <= radius)
        S_SpellDamage(target, caster, damage);
}
/* Name=Howl of Terror
 * Ubertip="Reduces the attack damage of nearby enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityHowlOfTerror) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    float duration = S_SpellDuration(spell->code, level, false);
    cstring_t buff = G_AbilityLevel(spell->code, level)->buffID;
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= radius)
        if (buff && strlen(buff) >= 4) unit_addtimedstatus(target, buff, level, duration);
}
/* Name=Drunken Haze
 * Ubertip="Slows enemy units and gives them a chance to miss on attacks."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDrunkenHaze) { target_status_execute(caster, st, spell); }
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
        FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                      Vector2_distance(&target->s.origin2, &visited[i]->s.origin2) <= S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level)) {
            bool seen = false; FOR_LOOP(j, i + 1) seen |= target == visited[j];
            if (!seen) { current = target; break; }
        }
    }
}
/* Name=Hex
 * Ubertip="Transforms an enemy unit into a random critter for <ANhx,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityHex) { target_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySpiritOfVengeance) { summon_execute_requested(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityVoodoo) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = spell_buff(spell, level);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    if (!buff) return;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        unit_addtimedstatus(target, buff, level, S_SpellDuration(spell->code, level, false));
}
BZ_SIMPLE_SPELL_PROC(AbilityAcidBomb) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = spell_buff(spell, level);
    edict_t *thinker;
    if (!st.entity || !S_SpellIsAliveTarget(st.entity)) return;
    if (buff) unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, false));
    thinker = G_Spawn(); thinker->owner = caster; thinker->goalentity = st.entity; thinker->damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 3));
    thinker->spawn_time = G_Time() + (uint32_t)(S_SpellDuration(spell->code, level, false) * 1000.0f); thinker->think = acid_bomb_think;
}

BZ_SIMPLE_SPELL_PROC(AbilityFlamingArrows) {
    toggle_status_execute(caster, st, spell);
}
