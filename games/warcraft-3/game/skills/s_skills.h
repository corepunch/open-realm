#ifndef s_skills_h
#define s_skills_h

#include "../g_local.h"

#define AURA_UPDATE_MS 2000 // milliseconds; retail aura refresh interval; used to throttle recipient recalculation

#define BZ_SIMPLE_SPELL_PROC(NAME) \
    static void NAME##_Execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell); \
    BZ_ABILITY_PROC(C##NAME) { \
        if (msg == A_EXECUTE) { \
            spellTarget_t target = call && call->target ? *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE); \
            NAME##_Execute(ent, target, call ? call->item : NULL); \
            return true; \
        } \
        return CAbilitySimpleSpell(ent, msg, call); \
    } \
    void NAME##_Execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell)
#define BZ_VALIDATED_SPELL_PROC(NAME, VALIDATE, EXECUTE) \
    BZ_ABILITY_PROC(C##NAME) { \
        spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ? \
            *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE); \
        switch (msg) { \
        case A_VALIDATE: return VALIDATE(ent, target, call ? call->item : NULL); \
        case A_EXECUTE: EXECUTE(ent, target, call ? call->item : NULL); return true; \
        default: return CAbilitySimpleSpell(ent, msg, call); \
        } \
    }
#define BZ_COMMAND_PROC(NAME) \
    static void NAME##_Command(LPEDICT clent); \
    BZ_ABILITY_PROC(C##NAME) { \
        if (msg != A_COMMAND) return false; \
        NAME##_Command(call && call->client ? call->client : ent); \
        return true; \
    } \
    void NAME##_Command(LPEDICT clent)
#define BZ_ITEM_PROC(NAME) \
    static BOOL NAME##_ItemUse(LPEDICT clent); \
    BZ_ABILITY_PROC(C##NAME) { \
        (void)call; \
        return msg == A_ITEM_USE && NAME##_ItemUse(ent); \
    } \
    BOOL NAME##_ItemUse(LPEDICT clent)

/* Concrete AbilityData implementations. */

extern LPCSTR const raven_orders[];
BZ_ABILITY_PROC(CAbilityHarvest);
BZ_ABILITY_PROC(CAbilityMove);
BZ_ABILITY_PROC(CAbilityRavenForm);
BZ_ABILITY_PROC(CAbilityAttack);
BZ_ABILITY_PROC(CAbilityBuild);
BZ_ABILITY_PROC(CAbilityTrain);
BZ_ABILITY_PROC(CAbilityGoldMine);
BZ_ABILITY_PROC(CAbilityCancel);
BZ_ABILITY_PROC(CAbilityRepair);
BZ_ABILITY_PROC(CAbilityStop);
BZ_ABILITY_PROC(CAbilityHoldPosition);
BZ_ABILITY_PROC(CAbilityPatrol);
BZ_ABILITY_PROC(CAbilityRally);
BZ_ABILITY_PROC(CAbilityMilitiaConvert);
BZ_ABILITY_PROC(CAbilityMilitia);
BZ_ABILITY_PROC(CAbilitySelectSkill);
BZ_ABILITY_PROC(CAbilityAuraDevotion);
BZ_ABILITY_PROC(CAbilityHolyBolt);
BZ_ABILITY_PROC(CAbilitySimpleSpell);
BZ_ABILITY_PROC(S_AbilityMessage);
BZ_ABILITY_PROC(CAbilityNoop);
BZ_ABILITY_PROC(CAbilityPassive);
BZ_ABILITY_PROC(CAbilityThunderBolt);
BZ_ABILITY_PROC(CAbilityFireBolt);
BZ_ABILITY_PROC(CAbilityWaterElemental);
BZ_ABILITY_PROC(CAbilitySpiritWolf);
BZ_ABILITY_PROC(CAbilityForceOfNature);
BZ_ABILITY_PROC(CAbilitySummonGrizzly);
BZ_ABILITY_PROC(CAbilitySummonQuillbeast);
BZ_ABILITY_PROC(CAbilitySummonWarEagle);
BZ_ABILITY_PROC(CAbilityMirrorImage);
BZ_ABILITY_PROC(CAbilityBlizzard);
BZ_ABILITY_PROC(CAbilityStarfall);
BZ_ABILITY_PROC(CAbilityCarrionSwarm);
BZ_ABILITY_PROC(CAbilityShockwave);
BZ_ABILITY_PROC(CAbilityRainOfFire);
BZ_ABILITY_PROC(CAbilityDeathAndDecay);
BZ_ABILITY_PROC(CAbilityThunderClap);
BZ_ABILITY_PROC(CAbilityFrostNova);
BZ_ABILITY_PROC(CAbilityTranquility);
BZ_ABILITY_PROC(CAbilityChannel);
BZ_ABILITY_PROC(CAbilityImmolation);
BZ_ABILITY_PROC(CAbilityColdArrows);
BZ_ABILITY_PROC(CAbilityCharm);
BZ_ABILITY_PROC(CAbilityEatTree);
BZ_ABILITY_PROC(CAbilityManaBattery);
BZ_ABILITY_PROC(CAbilityBlightedGoldMine);
BZ_ABILITY_PROC(CAbilityBlightGrowth);
BZ_ABILITY_PROC(CAbilityAcolyteHarvest);
BZ_ABILITY_PROC(CAbilityReturn);
BZ_ABILITY_PROC(CAbilityWispHarvest);
BZ_ABILITY_PROC(CAbilityHarvestLumber);
BZ_ABILITY_PROC(CAbilityRepairGeneric);
BZ_ABILITY_PROC(CAbilityRoot);
BZ_ABILITY_PROC(CAbilityBlink);
BZ_ABILITY_PROC(CAbilityFanOfKnives);
BZ_ABILITY_PROC(CAbilityShadowStrike);
BZ_ABILITY_PROC(CAbilityEntangle);
BZ_ABILITY_PROC(CAbilityCargoHold);
BZ_ABILITY_PROC(CAbilityBattlestations);
BZ_ABILITY_PROC(CAbilityStandDown);
BZ_ABILITY_PROC(CAbilityCargoLoad);
BZ_ABILITY_PROC(CAbilityCargoDrop);
BZ_ABILITY_PROC(CAbilityCargoDropInstant);
BZ_ABILITY_PROC(CAbilityInventory);
BZ_ABILITY_PROC(CAbilityPurchaseItem);
BZ_ABILITY_PROC(CAbilityCoupleInstant);
BZ_ABILITY_PROC(CAbilityItemHeal);
BZ_ABILITY_PROC(CAbilityItemManaRestore);
BZ_ABILITY_PROC(CAbilityAttackBonus);
BZ_ABILITY_PROC(CAbilityAttributeBonus);
BZ_ABILITY_PROC(CAbilityStrengthMod);
BZ_ABILITY_PROC(CAbilityDefenseBonus);
BZ_ABILITY_PROC(CAbilityMaxLifeBonus);
BZ_ABILITY_PROC(CAbilityMaxManaBonus);
BZ_ABILITY_PROC(CAbilityFigurineSkeleton);
BZ_ABILITY_PROC(CAbilityMaxLifeMod);
BZ_ABILITY_PROC(CAbilityExperienceMod);
BZ_ABILITY_PROC(CAbilityLevelMod);
BZ_ABILITY_PROC(CAbilityItemDefenseAoe);
BZ_ABILITY_PROC(CAbilityItemChangeTOD);
BZ_ABILITY_PROC(CAbilityFlameStrikeNeutral);
BZ_ABILITY_PROC(CAbilityDrainNeutral);
BZ_ABILITY_PROC(CAbilityFlameStrike);
BZ_ABILITY_PROC(CAbilityDrain);
BZ_ABILITY_PROC(CAbilityManaBurn);
BZ_ABILITY_PROC(CAbilityStomp);
BZ_ABILITY_PROC(CAbilityWindWalk);
BZ_ABILITY_PROC(CAbilityEntanglingRoots);
BZ_ABILITY_PROC(CAbilityDarkRitual);
BZ_ABILITY_PROC(CAbilityFrostArmor);
BZ_ABILITY_PROC(CAbilityDivineShield);
BZ_ABILITY_PROC(CAbilityCriticalStrike);
BZ_ABILITY_PROC(CAbilityEvasion);
BZ_ABILITY_PROC(CAbilityMassTeleport);
BZ_ABILITY_PROC(CAbilityStampede);
BZ_ABILITY_PROC(CAbilityWhirlwind);
BZ_ABILITY_PROC(CAbilityTornado);
BZ_ABILITY_PROC(CAbilityBanish);
BZ_ABILITY_PROC(CAbilitySummonPhoenix);
BZ_ABILITY_PROC(CAbilityCarrionScarabs);
BZ_ABILITY_PROC(CAbilityImpale);
BZ_ABILITY_PROC(CAbilityLocustSwarm);
BZ_ABILITY_PROC(CAbilityBlackArrow);
BZ_ABILITY_PROC(CAbilitySilence);
BZ_ABILITY_PROC(CAbilityAnimateDead);
BZ_ABILITY_PROC(CAbilityDeathCoil);
BZ_ABILITY_PROC(CAbilityDeathPact);
BZ_ABILITY_PROC(CAbilityMetamorphosis);
BZ_ABILITY_PROC(CAbilitySleep);
BZ_ABILITY_PROC(CAbilitySleepAlways);
BZ_ABILITY_PROC(CAbilityCreepSleep);
BZ_ABILITY_PROC(CAbilityDreadLordInferno);
BZ_ABILITY_PROC(CAbilityChainLightning);
BZ_ABILITY_PROC(CAbilityForkedLightning);
BZ_ABILITY_PROC(CAbilityEarthquake);
BZ_ABILITY_PROC(CAbilityFarSight);
BZ_ABILITY_PROC(CAbilityResurrection);
BZ_ABILITY_PROC(CAbilityBreathOfFire);
BZ_ABILITY_PROC(CAbilityHowlOfTerror);
BZ_ABILITY_PROC(CAbilityFlamingArrows);
BZ_ABILITY_PROC(CAbilityHealingWave);
BZ_ABILITY_PROC(CAbilityHex);
BZ_ABILITY_PROC(CAbilitySpiritOfVengeance);
BZ_ABILITY_PROC(CAbilityVoodoo);
BZ_ABILITY_PROC(CAbilityAcidBomb);
BZ_ABILITY_PROC(CAbilityPoisonArrows);
BZ_ABILITY_PROC(CAbilityOnFireHuman);
BZ_ABILITY_PROC(CAbilityAttributeModSkill);
BZ_ABILITY_PROC(CAbilitySpawnTentacle);
BZ_ABILITY_PROC(CAbilityAvatarCampaign);
BZ_ABILITY_PROC(CAbilityDarkConversion);
BZ_ABILITY_PROC(CAbilityShockwaveCampaign);
BZ_ABILITY_PROC(CAbilityWarStompCampaign);
BZ_ABILITY_PROC(CAbilityFeralSpiritCampaign);
BZ_ABILITY_PROC(CAbilitySpiritBeast);
BZ_ABILITY_PROC(CAbilityReincarnationCampaign);
BZ_ABILITY_PROC(CAbilityFeedbackCampaign);
BZ_ABILITY_PROC(CAbilityAbolishMagic);
BZ_ABILITY_PROC(CAbilitySubmergeMyrmidon);
BZ_ABILITY_PROC(CAbilitySubmergeRoyalGuard);
BZ_ABILITY_PROC(CAbilitySubmergeSnapDragon);
BZ_ABILITY_PROC(CAbilityEnsnare);
BZ_ABILITY_PROC(CAbilityFrostArmorCampaign);
BZ_ABILITY_PROC(CAbilityParasiteCampaign);
BZ_ABILITY_PROC(CAbilityCycloneCampaign);
BZ_ABILITY_PROC(CAbilitySummoningRitual);
BZ_ABILITY_PROC(CAbilitySummonQuilbeastCampaign);
BZ_ABILITY_PROC(CAbilitySummonMisha);
BZ_ABILITY_PROC(CAbilityStampedeCampaign);
BZ_ABILITY_PROC(CAbilityBattleRoar);
BZ_ABILITY_PROC(CAbilityStormBoltCampaign);
BZ_ABILITY_PROC(CAbilityBreathOfFireCampaign);
BZ_ABILITY_PROC(CAbilityDrunkenHazeCampaign);
BZ_ABILITY_PROC(CAbilityStormEarthFire);
BZ_ABILITY_PROC(CAbilityHealingWaveCampaign);
BZ_ABILITY_PROC(CAbilityHexCampaign);
BZ_ABILITY_PROC(CAbilitySerpentWard);
BZ_ABILITY_PROC(CAbilityShockwaveCairne);
BZ_ABILITY_PROC(CAbilityEnduranceAuraCampaign);
BZ_ABILITY_PROC(CAbilityReincarnationCairne);
BZ_ABILITY_PROC(CAbilityVoodooSpirits);
BZ_ABILITY_PROC(CAbilityMagicLeash);
BZ_ABILITY_PROC(CAbilityControlMagic);
BZ_ABILITY_PROC(CAbilityMagicDefense);
BZ_ABILITY_PROC(CAbilitySpellSteal);
BZ_ABILITY_PROC(CAbilityCloudOfFog);
BZ_ABILITY_PROC(CAbilityDefend);
BZ_ABILITY_PROC(CAbilityFlare);
BZ_ABILITY_PROC(CAbilityInnerFire);
BZ_ABILITY_PROC(CAbilityDispelMagic);
BZ_ABILITY_PROC(CAbilityHeal);
BZ_ABILITY_PROC(CAbilitySlow);
BZ_ABILITY_PROC(CAbilityInvisibility);
BZ_ABILITY_PROC(CAbilityPolymorph);
BZ_ABILITY_PROC(CAbilityAvatar);
BZ_ABILITY_PROC(CAbilityDoom);
BZ_ABILITY_PROC(CAbilityDrunkenHaze);
BZ_ABILITY_PROC(CAbilityBloodlust);
BZ_ABILITY_PROC(CAbilityFaerieFire);
BZ_ABILITY_PROC(CAbilityRejuvination);
BZ_ABILITY_PROC(CAbilityRoar);

void human_ability_think(LPEDICT thinker);
BOOL S_HoldPosition(LPEDICT unit);
BOOL S_MilitiaEnsureHallAbility(LPEDICT hall);
FLOAT S_MilitiaPairSearchRadius(DWORD ability);
FLOAT S_RegenerationHealthAura(LPEDICT unit);
FLOAT S_RegenerationManaAura(LPEDICT unit);
void S_UpdateRegenerationAuraEffects(LPEDICT unit);
BOOL S_RegenerationAuraUpdateDue(LPEDICT unit);
FLOAT S_BrillianceManaRegen(LPEDICT unit);
FLOAT S_UnholyHealthRegen(LPEDICT unit);
FLOAT S_UnholyMoveBonus(LPEDICT unit);
FLOAT S_VampiricLifeSteal(LPEDICT unit);
FLOAT S_TrueshotAttackBonus(LPEDICT unit);
int S_SearingArrowDamage(LPEDICT attacker, int damage);
FLOAT S_ThornsDamageReturn(LPCEDICT target, LPCEDICT attacker, FLOAT damage);
BOOL S_EvasionRoll(LPEDICT target);
int S_CriticalStrikeDamage(LPEDICT attacker, int damage);
FLOAT S_SpikedArmorBonus(LPCEDICT unit);
FLOAT S_SpikedDamageReturn(LPCEDICT unit, FLOAT damage);
int S_ManaShieldDamage(LPEDICT target, int damage);
void S_SummonUnits(LPEDICT caster, DWORD unit_id, DWORD count, FLOAT duration);
LPEDICT S_SummonAt(LPEDICT caster, DWORD unit_id, LPCVECTOR2 loc, FLOAT duration);
BOOL S_UnitHasStatus(LPCEDICT unit, DWORD code);
BOOL S_UnitPolymorphed(LPCEDICT unit);
void S_PolymorphRemove(LPEDICT unit);
int S_BlackArrowDamage(LPEDICT attacker, int damage);
void S_BlackArrowDeath(LPEDICT attacker, LPEDICT target);
void S_ResolveAttackHit(LPEDICT attacker, LPEDICT target, int damage);
void S_ReincarnationOnDeath(LPEDICT unit);
BOOL S_HumanCanAttack(LPCEDICT unit);
FLOAT S_HumanMoveFactor(LPCEDICT unit);
FLOAT S_HumanArmorBonus(LPCEDICT unit);
int S_HumanAttackDamage(LPEDICT attacker, LPEDICT target, int damage);
void S_HumanAttackSplash(LPEDICT attacker, LPEDICT target, int damage);
void S_HumanBreakInvisibility(LPEDICT unit);
void S_HumanStatusExpired(LPEDICT unit, DWORD code, DWORD level);
BOOL S_UnitSpellImmune(LPCEDICT unit);
BOOL S_SpellDamage(LPEDICT target, LPEDICT caster, int damage);
void S_AvatarExpire(LPEDICT unit);
FLOAT S_BloodlustAttackBonus(LPCEDICT unit);
FLOAT S_BloodlustMoveBonus(LPCEDICT unit);
FLOAT S_FaerieArmorDelta(LPCEDICT unit);
FLOAT S_RoarDamageBonus(LPCEDICT unit);
FLOAT S_RejuvHealRate(LPCEDICT unit);

FLOAT AB_Data(LPCSTR classname, DWORD level, DWORD index);
DWORD AB_DataId(LPCSTR classname, DWORD level, DWORD index);

typedef enum {
	RETURN_RESOURCE_GOLD = 1,
	RETURN_RESOURCE_LUMBER = 2,
} returnResource_t;

BOOL S_CanReturnResourceAt(LPEDICT unit, LPEDICT building, returnResource_t resource);
LPEDICT S_FindNearestResourceDropoff(LPEDICT unit, returnResource_t resource);
void S_SetCarriedResource(LPEDICT unit, returnResource_t resource, DWORD amount);

typedef enum {
	ABILITY_NUMBER_CAST,
	ABILITY_NUMBER_DURATION,
	ABILITY_NUMBER_HERO_DURATION,
	ABILITY_NUMBER_COOLDOWN,
	ABILITY_NUMBER_COST,
	ABILITY_NUMBER_AREA,
	ABILITY_NUMBER_RANGE
} abilityNumber_t;
DWORD S_SpellCurrentCode(LPEDICT clent, DWORD fallback);
ability_t const *S_SpellAbilityForCode(DWORD code);
DWORD S_SpellLevel(LPEDICT caster, DWORD code);
FLOAT S_SpellNumber(DWORD code, abilityNumber_t field, DWORD level);
LPCSTR S_SpellString(DWORD code, LPCSTR field, DWORD level);
FLOAT S_SpellData(DWORD code, DWORD level, DWORD index);
DWORD S_SpellDataId(DWORD code, DWORD level, DWORD index);
DWORD S_SpellUnitId(DWORD code, DWORD level);
FLOAT S_SpellRange(DWORD code, DWORD level);
FLOAT S_SpellDuration(DWORD code, DWORD level, BOOL hero);
BOOL S_SpellCooldownReady(LPEDICT caster, DWORD code);
FLOAT S_SpellCooldownRemaining(LPEDICT caster, DWORD code);
FLOAT S_SpellCooldownLength(LPEDICT caster, DWORD code);
BOOL S_SpellCooldownWindow(LPEDICT caster, DWORD code, abilityCooldownWindow_t *window);
FLOAT S_SpellCooldownFraction(LPEDICT caster, DWORD code, DWORD level);
void S_SpellStartCooldownDuration(LPEDICT caster, DWORD code, FLOAT seconds);
void S_SpellStartCooldown(LPEDICT caster, DWORD code, DWORD level);
void S_SpellEndCooldown(LPEDICT caster, DWORD code);
void S_SpellResetCooldowns(LPEDICT caster);
BOOL S_SpellSpendMana(LPEDICT caster, DWORD code, DWORD level);
BOOL S_SpellCanPay(LPEDICT caster, DWORD code, DWORD level);
BOOL S_CastNoTargetSpell(LPEDICT caster, DWORD code);
BOOL S_CastPointTargetSpell(LPEDICT caster, DWORD code, LPCVECTOR2 point);
BOOL S_CastUnitTargetSpell(LPEDICT caster, DWORD code, LPEDICT target);
BOOL S_IssueUnitTargetSpell(LPEDICT caster, DWORD code, LPEDICT target);
BOOL S_SpellTargetInRange(LPEDICT caster, LPEDICT target, FLOAT range);
BOOL S_SpellIsAliveTarget(LPEDICT target);
BOOL S_SpellIsEnemy(LPEDICT caster, LPEDICT target);
BOOL S_SpellIsFriend(LPEDICT caster, LPEDICT target);
BOOL S_SpellAllowsTarget(DWORD code, LPEDICT caster, LPEDICT target);
void S_SpellHeal(LPEDICT target, FLOAT amount);
void S_SpellCursorSplat(LPEDICT clent, FLOAT radius);
void S_SpellCodeString(DWORD code, LPSTR out);
BOOL S_SpellIsChanneling(LPEDICT caster);
void S_SpellCancelChannel(LPEDICT caster);
LPEDICT S_SpellChannelThinker(LPEDICT caster, DWORD code);
BOOL S_SpellChannelActive(LPEDICT thinker);
void S_SpellEndChannel(LPEDICT thinker);

/* Unified spell pipeline owns targeting and cast lifecycle; concrete procedures
 * receive validation and execution messages through the registry row. */
void spell_cmd(LPEDICT clent);
void spell_run_frame(LPEDICT ent);

#endif
