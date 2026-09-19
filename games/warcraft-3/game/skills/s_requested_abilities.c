#include "s_skills.h"

#define ID_EARTHQUAKE MAKEFOURCC('A', 'O', 'e', 'q')
#define ID_EARTHQUAKE_BUFF MAKEFOURCC('B', 'O', 'e', 'q')
#define ID_CHAIN_LIGHTNING_VISIT MAKEFOURCC('C', 'L', 'v', 's')
#define CHAIN_LIGHTNING_JUMP_MS 250
#define CHAIN_LIGHTNING_BOLT_MS 2000

void whirlwind_think(LPEDICT ent);

typedef struct {
    LPEDICT caster;
    spellTarget_t target;
    abilityitem_t const *spell;
    FLOAT scale;
    BOOL random_jumps;
} bounceParams_t;

BOOL S_UnitHasStatus(LPCEDICT unit, DWORD code) {
    if (!unit) return false;
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == code &&
            (!unit->abilstatus[i].timestamp || unit->abilstatus[i].timestamp > G_Time())) return true;
    return false;
}

static LPCSTR spell_buff_fallback(DWORD code) {
    /* ROC omits BuffID; ACsl/AUsl share the TFT token. */
    if (G_AbilityCode(code) == MAKEFOURCC('A', 'U', 's', 'l')) return "BUsl";
    return NULL;
}

static LPCSTR spell_buff(abilityitem_t const *spell, DWORD level) {
    LPCSTR buff = G_AbilityLevel(spell->code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : spell_buff_fallback(spell->code);
}

static void target_status_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR buff = spell_buff(spell, level);
    if (!st.entity || !buff) return;
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

static void toggle_status_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = caster->abilstatus + i;
        if (status->level && status->code == spell->code) { memset(status, 0, sizeof(*status)); return; }
    }
    unit_addstatus(caster, GetClassName(spell->code), S_SpellLevel(caster, spell->code));
}

static void radial_damage_status(LPEDICT caster, VECTOR2 point, abilityitem_t const *spell, DWORD data) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    LPCSTR buff = spell_buff(spell, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &point) <= area) {
        S_SpellDamage(target, caster, (int)MAX(1.0f, S_SpellData(spell->code, level, data)));
        if (buff && !M_IsDead(target))
            unit_addtimedstatus(target, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(target)));
    }
}

static BOOL earthquake_hits_destructable(LPEDICT target, FLOAT radius, LPCVECTOR2 origin) {
    if (!target || !target->inuse || (target->targtype != TARG_TREE && target->targtype != TARG_DEBRIS)) return false;
    if (!G_IsDestructable(target) || target->destructable.dead) return false;
    return Vector2_distance(&target->s.origin2, origin) <= radius;
}

FLOAT S_EarthquakeMoveReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, ID_EARTHQUAKE_BUFF);
    if (!level) return 0.0f;
    return MIN(1.0f, MAX(0.0f, S_SpellData(ID_EARTHQUAKE, level, 3)));
}

static LPEDICT spell_begin_area_presentation(LPEDICT owner, DWORD code, LPCVECTOR2 point) {
    LPEDICT effect;
    int loop_sound;
    G_PlayAbilityEffectSound(code, point);
    effect = G_SpawnOwnedAbilityEffectAtPoint(owner, code, WC3_EFFECT_AREA_EFFECT, 0, point);
    if (!effect) effect = G_SpawnOwnedAbilityEffectAtPoint(owner, code, WC3_EFFECT_EFFECT, 0, point);
    loop_sound = G_AbilityEffectSoundIndex(code, true);
    if (effect && loop_sound) effect->s.sound = (USHORT)loop_sound;
    return effect;
}

static void spell_end_area_presentation(LPEDICT owner) {
    G_DestroyOwnedEffects(owner);
}

void earthquake_think(LPEDICT ent) {
    if (!S_SpellChannelActive(ent)) { spell_end_area_presentation(ent); S_SpellEndChannel(ent); return; }
    DWORD level = S_SpellLevel(ent->owner, ent->class_id), now = G_Time();
    abilityitem_t item = S_AbilityItem(ent->class_id);
    abilityitem_t const *spell = &item;
    LPCSTR buff = spell_buff(spell, level);
    FLOAT radius = S_SpellNumber(ent->class_id, ABILITY_NUMBER_AREA, level);
    FLOAT damage = S_SpellData(ent->class_id, level, 2);
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

void far_sight_think(LPEDICT thinker) {
    if (G_Time() >= thinker->spawn_time || thinker->s.player >= MAX_PLAYERS) {
        spell_end_area_presentation(thinker);
        G_FreeEdict(thinker);
        return;
    }
    G_FowSetStateRadius(&(FOGWRITE){ thinker->s.player, WC3_FOG_STATE_VISIBLE, true },
                        &thinker->s.origin2, thinker->collision);
}

void whirlwind_think(LPEDICT ent) {
    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    abilityitem_t item = S_AbilityItem(ent->class_id);
    DWORD data = item.ability && item.ability->proc == CAbilityStampede ? 2 : 1;
    if (G_Time() >= ent->spawn_time) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && G_Time() < ent->freetime) return;
    if (item.ability && (item.ability->proc == CAbilityWhirlwind || item.ability->proc == CAbilityTornado))
        ent->s.origin2 = ent->owner->s.origin2;
    radial_damage_status(ent->owner, ent->s.origin2, &item, data);
    ent->freetime = G_Time() + 1000;
}

static void whirlwind_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->spawn_time = G_Time() + (DWORD)(S_SpellDuration(spell->code, level, true) * 1000.0f);
    thinker->think = whirlwind_think; whirlwind_think(thinker);
}

static void morph_end(LPEDICT thinker) {
    if (G_Time() < thinker->spawn_time) return;
    if (thinker->owner && thinker->owner->inuse) G_TransformUnitType(thinker->owner, thinker->resources);
    G_FreeEdict(thinker);
}

static void summon_execute_requested(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    S_SummonUnits(caster, S_SpellUnitId(spell->code, level), (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1)),
                  S_SpellDuration(spell->code, level, false));
}

int S_BlackArrowDamage(LPEDICT attacker, int damage) {
    DWORD level = G_UnitStatusLevel(attacker, MAKEFOURCC('A','N','b','a'));
    return level ? damage + (int)S_SpellData(MAKEFOURCC('A','N','b','a'), level, 1) : damage;
}

void S_BlackArrowDeath(LPEDICT attacker, LPEDICT target) {
    DWORD level = G_UnitStatusLevel(attacker, MAKEFOURCC('A','N','b','a'));
    if (level && target && M_IsDead(target))
        S_SummonAt(attacker, S_SpellUnitId(MAKEFOURCC('A','N','b','a'), level), &target->s.origin2,
                   S_SpellData(MAKEFOURCC('A','N','b','a'), level, 3));
}

static BOOL death_coil_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    LPCSTR race = st.entity && st.entity->data.UnitData ? st.entity->data.UnitData->race : NULL;
    return S_SpellIsAliveTarget(st.entity) && race && ((!strcmp(race, STR_UNDEAD) && S_SpellIsFriend(caster, st.entity)) ||
           (strcmp(race, STR_UNDEAD) && S_SpellIsEnemy(caster, st.entity)));
}

static void death_coil_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT amount = S_SpellData(spell->code, level, 1);
    if (!strcmp(st.entity->data.UnitData->race, STR_UNDEAD)) S_SpellHeal(st.entity, amount);
    else S_SpellDamage(st.entity, caster, (int)(amount * 0.5f));
}

/* Resolve chained damage jumps while keeping target selection separate from spell metadata. */
static void bounce_execute(bounceParams_t const *params) {
    LPEDICT caster = params->caster;
    spellTarget_t st = params->target;
    abilityitem_t const *spell = params->spell;
    FLOAT scale = params->scale;
    BOOL random_jumps = params->random_jumps;
    DWORD level = S_SpellLevel(caster, spell->code), hits = (DWORD)S_SpellData(spell->code, level, 2);
    FLOAT damage = S_SpellData(spell->code, level, 1);
    LPEDICT current = st.entity, visited[32] = {0};
    DWORD nvisited = 0;
    FOR_LOOP(i, MIN(hits, 32)) {
        LPEDICT candidates[MAX_GROUP_SIZE];
        DWORD candidate_count = 0;
        if (!current) break;
        S_SpellDamage(current, caster, (int)MAX(1.0f, damage));
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, current, NULL, true);
        visited[nvisited++] = current; damage *= scale;
        FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                      S_SpellAllowsTarget(spell->code, caster, target) &&
                      Vector2_distance(&target->s.origin2, &current->s.origin2) <= S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level)) {
            BOOL seen = false;
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
static BOOL chain_lightning_visited(LPEDICT thinker, LPCEDICT target) {
    FILTER_EDICTS(marker, marker->class_id == ID_CHAIN_LIGHTNING_VISIT && marker->owner == thinker &&
                  marker->channel.owner_spawn_time == thinker->spawn_time &&
                  marker->goalentity == target && marker->resources == target->spawn_time)
        return true;
    return false;
}

static void chain_lightning_mark_visited(LPEDICT thinker, LPEDICT target) {
    LPEDICT marker = G_Spawn();
    if (!marker) return;
    marker->class_id = ID_CHAIN_LIGHTNING_VISIT;
    marker->svflags |= SVF_NOCLIENT;
    marker->owner = thinker;
    marker->channel.owner_spawn_time = thinker->spawn_time;
    marker->goalentity = target;
    marker->resources = target->spawn_time;
}

static void chain_lightning_finish(LPEDICT thinker) {
    LPEDICT markers[32];
    DWORD count = 0;
    FILTER_EDICTS(marker, marker->class_id == ID_CHAIN_LIGHTNING_VISIT && marker->owner == thinker &&
                  marker->channel.owner_spawn_time == thinker->spawn_time)
        if (count < 32) markers[count++] = marker;
    FOR_LOOP(i, count) G_FreeEdict(markers[i]);
    G_FreeEdict(thinker);
}

void chain_lightning_think(LPEDICT thinker) {
    LPEDICT caster, next = NULL;
    FLOAT nearest_distance = 0.0f;

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
        FLOAT distance;
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
        G_SpawnAbilityLightning(thinker->class_id, 1, thinker->goalentity, next, CHAIN_LIGHTNING_BOLT_MS);
    } else {
        VECTOR3 from = { thinker->s.origin2.x, thinker->s.origin2.y,
            CM_GetHeightAtPoint(thinker->s.origin2.x, thinker->s.origin2.y) + next->s.radius * 0.5f };
        VECTOR3 to = next->s.origin;
        DWORD lightning = G_AbilityLightningId(thinker->class_id, 1);
        to.z += next->s.radius * 0.5f;
        if (lightning) G_LightningAdd(lightning, &from, &to, COLOR32_WHITE, CHAIN_LIGHTNING_BOLT_MS);
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

static void chain_lightning_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD hits = MIN(32, (DWORD)MAX(0.0f, S_SpellData(spell->code, level, 2)));
    FLOAT damage = S_SpellData(spell->code, level, 1);
    LPEDICT thinker;

    if (!st.entity || !hits) return;
    G_PlayAbilityEffectSound(spell->code, &st.entity->s.origin2);
    G_SpawnAbilityLightning(spell->code, 0, caster, st.entity, CHAIN_LIGHTNING_BOLT_MS);
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

static void reincarnation_think(LPEDICT thinker) {
    if (!thinker->owner || !thinker->owner->inuse) { G_FreeEdict(thinker); return; }
    if (G_Time() < thinker->spawn_time) return;
    if (M_IsDead(thinker->owner)) G_ReviveHero(thinker->owner, thinker->s.origin2.x, thinker->s.origin2.y);
    G_FreeEdict(thinker);
}

void S_ReincarnationOnDeath(LPEDICT unit) {
    DWORD code = MAKEFOURCC('A', 'O', 'r', 'e'), level = G_UnitAbilityLevel(unit, code);
    LPEDICT thinker;
    if (!level || !S_SpellCooldownReady(unit, code)) return;
    thinker = G_Spawn(); thinker->owner = unit; thinker->s.origin2 = unit->s.origin2;
    thinker->spawn_time = G_Time() + (DWORD)(S_SpellData(code, level, 1) * 1000.0f);
    thinker->think = reincarnation_think; S_SpellStartCooldown(unit, code, level);
}

static void acid_bomb_think(LPEDICT thinker) {
    LPEDICT target = thinker->goalentity;
    if (G_Time() >= thinker->spawn_time || !target || !target->inuse || M_IsDead(target)) { G_FreeEdict(thinker); return; }
    if (!thinker->freetime || G_Time() >= thinker->freetime) {
        S_SpellDamage(target, thinker->owner, thinker->damage); thinker->freetime = G_Time() + 1000;
    }
}

/* Name=Mass Teleport
 * Ubertip="Teleports the caster and nearby friendly units to a target location."
 */
BZ_SIMPLE_SPELL_PROC(AbilityMassTeleport) {
    DWORD level = S_SpellLevel(caster, spell->code), count = 1;
    VECTOR2 src = caster->s.origin2, dst = st.entity ? st.entity->s.origin2 : st.point;
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    DWORD limit = (DWORD)S_SpellData(spell->code, level, 1);
    FILTER_EDICTS(unit, count < limit && unit != caster && S_SpellIsAliveTarget(unit) && S_SpellIsFriend(caster, unit) &&
                  Vector2_distance(&unit->s.origin2, &src) <= area) {
        VECTOR2 offset = Vector2_sub(&unit->s.origin2, &src);
        VECTOR2 requested = S_SpellData(spell->code, level, 3) ? dst : Vector2_add(&dst, &offset);
        if (G_FindUnitUnstuckPosition(unit, &requested, &unit->s.origin2)) {
            unit->s.origin.x = unit->s.origin2.x; unit->s.origin.y = unit->s.origin2.y; count++;
        }
    }
    if (G_FindUnitUnstuckPosition(caster, &dst, &caster->s.origin2)) {
        caster->s.origin.x = caster->s.origin2.x; caster->s.origin.y = caster->s.origin2.y;
    }
}
/* Name=Stampede
 * Ubertip="Calls down hordes of rampaging thunder lizards to explode upon the Beastmaster's enemies."
 */
BZ_SIMPLE_SPELL_PROC(AbilityStampede) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT thinker = S_SpellChannelThinker(caster, spell->code); thinker->s.origin2 = st.point;
    thinker->spawn_time = G_Time() + (DWORD)(S_SpellDuration(spell->code, level, false) * 1000.0f);
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
    DWORD level = S_SpellLevel(caster, spell->code), count = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1));
    FLOAT range = S_SpellRange(spell->code, level);
    LPEDICT corpse = NULL;
    FILTER_EDICTS(unit, unit->inuse && M_IsDead(unit) && !G_UnitIsHero(unit) &&
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
    DWORD level = S_SpellLevel(caster, spell->code);
    VECTOR2 offset = Vector2_sub(&st.point, &caster->s.origin2);
    FLOAT distance = Vector2_distance(&caster->s.origin2, &st.point);
    VECTOR2 direction;
    LPCSTR buff = spell_buff(spell, level);
    if (distance <= 0.0f) return;
    direction = Vector2_scale(&offset, 1.0f / distance);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target)) {
        VECTOR2 delta = Vector2_sub(&target->s.origin2, &caster->s.origin2);
        FLOAT along = Vector2_dot(&delta, &direction);
        FLOAT across = delta.x * direction.y - delta.y * direction.x;
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
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    LPCSTR buff = spell_buff(spell, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &st.point) <= area) {
        if (buff) unit_addtimedstatus(target, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(target)));
    }
}
/* Name=Animate Dead
 * Ubertip="Raises a number of corpses to serve the caster for a limited time."
 */
BZ_SIMPLE_SPELL_PROC(AbilityAnimateDead) {
    DWORD level = S_SpellLevel(caster, spell->code), count = 0, limit = (DWORD)S_SpellData(spell->code, level, 1);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FILTER_EDICTS(unit, count < limit && unit->inuse && M_IsDead(unit) && !G_UnitIsHero(unit) &&
                  Vector2_distance(&unit->s.origin2, &caster->s.origin2) <= area) {
        G_SetHealth(unit, unit->health.max_value); unit->svflags &= ~SVF_DEADMONSTER; unit->s.flags &= ~EF_NOT_SELECTABLE;
        unit->s.player = caster->s.player; unit->owner = caster;
        unit_addtimedstatus(unit, "BTLF", level, S_SpellDuration(spell->code, level, false));
        if (unit->stand) unit->stand(unit);
        count++; /* Keep the revived-unit limit independent of the optional animation callback. */
    }
}
BZ_VALIDATED_SPELL_PROC(AbilityDeathCoil, death_coil_validate, death_coil_execute)
/* Name=Death Pact
 * Ubertip="Sacrifices a friendly undead unit to restore the Death Knight's life and mana."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDeathPact) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT life = st.entity->health.value;
    S_SpellHeal(caster, S_SpellData(spell->code, level, 2) * life);
    caster->mana.value = MIN(caster->mana.max_value, caster->mana.value + S_SpellData(spell->code, level, 1) * life);
    S_SpellDamage(st.entity, caster, (int)MAX(1.0f, st.entity->health.value));
}
/* Name=Metamorphosis
 * Ubertip="Transforms the Demon Hunter into a powerful demon."
 */
BZ_SIMPLE_SPELL_PROC(AbilityMetamorphosis) {
    DWORD level = S_SpellLevel(caster, spell->code), form = S_SpellUnitId(spell->code, level), original = caster->class_id;
    FLOAT duration = S_SpellDuration(spell->code, level, true);
    if (!form || !G_TransformUnitType(caster, form) || duration <= 0.0f) return;
    LPEDICT thinker = G_Spawn();
    thinker->owner = caster; thinker->resources = original;
    thinker->spawn_time = G_Time() + (DWORD)(duration * 1000.0f); thinker->think = morph_end;
}
/* Name=Sleep
 * Ubertip="Puts a target enemy unit to sleep."
 */
BZ_SIMPLE_SPELL_PROC(AbilitySleep) { target_status_execute(caster, st, spell); }
/* Name=Inferno
 * Ubertip="Calls down an infernal that damages nearby enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDreadLordInferno) {
    DWORD level = S_SpellLevel(caster, spell->code);
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
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT thinker = S_SpellChannelThinker(caster, spell->code); thinker->s.origin2 = st.point;
    thinker->spawn_time = G_Time() + (DWORD)(S_SpellDuration(spell->code, level, false) * 1000.0f);
    thinker->freetime = G_Time() + (DWORD)(MAX(0.0f, S_SpellData(spell->code, level, 1)) * 1000.0f);
    thinker->think = earthquake_think;
    spell_begin_area_presentation(thinker, spell->code, &st.point);
    earthquake_think(thinker);
}
/* Name=Far Sight
 * Ubertip="Reveals a specified area of the map."
 */
BZ_SIMPLE_SPELL_PROC(AbilityFarSight) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT thinker = G_Spawn();
    thinker->class_id = spell->code;
    thinker->s.player = caster->s.player;
    thinker->s.origin2 = st.point;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->spawn_time = G_Time() + (DWORD)(S_SpellDuration(spell->code, level, false) * 1000.0f);
    thinker->think = far_sight_think;
    spell_begin_area_presentation(thinker, spell->code, &st.point);
    far_sight_think(thinker);
}
/* Resurrection operates on nearby ordinary corpses; Heroes retain their separate altar revival lifecycle. */
static BOOL resurrection_target(LPEDICT caster, LPEDICT target, abilityitem_t const *spell) {
    FLOAT radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, S_SpellLevel(caster, spell->code));
    return target->inuse && (target->svflags & SVF_MONSTER) &&
        (target->svflags & SVF_DEADMONSTER) && M_IsDead(target) &&
        !G_UnitIsHero(target) && !G_UnitIsBuilding(target->class_id) && S_SpellIsFriend(caster, target) &&
        Vector2_distance(&target->s.origin2, &caster->s.origin2) <= radius;
}

/* Reject empty casts before the shared pipeline commits mana and cooldown. */
static BOOL resurrection_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    FILTER_EDICTS(target, resurrection_target(caster, target, spell)) return true;
    return false;
}

/* Reuse each corpse's edict and retire its death animation/timer before restoring ordinary unit activity. */
static void resurrection_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD rank = S_SpellLevel(caster, spell->code), count = 0;
    DWORD limit = (DWORD)S_SpellData(spell->code, rank, 1);
    FILTER_EDICTS(target, count < limit && resurrection_target(caster, target, spell)) {
        G_ReviveCorpse(target, 1.0f);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
        count++;
    }
}

BZ_VALIDATED_SPELL_PROC(AbilityResurrection, resurrection_validate, resurrection_execute)
/* Name=Breath of Fire
 * Ubertip="Breathes a cone of fire at enemy units, dealing <ANcf,DataA1> initial damage."
 */
BZ_SIMPLE_SPELL_PROC(AbilityBreathOfFire) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    DWORD damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1));
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &st.point) <= radius)
        S_SpellDamage(target, caster, damage);
}
/* Name=Howl of Terror
 * Ubertip="Reduces the attack damage of nearby enemy units."
 */
BZ_SIMPLE_SPELL_PROC(AbilityHowlOfTerror) {
    DWORD level = S_SpellLevel(caster, spell->code);
    AbilityData_t const *data = G_AbilityData(spell->code);
    FLOAT radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FLOAT duration = S_SpellDuration(spell->code, level, false);
    LPCSTR buff = data->level[level - 1].buffID;
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
    DWORD level = S_SpellLevel(caster, spell->code), count = (DWORD)S_SpellData(spell->code, level, 2);
    FLOAT amount = S_SpellData(spell->code, level, 1), loss = S_SpellData(spell->code, level, 3);
    LPEDICT current = st.entity, visited[32] = {0};
    FOR_LOOP(i, MIN(count ? count : 1, 32)) {
        if (!current || !S_SpellIsAliveTarget(current) || !S_SpellIsFriend(caster, current)) break;
        S_SpellHeal(current, amount); visited[i] = current; amount *= 1.0f - loss;
        current = NULL;
        FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                      Vector2_distance(&target->s.origin2, &visited[i]->s.origin2) <= S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level)) {
            BOOL seen = false; FOR_LOOP(j, i + 1) seen |= target == visited[j];
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
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR buff = spell_buff(spell, level);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    if (!buff) return;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        unit_addtimedstatus(target, buff, level, S_SpellDuration(spell->code, level, false));
}
BZ_SIMPLE_SPELL_PROC(AbilityAcidBomb) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR buff = spell_buff(spell, level);
    LPEDICT thinker;
    if (!st.entity || !S_SpellIsAliveTarget(st.entity)) return;
    if (buff) unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, false));
    thinker = G_Spawn(); thinker->owner = caster; thinker->goalentity = st.entity; thinker->damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 3));
    thinker->spawn_time = G_Time() + (DWORD)(S_SpellDuration(spell->code, level, false) * 1000.0f); thinker->think = acid_bomb_think;
}

BZ_SIMPLE_SPELL_PROC(AbilityFlamingArrows) {
    toggle_status_execute(caster, st, spell);
}
