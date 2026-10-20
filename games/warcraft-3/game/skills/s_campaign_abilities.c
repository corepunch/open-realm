#include "s_skills.h"

static void campaign_status_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = S_SpellBuffId(spell->code, level);
    if (st.entity && buff) S_SpellApplyTimedStatus(st.entity, buff, level, S_SpellHeroDuration(spell->code, level, st.entity));
}

static void campaign_area_damage_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    S_SpellDamageEnemiesInRadius(caster, &st.point, S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level),
                                 (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1)));
}

static void campaign_toggle_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)st;
    S_ToggleUnitAbilityStatus(caster, spell->code, S_SpellLevel(caster, spell->code));
}

BZ_SIMPLE_SPELL_PROC(AbilityAttributeModSkill) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySpawnTentacle) { S_SummonAbilityUnits(caster, spell->code, &st); }
BZ_SIMPLE_SPELL_PROC(AbilityAvatarCampaign) {
    uint32_t level = S_SpellLevel(caster, spell->code), form = S_SpellUnitId(spell->code, level);
    if (form) G_TransformUnitType(caster, form);
}
/* Dark Conversion consumes its victim only after publishing the spawned unit;
 * campaign JASS observes that summon and owns its final replacement/order. */
BZ_SIMPLE_SPELL_PROC(AbilityDarkConversion) {
    uint32_t level = S_SpellLevel(caster, spell->code), unit = S_SpellUnitId(spell->code, level);
    edict_t *summon;
    cstring_t buff;

    if (!caster || !st.entity || !unit) return;
    summon = S_SummonAbilityAt(caster, spell->code, unit, &st.entity->s.origin2, 0.0f);
    if (!summon) return;
    buff = S_SpellBuffId(spell->code, level);
    if (buff) S_SpellApplyTimedStatus(summon, buff, level, S_SpellDuration(spell->code, level, false));
    G_FreeEdict(st.entity);
}
BZ_SIMPLE_SPELL_PROC(AbilityShockwaveCampaign) { campaign_area_damage_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityWarStompCampaign) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level), duration = S_SpellDuration(spell->code, level, false);
    uint32_t damage = (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1));
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) && G_UnitTargetType(target) == TARG_GROUND && Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area) {
        S_SpellDamage(target, caster, damage);
        if (!M_IsDead(target) && duration > 0.0f) S_SpellApplyStun(target, duration);
    }
}
BZ_SIMPLE_SPELL_PROC(AbilityFeralSpiritCampaign) { S_SummonAbilityUnits(caster, spell->code, &st); }
BZ_SIMPLE_SPELL_PROC(AbilitySpiritBeast) { S_SummonAbilityUnits(caster, spell->code, &st); }
BZ_SIMPLE_SPELL_PROC(AbilityReincarnationCampaign) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityFeedbackCampaign) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityAbolishMagic) {
    uint32_t level = S_SpellLevel(caster, spell->code), count = 0; float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FILTER_EDICTS(target, count < (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1)) && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) && Vector2_distance(&target->s.origin2, &st.point) <= area) {
        FOR_LOOP(i, G_UnitStatusSlotCount(target)) if (unit_status_can_magic_dispel(target->abilstatus + i))
            unit_removestatus_deferred(target, target->abilstatus + i, STATUS_REMOVE_DISPEL);
        unit_refreshstatusflags(target);
        G_InvalidateUnitInfoPanel(target);
        count++;
    }
}
cstring_t const submerge_orders[] = { "submerge", "unsubmerge", NULL };

#define ID_ASB1 MAKEFOURCC('A','s','b','1')
#define ID_ASB2 MAKEFOURCC('A','s','b','2')
#define ID_ASB3 MAKEFOURCC('A','s','b','3')
#define ID_ANSU MAKEFOURCC('A','N','s','u')

static bool submerge_base_code(uint32_t code) {
    uint32_t const base = G_AbilityCode(code);
    return base == ID_ASB1 || base == ID_ASB2 || base == ID_ASB3 || base == ID_ANSU;
}

static heroabilitystatus_t *submerge_status(edict_t *unit) {
    if (!unit) return NULL;
    FOR_LOOP(i, G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *status = unit->abilstatus + i;
        if (status->level && submerge_base_code(G_UnitStatusClassCode(status))) return status;
    }
    return NULL;
}

bool S_UnitIsSubmerged(edict_t const *unit) {
    if (!unit) return false;
    FOR_LOOP(i, G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t const *status = unit->abilstatus + i;
        if (status->level && submerge_base_code(G_UnitStatusClassCode(status))) return true;
    }
    return false;
}

static bool submerge_types(uint32_t code, uint32_t *normal, uint32_t *submerged) {
    AbilityData_t const *ability = G_AbilityData(code);
    if (!ability || !ability->id || !ability->level[0].data[0].id || !ability->level[0].unitID) return false;
    if (normal) *normal = ability->level[0].data[0].id;
    if (submerged) *submerged = ability->level[0].unitID;
    return true;
}

static bool submerge_deep_water(edict_t const *unit) {
    return unit && CM_TerrainPointIsSwimmable(&unit->s.origin2) &&
           !CM_TerrainPointIsWalkable(&unit->s.origin2);
}

static uint32_t submerge_find_code(edict_t const *unit) {
    char const *abilities;
    if (!unit) return 0;
    FOR_LOOP(i, G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t const *status = unit->abilstatus + i;
        if (status->level && submerge_base_code(G_UnitStatusClassCode(status))) return G_UnitStatusClassCode(status);
    }
    if ((abilities = unit->data.UnitAbilities ? unit->data.UnitAbilities->abilList : NULL)) {
        PARSE_LIST(abilities, token, parse_segment) {
            uint32_t code = 0;
            if (strlen(token) != 4) continue;
            memcpy(&code, token, 4);
            if (submerge_base_code(code) && G_UnitAbilityClassLevel(unit, code)) return code;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(unit->abilities.added)) {
        uint32_t const code = unit->abilities.added[i];
        if (code && submerge_base_code(code) && G_UnitAbilityClassLevel(unit, code)) return code;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        uint32_t const code = unit->heroabilities[i].level ? unit->heroabilities[i].code : 0;
        if (code && submerge_base_code(code) && G_UnitAbilityClassLevel(unit, code)) return code;
    }
    return 0;
}

static bool submerge_validate(edict_t *unit, uint32_t code) {
    uint32_t normal, submerged;
    heroabilitystatus_t *status;
    if (!unit || !submerge_types(code, &normal, &submerged) ||
        (unit->morph && unit->morph->transition.active)) return false;
    status = submerge_status(unit);
    if (status) return unit->class_id == submerged;
    return unit->class_id == normal && submerge_deep_water(unit);
}

/*4ba940 changes occupancy c2 while preserving the query and bit8. */
static void submerge_category(edict_t *unit,bool hidden) {
    if(!unit->morph && hidden) {
        unit->morph=G_AllocMorph();assert(unit->morph);
    }
    if(unit->morph) {
        unit->morph->category_clear=hidden ? 0xc2 : 0;
        unit->morph->category_set=hidden ? 8 : 0xca;
    }
    gi.LinkEntity(unit);
}
static void submerge_publish(edict_t *unit,uint32_t code,bool hidden) {
    bool before=S_UnitHasInvisibilityState(unit);
    if(S_UnitIsSubmerged(unit)!=hidden)
        S_ToggleUnitAbilityStatus(unit,code,MAX(1,G_UnitAbilityClassLevel(unit,code)));
    if(hidden)unit->s.renderfx|=RF_HIDDEN;
    else if(!S_UnitHasInvisibilityState(unit))unit->s.renderfx&=~RF_HIDDEN;
    submerge_category(unit,hidden);
    bool after=S_UnitHasInvisibilityState(unit);
    if(!before && after)S_UnitTargetLost(unit);
    else if(before && !after)S_UnitTargetAvailable(unit);
}
static bool submerge_execute(edict_t *unit,abilityCall_t const *call) {
    uint32_t code=call->item->code,normal,submerged;
    bool active=S_UnitIsSubmerged(unit);
    if(!unit || !submerge_types(code,&normal,&submerged) ||
       (!active && !submerge_deep_water(unit)) ||
       unit->class_id!=(active ? submerged : normal))return false;
    /* Original virtual410 publishes the inverse BEFORE the reverse hold. */
    if(active)submerge_publish(unit,code,false);
    bool accepted=CAbilityGroundMorph(unit,A_EXECUTE,call);
    if(accepted && active)submerge_category(unit,false);
    return accepted;
}

static intptr_t submerge_ability(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    uint32_t code = call && call->item ? call->item->code : 0;
    bool active;

    if (!code) code = submerge_find_code(ent);
    active = S_UnitIsSubmerged(ent);
    switch (msg) {
    case A_UNIT_EVENT_MASK:
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_COMMAND,A_VALIDATE,A_DISABLE,A_UNIT_INIT,A_MOVE_LEAVE,A_TARGET_MODE_EXIT);
    case A_ENABLE:case A_UNIT_INIT: {
        uint32_t normal,submerged;
        if(ent && submerge_types(code,&normal,&submerged) && ent->class_id==submerged &&
           (!ent->morph || ent->morph->code!=code)) {
            CAbilityGroundMorph(ent,A_UNIT_INIT,call);
            /* Own the active-form suspension before publishing hidden state.
             * Runtime attachment inherits this aggregate after A_ENABLE. */
            S_SetMorphAbilityDisables(ent,code,true);
            submerge_publish(ent,code,true);
        }
        return true;
    }
    case A_COMMAND: {
        gameClient_t *client;
        uint32_t subgroup_class;
        bool executed = false, invalid_water = false;
        if (!ent || !call || !call->client || !(client = call->client->client) || !code) return false;
        subgroup_class = ent->class_id;
        FOR_CONTROLLABLE_SELECTED_UNITS(client, unit) {
            uint32_t unit_code;
            if (unit->class_id != subgroup_class || !(unit_code = submerge_find_code(unit))) continue;
            if (!S_UnitIsSubmerged(unit) && !submerge_deep_water(unit)) {
                invalid_water = true;
                continue;
            }
            if (S_CastNoTargetSpell(unit, unit_code)) executed = true;
        }
        if (!executed && invalid_water) G_ShowCommandErrorKey(call->client, "Cantsubmergethere", NULL);
        if (executed) Get_Commands_f(call->client);
        return executed;
    }
    case A_ORDER:
        if (!call || !call->order || !code) return false;
        if (!strcmp(call->order, submerge_orders[0])) {
            if (active) return false;
        } else if (!strcmp(call->order, submerge_orders[1])) {
            if (!active) return false;
        } else return false;
        return S_CastNoTargetSpell(ent, code);
    case A_TOGGLE_ON:
        return active;
    case A_VALIDATE:
        return code && submerge_validate(ent, code);
    case A_EXECUTE:
        return code && submerge_execute(ent, call);
    case A_MORPH_COMPLETE: {
        bool hidden=!call->enabled && G_ActorHasAbilityCode(ent,code);
        /* Detach has already removed membership when it forces completion;
         * acquire its forward source so the immediate inverse also resumes passives. */
        if(!call->enabled)S_SetMorphAbilityDisables(ent,code,true);
        submerge_publish(ent,code,hidden);
        /* Reverse admission removed the subtype source, but the passive
         * remains suspended until this authored reverse completion. */
        if(call->enabled)S_SetMorphAbilityDisables(ent,code,false);
        return CAbilityGroundMorph(ent,msg,call);
    }
    case A_MORPH_EXPIRE:
        return code && submerge_execute(ent,call);
    case A_DISABLE: {
        uint32_t normal,submerged;
        /* A pending forward completion can acquire the form owner. Finish
         * it before testing the alternate-type detach inverse. */
        S_CompleteMorphDetachTransition(ent,code);
        bool release=ent && submerge_types(code,&normal,&submerged) && ent->class_id==submerged;
        if(ent && S_UnitIsSubmerged(ent))submerge_publish(ent,code,false);
        intptr_t result=CAbilityGroundMorph(ent,msg,call);
        /* Restore the base while its passive owner is still suspended. */
        if(release)S_SetMorphAbilityDisables(ent,code,false);
        return result;
    }
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}

BZ_ABILITY_PROC(CAbilitySubmergeMyrmidon) { return submerge_ability(ent, msg, call); }
BZ_ABILITY_PROC(CAbilitySubmergeRoyalGuard) { return submerge_ability(ent, msg, call); }
BZ_ABILITY_PROC(CAbilitySubmergeSnapDragon) { return submerge_ability(ent, msg, call); }

static bool ensnare_is_flyer(edict_t const *unit) {
    cstring_t movetp;
    if (!unit) return false;
    if (unit->aiflags & AI_FLYING) return true;
    movetp = unit->data.UnitData ? unit->data.UnitData->moveTypeName : NULL;
    return movetp && !strcmp(movetp, "fly");
}

static heroabilitystatus_t *ensnare_status(edict_t *unit) {
    if (!unit) return NULL;
    FOR_LOOP(i, G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *slot = unit->abilstatus + i;
        if (!slot->level) continue;
        if (S_StatusIsEnsnare(G_UnitStatusClassCode(slot)))
            return slot;
    }
    return NULL;
}

static float ensnare_authored_height(edict_t const *unit) {
    return unit && unit->data.UnitData ? unit->data.UnitData->moveHeight : 0.0f;
}

/* Apply DataA/B land: zero DataA snaps; otherwise start at DataB (else current/authored). */
static void ensnare_begin_land(edict_t *unit, uint32_t spell_code, uint32_t level) {
    float adjust, height;
    if (!unit || !ensnare_is_flyer(unit)) {
        if (unit) G_FreeEnsnare(unit);
        return;
    }
    adjust = S_SpellData(spell_code, level, 1);
    height = S_SpellData(spell_code, level, 2);
    if (height <= 0.0f) height = unit->unitinfo.FlyHeight > 0.0f ? unit->unitinfo.FlyHeight : ensnare_authored_height(unit);
    if (!unit->ensnare) unit->ensnare = G_AllocEnsnare();
    assert(unit->ensnare);
    unit->ensnare->adjust = adjust;
    unit->ensnare->height = height;
    unit->ensnare->start = G_Time();
    if (adjust <= 0.0f) {
        unit->unitinfo.FlyHeight = 0.0f;
        G_FreeEnsnare(unit);
    } else {
        unit->ensnare->phase = ENSNARE_HEIGHT_LAND;
        unit->unitinfo.FlyHeight = height;
    }
    M_CheckGround(unit);
    gi.LinkEntity(unit);
}

static void ensnare_set_height(edict_t *unit, float height) {
    unit->unitinfo.FlyHeight = MAX(0.0f, height);
    M_CheckGround(unit);
    gi.LinkEntity(unit);
}

/* Advance land/rise owned by CAbilityEnsnare (AB_UPDATE). */
static void ensnare_update(edict_t *unit) {
    float frac, target;
    if (!unit || !unit->ensnare || unit->ensnare->phase == ENSNARE_HEIGHT_NONE || unit->ensnare->adjust <= 0.0f) return;
    frac = ((float)G_Time() - (float)unit->ensnare->start) / (unit->ensnare->adjust * 1000.0f);
    if (unit->ensnare->phase == ENSNARE_HEIGHT_LAND) {
        if (frac >= 1.0f) {
            ensnare_set_height(unit, 0.0f);
            unit->ensnare->phase = ENSNARE_HEIGHT_NONE;
        } else {
            ensnare_set_height(unit, unit->ensnare->height * MAX(0.0f, 1.0f - frac));
        }
        return;
    }
    target = unit->ensnare->height > 0.0f ? unit->ensnare->height : ensnare_authored_height(unit);
    if (frac >= 1.0f) {
        ensnare_set_height(unit, target);
        G_FreeEnsnare(unit);
    } else {
        ensnare_set_height(unit, target * MAX(0.0f, frac));
    }
}

bool S_StatusIsEnsnare(uint32_t code) {
    return code == MAKEFOURCC('B', 'e', 'n', 's') || code == MAKEFOURCC('B', 'e', 'n', 'a') ||
        code == MAKEFOURCC('B', 'e', 'n', 'g') || code == MAKEFOURCC('B','w','e','a') ||
        code == MAKEFOURCC('B','w','e','b');
}

bool S_UnitIsEnsnared(edict_t const *unit) {
    /* Ensnare/Web remain distinct from Entangling Roots (which also disarms).
     * They are bound status families, so avoid making ROOTED mean Ensnared. */
    if (unit) FOR_LOOP(i, G_UnitStatusSlotCount(unit))
        if (S_StatusIsEnsnare(G_UnitStatusClassCode(unit->abilstatus+i)) && S_UnitHasStatus(unit, G_UnitStatusClassCode(unit->abilstatus+i))) return true;
    return false;
}

/* DataC Melee Attack Range while the bind is active; 0 when not ensnared. */
float S_EnsnareMeleeRange(edict_t const *unit) {
    heroabilitystatus_t const *slot = ensnare_status((edict_t *)unit);
    if (!slot || !slot->data) return 0.0f;
    return S_SpellData(slot->data, slot->level, 3);
}

static bool ensnare_authored_flyer(edict_t const *unit) {
    cstring_t movetp = unit && unit->data.UnitData ? unit->data.UnitData->moveTypeName : NULL;
    return movetp && !strcmp(movetp, "fly");
}

/* Restore authored flight after the last bind ends. Dispel/expiry mid-land
 * converts the stored DataA land into a rise; a snapped land restores height
 * immediately. Unrelated flight state is never touched. */
static void ensnare_restore_flight(edict_t *unit) {
    if (!unit || !ensnare_authored_flyer(unit) || (unit->aiflags & AI_FLYING)) return;
    unit->aiflags |= AI_FLYING;
    unit->targtype = TARG_AIR;
    if (unit->ensnare && unit->ensnare->phase == ENSNARE_HEIGHT_LAND && unit->ensnare->adjust > 0.0f) {
        unit->ensnare->height = unit->data.UnitData->moveHeight;
        unit->ensnare->start = G_Time();
        unit->ensnare->phase = ENSNARE_HEIGHT_RISE;
        unit->unitinfo.FlyHeight = 0.0f;
    } else if ((!unit->ensnare || unit->ensnare->phase != ENSNARE_HEIGHT_RISE) && unit->unitinfo.FlyHeight <= 0.0f) {
        unit->unitinfo.FlyHeight = unit->data.UnitData->moveHeight;
    }
    M_CheckGround(unit);
}

/* A_STATUS_REFRESH: reconcile derived flight state with binds that remain. */
static void ensnare_refresh(edict_t *unit) {
    if (!unit) return;
    if (S_UnitIsEnsnared(unit)) {
        if (unit->aiflags & AI_FLYING) {
            unit->aiflags &= ~AI_FLYING;
            unit->targtype = TARG_GROUND;
            M_CheckGround(unit);
        }
    } else ensnare_restore_flight(unit);
}

/* A_STATUS_REMOVE: inverse while the expiring slot is still valid. Buff
 * expiry starts a gradual rise when DataA was non-zero; flight restores only
 * when no other Ensnare bind remains on the victim. */
static void ensnare_remove(edict_t *unit, heroabilitystatus_t const *expiring) {
    float adjust, target;
    if (!unit || !expiring) return;
    /* Web and Ensnare share one height transition: removing either cannot release the other. */
    FOR_LOOP(i, G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t const *slot = unit->abilstatus + i;
        if (slot->level && slot != expiring && S_StatusIsEnsnare(G_UnitStatusClassCode(slot))) return;
    }
    if (ensnare_is_flyer(unit)) {
        adjust = expiring->data ? S_SpellData(expiring->data, expiring->level, 1) : (unit->ensnare ? unit->ensnare->adjust : 0.0f);
        target = ensnare_authored_height(unit);
        if (adjust > 0.0f) {
            if (!unit->ensnare) unit->ensnare = G_AllocEnsnare();
            assert(unit->ensnare);
            unit->ensnare->adjust = adjust;
            unit->ensnare->height = target > 0.0f ? target : unit->ensnare->height;
            unit->ensnare->start = G_Time();
            unit->ensnare->phase = ENSNARE_HEIGHT_RISE;
            unit->unitinfo.FlyHeight = 0.0f;
        } else G_FreeEnsnare(unit);
    }
    ensnare_restore_flight(unit);
}

static void ensnare_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t list, buff;
    heroabilitystatus_t *slot;
    (void)caster;
    if (!st.entity) return;
    list = G_AbilityLevel(spell->code, level)->buffID;
    buff = S_SpellBuffToken(list, ensnare_is_flyer(st.entity) ? 0 : 1);
    if (!buff || strlen(buff) < 4) buff = S_SpellBuffToken(list, 0);
    /* ROC omits BuffID. Use each family's authored TFT token, preserving Web's air bind. */
    if (!buff || strlen(buff) < 4) buff = spell->ability->proc == CAbilityWeb ? "Bwea" : "Bens";
    slot = S_SpellApplyTimedStatus(st.entity, buff, level,
                                     S_SpellResistantDuration(spell->code, level, st.entity));
    if (!slot) return; /* A failed allocation must not start a ground-bind transition. */
    slot->data = spell->code;
    /* Ensnare/Web are physical binds, not removable by magical dispels.
     * Limit classification to stock identities; custom BuffIDs retain policy. */
    if (S_StatusIsEnsnare(slot->code))
        slot->buff_flags = (slot->buff_flags & ~(WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_TRANSFERABLE)) |
                           WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_PHYSICAL;
    ensnare_begin_land(st.entity, spell->code, level);
    ensnare_refresh(st.entity);
    S_SetMoveGoal(st.entity, &st.entity->goalentity, NULL);
}

/* Name=Ensnare — bind; air takes Bena and lands via DataA/B (AB_UPDATE advances height). */
BZ_ABILITY_PROC(CAbilityEnsnare) {
    if (msg == A_UNIT_TYPE_UPDATE && ent) return 0;
    if (msg == A_UNIT_TYPE_UPDATE) return UNIT_UPDATE_POINTER(ensnare);
    if (msg == A_UPDATE) { ensnare_update(ent); return true; }
    if (msg == A_STATUS_REFRESH) { ensnare_refresh(ent); return true; }
    if (msg == A_STATUS_REMOVE) {
        if (call) ensnare_remove(ent, call->status.slot);
        return true;
    }
    if (msg == A_EXECUTE) {
        spellTarget_t target = call && call->target ? *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
        ensnare_execute(ent, target, call ? call->item : NULL);
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}

/* Web shares the bind/landing mechanism, but only accepts flying enemies and supports autocast. */
BZ_ABILITY_PROC(CAbilityWeb) {
    if (msg == A_AUTOCAST_ON || msg == A_AUTOCAST_SET) return CAbilityModalSpell(ent, msg, call);
    if (msg == A_VALIDATE) {
        edict_t *target = call && call->target ? call->target->entity : NULL;
        return target && target != ent && target->targtype == TARG_AIR &&
            S_SpellIsAliveTarget(target) && S_SpellIsEnemy(ent, target);
    }
    if (msg == A_AUTOCAST_ACQUIRE && call && call->item) {
        FILTER_EDICTS(target, target->targtype == TARG_AIR && S_SpellIsEnemy(ent, target))
            if (S_CastUnitTargetSpell(ent, call->item->code, target)) return true;
        return false;
    }
    return CAbilityEnsnare(ent, msg, call);
}

BZ_SIMPLE_SPELL_PROC(AbilityFrostArmorCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityParasiteCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityCycloneCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySummoningRitual) { S_SummonAbilityUnits(caster, spell->code, &st); }
BZ_SIMPLE_SPELL_PROC(AbilitySummonQuilbeastCampaign) { S_SummonAbilityUnits(caster, spell->code, &st); }
BZ_SIMPLE_SPELL_PROC(AbilitySummonMisha) {
    uint32_t retired = 0;
    /* Rexxar's campaign Misha is the one-unit counterpart to Feral Spirit:
     * recasting replaces only the living Misha summoned by this caster. */
    if (!caster) return;
    FILTER_EDICTS(unit, unit->inuse && unit->owner == caster &&
                  unit->summon_ability == spell->code && !M_IsDead(unit)) {
        if (WC3_MISHA_DEBUG_ENABLED() && spell->code == MAKEFOURCC('A','r','s','g'))
            fprintf(stderr, "WC3_MISHA retire-existing caster=%u summon=%u id=%.4s health=%.1f/%.1f\n",
                    (unsigned)caster->s.number, (unsigned)unit->s.number,
                    (cstring_t)&unit->class_id, unit->health.value, unit->health.max_value);
        if (unit->die) unit->die(unit, caster);
        else unit_die(unit, caster);
        retired++;
    }
    if (WC3_MISHA_DEBUG_ENABLED() && spell->code == MAKEFOURCC('A','r','s','g'))
        fprintf(stderr, "WC3_MISHA execute caster=%u ability=Arsg mana=%.1f retired=%u\n",
                (unsigned)caster->s.number, caster->mana.value, retired);
    S_SummonAbilityUnits(caster, spell->code, &st);
}
BZ_SIMPLE_SPELL_PROC(AbilityStampedeCampaign) { S_SummonAbilityUnits(caster, spell->code, &st); }
BZ_SIMPLE_SPELL_PROC(AbilityBattleRoar) {
    uint32_t level = S_SpellLevel(caster, spell->code); cstring_t buff = S_SpellBuffId(spell->code, level);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) && Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        if (buff) S_SpellApplyTimedStatus(target, buff, level, S_SpellDuration(spell->code, level, false));
}
BZ_SIMPLE_SPELL_PROC(AbilityStormBoltCampaign) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    if (!st.entity || !S_SpellIsAliveTarget(st.entity)) return;
    S_SpellDamage(st.entity, caster, (uint32_t)MAX(1.0f, S_SpellData(spell->code, level, 1)));
    if (!M_IsDead(st.entity)) S_SpellApplyStun(st.entity, S_SpellHeroDuration(spell->code, level, st.entity));
}
BZ_SIMPLE_SPELL_PROC(AbilityBreathOfFireCampaign) { campaign_area_damage_execute(caster, st, spell); }
BZ_ABILITY_PROC(CAbilityDrunkenHazeCampaign) { return CAbilityDrunkenHaze(ent, msg, call); }
BZ_SIMPLE_SPELL_PROC(AbilityStormEarthFire) { S_SummonAbilityUnits(caster, spell->code, &st); }
BZ_SIMPLE_SPELL_PROC(AbilityHealingWaveCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityHexCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySerpentWard) { S_SummonAbilityUnits(caster, spell->code, &st); }
BZ_SIMPLE_SPELL_PROC(AbilityShockwaveCairne) { campaign_area_damage_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityEnduranceAuraCampaign) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityReincarnationCairne) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityVoodooSpirits) { S_SummonAbilityUnits(caster, spell->code, &st); }
