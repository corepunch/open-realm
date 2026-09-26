#include "s_skills.h"

#ifdef WC3_DEBUG_AUTOCAST
int G_AutocastDebugLevel(void) {
    cstring_t value;

    value = gi.CvarString("wc3_autocast_debug", "0");
    return value ? atoi(value) : 0;
}
#endif

cstring_t const raven_orders[] = { "ravenform", "unravenform", NULL };
static cstring_t const mana_shield_orders[] = { "manashieldon", "manashieldoff", NULL };
static cstring_t const build_orders[] = { "build", NULL };

static ability_t abilitylist[] = {
    { STR_CmdStop, CAbilityStop, AB_COMMAND },  // Stop — engine command
    { STR_CmdMove, CAbilityMove, AB_COMMAND },  // Move — engine command
    { STR_CmdAttack, CAbilityAttack, AB_COMMAND },  // Attack — engine command
    { STR_CmdAttackGround, CAbilityAttackGround, AB_COMMAND },  // Attack Ground — artillery engine command
    { STR_CmdBuild, CAbilityBuild, AB_COMMAND, SPELL_TARGET_NONE, build_orders },  // Build — engine command and queued-order owner
    { STR_CmdHoldPos, CAbilityHoldPosition, AB_COMMAND },  // Hold Position — engine command
    { STR_CmdPatrol, CAbilityPatrol, AB_COMMAND },  // Patrol — engine command
    { STR_CmdRally, CAbilityRally, AB_COMMAND },  // Rally — engine command
    { STR_CmdCancel, CAbilityCancel, AB_COMMAND },  // Cancel — engine command
    { STR_CmdCancelBuild, CAbilityCancel, AB_COMMAND },  // Cancel Build — engine command
    { STR_CmdSelectSkill, CAbilitySelectSkill, AB_COMMAND },  // Select Skill — engine command
    { STR_CmdTrains, CAbilityTrain, 0 },  // Training — internal move identity

    { "Amrf", CAbilityRavenForm, AB_COMMAND | AB_UPDATE, SPELL_TARGET_NONE, raven_orders },  /* Medivh Crow Form */
    { "Arav", CAbilityRavenForm, AB_COMMAND | AB_UPDATE, SPELL_TARGET_NONE, raven_orders },  /* Storm Crow Form */

    /* BEGIN GENERATED ABILITY STRINGS */

    /* CampaignAbilityStrings.txt */
    { "Aamk", CAbilityAttributeModSkill, AB_SPELL },  /* Attribute Bonus */
    { "ACtn", CAbilitySpawnTentacle, AB_SPELL, SPELL_TARGET_POINT },  /* Spawn Tentacle */
    { "ACs7", CAbilityFeralSpiritCampaign, AB_SPELL },  /* Feral Spirit */
    { "ANav", CAbilityAvatarCampaign, AB_SPELL },  /* Avatar */
    { "ANdc", CAbilityDarkConversion, AB_SPELL, SPELL_TARGET_UNIT },  /* Malganis - Dark Conversion */
    { "SNdc", CAbilityDarkConversion, AB_SPELL, SPELL_TARGET_UNIT },  /* Dark Conversion (Fast) */
    { "ANsh", CAbilityShockwaveCampaign, AB_SPELL, SPELL_TARGET_POINT },  /* Shockwave */
    { "ACs8", CAbilitySpiritBeast, AB_SPELL },  /* Spirit Beast */
    { "ANr2", CAbilityReincarnationCampaign, AB_SPELL },  /* Reincarnation */
    { "Afbb", CAbilityFeedbackCampaign, AB_SPELL | AB_TOGGLE },  /* Feedback (campaign toggle) */
    { "Andm", CAbilityAbolishMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Abolish Magic */
    { "Asb1", CAbilitySubmergeMyrmidon, AB_SPELL | AB_TOGGLE },  /* Submerge */
    { "Asb2", CAbilitySubmergeRoyalGuard, AB_SPELL | AB_TOGGLE },  /* Submerge */
    { "Asb3", CAbilitySubmergeSnapDragon, AB_SPELL | AB_TOGGLE },  /* Submerge */
    { "ANha", CAbilityHarvest, AB_COMMAND },  /* Harvest */
    { "ANen", CAbilityEnsnare, AB_SPELL | AB_UPDATE, SPELL_TARGET_UNIT },  /* Ensnare */
    { "ACfu", CAbilityFrostArmorCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Frost Armor */
    { "ANpa", CAbilityParasiteCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Parasite */
    { "Acny", CAbilityCyclone, AB_SPELL, SPELL_TARGET_UNIT },  /* Cyclone (naga; code=Acyc) */
    { "Ahnl", CAbilitySummoningRitual, AB_SPELL },  /* Summoning Ritual */
    { "ANcl", CAbilityChannel, AB_COMMAND },  /* Channel */
    { "Arsq", CAbilitySummonQuilbeastCampaign, AB_SPELL },  /* Summon Quilbeast */
    { "Arsg", CAbilitySummonMisha, AB_SPELL },  /* Summon Misha */
    { "Arsp", CAbilityStampedeCampaign, AB_SPELL, SPELL_TARGET_POINT },  /* Stampede */
    { "ANbr", CAbilityBattleRoar, AB_SPELL },  /* Battle Roar */
    { "ANsb", CAbilityStormBoltCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Storm Bolt */
    { "ANcf", CAbilityBreathOfFireCampaign, AB_SPELL, SPELL_TARGET_POINT },  /* Breath of Fire */
    { "Acdh", CAbilityDrunkenHazeCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Drunken Haze */
    { "Acef", CAbilityStormEarthFire, AB_SPELL },  /* "Storm, Earth, And Fire" */
    { "ANhw", CAbilityHealingWaveCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Healing Wave */
    { "ANhx", CAbilityHexCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Hex */
    { "Arsw", CAbilitySerpentWard, AB_SPELL, SPELL_TARGET_POINT },  /* Serpent Ward */
    { "AOr2", CAbilityEnduranceAuraCampaign, AB_SPELL },  /* Endurance Aura */
    { "AOs2", CAbilityShockwaveCairne, AB_SPELL, SPELL_TARGET_POINT },  /* Shockwave */
    { "AOr3", CAbilityReincarnationCairne, AB_SPELL },  /* Reincarnation */
    { "AOw2", CAbilityWarStompCampaign, AB_SPELL },  /* War Stomp */
    { "AOls", CAbilityVoodooSpirits, AB_SPELL },  /* Voodoo Spirits */

    /* CommonAbilityStrings.txt */
    { "Aall", CAbilityPassive, AB_PASSIVE },  /* Shop Sharing, Allied Bldg. */
    { "Abdt", CAbilityPassive, AB_PASSIVE },  /* Burrow Detection */
    { "Apit", CAbilityPurchaseItem, AB_COMMAND },  /* Shop Purchase Item */
    { "Ahar", CAbilityHarvest, AB_COMMAND },  /* Harvest */
    { "Ahrl", CAbilityHarvestLumber, AB_COMMAND },  /* Harvest */
    { "Arev", CAbilityPassive, AB_PASSIVE },  /* Revive Hero */
    { "Aawa", CAbilityPassive, AB_PASSIVE },  /* Revive Hero Instantly */
    { "Adet", CAbilityDetector, AB_PASSIVE },  /* Detector */
    { "Arep", CAbilityRepair, AB_COMMAND | AB_AUTOCAST },  /* Repair */
    { "AEpa", CAbilityPoisonArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Poison Arrows */
    { "AEbu", CAbilityBuild, AB_COMMAND },  /* Build (Night Elf) */
    { "AGbu", CAbilityBuild, AB_COMMAND },  /* Build (Naga) */
    { "AHbu", CAbilityBuild, AB_COMMAND },  /* Build (Human) */
    { "AHer", CAbilityPassive, AB_PASSIVE },  /* Hero */
    { "ANbu", CAbilityBuild, AB_COMMAND },  /* Build (Neutral) */
    { "AObu", CAbilityBuild, AB_COMMAND },  /* Build (Orc) */
    { "ARal", CAbilityRally, AB_COMMAND },  /* Rally */
    { "AUbu", CAbilityBuild, AB_COMMAND },  /* Build (Undead) */
    { "Aalr", CAbilityPassive, AB_PASSIVE },  /* Alarm */
    { "Aatk", CAbilityAttack, AB_COMMAND },  /* Attack */
    { "Afih", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire (Human) */
    { "Afin", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire (Night Elf) */
    { "Afio", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire (Orc) */
    { "Afir", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire */
    { "Afiu", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire (Undead) */
    { "Aloc", CAbilityPassive, AB_PASSIVE },  /* Locust */
    { "Amov", CAbilityMove, AB_COMMAND },  /* Move */
    { "Atdp", CAbilityCargoDrop, AB_COMMAND },  /* Drop Pilot */
    { "Atlp", CAbilityCargoLoad, AB_COMMAND },  /* Load Pilot */
    { "Attu", CAbilityPassive, AB_PASSIVE },  /* Turret */

    /* HumanAbilityStrings.txt */
    { "Amls", CAbilityMagicLeash, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Aerial Shackles */
    { "Afbk", CAbilityFeedback, AB_PASSIVE },  /* Feedback */
    { "Acmg", CAbilityControlMagic, AB_SPELL, SPELL_TARGET_UNIT },  /* Control Magic */
    { "AHdr", CAbilityDrain, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Siphon Mana */
    { "Aflk", CAbilityPassive, AB_PASSIVE },  /* Flak Cannons */
    { "Afsh", CAbilityPassive, AB_PASSIVE },  /* Fragmentation Shards */
    { "Aroc", CAbilityPassive, AB_PASSIVE },  /* Barrage */
    { "Amdf", CAbilityMagicDefense, AB_SPELL | AB_TOGGLE },  /* Magic Defense */
    { "Asph", CAbilityPassive, AB_PASSIVE },  /* Sphere */
    { "Asps", CAbilitySpellSteal, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Spell Steal */
    { "Aclf", CAbilityCloudOfFog, AB_SPELL, SPELL_TARGET_UNIT },  /* Cloud */
    { "AHfs", CAbilityFlameStrike, AB_SPELL, SPELL_TARGET_POINT },  /* Flame Strike */
    { "AHbn", CAbilityBanish, AB_SPELL, SPELL_TARGET_UNIT },  /* Banish */
    { "AHpx", CAbilitySummonPhoenix, AB_SPELL },  /* Phoenix */
    { "Aphx", CAbilityPassive, AB_PASSIVE },  /* Phoenix Morphing (Egg Related) */
    { "Apxf", CAbilityPassive, AB_PASSIVE },  /* Phoenix Fire */
    { "Agyb", CAbilityPassive, AB_PASSIVE },  /* Flying Machine Bombs */
    { "Asth", CAbilityPassive, AB_PASSIVE },  /* Storm Hammers */
    { "Agyv", CAbilityTrueSight, AB_PASSIVE },  /* True Sight */
    { "Adef", CAbilityDefend, AB_SPELL | AB_TOGGLE },  /* Defend */
    { "Afla", CAbilityFlare, AB_SPELL, SPELL_TARGET_POINT },  /* Flare */
    { "Adts", CAbilityMagicSentry, AB_PASSIVE },  /* Magic Sentry */
    { "Ainf", CAbilityInnerFire, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Inner Fire */
    { "Adis", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Dispel Magic */
    { "Ahea", CAbilityHeal, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Heal */
    { "Aslo", CAbilitySlow, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Slow */
    { "Aivs", CAbilityInvisibility, AB_SPELL, SPELL_TARGET_UNIT },  /* Invisibility */
    { "Aply", CAbilityPolymorph, AB_SPELL, SPELL_TARGET_UNIT },  /* Polymorph */
    { "ACpy", CAbilityPolymorph, AB_SPELL, SPELL_TARGET_UNIT },  /* Polymorph (creep) */
    { "AHbz", CAbilityBlizzard, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Blizzard */
    { "AHwe", CAbilityWaterElemental, AB_SPELL },  /* Summon Water Elemental */
    { "AHab", CAbilityPassive, AB_PASSIVE },  /* Brilliance Aura */
    { "AHmt", CAbilityMassTeleport, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Mass Teleport */
    { "AHtb", CAbilityThunderBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Storm Bolt */
    { "AHtc", CAbilityThunderClap, AB_SPELL },  /* Thunder Clap */
    { "AHbh", CAbilityPassive, AB_PASSIVE },  /* Bash */
    { "AHav", CAbilityAvatar, AB_SPELL },  /* Avatar */
    { "AHhb", CAbilityHolyBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Holy Light */
    { "AHds", CAbilityDivineShield, AB_SPELL },  /* Divine Shield */
    { "AHad", CAbilityAuraDevotion, AB_PASSIVE },  /* Devotion Aura */
    { "AHre", CAbilityResurrection, AB_SPELL, SPELL_TARGET_NONE },  /* Resurrection */
    { "Amil", CAbilityMilitia, AB_COMMAND },  /* Call to Arms */
    { "Amic", CAbilityMilitiaConvert, AB_COMMAND | AB_SEPARATE_OFF },  /* Call To Arms */

    /* ItemAbilityStrings.txt */
    { "AIsm", CAbilityStrengthMod, AB_ITEM },  /* Item Strength Gain */
    { "AIam", CAbilityStrengthMod, AB_ITEM },  /* Item Agility Gain */
    { "AIat", CAbilityAttackBonus, 0 },  /* Item Damage Bonus */
    { "AIt6", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +6 */
    { "AIt9", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +9 */
    { "AItc", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +12 */
    { "AItf", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +15 */
    { "AItg", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +1 */
    { "AIth", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +2 */
    { "AIti", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +4 */
    { "AItj", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +5 */
    { "AItk", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +7 */
    { "AItl", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +8 */
    { "AItn", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +10 */
    { "AItx", CAbilityAttackBonus, 0 },  /* Item Damage Bonus +20 */
    { "AIde", CAbilityDefenseBonus, 0 },  /* Item Armor Bonus */
    { "AIem", CAbilityExperienceMod, AB_ITEM },  /* Item Experience Gain */
    { "AIlm", CAbilityLevelMod, AB_ITEM },  /* Item Level Gain */
    { "AIim", CAbilityStrengthMod, AB_ITEM },  /* Item Intelligence Gain */
    { "AIxm", CAbilityStrengthMod, AB_ITEM },  /* Item Int/Agi/Str gain */
    { "AIhe", CAbilityItemHeal, AB_ITEM },  /* Item Healing */
    { "AIma", CAbilityItemManaRestore, AB_ITEM },  /* Item Mana Regain */
    { "AIda", CAbilityItemDefenseAoe, AB_ITEM },  /* Item Temporary Area Armor Bonus */
    { "AIco", CAbilityCharm, AB_SPELL, SPELL_TARGET_UNIT },  /* Item Command */
    { "AIfs", CAbilityFigurineSkeleton, AB_ITEM },  /* Item Skeleton Summon */
    { "AImi", CAbilityMaxLifeMod, AB_ITEM },  /* Item Permanent Life Gain */
    { "AIab", CAbilityAttributeBonus, 0 },  /* Item Hero Stat Bonus */
    { "AIml", CAbilityMaxLifeBonus, 0 },  /* Item Life Bonus */
    { "AImm", CAbilityMaxManaBonus, 0 },  /* Item Mana Bonus */
    { "AIct", CAbilityItemChangeTOD, AB_ITEM },  /* Change Time of Day */

    /* NeutralAbilityStrings.txt */
    { "ANab", CAbilityAcidBomb, AB_SPELL, SPELL_TARGET_UNIT },  /* Acid Bomb */
    { "ANms", CAbilityManaShield, AB_SPELL | AB_TOGGLE, SPELL_TARGET_NONE, mana_shield_orders },  /* Mana Shield */
    { "ACmf", CAbilityManaShield, AB_SPELL | AB_TOGGLE, SPELL_TARGET_NONE, mana_shield_orders },  /* Mana Shield (creep) */
    { "ANrf", CAbilityRainOfFire, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Rain of Fire */
    { "AHca", CAbilityColdArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Cold Arrows */
    { "ANht", CAbilityHowlOfTerror, AB_SPELL },  /* Howl of Terror */
    { "ANca", CAbilityCleavingAttack, AB_PASSIVE },  /* Cleaving Attack */
    { "ANdo", CAbilityDoom, AB_SPELL, SPELL_TARGET_UNIT },  /* Doom */
    { "ANdr", CAbilityDrainNeutral, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Life Drain */
    { "ANbf", CAbilityBreathOfFire, AB_SPELL, SPELL_TARGET_POINT },  /* Breath of Fire */
    { "ANsg", CAbilitySummonGrizzly, AB_SPELL },  /* Summon Bear */
    { "ANsq", CAbilitySummonQuillbeast, AB_SPELL },  /* Summon Quilbeast */
    { "ANsw", CAbilitySummonWarEagle, AB_SPELL },  /* Summon Hawk */
    { "ANst", CAbilityStampede, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Stampede */
    { "ANfs", CAbilityFlameStrikeNeutral, AB_SPELL, SPELL_TARGET_POINT },  /* Flame Strike */
    { "AInv", CAbilityInventory, AB_PASSIVE },  /* Inventory */
    { "ANdb", CAbilityDrunkenBrawler, AB_PASSIVE },  /* Drunken Brawler */
    { "ANdh", CAbilityDrunkenHaze, AB_SPELL, SPELL_TARGET_UNIT },  /* Drunken Haze */
    { "ANsi", CAbilitySilence, AB_SPELL, SPELL_TARGET_POINT },  /* Silence */
    { "ANba", CAbilityBlackArrow, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Black Arrow */
    { "ANch", CAbilityCharm, AB_SPELL, SPELL_TARGET_UNIT },  /* Charm */
    { "ANto", CAbilityTornado, AB_SPELL | AB_CHANNEL },  /* Tornado */
    { "Abgm", CAbilityBlightedGoldMine, 0 },  /* Blighted Gold Mine Ability */
    { "Aegm", CAbilityPassive, AB_PASSIVE },  /* Entangled Gold Mine Ability */
    { "Aloa", CAbilityCargoLoad, AB_COMMAND },  /* Load */
    { "Adro", CAbilityCargoDrop, AB_COMMAND },  /* Unload */
    { "Adri", CAbilityCargoDropInstant, AB_COMMAND },  /* Unload Instant */
    { "Abun", CAbilityPassive, AB_PASSIVE },  /* Cargo Hold (Orc Burrow) */
    { "Acar", CAbilityPassive, AB_PASSIVE },  /* Cargo Hold */
    { "Aneu", CAbilityPassive, AB_PASSIVE },  /* Select Hero */
    { "Ane2", CAbilityPassive, AB_PASSIVE },  /* Neutral Building (any unit) */
    { "ANfb", CAbilityFireBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Firebolt */
    { "Agld", CAbilityGoldMine, 0 },  /* Gold Mine ability */
    { "Artn", CAbilityReturn, AB_COMMAND },  /* Return */
    { "Avul", CAbilityPassive, AB_PASSIVE },  /* Invulnerable */
    { "Abli", CAbilityBlightGrowth, AB_PASSIVE | AB_UPDATE | AB_INNATE },  /* Blight */
    { "ANfl", CAbilityForkedLightning, AB_SPELL, SPELL_TARGET_UNIT },  /* Forked Lightning */

    /* NightElfAbilityStrings.txt */
    { "AEbl", CAbilityBlink, AB_SPELL, SPELL_TARGET_POINT },  /* Blink */
    { "AEfk", CAbilityFanOfKnives, AB_SPELL },  /* Fan of Knives */
    { "AEsh", CAbilityShadowStrike, AB_SPELL, SPELL_TARGET_UNIT },  /* Shadow Strike */
    { "AEsv", CAbilitySpiritOfVengeance, AB_SPELL },  /* Vengeance */
    { "Aeat", CAbilityEatTree, AB_SPELL, SPELL_TARGET_UNIT },  /* Eat Tree */
    { "Ambt", CAbilityManaBattery, AB_SPELL, SPELL_TARGET_UNIT },  /* Replenish Mana and Life */
    { "Awha", CAbilityWispHarvest, AB_COMMAND },  /* Gather */
    { "Aent", CAbilityEntangle, AB_COMMAND },  /* Entangle Gold Mine */
    { "Aenc", CAbilityPassive, AB_PASSIVE },  /* Load */
    { "Aroo", CAbilityRoot, AB_COMMAND },  /* Root */
    { "AEmb", CAbilityManaBurn, AB_SPELL, SPELL_TARGET_UNIT },  /* Mana Burn */
    { "AEim", CAbilityImmolation, AB_SPELL | AB_TOGGLE },  /* Immolation */
    { "AEev", CAbilityPassive, AB_PASSIVE },  /* Evasion */
    { "AEme", CAbilityMetamorphosis, AB_SPELL },  /* Metamorphosis */
    { "AEer", CAbilityEntanglingRoots, AB_SPELL, SPELL_TARGET_UNIT },  /* Entangling Roots */
    { "Aenr", CAbilityEntanglingRoots, AB_SPELL, SPELL_TARGET_UNIT },  /* Entangling Roots (creep) */
    { "Aenw", CAbilityEntanglingRoots, AB_SPELL, SPELL_TARGET_UNIT },  /* Entangling Seaweed */
    { "AEfn", CAbilityForceOfNature, AB_SPELL },  /* Force of Nature */
    { "AEah", CAbilityCreepAura, AB_PASSIVE },  /* Thorns Aura */
    { "AEtq", CAbilityTranquility, AB_SPELL | AB_CHANNEL },  /* Tranquility */
    { "AHfa", CAbilityFlamingArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Searing Arrows */
    { "ACsa", CAbilityFlamingArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Searing Arrows (creep) */
    { "AEar", CAbilityCreepAura, AB_PASSIVE },  /* Trueshot Aura */
    { "AEsf", CAbilityStarfall, AB_SPELL | AB_CHANNEL },  /* Starfall */
    { "Aren", CAbilityRepairGeneric, AB_COMMAND | AB_AUTOCAST },  /* Renew */
    { "Afae", CAbilityFaerieFire, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Faerie Fire */
    { "Arej", CAbilityRejuvination, AB_SPELL, SPELL_TARGET_UNIT },  /* Rejuvenation */
    { "Aroa", CAbilityRoar, AB_SPELL },  /* Roar */

    /* OrcAbilityStrings.txt */
    { "AOhw", CAbilityHealingWave, AB_SPELL, SPELL_TARGET_UNIT },  /* Healing Wave */
    { "AOhx", CAbilityHex, AB_SPELL, SPELL_TARGET_UNIT },  /* Hex */
    { "AOvd", CAbilityVoodoo, AB_SPELL },  /* Big Bad Voodoo */
    { "Astd", CAbilityStandDown, AB_COMMAND },  /* Stand Down */
    { "Abtl", CAbilityBattlestations, AB_COMMAND },  /* Battle Stations */
    { "AOwk", CAbilityWindWalk, AB_SPELL },  /* Wind Walk */
    { "AOmi", CAbilityMirrorImage, AB_SPELL },  /* Mirror Image */
    { "AOcr", CAbilityCreepAura, AB_PASSIVE },  /* Critical Strike */
    { "AOww", CAbilityWhirlwind, AB_SPELL | AB_CHANNEL },  /* Bladestorm */
    { "AOcl", CAbilityChainLightning, AB_SPELL, SPELL_TARGET_UNIT },  /* Chain Lightning */
    { "AOfs", CAbilityFarSight, AB_SPELL, SPELL_TARGET_POINT },  /* Far Sight */
    { "AOsf", CAbilitySpiritWolf, AB_SPELL },  /* Feral Spirit */
    { "AOeq", CAbilityEarthquake, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Earthquake */
    { "AOsh", CAbilityShockwave, AB_SPELL, SPELL_TARGET_POINT },  /* Shockwave */
    { "AOae", CAbilityPassive, AB_PASSIVE },  /* Endurance Aura */
    { "AOre", CAbilityReincarnation, AB_PASSIVE },  /* Reincarnation */
    { "AOws", CAbilityStomp, AB_SPELL },  /* War Stomp */
    { "Ablo", CAbilityBloodlust, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Bloodlust */

    /* UndeadAbilityStrings.txt */
    { "AUim", CAbilityImpale, AB_SPELL, SPELL_TARGET_POINT },  /* Impale */
    { "AUts", CAbilityPassive, AB_PASSIVE },  /* Spiked Carapace */
    { "AUcb", CAbilityCarrionScarabs, AB_SPELL | AB_AUTOCAST },  /* Carrion Beetles */
    { "AUls", CAbilityLocustSwarm, AB_SPELL | AB_CHANNEL },  /* Locust Swarm */
    { "Aaha", CAbilityAcolyteHarvest, AB_COMMAND },  /* Gather */
    { "AUdc", CAbilityDeathCoil, AB_SPELL, SPELL_TARGET_UNIT },  /* Death Coil */
    { "AUau", CAbilityCreepAura, AB_PASSIVE },  /* Unholy Aura */
    { "AUdp", CAbilityDeathPact, AB_SPELL, SPELL_TARGET_UNIT },  /* Death Pact */
    { "AUan", CAbilityAnimateDead, AB_SPELL },  /* Animate Dead */
    { "AUa2", CAbilityAnimateDead, AB_SPELL },  /* Animate Dead (2.0.3 ability-preserving variant) */
    { "AUcs", CAbilityCarrionSwarm, AB_SPELL, SPELL_TARGET_POINT },  /* Carrion Swarm */
    { "ACca", CAbilityCarrionSwarm, AB_SPELL, SPELL_TARGET_POINT },  /* Carrion Swarm (creep) */
    { "ACcv", CAbilityCarrionSwarm, AB_SPELL, SPELL_TARGET_POINT },  /* Crushing Wave */
    { "ACc2", CAbilityCarrionSwarm, AB_SPELL, SPELL_TARGET_POINT },  /* Crushing Wave (Dragon Turtle) */
    { "ACc3", CAbilityCarrionSwarm, AB_SPELL, SPELL_TARGET_POINT },  /* Crushing Wave (Lesser) */
    { "AUsl", CAbilitySleep, AB_SPELL, SPELL_TARGET_UNIT },  /* Sleep */
    { "ACsl", CAbilitySleep, AB_SPELL, SPELL_TARGET_UNIT },  /* Sleep (creep) */
    { "AUav", CAbilityCreepAura, AB_PASSIVE },  /* Vampiric Aura */
    { "AUfn", CAbilityFrostNova, AB_SPELL, SPELL_TARGET_UNIT },  /* Frost Nova */
    { "AUfa", CAbilityFrostArmor, AB_SPELL, SPELL_TARGET_UNIT },  /* Frost Armor */
    { "AUfu", CAbilityFrostArmor, AB_SPELL, SPELL_TARGET_UNIT },  /* Frost Armor */
    { "AUdr", CAbilityDarkRitual, AB_SPELL, SPELL_TARGET_UNIT },  /* Dark Ritual */
    { "AUdd", CAbilityDeathAndDecay, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Death And Decay */
    { "Arst", CAbilityRepairGeneric, AB_COMMAND | AB_AUTOCAST },  /* Restore */
    { "AUin", CAbilityDreadLordInferno, AB_SPELL, SPELL_TARGET_POINT },  /* Inferno */

    /* No AbilityStrings source file */
    { "Acoi", CAbilityCoupleInstant, AB_COMMAND },  /* Couple Instant */

    /* Concrete AbilityData entries without a generated AbilityStrings entry. */
    { "AAns", CAbilityPassive, AB_PASSIVE },  /* AAns */
    { "Atpi", CAbilityPassive, AB_PASSIVE },  /* Atpi */
    /* END GENERATED ABILITY STRINGS */
    /* BEGIN GENERATED TODO ABILITIES */

    /* CampaignAbilityStrings.txt */

    /* ItemAbilityStrings.txt */
    // TODO: AIsp a_item_speed  /* Item Temporary Speed Bonus */
    // TODO: AIdm a_bounce  /* Item Area tree/wall damage */
    // TODO: AIfl a_button  /* Item Capture The Flag */
    // TODO: AIfm a_button  /* Item Capture The Flag */
    // TODO: AIfn a_button  /* Item Capture The Flag */
    // TODO: AIfo a_button  /* Item Capture The Flag */
    // TODO: AIfe a_button  /* Item Capture The Flag */
    // TODO: AIha a_item_heal_aoe  /* Item Area Healing */
    // TODO: AIvi a_unknown  /* Item Temporary Invisibility */
    // TODO: AIvu a_item_invul  /* Item Temporary Invulnerability */
    // TODO: AImr a_item_mana_restore_aoe  /* Item Area Mana Regain */
    // TODO: AIre a_item_restore  /* Item Heal/Mana Regain */
    // TODO: AIra a_item_restore_aoe  /* Item Area Heal/Mana Regain */
    // TODO: AIta a_item_town_portal  /* Item Area Detection */
    // TODO: AIrm a_item_regen_mana  /* Item Mana Regeneration */
    // TODO: AIil a_item_illusion  /* Item Illusions */
    // TODO: AIdi a_item_dispel_aoe  /* Item Dispel */
    { "AIfb", CAbilityAttackBonus, 0 },  /* Item Attack Fire Bonus (orb) */
    { "AIlb", CAbilityAttackBonus, 0 },  /* Item Attack Lightning Bonus (orb) */
    { "AIob", CAbilityAttackBonus, 0 },  /* Item Attack Frost Bonus (orb) */
    { "AIlp", CAbilityPurge, AB_SPELL, SPELL_TARGET_UNIT },  /* Item Purge */
    { "AIpb", CAbilityAttackBonus, 0 },  /* Item Attack Poison Bonus (orb) */
    { "AIcb", CAbilityAttackBonus, 0 },  /* Item Attack Corruption Bonus (orb) */
    // TODO: AIsi a_sight_bonus  /* Item Sight Range Bonus */
    // TODO: AIso a_simple_spell  /* Item Soul Theft */
    // TODO: Asou a_simple_spell  /* Item Soul Possession */
    // TODO: AIrc a_item_reincarnation  /* Item Reincarnation */
    // TODO: AIrt a_item_recall  /* Item Recall */
    // TODO: AItp a_item_town_portal  /* Item Town Portal */
    // TODO: AIpm a_button  /* Item Place Goblin Land Mine */
    // TODO: AIaa a_damage_bonus_base  /* Item Permanent Damage Gain */
    // TODO: AIva a_attack_mod  /* Item Life Steal */
    // TODO: AIcf CAbilityImmolation  /* Item Immolation */
    { "AIzb", CAbilityAttackBonus, 0 },  /* Item Freeze Damage Bonus (orb) */
    // TODO: Arel a_aura_regen_life  /* Item Life Regeneration */
    { "Aami", CAbilityAntiMagicShellInstant, AB_SPELL, SPELL_TARGET_UNIT },  /* Item Anti-Magic Shell Instant */
    // TODO: AIas a_unknown  /* Item Attack Speed Bonus */
    // TODO: AIan a_simple_spell  /* Item Animate Dead */
    // TODO: AIrs a_item_reincarnation  /* Item Resurrection */
    // TODO: AIms a_move_speed_bonus  /* Item Move Speed Bonus */
    // TODO: AIgo a_attack_mod  /* Chest of Gold */
    // TODO: AIlu a_item_heal_aoe  /* Bundle of Lumber */
    // TODO: AIfa a_agility_mod  /* Flare Gun */
    // TODO: AIrv a_item_heal_aoe  /* Item Reveal Entire Map */
    // TODO: AIdc CAbilityItemDefenseAoe  /* Item Chain Dispel */
    // TODO: AIwb a_button  /* Item Web */
    // TODO: AImo a_item_mana_restore_aoe  /* Monster Lure */
    // TODO: AIri a_item_speed  /* Random Item */
    // TODO: Ablp CAbilityItemHeal  /* Blight Placement */
    // TODO: Aste a_figurine_rock_golem  /* Steal */
    // TODO: AIpv a_item_mana_restore_aoe  /* Vampiric Potion */
    // TODO: AIsr a_item_speed  /* Spell Damage Reduction */
    // TODO: AIbl CAbilityOnFireHuman  /* Build Tiny Castle */
    // TODO: Ashs a_spell  /* Wand of Shadowsight */
    // TODO: Aret CAbilityResurrection  /* Tome of Retraining */
    // TODO: ANpr a_button  /* Staff of Preservation */
    // TODO: Amec a_button  /* Mechanical Critter */
    // TODO: ANss a_bounce  /* Spell Shield */
    // TODO: ANse a_spell  /* Spell Shield */
    // TODO: Aspb a_bounce  /* Spell Book */
    { "AIrd", CAbilityRaiseDead, AB_SPELL },  /* Raise Dead (Item) */
    // TODO: ANsa a_bounce  /* Staff of Sanctuary */
    // TODO: AIsa a_item_speed  /* Scroll of Haste */
    // TODO: AItb a_button  /* Dust of Appearance */
    // TODO: AIsb CAbilityItemHeal  /* Orb of Slow */
    // TODO: ANbs a_spell  /* Orb of Darkness */
    // TODO: AIrb a_item_heal_aoe  /* Rebirth */
    // TODO: AUds CAbilityMassTeleport  /* Dark Summoning */
    // TODO: AIdd CAbilityItemHeal  /* Defend */
    // TODO: AIsh a_item_town_portal  /* Summon Headhunter */

    /* NeutralAbilityStrings.txt */
    { "ANic", CAbilityIncinerate, AB_PASSIVE },  /* Incinerate */
    { "ANia", CAbilityIncinerate, AB_PASSIVE },  /* Incinerate Arrow */
    { "ANso", CAbilitySoulBurn, AB_SPELL, SPELL_TARGET_UNIT },  /* Soul Burn */
    { "ANlm", CAbilityWaterElemental, AB_SPELL },  /* Summon Lava Spawn */
    { "ANvc", CAbilityVolcano, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Volcano */
    { "ANsy", CAbilityPocketFactory, AB_SPELL, SPELL_TARGET_POINT },  /* Pocket Factory */
    { "ANs1", CAbilityPocketFactory, AB_SPELL, SPELL_TARGET_POINT },  /* Pocket Factory (Level 1) */
    { "ANs2", CAbilityPocketFactory, AB_SPELL, SPELL_TARGET_POINT },  /* Pocket Factory (Level 2) */
    { "ANs3", CAbilityPocketFactory, AB_SPELL, SPELL_TARGET_POINT },  /* Pocket Factory (Level 3) */
    { "ANcs", CAbilityFlameStrike, AB_SPELL, SPELL_TARGET_POINT },  /* Cluster Rockets */
    { "ANeg", CAbilityEngineeringUpgrade, AB_PASSIVE },  /* Engineering Upgrade */
    { "ANrg", CAbilityMetamorphosis, AB_SPELL },  /* Robo-Goblin */
    { "ANde", CAbilityDemolish, AB_PASSIVE },  /* Demolish */
    { "ANfy", CAbilityFactory, AB_PASSIVE },  /* Factory */
    { "ANhs", CAbilityHealingSpray, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Healing Spray */
    { "ANcr", CAbilityMetamorphosis, AB_SPELL },  /* Chemical Rage */
    { "ANtm", CAbilityTransmute, AB_SPELL, SPELL_TARGET_UNIT },  /* Transmute */
    { "Aasl", CAbilitySlowAura, AB_PASSIVE },  /* Slow Aura */
    { "Atdg", CAbilityTornadoDamage, AB_PASSIVE },  /* Building Damage Aura */
    { "Atsp", CAbilityTornado, AB_SPELL | AB_CHANNEL },  /* Tornado Spin */
    { "Atwa", CAbilityTornado, AB_SPELL | AB_CHANNEL },  /* Tornado Wander */
    { "ANef", CAbilityStormEarthFire, AB_SPELL },  /* Storm, Earth, And Fire */
    { "ACbf", CAbilityFrostNova, AB_SPELL, SPELL_TARGET_UNIT },  /* Breath of Frost */
    { "ANmr", CAbilityMindRot, AB_PASSIVE },  /* Mind Rot */
    { "ANmo", CAbilityMonsoon, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Monsoon */
    { "ANwm", CAbilityWaterElemental, AB_SPELL },  /* Watery Minion */
    { "Arng", CAbilityRevenge, AB_PASSIVE },  /* Revenge */
    { "Atol", CAbilityTreeOfLife, AB_PASSIVE },  /* Tree of Life upgrade ability */
    { "Awrp", CAbilityWarp, AB_PASSIVE | AB_INNATE },  /* Waygate ability */
    { "ANsl", CAbilityResurrection, AB_SPELL, SPELL_TARGET_NONE },  /* Soul Preservation */
    { "ANfd", CAbilityFingerOfDeath, AB_SPELL, SPELL_TARGET_UNIT },  /* Finger of Death */
    { "ANdp", CAbilityDarkPortal, AB_SPELL, SPELL_TARGET_POINT },  /* Dark Portal */
    { "ANrc", CAbilityRainOfChaos, AB_SPELL, SPELL_TARGET_POINT },  /* Rain of Chaos */
    { "ANr3", CAbilityRainOfChaos, AB_SPELL, SPELL_TARGET_POINT },  /* Rain of Chaos (button) */
    { "Achd", CAbilityCargoHold, AB_PASSIVE },  /* Cargo Hold Death */
    { "Asla", CAbilitySleepAlways, AB_PASSIVE | AB_INNATE },  /* Sleep Always */
    { "Advc", CAbilityCargoLoad, AB_COMMAND },  /* Devour Cargo */
    { "ANpi", CAbilityImmolation, AB_SPELL | AB_TOGGLE },  /* Permanent Immolation */
    { "Apig", CAbilityImmolation, AB_SPELL | AB_TOGGLE },  /* Permanent Immolation */
    { "Andt", CAbilityFarSight, AB_SPELL, SPELL_TARGET_POINT },  /* Reveal */
    { "ANin", CAbilityInferno, AB_SPELL, SPELL_TARGET_POINT },  /* Inferno */
    { "Anhe", CAbilityHeal, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Heal (creep) */
    { "ACtc", CAbilityThunderClap, AB_SPELL },  /* Slam */
    { "ACtb", CAbilityThunderBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Hurl Boulder */
    { "Afzy", CAbilityFrenzy, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Frenzy */
    { "ACdv", CAbilityCannibalize, AB_SPELL | AB_CHANNEL },  /* Devour */
    { "ACsp", CAbilityCreepSleep, AB_PASSIVE | AB_INNATE },  /* Natural creep sleep */
    { "Asod", CAbilityRaiseDead, AB_SPELL },  /* Spawn Skeleton */
    { "Assp", CAbilityForceOfNature, AB_SPELL },  /* Spawn Spiderlings */
    { "Aspd", CAbilityForceOfNature, AB_SPELL },  /* Spawn Spiders */
    { "AOac", CAbilityCommandAura, AB_PASSIVE },  /* Command Aura */
    { "ACad", CAbilityAnimateDead, AB_SPELL },  /* Animate Dead (creep) */
    { "ACrn", CAbilityReincarnation, AB_PASSIVE },  /* Reincarnation (creep) */
    { "Adda", CAbilityDeathDamageAoe, AB_PASSIVE },  /* AOE damage upon death */
    { "Agho", CAbilityGhost, AB_PASSIVE },  /* Ghost */
    { "Aeth", CAbilityGhostVisible, AB_PASSIVE },  /* Ghost */
    { "Amin", CAbilityStasisTrap, AB_SPELL, SPELL_TARGET_POINT },  /* Mine - exploding */
    { "Apiv", CAbilityPermanentInvisibility, AB_PASSIVE | AB_INNATE },  /* Permanent Invisibility */
    { "Awan", CAbilityWander, AB_PASSIVE },  /* Wander */
    /* Aarm is registered with the explicit regeneration family below. */
    { "Asid", CAbilitySellItem, AB_PASSIVE },  /* Sell Items */
    { "Asud", CAbilitySellUnit, AB_PASSIVE },  /* Sell Units */

    /* NightElfAbilityStrings.txt */
    { "Avng", CAbilitySpiritOfVengeance, AB_SPELL, SPELL_TARGET_NONE },  /* Spirit of Vengeance */
    { "Amfl", CAbilityManaFlare, AB_SPELL | AB_CHANNEL | AB_UPDATE, SPELL_TARGET_NONE },  /* Mana Flare */
    { "Apsh", CAbilityDivineShield, AB_SPELL },  /* Phase Shift */
    { "Aetl", CAbilityEthereal, AB_PASSIVE },  /* Ethereal */
    { "Agra", CAbilityGrabTree, AB_PASSIVE },  /* War Club */
    { "Assk", CAbilityHardenedSkin, AB_PASSIVE },  /* Hardened Skin */
    { "Arsk", CAbilityResistantSkin, AB_PASSIVE },  /* Resistant Skin */
    { "Atau", CAbilityTaunt, AB_SPELL },  /* Taunt */
    { "Amgl", CAbilityMoonGlaive, AB_PASSIVE | AB_INNATE },  /* Moon Glaive */
    { "Aspo", CAbilitySlowPoison, AB_PASSIVE | AB_INNATE },  /* Slow Poison */
    { "Ashm", CAbilityWindWalk, AB_SPELL },  /* Shadow Meld */
    { "Ahid", CAbilityWindWalk, AB_SPELL },  /* Hide */
    { "Aesn", CAbilityEvilEye, AB_SPELL, SPELL_TARGET_POINT },  /* Sentinel */
    { "Adtn", CAbilitySelfDestruct, AB_SPELL, SPELL_TARGET_NONE },  /* Detonate */
    { "Abrf", CAbilityMetamorphosis, AB_SPELL },  /* Bear Form */

    { "Aadm", CAbilityAbolishMagic, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_POINT },  /* Abolish Magic */
    { "Amim", CAbilityPassive, AB_PASSIVE },  /* Spell Immunity */
    { "Ault", CAbilityPassive, AB_PASSIVE },  /* Ultravision */
    { "Acoa", CAbilityCoupleArcher, AB_SPELL, SPELL_TARGET_UNIT },  /* Mount Hippogryph */
    { "Acoh", CAbilityCoupleHippogryph, AB_SPELL, SPELL_TARGET_UNIT },  /* Pick up Archer */
    { "Adec", CAbilityDecouple, AB_SPELL, SPELL_TARGET_NONE },  /* Dismount */
    { "Acor", CAbilityCorrosiveBreath, AB_PASSIVE },  /* Corrosive Breath */
    { "AEst", CAbilityScout, AB_PASSIVE },  /* Scout */
    { "Acyc", CAbilityCyclone, AB_SPELL, SPELL_TARGET_UNIT },  /* Cyclone */
    { "ACcy", CAbilityCyclone, AB_SPELL, SPELL_TARGET_UNIT },  /* Cyclone (creep) */
    { "SCc1", CAbilityCyclone, AB_SPELL, SPELL_TARGET_UNIT },  /* Cyclone (Cenarius) */
    { "Alit", CAbilityLightningAttack, AB_PASSIVE },  /* Lightning Attack */

    /* OrcAbilityStrings.txt */
    { "Abof", CAbilityBallsOfFire, AB_PASSIVE },  /* Burning Oil */
    { "Absk", CAbilityFrenzy, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Berserk */
    { "Arbr", CAbilityPassive, AB_PASSIVE },  /* Reinforced Burrows Upgrade */
    { "Aast", CAbilityAncestralSpirit, AB_SPELL, SPELL_TARGET_NONE },  /* Ancestral Spirit */
    { "Adch", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Disenchant */
    { "Acpf", CAbilityBanish, AB_SPELL },  /* Corporeal Form */
    { "Aetf", CAbilityBanish, AB_SPELL },  /* Ethereal Form */
    { "Aspl", CAbilitySpiritLink, AB_SPELL, SPELL_TARGET_UNIT },  /* Spirit Link */
    { "Aliq", CAbilityLiquidFire, AB_PASSIVE },  /* Liquid Fire */
    { "Auco", CAbilityUnstableConcoction, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Unstable Concoction */
    { "Acha", CAbilityChaos, AB_PASSIVE },  /* Chaos */
    { "Achl", CAbilityCargoLoad, AB_COMMAND },  /* Chaos Cargo Load */
    { "Awar", CAbilityPulverize, AB_PASSIVE },  /* Pulverize */
    { "Aens", CAbilityEnsnare, AB_SPELL | AB_UPDATE, SPELL_TARGET_UNIT },  /* Ensnare */
    { "Adev", CAbilityCannibalize, AB_SPELL | AB_CHANNEL },  /* Devour */
    { "Aprg", CAbilityPurge, AB_SPELL, SPELL_TARGET_UNIT },  /* Purge */
    { "Apg2", CAbilityPurge, AB_SPELL, SPELL_TARGET_UNIT },  /* Purge (TFT melee; DataD/DataE pause) */
    { "Alsh", CAbilityLightningShield, AB_SPELL, SPELL_TARGET_UNIT },  /* Lightning Shield */
    { "Aeye", CAbilityEvilEye, AB_SPELL, SPELL_TARGET_POINT },  /* Sentry Ward */
    { "Asta", CAbilityStasisTrap, AB_SPELL, SPELL_TARGET_POINT },  /* Stasis Trap */
    { "Ahwd", CAbilityHealingWard, AB_SPELL, SPELL_TARGET_POINT },  /* Healing Ward */
    { "Aoar", CAbilityAuraRegenLife, AB_PASSIVE | AB_INNATE },  /* Healing Ward Aura */
    { "Aven", CAbilityPoisonAttack, AB_PASSIVE },  /* Envenomed Spears */
    { "Apoi", CAbilityPoisonAttack, AB_PASSIVE },  /* Poison Sting */
    { "Apo2", CAbilityPoisonAttack, AB_PASSIVE },  /* Orb of Venom (Poison Attack) */
    { "Aspi", CAbilitySpiked, AB_PASSIVE },  /* Spiked Barricades */
    { "Asal", CAbilitySalvage, AB_PASSIVE },  /* Pillage */
    { "Aakb", CAbilityWarDrums, AB_PASSIVE },  /* War Drums */

    /* UndeadAbilityStrings.txt */
    { "Arpb", CAbilityReplenish, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Replenish */
    { "Arpl", CAbilityReplenishLife, AB_SPELL | AB_AUTOCAST },  /* Essence of Blight */
    { "Arpm", CAbilityReplenishMana, AB_SPELL | AB_AUTOCAST },  /* Spirit Touch */
    { "Aexh", CAbilityExhumeCorpses, AB_PASSIVE | AB_UPDATE },  /* Exhume Corpses */
    { "Aave", CAbilityMetamorphosis, AB_SPELL },  /* Destroyer Form */
    { "Afak", CAbilityFlamingArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Orb of Annihilation */
    { "Advm", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Devour Magic */
    { "Aabr", CAbilityAuraRegenLife, AB_PASSIVE | AB_INNATE },  /* Aura of Blight */
    { "Aabs", CAbilityAbsorb, AB_PASSIVE },  /* Absorb Mana */
    { "Abur", CAbilityCreepSleep, AB_PASSIVE | AB_INNATE },  /* Burrow */
    { "Amtc", CAbilityCargoHold, AB_PASSIVE },  /* Cargo Hold */
    { "Atru", CAbilityTrueSight, AB_PASSIVE },  /* True Sight */
    { "Auns", CAbilityUnsummon, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Unsummon Building */
    { "Agyd", CAbilityGraveyard, AB_PASSIVE | AB_UPDATE },  /* Create Corpse */
    { "Alam", CAbilitySacrifice, AB_SPELL, SPELL_TARGET_UNIT },  /* Sacrifice (Acolyte) */
    { "Asac", CAbilitySacrifice, AB_SPELL, SPELL_TARGET_UNIT },  /* Sacrifice (Sacrificial Pit) */
    { "Acan", CAbilityCannibalize, AB_SPELL | AB_CHANNEL },  /* Cannibalize */
    { "Aspa", CAbilitySpiderAttack, AB_PASSIVE },  /* Spider Attack */
    { "Aweb", CAbilityWeb, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Web */
    { "Astn", CAbilityStoneForm, AB_SPELL, SPELL_TARGET_NONE, stone_form_orders },  /* Stone Form */
    { "Amel", CAbilityCargoLoad, AB_COMMAND | AB_AUTOCAST },  /* Get Corpse */
    { "Amed", CAbilityCargoDrop, AB_COMMAND },  /* Drop Corpse */
    { "Aapl", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE },  /* Disease Cloud */
    { "Apts", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE },  /* Disease Cloud */
    { "Afrb", CAbilityFrostNova, AB_SPELL, SPELL_TARGET_UNIT },  /* Frost Breath */
    { "Afra", CAbilityColdArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Frost Attack */
    { "Afrz", CAbilityFrostNova, AB_SPELL, SPELL_TARGET_UNIT },  /* Freezing Breath */
    { "Arai", CAbilityRaiseDead, AB_SPELL | AB_AUTOCAST },  /* Raise Dead */
    { "Auhf", CAbilityUnholyFrenzy, AB_SPELL, SPELL_TARGET_UNIT },  /* Unholy Frenzy */
    { "Acrs", CAbilityCurse, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Curse */
    { "Aams", CAbilityAntiMagicShell, AB_SPELL, SPELL_TARGET_UNIT },  /* Anti-magic Shell */
    { "Aam2", CAbilityAntiMagicShell, AB_SPELL, SPELL_TARGET_UNIT },  /* Anti-magic Shell (Magic Resistance) */
    { "Apos", CAbilityPossession, AB_SPELL, SPELL_TARGET_UNIT },  /* Possession */
    { "Aps2", CAbilityPossessionTwo, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Possession (Channeling) */
    { "Acri", CAbilityCripple, AB_SPELL, SPELL_TARGET_UNIT },  /* Cripple */

    /* No AbilityStrings source file */
    // TODO: AIgl a_unknown  /* FortificationGlyph — CAbility [ITEM] other */
    // TODO: AIrg a_unknown  /* Potion of Life Regen — CAbility [ITEM] other */
    { "ANsu", CAbilitySubmergeMyrmidon, AB_SPELL | AB_TOGGLE },  /* Submerge (Myrmidon) */
    { "AOwd", CAbilitySerpentWard, AB_SPELL, SPELL_TARGET_POINT },  /* Shadow Hunter - Serpent Ward */
    { "Aimp", CAbilityImpale, AB_SPELL, SPELL_TARGET_POINT },  /* Impaling Bolt */
    { "Ansp", CAbilityNeutralSpell, AB_PASSIVE },  /* Neutral Spies */

    /* Extra AbilityData aliases of registered parents; poll code= before mapping. */
    { "ACac", CAbilityCommandAura, AB_PASSIVE },  /* Aura - Command (Creep) */
    { "ACah", CAbilityCreepAura, AB_PASSIVE },  /* Thorns Aura (creep) */
    { "ACam", CAbilityAntiMagicShell, AB_SPELL, SPELL_TARGET_UNIT },  /* Anti-magic Shield (creep) */
    { "ACps", CAbilityPossession, AB_SPELL, SPELL_TARGET_UNIT },  /* Possession (creep) */
    { "ACat", CAbilityCreepAura, AB_PASSIVE },  /* Aura - Trueshot (Creep) */
    { "ACav", CAbilityCreepAura, AB_PASSIVE },  /* Aura - Devotion (Creep) */
    { "ACba", CAbilityCreepAura, AB_PASSIVE },  /* Aura - Brilliance (creep) */
    { "ACbb", CAbilityBloodlust, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Bloodlust (creep, Hotkey B) */
    { "ACbc", CAbilityBreathOfFire, AB_SPELL, SPELL_TARGET_POINT },  /* Breath of Fire (Creep) */
    { "ACbh", CAbilityBash, AB_PASSIVE },  /* Bash (creep) */
    { "ACbk", CAbilityBlackArrow, AB_SPELL | AB_TOGGLE | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Black Arrow (melee, creep) */
    { "ACbl", CAbilityBloodlust, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Bloodlust (Creep) */
    { "ACbn", CAbilityBanish, AB_SPELL, SPELL_TARGET_UNIT },  /* Banish (Creep) */
    { "ACbz", CAbilityBlizzard, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Blizzard (creep) */
    { "ACcb", CAbilityThunderBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Frost Bolt */
    { "ACce", CAbilityCleavingAttack, AB_PASSIVE },  /* Cleaving Attack (Creep) */
    { "ACch", CAbilityCharm, AB_SPELL, SPELL_TARGET_UNIT },  /* Charm */
    { "ACcl", CAbilityChainLightning, AB_SPELL, SPELL_TARGET_UNIT },  /* Chain Lightning (creep) */
    { "ACcn", CAbilityCannibalize, AB_SPELL | AB_CHANNEL },  /* Cannibalize (creep) */
    { "ACcr", CAbilityCripple, AB_SPELL, SPELL_TARGET_UNIT },  /* Cripple (creep) */
    { "ACcs", CAbilityCurse, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Curse (creep) */
    { "ACct", CAbilityCreepAura, AB_PASSIVE },  /* Critical Strike (creep) */
    { "ACcw", CAbilityColdArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Cold Arrows (creep) */
    { "ACd2", CAbilityAbolishMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Abolish Magic (Creep, 1,2 pos) */
    { "ACdc", CAbilityDeathCoil, AB_SPELL, SPELL_TARGET_UNIT },  /* Death Coil (creep) */
    { "ACde", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Devour Magic (creep) */
    { "ACdm", CAbilityAbolishMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Abolish Magic (Creep) */
    { "ACdr", CAbilityDrainNeutral, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Drain Life (Creep) */
    { "ACds", CAbilityDivineShield, AB_SPELL },  /* Divine Shield (creep) */
    { "ACen", CAbilityEnsnare, AB_SPELL | AB_UPDATE, SPELL_TARGET_UNIT },  /* Ensnare (Creep) */
    { "ACes", CAbilityCreepAura, AB_PASSIVE },  /* Evasion (creep 100%) */
    { "ACev", CAbilityCreepAura, AB_PASSIVE },  /* Evasion (creep) */
    { "ACf2", CAbilityFrostArmor, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Frost Armor (creep, autocast) */
    { "ACf3", CAbilityFingerOfDeath, AB_SPELL, SPELL_TARGET_UNIT },  /* Finger of Pain (2,1 Button) */
    { "ACfa", CAbilityFrostArmor, AB_SPELL, SPELL_TARGET_UNIT },  /* Frost Armor (creep, old) */
    { "ACfb", CAbilityFireBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Fire Bolt (creep) */
    { "ACfd", CAbilityFingerOfDeath, AB_SPELL, SPELL_TARGET_UNIT },  /* Finger of Pain */
    { "ACff", CAbilityFaerieFire, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Faerie Fire (creep) */
    { "ACfl", CAbilityForkedLightning, AB_SPELL, SPELL_TARGET_UNIT },  /* Forked Lightning (creep) */
    { "ACfn", CAbilityFrostNova, AB_SPELL, SPELL_TARGET_UNIT },  /* Frost Nova (creep) */
    { "ACfr", CAbilityForceOfNature, AB_SPELL },  /* Force of Nature (creep) */
    { "ACfs", CAbilityFlameStrikeNeutral, AB_SPELL, SPELL_TARGET_POINT },  /* Flame Strike (Creep) */
    { "AChv", CAbilityHealingWave, AB_SPELL, SPELL_TARGET_UNIT },  /* Healing Wave (Creep) */
    { "AChw", CAbilityHealingWard, AB_SPELL, SPELL_TARGET_POINT },  /* Healing Ward (creep) */
    { "AChx", CAbilityHex, AB_SPELL, SPELL_TARGET_UNIT },  /* Hex (Creep) */
    { "ACif", CAbilityInnerFire, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Inner Fire (Creep) */
    { "ACim", CAbilityImmolation, AB_SPELL | AB_TOGGLE },  /* Immolation (creep) */
    { "ACls", CAbilityLightningShield, AB_SPELL, SPELL_TARGET_UNIT },  /* Lightning Shield (creep) */
    { "ACm2", CAbilityMagicImmunity, AB_PASSIVE },  /* Magic Immunity (Archimonde) */
    { "ACm3", CAbilityMagicImmunity, AB_PASSIVE },  /* Magic Immunity (Dragons) */
    { "ACmi", CAbilityCreepAura, AB_PASSIVE },  /* Magic Immunity (Creep) */
    { "ACmo", CAbilityMonsoon, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Monsoon (creep) */
    { "ACmp", CAbilityImpale, AB_SPELL, SPELL_TARGET_POINT },  /* Impale (Creep) */
    { "ACnr", CAbilityAuraRegenLife, AB_PASSIVE },  /* Neutral Regen (health only) */
    { "ACpa", CAbilityParasiteCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Parasite (eredar) */
    { "ACpu", CAbilityPurge, AB_SPELL, SPELL_TARGET_UNIT },  /* Purge (Creep) */
    { "ACpv", CAbilityPulverize, AB_PASSIVE },  /* Pulverize (Sea Giant) */
    { "ACr1", CAbilityRoar, AB_SPELL },  /* Roar (Skeletal Orc) */
    { "ACr2", CAbilityRejuvination, AB_SPELL, SPELL_TARGET_UNIT },  /* Rejuvenation (Furbolg) */
    { "ACrd", CAbilityRaiseDead, AB_SPELL | AB_AUTOCAST },  /* Raise Dead (Creep) */
    { "ACrf", CAbilityRainOfFire, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Rain of Fire (creep) */
    { "ACrg", CAbilityRainOfFire, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Rain of Fire (creep, greater) */
    { "ACrj", CAbilityRejuvination, AB_SPELL, SPELL_TARGET_UNIT },  /* Rejuvenation (creep) */
    { "ACrk", CAbilityResistantSkin, AB_PASSIVE },  /* Resistant Skin (creep) */
    { "ACro", CAbilityRoar, AB_SPELL },  /* Roar (creep) */
    { "ACs9", CAbilitySpiritWolf, AB_SPELL },  /* Feral Spirit (creep - pig) */
    { "ACsf", CAbilitySpiritWolf, AB_SPELL },  /* Feral Spirit (creep) */
    { "ACsh", CAbilityShockwave, AB_SPELL, SPELL_TARGET_POINT },  /* Shockwave (Creep) */
    { "ACsi", CAbilitySilence, AB_SPELL, SPELL_TARGET_POINT },  /* Silence (Creep) */
    { "ACsk", CAbilityResistantSkin, AB_PASSIVE },  /* Resistant Skin (3,1 pos, creep) */
    { "ACsm", CAbilityDrain, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Siphon Mana (Creep) */
    { "ACss", CAbilityShadowStrike, AB_SPELL, SPELL_TARGET_UNIT },  /* Shadow Strike (Creep) */
    { "ACst", CAbilityShockwave, AB_SPELL, SPELL_TARGET_POINT },  /* Shockwave (Trap) */
    { "ACsw", CAbilitySlow, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Slow (Creep) */
    { "ACt2", CAbilityThunderClap, AB_SPELL },  /* Thunder Clap (Thunder Lizard) */
    { "ACua", CAbilityCreepAura, AB_PASSIVE },  /* Unholy Aura (creep) */
    { "ACuf", CAbilityUnholyFrenzy, AB_SPELL, SPELL_TARGET_UNIT },  /* Unholy Frenzy (creep) */
    { "ACvp", CAbilityCreepAura, AB_PASSIVE },  /* Vampiric Aura (creep) */
    { "ACvs", CAbilityPoisonAttack, AB_PASSIVE },  /* Venom Spears (Creep) */
    { "ACwb", CAbilityWeb, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Web (creep) */
    { "ACwe", CAbilityWaterElemental, AB_SPELL },  /* Summon Sea Elemental */
    { "AHta", CAbilityFarSight, AB_SPELL, SPELL_TARGET_POINT },  /* Reveal (Arcane Tower) */
    { "ANak", CAbilityOrbAnnihilation, AB_PASSIVE },  /* Orb of Annihilation (Quill Spray) */
    { "ANb2", CAbilityBash, AB_PASSIVE },  /* Bash (maul, SP Bear, level 3) */
    { "ANbh", CAbilityBash, AB_PASSIVE },  /* Bash (Beastmaster Bear) */
    { "ANbl", CAbilityBlink, AB_SPELL, SPELL_TARGET_POINT },  /* Blink (Beastmaster Bear) */
    { "ANfa", CAbilityColdArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Sea Witch - Frost Arrows */
    { "ANre", CAbilityAuraRegenMana, AB_PASSIVE },  /* Neutral Regen (mana only) */
    { "ANrn", CAbilityReincarnation, AB_PASSIVE },  /* Mannoroth - Reincarnation */
    { "ANta", CAbilityTaunt, AB_SPELL },  /* Taunt (Creep) */
    { "ANtr", CAbilityTrueSight, AB_PASSIVE },  /* Detect (War Eagle) */
    { "ANwk", CAbilityWindWalk, AB_SPELL },  /* Wind Walk */
    { "Aap1", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE },  /* Aura - Plague (Abomination) */
    { "Aap2", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE },  /* Aura - Plague (Plague Ward) */
    { "Aap3", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE },  /* Aura - Plague (Creep) */
    { "Aap4", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE },  /* Aura - Plague (Creep gfx) */
    { "SCae", CAbilityCreepAura, AB_PASSIVE },  /* Aura - Endurance (Creep) */
    { "SCva", CAbilityAuraRegenLife, AB_PASSIVE },  /* Vampiric attack */
    { "Adsm", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Dispel Magic (creep) */
    { "Adcn", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Disenchant (new) */
    { "Ache", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Chain Dispel */
    { "Acht", CAbilityHowlOfTerror, AB_SPELL },  /* Howl of Terror */
    { "Acn2", CAbilityCannibalize, AB_SPELL | AB_CHANNEL },  /* Cannibalize (Abomination) */
    { "Acdb", CAbilityDrunkenBrawler, AB_PASSIVE },  /* Chen - Drunken Brawler */
    { "Aco2", CAbilityCoupleInstant, AB_COMMAND },  /* Couple Instant (Archer) */
    { "Aco3", CAbilityCoupleInstant, AB_COMMAND },  /* Couple Instant (Hippogryph) */
    { "Adt1", CAbilityDetector, AB_PASSIVE },  /* Detect (Sentry Ward) */
    { "Adtg", CAbilityTrueSight, AB_PASSIVE },  /* Detect (general) */
    { "Afa2", CAbilityFaerieFire, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Faerie Fire */
    { "Afbt", CAbilityFeedback, AB_PASSIVE },  /* Feedback (Arcane Tower) */
    { "Anh1", CAbilityHeal, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Heal (Creep Normal) */
    { "Anh2", CAbilityHeal, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Heal (Creep High) */
    { "Ansk", CAbilityHardenedSkin, AB_PASSIVE },  /* Hardened Skin (Naga Turtle) */
    { "Aihn", CAbilityInventory, AB_PASSIVE },  /* Inventory (2 slot unit) Human */
    { "Aion", CAbilityInventory, AB_PASSIVE },  /* Inventory (2 slot unit) Orc */
    { "Aiun", CAbilityInventory, AB_PASSIVE },  /* Inventory (2 slot unit) Undead */
    { "Aien", CAbilityInventory, AB_PASSIVE },  /* Inventory (2 slot unit) Night Elf */
    { "Apak", CAbilityInventory, AB_PASSIVE },  /* Inventory (Pack Mule) */
    { "Amb2", CAbilityManaBattery, AB_SPELL, SPELL_TARGET_UNIT },  /* Mana Battery (Obsidian Statue) */
    { "Ambb", CAbilityManaBurn, AB_SPELL, SPELL_TARGET_UNIT },  /* Mana Burn (Hotkey B) */
    { "Ambd", CAbilityManaBurn, AB_SPELL, SPELL_TARGET_UNIT },  /* Mana Burn (demon) */
    { "Amnb", CAbilityManaBurn, AB_SPELL, SPELL_TARGET_UNIT },  /* Mana Burn (demon) */
    { "Ara2", CAbilityRoar, AB_SPELL },  /* Roar */
    { "Argd", CAbilityReturn, AB_COMMAND },  /* Return (Gold) */
    { "Argl", CAbilityReturn, AB_COMMAND },  /* Return (Gold & Lumber) */
    { "Arlm", CAbilityReturn, AB_COMMAND },  /* Return (Lumber) */
    { "Aro1", CAbilityRoot, AB_COMMAND },  /* Root (Ancients) */
    { "Aro2", CAbilityRoot, AB_COMMAND },  /* Root (Ancient Protector) */
    { "Awfb", CAbilityFireBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Fire Bolt (warlock) */
    { "Awrg", CAbilityStomp, AB_SPELL },  /* War Stomp (sea giant) */
    { "Awrh", CAbilityStomp, AB_SPELL },  /* War Stomp (hydra) */
    { "Awrs", CAbilityStomp, AB_SPELL },  /* War Stomp (creep) */
    { "Sca1", CAbilityPassive, AB_PASSIVE },  /* Chaos (Grunt) */
    { "Sca2", CAbilityPassive, AB_PASSIVE },  /* Chaos (Raider) */
    { "Sca3", CAbilityPassive, AB_PASSIVE },  /* Chaos (Shaman) */
    { "Sca4", CAbilityPassive, AB_PASSIVE },  /* Chaos (Kodo) */
    { "Sca5", CAbilityPassive, AB_PASSIVE },  /* Chaos (Peon) */
    { "Sca6", CAbilityPassive, AB_PASSIVE },  /* Chaos (Grom) */
    { "Sch2", CAbilityCargoHold, AB_PASSIVE },  /* Cargo Hold (Meat Wagon) */
    { "Sch3", CAbilityCargoHold, AB_PASSIVE },  /* Cargo Hold (Transport) */
    { "Sch4", CAbilityCargoHold, AB_PASSIVE },  /* Cargo Hold (Tank) */
    { "Sch5", CAbilityCargoHold, AB_PASSIVE },  /* Cargo Hold (Ship) */
    { "Scri", CAbilityCripple, AB_SPELL, SPELL_TARGET_UNIT },  /* Cripple (Warlock) */
    { "Sdro", CAbilityCargoDrop, AB_COMMAND },  /* Drop */
    { "Slo2", CAbilityCargoLoad, AB_COMMAND },  /* Load (Entangled Gold Mine) */
    { "Slo3", CAbilityCargoLoad, AB_COMMAND },  /* Load (Navies) */
    { "Sloa", CAbilityCargoLoad, AB_COMMAND },  /* Load (Burrow) */
    { "Stpm", CAbilityCargoLoad, AB_COMMAND },  /* Pilot Tank (Mortar Team) */
    { "Stpr", CAbilityCargoLoad, AB_COMMAND },  /* Pilot Tank (Rifleman) */
    { "Suhf", CAbilityUnholyFrenzy, AB_SPELL, SPELL_TARGET_UNIT },  /* Unholy Frenzy (Warlock) */
    { "Sbtl", CAbilityBattlestations, AB_COMMAND },  /* Battlestations (Chaos) */
    { "Sbsk", CAbilityPassive, AB_PASSIVE },  /* Berserker Upgrade */
    { "AIcy", CAbilityCyclone, AB_SPELL, SPELL_TARGET_UNIT },  /* Item Cyclone */
    { "AIsw", CAbilityEvilEye, AB_SPELL, SPELL_TARGET_POINT },  /* Sentry Ward (item; code=Aeye) */
    { "AIxs", CAbilityAntiMagicShellInstant, AB_SPELL, SPELL_TARGET_UNIT },  /* Item Anti-magic Shield */
    { "Amgr", CAbilityMoonGlaive, AB_PASSIVE | AB_INNATE },  /* Moon Glaive (Naisha) */
    { "Asds", CAbilitySelfDestruct, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_POINT },  /* Kaboom! */
    { "Asdg", CAbilitySelfDestruct, AB_PASSIVE },  /* Self Destruct (Clockwerk Goblins) */
    { "Asd2", CAbilitySelfDestruct, AB_PASSIVE },  /* Self Destruct 2 (Clockwerk Goblins) */
    { "Asd3", CAbilitySelfDestruct, AB_PASSIVE },  /* Self Destruct 3 (Clockwerk Goblins) */
    { "Srtt", CAbilityPassive, AB_PASSIVE },  /* Tank Upgrade */
    /* END GENERATED TODO ABILITIES */

    /* Passive regeneration base codes remain explicit outside generated TODOs. */
    { "Abar", CAbilityBarkskin, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT, barkskin_orders },  /* Barkskin */
    { "Aarm", CAbilityAuraRegenMana, AB_PASSIVE },  /* Mana Regeneration Aura */
};

/* Build a compact unique procedure list once, rather than scan the whole registry per unit tick. */
static abilityProc_t ability_updates[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t num_updates;
static abilityitem_t innate_items[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t num_innate;
static abilityProc_t ability_index_procs[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t ability_index_values[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t num_ability_index_procs;

/* ROC/TFT physical data columns are normalized by the AbilityData DDX schema. */
float AB_Data(cstring_t classname, uint32_t level, uint32_t index) {
    abilityLevel_t const *row = G_AbilityLevel(FS_SLKKey(classname), level);
    index = MAX(1, MIN(index, 9));
    return row->data[index - 1].number;
}

uint32_t AB_DataId(cstring_t classname, uint32_t level, uint32_t index) {
    abilityLevel_t const *row = G_AbilityLevel(FS_SLKKey(classname), level);
    index = MAX(1, MIN(index, 9));
    return row->data[index - 1].id;
}

/* Order names belong to their ability, including orders for preplaced alternate forms. */
ability_t const *FindAbilityByOrder(cstring_t order) {
    if (!order) return NULL;
    FOR_LOOP(i, game.num_abilities) {
        ability_t const *ability = abilitylist + i;
        if (!ability->orders || !ability->proc) continue;
        for (cstring_t const *name = ability->orders; *name; name++)
            if (!strcmp(*name, order)) return ability;
    }
    return NULL;
}

/* Persistent effects can outlive their active order; the callback owns its per-unit state checks. */
void S_RunAbilityUpdates(LPEDICT ent) {
    FOR_LOOP(i, num_updates)
        ability_updates[i](ent, A_UPDATE, NULL);
    S_UpdateUnitPassiveEffects(ent);
}

/* Unit-data abilities exist independently of command-card slots. Notifications visit every owner;
 * idle and acquisition queries stop when an owner consumes the decision. */
bool S_UnitAbilityEvent(LPEDICT ent, abilityMsg_t msg) {
    bool handled = false;
    if (msg == A_MOVE_LEAVE && ent && ent->channel.code) {
        abilityitem_t item = S_AbilityItem(ent->channel.code);
        abilityCall_t call = MAKE(abilityCall_t, .item = &item);
        handled = S_AbilityMessage(ent, msg, &call) != 0;
    }
    FOR_LOOP(i, num_innate) {
        abilityCall_t call = MAKE(abilityCall_t, .item = innate_items + i);
        handled |= S_AbilityMessage(ent, msg, &call) != 0;
        if (handled && (msg == A_IDLE || msg == A_NO_ACQUIRE)) break;
    }
    return handled;
}

/* Accepted instant/spell orders can leave the current movement object untouched.
 * Give innate behavior owners one generic post-accept hook so they can retire
 * their own state without putting ability names in m_unit.c. */
bool S_UnitAbilityOrderAccepted(LPEDICT ent, cstring_t order) {
    bool handled = false;
    if (!ent || !order) return false;
    FOR_LOOP(i, num_innate) {
        abilityCall_t call = MAKE(abilityCall_t, .item = innate_items + i, .order = order);
        handled |= S_AbilityMessage(ent, A_ORDER_ACCEPTED, &call) != 0;
    }
    return handled;
}

/* Queued work is returned to the procedure that owns its order. The stored
 * order_id remains the concrete rawcode payload for that ability. */
bool S_UnitQueuedOrderEvent(LPEDICT ent, unitOrder_t const *queued, abilityMsg_t msg) {
    ability_t const *ability;
    abilityitem_t item;
    abilityCall_t call;

    if (!ent || !queued || (msg != A_QUEUE_ORDER_START && msg != A_QUEUE_ORDER_CANCEL)) return false;
    ability = FindAbilityByOrder(queued->order);
    if (!ability || !ability->proc) return false;
    item = MAKE(abilityitem_t, .code = queued->order_id, .ability = ability);
    call = MAKE(abilityCall_t, .item = &item, .queued_order = queued);
    return S_AbilityMessage(ent, msg, &call) != 0;
}

static bool unit_target_ability_try(LPEDICT target, LPEDICT issuer, cstring_t order, uint32_t code,
                                    uint32_t *seen, uint32_t *seen_count, uint32_t seen_capacity) {
    abilityitem_t item;
    abilityCall_t call;
    if (!target || !issuer || !order || !code || !seen || !seen_count) return false;
    FOR_LOOP(i, *seen_count) if (seen[i] == code) return false;
    if (*seen_count < seen_capacity) seen[(*seen_count)++] = code;
    if (!G_UnitAbilityLevel(target, code)) return false;
    item = S_AbilityItem(code);
    if (!item.ability) return false;
    call = MAKE(abilityCall_t, .item = &item, .target_order = { issuer, order });
    return S_AbilityMessage(target, A_TARGET_ORDER, &call) != 0;
}

/* Generic target-owned interaction dispatch. The target's concrete authored
 * rawcode is retained so derived AbilityData aliases can consume their own data. */
bool S_UnitTargetAbilityOrder(LPEDICT target, LPEDICT issuer, cstring_t order) {
    uint32_t seen[MAX_ABILITIES * 2 + MAX_HERO_ABILITIES] = {0};
    uint32_t seen_count = 0;
    uint32_t const seen_capacity = sizeof(seen) / sizeof(*seen);
    char const *abilities;

    if (!target || !target->inuse || !issuer || !order) return false;
    abilities = target->data.UnitAbilities ? target->data.UnitAbilities->abilList : NULL;
    if (abilities) {
        PARSE_LIST(abilities, token, parse_segment) {
            uint32_t code = 0;
            if (strlen(token) != 4) continue;
            memcpy(&code, token, 4);
            if (unit_target_ability_try(target, issuer, order, code, seen, &seen_count, seen_capacity))
                return true;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(target->abilities.added)) {
        uint32_t const code = target->abilities.added[i];
        if (unit_target_ability_try(target, issuer, order, code, seen, &seen_count, seen_capacity))
            return true;
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        uint32_t const code = target->heroabilities[i].level ? target->heroabilities[i].code : 0;
        if (unit_target_ability_try(target, issuer, order, code, seen, &seen_count, seen_capacity))
            return true;
    }
    return false;
}

/* Dispatch projectile impact to the target's authored abilities before damage is applied. */
bool S_UnitProjectileHit(LPEDICT projectile) {
    LPEDICT target = projectile ? projectile->goalentity : NULL;
    if (!target || !target->inuse) return false;
    FOR_LOOP(i, game.num_abilities) {
        ability_t const *ability = abilitylist + i;
        abilityitem_t item;
        abilityCall_t call;
        uint32_t code;
        if (!ability->classname || strlen(ability->classname) != 4) continue;
        code = FS_SLKKey(ability->classname);
        if (!G_UnitAbilityLevel(target, code)) continue;
        item = MAKE(abilityitem_t, .code = code, .ability = ability);
        call = MAKE(abilityCall_t, .item = &item, .projectile = projectile);
        if (S_AbilityMessage(target, A_PROJECTILE_HIT, &call)) return true;
    }
    return false;
}

ability_t const *FindAbilityByClassname(cstring_t classname) {
    FOR_LOOP(i, game.num_abilities) {
        if (!abilitylist[i].classname)
            continue;
        if (!strcmp(abilitylist[i].classname, classname))
            return abilitylist + i;
    }
    return NULL;
}

/* Command-card names use two namespaces. Engine commands (CmdBuild, CmdMove,
 * etc.) are full strings registered directly in abilitylist. WC3 abilities are
 * four-character rawcodes whose AbilityData alias may point at a base handler.
 * Only rawcodes belong in the SLK resolver: passing CmdBuild through FS_SLKKey
 * truncates it to CmdB and loses the registered build command. */
ability_t const *FindAbilityForCommand(cstring_t classname) {
    if (!classname || !*classname) {
        return NULL;
    }
    if (strlen(classname) != 4) {
        return FindAbilityByClassname(classname);
    }
    return FindAbilityByClassname(GetClassName(G_AbilityCodeName(classname)));
}

/* Keep the requested rawcode even when AbilityData resolves its code to a shared implementation. */
abilityitem_t S_AbilityItem(uint32_t code) {
    return MAKE(abilityitem_t, .code = code, .ability = code ? FindAbilityForCommand(GetClassName(code)) : NULL);
}

/* Dispatch is synchronous and retains the concrete row and authored rawcode in the typed payload. */
BZ_ABILITY_PROC(S_AbilityMessage) {
    ability_t const *ability = call && call->item ? call->item->ability : NULL;
    return ability && ability->proc ? ability->proc(ent, msg, call) : false;
}

void S_EnableAbility(LPEDICT ent, uint32_t code) {
    abilityitem_t item = S_AbilityItem(code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    if (item.ability) S_AbilityMessage(ent, A_ENABLE, &call);
}

void S_DisableAbility(LPEDICT ent, uint32_t code) {
    if (ent && ent->autocast_code == code) G_SetUnitAutocast(ent, code, false);
    abilityitem_t item = S_AbilityItem(code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    if (item.ability) S_AbilityMessage(ent, A_DISABLE, &call);
}

void S_RefreshAbilityLevel(LPEDICT ent, ability_t const *ability) {
    abilityitem_t item = MAKE(abilityitem_t, .ability = ability);
    abilityCall_t query = MAKE(abilityCall_t, .item = &item);
    abilityCall_t changed;
    if (!ability || !ability->proc) return;
    changed = MAKE(abilityCall_t, .item = &item, .level = (uint32_t)S_AbilityMessage(ent, A_LEVEL, &query));
    S_AbilityMessage(ent, A_LEVEL_CHANGED, &changed);
}

/* The selected rawcode is shared state; each procedure owns its autocast policy and side effects. */
bool G_UnitAutocastIsOn(LPEDICT ent, uint32_t code) {
    abilityitem_t item = S_AbilityItem(code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    return ent && code && ent->autocast_code == code && item.ability && (item.ability->flags & AB_AUTOCAST) &&
        G_UnitAbilityLevel(ent, code) && S_AbilityMessage(ent, A_AUTOCAST_ON, &call);
}

/* Keeping the alias through the UI and scheduler preserves authored cost, range and effect data. */
bool G_SetUnitAutocast(LPEDICT ent, uint32_t code, bool enabled) {
    abilityitem_t item = S_AbilityItem(code), old;
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .enabled = enabled);
    if (!ent || !item.ability || !(item.ability->flags & AB_AUTOCAST) ||
        (enabled && !G_UnitAbilityLevel(ent, code))) return false;
    old = S_AbilityItem(ent->autocast_code);
    if (!enabled && old.code != code) return true;
    abilityCall_t prev = MAKE(abilityCall_t, .item = &old, .enabled = false);
    bool switched = enabled && old.code && old.code != code;
    /* Distinct procedures may share policy state (the two Repair families). Retire it before enabling the next. */
    if (switched) S_AbilityMessage(ent, A_AUTOCAST_SET, &prev);
    if (!S_AbilityMessage(ent, A_AUTOCAST_SET, &call)) {
        prev.enabled = true;
        if (switched) S_AbilityMessage(ent, A_AUTOCAST_SET, &prev);
        return false;
    }
    if (enabled) {
        ent->autocast_code = code;
        ent->aiflags |= AI_AUTOCAST_ACTIVE;
    } else if (old.code == code) {
        ent->autocast_code = 0;
        ent->aiflags &= ~AI_AUTOCAST_ACTIVE;
    }
    return true;
}

/* Dispatch the selected alias directly, including runtime-added abilities absent from UnitAbilities. */
bool G_TryUnitAutocast(LPEDICT ent) {
    if (!ent || !(ent->aiflags & AI_AUTOCAST_ACTIVE)) return false;
    abilityitem_t item = S_AbilityItem(ent->autocast_code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    return G_UnitAutocastIsOn(ent, item.code) && S_AbilityMessage(ent, A_AUTOCAST_ACQUIRE, &call);
}

uint32_t FindAbilityIndex(cstring_t classname) {
    FOR_LOOP(i, game.num_abilities) {
        if (!abilitylist[i].classname)
            continue;
        if (!strcmp(abilitylist[i].classname, classname))
            return i;
    }
    return 255;
}

/* Shared casts and bespoke commands expose the same capability to HUD and item callers. */
bool S_AbilityHasCommand(ability_t const *ability) {
    return ability && ability->proc && (ability->flags & (AB_SPELL | AB_COMMAND));
}

/* The shared-cast bit owns dispatch; procedures can override command handling
 * and delegate the message to CAbilitySimpleSpell when it is not specialized. */
void S_AbilityCommand(LPEDICT clent, ability_t const *ability) {
    abilityitem_t item;
    abilityCall_t call;

    if (!S_AbilityHasCommand(ability)) return;
    item = MAKE(abilityitem_t, .code = clent->client->menu.ability_code, .ability = ability);
    call = MAKE(abilityCall_t, .item = &item, .client = clent);
    S_AbilityMessage(G_GetMainSelectedUnit(clent->client), A_COMMAND, &call);
}

void InitAbilities(void) {
    game.num_abilities = sizeof(abilitylist)/sizeof(abilitylist[0]);
    num_updates = 0;
    num_innate = 0;
    num_ability_index_procs = 0;
    FOR_LOOP(i, game.num_abilities) {
        ability_t *entry = &abilitylist[i];
        uint32_t n;
        abilityitem_t item = MAKE(abilityitem_t, .code = strlen(entry->classname) == 4 ? FS_SLKKey(entry->classname) : 0,
                                  .ability = entry);
        abilityCall_t call = MAKE(abilityCall_t, .item = &item, .classname = entry->classname);
        if (!entry->proc) gi.error("InitAbilities: %s has no procedure", entry->classname);
        entry->proc(NULL, A_INIT, &call);
        if (entry->flags & AB_INNATE) innate_items[num_innate++] = item;
        if (entry->flags & AB_UPDATE) {
            for (n = 0; n < num_updates && ability_updates[n] != entry->proc; n++) {}
            if (n == num_updates) ability_updates[num_updates++] = entry->proc;
        }
        for (n = 0; n < num_ability_index_procs && ability_index_procs[n] != entry->proc; n++) {}
        if (n == num_ability_index_procs) {
            ability_index_procs[num_ability_index_procs] = entry->proc;
            ability_index_values[num_ability_index_procs++] = i;
        }
    }
}

ability_t const *GetAbilityByIndex(uint32_t index) {
    if (index >= game.num_abilities)
        return NULL;
    return abilitylist + index;
}

uint32_t GetAbilityIndex(abilityProc_t proc) {
    if (!proc) return 255;
    FOR_LOOP(i, num_ability_index_procs)
        if (ability_index_procs[i] == proc) return ability_index_values[i];
    return 255;
}
