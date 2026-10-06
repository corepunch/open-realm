#include "s_skills.h"

#define ID_CHARM MAKEFOURCC('A', 'N', 'c', 'h')
#define ID_MOON_WELL MAKEFOURCC('A', 'm', 'b', 't')
#define ID_ROOT_LIFE MAKEFOURCC('A', 'r', 'o', 'o')
#define ID_ROOT_ANCIENT MAKEFOURCC('A', 'r', 'o', '1')
#define ID_ROOT_PROTECTOR MAKEFOURCC('A', 'r', 'o', '2')

static umove_t ancient_root_morph, ancient_uproot_morph;

#ifdef BZ_TESTS
static uint32_t moon_well_effect_release_calls;
static uint32_t moon_well_missing_data_warning_calls;
void S_TestResetMoonWellEffectReleaseCalls(void) { moon_well_effect_release_calls = 0; }
uint32_t S_TestMoonWellEffectReleaseCalls(void) { return moon_well_effect_release_calls; }
void S_TestResetMoonWellMissingDataWarningCalls(void) { moon_well_missing_data_warning_calls = 0; }
uint32_t S_TestMoonWellMissingDataWarningCalls(void) { return moon_well_missing_data_warning_calls; }
#endif

static bool root_code(uint32_t code) {
    return code == ID_ROOT_LIFE || code == ID_ROOT_ANCIENT || code == ID_ROOT_PROTECTOR;
}

static uint32_t ancient_root_ability(edict_t const *unit) {
    char name[5] = {0};
    if (!unit) return 0;
    if (unit->data.UnitAbilities && unit->data.UnitAbilities->abilList) {
        PARSE_LIST(unit->data.UnitAbilities->abilList, token, parse_segment) {
            uint32_t code = 0;
            if (strlen(token) != 4) continue;
            memcpy(&code, token, 4);
            if (root_code(code) && G_ActorHasSkill(unit, token)) return code;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(unit->abilities.added)) {
        uint32_t code = unit->abilities.added[i];
        if (!root_code(code)) continue;
        memcpy(name, &code, 4);
        if (G_ActorHasSkill(unit, name)) return code;
    }
    return 0;
}

bool S_AncientIsRooted(edict_t const *unit) {
    if (!unit || !root_code(ancient_root_ability(unit))) return false;
    if (!unit->ancient_root || unit->ancient_root->mode == ANCIENT_ROOT_UNINITIALIZED)
        return G_UnitIsStructure(unit) && (unit->aiflags & AI_IMMOBILE);
    return unit->ancient_root->mode == ANCIENT_ROOTED;
}

bool S_AncientHasRootAbility(edict_t const *unit) {
	return unit && unit->ancient_root && root_code(unit->ancient_root->ability) ? true : ancient_root_ability(unit) != 0;
}

bool S_AncientCanReceiveOrder(edict_t const *unit) {
    return !unit || !S_AncientHasRootAbility(unit) || !unit->ancient_root ||
        unit->ancient_root->mode == ANCIENT_ROOT_UNINITIALIZED ||
        unit->ancient_root->mode == ANCIENT_ROOTED ||
        unit->ancient_root->mode == ANCIENT_UPROOTED ||
        (unit->ancient_root->mode == ANCIENT_ROOTING && unit->ancient_root->approaching);
}

bool S_AncientAbilityAvailable(edict_t const *unit, ability_t const *ability) {
    bool rooted;
    if (!unit || !S_AncientHasRootAbility(unit)) return true;
    if (!ability || !S_AncientCanReceiveOrder(unit)) return false;
    if (ability->proc == CAbilityRoot) return true;
    if (ability->proc == CAbilityMove || ability->proc == CAbilityAttack ||
        ability->proc == CAbilityStop || ability->proc == CAbilityHoldPosition ||
        ability->proc == CAbilityPatrol) return true;
    rooted = S_AncientIsRooted(unit);
    if (ability->proc == CAbilityEatTree) return !rooted;
    if (ability->proc == CAbilityEntangle) return rooted;
    return rooted;
}

uint32_t S_AncientAttackMask(edict_t const *unit) {
    abilityLevel_t const *level;
    AbilityData_t const *data;
    uint32_t ability = unit && unit->ancient_root ? unit->ancient_root->ability : 0;
    if (!ability) ability = ancient_root_ability(unit);
    if (!unit || !ability) return 3;
    data = G_AbilityData(ability);
    if (data->id != ability) {
        fprintf(stderr, "WC3 Ancient Root: missing AbilityData %08x (attack mask)\n",
                ability);
        return 3;
    }
    level = G_AbilityLevel(ability, 1);
    return (uint32_t)level->data[S_AncientIsRooted(unit) ? 0 : 1].number;
}

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
    if (S_AncientIsRooted(caster)) return false;
    if (S_AncientHasRootAbility(caster) && (!caster->ancient_root || caster->ancient_root->mode != ANCIENT_UPROOTED)) return false;
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

static void moon_well_warn_missing_data(uint32_t alias) {
    static uint32_t warned[256];
    static uint32_t warned_count;
    char name[5] = {0};
#ifdef BZ_TESTS
    moon_well_missing_data_warning_calls++;
#endif
    if (!alias) return;
    FOR_LOOP(i, warned_count) if (warned[i] == alias) return;
    if (warned_count < sizeof(warned) / sizeof(warned[0])) warned[warned_count++] = alias;
    memcpy(name, &alias, 4);
    fprintf(stderr, "WC3 AbilityData: missing %s row; suppressing Moon Well natural mana regeneration\n",
            name);
}

static bool moon_well_effect_matches(edict_t const * effect, edict_t const * well) {
    uint32_t const base = ID_MOON_WELL;
    return effect && effect->inuse && effect->owner == well && effect->summon_ability &&
           (effect->summon_ability == base || G_AbilityCode(effect->summon_ability) == base) &&
           (effect->s.flags & EF_NOT_SELECTABLE);
}

void S_MoonWellEffectsRelease(edict_t * well) {
    if (!well) return;
#ifdef BZ_TESTS
    moon_well_effect_release_calls++;
#endif
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

    /* AB_UPDATE procedures run for every unit. Non-owners and dead wells are
     * cheap no-ops; disable/death/removal own their teardown. */
    if (!well || !(alias = moon_well_alias(well)) || M_IsDead(well)) return;
    /* An authored Moon Well can enter an in-place upgrade construction state.
     * Keep its presentation suppressed while that state is active. */
    if (well->construction) {
        S_MoonWellEffectsRelease(well);
        return;
    }
    level = S_SpellLevel(well, alias);
    if (!level || !(row = G_AbilityLevel(alias, level))) return;

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
    case A_NATURAL_MANA_REGEN_BLOCKED:
        if (!ent || !code || G_AbilityCode(code) != ID_MOON_WELL) return false;
        {
            AbilityData_t const *data = G_AbilityData(code);
            if (data->id != code) {
                moon_well_warn_missing_data(code);
                return true;
            }
            return ent->construction ||
                   (data->level[0].data[4].number != 0.0f && !G_IsNight()); /* DataE */
        }
    case A_DEATH:
    case A_UNIT_REMOVE:
    case A_DISABLE: S_MoonWellEffectsRelease(ent); return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Ancient Root/Uproot ------------------------------------------------- */

static float ancient_root_duration(edict_t const *unit, bool rooted) {
    AbilityData_t const *data;
    assert(unit && unit->ancient_root);
    if (!unit->ancient_root->ability) return 0.0f;
    data = G_AbilityData(unit->ancient_root->ability);
    if (data->id != unit->ancient_root->ability) {
        fprintf(stderr, "WC3 Ancient Root: missing AbilityData %08x (morph duration)\n",
                unit->ancient_root->ability);
        return -1.0f;
    }
    return MAX(0.0f, rooted ? data->level[0].dur : data->level[0].heroDur);
}

static float ancient_root_animation_duration(edict_t const *unit) {
    return ancient_root_duration(unit, unit && unit->currentmove == &ancient_root_morph);
}

static void ancient_root_morph_think(edict_t *unit) {
    if (unit) unit->wait = MAX(0.0f, unit->wait - FRAMETIME / 1000.0f);
}

static bool ancient_root_restore_pathing(edict_t *unit) {
    cstring_t pathing;
    if (!unit || unit->pathtex || !unit->data.UnitData) return false;
    pathing = unit->data.UnitData->pathingTexture;
    if (!pathing || !*pathing) return false;
    unit->pathtex = M_LoadPathTex(pathing);
    return unit->pathtex != NULL;
}

void S_AncientBeginMorph(edict_t *unit, bool rooted) {
    float duration;
    if (!unit) return;
    assert(unit->ancient_root);
    duration = ancient_root_duration(unit, rooted);
    if (duration < 0.0f) return;
    unit->ancient_root->approaching = false;
    unit->ancient_root->transition_end_time = G_Time() + (uint32_t)(duration * 1000.0f);
    unit->ancient_root->mode = rooted ? ANCIENT_ROOTING : ANCIENT_UPROOTING;
    unit->wait = duration;
    unit_setmove(unit, rooted ? &ancient_root_morph : &ancient_uproot_morph);
    if (unit->animation) unit->s.frame = unit->animation->interval[0];
}

static void ancient_root_commit(edict_t *unit, bool rooted) {
    abilityLevel_t const *level;
    if (!unit || !unit->inuse || M_IsDead(unit)) return;
    assert(unit->ancient_root);
    level = G_AbilityLevel(unit->ancient_root->ability, 1);
    if (rooted) {
        ancient_root_restore_pathing(unit);
        if (unit->ancient_root->has_rooted_collision) unit->collision = unit->ancient_root->rooted_collision;
        unit->aiflags |= AI_IMMOBILE;
        unit->runtime.flags |= UNIT_BALANCE_BUILDING;
        unit->s.flags |= EF_BUILDING;
        unit->movetype = MOVETYPE_NONE;
        unit->defense_type = unit->ancient_root->rooted_defense_type;
        unit->s.angle = game.constants.rootAngle * (float)M_PI / 180.0f;
        G_AddUnitAnimationProperties(unit, "alternate", true);
        unit->ancient_root->mode = ANCIENT_ROOTED;
    } else {
        S_ReleaseEntangledMineForTree(unit);
        unit->aiflags &= ~AI_IMMOBILE;
        unit->runtime.flags &= ~UNIT_BALANCE_BUILDING;
        unit->s.flags &= ~EF_BUILDING;
        unit->movetype = MOVETYPE_STEP;
        if (unit->ancient_root->has_mobile_collision) unit->collision = unit->ancient_root->mobile_collision;
        if (level) unit->defense_type = (uint32_t)level->data[3].number;
        G_AddUnitAnimationProperties(unit, "alternate", false);
        unit->ancient_root->mode = ANCIENT_UPROOTED;
    }
    unit->s.collision = unit->collision;
    unit->ancient_root->transition_end_time = 0;
    unit->ancient_root->approaching = false;
    CM_BakeStaticObstacles();
    gi.LinkEntity(unit);
    unit_stand(unit);
    /* Retail Ancients automatically begin ordinary Entangle when a completed
     * Root leaves an eligible Gold Mine in authored Aent range. Initial melee
     * setup remains script-owned and uses its explicit entangleinstant order. */
    if (rooted) S_AutoEntangleNearby(unit, false);
    {
        gameClient_t *owner = G_GetPlayerClientByNumber(unit->s.player);
        if (owner) G_InvalidateCommands(owner);
    }
}

static bool ancient_root_validate(edict_t *unit, vec2_t const *point, vec2_t *snapped) {
    return unit && point && G_EvaluateRootPlacement(unit, point, snapped) == PLACE_OK;
}

static bool ancient_root_select_location(edict_t *clent, vec2_t const *point) {
    edict_t *unit;
    vec2_t snapped;
    if (!clent || !clent->client || !point || !(unit = G_GetMainSelectedUnit(clent->client)) ||
        !G_UnitCanControl(clent->client, unit) || !ancient_root_ability(unit) ||
        unit->ancient_root->mode != ANCIENT_UPROOTED) {
        G_ShowCommandErrorKey(clent, "Cantrootunit", "Target is no longer rootable.");
        G_ClearRootPlacementCursor(clent);
        return false;
    }
    if (!ancient_root_validate(unit, point, &snapped)) {
        G_ShowCommandErrorKey(clent, "Cantroot", "Unable to root there.");
        return false;
    }
    G_ClearRootPlacementCursor(clent);
    clent->client->menu.on_location_selected = NULL;
    clent->client->menu.supports_order_queue = false;
    unit->ancient_root->destination = snapped;
    unit->ancient_root->mode = ANCIENT_ROOTING;
    unit->ancient_root->approaching = true;
    order_move(unit, Waypoint_add(&snapped));
    unit->ancient_root->approach_goal = unit->goalentity;
    unit->ancient_root->approach_goal_spawn_time = unit->goalentity ? unit->goalentity->spawn_time : 0;
    if (!unit->currentmove || unit->currentmove->proc != CAbilityMove || !unit->goalentity) {
        unit->ancient_root->mode = ANCIENT_UPROOTED;
        unit->ancient_root->approaching = false;
        unit->ancient_root->approach_goal = NULL;
        return false;
    }
    return true;
}

static void ancient_root_command(edict_t *clent) {
    edict_t *unit;
    if (!clent || !clent->client || !(unit = G_GetMainSelectedUnit(clent->client)) ||
        !G_UnitCanControl(clent->client, unit) || !ancient_root_ability(unit)) return;
    if (!unit->ancient_root || unit->ancient_root->mode == ANCIENT_ROOT_UNINITIALIZED) {
        if (!unit->ancient_root) unit->ancient_root = G_AllocAncientRoot();
        assert(unit->ancient_root);
        unit->ancient_root->ability = ancient_root_ability(unit);
        unit->ancient_root->unit_type = unit->class_id;
        unit->ancient_root->rooted_defense_type = FindEnumValue(unit->data.UnitBalance->defenseType, defense_type);
        unit->ancient_root->mobile_collision = G_UnitCollision(unit->class_id);
        unit->ancient_root->has_mobile_collision = unit->ancient_root->mobile_collision > 0.0f;
        unit->ancient_root->rooted_collision = unit->collision;
        unit->ancient_root->has_rooted_collision = true;
        unit->ancient_root->mode = G_UnitIsStructure(unit) ? ANCIENT_ROOTED : ANCIENT_UPROOTED;
    }
    if (unit->ancient_root->mode == ANCIENT_ROOTED) {
        if (unit->training || unit->construction || unit->build || G_BuildingUpgradeActive(unit)) {
            G_ShowCommandErrorKey(clent, "Cantrootunit", "Unable to uproot while this Ancient is busy.");
            return;
        }
        unit->ancient_root->destination = unit->s.origin2;
        S_ReleaseEntangledMineForTree(unit);
        S_AncientBeginMorph(unit, false);
        return;
    }
    if (unit->ancient_root->mode != ANCIENT_UPROOTED) return;
    clent->client->menu.on_location_selected = ancient_root_select_location;
    G_ShowRootPlacementCursor(clent, unit);
}

static void ancient_root_update(edict_t *unit) {
    abilityLevel_t const *level;
    bool pathing_changed = false;
    if (!unit || !unit->inuse || M_IsDead(unit)) return;
    if (!unit->ancient_root || unit->ancient_root->mode == ANCIENT_ROOT_UNINITIALIZED) {
        uint32_t alias = ancient_root_ability(unit);
        if (!alias) return;
        if (!unit->ancient_root) unit->ancient_root = G_AllocAncientRoot();
        assert(unit->ancient_root);
        unit->ancient_root->ability = alias;
        unit->ancient_root->unit_type = unit->class_id;
        unit->ancient_root->rooted_defense_type = FindEnumValue(unit->data.UnitBalance->defenseType, defense_type);
        unit->ancient_root->mobile_collision = G_UnitCollision(unit->class_id);
        unit->ancient_root->has_mobile_collision = unit->ancient_root->mobile_collision > 0.0f;
        unit->ancient_root->rooted_collision = unit->collision;
        unit->ancient_root->has_rooted_collision = true;
        unit->ancient_root->mode = G_UnitIsStructure(unit) ? ANCIENT_ROOTED : ANCIENT_UPROOTED;
        pathing_changed = true;
    }
    if (unit->ancient_root->unit_type != unit->class_id) {
        unit->ancient_root->unit_type = unit->class_id;
        unit->ancient_root->rooted_defense_type = FindEnumValue(unit->data.UnitBalance->defenseType, defense_type);
        unit->ancient_root->mobile_collision = G_UnitCollision(unit->class_id);
        unit->ancient_root->has_mobile_collision = unit->ancient_root->mobile_collision > 0.0f;
        unit->ancient_root->rooted_collision = unit->collision;
        unit->ancient_root->has_rooted_collision = true;
        pathing_changed = true;
    }
    assert(unit->ancient_root);
    level = G_AbilityLevel(unit->ancient_root->ability, 1);
    if (level) unit->ancient_root->rooted_turning = level->data[2].number != 0.0f;
    if (unit->ancient_root->mode == ANCIENT_ROOTED || unit->ancient_root->mode == ANCIENT_UPROOTED) {
        bool rooted = unit->ancient_root->mode == ANCIENT_ROOTED;
        if (G_UnitIsStructure(unit) != rooted) pathing_changed = true;
        if (rooted) {
            if (ancient_root_restore_pathing(unit)) pathing_changed = true;
            unit->aiflags |= AI_IMMOBILE;
            unit->runtime.flags |= UNIT_BALANCE_BUILDING;
            unit->s.flags |= EF_BUILDING;
            unit->movetype = MOVETYPE_NONE;
            unit->defense_type = unit->ancient_root->rooted_defense_type;
            if (unit->ancient_root->has_rooted_collision) unit->collision = unit->ancient_root->rooted_collision;
        } else {
            unit->aiflags &= ~AI_IMMOBILE;
            unit->runtime.flags &= ~UNIT_BALANCE_BUILDING;
            unit->s.flags &= ~EF_BUILDING;
            unit->movetype = MOVETYPE_STEP;
            if (unit->ancient_root->has_mobile_collision) unit->collision = unit->ancient_root->mobile_collision;
            if (level) unit->defense_type = (uint32_t)level->data[3].number;
        }
        unit->s.collision = unit->collision;
        if (pathing_changed) {
            G_AddUnitAnimationProperties(unit, "alternate", rooted);
            CM_BakeStaticObstacles();
        }
    }
    if (unit->ancient_root->mode == ANCIENT_ROOTING && unit->ancient_root->approaching) {
        if (!unit->currentmove || unit->currentmove->proc != CAbilityMove ||
            unit->goalentity != unit->ancient_root->approach_goal || !unit->goalentity ||
            unit->goalentity->spawn_time != unit->ancient_root->approach_goal_spawn_time) {
            unit->ancient_root->mode = ANCIENT_UPROOTED;
            unit->ancient_root->approaching = false;
            unit->ancient_root->approach_goal = NULL;
        }
        return;
    }
    if (unit->ancient_root->mode == ANCIENT_ROOTING || unit->ancient_root->mode == ANCIENT_UPROOTING) {
        bool const rooted = unit->ancient_root->mode == ANCIENT_ROOTING;
        umove_t const *expected = rooted ? &ancient_root_morph : &ancient_uproot_morph;
        if (G_Time() >= unit->ancient_root->transition_end_time && unit->currentmove == expected) {
            ancient_root_commit(unit, rooted);
        }
    }
}

BZ_ABILITY_PROC(CAbilityRoot) {
    /* Root is a single command whose alternate presentation is Uproot while
     * the Ancient occupies rooted building mode. The HUD uses this response
     * to resolve the authored Unart/Untip fields for the command button. */
    if (msg == A_TOGGLE_ON) return S_AncientIsRooted(ent);
    if (msg == A_ORDER && call && call->order) {
        if (ent && (!ent->ancient_root || ent->ancient_root->mode == ANCIENT_ROOT_UNINITIALIZED))
            ancient_root_update(ent);
        if (!strcmp(call->order, "unroot") && ent && ent->ancient_root && ent->ancient_root->mode == ANCIENT_ROOTED &&
            !ent->training && !ent->construction && !ent->build && !G_BuildingUpgradeActive(ent)) {
            ent->ancient_root->destination = ent->s.origin2;
            S_ReleaseEntangledMineForTree(ent);
            S_AncientBeginMorph(ent, false);
            return true;
        }
        return false;
    }
    switch (msg) {
    case A_COMMAND: ancient_root_command(call && call->client ? call->client : ent); return true;
    case A_UPDATE: ancient_root_update(ent); return true;
    case A_MOVE_LEAVE:
        if (ent && ent->ancient_root && !ent->ancient_root->approaching &&
            (ent->ancient_root->mode == ANCIENT_ROOTING || ent->ancient_root->mode == ANCIENT_UPROOTING) &&
            call && call->next_move_proc != CAbilityRoot) {
            ent->ancient_root->mode = ent->ancient_root->mode == ANCIENT_ROOTING ?
                ANCIENT_UPROOTED : ANCIENT_ROOTED;
            ent->ancient_root->transition_end_time = 0;
        }
        if (ent && ent->ancient_root && ent->ancient_root->mode == ANCIENT_ROOTING && ent->ancient_root->approaching &&
            call && call->next_move_proc != CAbilityMove) {
            ent->ancient_root->mode = ANCIENT_UPROOTED;
            ent->ancient_root->approaching = false;
            ent->ancient_root->approach_goal = NULL;
        }
        return false;
    case A_MOVE_ARRIVE:
        if (ent && ent->ancient_root && ent->ancient_root->mode == ANCIENT_ROOTING && ent->ancient_root->approaching &&
            ent->goalentity == ent->ancient_root->approach_goal && ent->goalentity &&
            ent->goalentity->spawn_time == ent->ancient_root->approach_goal_spawn_time) {
            if (Vector2_distance(&ent->s.origin2, &ent->ancient_root->destination) > 1.0f ||
                !ancient_root_validate(ent, &ent->ancient_root->destination, &ent->ancient_root->destination)) {
                ent->ancient_root->mode = ANCIENT_UPROOTED;
                ent->ancient_root->approaching = false;
                ent->ancient_root->approach_goal = NULL;
                {
                    edict_t *player = G_GetPlayerEntityByNumber(ent->s.player);
                    if (player) G_ShowCommandErrorKey(player, "Cantroot", "Unable to root there.");
                }
                return false;
            }
            ent->ancient_root->approach_goal = NULL;
            S_AncientBeginMorph(ent, true);
            return true;
        }
        return false;
    case A_DEATH:
    case A_UNIT_REMOVE:
        if (ent && ent->ancient_root) {
            ent->ancient_root->mode = ANCIENT_ROOT_UNINITIALIZED;
            ent->ancient_root->transition_end_time = 0;
            ent->ancient_root->approaching = false;
            ent->ancient_root->approach_goal = NULL;
        }
        return false;
    default: return false;
    }
}

static umove_t ancient_root_morph = { "morph", ancient_root_morph_think, NULL, CAbilityRoot, ancient_root_animation_duration };
static umove_t ancient_uproot_morph = { "morph alternate", ancient_root_morph_think, NULL, CAbilityRoot, ancient_root_animation_duration };
