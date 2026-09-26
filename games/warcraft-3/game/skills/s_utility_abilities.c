#include "s_skills.h"

#define ID_CHARM MAKEFOURCC('A', 'N', 'c', 'h')
#define ID_MOON_WELL MAKEFOURCC('A', 'm', 'b', 't')

/* ---- Charm (ANch): transfer target ownership to caster -------------------- */

static bool charm_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    uint32_t max_level = (uint32_t)S_SpellData(spell->code, level, 1);

    if (!S_SpellIsEnemy(caster, target)) return false;
    if (max_level && target->data.UnitBalance->level > max_level) return false;
    return true;
}

static void charm_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    (void)spell;

    G_SetUnitPlayer(target, caster->s.player);
    target->owner = caster;
    target->combatentity = NULL;
    if (target->stand)
        target->stand(target);
}

/* ---- Eat Tree (Aeat): consume a tree for healing ------------------------- */

static bool eat_tree_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    edict_t *target = st.entity;
    if (!target || target->targtype != TARG_TREE) return false;
    return true;
}

static void eat_tree_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    float heal = S_SpellData(spell->code, level, 3);

    S_SpellHeal(caster, heal);
    G_FreeEdict(target);
}

/* ---- Moon Well (Ambt): replenish, autocast, and night regeneration -------- */

static uint32_t moon_well_alias(edict_t const * ent) {
    char token_code[5] = {0};

    if (!ent) return 0;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            uint32_t alias = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&alias, token, 4);
            if (alias == ID_MOON_WELL || G_AbilityCode(alias) == ID_MOON_WELL)
                return alias;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        uint32_t const alias = ent->abilities.added[i];
        if (!alias) continue;
        memcpy(token_code, &alias, 4);
        token_code[4] = '\0';
        if (G_ActorHasSkill(ent, token_code) &&
            (alias == ID_MOON_WELL || G_AbilityCode(alias) == ID_MOON_WELL))
            return alias;
    }
    return 0;
}

/* DataE means the Moon Well's authored base mana regeneration only runs at
 * night.  Other regeneration sources stay independent instead of being
 * accidentally suppressed by this racial ability. */
bool S_MoonWellNaturalManaRegenAllowed(edict_t const * ent) {
    uint32_t const alias = moon_well_alias(ent);
    AbilityData_t const *data;

    if (!alias || !(data = G_AbilityData(alias)) || data->id != alias)
        return true;
    return data->level[0].data[4].number == 0.0f || G_IsNight(); /* DataE */
}

static bool moon_well_effect_matches(edict_t const * effect, edict_t const * well) {
    uint32_t const base = ID_MOON_WELL;
    return effect && effect->inuse && effect->owner == well && effect->summon_ability &&
           (effect->summon_ability == base || G_AbilityCode(effect->summon_ability) == base) &&
           (effect->s.flags & EF_NOT_SELECTABLE);
}

void S_MoonWellEffectsRelease(edict_t * well) {
    if (!well) return;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t * effect = globals.edicts + i;
        if (!moon_well_effect_matches(effect, well)) continue;
        effect->owner = NULL;
        effect->summon_ability = 0;
        G_DestroyEffect(effect);
    }
}

static uint32_t moon_well_effect_index(edict_t const * well) {
    if (!well || well->race < RACE_HUMAN || well->race > RACE_NIGHTELF) return 0;
    return (uint32_t)well->race - (uint32_t)RACE_HUMAN;
}

/* Warsmash renders the Moon Well's persistent EffectArt at DataD multiplied
 * by the current mana fraction. A linked effect lets the presentation follow
 * the unit while keeping the water height entirely data-driven. */
static void moon_well_update_effect(edict_t * well) {
    uint32_t alias, level;
    abilityLevel_t const *row;
    edict_t * effect = NULL;
    float fraction, height;

    if (!well || !(alias = moon_well_alias(well)) || M_IsDead(well) || well->construction.active) {
        if (well) S_MoonWellEffectsRelease(well);
        return;
    }
    level = S_SpellLevel(well, alias);
    if (!level || !(row = G_AbilityLevel(alias, level))) {
        S_MoonWellEffectsRelease(well);
        return;
    }

    FOR_LOOP(i, globals.num_edicts) {
        edict_t * candidate = globals.edicts + i;
        if (moon_well_effect_matches(candidate, well)) {
            effect = candidate;
            break;
        }
    }
    if (!effect) {
        effect = G_SpawnAbilityEffectTarget(alias, WC3_EFFECT_EFFECT, moon_well_effect_index(well),
                                            well, NULL, false);
        if (!effect) return;
        effect->owner = well;
    }
    effect->summon_ability = alias;
    fraction = well->mana.max_value > 0.0f ? well->mana.value / well->mana.max_value : 0.0f;
    fraction = MAX(0.0f, MIN(1.0f, fraction));
    height = row->data[3].number * fraction; /* DataD */
    effect->wait = height;
    effect->s.origin.z = well->s.origin.z + height;
    gi.LinkEntity(effect);
}

static bool moon_well_validate(edict_t * caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t * target = st.entity;
    uint32_t level;
    float mana_ratio;
    float life_ratio;

    if (!caster || !target || !spell || !S_SpellIsAliveTarget(target) ||
        !S_SpellIsFriend(caster, target) || !S_SpellAllowsTarget(spell->code, caster, target) ||
        caster->mana.value <= 0.0f) return false;
    level = S_SpellLevel(caster, spell->code);
    mana_ratio = S_SpellData(spell->code, level, 1); /* DataA: well mana / target mana */
    life_ratio = S_SpellData(spell->code, level, 2); /* DataB: well mana / target HP */
    if (life_ratio > 0.0f && target->health.value < target->health.max_value) return true;
    return mana_ratio > 0.0f && target->mana.value < target->mana.max_value;
}

static void moon_well_execute(edict_t * caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t * target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    float mana_ratio = S_SpellData(spell->code, level, 1);
    float life_ratio = S_SpellData(spell->code, level, 2);
    float before = MAX(0.0f, caster->mana.value);
    float available = before;

    /* Warsmash/Warcraft spends the same Moon Well pool sequentially: restore
     * missing life first with DataB, then spend what remains on missing mana
     * with DataA. These values are ratios, not per-cast restoration caps. */
    if (life_ratio > 0.0f && available > 0.0f) {
        float wanted = MAX(0.0f, target->health.max_value - target->health.value);
        float gained = MIN(wanted, available / life_ratio);
        if (gained > 0.0f) {
            S_SpellHeal(target, gained);
            available -= gained * life_ratio;
        }
    }
    if (mana_ratio > 0.0f && available > 0.0f) {
        float wanted = MAX(0.0f, target->mana.max_value - target->mana.value);
        float gained = MIN(wanted, available / mana_ratio);
        if (gained > 0.0f) {
            target->mana.value = MIN(target->mana.max_value, target->mana.value + gained);
            available -= gained * mana_ratio;
        }
    }
    caster->mana.value = MAX(0.0f, available);
    if (caster->mana.value < before) {
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
        G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_SPECIAL, 0, target, NULL, true);
    }
}

/* Moon Wells use Warsmash's nearest-valid autocast policy. DataC is the
 * minimum well-mana threshold; Area is the acquisition/cast radius. */
static bool moon_well_autocast_acquire(edict_t * caster, uint32_t code) {
    uint32_t const level = S_SpellLevel(caster, code);
    AbilityData_t const *data = G_AbilityData(code);
    abilityLevel_t const *row;
    float range;
    float best_distance = FLT_MAX;
    edict_t * best = NULL;
    abilityitem_t item = S_AbilityItem(code);

    if (!caster || !data || data->id != code || !level) return false;
    row = G_AbilityLevel(code, level);
    if (caster->mana.value <= row->data[2].number) /* DataC */ return false;
    range = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    if (range <= 0.0f) range = S_SpellRange(code, level);
    if (range <= 0.0f) return false;

    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target)) {
        float distance;
        spellTarget_t st = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);
        if (!moon_well_validate(caster, st, &item)) continue;
        distance = Vector2_distance(&target->s.origin2, &caster->s.origin2);
        if (distance <= range && distance < best_distance) {
            best = target;
            best_distance = distance;
        }
    }
    return best && S_CastUnitTargetSpell(caster, code, best);
}

/* ---- Registration -------------------------------------------------------- */

BZ_VALIDATED_SPELL_PROC(AbilityCharm, charm_validate, charm_execute)

BZ_VALIDATED_SPELL_PROC(AbilityEatTree, eat_tree_validate, eat_tree_execute)

BZ_ABILITY_PROC(CAbilityManaBattery) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_VALIDATE: return moon_well_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: moon_well_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return code && moon_well_autocast_acquire(ent, code);
    case A_UPDATE: moon_well_update_effect(ent); return true;
    case A_DISABLE: S_MoonWellEffectsRelease(ent); return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Root (Aroo): toggle rooted state ------------------------------------ */

BZ_COMMAND_PROC(AbilityRoot) {
    edict_t *caster = G_GetMainSelectedUnit(clent->client);
    if (!caster) return;
    caster->no_pathing = !caster->no_pathing;
    caster->movetype = caster->no_pathing ? MOVETYPE_NONE : MOVETYPE_STEP;
    if (caster->stand)
        caster->stand(caster);
}
