#include "s_skills.h"

#define ALCHEMIST_ACID_BUFF MAKEFOURCC('B','N','a','b')

/* The function-text missile speed is independent of AbilityData rank fields. */
static float alchemist_missile_speed(uint32_t code) {
    cstring_t const value = S_SpellString(code, "Missilespeed", 0);
    float const speed = value ? (float)atof(value) : 0.0f;
    return speed > 0.0f ? speed : 700.0f; // world units per second; fallback for Healing Spray when MissileSpeed is absent
}


/* ---- Healing Spray (ANhs): channeled point AoE heal waves ---------------- */

static bool healing_spray_hits(edict_t *caster, uint32_t code, edict_t *target, float radius, vec2_t const *origin) {
    if (!target || !S_SpellIsAliveTarget(target)) return false;
    if (Vector2_distance(&target->s.origin2, origin) > radius) return false;
    return S_SpellAllowsTarget(code, caster, target);
}

/* Healing occurs on the existing authored pulses.  The bottles are purely
 * visual: there must not be a second heal when a missile reaches the ground. */
static void healing_spray_bottle_hit(edict_t *bottle) {
    channel_t const *flight = G_GetChannel(bottle);
    if (flight) {
        uint32_t const effect = G_AbilityEffectCode(bottle->class_id, 1);
        if (effect && effect != bottle->class_id)
            G_SpawnAbilityEffectAtPoint(effect, WC3_EFFECT_EFFECT, 0, &flight->origin, true);
    }
    G_FreeEdict(bottle);
}

static umove_t healing_spray_bottle_move = {
    .animation = "stand", .endfunc = healing_spray_bottle_hit
};

static void healing_spray_bottles(edict_t *thinker) {
    edict_t *caster = S_SpellChannelOwner(thinker);
    uint32_t const rank = thinker->variation;
    uint32_t const count = (uint32_t)MAX(0.0f, S_SpellData(thinker->class_id, rank, 3));
    cstring_t const art = G_AbilityEffectArt(thinker->class_id, WC3_EFFECT_MISSILE, 0);
    float const radius = thinker->collision;
    if (!caster || !art || !*art || !count) return;
    FOR_LOOP(i, MIN(count, 64u)) { // bottles; bounds cosmetic projectile allocation for malformed custom ability data
        /* A deterministic dispersed pattern prevents every bottle following
         * exactly the same line.  Retail's random offsets remain unverified. */
        float const distance = sqrtf(((float)i + 0.5f) / (float)count) * radius * 0.8f;
        float const theta = (float)i * 2.39996323f; // radians; golden angle spaces bottle impacts without overlap bands
        vec2_t point = { thinker->s.origin2.x + cosf(theta) * distance,
                         thinker->s.origin2.y + sinf(theta) * distance };
        edict_t *bottle = G_Spawn();
        channel_t *flight;
        if (!bottle) break;
        bottle->class_id = thinker->class_id;
        bottle->owner = caster;
        bottle->s.player = caster->s.player;
        bottle->s.model = G_RegisterModel(art);
        bottle->s.origin = caster->s.origin;
        bottle->s.origin2 = caster->s.origin2;
        bottle->s.angle = atan2f(point.y - caster->s.origin2.y,
                                 point.x - caster->s.origin2.x);
        G_InheritUnitTeamColor(bottle, caster);
        flight = G_AddChannel(bottle);
        flight->owner_spawn_time = caster->spawn_time;
        flight->origin = point;
        bottle->velocity = alchemist_missile_speed(thinker->class_id) / 1000.0f;
        bottle->movetype = MOVETYPE_FLYMISSILE;
        bottle->aiflags |= AI_PROJECTILE_FIXED_TARGET;
        bottle->currentmove = &healing_spray_bottle_move;
        G_StartProjectilePresentation(bottle);
    }
}

void healing_spray_think(edict_t *ent) {
    uint32_t now = G_Time(), ntargets = 0;
    edict_t *caster = ent->owner;
    float heal = (float)ent->damage, maxheal = ent->velocity;

    if (!S_SpellChannelActive(ent)) { S_SpellEndChannel(ent); return; }
    if (ent->freetime && now < ent->freetime) return;
    healing_spray_bottles(ent);
    if (maxheal > 0.0f) {
        FILTER_EDICTS(target, healing_spray_hits(caster, ent->class_id, target, ent->collision, &ent->s.origin2))
            ntargets++;
        if (ntargets && heal * (float)ntargets > maxheal)
            heal = maxheal / (float)ntargets;
    }
    FILTER_EDICTS(target, healing_spray_hits(caster, ent->class_id, target, ent->collision, &ent->s.origin2))
        S_SpellHeal(target, heal);
    if (!--ent->resources) { S_SpellEndChannel(ent); return; }
    ent->freetime = now + (uint32_t)(MAX(0.1f, ent->wait) * 1000.0f);
}

/* Name=Healing Spray
 * Ubertip: DataF waves; each heals DataA to friendlies in Area. DataB interval,
 * DataD per-wave max heal budget. DataC visual bottles; DataE build factor
 * is unused because the authored target mask excludes structures.
 */
BZ_SIMPLE_SPELL_PROC(AbilityHealingSpray) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    edict_t *thinker = S_SpellChannelThinker(caster, spell->code);

    thinker->s.origin2 = st.point;
    thinker->s.origin.x = st.point.x;
    thinker->s.origin.y = st.point.y;
    thinker->collision = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    thinker->variation = level; /* data rank survives save/load for bottle count */
    thinker->damage = (uint32_t)MAX(0.0f, S_SpellData(spell->code, level, 1)); /* DataA gainedHP */
    thinker->wait = MAX(0.1f, S_SpellData(spell->code, level, 2)); /* DataB frequency */
    thinker->velocity = S_SpellData(spell->code, level, 4); /* DataD maxGainedHP */
    thinker->resources = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 6)); /* DataF waves */
    thinker->think = healing_spray_think;
    healing_spray_think(thinker);
}

/* ---- Acid Bomb (ANab): homing bottle, impact splash and status DOT ---- */

/* One independent damage thinker per unit, keyed to the current buff instance.
 * Replacement, dispel, expiry and death all retire the old thinker without
 * undoing a newer application's armor debuff.  The source and target edict
 * generations are checked through their serialized channel records. */
void acid_bomb_think(edict_t *thinker) {
    edict_t *target = S_SpellChannelTarget(thinker);
    edict_t *caster = S_SpellChannelOwner(thinker);
    heroabilitystatus_t *status = target ? unit_findstatus(target, thinker->resources) : NULL;
    uint32_t const now = G_Time();
    if (!target || !caster || M_IsDead(target) || !status ||
        status->instance_id != thinker->variation ||
        (status->timestamp && status->timestamp < now) || now > thinker->spawn_time) {
        G_FreeEdict(thinker);
        return;
    }
    if (now < thinker->freetime) return;
    /* Fractional secondary DPS (e.g. 8.5) is carried across whole-number
     * damage pulses instead of truncating half a point each second. */
    thinker->wait += thinker->velocity;
    int const damage = (int)floorf(thinker->wait);
    thinker->wait -= damage;
    if (damage > 0) S_SpellDamage(target, caster, damage);
    thinker->freetime += 1000;
}

static void acid_bomb_apply(edict_t *caster, uint32_t code, uint32_t rank,
                            edict_t *target, bool primary) {
    float const duration = S_SpellHeroDuration(code, rank, target);
    cstring_t buff = S_SpellBuffId(code, rank);
#if BZ_COMPAT_WORKAROUNDS
    if ((!buff || strlen(buff) < 4) && G_AbilityCode(code) == MAKEFOURCC('A','N','a','b')) buff = "BNab";
#endif
    heroabilitystatus_t *status;
    edict_t *thinker;
    wc3_status_modifier_t armor;
    channel_t *owners;
    if (!buff || !*buff || !target || duration <= 0.0f) return;
    status = unit_applystatus(target, &(status_application_t){
        .buff = buff, .level = rank, .duration = duration,
        .source_ability = code, .source = caster, .rank = rank,
        .buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL |
                      WC3_STATUS_BUFF_TRANSFERABLE
    });
    if (!status) return;
    armor = (wc3_status_modifier_t){
        .type = WC3_STATUS_MOD_ARMOR_FLAT, .family = ALCHEMIST_ACID_BUFF,
        .policy = WC3_STATUS_MOD_STRONGEST_NEGATIVE,
        .value = -MAX(0.0f, S_SpellData(code, rank, 3)),
        .owner = WC3_STATUS_MOD_OWNER_AUTHORED
    };
    (void)unit_status_setmodifier(status, 0, &armor);
    unit_status_enabletargetart(target, status, "chest");
    thinker = G_Spawn();
    if (!thinker) return; /* Status still expires and restores armor normally. */
    thinker->owner = caster;
    thinker->goalentity = target;
    thinker->class_id = code;
    owners = G_AddChannel(thinker);
    owners->owner_spawn_time = caster->spawn_time;
    owners->target_spawn_time = target->spawn_time;
    thinker->resources = FS_SLKKey(buff);
    thinker->variation = status->instance_id;
    thinker->velocity = MAX(0.0f, S_SpellData(code, rank, primary ? 4 : 5));
    thinker->freetime = G_Time() + 1000;
    thinker->spawn_time = G_Time() + (uint32_t)(duration * 1000.0f);
    thinker->think = acid_bomb_think;
}

static void acid_bomb_hit(edict_t *missile) {
    edict_t *caster = S_SpellProjectileOwner(missile);
    edict_t *primary = S_SpellProjectileTarget(missile);
    uint32_t rank = missile->resources;
    if (caster && primary && S_SpellIsAliveTarget(primary) &&
        !S_TryBlockSpellShield(caster, missile->class_id, primary)) {
        vec2_t const point = primary->s.origin2;
        float const area = S_SpellNumber(missile->class_id, ABILITY_NUMBER_AREA, rank);
        FILTER_EDICTS(unit, S_SpellIsAliveTarget(unit) &&
                      Vector2_distance(&unit->s.origin2, &point) <= area &&
                      S_SpellAllowsAreaTarget(missile->class_id, caster, unit)) {
            acid_bomb_apply(caster, missile->class_id, rank, unit, unit == primary);
        }
    }
    G_FreeEdict(missile);
}

static umove_t acid_bomb_missile_move = { "stand", NULL, acid_bomb_hit };

static bool acid_bomb_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return caster && S_SpellIsAliveTarget(st.entity);
}

static void acid_bomb_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t const rank = S_SpellLevel(caster, spell->code);
    edict_t *missile = S_SpawnUnitTargetSpellMissile(caster, spell->code, st.entity,
                                                     alchemist_missile_speed(spell->code),
                                                     &acid_bomb_missile_move);
    if (!missile) return;
    missile->resources = rank;
}

BZ_VALIDATED_SPELL_PROC(AbilityAcidBomb, acid_bomb_validate, acid_bomb_execute)

/* ---- Transmute (ANtm): kill target, credit goldCost * DataA -------------- */

static bool transmute_is_neutral(edict_t const *target) {
    return target && target->s.player < MAX_PLAYERS && level.mapinfo &&
        level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral;
}

static bool transmute_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t max_level = (uint32_t)S_SpellData(spell->code, level, 3); /* DataC maxCreepLv */
    UnitBalance_t const *bal;

    if (!target || !target->data.UnitBalance) return false;
    bal = target->data.UnitBalance;
    if (S_UnitIsResistant(target)) return false;
    if (!S_SpellIsEnemy(caster, target) && !transmute_is_neutral(target)) return false;
    if (max_level && (uint32_t)bal->level > max_level) return false;
    return true;
}

/* Resolve conversion at impact, not at launch.  The native death path still
 * owns XP, death triggers, corpses and score accounting. */
static void transmute_resolve(edict_t *caster, edict_t *target, uint32_t code, uint32_t rank) {
    UnitBalance_t const *bal = target->data.UnitBalance;
    gameClient_t *client = G_GetPlayerClientByNumber(caster->s.player);
    float const gold_factor = S_SpellData(code, rank, 1);
    float const lumber_factor = S_SpellData(code, rank, 2);
    int32_t gold, lumber;
    if (client) {
        gold = (int32_t)(MAX(0, bal->goldCost) * gold_factor);
        lumber = (int32_t)(MAX(0, bal->lumberCost) * lumber_factor);
        if (gold > 0) G_CreditResourceIncome(&client->ps, target, PLAYERSTATE_RESOURCE_GOLD, gold);
        if (lumber > 0) G_CreditResourceIncome(&client->ps, target, PLAYERSTATE_RESOURCE_LUMBER, lumber);
    }
    G_SpawnAbilityEffectAtPoint(FS_SLKKey("BNtm"), WC3_EFFECT_SPECIAL, 0, &target->s.origin2, true);
    /* Custom DataD=0 suppresses ordinary death bounty while retaining the
     * separate transmuted-gold income and all native death/XP events. */
    if (S_SpellData(code, rank, 4) <= 0.0f) target->aiflags |= AI_TRANSMUTE_NO_BOUNTY;
    G_SetHealth(target, 0);
    if (target->die) target->die(target, caster);
    else unit_die(target, caster);
    if (target->inuse) target->aiflags &= ~AI_TRANSMUTE_NO_BOUNTY;
}

static void transmute_missile_hit(edict_t *missile) {
    edict_t *caster = S_SpellProjectileOwner(missile);
    edict_t *target = S_SpellProjectileTarget(missile);
    abilityitem_t const spell = S_AbilityItem(missile->class_id);
    spellTarget_t const st = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);
    if (caster && target && S_SpellIsAliveTarget(target) &&
        transmute_validate(caster, st, &spell) &&
        !S_TryBlockSpellShield(caster, missile->class_id, target))
        transmute_resolve(caster, target, missile->class_id, missile->resources);
    G_FreeEdict(missile);
}

static umove_t transmute_missile_move = { "stand", NULL, transmute_missile_hit };

static void transmute_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t const rank = S_SpellLevel(caster, spell->code);
    /* Test/custom maps may intentionally omit missile art.  Preserve instant
     * behavior only for those definitions; authored TFT uses a gold bottle. */
    cstring_t const art = G_AbilityEffectArt(spell->code, WC3_EFFECT_MISSILE, 0);
    edict_t *missile;
    if (!art || !*art) {
        if (!S_TryBlockSpellShield(caster, spell->code, st.entity))
            transmute_resolve(caster, st.entity, spell->code, rank);
        return;
    }
    missile = S_SpawnUnitTargetSpellMissile(caster, spell->code, st.entity,
                                            alchemist_missile_speed(spell->code),
                                            &transmute_missile_move);
    if (missile) missile->resources = rank;
}

BZ_VALIDATED_SPELL_PROC(AbilityTransmute, transmute_validate, transmute_execute)
