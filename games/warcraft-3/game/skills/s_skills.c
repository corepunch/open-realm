#include "s_skills.h"

#ifdef WC3_DEBUG_AUTOCAST
int G_AutocastDebugLevel(void) {
    cstring_t value;

    value = gi.CvarString("wc3_autocast_debug", "0");
    return value ? atoi(value) : 0;
}
#endif

cstring_t const raven_orders[] = { "ravenform", "unravenform", NULL };
cstring_t const ancient_root_orders[] = { "root", "unroot", NULL };
static cstring_t const mana_shield_orders[] = { "manashieldon", "manashieldoff", NULL };
static cstring_t const defend_orders[] = { "defend", "undefend", NULL };
static cstring_t const repair_orders[] = { "repair", "repairon", "repairoff", NULL };
static cstring_t const renew_orders[] = { "renew", "renewon", "renewoff", NULL };
static cstring_t const restoration_orders[] = { "restoration", "restorationon", "restorationoff", NULL };
static cstring_t const move_orders[] = { "move", "smart", NULL };
static cstring_t const patrol_orders[] = { "patrol", NULL };
static cstring_t const attack_orders[] = { "attack", "attackonce", NULL };
static cstring_t const build_orders[] = { "build", NULL };
static cstring_t const hide_orders[] = { "ambush", NULL };
static cstring_t const entangle_orders[] = {
    "entangle", "entangleinstant", "autoentangle", "autoentangleinstant", NULL
};

static ability_t abilitylist[] = {
    { STR_CmdStop, CAbilityStop, AB_COMMAND | AB_ENGINE_EVENTS | AB_QUEUEABLE },  // Stop command policy
    { STR_CmdMove, CAbilityMove, AB_COMMAND | AB_ENGINE_EVENTS },  // Move — engine command and target death policy
    { STR_CmdAttack, CAbilityAttack, AB_COMMAND | AB_ENGINE_EVENTS | AB_PRIMARY_TIMER, SPELL_TARGET_NONE, attack_orders },
    { "BTLF", CAbilityTimedLife, AB_ENGINE_EVENTS | AB_PRIMARY_TIMER },
    { STR_CmdAttackGround, CAbilityAttackGround, AB_COMMAND },  // Attack Ground — artillery engine command
    { STR_CmdBuild, CAbilityBuild, AB_COMMAND, SPELL_TARGET_NONE, build_orders },  // Build — engine command and queued-order owner
    { STR_CmdHoldPos, CAbilityHoldPosition, AB_COMMAND | AB_ENGINE_EVENTS | AB_QUEUEABLE },  // Hold command policy
    { STR_CmdPatrol, CAbilityPatrol, AB_COMMAND, .orders = patrol_orders },  // Patrol — engine command
    { STR_CmdRally, CAbilityRally, AB_COMMAND },  // Rally — engine command
    { STR_CmdCancel, CAbilityCancel, AB_COMMAND },  // Cancel — engine command
    { STR_CmdCancelBuild, CAbilityCancel, AB_COMMAND },  // Cancel Build — engine command
    { STR_CmdSelectSkill, CAbilitySelectSkill, AB_COMMAND },  // Select Skill — engine command
    { STR_CmdTrains, CAbilityTrain, 0 },  // Training — internal move identity

    { "Amrf", CAbilityRavenForm, AB_COMMAND | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_NONE, raven_orders },  /* Medivh Crow Form */
    { "Arav", CAbilityRavenForm, AB_COMMAND | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_NONE, raven_orders },  /* Storm Crow Form */

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
    { "Asb1", CAbilitySubmergeMyrmidon, AB_SPELL | AB_TOGGLE, SPELL_TARGET_NONE, submerge_orders },  /* Submerge */
    { "Asb2", CAbilitySubmergeRoyalGuard, AB_SPELL | AB_TOGGLE, SPELL_TARGET_NONE, submerge_orders },  /* Submerge */
    { "Asb3", CAbilitySubmergeSnapDragon, AB_SPELL | AB_TOGGLE, SPELL_TARGET_NONE, submerge_orders },  /* Submerge */
    { "ANha", CAbilityHarvest, AB_COMMAND },  /* Harvest */
    { "ANen", CAbilityEnsnare, AB_SPELL | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_UNIT },  /* Ensnare */
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
    { "Acdh", CAbilityDrunkenHazeCampaign, AB_SPELL | AB_STATUS_EVENTS | AB_STATUS_POLICY, SPELL_TARGET_UNIT },  /* Drunken Haze */
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
    { "Arep", CAbilityRepair, AB_COMMAND | AB_AUTOCAST, SPELL_TARGET_NONE, repair_orders },  /* Repair */
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
    { "Aatk", CAbilityAttack, AB_COMMAND | AB_INTRINSIC },  /* Attack */
    { "Afih", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire (Human) */
    { "Afin", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire (Night Elf) */
    { "Afio", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire (Orc) */
    { "Afir", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire */
    { "Afiu", CAbilityOnFireHuman, AB_PASSIVE },  /* On Fire (Undead) */
    { "Aloc", CAbilityPassive, AB_PASSIVE },  /* Locust */
    { "Amov", CAbilityMove, AB_COMMAND | AB_INNATE | AB_INTRINSIC | AB_TYPE_INIT | AB_OWNER_UPDATE | AB_PRIMARY_TIMER, SPELL_TARGET_NONE, move_orders },  /* Move */
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
    { "Aclf", CAbilityCloudOfFog, AB_SPELL | AB_STATUS_EVENTS | AB_STATUS_POLICY, SPELL_TARGET_UNIT },  /* Cloud */
    { "AHfs", CAbilityFlameStrike, AB_SPELL, SPELL_TARGET_POINT },  /* Flame Strike */
    { "AHbn", CAbilityBanish, AB_SPELL, SPELL_TARGET_UNIT },  /* Banish */
    { "AHpx", CAbilitySummonPhoenix, AB_SPELL },  /* Phoenix */
    { "Aphx", CAbilityPassive, AB_PASSIVE },  /* Phoenix Morphing (Egg Related) */
    { "Apxf", CAbilityPassive, AB_PASSIVE },  /* Phoenix Fire */
    { "Agyb", CAbilityPassive, AB_PASSIVE },  /* Flying Machine Bombs */
    { "Asth", CAbilityPassive, AB_PASSIVE },  /* Storm Hammers */
    { "Agyv", CAbilityTrueSight, AB_PASSIVE },  /* True Sight */
    { "Adef", CAbilityDefend, AB_SPELL | AB_TOGGLE, SPELL_TARGET_NONE, defend_orders },  /* Defend */
    { "Afla", CAbilityFlare, AB_SPELL, SPELL_TARGET_POINT },  /* Flare */
    { "Adts", CAbilityMagicSentry, AB_PASSIVE },  /* Magic Sentry */
    { "Ainf", CAbilityInnerFire, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Inner Fire */
    { "Adis", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Dispel Magic */
    { "Ahea", CAbilityHeal, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Heal */
    { "Aslo", CAbilitySlow, AB_SPELL | AB_AUTOCAST | AB_STATUS_POLICY, SPELL_TARGET_UNIT },  /* Slow */
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
    { "AIvi", CAbilityItemInvis, AB_ITEM },  /* Item Temporary Invisibility */
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
    { "ANdh", CAbilityDrunkenHaze, AB_SPELL | AB_STATUS_EVENTS | AB_STATUS_POLICY, SPELL_TARGET_UNIT },  /* Drunken Haze */
    { "ANsi", CAbilitySilence, AB_SPELL | AB_STATUS_EVENTS | AB_STATUS_POLICY, SPELL_TARGET_POINT },  /* Silence */
    { "ANba", CAbilityBlackArrow, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Black Arrow */
    { "ANch", CAbilityCharm, AB_SPELL, SPELL_TARGET_UNIT },  /* Charm */
    { "ANto", CAbilityTornado, AB_SPELL | AB_CHANNEL },  /* Tornado */
    { "Abgm", CAbilityBlightedGoldMine, 0 },  /* Blighted Gold Mine Ability */
    { "Aegm", CAbilityEntangledGoldMine, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE },  /* Entangled Gold Mine Ability */
    { "Aloa", CAbilityCargoLoad, AB_COMMAND },  /* Load */
    { "Adro", CAbilityCargoDrop, AB_COMMAND | AB_MOVE_TARGET_NO_WARP },  /* Unload */
    { "Adri", CAbilityCargoDropInstant, AB_COMMAND },  /* Unload Instant */
    { "Abun", CAbilityPassive, AB_PASSIVE },  /* Cargo Hold (Orc Burrow) */
    { "Acar", CAbilityPassive, AB_PASSIVE },  /* Cargo Hold */
    { "Aneu", CAbilityPassive, AB_PASSIVE },  /* Select Hero */
    { "Ane2", CAbilityPassive, AB_PASSIVE },  /* Neutral Building (any unit) */
    { "ANfb", CAbilityFireBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Firebolt */
    { "Agld", CAbilityGoldMine, 0 },  /* Gold Mine ability */
    { "Artn", CAbilityReturn, AB_COMMAND },  /* Return */
    { "Avul", CAbilityPassive, AB_PASSIVE },  /* Invulnerable */
    { "Abli", CAbilityBlightGrowth, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_INNATE | AB_TYPE_INIT },  /* Blight */
    { "ANfl", CAbilityForkedLightning, AB_SPELL, SPELL_TARGET_UNIT },  /* Forked Lightning */

    /* NightElfAbilityStrings.txt */
    { "AEbl", CAbilityBlink, AB_SPELL, SPELL_TARGET_POINT },  /* Blink */
    { "AEfk", CAbilityFanOfKnives, AB_SPELL },  /* Fan of Knives */
    { "AEsh", CAbilityShadowStrike, AB_SPELL, SPELL_TARGET_UNIT },  /* Shadow Strike */
    { "AEsv", CAbilitySpiritOfVengeance, AB_SPELL },  /* Vengeance */
    { "Aeat", CAbilityEatTree, AB_SPELL, SPELL_TARGET_UNIT },  /* Eat Tree */
    { "Ambt", CAbilityManaBattery, AB_SPELL | AB_AUTOCAST | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_UNIT },  /* Replenish Mana and Life */
    { "Awha", CAbilityWispHarvest, AB_COMMAND },  /* Gather */
    { "Aent", CAbilityEntangle, AB_COMMAND, SPELL_TARGET_NONE, entangle_orders },  /* Entangle Gold Mine */
    { "Aenc", CAbilityPassive, AB_PASSIVE },  /* Load */
    { "Aroo", CAbilityRoot, AB_COMMAND | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_NONE, ancient_root_orders },  /* Root */
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
    { "Aren", CAbilityRepairGeneric, AB_COMMAND | AB_AUTOCAST, SPELL_TARGET_NONE, renew_orders },  /* Renew */
    { "Afae", CAbilityFaerieFire, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Faerie Fire */
    { "Arej", CAbilityRejuvination, AB_SPELL, SPELL_TARGET_UNIT },  /* Rejuvenation */
    { "Aroa", CAbilityRoar, AB_SPELL },  /* Roar */

    /* OrcAbilityStrings.txt */
    { "AOhw", CAbilityHealingWave, AB_SPELL, SPELL_TARGET_UNIT },  /* Healing Wave */
    { "AOhx", CAbilityHex, AB_SPELL, SPELL_TARGET_UNIT },  /* Hex */
    { "AOvd", CAbilityVoodoo, AB_SPELL },  /* Big Bad Voodoo */
    { "Astd", CAbilityStandDown, AB_COMMAND },  /* Stand Down */
    { "Abtl", CAbilityBattlestations, AB_COMMAND },  /* Battle Stations */
    { "AOwk", CAbilityWindWalk, AB_SPELL | AB_COOLDOWN_ON_STATUS_REMOVE | AB_STATUS_EVENTS },  /* Wind Walk */
    { "AOmi", CAbilityMirrorImage, AB_SPELL },  /* Mirror Image */
    { "AOcr", CAbilityCreepAura, AB_PASSIVE },  /* Critical Strike */
    { "AOww", CAbilityWhirlwind, AB_SPELL | AB_CHANNEL },  /* Bladestorm */
    { "AOcl", CAbilityChainLightning, AB_SPELL, SPELL_TARGET_UNIT },  /* Chain Lightning */
    { "AOfs", CAbilityFarSight, AB_SPELL, SPELL_TARGET_POINT },  /* Far Sight */
    { "AOsf", CAbilitySpiritWolf, AB_SPELL },  /* Feral Spirit */
    { "AOeq", CAbilityEarthquake, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Earthquake */
    { "AOsh", CAbilityShockwave, AB_SPELL, SPELL_TARGET_POINT },  /* Shockwave */
    { "AOae", CAbilityEnduranceAura, AB_PASSIVE },  /* Endurance Aura */
    { "AOre", CAbilityReincarnation, AB_PASSIVE },  /* Reincarnation */
    { "AOws", CAbilityStomp, AB_SPELL },  /* War Stomp */
    { "Ablo", CAbilityBloodlust, AB_SPELL | AB_AUTOCAST | AB_STATUS_POLICY, SPELL_TARGET_UNIT },  /* Bloodlust */

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
    { "Arst", CAbilityRepairGeneric, AB_COMMAND | AB_AUTOCAST, SPELL_TARGET_NONE, restoration_orders },  /* Restoration */
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
    { "AIsp", CAbilityItemSpeed, AB_ITEM | AB_POWERUP },  /* Item Temporary Speed Bonus */
    // TODO: AIdm a_bounce  /* Item Area tree/wall damage */
    // TODO: AIfl a_button  /* Item Capture The Flag */
    // TODO: AIfm a_button  /* Item Capture The Flag */
    // TODO: AIfn a_button  /* Item Capture The Flag */
    // TODO: AIfo a_button  /* Item Capture The Flag */
    // TODO: AIfe a_button  /* Item Capture The Flag */
    { "AIha", CAbilityItemHealAoe, AB_ITEM | AB_POWERUP },  /* Item Area Healing / Healing Runes */
    // TODO: AIvu a_item_invul  /* Item Temporary Invulnerability */
    { "AImr", CAbilityItemManaAoe, AB_ITEM | AB_POWERUP }, /* Item Area Mana Regain */
    { "APmr", CAbilityItemManaAoe, AB_ITEM | AB_POWERUP }, /* Rune of Mana */
    { "APmg", CAbilityItemManaAoe, AB_ITEM | AB_POWERUP }, /* Rune of Greater Mana */
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
    { "AIso", CAbilitySoulTrap, AB_SPELL, SPELL_TARGET_UNIT },  /* Item Soul Theft */
    { "Asou", CAbilitySoulTrapped, AB_PASSIVE },  /* Item Soul Possession */
    // TODO: AIrc a_item_reincarnation  /* Item Reincarnation */
    // TODO: AIrt a_item_recall  /* Item Recall */
    // TODO: AItp a_item_town_portal  /* Item Town Portal */
    { "AIpm", CAbilityPlaceMine, AB_SPELL, SPELL_TARGET_POINT },  /* Item Place Goblin Land Mine */
    // TODO: AIaa a_damage_bonus_base  /* Item Permanent Damage Gain */
    // TODO: AIva a_attack_mod  /* Item Life Steal */
    // TODO: AIcf CAbilityImmolation  /* Item Immolation */
    { "AIzb", CAbilityAttackBonus, 0 },  /* Item Freeze Damage Bonus (orb) */
    // TODO: Arel a_aura_regen_life  /* Item Life Regeneration */
    { "Aami", CAbilityAntiMagicShellInstant, AB_SPELL, SPELL_TARGET_UNIT },  /* Item Anti-Magic Shell Instant */
    // TODO: AIas a_unknown  /* Item Attack Speed Bonus */
    // TODO: AIan a_simple_spell  /* Item Animate Dead */
    // TODO: AIrs a_item_reincarnation  /* Item Resurrection */
    { "AIms", CAbilityMoveSpeedBonus, AB_PASSIVE | AB_MOVE_SPEED_BONUS },  /* Item Move Speed Bonus */
    { "APrl", CAbilityItemResurrection, AB_ITEM | AB_POWERUP },  /* Rune of Lesser Resurrection */
    { "APrr", CAbilityItemResurrection, AB_ITEM | AB_POWERUP },  /* Rune of Greater Resurrection */
    { "AIgo", CAbilityItemGold, AB_ITEM | AB_POWERUP },  /* Chest of Gold / Gold Coins */
    { "AIlu", CAbilityItemLumber, AB_ITEM | AB_POWERUP },  /* Bundle of Lumber */
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
    { "AIbl", CAbilityTinyStructure, AB_SPELL, SPELL_TARGET_POINT }, /* Build Tiny Castle */
    { "AIbg", CAbilityTinyStructure, AB_SPELL, SPELL_TARGET_POINT }, /* Build Tiny Great Hall */
    { "AIbt", CAbilityTinyStructure, AB_SPELL, SPELL_TARGET_POINT }, /* Build Tiny Scout Tower */
    { "AIbb", CAbilityTinyStructure, AB_SPELL, SPELL_TARGET_POINT }, /* Build Tiny Blacksmith */
    { "AIbf", CAbilityTinyStructure, AB_SPELL, SPELL_TARGET_POINT }, /* Build Tiny Farm */
    { "AIbr", CAbilityTinyStructure, AB_SPELL, SPELL_TARGET_POINT }, /* Build Tiny Lumber Mill */
    { "AIbs", CAbilityTinyStructure, AB_SPELL, SPELL_TARGET_POINT }, /* Build Tiny Barracks */
    { "AIbh", CAbilityTinyStructure, AB_SPELL, SPELL_TARGET_POINT }, /* Build Tiny Altar of Kings */
    // TODO: Ashs a_spell  /* Wand of Shadowsight */
    // TODO: Aret CAbilityResurrection  /* Tome of Retraining */
    // TODO: ANpr a_button  /* Staff of Preservation */
    { "Amec", CAbilityMechanicalCritter, AB_ITEM | AB_STATUS_EVENTS }, /* Mechanical Critter */
    // TODO: ANss a_bounce  /* Spell Shield */
    { "ANse", CAbilitySpellShieldAoe, AB_ITEM | AB_POWERUP }, /* Rune of Shielding */
    // TODO: Aspb a_bounce  /* Spell Book */
    { "AIrd", CAbilityRaiseDead, AB_SPELL },  /* Raise Dead (Item) */
    // TODO: ANsa a_bounce  /* Staff of Sanctuary */
    { "APsa", CAbilityItemSpeedAoe, AB_ITEM | AB_POWERUP },  /* Rune of Speed */
    { "AIsa", CAbilityItemSpeedAoe, AB_ITEM | AB_POWERUP },  /* Scroll of Haste / Speed AOE */
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
    { "Awrp", CAbilityWarp, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT },  /* Waygate ability */
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
    { "ACsp", CAbilityCreepSleep, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT },  /* Natural creep sleep */
    { "Asod", CAbilityRaiseDead, AB_SPELL },  /* Spawn Skeleton */
    { "Assp", CAbilityForceOfNature, AB_SPELL },  /* Spawn Spiderlings */
    { "Aspd", CAbilityForceOfNature, AB_SPELL },  /* Spawn Spiders */
    { "AOac", CAbilityCommandAura, AB_PASSIVE },  /* Command Aura */
    { "ACad", CAbilityAnimateDead, AB_SPELL },  /* Animate Dead (creep) */
    { "ACrn", CAbilityReincarnation, AB_PASSIVE },  /* Reincarnation (creep) */
    { "Adda", CAbilityDeathDamageAoe, AB_PASSIVE },  /* AOE damage upon death */
    { "Agho", CAbilityGhost, AB_PASSIVE },  /* Ghost */
    { "Aeth", CAbilityGhostVisible, AB_PASSIVE },  /* Ghost */
    { "Amin", CAbilityLandMine, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT },  /* Mine - exploding */
    { "Apiv", CAbilityPermanentInvisibility, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT | AB_PRIMARY_TIMER },  /* Permanent Invisibility */
    { "Awan", CAbilityWander, AB_PASSIVE | AB_INNATE },  /* Wander */
    /* Aarm is registered with the explicit regeneration family below. */
    { "Asid", CAbilitySellItem, AB_PASSIVE },  /* Sell Items */
    { "Asud", CAbilitySellUnit, AB_PASSIVE },  /* Sell Units */

    /* NightElfAbilityStrings.txt */
    { "Avng", CAbilitySpiritOfVengeance, AB_SPELL, SPELL_TARGET_NONE },  /* Spirit of Vengeance */
    { "Amfl", CAbilityManaFlare, AB_SPELL | AB_CHANNEL | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_NONE },  /* Mana Flare */
    { "Apsh", CAbilityDivineShield, AB_SPELL },  /* Phase Shift */
    { "Aetl", CAbilityEthereal, AB_PASSIVE },  /* Ethereal */
    { "Agra", CAbilityGrabTree, AB_PASSIVE },  /* War Club */
    { "Assk", CAbilityHardenedSkin, AB_PASSIVE },  /* Hardened Skin */
    { "Arsk", CAbilityResistantSkin, AB_PASSIVE },  /* Resistant Skin */
    { "Atau", CAbilityTaunt, AB_SPELL },  /* Taunt */
    { "Amgl", CAbilityMoonGlaive, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT },  /* Moon Glaive */
    { "Aspo", CAbilitySlowPoison, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT },  /* Slow Poison */
    { "Ashm", CAbilityShadowMeld, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_INNATE | AB_TYPE_INIT | AB_SPELL, SPELL_TARGET_NONE, hide_orders },  /* Shadow Meld */
    { "Ahid", CAbilityShadowMeldAkama, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_INNATE | AB_TYPE_INIT | AB_SPELL, SPELL_TARGET_NONE, hide_orders },  /* Shadow Meld (Akama) */
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
    { "Acha", CAbilityChaos, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_PRIMARY_TIMER },  /* Chaos */
    { "Achl", CAbilityCargoLoad, AB_COMMAND },  /* Chaos Cargo Load */
    { "Awar", CAbilityPulverize, AB_PASSIVE },  /* Pulverize */
    { "Aens", CAbilityEnsnare, AB_SPELL | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_UNIT },  /* Ensnare */
    { "Adev", CAbilityCannibalize, AB_SPELL | AB_CHANNEL },  /* Devour */
    { "Aprg", CAbilityPurge, AB_SPELL, SPELL_TARGET_UNIT },  /* Purge */
    { "Apg2", CAbilityPurge, AB_SPELL, SPELL_TARGET_UNIT },  /* Purge (TFT melee; DataD/DataE pause) */
    { "Alsh", CAbilityLightningShield, AB_SPELL, SPELL_TARGET_UNIT },  /* Lightning Shield */
    { "Aeye", CAbilityEvilEye, AB_SPELL, SPELL_TARGET_POINT },  /* Sentry Ward */
    { "Asta", CAbilityStasisTrap, AB_SPELL, SPELL_TARGET_POINT },  /* Stasis Trap */
    { "Ahwd", CAbilityHealingWard, AB_SPELL, SPELL_TARGET_POINT },  /* Healing Ward */
    { "Aoar", CAbilityAuraRegenLife, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT },  /* Healing Ward Aura */
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
    { "Aexh", CAbilityExhumeCorpses, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE },  /* Exhume Corpses */
    { "Aave", CAbilityMetamorphosis, AB_SPELL },  /* Destroyer Form */
    { "Afak", CAbilityFlamingArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Orb of Annihilation */
    { "Advm", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Devour Magic */
    { "Aabr", CAbilityAuraRegenLife, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT },  /* Aura of Blight */
    { "Aabs", CAbilityAbsorb, AB_PASSIVE },  /* Absorb Mana */
    { "Abur", CAbilityCreepSleep, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT },  /* Burrow */
    { "Amtc", CAbilityCargoHold, AB_PASSIVE },  /* Cargo Hold */
    { "Atru", CAbilityTrueSight, AB_PASSIVE },  /* True Sight */
    { "Auns", CAbilityUnsummon, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Unsummon Building */
    { "Agyd", CAbilityGraveyard, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE },  /* Create Corpse */
    { "Alam", CAbilitySacrifice, AB_SPELL, SPELL_TARGET_UNIT },  /* Sacrifice (Acolyte) */
    { "Asac", CAbilitySacrifice, AB_SPELL, SPELL_TARGET_UNIT },  /* Sacrifice (Sacrificial Pit) */
    { "Acan", CAbilityCannibalize, AB_SPELL | AB_CHANNEL },  /* Cannibalize */
    { "Aspa", CAbilitySpiderAttack, AB_PASSIVE },  /* Spider Attack */
    { "Aweb", CAbilityWeb, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Web */
    { "Astn", CAbilityStoneForm, AB_SPELL, SPELL_TARGET_NONE, stone_form_orders },  /* Stone Form */
    { "Amel", CAbilityCargoLoad, AB_COMMAND | AB_AUTOCAST },  /* Get Corpse */
    { "Amed", CAbilityCargoDrop, AB_COMMAND },  /* Drop Corpse */
    { "Aapl", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE },  /* Disease Cloud */
    { "Apts", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE },  /* Disease Cloud */
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
    { "ANsu", CAbilitySubmergeMyrmidon, AB_SPELL | AB_TOGGLE, SPELL_TARGET_NONE, submerge_orders },  /* Submerge (Myrmidon) */
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
    { "ACbb", CAbilityBloodlust, AB_SPELL | AB_AUTOCAST | AB_STATUS_POLICY, SPELL_TARGET_UNIT },  /* Bloodlust (creep, Hotkey B) */
    { "ACbc", CAbilityBreathOfFire, AB_SPELL, SPELL_TARGET_POINT },  /* Breath of Fire (Creep) */
    { "ACbh", CAbilityBash, AB_PASSIVE },  /* Bash (creep) */
    { "ACbk", CAbilityBlackArrow, AB_SPELL | AB_TOGGLE | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Black Arrow (melee, creep) */
    { "ACbl", CAbilityBloodlust, AB_SPELL | AB_AUTOCAST | AB_STATUS_POLICY, SPELL_TARGET_UNIT },  /* Bloodlust (Creep) */
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
    { "ACen", CAbilityEnsnare, AB_SPELL | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_UNIT },  /* Ensnare (Creep) */
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
    { "ACsi", CAbilitySilence, AB_SPELL | AB_STATUS_EVENTS | AB_STATUS_POLICY, SPELL_TARGET_POINT },  /* Silence (Creep) */
    { "ACsk", CAbilityResistantSkin, AB_PASSIVE },  /* Resistant Skin (3,1 pos, creep) */
    { "ACsm", CAbilityDrain, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Siphon Mana (Creep) */
    { "ACss", CAbilityShadowStrike, AB_SPELL, SPELL_TARGET_UNIT },  /* Shadow Strike (Creep) */
    { "ACst", CAbilityShockwave, AB_SPELL, SPELL_TARGET_POINT },  /* Shockwave (Trap) */
    { "ACsw", CAbilitySlow, AB_SPELL | AB_AUTOCAST | AB_STATUS_POLICY, SPELL_TARGET_UNIT },  /* Slow (Creep) */
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
    { "ANwk", CAbilityWindWalk, AB_SPELL | AB_COOLDOWN_ON_STATUS_REMOVE | AB_STATUS_EVENTS },  /* Wind Walk */
    { "Aap1", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE },  /* Aura - Plague (Abomination) */
    { "Aap2", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE },  /* Aura - Plague (Plague Ward) */
    { "Aap3", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE },  /* Aura - Plague (Creep) */
    { "Aap4", CAbilityDiseaseCloud, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE },  /* Aura - Plague (Creep gfx) */
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
    { "Aro1", CAbilityRoot, AB_COMMAND | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_NONE, ancient_root_orders },  /* Root (Ancients) */
    { "Aro2", CAbilityRoot, AB_COMMAND | AB_UPDATE | AB_TYPE_UPDATE, SPELL_TARGET_NONE, ancient_root_orders },  /* Root (Ancient Protector) */
    { "Awfb", CAbilityFireBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Fire Bolt (warlock) */
    { "Awrg", CAbilityStomp, AB_SPELL },  /* War Stomp (sea giant) */
    { "Awrh", CAbilityStomp, AB_SPELL },  /* War Stomp (hydra) */
    { "Awrs", CAbilityStomp, AB_SPELL },  /* War Stomp (creep) */
    { "Sca1", CAbilityChaos, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_PRIMARY_TIMER },  /* Chaos (Grunt) */
    { "Sca2", CAbilityChaos, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_PRIMARY_TIMER },  /* Chaos (Raider) */
    { "Sca3", CAbilityChaos, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_PRIMARY_TIMER },  /* Chaos (Shaman) */
    { "Sca4", CAbilityChaos, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_PRIMARY_TIMER },  /* Chaos (Kodo) */
    { "Sca5", CAbilityChaos, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_PRIMARY_TIMER },  /* Chaos (Peon) */
    { "Sca6", CAbilityChaos, AB_PASSIVE | AB_UPDATE | AB_TYPE_UPDATE | AB_PRIMARY_TIMER },  /* Chaos (Grom) */
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
    { "Amgr", CAbilityMoonGlaive, AB_PASSIVE | AB_INNATE | AB_TYPE_INIT },  /* Moon Glaive (Naisha) */
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
static abilityitem_t ability_update_items[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t num_updates;
static abilityProc_t owner_updates[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t num_owner_updates;
static abilityProc_t timer_updates[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t num_timer_updates;
static abilityitem_t innate_items[sizeof(abilitylist) / sizeof(abilitylist[0])];
static abilityMessageSet_t innate_messages[sizeof(abilitylist) / sizeof(abilitylist[0])];
static abilityProc_t innate_message_procs[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t num_innate;
#ifdef BZ_TESTS
static uint32_t innate_event_visits;
static uint32_t authored_event_membership_queries;
static uint32_t authored_event_visits;
#endif
static abilityitem_t engine_event_items[sizeof(abilitylist) / sizeof(abilitylist[0])];
static abilityMessageSet_t engine_event_messages[sizeof(abilitylist) / sizeof(abilitylist[0])];
static abilityProc_t engine_event_procs[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t num_engine_events;
static abilityProc_t ability_index_procs[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t ability_index_values[sizeof(abilitylist) / sizeof(abilitylist[0])];
static uint32_t num_ability_index_procs;

static bool innate_receives(uint32_t index, abilityMsg_t msg) {
    return msg >= 0 && msg < A_NUM_MESSAGES &&
        (innate_messages[index].bits[msg / 64] & (UINT64_C(1) << (msg % 64))) != 0;
}

static bool engine_event_receives(uint32_t index, abilityMsg_t msg) {
    /* Registry replacement keeps the original live fallback, as with authored
     * plans. The subscription belongs to the procedure queried at init. */
    if (engine_event_items[index].ability->proc != engine_event_procs[index]) return true;
    return msg >= 0 && msg < A_NUM_MESSAGES &&
        (engine_event_messages[index].bits[msg / 64] & (UINT64_C(1) << (msg % 64))) != 0;
}

/* Reuse a contract only for that exact procedure. A child can handle messages
 * its passive parent ignores, so querying an inherited empty mask would lose
 * the child's lifecycle notifications. Unknown procedures stay subscribed. */
static bool known_unit_ability_receives(abilityitem_t item, abilityMsg_t msg) {
    if (!item.ability || msg == A_UNIT_EVENT_MASK) return true;
    abilityProc_t proc = item.ability->proc;
    if (proc == CAbilityNoop || proc == CAbilityPassive) return false;
    FOR_LOOP(i, num_innate)
        if (innate_message_procs[i] == proc) return innate_receives(i, msg);
    return true;
}

/* Class names never change between registry initializations. Store the first
 * row for each exact name, including full command strings and unknown names. */
#define ABILITY_NAME_SLOTS 2048
static uint32_t ability_names[ABILITY_NAME_SLOTS];
typedef struct {
    uint32_t code,generation;
    ability_t const *ability;
    bool valid;
} abilityAliasCache_t;
static abilityAliasCache_t ability_alias_cache[ABILITY_NAME_SLOTS];
_Static_assert(sizeof(abilitylist) / sizeof(*abilitylist) < ABILITY_NAME_SLOTS / 2,
               "Ability name index requires spare slots");

static uint32_t ability_name_hash(cstring_t name) {
    uint32_t hash = 2166136261u;
    for (unsigned char const *p = (unsigned char const *)name; *p; p++)
        hash = (hash ^ *p) * 16777619u;
    return hash & (ABILITY_NAME_SLOTS - 1);
}

static void ability_build_names(void) {
    memset(ability_names, 0, sizeof(ability_names));
    memset(ability_alias_cache,0,sizeof(ability_alias_cache));
    FOR_LOOP(i, game.num_abilities) {
        cstring_t name = abilitylist[i].classname;
        if (!name) continue;
        uint32_t slot = ability_name_hash(name);
        while (ability_names[slot] && strcmp(abilitylist[ability_names[slot] - 1].classname, name))
            slot = (slot + 1) & (ABILITY_NAME_SLOTS - 1);
        if (!ability_names[slot]) ability_names[slot] = i + 1;
    }
}

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

/* Owner lists are ability-owned; keep the scheduler independent of concrete movement rules. */
void S_BeginAbilityOwnerUpdates(void) {
    FOR_LOOP(i, num_owner_updates) owner_updates[i](NULL, A_OWNER_BEGIN, NULL);
}

void S_RunAbilityOwnerUpdates(void) {
    FOR_LOOP(i, num_owner_updates) owner_updates[i](NULL, A_OWNER_UPDATE, NULL);
}

/* Each procedure owns its active timer list and deadline checks. */
void S_RunAbilityTimers(void) {
    FOR_LOOP(i, num_timer_updates) timer_updates[i](NULL, A_PRIMARY_TIMER, NULL);
}

/* Timer owners retain their own queues. The clock merges their earliest
 * request with script timers and the path owner by deadline, then serial. */
bool S_NextAbilityPrimaryTimer(abilityTimerRequest_t *next) {
    bool found=false;
    FOR_LOOP(i,num_timer_updates) {
        abilityTimerRequest_t candidate={0};
        abilityCall_t call={.primary_timer=&candidate};
        if(!timer_updates[i](NULL,A_PRIMARY_TIMER_NEXT,&call))continue;
        candidate.proc=timer_updates[i];
        if(!found || candidate.deadline.time<next->deadline.time ||
            (candidate.deadline.time==next->deadline.time && candidate.sequence<next->sequence)) {
            *next=candidate;found=true;
        }
    }
    return found;
}

void S_RebaseAbilityPrimaryTimers(float span) {
    abilityCall_t call={.clock_span=span};
    FOR_LOOP(i,num_timer_updates)timer_updates[i](NULL,A_PRIMARY_TIMER_REBASE,&call);
}

void S_ResetAbilityTimers(void) {
    FOR_LOOP(i, num_timer_updates) timer_updates[i](NULL, A_TIMERS_RESET, NULL);
}

void S_RebuildAbilityTimers(void) {
    FOR_LOOP(i, num_timer_updates) timer_updates[i](NULL, A_TIMERS_REBUILD, NULL);
}

/* Persistent effects can outlive their active order; the callback owns its per-unit state checks. */
static void unit_dispatch_updates(edict_t *ent);
void S_RunAbilityUpdates(edict_t *ent) {
    unit_dispatch_updates(ent);
    S_UpdateUnitPassiveEffects(ent);
}

static intptr_t unit_dispatch_ability_code(edict_t *ent, abilityMsg_t msg, abilityCall_t const *payload,
                                           uint32_t code, uint32_t *seen, uint32_t *count,
                                           uint32_t capacity, bool require_skill) {
    abilityitem_t item;
    abilityCall_t invoke;

    if (!ent || !code || !seen || !count) return ABILITY_ORDER_UNHANDLED;
    FOR_LOOP(i, *count) if (seen[i] == code) return ABILITY_ORDER_UNHANDLED;
    if (*count < capacity) seen[(*count)++] = code;
    if (require_skill) {
#ifdef BZ_TESTS
        authored_event_membership_queries++;
#endif
        if (!G_ActorHasAbilityCode(ent,code)) return ABILITY_ORDER_UNHANDLED;
    }
    item = S_AbilityItem(code);
    if (!item.ability) return ABILITY_ORDER_UNHANDLED;
    invoke = payload ? *payload : MAKE(abilityCall_t, 0);
    invoke.item = &item;
    return S_AbilityMessage(ent, msg, &invoke);
}

/* Route policy notifications only to abilities that opt in. Status data is
 * also used for numeric payloads, so it is not by itself an owner contract. */
intptr_t S_UnitStatusAbilityEvent(edict_t *ent, abilityMsg_t msg, abilityCall_t const *payload) {
    intptr_t result = 0;

    if (!ent) return 0;
    FOR_LOOP(i, G_UnitStatusSlotCount(ent)) {
        heroabilitystatus_t *status = ent->abilstatus + i;
        abilityitem_t item;
        abilityCall_t call;
        intptr_t handled;

        if (!status->level || !status->data) continue;
        item = S_AbilityItem(status->data);
        if (!item.ability || !(item.ability->flags & AB_STATUS_EVENTS)) continue;
        call = payload ? *payload : MAKE(abilityCall_t, 0);
        call.status.slot = status;
        call.status.ability = status->data;
        handled = item.ability->proc(ent, msg, &call);
        if (msg == A_ATTACK_DAMAGE_BONUS) result += handled;
        else result |= handled;
    }
    return result;
}

/* Dispatch lifecycle notifications to the unit's concrete authored abilities.
 * The active order is visited first, then innate hooks and AbilityData rows;
 * stop-first queries retain the owning procedure's result. */
static intptr_t unit_dispatch_authored_abilities_uncached(edict_t *ent, abilityMsg_t msg,
                                                 abilityCall_t const *payload, bool stop_first,
                                                 bool include_innate, bool include_channel) {
    uint32_t seen[MAX_ABILITIES * 2 + MAX_HERO_ABILITIES] = {0}, count = 0;
    uint32_t const capacity = sizeof(seen) / sizeof(*seen);
    bool handled = false;

    if (!ent) return ABILITY_ORDER_UNHANDLED;
    if (include_channel && msg == A_MOVE_LEAVE && ent->channel && ent->channel->code) {
        intptr_t const result = unit_dispatch_ability_code(ent, msg, payload, ent->channel->code,
                                                            seen, &count, capacity, false);
        if (msg == A_ISSUED_TARGET_ORDER && result != ABILITY_ORDER_UNHANDLED) return result;
        handled |= result != 0;
        if (stop_first && handled) return true;
    }
    if (include_innate) {
        FOR_LOOP(i, num_innate) {
            abilityitem_t const *item = innate_items + i;
            abilityCall_t invoke = payload ? *payload : MAKE(abilityCall_t, 0);
            intptr_t result;
            if (item->code) {
                bool duplicate = false;
                FOR_LOOP(k, count) if (seen[k] == item->code) { duplicate = true; break; }
                if (duplicate) continue;
                if (count < capacity) seen[count++] = item->code;
            }
            /* Keep deduplication before filtering: an implicit owner and its
             * authored rawcode remain a single notification in registry order. */
            if (!innate_receives(i, msg)) continue;
#ifdef BZ_TESTS
            innate_event_visits++;
#endif
            invoke.item = item;
            result = S_AbilityMessage(ent, msg, &invoke);
            if (msg == A_ISSUED_TARGET_ORDER && result != ABILITY_ORDER_UNHANDLED) return result;
            handled |= result != 0;
            if (stop_first && handled) return true;
        }
    }
#define DISPATCH_AUTHORED_ABILITY(code_) do { \
        intptr_t const result = unit_dispatch_ability_code(ent, msg, payload, (code_), \
                                                             seen, &count, capacity, true); \
        if (msg == A_ISSUED_TARGET_ORDER && result != ABILITY_ORDER_UNHANDLED) return result; \
        handled |= result != 0; \
        if (stop_first && handled) return true; \
    } while (0)
    uint32_t authored_count;
    uint32_t const *authored = G_UnitAbilityCodes(ent->data.UnitAbilities, &authored_count);
    FOR_LOOP(i, authored_count) DISPATCH_AUTHORED_ABILITY(authored[i]);
    if (msg == A_UNIT_REMOVE) {
        for (int32_t i = (int32_t)ARRAY_COUNT(ent->abilities.added) - 1; i >= 0; i--)
            if (ent->abilities.added[i]) DISPATCH_AUTHORED_ABILITY(ent->abilities.added[i]);
    } else {
        FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added))
            if (ent->abilities.added[i]) DISPATCH_AUTHORED_ABILITY(ent->abilities.added[i]);
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES)
        if (ent->heroabilities[i].level && ent->heroabilities[i].code)
            DISPATCH_AUTHORED_ABILITY(ent->heroabilities[i].code);
#undef DISPATCH_AUTHORED_ABILITY
    return handled;
}

#define UNIT_EVENT_CAPACITY (MAX_ABILITIES * 2 + MAX_HERO_ABILITIES)
typedef struct unitEventPlan_s {
    UnitAbilities_t const *row;
    UnitData_t const *data;
    cstring_t list;
    uint32_t unit_generation, ability_generation, registry_generation, count, seen_count, innate_count;
    abilityMsg_t msg;
    bool include_innate;
    struct { abilityitem_t item; abilityProc_t proc; bool authored, receives; union { unitInitPolicy_t init; intptr_t update; }; uint32_t update_index, init_next; bool init_result; } entries[UNIT_EVENT_CAPACITY];
    uint32_t seen[UNIT_EVENT_CAPACITY];
    struct unitEventPlan_s *next;
} unitEventPlan_t;
static unitEventPlan_t *unit_event_plans[512];
static unitEventPlan_t *unit_event_retired;
static uint32_t unit_event_dispatch_depth;
static uint32_t unit_initialization_epoch, unit_registry_generation;
#ifdef BZ_TESTS
static bool unit_events_force_uncached;
static bool unit_updates_force_uncached;
static uint32_t unit_update_calls, unit_init_steps;
#endif

/* Registry writes are observation barriers. Keep active plans alive so a
 * callback can replace a later procedure without restarting the event. */
void S_ReplaceAbilityProcedure(ability_t const *ability, abilityProc_t proc) {
    assert(ability && proc);
    if (ability->proc == proc) return;
    ((ability_t *)ability)->proc = proc;
    if (++unit_registry_generation == 0) { gi.error("Ability registry generation exhausted"); abort(); }
    if (++unit_initialization_epoch == 0) { gi.error("Unit initialization epoch exhausted"); abort(); }
}

static void unit_event_reclaim(void) {
    if (unit_event_dispatch_depth) return;
    while (unit_event_retired) {
        unitEventPlan_t *plan = unit_event_retired;
        unit_event_retired = plan->next;
        free(plan);
    }
}

void S_ClearUnitEventPlans(void) {
    if (++unit_initialization_epoch == 0) { gi.error("Unit initialization epoch exhausted"); abort(); }
    FOR_LOOP(i, sizeof(unit_event_plans) / sizeof(*unit_event_plans)) {
        unitEventPlan_t *plan = unit_event_plans[i];
        while (plan) {
            unitEventPlan_t *next = plan->next;
            plan->next = unit_event_retired; unit_event_retired = plan; plan = next;
        }
        unit_event_plans[i] = NULL;
    }
    /* Nested constructors immediately acquire new plans. An outer callback's
     * ordered continuation still owns its old immutable plan until it returns. */
    unit_event_reclaim();
}

static bool unit_event_seen(uint32_t code, uint32_t const *seen, uint32_t count) {
    FOR_LOOP(i, count) if (seen[i] == code) return true;
    return false;
}

/* Plans contain immutable registry rows, rawcodes and order only. Live unit
 * state and procedure results remain synchronous decisions on every call. Old
 * plans survive metadata changes because a callback may be using their rows. */
bool S_UnitTypeHasAbilityProc(UnitAbilities_t const *row, abilityProc_t proc) {
    uint32_t count;
    uint32_t const *codes = G_UnitAbilityCodes(row, &count);
    FOR_LOOP(i, count) {
        abilityitem_t item = S_AbilityItem(codes[i]);
        if (item.ability && item.ability->proc == proc) return true;
    }
    return false;
}

bool S_UnitTypeHasAbilityCode(UnitAbilities_t const *row, uint32_t code) {
    uint32_t count;
    unitAbilityToken_t const *tokens = G_UnitAbilityTokens(row, &count);
    FOR_LOOP(i, count)
        if (tokens[i].length == 4 && (tokens[i].code == code || tokens[i].base == code)) return true;
    return false;
}

static bool unit_ability_has_flags(uint32_t code, uint32_t flags) {
    abilityitem_t item = S_AbilityItem(code);
    return item.ability && (item.ability->flags & flags) == flags;
}

/* Query attached behavior, including custom aliases and item abilities. This
 * is a read-only membership query: disabled commands still own their class.
 * Native48cb80(Adro,1,0,1,1) includes both ordinary and item attachments. */
bool S_UnitHasAbilityFlags(edict_t const *ent, uint32_t flags) {
    if (!ent || !flags) return false;
    uint32_t count;
    uint32_t const *codes = G_UnitAbilityCodes(ent->data.UnitAbilities, &count);
    FOR_LOOP(i, count)
        if (!unit_event_seen(codes[i],ent->abilities.removed,ARRAY_COUNT(ent->abilities.removed)) &&
            unit_ability_has_flags(codes[i],flags)) return true;
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added))
        if (unit_ability_has_flags(ent->abilities.added[i],flags)) return true;
    FOR_LOOP(i, MAX_HERO_ABILITIES)
        if (ent->heroabilities[i].level &&
            !unit_event_seen(ent->heroabilities[i].code,ent->abilities.removed,ARRAY_COUNT(ent->abilities.removed)) &&
            unit_ability_has_flags(ent->heroabilities[i].code,flags)) return true;
    FOR_LOOP(i, MAX_INVENTORY) {
        cstring_t list = G_ItemAbilityList(ent->inventory[i]);
        if (!list) continue;
        PARSE_LIST(list, name, parse_segment)
            if (strlen(name)==4 && unit_ability_has_flags(FS_SLKKey(name),flags)) return true;
    }
    return false;
}

static unitEventPlan_t *unit_event_plan(edict_t const *ent, abilityMsg_t msg, bool include_innate) {
    UnitAbilities_t const *row = ent->data.UnitAbilities;
    cstring_t list = row ? row->abilList : NULL;
    UnitData_t const *data = msg == A_UNIT_INIT ? ent->data.UnitData : NULL;
    uint32_t const unit_generation = G_UnitDataGeneration(), ability_generation = G_AbilityDataGeneration();
    uintptr_t hash = ((uintptr_t)row >> 4) ^ (unsigned)msg ^ (include_innate ? 256 : 0);
    uint32_t const slot = (hash ^ (hash >> 16)) & 511;
    for (unitEventPlan_t *plan = unit_event_plans[slot]; plan; plan = plan->next)
        if (plan->row == row && plan->data == data && plan->list == list && plan->msg == msg &&
            plan->include_innate == include_innate && plan->unit_generation == unit_generation &&
            plan->ability_generation == ability_generation && plan->registry_generation == unit_registry_generation) return plan;
    uint32_t count;
    uint32_t const *codes = G_UnitAbilityCodes(row, &count);
    /* Keep the old capacity semantics for unusually long lists. */
    if (count + (include_innate ? num_innate : 0) + 1 > UNIT_EVENT_CAPACITY) return NULL;
    unitEventPlan_t *plan = calloc(1, sizeof(*plan));
    if (!plan) { gi.error("Unit event plan allocation failed"); abort(); }
    plan->row = row; plan->data = data; plan->list = list; plan->msg = msg; plan->include_innate = include_innate;
    plan->unit_generation = unit_generation; plan->ability_generation = ability_generation;
    plan->registry_generation = unit_registry_generation;
    if (msg == A_UNIT_TYPE_UPDATE) {
        if (num_updates > UNIT_EVENT_CAPACITY) { free(plan); return NULL; }
        FOR_LOOP(i, num_updates) {
            abilityitem_t item = ability_update_items[i];
            intptr_t policy = UNIT_UPDATE_RUN;
            bool declared = false;
            if ((item.ability->flags & AB_TYPE_UPDATE) && item.ability->proc == ability_updates[i]) {
                abilityCall_t call = { .item = &item, .unit_type = row };
                policy = ability_updates[i](NULL, A_UNIT_TYPE_UPDATE, &call);
                if (policy != UNIT_UPDATE_RUN && policy != UNIT_UPDATE_SKIP &&
                    !(policy >= UNIT_UPDATE_POINTER_BASE &&
                      policy <= UNIT_UPDATE_POINTER_BASE + sizeof(edict_t) - sizeof(void *)))
                    policy = UNIT_UPDATE_RUN;
                else declared = true;
            }
            if (policy == UNIT_UPDATE_SKIP) continue;
            uint32_t n = plan->count++;
            plan->entries[n].item = item;
            plan->entries[n].proc = ability_updates[i];
            plan->entries[n].update = policy;
            plan->entries[n].receives = declared;
            plan->entries[n].update_index = i;
        }
        plan->next = unit_event_plans[slot]; unit_event_plans[slot] = plan;
        return plan;
    }
    if (include_innate) FOR_LOOP(i, num_innate) {
        abilityitem_t item = innate_items[i];
        if (item.code && unit_event_seen(item.code, plan->seen, plan->seen_count)) continue;
        if (item.code) plan->seen[plan->seen_count++] = item.code;
        if (innate_receives(i, msg)) {
            uint32_t index = plan->count++;
            plan->entries[index].item = item;
            plan->entries[index].proc = item.ability->proc;
            if (msg == A_UNIT_INIT && (item.ability->flags & AB_TYPE_INIT) &&
                item.ability->proc == innate_message_procs[i]) {
                abilityCall_t call = { .item = &item, .unit_type = row, .unit_data = data };
                intptr_t policy = item.ability->proc(NULL, A_UNIT_TYPE_INIT, &call);
                if (policy == UNIT_INIT_RUN || policy == UNIT_INIT_RUN_LOCAL || policy == UNIT_INIT_SKIP_FALSE || policy == UNIT_INIT_SKIP_TRUE)
                    plan->entries[index].init = (unitInitPolicy_t)policy;
            }
        }
    }
    plan->innate_count = plan->count;
    /* A fresh constructor executes only actual work. Fold contiguous constant
     * callbacks while retaining their aggregate return and original boundary. */
    uint32_t next = plan->innate_count;
    bool result = false;
    for (uint32_t i = plan->innate_count; i-- > 0;) {
        unitInitPolicy_t policy = plan->entries[i].init;
        if (policy == UNIT_INIT_SKIP_FALSE || policy == UNIT_INIT_SKIP_TRUE) {
            result |= policy == UNIT_INIT_SKIP_TRUE;
        } else {
            next = i;
            result = false;
        }
        plan->entries[i].init_next = next;
        plan->entries[i].init_result = result;
    }
    FOR_LOOP(i, count) {
        uint32_t code = codes[i];
        if (!code || unit_event_seen(code, plan->seen, plan->seen_count)) continue;
        plan->seen[plan->seen_count++] = code;
        if (!(code & 0xffu) || !(code & 0xff00u) || !(code & 0xff0000u) || !(code & 0xff000000u)) continue;
        abilityitem_t item = S_AbilityItem(code);
        plan->entries[plan->count].item = item;
        plan->entries[plan->count].proc = item.ability ? item.ability->proc : NULL;
        plan->entries[plan->count].receives = known_unit_ability_receives(item, msg);
        plan->entries[plan->count++].authored = true;
    }
    plan->next = unit_event_plans[slot]; unit_event_plans[slot] = plan;
    return plan;
}

static bool unit_updates_have_runtime_ownership(edict_t const *ent) {
    if (ARRAY_COUNT(ent->abilities.added)) return true;
    FOR_LOOP(i, MAX_HERO_ABILITIES) if (ent->heroabilities[i].level) return true;
    return false;
}

/* Compile no-op ownership checks once per row. Mutable state pointers remain
 * live predicates. After an observable callback, a changed dependency resumes
 * the complete broadcast at its next original index, never at the beginning. */
static void unit_dispatch_updates(edict_t *ent) {
    uint32_t next = 0;
    unitEventPlan_t const *plan = NULL;
    if (ent && !unit_updates_have_runtime_ownership(ent)
#ifdef BZ_TESTS
        && !unit_updates_force_uncached
#endif
    ) plan = unit_event_plan(ent, A_UNIT_TYPE_UPDATE, false);
    if (plan) {
        uint32_t epoch = unit_initialization_epoch;
        FOR_LOOP(i, plan->count) {
            intptr_t policy = plan->entries[i].update;
            bool run = policy == UNIT_UPDATE_RUN;
            if (!run) {
                void const *state;
                memcpy(&state, (uint8_t const *)ent + (policy - UNIT_UPDATE_POINTER_BASE), sizeof(state));
                run = state != NULL;
            }
            next = plan->entries[i].update_index + 1;
            if (!run) continue;
#ifdef BZ_TESTS
            unit_update_calls++;
#endif
            bool declared = plan->entries[i].receives;
            plan->entries[i].proc(ent, A_UPDATE, NULL);
            if (!declared || unit_initialization_epoch != epoch ||
                G_UnitDataGeneration() != plan->unit_generation ||
                G_AbilityDataGeneration() != plan->ability_generation ||
                ent->data.UnitAbilities != plan->row ||
                (plan->row && plan->row->abilList != plan->list) ||
                unit_updates_have_runtime_ownership(ent)) goto complete;
        }
        return;
    }
complete:
    for (uint32_t i = next; i < num_updates; i++) {
#ifdef BZ_TESTS
        unit_update_calls++;
#endif
        ability_updates[i](ent, A_UPDATE, NULL);
    }
}

static intptr_t unit_dispatch_authored_abilities_execute(edict_t *ent, abilityMsg_t msg,
                                                      abilityCall_t const *payload, bool stop_first,
                                                      bool include_innate, bool include_channel, bool fresh, unitEventPlan_t const *prepared) {
    uint32_t seen[UNIT_EVENT_CAPACITY], count = 0, channel_code = 0;
    bool handled = false;
    if (!ent) return ABILITY_ORDER_UNHANDLED;
    if (fresh && (ent->abilstatus || ent->shadowmeld || ent->blight_growth || ent->sleep ||
        ent->waygate || ent->permanent_invisibility_fade.request.active || ARRAY_COUNT(ent->abilities.added) || ARRAY_COUNT(ent->abilities.removed))) fresh = false;
    if (fresh) FOR_LOOP(i, MAX_HERO_ABILITIES) if (ent->heroabilities[i].level) { fresh = false; break; }
#ifdef BZ_TESTS
    if (unit_events_force_uncached)
        return unit_dispatch_authored_abilities_uncached(ent,msg,payload,stop_first,include_innate,include_channel);
#endif
    unitEventPlan_t const *plan = prepared ? prepared : unit_event_plan(ent,msg,include_innate);
    bool validate_fresh = true;
    if (!plan) return unit_dispatch_authored_abilities_uncached(ent,msg,payload,stop_first,include_innate,include_channel);
#define UNIT_EVENT_RESULT(result_) do { \
    intptr_t const result = (result_); \
    if (msg == A_ISSUED_TARGET_ORDER && result != ABILITY_ORDER_UNHANDLED) return result; \
    handled |= result != 0; \
    if (stop_first && handled) return true; \
} while (0)
    if (include_channel && msg == A_MOVE_LEAVE && ent->channel && ent->channel->code) {
        channel_code = ent->channel->code;
        UNIT_EVENT_RESULT(unit_dispatch_ability_code(ent,msg,payload,channel_code,seen,&count,UNIT_EVENT_CAPACITY,false));
    }
    FOR_LOOP(phase, 2) {
      if (phase && (ent->data.UnitAbilities != plan->row ||
            (plan->row && plan->row->abilList != plan->list) ||
            G_UnitDataGeneration() != plan->unit_generation ||
            G_AbilityDataGeneration() != plan->ability_generation)) {
        /* The old dispatcher acquires authored data after implicit callbacks.
         * A type rebind or metadata callback must expose its new list here. */
        unitEventPlan_t const *updated = unit_event_plan(ent,msg,include_innate);
        if (!updated) {
            /* Rare oversized replacement: finish the original live authored
             * enumeration without invoking implicit owners a second time. */
            count = 0;
            if (channel_code) seen[count++] = channel_code;
            if (include_innate) FOR_LOOP(i,num_innate)
                if (innate_items[i].code && count < UNIT_EVENT_CAPACITY &&
                    !unit_event_seen(innate_items[i].code,seen,count)) seen[count++] = innate_items[i].code;
            uint32_t authored_count;
            uint32_t const *authored = G_UnitAbilityCodes(ent->data.UnitAbilities,&authored_count);
            FOR_LOOP(i,authored_count) UNIT_EVENT_RESULT(unit_dispatch_ability_code(ent,msg,payload,
                authored[i],seen,&count,UNIT_EVENT_CAPACITY,true));
            goto runtime_abilities;
        }
        plan = updated;
      }
      uint32_t const end = phase ? plan->count : plan->innate_count;
      for (uint32_t i = phase ? plan->innate_count : 0; i < end; i++) {
#ifdef BZ_TESTS
        if (msg == A_UNIT_INIT) unit_init_steps++;
#endif
        abilityitem_t item = plan->entries[i].item;
        if (item.code && item.code == channel_code) continue;
        if (!plan->entries[i].authored && fresh) {
            if ((validate_fresh && (ent->data.UnitAbilities != plan->row || ent->data.UnitData != plan->data ||
                (plan->row && plan->row->abilList != plan->list) ||
                G_UnitDataGeneration() != plan->unit_generation ||
                G_AbilityDataGeneration() != plan->ability_generation ||
                unit_registry_generation != plan->registry_generation)) ||
                item.ability->proc != plan->entries[i].proc) fresh = false;
            validate_fresh = false;
            if (fresh && plan->entries[i].init_next != i) {
                UNIT_EVENT_RESULT(plan->entries[i].init_result);
                i = plan->entries[i].init_next;
                if (i == end) break;
                item = plan->entries[i].item;
            }
            if (fresh && plan->entries[i].init == UNIT_INIT_RUN) validate_fresh = true;
            else if (plan->entries[i].init != UNIT_INIT_RUN_LOCAL) fresh = false;
        }
        if (plan->entries[i].authored) {
            /* An earlier synchronous callback can change alias metadata or a
             * registry procedure. Only skip a still-valid compiled contract. */
            if (!plan->entries[i].receives && G_AbilityDataGeneration() == plan->ability_generation &&
                item.ability && item.ability->proc == plan->entries[i].proc) continue;
            bool unchanged = ent->data.UnitAbilities == plan->row &&
                (!plan->row || plan->row->abilList == plan->list) && G_UnitDataGeneration() == plan->unit_generation;
            if (unchanged) {
                if (unit_event_seen(item.code,ent->abilities.removed,ARRAY_COUNT(ent->abilities.removed))) continue;
            } else {
#ifdef BZ_TESTS
                authored_event_membership_queries++;
#endif
                if (!G_ActorHasAbilityCode(ent,item.code)) continue;
            }
            if (G_AbilityDataGeneration() != plan->ability_generation) item = S_AbilityItem(item.code);
            if (!item.ability) continue;
        }
#ifdef BZ_TESTS
        if (plan->entries[i].authored) authored_event_visits++;
        else innate_event_visits++;
#endif
        abilityCall_t call = payload ? *payload : MAKE(abilityCall_t,0);
        call.item = &item;
        UNIT_EVENT_RESULT(S_AbilityMessage(ent,msg,&call));
      }
    }
    /* All implicit rawcodes claim deduplication, including unsubscribed hooks.
     * Runtime additions/ranks keep the original live enumeration and queries. */
    if (!ARRAY_COUNT(ent->abilities.added)) {
        bool ranked = false;
        FOR_LOOP(i, MAX_HERO_ABILITIES)
            if (ent->heroabilities[i].level && ent->heroabilities[i].code) { ranked = true; break; }
        if (!ranked) return handled;
    }
    count = 0;
    if (channel_code && !unit_event_seen(channel_code,plan->seen,plan->seen_count)) seen[count++] = channel_code;
    memcpy(seen + count, plan->seen, plan->seen_count * sizeof(*seen)); count += plan->seen_count;
runtime_abilities:
    if (msg == A_UNIT_REMOVE) {
        for (int32_t i = (int32_t)ARRAY_COUNT(ent->abilities.added) - 1; i >= 0; i--)
            if (ent->abilities.added[i]) UNIT_EVENT_RESULT(unit_dispatch_ability_code(ent,msg,payload,
                ent->abilities.added[i],seen,&count,UNIT_EVENT_CAPACITY,true));
    } else FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added))
        if (ent->abilities.added[i]) UNIT_EVENT_RESULT(unit_dispatch_ability_code(ent,msg,payload,
            ent->abilities.added[i],seen,&count,UNIT_EVENT_CAPACITY,true));
    FOR_LOOP(i, MAX_HERO_ABILITIES)
        if (ent->heroabilities[i].level && ent->heroabilities[i].code)
            UNIT_EVENT_RESULT(unit_dispatch_ability_code(ent,msg,payload,ent->heroabilities[i].code,
                seen,&count,UNIT_EVENT_CAPACITY,true));
#undef UNIT_EVENT_RESULT
    return handled;
}

static intptr_t unit_dispatch_authored_abilities_mode(edict_t *ent, abilityMsg_t msg,
        abilityCall_t const *payload, bool stop_first, bool include_innate,
        bool include_channel, bool fresh, unitEventPlan_t const *prepared) {
    unit_event_dispatch_depth++;
    intptr_t result = unit_dispatch_authored_abilities_execute(ent, msg, payload,
        stop_first, include_innate, include_channel, fresh, prepared);
    unit_event_dispatch_depth--;
    unit_event_reclaim();
    return result;
}

static intptr_t unit_dispatch_authored_abilities(edict_t *ent, abilityMsg_t msg,
                                                 abilityCall_t const *payload, bool stop_first,
                                                 bool include_innate, bool include_channel) {
    return unit_dispatch_authored_abilities_mode(ent, msg, payload, stop_first, include_innate, include_channel, false, NULL);
}

/*687b30 queries every authored owner and keeps the unsigned minimum.
 * UINT32_MAX means no override; the movement owner supplies the distance fallback. */
uint32_t S_UnitPointOrderPriority(edict_t *ent,uint32_t order,vec2_t const *point) {
    uint32_t minimum=UINT32_MAX;
    abilityCall_t call=MAKE(abilityCall_t,.point_priority={order,point,&minimum});
    unit_dispatch_authored_abilities(ent,A_POINT_ORDER_PRIORITY,&call,false,false,false);
    return minimum;
}

/* Only the zeroed factory uses this entry. Type rebinds and lifecycle cleanup
 * retain full dispatch; ordinary result and rawcode deduplication stay intact. */
bool S_InitFreshUnitAbilities(edict_t *ent) {
    return unit_dispatch_authored_abilities_mode(ent, A_UNIT_INIT, NULL, false, true, false, true, NULL) != 0;
}

/* A prepared definition borrows the ordered ability plan. Clearing/rebuilding
 * registration advances the epoch before freeing plans, so stale pointers are
 * never dereferenced by a subsequent constructor. */
bool S_InitPreparedUnitAbilities(edict_t *ent, unitRuntimeType_t *type) {
    unitEventPlan_t const *plan = type->initialization_epoch == unit_initialization_epoch ? type->initialization : NULL;
    UnitAbilities_t const *row = ent->data.UnitAbilities;
    if (!plan || plan->row != row || plan->data != ent->data.UnitData || (row && plan->list != row->abilList) ||
        plan->unit_generation != G_UnitDataGeneration() || plan->ability_generation != G_AbilityDataGeneration()) {
        plan = unit_event_plan(ent, A_UNIT_INIT, true);
        type->initialization = plan;
        type->initialization_epoch = unit_initialization_epoch;
    }
    return unit_dispatch_authored_abilities_mode(ent, A_UNIT_INIT, NULL, false, true, false, true, plan) != 0;
}

/* Engine-owned policies opt into generic lifecycle/order notifications on their registry row. */
#ifdef BZ_TESTS
static uint32_t engine_event_visits;
#endif
static bool unit_dispatch_engine_event_abilities(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    bool handled = false;
    FOR_LOOP(i, num_engine_events) {
        if (!engine_event_receives(i, msg)) continue;
#ifdef BZ_TESTS
        engine_event_visits++;
#endif
        abilityitem_t const *item = engine_event_items + i;
        abilityCall_t event_call;
        event_call = MAKE(abilityCall_t, .item = item);
        if (call) event_call = *call;
        event_call.item = item;
        handled |= S_AbilityMessage(ent, msg, &event_call) != 0;
    }
    return handled;
}

/* Unit-data abilities exist independently of command-card slots. Notifications visit every owner;
 * idle, acquisition, and ability queries stop when an owner consumes the decision. */
bool S_UnitAbilityEventWithCall(edict_t *ent, abilityMsg_t msg, abilityCall_t const *payload) {
    bool handled = false;

    if (!ent) return false;
    if (msg == A_UNIT_TYPE_CHANGING || msg == A_UNIT_TYPE_CHANGED)
        return unit_dispatch_engine_event_abilities(ent, msg, payload);
    if (msg == A_AUTO_COMBAT_START || msg == A_AUTO_COMBAT_END || msg == A_UNIT_STAND || msg == A_DEATH ||
        msg == A_UNIT_REMOVING || msg == A_UNIT_RETIRE || msg == A_UNIT_REMOVE)
        handled |= unit_dispatch_engine_event_abilities(ent, msg, payload);
    if (msg == A_REQUIREMENTS_CHANGED)
        return unit_dispatch_authored_abilities(ent, msg, payload, false, false, false) != 0;
    if (msg == A_UNIT_INIT)
        return unit_dispatch_authored_abilities(ent, msg, payload, false, true, false) != 0;
    if (msg == A_MOVE_LEAVE || msg == A_DEATH || msg == A_UNIT_RETIRE || msg == A_UNIT_REMOVE || msg == A_UNIT_REMOVING)
        return unit_dispatch_authored_abilities(ent, msg, payload, false,
                                                 msg != A_DEATH, msg == A_MOVE_LEAVE) != 0 || handled;
    if (msg == A_NATURAL_MANA_REGEN_BLOCKED)
        return unit_dispatch_authored_abilities(ent, msg, payload, true, false, false) != 0;

    FOR_LOOP(i, num_innate) {
        if (!innate_receives(i, msg)) continue;
        abilityCall_t call = payload ? *payload : MAKE(abilityCall_t, 0);
        call.item = innate_items + i;
        handled |= S_AbilityMessage(ent, msg, &call) != 0;
        if (handled && (msg == A_IDLE || msg == A_NO_ACQUIRE || msg == A_NO_RETALIATE)) break;
    }
    return handled;
}

void S_UnitCombatAlert(edict_t *unit,edict_t *source,uint32_t flags) {
    if(!unit)return;
    abilityCall_t call={.combat_alert={source,flags}};
    unit_dispatch_engine_event_abilities(unit,A_COMBAT_ALERT,&call);
    unit_dispatch_authored_abilities(unit,A_COMBAT_ALERT,&call,false,false,false);
}

void S_UnitAllyCombatAlert(edict_t *unit,edict_t *victim,edict_t *source) {
    if(!unit)return;
    abilityCall_t call={.combat_alert={.source=source,.victim=victim,.flags=4}};
    unit_dispatch_engine_event_abilities(unit,A_ALLY_COMBAT_ALERT,&call);
    unit_dispatch_authored_abilities(unit,A_ALLY_COMBAT_ALERT,&call,false,false,false);
}

/* The target publishes its state first. Each intrinsic owner maintains its
 * own subscriptions rather than discovering followers by scanning scenery. */
void S_UnitTargetLost(edict_t *target) {
    abilityCall_t call={.lost_target=target};
    unit_dispatch_engine_event_abilities(NULL,A_TARGET_LOST,&call);
}

void S_UnitTargetOwnerChanged(edict_t *target) {
    abilityCall_t call={.lost_target=target};
    unit_dispatch_engine_event_abilities(NULL,A_TARGET_OWNER_CHANGED,&call);
}

/* Notify active behavior owners after semantic removal, before deferred memory reclamation. */
void S_UnitTargetRemoved(edict_t *target) {
    abilityCall_t call = MAKE(abilityCall_t, .removed_target = target);
    uint32_t count = globals.num_edicts;

    /* Move's retained parent can be subscribed while a different ability owns
     * the active task. Retire that subscription before generic owner cleanup. */
    S_UnitTargetLost(target);
    FOR_LOOP(i, count) {
        edict_t *ent = globals.edicts + i;
        if (ent == target || !ent->inuse || G_IsDeferredFree(ent) || !ent->currentmove || !ent->currentmove->proc)
            continue;
        ent->currentmove->proc(ent, A_TARGET_REMOVED, &call);
    }
}

bool S_UnitAbilityEvent(edict_t *ent, abilityMsg_t msg) {
    return S_UnitAbilityEventWithCall(ent, msg, NULL);
}

void S_UnitAbilityMoveLeave(edict_t *ent, abilityProc_t next_move_proc) {
    abilityCall_t call = MAKE(abilityCall_t, .next_move_proc = next_move_proc);
    if (ent)
        unit_dispatch_authored_abilities(ent, A_MOVE_LEAVE, &call, false, true, true);
}

/* Raw state replacement still retires intrinsic behavior ownership. Keep the
 * authored animation/leave policy in unit_setmove; indexed owners cannot rely
 * on that wrapper being used by every forced transition. */
void S_UnitAbilityMoveChanged(edict_t *ent,abilityProc_t next_move_proc) {
    abilityCall_t call={.next_move_proc=next_move_proc};
    unit_dispatch_engine_event_abilities(ent,A_MOVE_LEAVE,&call);
}

bool S_UnitAbilityMoveArrive(edict_t *ent) {
    return ent && unit_dispatch_authored_abilities(ent, A_MOVE_ARRIVE, NULL, true, false, false) != 0;
}

abilityOrderResult_t S_UnitIssuedTargetOrder(edict_t *issuer, cstring_t order, edict_t *target) {
    abilityCall_t call = MAKE(abilityCall_t, .issued_target_order = { target, order });
    if (!issuer || !order || !target || !target->inuse) return ABILITY_ORDER_UNHANDLED;
    /* Intrinsic command owners are registered independently of authored SLK
     * ability lists. Dispatch their concrete order before authored fallback. */
    ability_t const *owner=FindAbilityByOrder(order);
    if (owner && (owner->flags&AB_ENGINE_EVENTS)) {
        abilityitem_t item={.ability=owner};
        call.item=&item;
        abilityOrderResult_t result=S_AbilityMessage(issuer,A_ISSUED_TARGET_ORDER,&call);
        if (result!=ABILITY_ORDER_UNHANDLED) return result;
        call.item=NULL;
    }
    return (abilityOrderResult_t)unit_dispatch_authored_abilities(
        issuer, A_ISSUED_TARGET_ORDER, &call, true, false, false);
}

/* Accepted instant/spell orders can leave the current movement object untouched.
 * Give registered policies and innate behavior owners a generic post-accept hook. */
bool S_UnitAbilityOrderAccepted(edict_t *ent, cstring_t order) {
    bool handled = false;
    abilityCall_t call = MAKE(abilityCall_t, .order = order);
    if (!ent || !order) return false;
    handled |= unit_dispatch_engine_event_abilities(ent, A_ORDER_ACCEPTED, &call);
    FOR_LOOP(i, num_innate) {
        if (!innate_receives(i, A_ORDER_ACCEPTED)) continue;
        abilityCall_t call = MAKE(abilityCall_t, .item = innate_items + i, .order = order);
        handled |= S_AbilityMessage(ent, A_ORDER_ACCEPTED, &call) != 0;
    }
    return handled;
}

/* Queued work is returned to the procedure that owns its order. The stored
 * order_id remains the concrete rawcode payload for that ability. */
queuedOrderResult_t S_UnitQueuedOrderEvent(edict_t *ent, unitOrder_t const *queued, abilityMsg_t msg) {
    ability_t const *ability;
    abilityitem_t item;
    abilityCall_t call;

    if (!ent || !queued || (msg != A_QUEUE_ORDER_START && msg != A_QUEUE_ORDER_CANCEL)) return false;
    /*673e80 retains the order through event delivery, releasing it afterward.
     * Queue growth or nested creation may recycle its old ring allocation. */
    unitOrder_t retained=*queued;
    queued=&retained;
    ability = FindAbilityByOrder(queued->order);
    if (!ability || !ability->proc) return false;
    item = MAKE(abilityitem_t, .code = queued->order_id, .ability = ability);
    call = MAKE(abilityCall_t, .item = &item, .queued_order = queued);
    return (queuedOrderResult_t)S_AbilityMessage(ent, msg, &call);
}

static bool unit_target_ability_try(edict_t *target, edict_t *issuer, cstring_t order, uint32_t code,
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
bool S_UnitTargetAbilityOrder(edict_t *target, edict_t *issuer, cstring_t order) {
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
bool S_UnitProjectileHit(edict_t *projectile) {
    edict_t *target = projectile ? projectile->goalentity : NULL;
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

#ifdef BZ_TESTS
static uint32_t ability_name_comparisons;
#endif

ability_t const *FindAbilityByClassname(cstring_t classname) {
    uint32_t slot = ability_name_hash(classname);
    while (ability_names[slot]) {
        ability_t const *entry = abilitylist + ability_names[slot] - 1;
#ifdef BZ_TESTS
        ability_name_comparisons++;
#endif
        if (!strcmp(entry->classname, classname)) return entry;
        slot = (slot + 1) & (ABILITY_NAME_SLOTS - 1);
    }
    return NULL;
}

/* Command-card names use two namespaces. Engine commands (CmdBuild, CmdMove,
 * etc.) are full strings registered directly in abilitylist. WC3 abilities are
 * four-character rawcodes whose AbilityData alias may point at a base handler.
 * Only rawcodes belong in the SLK resolver: passing CmdBuild through FS_SLKKey
 * truncates it to CmdB and loses the registered build command. */
ability_t const *FindAbilityForCommand(cstring_t classname) {
    ability_t const *ability;

    if (!classname || !*classname) {
        return NULL;
    }
    if (strlen(classname) != 4) {
        return FindAbilityByClassname(classname);
    }
    /* Prefer a concrete rawcode registration before following AbilityData's
     * `code` alias. Some item abilities inherit a spell's data (for example
     * APrl/APrr from AHre) but have their own runtime class and pickup flags. */
    ability = FindAbilityByClassname(classname);
    if (ability) {
        return ability;
    }
    return FindAbilityByClassname(GetClassName(G_AbilityCodeName(classname)));
}

/* Keep the requested rawcode even when AbilityData resolves its code to a shared implementation. */
abilityitem_t S_AbilityItem(uint32_t code) {
    if(!code)return MAKE(abilityitem_t,0);
    uint32_t hash=code^(code>>16),generation=G_AbilityDataGeneration();
    hash*=0x7feb352du;hash^=hash>>15;
    abilityAliasCache_t *cached=ability_alias_cache+(hash&(ABILITY_NAME_SLOTS-1));
    if(!cached->valid || cached->code!=code || cached->generation!=generation) {
        /* Preserve the complete string resolver on a miss, including short
         * malformed IDs, aliases, unknown results, and first registry rows. */
        *cached=(abilityAliasCache_t){code,generation,FindAbilityForCommand(GetClassName(code)),true};
    }
    return MAKE(abilityitem_t,.code=code,.ability=cached->ability);
}

/* Dispatch is synchronous and retains the concrete row and authored rawcode in the typed payload. */
#ifdef BZ_TESTS
static uint32_t move_bonus_messages;
uint32_t S_TestMoveBonusMessages(bool reset) {
    uint32_t result=move_bonus_messages;
    if(reset)move_bonus_messages=0;
    return result;
}
#endif

BZ_ABILITY_PROC(S_AbilityMessage) {
#ifdef BZ_TESTS
    if(msg==A_MOVE_SPEED_BONUS)move_bonus_messages++;
#endif
    ability_t const *ability = call && call->item ? call->item->ability : NULL;
    bool activating = msg == A_COMMAND || msg == A_ORDER || msg == A_POINT_ORDER_ADMIT ||
                      msg == A_POINT_ORDER || msg == A_VALIDATE || msg == A_EXECUTE ||
                      msg == A_AUTOCAST_ACQUIRE || (msg == A_AUTOCAST_SET && call && call->enabled);
    if (activating && ability && (ability->flags & (AB_COMMAND | AB_SPELL | AB_AUTOCAST))) {
        uint32_t const code = call && call->item ? call->item->code : 0;
        if ((ability->flags & AB_SPELL) && code &&
            !G_UnitAbilityResearchAvailable(ent, code)) return false;
        if ((code && !G_IsUnitAbilityAvailable(ent, code)) ||
            !S_AncientAbilityAvailable(ent, ability)) return false;
    }
    return ability && ability->proc ? ability->proc(ent, msg, call) : false;
}

bool S_UnitAbilityMessage(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    uint32_t seen[MAX_ABILITIES * 2 + MAX_HERO_ABILITIES] = {0}, count = 0;
    bool aggregate = msg == A_MOVE_SPEED_BONUS, handled = false;
    if (!ent) return false;
    FOR_LOOP(i, num_innate) {
        if (aggregate && !(innate_items[i].ability->flags & AB_MOVE_SPEED_BONUS)) continue;
        abilityCall_t invoke = call ? *call : MAKE(abilityCall_t, 0);
        invoke.item = innate_items + i;
        if (S_AbilityMessage(ent, msg, &invoke)) {
            handled = true;
            if (!aggregate) return true;
        }
    }
#define DISPATCH_UNIT_ABILITY(code_) do { \
        uint32_t const code = (code_); bool duplicate = false; \
        FOR_LOOP(k, count) if (seen[k] == code) { duplicate = true; break; } \
        if (code && !duplicate && count < sizeof(seen) / sizeof(seen[0])) { \
            seen[count++] = code; \
            abilityitem_t item = S_AbilityItem(code); \
            if (item.ability && (!aggregate || (item.ability->flags & AB_MOVE_SPEED_BONUS)) && \
                G_ActorHasAbilityCode(ent, code)) { \
                abilityCall_t invoke = call ? *call : MAKE(abilityCall_t, 0); \
                invoke.item = &item; \
                if (S_AbilityMessage(ent, msg, &invoke)) { \
                    handled = true; if (!aggregate) return true; \
                } \
            } \
        } \
    } while (0)
    uint32_t authored_count;
    uint32_t const *authored = G_UnitAbilityCodes(ent->data.UnitAbilities, &authored_count);
    FOR_LOOP(i, authored_count) DISPATCH_UNIT_ABILITY(authored[i]);
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added))
        if (ent->abilities.added[i]) DISPATCH_UNIT_ABILITY(ent->abilities.added[i]);
    FOR_LOOP(i, MAX_HERO_ABILITIES)
        if (ent->heroabilities[i].level && ent->heroabilities[i].code)
            DISPATCH_UNIT_ABILITY(ent->heroabilities[i].code);
#undef DISPATCH_UNIT_ABILITY
    return handled;
}

void S_EnableAbility(edict_t *ent, uint32_t code) {
    S_MarkAuraSource(ent);
    abilityitem_t item = S_AbilityItem(code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    if (item.ability) S_AbilityMessage(ent, A_ENABLE, &call);
}

void S_DisableAbility(edict_t *ent, uint32_t code) {
    S_MarkAuraSource(ent);
    if (ent && ent->autocast_code == code) G_SetUnitAutocast(ent, code, false);
    abilityitem_t item = S_AbilityItem(code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    if (item.ability) S_AbilityMessage(ent, A_DISABLE, &call);
}

void S_RefreshAbilityLevel(edict_t *ent, ability_t const *ability) {
    abilityitem_t item = MAKE(abilityitem_t, .ability = ability);
    abilityCall_t query = MAKE(abilityCall_t, .item = &item);
    abilityCall_t changed;
    if (!ability || !ability->proc) return;
    changed = MAKE(abilityCall_t, .item = &item, .level = (uint32_t)S_AbilityMessage(ent, A_LEVEL, &query));
    S_AbilityMessage(ent, A_LEVEL_CHANGED, &changed);
}

/* The selected rawcode is shared state; each procedure owns its autocast policy and side effects. */
bool G_UnitAutocastIsOn(edict_t *ent, uint32_t code) {
    abilityitem_t item = S_AbilityItem(code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    return ent && code && ent->autocast_code == code && item.ability && (item.ability->flags & AB_AUTOCAST) &&
        G_UnitAbilityLevel(ent, code) && S_AbilityMessage(ent, A_AUTOCAST_ON, &call);
}

/* Keeping the alias through the UI and scheduler preserves authored cost, range and effect data. */
bool G_SetUnitAutocast(edict_t *ent, uint32_t code, bool enabled) {
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
bool G_TryUnitAutocast(edict_t *ent) {
    if (!ent || !(ent->aiflags & AI_AUTOCAST_ACTIVE)) return false;
    abilityitem_t item = S_AbilityItem(ent->autocast_code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    return G_UnitAutocastIsOn(ent, item.code) && S_AbilityMessage(ent, A_AUTOCAST_ACQUIRE, &call);
}

uint32_t FindAbilityIndex(cstring_t classname) {
    ability_t const *entry = FindAbilityByClassname(classname);
    return entry ? (uint32_t)(entry - abilitylist) : 255;
}

/* Shared casts and bespoke commands expose the same capability to HUD and item callers. */
bool S_AbilityHasCommand(ability_t const *ability) {
    return ability && ability->proc && (ability->flags & (AB_SPELL | AB_COMMAND));
}

/* The shared-cast bit owns dispatch; procedures can override command handling
 * and delegate the message to CAbilitySimpleSpell when it is not specialized. */
void S_AbilityCommand(edict_t *clent, ability_t const *ability) {
    abilityitem_t item;
    abilityCall_t call;

    if (!S_AbilityHasCommand(ability)) return;
    item = MAKE(abilityitem_t, .code = clent->client->menu.ability_code, .ability = ability);
    call = MAKE(abilityCall_t, .item = &item, .client = clent);
    S_AbilityMessage(G_GetMainSelectedUnit(clent->client), A_COMMAND, &call);
}

void InitAbilities(void) {
    S_ClearUnitEventPlans();
    game.num_abilities = sizeof(abilitylist)/sizeof(abilitylist[0]);
    ability_build_names(); /* Ready before any procedure's A_INIT callback. */
    num_updates = num_owner_updates = num_timer_updates = 0;
    num_innate = 0;
    num_engine_events = 0;
    num_ability_index_procs = 0;
    FOR_LOOP(i, game.num_abilities) {
        ability_t *entry = &abilitylist[i];
        uint32_t n;
        abilityitem_t item = MAKE(abilityitem_t, .code = strlen(entry->classname) == 4 ? FS_SLKKey(entry->classname) : 0,
                                  .ability = entry);
        abilityCall_t call = MAKE(abilityCall_t, .item = &item, .classname = entry->classname);
        if (!entry->proc) gi.error("InitAbilities: %s has no procedure", entry->classname);
        entry->proc(NULL, A_INIT, &call);
        abilityMessageSet_t subscriptions;
        if (entry->flags & (AB_INNATE | AB_ENGINE_EVENTS)) {
            abilityCall_t query = MAKE(abilityCall_t, .item = &item, .unit_messages = &subscriptions);
            /* Subscription is optional. A procedure that does not answer the
             * query retains the original complete broadcast contract. */
            memset(&subscriptions, 0xff, sizeof(subscriptions));
            if (!entry->proc(NULL, A_UNIT_EVENT_MASK, &query))
                memset(&subscriptions, 0xff, sizeof(subscriptions));
        }
        if (entry->flags & AB_INNATE) {
            innate_messages[num_innate] = subscriptions;
            innate_message_procs[num_innate] = entry->proc;
            innate_items[num_innate++] = item;
        }
        if (entry->flags & AB_ENGINE_EVENTS) {
            engine_event_messages[num_engine_events] = subscriptions;
            engine_event_procs[num_engine_events] = entry->proc;
            engine_event_items[num_engine_events++] = MAKE(abilityitem_t, .ability = entry);
        }
        if (entry->flags & AB_UPDATE) {
            for (n = 0; n < num_updates && ability_updates[n] != entry->proc; n++) {}
            if (n == num_updates) {
                ability_update_items[num_updates] = item;
                ability_updates[num_updates++] = entry->proc;
            }
        }
        if (entry->flags & AB_OWNER_UPDATE) {
            for (n = 0; n < num_owner_updates && owner_updates[n] != entry->proc; n++) {}
            if (n == num_owner_updates) owner_updates[num_owner_updates++] = entry->proc;
        }
        if (entry->flags & AB_PRIMARY_TIMER) {
            for (n = 0; n < num_timer_updates && timer_updates[n] != entry->proc; n++) {}
            if (n == num_timer_updates) timer_updates[num_timer_updates++] = entry->proc;
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

#ifdef BZ_TESTS
#include "shared/test.h"
TEST(wc3_ability_dispatch, persistent_updates_prune_unowned_procedures) {
    UnitAbilities_t row = {.abilList = "Amov,Aatk"};
    edict_t unit = {.data.UnitAbilities = &row}, before = unit;
    unit_update_calls = 0;
    S_RunAbilityUpdates(&unit);
    T_EQ(unit_update_calls, 0);
    T_EQ(memcmp(&unit, &before, sizeof(unit)), 0);
    unit_updates_force_uncached = true;
    unit_update_calls = 0;
    S_RunAbilityUpdates(&unit);
    T_EQ(unit_update_calls, num_updates);
    T_EQ(memcmp(&unit, &before, sizeof(unit)), 0);
    unit_updates_force_uncached = false;
}

static uint32_t update_test_mode, update_test_trace[8], update_test_count;
static UnitAbilities_t update_test_rows[2] = {{.abilList = "Amov"}, {.abilList = "Aroo"}};
static raven_t update_test_raven;
static intptr_t update_test_first(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    (void)call;
    if (msg == A_UNIT_TYPE_UPDATE) return UNIT_UPDATE_RUN;
    if (msg != A_UPDATE) return 0;
    update_test_trace[update_test_count++] = 1;
    if (update_test_mode == 1) ent->data.UnitAbilities = update_test_rows + 1;
    if (update_test_mode == 2) ent->raven = &update_test_raven;
    if (update_test_mode == 3) ent->abilities.added[ent->abilities.added_count++] = MAKEFOURCC('A','r','o','o');
    if (update_test_mode == 4) S_ClearUnitEventPlans();
    if (update_test_mode == 5) ent->heroabilities[0] = (heroability_t){MAKEFOURCC('A','r','o','o'),1};
    return 0;
}
static intptr_t update_test_second(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    if (msg == A_UNIT_TYPE_UPDATE) return call->unit_type == update_test_rows + 1 ? UNIT_UPDATE_RUN : UNIT_UPDATE_SKIP;
    if (msg == A_UPDATE && (ent->data.UnitAbilities == update_test_rows + 1 ||
        ARRAY_COUNT(ent->abilities.added) || ent->heroabilities[0].level))
        update_test_trace[update_test_count++] = 2;
    return 0;
}
static intptr_t update_test_state(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    (void)call;
    if (msg == A_UNIT_TYPE_UPDATE) return UNIT_UPDATE_POINTER(raven);
    if (msg == A_UPDATE && ent->raven) update_test_trace[update_test_count++] = 3;
    return 0;
}
static intptr_t update_test_unknown(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    (void)ent; (void)call;
    if (msg == A_UPDATE) update_test_trace[update_test_count++] = 4;
    return 0; /* No declared update policy: preserve its observable callback. */
}
TEST(wc3_ability_dispatch, persistent_update_plans_preserve_live_state_and_mutation_order) {
    abilityProc_t saved[sizeof(ability_updates) / sizeof(*ability_updates)];
    abilityitem_t saved_items[sizeof(ability_update_items) / sizeof(*ability_update_items)];
    uint32_t saved_count = num_updates;
    memcpy(saved, ability_updates, sizeof(saved));
    memcpy(saved_items, ability_update_items, sizeof(saved_items));
    ability_t rows[] = {
        {.proc = update_test_first, .flags = AB_TYPE_UPDATE},
        {.proc = update_test_second, .flags = AB_TYPE_UPDATE},
        {.proc = update_test_state, .flags = AB_TYPE_UPDATE},
        {.proc = update_test_unknown, .flags = AB_TYPE_UPDATE},
    };
    S_ClearUnitEventPlans();
    num_updates = 4;
    FOR_LOOP(i, 4) {
        ability_updates[i] = rows[i].proc;
        ability_update_items[i] = (abilityitem_t){.ability = rows + i};
    }
    FOR_LOOP(mode, 6) {
        edict_t initial = {.data.UnitAbilities = update_test_rows};
        edict_t reference = initial, compiled = initial;
        uint32_t expected[8], expected_count;
        update_test_mode = mode; update_test_count = 0;
        unit_updates_force_uncached = true;
        S_RunAbilityUpdates(&reference);
        expected_count = update_test_count;
        memcpy(expected, update_test_trace, sizeof(expected));
        unit_updates_force_uncached = false; update_test_count = 0;
        S_RunAbilityUpdates(&compiled);
        T_EQ(update_test_count, expected_count);
        T_EQ(memcmp(expected, update_test_trace, expected_count * sizeof(*expected)), 0);
        T_EQ(memcmp(&reference, &compiled, sizeof(compiled)), 0);
    }
    S_ClearUnitEventPlans();
    num_updates = saved_count;
    memcpy(ability_updates, saved, sizeof(saved));
    memcpy(ability_update_items, saved_items, sizeof(saved_items));
}

static ability_t const *engine_event_test_rows[sizeof(abilitylist) / sizeof(*abilitylist)];
static uint32_t engine_event_test_count;
static intptr_t engine_event_test_proc(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    T_NOT_NULL(ent);
    T_EQ(msg, A_UNIT_STAND);
    T_EQ(call->item->code, 0);
    T_STREQ(call->order, "probe_order");
    engine_event_test_rows[engine_event_test_count++] = call->item->ability;
    return 1;
}
TEST(wc3_ability_dispatch, engine_events_visit_only_subscribers_in_registry_order) {
    InitAbilities();
    abilityProc_t old[sizeof(abilitylist) / sizeof(*abilitylist)];
    uint32_t count = 0;
    FOR_LOOP(i, game.num_abilities) {
        old[i] = abilitylist[i].proc;
        if (abilitylist[i].flags & AB_ENGINE_EVENTS) {
            S_ReplaceAbilityProcedure(abilitylist + i, engine_event_test_proc);
            count++;
        }
    }
    edict_t unit = { 0 };
    abilityCall_t call = { .order = "probe_order" };
    engine_event_visits = 0;
    FOR_LOOP(pass, 16) {
        engine_event_test_count = 0;
        T_ASSERT(unit_dispatch_engine_event_abilities(&unit, A_UNIT_STAND, &call));
        T_EQ(engine_event_test_count, count);
        uint32_t at = 0;
        FOR_LOOP(i, game.num_abilities)
            if (abilitylist[i].flags & AB_ENGINE_EVENTS)
                T_EQ(engine_event_test_rows[at++], abilitylist + i);
    }
    T_EQ(engine_event_visits, count * 16);
    FOR_LOOP(i, game.num_abilities) S_ReplaceAbilityProcedure(abilitylist + i, old[i]);
    InitAbilities();
}
TEST(wc3_ability_dispatch, fresh_init_matches_full_dispatch_for_empty_owners) {
    InitAbilities();
    cstring_t lists[] = { "AInv,Adef", "Ashm", "Ahid", "Apiv" };
    UnitData_t data = {0};
    FOR_LOOP(i, sizeof(lists) / sizeof(*lists)) {
        UnitAbilities_t row = { .abilList = lists[i] };
        edict_t original, fresh;
        memset(&original, 0, sizeof(original));
        original.data.UnitAbilities = &row; original.data.UnitData = &data;
        original.s.player = 3;
        fresh = original;
        innate_event_visits = 0;
        bool expected = S_UnitAbilityEvent(&original, A_UNIT_INIT);
        uint32_t full_visits = innate_event_visits;
        innate_event_visits = unit_init_steps = 0;
        T_EQ(S_InitFreshUnitAbilities(&fresh), expected);
        T_ASSERT(unit_init_steps < unit_event_plan(&fresh, A_UNIT_INIT, true)->count);
        T_EQ(memcmp(&original, &fresh, sizeof(fresh)), 0);
        T_ASSERT(innate_event_visits < full_visits);
        T_EQ(fresh.movement.fine_class, 3);
    }
    InitAbilities();
}

TEST(wc3_ability_dispatch, fresh_plan_uses_unit_data_and_ownership_as_separate_inputs) {
    InitAbilities();
    UnitAbilities_t row = { .abilList = "" };
    UnitData_t awake = {0}, sleeper = { .canSleep = true };
    edict_t left = { .data.UnitAbilities = &row, .data.UnitData = &awake };
    edict_t right = { .data.UnitAbilities = &row, .data.UnitData = &sleeper };
    T_ASSERT(unit_event_plan(&left, A_UNIT_INIT, true) != unit_event_plan(&right, A_UNIT_INIT, true));
    S_InitFreshUnitAbilities(&left);
    S_InitFreshUnitAbilities(&right);
    T_NULL(left.sleep); T_NOT_NULL(right.sleep); T_ASSERT(right.sleep->can_sleep);
    G_FreeSleep(&right);
    /* A rebind with identical authored abilities must acquire the new data
     * dependency, including through an already borrowed runtime definition. */
    unitRuntimeType_t type = {0};
    left = (edict_t){ .data.UnitAbilities = &row, .data.UnitData = &awake };
    S_InitPreparedUnitAbilities(&left, &type);
    void const *original = type.initialization;
    left.data.UnitData = &sleeper;
    S_InitPreparedUnitAbilities(&left, &type);
    T_NE(type.initialization, original);
    T_NOT_NULL(left.sleep); T_ASSERT(left.sleep->can_sleep);
    G_FreeSleep(&left);
    InitAbilities();
}

TEST(wc3_ability_dispatch, fresh_init_keeps_existing_shadow_cleanup) {
    InitAbilities();
    UnitAbilities_t row = { .abilList = "" };
    UnitData_t data = {0};
    shadowMeld_t left = { .active = true, .fading = true, .fade_start = 123 };
    shadowMeld_t right = left;
    edict_t original = { .data.UnitAbilities = &row, .data.UnitData = &data, .shadowmeld = &left };
    edict_t fresh = original; fresh.shadowmeld = &right;
    bool expected = S_UnitAbilityEvent(&original, A_UNIT_INIT);
    T_EQ(S_InitFreshUnitAbilities(&fresh), expected);
    T_EQ(memcmp(&left, &right, sizeof(left)), 0);
    T_ASSERT(!right.active && !right.fading);
    T_EQ(right.fade_start, 0);
    InitAbilities();
}

static uint32_t fresh_unknown_calls;
static intptr_t fresh_unknown_initializer(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    /* A replacement that lacks the type contract must never receive the
     * speculative null-unit query and may mutate subsequent owner state. */
    T_NOT_NULL(ent); T_EQ(msg, A_UNIT_INIT); T_NOT_NULL(call);
    fresh_unknown_calls++;
    ent->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;
    ent->permanent_invisibility_fade.request.active = true;
    return false;
}
TEST(wc3_ability_dispatch, fresh_init_preserves_unknown_callback_mutations) {
    InitAbilities();
    UnitAbilities_t row = { .abilList = "" };
    UnitData_t data = {0};
    edict_t original = { .data.UnitAbilities = &row, .data.UnitData = &data };
    edict_t fresh = original;
    /* Warm a plan, then replace its first procedure. Remaining callbacks
     * must see the foreign mutation even when their old type policy skips. */
    unit_event_plan(&fresh, A_UNIT_INIT, true);
    ability_t *move = (ability_t *)FindAbilityByClassname("Amov");
    abilityProc_t saved_move = move->proc;
    S_ReplaceAbilityProcedure(move, fresh_unknown_initializer);
    fresh_unknown_calls = 0;
    bool expected = S_UnitAbilityEvent(&original, A_UNIT_INIT);
    T_EQ(S_InitFreshUnitAbilities(&fresh), expected);
    T_EQ(fresh_unknown_calls, 2);
    T_EQ(memcmp(&original, &fresh, sizeof(fresh)), 0);
    T_EQ(fresh.runtime.flags & UNIT_BALANCE_PERMANENT_INVISIBLE, 0);
    T_EQ(fresh.permanent_invisibility_fade.request.active, 0);
    S_ReplaceAbilityProcedure(move, saved_move);
    InitAbilities();
    /* A cold plan must not query the replacement either. */
    ability_t *blight = (ability_t *)FindAbilityByClassname("Abli");
    abilityProc_t saved_blight = blight->proc;
    S_ReplaceAbilityProcedure(blight, fresh_unknown_initializer);
    original = (edict_t){ .data.UnitAbilities = &row, .data.UnitData = &data };
    fresh = original; fresh_unknown_calls = 0;
    expected = S_UnitAbilityEvent(&original, A_UNIT_INIT);
    T_EQ(S_InitFreshUnitAbilities(&fresh), expected);
    T_EQ(fresh_unknown_calls, 2);
    T_EQ(memcmp(&original, &fresh, sizeof(fresh)), 0);
    T_EQ(fresh.runtime.flags & UNIT_BALANCE_PERMANENT_INVISIBLE, 0);
    S_ReplaceAbilityProcedure(blight, saved_blight);
    InitAbilities();
}

static abilityProc_t fresh_barrier_parent;
static uint32_t fresh_barrier_calls;
static intptr_t fresh_barrier_initializer(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    if (msg == A_UNIT_TYPE_INIT) return UNIT_INIT_RUN;
    if (msg == A_UNIT_INIT) {
        fresh_barrier_calls++;
        S_ReplaceAbilityProcedure(FindAbilityByClassname("Abli"), fresh_unknown_initializer);
    }
    return fresh_barrier_parent(ent, msg, call);
}

TEST(wc3_ability_dispatch, fresh_plan_registry_barrier_observes_later_replacement_once) {
    InitAbilities();
    ability_t const *move = FindAbilityByClassname("Amov"), *blight = FindAbilityByClassname("Abli");
    fresh_barrier_parent = move->proc;
    abilityProc_t old_blight = blight->proc;
    S_ReplaceAbilityProcedure(move, fresh_barrier_initializer);
    InitAbilities();
    UnitAbilities_t row = { .abilList = "" };
    UnitData_t data = {0};
    edict_t original = { .data.UnitAbilities = &row, .data.UnitData = &data }, fresh = original;
    fresh_barrier_calls = fresh_unknown_calls = 0;
    bool expected = S_UnitAbilityEvent(&original, A_UNIT_INIT);
    T_EQ(fresh_barrier_calls, 1); T_EQ(fresh_unknown_calls, 1);
    S_ReplaceAbilityProcedure(blight, old_blight);
    fresh_barrier_calls = fresh_unknown_calls = 0;
    T_EQ(S_InitFreshUnitAbilities(&fresh), expected);
    T_EQ(fresh_barrier_calls, 1); T_EQ(fresh_unknown_calls, 1);
    T_EQ(memcmp(&original, &fresh, sizeof(fresh)), 0);
    S_ReplaceAbilityProcedure(move, fresh_barrier_parent);
    S_ReplaceAbilityProcedure(blight, old_blight);
    InitAbilities();
}

static abilityProc_t fresh_clear_parent;
static bool fresh_clear_nested;
static uint32_t fresh_clear_calls;
static intptr_t fresh_clear_initializer(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    if (msg == A_UNIT_TYPE_INIT) return UNIT_INIT_RUN;
    if (msg == A_UNIT_INIT) {
        fresh_clear_calls++;
        S_ClearUnitEventPlans();
        if (!unit_events_force_uncached) T_NOT_NULL(unit_event_retired);
        if (!fresh_clear_nested) {
            fresh_clear_nested = true;
            edict_t child = {.data = ent->data, .s.player = 7};
            unitRuntimeType_t type = {0};
            S_InitPreparedUnitAbilities(&child, &type);
            T_EQ(child.movement.fine_class, 7);
            fresh_clear_nested = false;
        }
    }
    return fresh_clear_parent(ent, msg, call);
}

TEST(wc3_ability_dispatch, initialization_clear_retains_outer_and_nested_continuations) {
    InitAbilities();
    ability_t const *move = FindAbilityByClassname("Amov");
    fresh_clear_parent = move->proc;
    S_ReplaceAbilityProcedure(move, fresh_clear_initializer);
    UnitAbilities_t row = {.abilList = ""};
    UnitData_t data = {0};
    edict_t original = {.data.UnitAbilities = &row, .data.UnitData = &data}, compiled = original;
    fresh_clear_calls = 0;
    unit_events_force_uncached = true;
    bool expected = S_InitFreshUnitAbilities(&original);
    T_EQ(fresh_clear_calls, 2);
    unit_events_force_uncached = false;
    unitRuntimeType_t type = {0};
    FOR_LOOP(pass, 2) {
        compiled = (edict_t){.data.UnitAbilities = &row, .data.UnitData = &data};
        fresh_clear_calls = 0;
        T_EQ(S_InitPreparedUnitAbilities(&compiled, &type), expected);
        T_EQ(fresh_clear_calls, 2);
        T_EQ(memcmp(&compiled, &original, sizeof(compiled)), 0);
        T_EQ(unit_event_dispatch_depth, 0);
        T_NULL(unit_event_retired);
    }
    S_ReplaceAbilityProcedure(move, fresh_clear_parent);
    InitAbilities();
}

TEST(wc3_ability_dispatch, engine_stand_visits_only_the_hold_owner) {
    InitAbilities();
    edict_t unit = {0};
    engine_event_visits = 0;
    FOR_LOOP(i, 128) T_ASSERT(!unit_dispatch_engine_event_abilities(&unit, A_UNIT_STAND, NULL));
    T_EQ(engine_event_visits, 128);
}
TEST(wc3_ability_dispatch, innate_stand_dispatch_does_not_visit_unhandled_procedures) {
    InitAbilities();
    edict_t unit = {0};
    innate_event_visits = 0;
    FOR_LOOP(i, 128)
        T_ASSERT(!unit_dispatch_authored_abilities(&unit, A_UNIT_STAND, NULL, false, true, false));
    T_EQ(innate_event_visits, 0);
}
TEST(wc3_ability_dispatch, unchanged_authored_events_do_not_query_membership_again) {
    InitAbilities();
    UnitAbilities_t row = {.abilList = "AInv,Adef,AInv,Adef"};
    edict_t unit = {.data.UnitAbilities = &row};
    unit_dispatch_authored_abilities(&unit, A_REQUIREMENTS_CHANGED, NULL, false, false, false);
    authored_event_membership_queries = 0;
    FOR_LOOP(i, 128)
        unit_dispatch_authored_abilities(&unit, A_REQUIREMENTS_CHANGED, NULL, false, false, false);
    T_EQ(authored_event_membership_queries, 0);
}
static intptr_t authored_subscription_test_proc(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    (void)ent;
    T_EQ(msg, A_UNIT_STAND); T_EQ(call->item->code, FS_SLKKey("Aall"));
    return true;
}
TEST(wc3_ability_dispatch, authored_known_procedures_skip_unhandled_broadcasts) {
    InitAbilities();
    UnitAbilities_t row = {.abilList = "Aall,Aloc,Amov"};
    edict_t unit = {.data.UnitAbilities = &row};
    authored_event_visits = 0;
    FOR_LOOP(i, 128)
        T_ASSERT(!unit_dispatch_authored_abilities(&unit, A_UNIT_STAND, NULL, false, false, false));
    T_EQ(authored_event_visits, 0);
    ability_t *sharing = (ability_t *)FindAbilityByClassname("Aall");
    abilityProc_t saved = sharing->proc;
    S_ReplaceAbilityProcedure(sharing, authored_subscription_test_proc);
    T_ASSERT(unit_dispatch_authored_abilities(&unit, A_UNIT_STAND, NULL, false, false, false));
    T_EQ(authored_event_visits, 1);
    S_ReplaceAbilityProcedure(sharing, saved);
    InitAbilities();
}
static uint32_t event_trace[128], event_trace_count, event_trace_mutation;
static UnitAbilities_t const *event_trace_replacement;
static intptr_t event_trace_proc(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    uint32_t code = call->item->code;
    T_ASSERT(event_trace_count < sizeof(event_trace)/sizeof(*event_trace));
    event_trace[event_trace_count++] = code;
    if (event_trace_mutation == 1 && code == FS_SLKKey("Adef")) {
        ent->abilities.removed[0] = FS_SLKKey("AInv"); ent->abilities.removed_count = 1;
    }
    if (event_trace_mutation == 2 && code == FS_SLKKey("Amov"))
        ent->data.UnitAbilities = event_trace_replacement;
    if (event_trace_mutation == 3 && code == FS_SLKKey("Adef"))
        ent->data.UnitAbilities = event_trace_replacement;
    if (event_trace_mutation == 4 && code == FS_SLKKey("Adef")) {
        ent->abilities.added[0] = FS_SLKKey("Ahea"); ent->abilities.added_count = 1;
    }
    return code == FS_SLKKey("AInv") ? (msg == A_ISSUED_TARGET_ORDER ? ABILITY_ORDER_REJECTED : 1) : 0;
}
TEST(wc3_ability_dispatch, compiled_events_match_live_dispatch_order_results_and_callback_mutations) {
    InitAbilities();
    abilityProc_t saved[sizeof(abilitylist)/sizeof(*abilitylist)];
    FOR_LOOP(i,game.num_abilities) { saved[i] = abilitylist[i].proc; S_ReplaceAbilityProcedure(abilitylist + i, event_trace_proc); }
    UnitAbilities_t original = {.abilList = "Adef,AInv,Adef,ZZZZ,Amov,Ahar"};
    UnitAbilities_t replacement = {.abilList = "Amov,Ahea,Adef"};
    event_trace_replacement = &replacement;
    abilityMsg_t messages[] = {A_UNIT_INIT,A_MOVE_LEAVE,A_UNIT_REMOVE,A_ISSUED_TARGET_ORDER,A_REQUIREMENTS_CHANGED};
    FOR_LOOP(mutation,5) FOR_LOOP(innate,2) FOR_LOOP(stop,2) FOR_LOOP(m,sizeof(messages)/sizeof(*messages)) {
        edict_t initial = {.data.UnitAbilities = &original};
        initial.abilities.added[0] = FS_SLKKey("Ahar"); initial.abilities.added[1] = FS_SLKKey("Ahea");
        initial.abilities.added_count = 2;
        initial.heroabilities[0] = (heroability_t){.code=FS_SLKKey("Ahar"),.level=2};
        channel_t channel = {.code=FS_SLKKey("AInv")};
        initial.channel = &channel;
        uint32_t expected[128], expected_count;
        edict_t reference = initial, compiled = initial;
        event_trace_mutation = mutation; event_trace_count = 0;
        unit_events_force_uncached = true;
        intptr_t expected_result = unit_dispatch_authored_abilities(&reference,messages[m],NULL,stop,innate,true);
        expected_count = event_trace_count; memcpy(expected,event_trace,expected_count*sizeof(*expected));
        event_trace_count = 0; unit_events_force_uncached = false;
        intptr_t result = unit_dispatch_authored_abilities(&compiled,messages[m],NULL,stop,innate,true);
        T_EQ(result,expected_result); T_EQ(event_trace_count,expected_count);
        T_EQ(memcmp(event_trace,expected,expected_count*sizeof(*expected)),0);
        T_EQ(memcmp(&compiled,&reference,sizeof(compiled)),0);
    }
    FOR_LOOP(i,game.num_abilities) S_ReplaceAbilityProcedure(abilitylist + i, saved[i]);
    event_trace_mutation = 0; event_trace_replacement = NULL;
    InitAbilities();
}
TEST(wc3_ability_dispatch, excluded_innate_messages_return_zero_and_leave_unit_untouched) {
    InitAbilities();
    UnitData_t data = {0};
    edict_t unit = {.health = {100, 100}, .data.UnitData = &data};
    FOR_LOOP(i, num_innate) {
        abilityCall_t call = {.item = innate_items + i};
        FOR_LOOP(msg, A_NUM_MESSAGES) {
            if (msg == A_UNIT_EVENT_MASK || innate_receives(i, msg)) continue;
            edict_t before = unit;
            T_EQ(S_AbilityMessage(&unit, msg, &call), 0);
            T_EQ(memcmp(&unit, &before, sizeof(unit)), 0);
        }
    }
}
static intptr_t unqueried_innate_test_proc(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    (void)ent; (void)call;
    return msg == A_UNIT_STAND;
}
TEST(wc3_ability_dispatch, unqueried_procedures_retain_original_broadcast_contract) {
    ability_t *move = (ability_t *)FindAbilityByClassname("Amov");
    abilityProc_t saved = move->proc;
    S_ReplaceAbilityProcedure(move, unqueried_innate_test_proc);
    InitAbilities();
    edict_t unit = {0};
    innate_event_visits = 0;
    T_ASSERT(unit_dispatch_authored_abilities(&unit, A_UNIT_STAND, NULL, false, true, false));
    T_EQ(innate_event_visits, 1);
    S_ReplaceAbilityProcedure(move, saved);
    InitAbilities();
}
TEST(wc3_ability_dispatch, repeated_rawcode_dispatch_avoids_name_resolution_and_tracks_metadata) {
    uint32_t alias=MAKEFOURCC('A','0','0','1');
    uint32_t missing=MAKEFOURCC('Z','Z','Z','Z');
    AbilityData_t row={.id=alias,.code=MAKEFOURCC('A','I','n','v')};
    slkTestData_t table={.rows=&row,.count=1};
    slkTestData_t *old=G_SetSLKRows("AbilityData",&table);
    InitAbilities();
    ability_t const *inventory=FindAbilityByClassname("AInv");
    T_ASSERT(S_AbilityItem(alias).ability==inventory);
    T_ASSERT(S_AbilityItem(missing).ability==NULL);
    ability_name_comparisons=0;
    FOR_LOOP(i,1024) {
        abilityitem_t item=S_AbilityItem(alias);
        T_EQ(item.code,alias);T_ASSERT(item.ability==inventory);
        T_ASSERT(S_AbilityItem(missing).ability==NULL);
    }
    T_ASSERT(ability_name_comparisons<12);
    AbilityData_t updated={.id=alias,.code=MAKEFOURCC('A','m','o','v')};
    slkTestData_t replacement={.rows=&updated,.count=1};
    slkTestData_t *previous=G_SetSLKRows("AbilityData",&replacement);
    T_ASSERT(S_AbilityItem(alias).ability==FindAbilityByClassname("Amov"));
    InitAbilities();
    T_ASSERT(S_AbilityItem(alias).ability==FindAbilityByClassname("Amov"));
    G_SetSLKRows("AbilityData",old);free(previous);free(old);
    uint32_t invalid[]={0,1,0x00496e41,0x41007641};
    FOR_LOOP(i,sizeof(invalid)/sizeof(*invalid)) {
        abilityitem_t item=S_AbilityItem(invalid[i]);
        T_EQ(item.code,invalid[i]);
        T_ASSERT(item.ability==(invalid[i]?FindAbilityForCommand(GetClassName(invalid[i])):NULL));
    }
}
TEST(wc3_ability_dispatch, classname_lookup_keeps_first_rows_without_registry_scans) {
    InitAbilities();
    FOR_LOOP(i, game.num_abilities) {
        ability_t const *expected = NULL;
        FOR_LOOP(j, game.num_abilities)
            if (abilitylist[j].classname && !strcmp(abilitylist[j].classname, abilitylist[i].classname)) {
                expected = abilitylist + j;
                break;
            }
        T_ASSERT(FindAbilityByClassname(abilitylist[i].classname) == expected);
        T_EQ(FindAbilityIndex(abilitylist[i].classname), (uint32_t)(expected - abilitylist));
    }
    ability_name_comparisons = 0;
    FOR_LOOP(i, 1024) {
        T_ASSERT(FindAbilityByClassname("unregistered-command") == NULL);
        T_ASSERT(FindAbilityByClassname("cmdmove") == NULL);
        T_ASSERT(FindAbilityByClassname("CmdMove") == abilitylist + 1);
        T_EQ(FindAbilityIndex("unregistered-command"), 255);
    }
    T_ASSERT(ability_name_comparisons < 1024 * 12);
    InitAbilities();
    T_ASSERT(FindAbilityByClassname("CmdMove") == abilitylist + 1);
}
#endif
