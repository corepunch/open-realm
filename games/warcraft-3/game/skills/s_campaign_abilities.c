#include "s_skills.h"

/* Campaign rawcodes keep their own spell descriptor so every lookup uses the campaign AbilityData row. */
static LPCSTR campaign_buff(abilityitem_t const *spell, DWORD level) {
    LPCSTR buff = G_AbilityLevel(spell->code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : NULL;
}

static void campaign_status_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR buff = campaign_buff(spell, level);
    if (st.entity && buff) unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)));
}

static void campaign_area_damage_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    DWORD damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1));
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) && Vector2_distance(&target->s.origin2, &st.point) <= area)
        S_SpellDamage(target, caster, damage);
}

static void campaign_summon_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code), count = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1));
    DWORD unit = S_SpellUnitId(spell->code, level); FLOAT duration = S_SpellDuration(spell->code, level, false);
    if (st.type == SPELL_TARGET_POINT) { FOR_LOOP(i, count) S_SummonAt(caster, unit, &st.point, duration); }
    else S_SummonUnits(caster, unit, count, duration);
}

static void campaign_toggle_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *status = caster->abilstatus + i;
        if (status->level && status->code == spell->code) { memset(status, 0, sizeof(*status)); return; }
    }
    unit_addstatus(caster, GetClassName(spell->code), S_SpellLevel(caster, spell->code));
}

BZ_SIMPLE_SPELL_PROC(AbilityAttributeModSkill) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySpawnTentacle) { campaign_summon_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityAvatarCampaign) {
    DWORD level = S_SpellLevel(caster, spell->code), form = S_SpellUnitId(spell->code, level);
    if (form) G_TransformUnitType(caster, form);
}
/* Dark Conversion consumes its victim only after publishing the spawned unit;
 * campaign JASS observes that summon and owns its final replacement/order. */
BZ_SIMPLE_SPELL_PROC(AbilityDarkConversion) {
    DWORD level = S_SpellLevel(caster, spell->code), unit = S_SpellUnitId(spell->code, level);
    LPEDICT summon;
    LPCSTR buff;

    if (!caster || !st.entity || !unit) return;
    summon = S_SummonAt(caster, unit, &st.entity->s.origin2, 0.0f);
    if (!summon) return;
    buff = campaign_buff(spell, level);
    if (buff) unit_addtimedstatus(summon, buff, level, S_SpellDuration(spell->code, level, false));
    G_FreeEdict(st.entity);
}
BZ_SIMPLE_SPELL_PROC(AbilityShockwaveCampaign) { campaign_area_damage_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityWarStompCampaign) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level), duration = S_SpellDuration(spell->code, level, false);
    DWORD damage = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1));
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) && target->targtype == TARG_GROUND && Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area) {
        S_SpellDamage(target, caster, damage);
        if (!M_IsDead(target) && duration > 0.0f) unit_addtimedstatus(target, "Bstu", 1, duration);
    }
}
BZ_SIMPLE_SPELL_PROC(AbilityFeralSpiritCampaign) { campaign_summon_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySpiritBeast) { campaign_summon_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityReincarnationCampaign) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityFeedbackCampaign) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityAbolishMagic) {
    DWORD level = S_SpellLevel(caster, spell->code), count = 0; FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FILTER_EDICTS(target, count < (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1)) && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) && Vector2_distance(&target->s.origin2, &st.point) <= area) {
        FOR_LOOP(i, MAX_UNIT_STATUSES) if (target->abilstatus[i].level && target->abilstatus[i].timestamp) memset(target->abilstatus + i, 0, sizeof(target->abilstatus[i]));
        count++;
    }
}
BZ_SIMPLE_SPELL_PROC(AbilitySubmergeMyrmidon) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySubmergeRoyalGuard) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySubmergeSnapDragon) { campaign_toggle_execute(caster, st, spell); }

/* BuffID is "Bena,Beng"; index 0 is air, index 1 is ground. Empty ROC BuffID falls back to Bens. */
static LPCSTR ensnare_buff_token(LPCSTR list, DWORD index) {
    DWORD i = 0;
    if (!list) return NULL;
    for (;;) {
        if (strlen(list) < 4) return NULL;
        if (i == index) return list;
        list = strchr(list, ',');
        if (!list) return NULL;
        list++; i++;
    }
}

static BOOL ensnare_is_flyer(LPCEDICT unit) {
    LPCSTR movetp;
    if (!unit) return false;
    if (unit->aiflags & AI_FLYING) return true;
    movetp = unit->data.UnitData ? unit->data.UnitData->moveTypeName : NULL;
    return movetp && !strcmp(movetp, "fly");
}

BOOL S_UnitIsEnsnared(LPCEDICT unit) {
    return unit && (G_UnitStatusLevel(unit, MAKEFOURCC('B', 'e', 'n', 's')) ||
                    G_UnitStatusLevel(unit, MAKEFOURCC('B', 'e', 'n', 'a')) ||
                    G_UnitStatusLevel(unit, MAKEFOURCC('B', 'e', 'n', 'g')));
}

/* Name=Ensnare — bind target; air takes Bena and lands via unit_refreshstatusflags. */
BZ_SIMPLE_SPELL_PROC(AbilityEnsnare) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR list, buff;
    (void)caster;
    if (!st.entity) return;
    list = G_AbilityLevel(spell->code, level)->buffID;
    buff = ensnare_buff_token(list, ensnare_is_flyer(st.entity) ? 0 : 1);
    if (!buff || strlen(buff) < 4) buff = ensnare_buff_token(list, 0);
    if (!buff || strlen(buff) < 4) buff = "Bens";
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)));
    st.entity->goalentity = NULL;
}

BZ_SIMPLE_SPELL_PROC(AbilityFrostArmorCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityParasiteCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityCycloneCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySummoningRitual) { campaign_summon_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySummonQuilbeastCampaign) { campaign_summon_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySummonMisha) { campaign_summon_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityStampedeCampaign) { campaign_summon_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityBattleRoar) {
    DWORD level = S_SpellLevel(caster, spell->code); LPCSTR buff = campaign_buff(spell, level);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) && Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        if (buff) unit_addtimedstatus(target, buff, level, S_SpellDuration(spell->code, level, false));
}
BZ_SIMPLE_SPELL_PROC(AbilityStormBoltCampaign) {
    DWORD level = S_SpellLevel(caster, spell->code);
    if (!st.entity || !S_SpellIsAliveTarget(st.entity)) return;
    S_SpellDamage(st.entity, caster, (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1)));
    if (!M_IsDead(st.entity)) unit_addtimedstatus(st.entity, "Bstu", 1, S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)));
}
BZ_SIMPLE_SPELL_PROC(AbilityBreathOfFireCampaign) { campaign_area_damage_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityDrunkenHazeCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityStormEarthFire) { campaign_summon_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityHealingWaveCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityHexCampaign) { campaign_status_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilitySerpentWard) { campaign_summon_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityShockwaveCairne) { campaign_area_damage_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityEnduranceAuraCampaign) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityReincarnationCairne) { campaign_toggle_execute(caster, st, spell); }
BZ_SIMPLE_SPELL_PROC(AbilityVoodooSpirits) { campaign_summon_execute(caster, st, spell); }
