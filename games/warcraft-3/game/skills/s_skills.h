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
    static bool NAME##_ItemUse(LPEDICT clent); \
    BZ_ABILITY_PROC(C##NAME) { \
        (void)call; \
        return msg == A_ITEM_USE && NAME##_ItemUse(ent); \
    } \
    bool NAME##_ItemUse(LPEDICT clent)

/* Concrete AbilityData implementations. */

extern cstring_t const raven_orders[];
extern cstring_t const barkskin_orders[];
extern cstring_t const stone_form_orders[];
BZ_ABILITY_PROC(CAbilityHarvest);
BZ_ABILITY_PROC(CAbilityMove);
BZ_ABILITY_PROC(CAbilityRavenForm);
BZ_ABILITY_PROC(CAbilityAttack);
BZ_ABILITY_PROC(CAbilityAttackGround);
BZ_ABILITY_PROC(CAbilityBuild);
BZ_ABILITY_PROC(CAbilityTrain);
void TrainSetBuildMove(LPEDICT producer);
void G_RefreshTrainingQueue(LPEDICT producer);
void unit_add_build_queue(LPEDICT self, LPEDICT item);
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
BZ_ABILITY_PROC(CAbilityModalSpell);
BZ_ABILITY_PROC(S_AbilityMessage);
BZ_ABILITY_PROC(CAbilityNoop);
BZ_ABILITY_PROC(CAbilityPassive);
BZ_ABILITY_PROC(CAbilityPermanentInvisibility);
BZ_ABILITY_PROC(CAbilityThunderBolt);
BZ_ABILITY_PROC(CAbilityFireBolt);
BZ_ABILITY_PROC(CAbilityWaterElemental);
BZ_ABILITY_PROC(CAbilitySpiritWolf);
BZ_ABILITY_PROC(CAbilityForceOfNature);
BZ_ABILITY_PROC(CAbilitySummonGrizzly);
BZ_ABILITY_PROC(CAbilitySummonQuillbeast);
BZ_ABILITY_PROC(CAbilitySummonWarEagle);
BZ_ABILITY_PROC(CAbilityPocketFactory);
BZ_ABILITY_PROC(CAbilitySelfDestruct);
BZ_ABILITY_PROC(CAbilityDeathDamageAoe);
BZ_ABILITY_PROC(CAbilityMindRot);
BZ_ABILITY_PROC(CAbilityLiquidFire);
BZ_ABILITY_PROC(CAbilityCorrosiveBreath);
BZ_ABILITY_PROC(CAbilityLightningAttack);
BZ_ABILITY_PROC(CAbilitySlowAura);
BZ_ABILITY_PROC(CAbilityCommandAura);
BZ_ABILITY_PROC(CAbilityWarDrums);
BZ_ABILITY_PROC(CAbilityBash);
BZ_ABILITY_PROC(CAbilityFeedback);
BZ_ABILITY_PROC(CAbilityCleavingAttack);
BZ_ABILITY_PROC(CAbilityPulverize);
BZ_ABILITY_PROC(CAbilitySpiked);
BZ_ABILITY_PROC(CAbilityHardenedSkin);
BZ_ABILITY_PROC(CAbilityAuraRegenMana);
BZ_ABILITY_PROC(CAbilityDiseaseCloud);
BZ_ABILITY_PROC(CAbilityResistantSkin);
BZ_ABILITY_PROC(CAbilityCreepAura);
BZ_ABILITY_PROC(CAbilityReincarnation);
BZ_ABILITY_PROC(CAbilityOrbAnnihilation);
BZ_ABILITY_PROC(CAbilityTrueSight);
BZ_ABILITY_PROC(CAbilityAbsorb);
BZ_ABILITY_PROC(CAbilityChaos);
BZ_ABILITY_PROC(CAbilitySpiderAttack);
BZ_ABILITY_PROC(CAbilityWander);
BZ_ABILITY_PROC(CAbilityMagicImmunity);
BZ_ABILITY_PROC(CAbilityEngineeringUpgrade);
BZ_ABILITY_PROC(CAbilityDemolish);
BZ_ABILITY_PROC(CAbilityFactory);
BZ_ABILITY_PROC(CAbilityTornadoDamage);
BZ_ABILITY_PROC(CAbilityRevenge);
BZ_ABILITY_PROC(CAbilityGhost);
BZ_ABILITY_PROC(CAbilityGhostVisible);
BZ_ABILITY_PROC(CAbilityEthereal);
BZ_ABILITY_PROC(CAbilityScout);
BZ_ABILITY_PROC(CAbilityBallsOfFire);
BZ_ABILITY_PROC(CAbilitySalvage);
BZ_ABILITY_PROC(CAbilityTreeOfLife);
BZ_ABILITY_PROC(CAbilityWarp);
BZ_ABILITY_PROC(CAbilityGrabTree);
BZ_ABILITY_PROC(CAbilityDetector);
BZ_ABILITY_PROC(CAbilityMagicSentry);
BZ_ABILITY_PROC(CAbilityNeutralSpell);
BZ_ABILITY_PROC(CAbilityDrunkenBrawler);
BZ_ABILITY_PROC(CAbilitySellItem);
BZ_ABILITY_PROC(CAbilitySellUnit);
BZ_ABILITY_PROC(CAbilityUnstableConcoction);
void S_UnitDeathAbilities(LPEDICT ent);
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
BZ_ABILITY_PROC(CAbilityCoupleArcher);
BZ_ABILITY_PROC(CAbilityCoupleHippogryph);
BZ_ABILITY_PROC(CAbilityDecouple);
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
BZ_ABILITY_PROC(CAbilityStoneForm);
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
BZ_ABILITY_PROC(CAbilityAncestralSpirit);
BZ_ABILITY_PROC(CAbilityBreathOfFire);
BZ_ABILITY_PROC(CAbilityHowlOfTerror);
BZ_ABILITY_PROC(CAbilityFlamingArrows);
BZ_ABILITY_PROC(CAbilityHealingWave);
BZ_ABILITY_PROC(CAbilityHex);
BZ_ABILITY_PROC(CAbilitySpiritOfVengeance);
BZ_ABILITY_PROC(CAbilityVoodoo);
BZ_ABILITY_PROC(CAbilityAcidBomb);
BZ_ABILITY_PROC(CAbilityManaShield);
BZ_ABILITY_PROC(CAbilityManaFlare);
void S_ManaFlareOnCast(LPEDICT caster, uint32_t spell_code, uint32_t spell_level);
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
BZ_ABILITY_PROC(CAbilityCyclone);
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
BZ_ABILITY_PROC(CAbilityFingerOfDeath);
BZ_ABILITY_PROC(CAbilityMonsoon);
BZ_ABILITY_PROC(CAbilityWeb);
BZ_ABILITY_PROC(CAbilityDrunkenHaze);
BZ_ABILITY_PROC(CAbilityBloodlust);
BZ_ABILITY_PROC(CAbilityFaerieFire);
BZ_ABILITY_PROC(CAbilityRejuvination);
BZ_ABILITY_PROC(CAbilityRoar);
BZ_ABILITY_PROC(CAbilityFrenzy);
BZ_ABILITY_PROC(CAbilityUnholyFrenzy);
BZ_ABILITY_PROC(CAbilityCurse);
BZ_ABILITY_PROC(CAbilityCripple);
BZ_ABILITY_PROC(CAbilitySoulBurn);
BZ_ABILITY_PROC(CAbilityTaunt);
BZ_ABILITY_PROC(CAbilityPurge);
BZ_ABILITY_PROC(CAbilityLightningShield);
BZ_ABILITY_PROC(CAbilityHealingWard);
BZ_ABILITY_PROC(CAbilityStasisTrap);
BZ_ABILITY_PROC(CAbilityEvilEye);
BZ_ABILITY_PROC(CAbilityAuraRegenLife);
BZ_ABILITY_PROC(CAbilityMoonGlaive);
BZ_ABILITY_PROC(CAbilitySlowPoison);
BZ_ABILITY_PROC(CAbilityPoisonAttack);
BZ_ABILITY_PROC(CAbilityBarkskin);
BZ_ABILITY_PROC(CAbilityReplenish);
BZ_ABILITY_PROC(CAbilityReplenishLife);
BZ_ABILITY_PROC(CAbilityReplenishMana);
BZ_ABILITY_PROC(CAbilityCannibalize);
BZ_ABILITY_PROC(CAbilityRaiseDead);
BZ_ABILITY_PROC(CAbilityExhumeCorpses);
BZ_ABILITY_PROC(CAbilityGraveyard);
BZ_ABILITY_PROC(CAbilityAntiMagicShell);
BZ_ABILITY_PROC(CAbilitySpiritLink);
BZ_ABILITY_PROC(CAbilityAntiMagicShellInstant);
BZ_ABILITY_PROC(CAbilityPossession);
BZ_ABILITY_PROC(CAbilityPossessionTwo);
BZ_ABILITY_PROC(CAbilityRainOfChaos);
BZ_ABILITY_PROC(CAbilityInferno);
BZ_ABILITY_PROC(CAbilityDarkPortal);
BZ_ABILITY_PROC(CAbilityVolcano);
BZ_ABILITY_PROC(CAbilityUnsummon);
BZ_ABILITY_PROC(CAbilitySacrifice);
uint32_t S_SacrificeAbilityCode(void);
bool S_SacrificeSkipsFoodReservation(LPCEDICT item);
BZ_ABILITY_PROC(CAbilityHealingSpray);
BZ_ABILITY_PROC(CAbilityTransmute);

void human_ability_think(LPEDICT thinker);
void divine_shield_think(LPEDICT thinker);
void rain_of_chaos_think(LPEDICT thinker);
void inferno_think(LPEDICT thinker);
void dark_portal_think(LPEDICT thinker);
void exhume_think(LPEDICT thinker);
void stasis_trap_think(LPEDICT thinker);
void healing_spray_think(LPEDICT thinker);
void cannibalize_think(LPEDICT thinker);
void possession_two_think(LPEDICT thinker);
void lsh_think(LPEDICT thinker);
bool S_UnitIsDetected(LPCEDICT unit);
bool S_UnitIsDetectedByPlayer(LPCEDICT unit, uint32_t player);
bool S_UnitIsInvisibleToPlayer(LPCEDICT unit, uint32_t player);
bool S_AuraUnitActive(LPCEDICT unit);
bool S_UnitUsesInvisibilityRenderFlag(LPCEDICT unit);
bool S_PermanentInvisibilityActive(LPCEDICT unit);
void S_PermanentInvisibilityInitialize(LPEDICT unit);
void S_PermanentInvisibilityReveal(LPEDICT unit);
void S_InfernoLand(LPEDICT caster, uint32_t code, uint32_t level, LPCVECTOR2 point);
bool S_HoldPosition(LPEDICT unit);
bool S_MilitiaEnsureHallAbility(LPEDICT hall);
float S_MilitiaPairSearchRadius(uint32_t ability);
float S_RegenerationHealthAura(LPEDICT unit);
float S_RegenerationManaAura(LPEDICT unit);
void S_UpdateRegenerationAuraEffects(LPEDICT unit);
void S_UpdateHeroAuraEffects(LPEDICT unit);
void S_UpdateUnitPassiveEffects(LPEDICT unit);
uint32_t S_DevotionAuraBuff(LPEDICT unit);
uint32_t S_UnholyAuraBuff(LPEDICT unit);
bool S_RegenerationAuraUpdateDue(LPEDICT unit);
float S_BrillianceManaRegen(LPEDICT unit);
float S_DevotionArmorBonus(LPEDICT unit);
float S_UnholyHealthRegen(LPEDICT unit);
float S_UnholyMoveBonus(LPEDICT unit);
float S_VampiricLifeSteal(LPEDICT unit);
float S_TrueshotAttackBonus(LPEDICT unit);
int S_SearingArrowDamage(LPEDICT attacker, int damage);
float S_ThornsDamageReturn(LPCEDICT target, LPCEDICT attacker, float damage);
typedef struct { uint32_t alias; uint32_t level; } abilityAliasRef_t;
abilityAliasRef_t S_ResolveAbilityAlias(LPEDICT ent, uint32_t base_code);
bool S_EvasionRoll(LPEDICT target);
int S_CriticalStrikeDamage(LPEDICT attacker, int damage);
float S_SpikedArmorBonus(LPCEDICT unit);
float S_SpikedDamageReturn(LPCEDICT unit, float damage);
void S_PulverizeAttack(LPEDICT attacker, LPCEDICT primary);
void S_IncinerateOnHit(LPEDICT attacker, LPEDICT target);
void S_CreepAttackOnHit(LPEDICT attacker, LPEDICT target);
float S_CreepAttackSpeedReduction(LPCEDICT unit);
float S_SlowAuraMoveReduction(LPCEDICT unit);
float S_SlowAuraAttackReduction(LPCEDICT unit);
float S_CommandAuraAttackBonus(LPEDICT unit);
float S_WarDrumsAttackBonus(LPEDICT unit);
int S_ManaShieldDamage(LPEDICT target, int damage);
void S_SummonUnits(LPEDICT caster, uint32_t unit_id, uint32_t count, float duration);
LPEDICT S_SummonAt(LPEDICT caster, uint32_t unit_id, LPCVECTOR2 loc, float duration);
uint32_t S_EnforceSummonedUnitTypeLimit(LPEDICT caster, uint32_t unit_id, uint32_t max_count);
bool S_UnitHasStatus(LPCEDICT unit, uint32_t code);
bool S_UnitPolymorphed(LPCEDICT unit);
void S_PolymorphRemove(LPEDICT unit);
int S_BlackArrowDamage(LPEDICT attacker, int damage);
void S_BlackArrowDeath(LPEDICT attacker, LPEDICT target);
void S_ResolveAttackHit(LPEDICT attacker, LPEDICT target, int damage);
void S_ResolveArtilleryHit(LPEDICT attacker, LPEDICT target, int raw_damage);
void S_ResolveArtilleryPointHit(LPEDICT attacker, LPEDICT primary, LPCVECTOR2 impact, int raw_damage,
                                struct edictArtillery_s const *profile);
bool S_OrderAttackGround(LPEDICT unit, LPCVECTOR2 point);
void S_ReincarnationOnDeath(LPEDICT unit);
bool S_HumanCanAttack(LPCEDICT unit);
float S_HumanMoveFactor(LPCEDICT unit);
float S_DefendAttackReduction(LPCEDICT unit);
float S_HumanArmorBonus(LPCEDICT unit);
int S_HumanAttackDamage(LPEDICT attacker, LPEDICT target, int damage);
int S_FeedbackDamage(LPEDICT attacker, LPEDICT target, int damage);
int S_HardenedSkinDamage(LPEDICT target, int damage);
int S_OrbAnnihilationDamage(LPEDICT attacker, int damage);
bool S_UnitIsResistant(LPCEDICT unit);
void S_HumanAttackSplash(LPEDICT attacker, LPEDICT target, int damage);
void S_HumanBreakInvisibility(LPEDICT unit);
void S_HumanStatusExpired(LPEDICT unit, uint32_t code, uint32_t level);
bool S_UnitSpellImmune(LPCEDICT unit);
int S_AntiMagicShellAbsorb(LPEDICT target, int damage);
int S_SpiritLinkRedirect(LPEDICT target, LPEDICT attacker, int damage);
bool S_PossessionSpellImmune(LPCEDICT unit);
int S_PossessionDamageTaken(LPEDICT target, int damage);
bool S_SpellDamage(LPEDICT target, LPEDICT caster, int damage);
void S_AvatarExpire(LPEDICT unit);
float S_BloodlustAttackBonus(LPCEDICT unit);
float S_BloodlustMoveBonus(LPCEDICT unit);
float S_FaerieArmorDelta(LPCEDICT unit);
float S_RoarDamageBonus(LPCEDICT unit);
float S_RejuvHealRate(LPCEDICT unit);
float S_FrenzyAttackBonus(LPCEDICT unit);
float S_FrenzyArmorDelta(LPCEDICT unit);
float S_UnholyFrenzyAttackBonus(LPCEDICT unit);
float S_UnholyFrenzyLifeDrain(LPCEDICT unit);
float S_CurseMissChance(LPCEDICT unit);
float S_CrippleMoveReduction(LPCEDICT unit);
float S_EarthquakeMoveReduction(LPCEDICT unit);
float S_CrippleAttackReduction(LPCEDICT unit);
float S_CrippleDamageReduction(LPCEDICT unit);
float S_SoulBurnDamageRate(LPCEDICT unit);
float S_SoulBurnDamageReduction(LPCEDICT unit);
float S_PurgeMoveReduction(LPCEDICT unit);
bool S_PurgeIsImmobilized(LPCEDICT unit);
void S_MoonGlaiveAttack(LPEDICT attacker, LPEDICT primary, int damage);
void S_SlowPoisonOnHit(LPEDICT attacker, LPEDICT target);
void S_PoisonOnHit(LPEDICT attacker, LPEDICT target);
float S_SlowPoisonMoveReduction(LPCEDICT unit);
float S_SlowPoisonAttackReduction(LPCEDICT unit);
void S_OrbOnHit(LPEDICT attacker, LPEDICT target);
float S_BarkskinArmorBonus(LPCEDICT unit);
float S_ManaFlareArmorBonus(LPCEDICT unit);

float AB_Data(cstring_t classname, uint32_t level, uint32_t index);
uint32_t AB_DataId(cstring_t classname, uint32_t level, uint32_t index);

typedef enum {
	RETURN_RESOURCE_GOLD = 1,
	RETURN_RESOURCE_LUMBER = 2,
} returnResource_t;

bool S_CanReturnResourceAt(LPEDICT unit, LPEDICT building, returnResource_t resource);
LPEDICT S_FindNearestResourceDropoff(LPEDICT unit, returnResource_t resource);
void S_SetCarriedResource(LPEDICT unit, returnResource_t resource, uint32_t amount);

typedef enum {
	ABILITY_NUMBER_CAST,
	ABILITY_NUMBER_DURATION,
	ABILITY_NUMBER_HERO_DURATION,
	ABILITY_NUMBER_COOLDOWN,
	ABILITY_NUMBER_COST,
	ABILITY_NUMBER_AREA,
	ABILITY_NUMBER_RANGE
} abilityNumber_t;
uint32_t S_SpellCurrentCode(LPEDICT clent, uint32_t fallback);
ability_t const *S_SpellAbilityForCode(uint32_t code);
uint32_t S_SpellLevel(LPEDICT caster, uint32_t code);
float S_SpellNumber(uint32_t code, abilityNumber_t field, uint32_t level);
cstring_t S_SpellString(uint32_t code, cstring_t field, uint32_t level);
float S_SpellData(uint32_t code, uint32_t level, uint32_t index);
uint32_t S_SpellDataId(uint32_t code, uint32_t level, uint32_t index);
uint32_t S_SpellUnitId(uint32_t code, uint32_t level);
float S_SpellRange(uint32_t code, uint32_t level);
float S_SpellDuration(uint32_t code, uint32_t level, bool hero);
bool S_SpellCooldownReady(LPEDICT caster, uint32_t code);
float S_SpellCooldownRemaining(LPEDICT caster, uint32_t code);
float S_SpellCooldownLength(LPEDICT caster, uint32_t code);
bool S_SpellCooldownWindow(LPEDICT caster, uint32_t code, abilityCooldownWindow_t *window);
float S_SpellCooldownFraction(LPEDICT caster, uint32_t code, uint32_t level);
void S_SpellStartCooldownDuration(LPEDICT caster, uint32_t code, float seconds);
void S_SpellStartCooldown(LPEDICT caster, uint32_t code, uint32_t level);
void S_SpellEndCooldown(LPEDICT caster, uint32_t code);
void S_SpellResetCooldowns(LPEDICT caster);
bool S_SpellSpendMana(LPEDICT caster, uint32_t code, uint32_t level);
bool S_SpellCanPay(LPEDICT caster, uint32_t code, uint32_t level);
bool S_CastNoTargetSpell(LPEDICT caster, uint32_t code);
bool S_CastPointTargetSpell(LPEDICT caster, uint32_t code, LPCVECTOR2 point);
bool S_CastUnitTargetSpell(LPEDICT caster, uint32_t code, LPEDICT target);
bool S_IssueUnitTargetSpell(LPEDICT caster, uint32_t code, LPEDICT target);
bool S_SpellTargetInRange(LPEDICT caster, LPEDICT target, float range);
bool S_SpellIsAliveTarget(LPEDICT target);
bool S_UnitIsCycloned(LPCEDICT unit);
bool S_StatusIsUndispellable(heroabilitystatus_t const *status);
bool S_SummonIsDispelImmune(LPCEDICT unit);
bool S_UnitIsSilenced(LPCEDICT unit);
bool S_StatusIsEnsnare(uint32_t code);
bool S_UnitIsEnsnared(LPCEDICT unit);
bool S_UnitCanTranslate(LPCEDICT unit);
float S_EnsnareMeleeRange(LPCEDICT unit);
bool S_SpellIsEnemy(LPEDICT caster, LPEDICT target);
bool S_SpellIsFriend(LPEDICT caster, LPEDICT target);
bool S_SpellAllowsTarget(uint32_t code, LPEDICT caster, LPEDICT target);
bool S_SpellAllowsCorpseTarget(uint32_t code, LPEDICT caster, LPEDICT target);
bool S_SpellAllowsStoredCorpseTarget(uint32_t code, LPEDICT caster, LPEDICT target);
void S_SpellHeal(LPEDICT target, float amount);
void S_SpellCursorSplat(LPEDICT clent, float radius);
void S_SpellCodeString(uint32_t code, string_t out);
bool S_SpellIsChanneling(LPEDICT caster);
void S_SpellCancelChannel(LPEDICT caster);
LPEDICT S_SpellChannelThinker(LPEDICT caster, uint32_t code);
bool S_SpellChannelActive(LPEDICT thinker);
void S_SpellEndChannel(LPEDICT thinker);

/* Unified spell pipeline owns targeting and cast lifecycle; concrete procedures
 * receive validation and execution messages through the registry row. */
void spell_cmd(LPEDICT clent);
void spell_run_frame(LPEDICT ent);

#endif
