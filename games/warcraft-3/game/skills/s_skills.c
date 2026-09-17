#include "s_skills.h"

#ifdef WC3_DEBUG_AUTOCAST
int G_AutocastDebugLevel(void) {
    LPCSTR value;

    value = gi.CvarString("wc3_autocast_debug", "0");
    return value ? atoi(value) : 0;
}
#endif

LPCSTR const raven_orders[] = { "ravenform", "unravenform", NULL };

static ability_t abilitylist[] = {
    { STR_CmdStop, CAbilityStop, AB_COMMAND },  // Stop — engine command
    { STR_CmdMove, CAbilityMove, AB_COMMAND },  // Move — engine command
    { STR_CmdAttack, CAbilityAttack, AB_COMMAND },  // Attack — engine command
    { STR_CmdBuild, CAbilityBuild, AB_COMMAND },  // Build — engine command
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
    { "ANen", CAbilityEnsnare, AB_SPELL, SPELL_TARGET_UNIT },  /* Ensnare */
    { "ACfu", CAbilityFrostArmorCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Frost Armor */
    { "ANpa", CAbilityParasiteCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Parasite */
    { "Acny", CAbilityCycloneCampaign, AB_SPELL, SPELL_TARGET_UNIT },  /* Cyclone */
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
    { "Adet", CAbilityPassive, AB_PASSIVE },  /* Detector */
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
    { "Afbk", CAbilityPassive, AB_PASSIVE },  /* Feedback */
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
    { "Agyv", CAbilityPassive, AB_PASSIVE },  /* True Sight */
    { "Adef", CAbilityDefend, AB_SPELL | AB_TOGGLE },  /* Defend */
    { "Afla", CAbilityFlare, AB_SPELL, SPELL_TARGET_POINT },  /* Flare */
    { "Adts", CAbilityPassive, AB_PASSIVE },  /* Magic Sentry */
    { "Ainf", CAbilityInnerFire, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Inner Fire */
    { "Adis", CAbilityDispelMagic, AB_SPELL, SPELL_TARGET_POINT },  /* Dispel Magic */
    { "Ahea", CAbilityHeal, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Heal */
    { "Aslo", CAbilitySlow, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Slow */
    { "Aivs", CAbilityInvisibility, AB_SPELL, SPELL_TARGET_UNIT },  /* Invisibility */
    { "Aply", CAbilityPolymorph, AB_SPELL, SPELL_TARGET_UNIT },  /* Polymorph */
    { "AHbz", CAbilityBlizzard, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Blizzard */
    { "AHwe", CAbilityWaterElemental, AB_SPELL },  /* Summon Water Elemental */
    { "AHab", CAbilityPassive, AB_PASSIVE },  /* Brilliance Aura */
    { "AHmt", CAbilityMassTeleport, AB_SPELL, SPELL_TARGET_UNIT },  /* Mass Teleport */
    { "AHtb", CAbilityThunderBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Storm Bolt */
    { "AHtc", CAbilityThunderClap, AB_SPELL },  /* Thunder Clap */
    { "AHbh", CAbilityPassive, AB_PASSIVE },  /* Bash */
    { "AHav", CAbilityAvatar, AB_SPELL },  /* Avatar */
    { "AHhb", CAbilityHolyBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Holy Light */
    { "AHds", CAbilityDivineShield, AB_SPELL },  /* Divine Shield */
    { "AHad", CAbilityAuraDevotion, AB_SPELL },  /* Devotion Aura */
    { "AHre", CAbilityResurrection, AB_SPELL, SPELL_TARGET_NONE },  /* Resurrection */
    { "Amil", CAbilityMilitia, AB_COMMAND },  /* Call to Arms */
    { "Amic", CAbilityMilitiaConvert, AB_COMMAND | AB_SEPARATE_OFF },  /* Call To Arms */

    /* ItemAbilityStrings.txt */
    { "AIsm", CAbilityStrengthMod, AB_ITEM },  /* Item Strength Gain */
    { "AIam", CAbilityStrengthMod, AB_ITEM },  /* Item Agility Gain */
    { "AIat", CAbilityAttackBonus, 0 },  /* Item Damage Bonus */
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
    { "ANms", CAbilityPassive, AB_PASSIVE },  /* Mana Shield */
    { "ANrf", CAbilityRainOfFire, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Rain of Fire */
    { "AHca", CAbilityColdArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Cold Arrows */
    { "ANht", CAbilityHowlOfTerror, AB_SPELL },  /* Howl of Terror */
    { "ANca", CAbilityPassive, AB_PASSIVE },  /* Cleaving Attack */
    { "ANdo", CAbilityDoom, AB_SPELL, SPELL_TARGET_UNIT },  /* Doom */
    { "ANdr", CAbilityDrainNeutral, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },  /* Life Drain */
    { "ANbf", CAbilityBreathOfFire, AB_SPELL, SPELL_TARGET_POINT },  /* Breath of Fire */
    { "ANsg", CAbilitySummonGrizzly, AB_SPELL },  /* Summon Bear */
    { "ANsq", CAbilitySummonQuillbeast, AB_SPELL },  /* Summon Quilbeast */
    { "ANsw", CAbilitySummonWarEagle, AB_SPELL },  /* Summon Hawk */
    { "ANst", CAbilityStampede, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Stampede */
    { "ANfs", CAbilityFlameStrikeNeutral, AB_SPELL, SPELL_TARGET_POINT },  /* Flame Strike */
    { "AInv", CAbilityInventory, AB_PASSIVE },  /* Inventory */
    { "ANdb", CAbilityPassive, AB_PASSIVE },  /* Drunken Brawler */
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
    { "ANfb", CAbilityFireBolt, AB_SPELL, SPELL_TARGET_UNIT },  /* Firebolt */
    { "Agld", CAbilityGoldMine, 0 },  /* Gold Mine ability */
    { "Artn", CAbilityReturn, AB_COMMAND },  /* Return */
    { "Avul", CAbilityPassive, AB_PASSIVE },  /* Invulnerable */
    { "Abli", CAbilityBlightGrowth, AB_COMMAND },  /* Blight */
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
    { "AEfn", CAbilityForceOfNature, AB_SPELL },  /* Force of Nature */
    { "AEah", CAbilityPassive, AB_PASSIVE },  /* Thorns Aura */
    { "AEtq", CAbilityTranquility, AB_SPELL | AB_CHANNEL },  /* Tranquility */
    { "AHfa", CAbilityFlamingArrows, AB_SPELL | AB_TOGGLE | AB_AUTOCAST },  /* Searing Arrows */
    { "AEar", CAbilityPassive, AB_PASSIVE },  /* Trueshot Aura */
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
    { "AOcr", CAbilityPassive, AB_PASSIVE },  /* Critical Strike */
    { "AOww", CAbilityWhirlwind, AB_SPELL | AB_CHANNEL },  /* Bladestorm */
    { "AOcl", CAbilityChainLightning, AB_SPELL, SPELL_TARGET_UNIT },  /* Chain Lightning */
    { "AOfs", CAbilityFarSight, AB_SPELL, SPELL_TARGET_POINT },  /* Far Sight */
    { "AOsf", CAbilitySpiritWolf, AB_SPELL },  /* Feral Spirit */
    { "AOeq", CAbilityEarthquake, AB_SPELL | AB_CHANNEL, SPELL_TARGET_POINT },  /* Earthquake */
    { "AOsh", CAbilityShockwave, AB_SPELL, SPELL_TARGET_POINT },  /* Shockwave */
    { "AOae", CAbilityPassive, AB_PASSIVE },  /* Endurance Aura */
    { "AOre", CAbilityPassive, AB_PASSIVE },  /* Reincarnation */
    { "AOws", CAbilityStomp, AB_SPELL },  /* War Stomp */
    { "Ablo", CAbilityBloodlust, AB_SPELL | AB_AUTOCAST, SPELL_TARGET_UNIT },  /* Bloodlust */

    /* UndeadAbilityStrings.txt */
    { "AUim", CAbilityImpale, AB_SPELL, SPELL_TARGET_POINT },  /* Impale */
    { "AUts", CAbilityPassive, AB_PASSIVE },  /* Spiked Carapace */
    { "AUcb", CAbilityCarrionScarabs, AB_SPELL | AB_AUTOCAST },  /* Carrion Beetles */
    { "AUls", CAbilityLocustSwarm, AB_SPELL | AB_CHANNEL },  /* Locust Swarm */
    { "Aaha", CAbilityAcolyteHarvest, AB_COMMAND },  /* Gather */
    { "AUdc", CAbilityDeathCoil, AB_SPELL, SPELL_TARGET_UNIT },  /* Death Coil */
    { "AUau", CAbilityPassive, AB_PASSIVE },  /* Unholy Aura */
    { "AUdp", CAbilityDeathPact, AB_SPELL, SPELL_TARGET_UNIT },  /* Death Pact */
    { "AUan", CAbilityAnimateDead, AB_SPELL },  /* Animate Dead */
    { "AUcs", CAbilityCarrionSwarm, AB_SPELL, SPELL_TARGET_POINT },  /* Carrion Swarm */
    { "AUsl", CAbilitySleep, AB_SPELL, SPELL_TARGET_UNIT },  /* Sleep */
    { "AUav", CAbilityPassive, AB_PASSIVE },  /* Vampiric Aura */
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
    // TODO: AIfb CAbilityOnFireHuman  /* Item Attack Fire Bonus */
    // TODO: AIlb a_bounce  /* Item Attack Lightning Bonus */
    // TODO: AIlp a_lightning_purge  /* Item Purge */
    // TODO: AIob CAbilityFrostNova  /* Item Attack Frost Bonus */
    // TODO: AIpb a_item_mana_restore  /* Item Attack Poison Bonus */
    // TODO: AIcb a_button  /* Item Attack Corruption Bonus */
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
    // TODO: AIzb CAbilityFrostNova  /* Item Freeze Damage Bonus */
    // TODO: Arel a_aura_regen_life  /* Item Life Regeneration */
    // TODO: Aami a_unknown  /* Item Anti-Magic Shell */
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
    // TODO: AIrd a_item_dispel_aoe  /* Raise Dead (Item) */
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
    // TODO: ANic CAbilityInnerFire  /* Incinerate */
    // TODO: ANia CAbilityInnerFire  /* Incinerate */
    // TODO: ANso a_spell  /* Soul Burn */
    // TODO: ANlm a_lightning_shield  /* Summon Lava Spawn */
    // TODO: ANvc a_spell  /* Volcano */
    // TODO: ANsy a_button  /* Pocket Factory */
    // TODO: ANcs a_spell  /* Cluster Rockets */
    // TODO: ANeg CAbilityEvasion  /* Engineering Upgrade */
    // TODO: ANrg a_regen_base  /* Robo-Goblin */
    // TODO: ANde a_button  /* Demolish */
    // TODO: ANfy CAbilityOnFireHuman  /* Factory */
    // TODO: ANhs CAbilityHeal  /* Healing Spray */
    // TODO: ANcr CAbilityCriticalStrike  /* Chemical Rage */
    // TODO: ANtm a_aura_regen_life  /* Transmute */
    // TODO: Aasl CAbilitySlow  /* Slow Aura */
    // TODO: Atdg CAbilityStampede  /* Building Damage Aura */
    // TODO: Atsp a_aura  /* Tornado Spin */
    // TODO: Atwa CAbilityStomp  /* Tornado Wander */
    // TODO: ANef a_button  /* "Storm, Earth, And Fire" */
    // TODO: ACbf CAbilityOnFireHuman  /* Breath of Frost */
    // TODO: ANmr a_aura  /* Mind Rot */
    // TODO: ANmo a_bounce  /* Monsoon */
    // TODO: ANwm CAbilityStomp  /* Watery Minion */
    // TODO: Arng a_revenge  /* Revenge */
    // TODO: Atol a_upgrade  /* Tree of Life upgrade ability */
    // TODO: Awrp a_warp  /* Waygate ability */
    // TODO: ANsl a_spell  /* Soul Preservation */
    // TODO: ANfd a_spell  /* Finger of Death */
    // TODO: ANdp CAbilityMassTeleport  /* Dark Portal */
    // TODO: ANrc CAbilityRainOfFire  /* Rain of Chaos */
    // TODO: Achd CAbilityCargoHold  /* Cargo Hold Death */
    // TODO: Asla a_sleep_always  /* Sleep Always; UnitCanSleepPerm recognizes ownership, behavior remains unresolved */
    // TODO: Advc a_devour_cargo  /* Devour Cargo */
    // TODO: ANpi a_perm_immolation  /* Permanent Immolation */
    // TODO: Apig a_harvest_return  /* Permanent Immolation */
    // TODO: Andt a_evil_eye  /* Reveal */
    // TODO: ANin a_creep_thunder_bolt  /* Inferno */
    // TODO: Asds a_button  /* Kaboom! */
    // TODO: Anhe CAbilityHeal  /* Heal */
    // TODO: ACtc a_creep_thunder_clap  /* Slam */
    // TODO: ACtb a_creep_thunder_bolt  /* Hurl Boulder */
    // TODO: Afzy a_spell  /* Frenzy */
    // TODO: ACdv a_creep_devour  /* Devour */
    { "ACsp", CAbilityCreepSleep, AB_PASSIVE | AB_INNATE },  /* Natural creep sleep */
    // TODO: Asod a_simple_spell  /* Spawn Skeleton */
    // TODO: Assp a_simple_spell  /* Spawn Spiderlings */
    // TODO: Aspd a_simple_spell  /* Spawn Spiders */
    // TODO: AOac a_unknown  /* Command Aura */
    // TODO: ACad a_revenge  /* Animate Dead */
    // TODO: ACrn a_creep_reincarnation  /* Reincarnation */
    // TODO: Adda CAbilityOnFireHuman  /* AOE damage upon death */
    // TODO: Agho a_ghost  /* Ghost */
    // TODO: Aeth a_ghost  /* Ghost */
    // TODO: Amin a_button  /* Mine - exploding */
    // TODO: Apiv a_perm_invis  /* Permanent Invisibility */
    // TODO: Awan a_wander  /* Wander */
    // TODO: Aarm a_unknown  /* Mana Regeneration Aura */
    // TODO: Asid a_button  /* Sell Items */
    // TODO: Asud a_spell  /* Sell Units */

    /* NightElfAbilityStrings.txt */
    // TODO: Avng a_revenge  /* Spirit of Vengeance */
    // TODO: Amfl CAbilityFrostNova  /* Mana Flare */
    // TODO: Apsh CAbilityStampede  /* Phase Shift */
    // TODO: Aetl a_button  /* Ethereal */
    // TODO: Agra a_poison_attack  /* War Club */
    // TODO: Assk a_button  /* Hardened Skin */
    // TODO: Arsk a_morph  /* Resistant Skin */
    // TODO: Atau a_spell  /* Taunt */
    // TODO: Amgl a_bounce  /* Moon Glaive */
    // TODO: Aspo a_poison_attack  /* Slow Poison */
    // TODO: Ashm a_shadow_meld  /* Shadow Meld */
    // TODO: Ahid a_shadow_meld  /* Shadow Meld */
    // TODO: Aesn a_button  /* Sentinel */
    // TODO: Adtn a_button  /* Detonate */
    // TODO: Abrf a_morph  /* Bear Form */

    // TODO: Aadm a_auto_dispel_magic  /* Abolish Magic */
    // TODO: Amim a_magic_immunity  /* Spell Immunity */
    // TODO: Ault a_night_vision  /* Ultravision */
    // TODO: Acoa a_morph  /* Mount Hippogryph */
    // TODO: Acoh a_morph  /* Pick up Archer */
    // TODO: Adec CAbilityDefend  /* Dismount */
    // TODO: Acor a_bounce  /* Corrosive Breath */
    // TODO: AEst a_button  /* Scout */
    // TODO: Acyc a_morph  /* Cyclone */
    // TODO: Alit a_lightning_attack  /* Lightning Attack */

    /* OrcAbilityStrings.txt */
    // TODO: Abof a_aura_command  /* Burning Oil */
    // TODO: Absk a_button  /* Berserk */
    // TODO: Arbr a_button  /* Reinforced Burrows Upgrade */
    // TODO: Aast a_aura  /* Ancestral Spirit */
    // TODO: Adch a_spell  /* Disenchant */
    // TODO: Acpf a_purge  /* Corporeal Form */
    // TODO: Aetf a_morph  /* Ethereal Form */
    // TODO: Aspl a_creep_sleep  /* Spirit Link */
    // TODO: Aliq CAbilityOnFireHuman  /* Liquid Fire */
    // TODO: Auco a_spell  /* Unstable Concoction */
    // TODO: Acha a_unknown  /* Chaos */
    // TODO: Achl a_cargo_load  /* Chaos Cargo Load */
    // TODO: Awar a_bounce  /* Pulverize */
    // TODO: Aens CAbilityEnsnare  /* Ensnare */
    // TODO: Adev a_devour  /* Devour */
    // TODO: Aprg a_lightning_purge  /* Purge */
    // TODO: Alsh a_lightning_shield  /* Lightning Shield */
    // TODO: Aeye a_button  /* Sentry Ward */
    // TODO: Asta a_stasis_trap  /* Stasis Trap */
    // TODO: Ahwd a_healing_ward  /* Healing Ward */
    // TODO: Aoar a_aura_regen_life  /* Healing Ward Aura */
    // TODO: Aven a_venom_spear  /* Envenomed Spears */
    // TODO: Apoi a_poison_attack  /* Poison Sting */
    // TODO: Apo2 a_item_invul  /* Poison Sting */
    // TODO: Aspi a_spiked  /* Spiked Barricades */
    // TODO: Asal a_button  /* Pillage */
    // TODO: Aakb a_aura_command  /* War Drums */

    /* UndeadAbilityStrings.txt */
    // TODO: Arpb a_spell  /* Replenish */
    // TODO: Arpl a_spell  /* Essence of Blight */
    // TODO: Arpm a_spell  /* Spirit Touch */
    // TODO: Aexh a_button  /* Exhume Corpses */
    // TODO: Aave CAbilityStampede  /* Destroyer Form */
    // TODO: Afak a_spell  /* Orb of Annihilation */
    // TODO: Advm CAbilityDispelMagic  /* Devour Magic */
    // TODO: Aabr a_aura_regen_life  /* Aura of Blight */
    // TODO: Aabs a_aura  /* Absorb Mana */
    // TODO: Abur a_creep_sleep  /* Burrow */
    // TODO: Amtc a_unknown  /* Cargo Hold */
    // TODO: Atru a_true_sight  /* True Sight */
    // TODO: Auns a_button  /* Unsummon Building */
    // TODO: Agyd a_simple_spell  /* Create Corpse */
    // TODO: Alam a_spell  /* Sacrifice */
    // TODO: Asac a_spell  /* Sacrifice */
    // TODO: Acan a_cannibalize  /* Cannibalize */
    // TODO: Aspa CAbilityAttack  /* Spider Attack */
    // TODO: Aweb a_auto_target_spell  /* Web */
    // TODO: Astn a_morph  /* Stone Form */
    // TODO: Amel a_cargo_load  /* Get Corpse */
    // TODO: Amed a_cargo_drop  /* Drop Corpse */
    // TODO: Aapl a_unknown  /* Disease Cloud */
    // TODO: Apts a_button  /* Disease Cloud */
    // TODO: Afrb a_button  /* Frost Breath */
    // TODO: Afra a_button  /* Frost Attack */
    // TODO: Afrz CAbilityFrostNova  /* Freezing Breath */
    // TODO: Arai a_simple_spell  /* Raise Dead */
    // TODO: Auhf a_creep_sleep  /* Unholy Frenzy */
    // TODO: Acrs a_auto_target_spell  /* Curse */
    // TODO: Aams a_magic_immunity  /* Anti-magic Shell */
    // TODO: Apos a_creep_sleep  /* Possession */
    // TODO: Aps2 a_creep_sleep  /* Possession */
    // TODO: Acri a_cripple  /* Cripple */

    /* No AbilityStrings source file */
    // TODO: AIgl a_unknown  /* FortificationGlyph — CAbility [ITEM] other */
    // TODO: AIrg a_unknown  /* Potion of Life Regen — CAbility [ITEM] other */
    // TODO: ANsu a_unknown  /* Submerge (Myrmidon) — CAbility creeps */
    // TODO: AOwd a_unknown  /* Shadow Hunter - Serpent Ward */
    // TODO: Aimp a_bounce  /* Impaling Bolt — CAbilityBounce nightelf */
    // TODO: Ansp a_neutral_spell  /* Neutral Spies — CAbilityNeutralSpell creeps */

    /* AbilityData rows without a generated AbilityStrings entry. */
    // TODO: ACac a_unknown  /* Aura - Command (Creep) */
    // TODO: ACah a_unknown  /* Thorns Aura (creep) */
    // TODO: ACam a_unknown  /* Anti-magic Shield (creep) */
    // TODO: ACat a_unknown  /* Aura - Trueshot (Creep) */
    // TODO: ACav a_unknown  /* Aura - Devotion (Creep) */
    // TODO: ACba a_unknown  /* Aura - Brilliance (creep) */
    // TODO: ACbb a_unknown  /* Bloodlust (creep, Hotkey B) */
    // TODO: ACbc a_unknown  /* Breath of Fire(Creep) */
    // TODO: ACbh a_unknown  /* Bash (creep) */
    // TODO: ACbk a_unknown  /* Black Arrow (melee, creep) */
    // TODO: ACbl a_unknown  /* Bloodlust (Creep) */
    // TODO: ACbn a_unknown  /* Banish(Creep) */
    // TODO: ACbz a_unknown  /* Blizzard (creep) */
    // TODO: ACc2 a_unknown  /* Crushing Wave (Dragon Turtle) */
    // TODO: ACc3 a_unknown  /* Crushing Wave (Lesser) */
    // TODO: ACca a_unknown  /* Carrion Swarm (creep) */
    // TODO: ACcb a_unknown  /* Frost Bolt */
    // TODO: ACce a_unknown  /* Cleaving Attack (Creep) */
    // TODO: ACch a_unknown  /* Charm */
    // TODO: ACcl a_unknown  /* Chain Lightning (creep) */
    // TODO: ACcn a_unknown  /* Cannibalize (creep) */
    // TODO: ACcr a_unknown  /* Cripple (creep) */
    // TODO: ACcs a_unknown  /* Curse (creep) */
    // TODO: ACct a_unknown  /* Critical Strike (creep) */
    // TODO: ACcv a_unknown  /* Crushing Wave */
    // TODO: ACcw a_unknown  /* Cold Arrows (creep) */
    // TODO: ACcy a_unknown  /* Cyclone (creep) */
    // TODO: ACd2 a_unknown  /* Abolish Magic (Creep, 1,2 pos) */
    // TODO: ACdc a_unknown  /* Death Coil (creep) */
    // TODO: ACde a_unknown  /* Devour Magic(creep) */
    // TODO: ACdm a_unknown  /* Abolish Magic (Creep) */
    // TODO: ACdr a_unknown  /* Drain Life(Creep) */
    // TODO: ACds a_unknown  /* Divine Shield (creep) */
    // TODO: ACen a_unknown  /* Ensnare (Creep) */
    // TODO: ACes a_unknown  /* Evasion (creep 100%) */
    // TODO: ACev a_unknown  /* Evasion (creep) */
    // TODO: ACf2 a_unknown  /* Frost Armor (creep,autocast) */
    // TODO: ACf3 a_unknown  /* Finger of Pain (2,1 Button) */
    // TODO: ACfa a_unknown  /* Frost Armor (creep,old) */
    // TODO: ACfb a_unknown  /* Fire Bolt (creep) */
    // TODO: ACfd a_unknown  /* Finger of Pain */
    // TODO: ACff a_unknown  /* Faerie Fire (creep) */
    // TODO: ACfl a_unknown  /* Forked Lightning(creep) */
    // TODO: ACfn a_unknown  /* Frost Nova (creep) */
    // TODO: ACfr a_unknown  /* Force of Nature (creep) */
    // TODO: ACfs a_unknown  /* Flame Strike (Creep) */
    // TODO: AChv a_unknown  /* Healing Wave(Creep) */
    // TODO: AChw a_unknown  /* Healing Ward (creep) */
    // TODO: AChx a_unknown  /* Hex (Creep) */
    // TODO: ACif a_unknown  /* Inner Fire (Creep) */
    // TODO: ACim a_unknown  /* Immolation (creep) */
    // TODO: ACls a_unknown  /* Lightning Shield (creep) */
    // TODO: ACm2 a_unknown  /* Magic Immunity (Archimonde) */
    // TODO: ACm3 a_unknown  /* Magic Immunity (Dragons) */
    // TODO: ACmf a_unknown  /* Mana Shield(Creep) */
    // TODO: ACmi a_unknown  /* Magic Immunity (Creep) */
    // TODO: ACmo a_unknown  /* Monsoon(creep) */
    // TODO: ACmp a_unknown  /* Impale(Creep) */
    // TODO: ACnr a_unknown  /* Neutral Regen (health only) */
    // TODO: ACpa a_unknown  /* Parasite(eredar) */
    // TODO: ACps a_unknown  /* Possession (creep) */
    // TODO: ACpu a_unknown  /* Purge (Creep) */
    // TODO: ACpv a_unknown  /* Pulverize (Sea Giant) */
    // TODO: ACpy a_unknown  /* Polymorph (creep) */
    // TODO: ACr1 a_unknown  /* Roar (creep) -- Skeletal Orc */
    // TODO: ACr2 a_unknown  /* Rejuvination (Furbolg) */
    // TODO: ACrd a_unknown  /* Raise Dead (Creep) */
    // TODO: ACrf a_unknown  /* Rain of Fire (creep) */
    // TODO: ACrg a_unknown  /* Rain of Fire (creep,greater) */
    // TODO: ACrj a_unknown  /* Rejuvination (creep) */
    // TODO: ACrk a_unknown  /* Resistant Skin (creep) */
    // TODO: ACro a_unknown  /* Roar (creep) */
    // TODO: ACs9 a_unknown  /* Feral Spirit (creep - pig) */
    // TODO: ACsa a_unknown  /* Searing Arrows (creep) */
    // TODO: ACsf a_unknown  /* Feral Spirit (creep) */
    // TODO: ACsh a_unknown  /* Shockwave (Creep) */
    // TODO: ACsi a_unknown  /* Silence(Creep) */
    // TODO: ACsk a_unknown  /* Resistant Skin(3,1 pos, creep) */
    // TODO: ACsl a_unknown  /* Sleep (creep) */
    // TODO: ACsm a_unknown  /* Siphon Mana (Creep) */
    // TODO: ACss a_unknown  /* Shadow Strike(Creep) */
    // TODO: ACst a_unknown  /* Shockwave (Trap) */
    // TODO: ACsw a_unknown  /* Slow (Creep) */
    // TODO: ACt2 a_unknown  /* Thunder Clap (Thunder Lizard) */
    // TODO: ACua a_unknown  /* Unholy Aura (creep) */
    // TODO: ACuf a_unknown  /* Unholy Frenzy (creep) */
    // TODO: ACvp a_unknown  /* Vampiric Aura (creep) */
    // TODO: ACvs a_unknown  /* Venom Spears (Creep) */
    // TODO: ACwb a_unknown  /* Web (creep) */
    // TODO: ACwe a_unknown  /* Summon Sea Elemental */
    // TODO: AEIl a_unknown  /* Illidan - Metamorphosis */
    // TODO: AEsb a_unknown  /* Cenarius - Beefy Starfall */
    // TODO: AEvi a_unknown  /* Evil Illidan - Metamorphosis */
    // TODO: AHta a_unknown  /* Reveal(Arcane Tower) */
    // TODO: AI2m a_unknown  /* 200 mana bonus */
    // TODO: AIa1 a_unknown  /* AgilityBonus (+1) */
    // TODO: AIa3 a_unknown  /* AgilityBonus (+3) */
    // TODO: AIa4 a_unknown  /* AgilityBonus (+4) */
    // TODO: AIa6 a_unknown  /* AgilityBonus (+6) */
    // TODO: AIad a_unknown  /* ItemAuraDevotion */
    // TODO: AIae a_unknown  /* ItemAuraEndurance */
    // TODO: AIar a_unknown  /* ItemAuraTrueshot */
    // TODO: AIau a_unknown  /* ItemAuraUnholy */
    // TODO: AIav a_unknown  /* ItemAuraVampiric */
    // TODO: AIaz a_unknown  /* AgilityBonus (+10) */
    // TODO: AIba a_unknown  /* ItemAuraBrilliance */
    // TODO: AIbb a_unknown  /* Build Tiny Blacksmith */
    // TODO: AIbf a_unknown  /* Build Tiny Farm */
    // TODO: AIbg a_unknown  /* Build Tiny Great Hall */
    // TODO: AIbh a_unknown  /* Build Tiny Altar */
    // TODO: AIbk a_unknown  /* Blink (Item) */
    // TODO: AIbm a_unknown  /* MaxManaBonus (Most) */
    // TODO: AIbr a_unknown  /* Build Tiny Lumber Mill */
    // TODO: AIbs a_unknown  /* Build Tiny Barracks */
    // TODO: AIbt a_unknown  /* Build Tiny Scout Tower */
    // TODO: AIbx a_unknown  /* Bash (item) */
    // TODO: AIcd a_unknown  /* ItemAuraCommand */
    // TODO: AIcl a_unknown  /* Chain Lightning (item) */
    // TODO: AIcm a_unknown  /* Control Magic (item) */
    // TODO: AIcs a_unknown  /* Critical Strike (item) */
    // TODO: AIcy a_unknown  /* Cyclone */
    // TODO: AId0 a_unknown  /* DefenseBonus (+10) */
    // TODO: AId1 a_unknown  /* DefenseBonus (+1) */
    // TODO: AId2 a_unknown  /* DefenseBonus (+2) */
    // TODO: AId3 a_unknown  /* DefenseBonus (+3) */
    // TODO: AId4 a_unknown  /* DefenseBonus (+4) */
    // TODO: AId5 a_unknown  /* DefenseBonus (+5) */
    // TODO: AId7 a_unknown  /* DefenseBonus (+7) */
    // TODO: AId8 a_unknown  /* DefenseBonus (+8) */
    // TODO: AIdb a_unknown  /* ItemDefenseAoe (+ Healing) */
    // TODO: AIdf a_unknown  /* Orb of Darkness */
    // TODO: AIdn a_unknown  /* Shadow Orb Ability */
    // TODO: AIdp a_unknown  /* Death Pact (item) */
    // TODO: AIds a_unknown  /* ItemDispelAoeWithCooldown */
    // TODO: AIdv a_unknown  /* Divine Shield (Item) */
    // TODO: AIe2 a_unknown  /* ExperienceMod greater */
    // TODO: AIev a_unknown  /* Evasion */
    // TODO: AIfd a_unknown  /* FigurineRedDrake */
    // TODO: AIff a_unknown  /* FigurineFurbolg */
    // TODO: AIfg a_unknown  /* Cloud of Fog (Item) */
    // TODO: AIfh a_unknown  /* FigurineFelHound */
    // TODO: AIfr a_unknown  /* FigurineRockGolem */
    // TODO: AIft a_unknown  /* Frostguard - frost melee */
    // TODO: AIfu a_unknown  /* FigurineDoomGuard */
    // TODO: AIfw a_unknown  /* Searing Blade - fire melee */
    // TODO: AIfx a_unknown  /* Flag (Orc Battle Standard) */
    // TODO: AIfz a_unknown  /* Finger of Death (item) */
    // TODO: AIgd a_unknown  /* Orb of Guldan */
    // TODO: AIgf a_unknown  /* FortificationGlyph */
    // TODO: AIgm a_unknown  /* AgilityMod +2 */
    // TODO: AIgu a_unknown  /* UltraVisionGlyph */
    // TODO: AIgx a_unknown  /* Aura - Regeneration (item) */
    // TODO: AIh1 a_unknown  /* ItemHeal (Lesser) */
    // TODO: AIh2 a_unknown  /* ItemHeal (Greater) */
    // TODO: AIh3 a_unknown  /* ItemHeal (Least) */
    // TODO: AIhb a_unknown  /* ItemHealAoeGreater */
    // TODO: AIhl a_unknown  /* Holy Light (item) */
    // TODO: AIhw a_unknown  /* Healing Ward */
    // TODO: AIhx a_unknown  /* ItemHeal (Leastest) */
    // TODO: AIi1 a_unknown  /* IntelligenceBonus (+1) */
    // TODO: AIi3 a_unknown  /* IntelligenceBonus (+3) */
    // TODO: AIi4 a_unknown  /* IntelligenceBonus (+4) */
    // TODO: AIi6 a_unknown  /* IntelligenceBonus (+6) */
    // TODO: AIin a_unknown  /* ItemInferno */
    // TODO: AIir a_unknown  /* FigurineIceRevenant */
    // TODO: AIl1 a_unknown  /* MaxLifeBonus (Lesser) */
    // TODO: AIl2 a_unknown  /* MaxLifeBonus (Greater) */
    // TODO: AIlf a_unknown  /* MaxLifeBonus (Least) */
    // TODO: AIll a_unknown  /* Orb of Lightning */
    // TODO: AIls a_unknown  /* Lightning Shield */
    // TODO: AIlx a_unknown  /* Shaman Claws - lightning melee */
    // TODO: AIlz a_unknown  /* MaxLifeBonus (Leastest) */
    // TODO: AIm1 a_unknown  /* ItemManaRestore (Lesser) */
    // TODO: AIm2 a_unknown  /* ItemManaRestore (Greater) */
    // TODO: AImb a_unknown  /* MaxManaBonus (Least) */
    // TODO: AImh a_unknown  /* Permanent Hit point Bonus */
    // TODO: AImt a_unknown  /* Staff o' Teleportation */
    // TODO: AImv a_unknown  /* MaxManaBonus (Leastest, Really) */
    // TODO: AImx a_unknown  /* Magic Immunity */
    // TODO: AImz a_unknown  /* MaxManaBonus (Leastest) */
    // TODO: AInd a_unknown  /* Animate Dead (item, special) */
    // TODO: AInm a_unknown  /* StrengthMod +2 */
    // TODO: AIos a_unknown  /* Slow */
    // TODO: AIp1 a_unknown  /* Potion of Rejuv I */
    // TODO: AIp2 a_unknown  /* Potion of Rejuv II */
    // TODO: AIp3 a_unknown  /* Potion of Rejuv III */
    // TODO: AIp4 a_unknown  /* Potion of Rejuv IV */
    // TODO: AIp5 a_unknown  /* Scroll of Rejuv I */
    // TODO: AIp6 a_unknown  /* Scroll of Rejuv II */
    // TODO: AIpg a_unknown  /* Purge(orb) */
    // TODO: AIpl a_unknown  /* Potion of Mana Regen(lesser) */
    // TODO: AIpr a_unknown  /* Potion of Mana Regen(greater) */
    // TODO: AIps a_unknown  /* Purge(Totem, SP) */
    // TODO: AIpx a_unknown  /* Permanent Hit point Bonus (small) */
    // TODO: AIpz a_unknown  /* Penguin Squeek */
    // TODO: AIrl a_unknown  /* Potion of Life Regen */
    // TODO: AIrn a_unknown  /* ItemRegenMana lesser */
    // TODO: AIrr a_unknown  /* Roar */
    // TODO: AIrx a_unknown  /* Resurrection - Item */
    // TODO: AIs1 a_unknown  /* StrengthBonus (+1) */
    // TODO: AIs2 a_unknown  /* Attack Speed Increase(greater) */
    // TODO: AIs3 a_unknown  /* StrengthBonus (+3) */
    // TODO: AIs4 a_unknown  /* StrengthBonus (+4) */
    // TODO: AIs6 a_unknown  /* StrengthBonus (+6) */
    // TODO: AIse a_unknown  /* Silence(Item) */
    // TODO: AIsl a_unknown  /* Scroll of Life Regen */
    // TODO: AIsw a_unknown  /* Sentry Ward */
    // TODO: AIsx a_unknown  /* Attack Speed Increase */
    // TODO: AIsz a_unknown  /* Slow Poison (item) */
    // TODO: AIt6 a_unknown  /* AttackBonus */
    // TODO: AIt9 a_unknown  /* AttackBonus */
    // TODO: AItc a_unknown  /* AttackBonus */
    // TODO: AItf a_unknown  /* AttackBonus */
    // TODO: AItg a_unknown  /* AttackBonus +1 */
    // TODO: AIth a_unknown  /* AttackBonus +2 */
    // TODO: AIti a_unknown  /* AttackBonus +4 */
    // TODO: AItj a_unknown  /* AttackBonus +5 */
    // TODO: AItk a_unknown  /* AttackBonus +7 */
    // TODO: AItl a_unknown  /* AttackBonus +8 */
    // TODO: AItm a_unknown  /* IntelligenceMod +2 */
    // TODO: AItn a_unknown  /* AttackBonus +10 */
    // TODO: AItx a_unknown  /* AttackBonus +20 */
    // TODO: AIuf a_unknown  /* Unholy Frenzy (item) */
    // TODO: AIuv a_unknown  /* ItemUltravision */
    // TODO: AIuw a_unknown  /* FigurineUrsaWarrior */
    // TODO: AIv1 a_unknown  /* ItemInvis (Lesser) */
    // TODO: AIv2 a_unknown  /* ItemInvis (Greater) */
    // TODO: AIvl a_unknown  /* ItemInvul */
    // TODO: AIwm a_unknown  /* Watery Minion (item) */
    // TODO: AIx1 a_unknown  /* (All + 1) */
    // TODO: AIx2 a_unknown  /* (All + 2) */
    // TODO: AIx3 a_unknown  /* (All + 3) */
    // TODO: AIx4 a_unknown  /* (All + 4) */
    // TODO: AIx5 a_unknown  /* Crown of Kings (All + 5) */
    // TODO: AIxk a_unknown  /* Beserk (item) */
    // TODO: AIxs a_unknown  /* Anti-magic Shield */
    // TODO: ANak a_unknown  /* Orb of Annihilation (Quill Spray) */
    // TODO: ANb2 a_unknown  /* Bash (maul , SP Bear, level 3) */
    // TODO: ANbh a_unknown  /* Bash (Beastmaster Bear) */
    // TODO: ANbl a_unknown  /* Blink(Beastmaster Bear) */
    // TODO: ANc1 a_unknown  /* Tinkerer - Cluster Rockets (Level 1) */
    // TODO: ANc2 a_unknown  /* Tinkerer - Cluster Rockets (Level 2) */
    // TODO: ANc3 a_unknown  /* Tinkerer - Cluster Rockets (Level 3) */
    // TODO: ANd1 a_unknown  /* Tinkerer - Demolish (Level 1) */
    // TODO: ANd2 a_unknown  /* Tinkerer - Demolish (Level 2) */
    // TODO: ANd3 a_unknown  /* Tinkerer - Demolish (Level 3) */
    // TODO: ANfa a_unknown  /* Sea Witch - Frost Arrows */
    // TODO: ANg1 a_unknown  /* Tinkerer - Robo-Goblin (Level 1) */
    // TODO: ANg2 a_unknown  /* Tinkerer - Robo-Goblin (Level 2) */
    // TODO: ANg3 a_unknown  /* Tinkerer - Robo-Goblin (Level 3) */
    // TODO: ANr3 a_unknown  /* Rain of Chaos(Button 0,2) */
    // TODO: ANre a_unknown  /* Neutral Regen (mana only) */
    // TODO: ANrn a_unknown  /* Mannoroth - Reincarnation */
    // TODO: ANs1 a_unknown  /* Tinkerer - Summon Factory (Level 1) */
    // TODO: ANs2 a_unknown  /* Tinkerer - Summon Factory (Level 2) */
    // TODO: ANs3 a_unknown  /* Tinkerer - Summon Factory (Level 3) */
    // TODO: ANt2 a_unknown  /* Thorny Shield (Dragon Turtle) */
    // TODO: ANta a_unknown  /* Taunt(Creep) */
    // TODO: ANth a_unknown  /* Thorny Shield (Creep) */
    // TODO: ANtr a_unknown  /* Detect(War Eagle) */
    // TODO: ANwk a_unknown  /* Wind Walk */
    // TODO: AOsw a_unknown  /* Shadow Hunter - Serpent Ward */
    // TODO: APdi a_unknown  /* PowerupDispelAoe */
    // TODO: APh1 a_unknown  /* PowerupHealAoeLesser */
    // TODO: APh2 a_unknown  /* PowerupHealAoe */
    // TODO: APh3 a_unknown  /* PowerupHealAoeGreater */
    // TODO: APmg a_unknown  /* RuneManaRestoreGreaterAoe */
    // TODO: APmr a_unknown  /* RuneManaRestoreAoe */
    // TODO: APra a_unknown  /* RuneRestoreAoe */
    // TODO: APrl a_unknown  /* Rune of Lesser Resurrection */
    // TODO: APrr a_unknown  /* Rune of Greater Resurrection */
    // TODO: APsa a_unknown  /* RuneSpeedAoe */
    // TODO: APwt a_unknown  /* Rune of the Watcher */
    // TODO: Aam2 a_unknown  /* Anti-magic Shield (Matrix) */
    // TODO: Aap1 a_unknown  /* Aura - Plague (Abomination) */
    // TODO: Aap2 a_unknown  /* Aura - Plague (Plague Ward) */
    // TODO: Aap3 a_unknown  /* Aura - Plague (Creep) */
    // TODO: Aap4 a_unknown  /* Aura - Plague (Creep gfx) */
    // TODO: Abdl a_unknown  /* Blight Dispel (Large) */
    // TODO: Abds a_unknown  /* Blight Dispel (Small) */
    // TODO: Abgl a_unknown  /* Blight Growth (Large) */
    // TODO: Abgs a_unknown  /* Blight Growth (Small) */
    // TODO: Abu2 a_unknown  /* Burrow(scarab lvl 2) */
    // TODO: Abu3 a_unknown  /* Burrow(scarab lvl 3) */
    // TODO: Abu5 a_unknown  /* Burrow(Barbed Arachnathid) */
    // TODO: Acdb a_unknown  /* Chen- Drunken Brawler */
    // TODO: Ache a_unknown  /* Chain Dispel */
    // TODO: Acht a_unknown  /* Howl of Terror */
    // TODO: Acn2 a_unknown  /* Cannibalize (Abomination) */
    // TODO: Aco2 a_unknown  /* Couple Instant (Archer) */
    // TODO: Aco3 a_unknown  /* Couple Instant (Hippogryph) */
    // TODO: Adcn a_unknown  /* Disenchant(new) */
    // TODO: Adsm a_unknown  /* Dispel Magic (creep) */
    // TODO: Adt1 a_unknown  /* Detect (Sentry Ward) */
    // TODO: Adtg a_unknown  /* Detect (general) */
    // TODO: Aegr a_unknown  /* Elune's Grace */
    // TODO: Aenr a_unknown  /* Entangling Roots (creep) */
    // TODO: Aenw a_unknown  /* Entangling Seaweed */
    // TODO: Aesr a_unknown  /* Sentinel (no research) */
    // TODO: Afa2 a_unknown  /* Faerie Fire */
    // TODO: Afbt a_unknown  /* Feedback(Arcane Tower) */
    // TODO: Afod a_unknown  /* Finger of Death */
    // TODO: Afr2 a_unknown  /* Frost Attack (1,2) */
    // TODO: Ahr2 a_unknown  /* Harvest Lumber (Arch ghouls) */
    // TODO: Ahr3 a_unknown  /* Harvest Lumber (shredder) */
    // TODO: Ahrp a_unknown  /* Repair (Human) */
    // TODO: Aien a_unknown  /* Inventory(2 slot unit) Night Elf */
    // TODO: Aihn a_unknown  /* Inventory(2 slot unit) Human */
    // TODO: Aion a_unknown  /* Inventory(2 slot unit) Orc */
    // TODO: Aiun a_unknown  /* Inventory(2 slot unit) Undead */
    // TODO: Amb2 a_unknown  /* Mana Battery (Obsidian Statue) */
    // TODO: Ambb a_unknown  /* Mana Burn (Hotkey B) */
    // TODO: Ambd a_unknown  /* Mana Burn (demon) */
    // TODO: Amgr a_unknown  /* Moon Glaive (No research) */
    // TODO: Amnb a_unknown  /* Mana Burn (demon) */
    // TODO: Amnx a_unknown  /* Death Damage (mine) */
    // TODO: Amnz a_unknown  /* Death Damage (mine BIG) */
    // TODO: Ane2 a_unknown  /* Neutral Building (any unit) */
    // TODO: Anh1 a_unknown  /* Heal (Creep Normal) */
    // TODO: Anh2 a_unknown  /* Heal (Creep High) */
    // TODO: Ansk a_unknown  /* Hardened Skin(Naga Turtle) */
    // TODO: Apak a_unknown  /* Inventory (Pack Mule) */
    // TODO: Apg2 a_unknown  /* Purge */
    // TODO: Apmf a_unknown  /* Permanent Immolation (flying) */
    // TODO: Ara2 a_unknown  /* Roar */
    // TODO: Argd a_unknown  /* Return (Gold) */
    // TODO: Argl a_unknown  /* Return (Gold & Lumber) */
    // TODO: Arll a_unknown  /* Regen Life */
    // TODO: Arlm a_unknown  /* Return (Lumber) */
    // TODO: Aro1 a_unknown  /* Root (Ancients) */
    // TODO: Aro2 a_unknown  /* Root (Ancient Protector) */
    // TODO: Asd2 a_unknown  /* Self Destruct 2 (Clockwerk Goblins) */
    // TODO: Asd3 a_unknown  /* Self Destruct 3 (Clockwerk Goblins) */
    // TODO: Asdg a_unknown  /* Self Destruct (Clockwerk Goblins) */
    // TODO: Aslp a_unknown  /* Summon Lobstrok Prawns */
    // TODO: Asp1 a_unknown  /* Sphere (SoV Level 1) */
    // TODO: Asp2 a_unknown  /* Sphere (SoV Level 2) */
    // TODO: Asp3 a_unknown  /* Sphere (SoV Level 3) */
    // TODO: Asp4 a_unknown  /* Sphere (SoV Level 4) */
    // TODO: Asp5 a_unknown  /* Sphere (SoV Level 5) */
    // TODO: Asp6 a_unknown  /* Sphere (SoV Level 6) */
    // TODO: Aspp a_unknown  /* Rune of Spirit Link */
    // TODO: Aspt a_unknown  /* Spawn Hydra Hatchling */
    // TODO: Aspy a_unknown  /* Spawn Hydra */
    // TODO: Awfb a_unknown  /* Fire Bolt (warlock) */
    // TODO: Awh2 a_unknown  /* Wisp Harvest (Invulnerable) */
    // TODO: Awrg a_unknown  /* War Stomp (sea giant) */
    // TODO: Awrh a_unknown  /* War Stomp (hydra) */
    // TODO: Awrs a_unknown  /* War Stomp (creep) */
    // TODO: SCae a_unknown  /* Aura - Endurance (Creep) */
    // TODO: SCc1 a_unknown  /* Cyclone (Cenarius) */
    // TODO: SCva a_unknown  /* Vampiric attack */
    // TODO: SNdd a_unknown  /* Super Death and Decay */
    // TODO: SNeq a_unknown  /* Super Earthquake */
    // TODO: SNin a_unknown  /* Tichondrius - Inferno */
    // TODO: Sbsk a_unknown  /* Berserker Upgrade */
    // TODO: Sbtl a_unknown  /* Battlestations (Chaos) */
    // TODO: Sca1 a_unknown  /* Chaos (Grunt) */
    // TODO: Sca2 a_unknown  /* Chaos (Raider) */
    // TODO: Sca3 a_unknown  /* Chaos (Shaman) */
    // TODO: Sca4 a_unknown  /* Chaos (Kodo) */
    // TODO: Sca5 a_unknown  /* Chaos (Peon) */
    // TODO: Sca6 a_unknown  /* Chaos (Grom) */
    // TODO: Sch2 a_unknown  /* Cargo Hold (Meat Wagon) */
    // TODO: Sch3 a_unknown  /* Cargo Hold (Transport) */
    // TODO: Sch4 a_unknown  /* Cargo Hold (Tank) */
    // TODO: Sch5 a_unknown  /* Cargo Hold (Ship) */
    // TODO: Scri a_unknown  /* Cripple (Warlock) */
    // TODO: Sdro a_unknown  /* Drop */
    // TODO: Slo2 a_unknown  /* Load (Entangled Gold Mine) */
    // TODO: Slo3 a_unknown  /* Load (Navies) */
    // TODO: Sloa a_unknown  /* Load (Burrow) */
    // TODO: Srtt a_unknown  /* Tank Upgrade */
    // TODO: Sshm a_unknown  /* Shadow Meld (Instant) */
    // TODO: Stpm a_unknown  /* Pilot Tank (Mortar Team) */
    // TODO: Stpr a_unknown  /* PIlot Tank (Rifleman) */
    // TODO: Suhf a_unknown  /* Unholy Frenzy (Warlock) */
    /* END GENERATED TODO ABILITIES */

    /* Passive regeneration base codes remain explicit outside generated TODOs. */
    { "Aarm", CAbilityPassive, AB_PASSIVE },  /* Mana Regeneration Aura */
    { "Aoar", CAbilityPassive, AB_PASSIVE },  /* Healing Ward Aura */
    { "Aabr", CAbilityPassive, AB_PASSIVE },  /* Aura of Blight */
};

/* Build a compact unique procedure list once, rather than scan the whole registry per unit tick. */
static abilityProc_t ability_updates[sizeof(abilitylist) / sizeof(abilitylist[0])];
static DWORD num_updates;
static abilityitem_t innate_items[sizeof(abilitylist) / sizeof(abilitylist[0])];
static DWORD num_innate;

/* ROC/TFT physical data columns are normalized by the AbilityData DDX schema. */
FLOAT AB_Data(LPCSTR classname, DWORD level, DWORD index) {
    abilityLevel_t const *row = G_AbilityLevel(FS_SLKKey(classname), level);
    index = MAX(1, MIN(index, 9));
    return row->data[index - 1].number;
}

DWORD AB_DataId(LPCSTR classname, DWORD level, DWORD index) {
    abilityLevel_t const *row = G_AbilityLevel(FS_SLKKey(classname), level);
    index = MAX(1, MIN(index, 9));
    return row->data[index - 1].id;
}

/* Order names belong to their ability, including orders for preplaced alternate forms. */
ability_t const *FindAbilityByOrder(LPCSTR order) {
    if (!order) return NULL;
    FOR_LOOP(i, game.num_abilities) {
        ability_t const *ability = abilitylist + i;
        if (!ability->orders || !ability->proc) continue;
        for (LPCSTR const *name = ability->orders; *name; name++)
            if (!strcmp(*name, order)) return ability;
    }
    return NULL;
}

/* Persistent effects can outlive their active order; the callback owns its per-unit state checks. */
void S_RunAbilityUpdates(LPEDICT ent) {
    FOR_LOOP(i, num_updates)
        ability_updates[i](ent, A_UPDATE, NULL);
}

/* Unit-data abilities exist independently of command-card slots. Notifications visit every owner;
 * idle and acquisition queries stop when an owner consumes the decision. */
BOOL S_UnitAbilityEvent(LPEDICT ent, abilityMsg_t msg) {
    BOOL handled = false;
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

ability_t const *FindAbilityByClassname(LPCSTR classname) {
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
ability_t const *FindAbilityForCommand(LPCSTR classname) {
    if (!classname || !*classname) {
        return NULL;
    }
    if (strlen(classname) != 4) {
        return FindAbilityByClassname(classname);
    }
    return FindAbilityByClassname(GetClassName(G_AbilityCodeName(classname)));
}

/* Keep the requested rawcode even when AbilityData resolves its code to a shared implementation. */
abilityitem_t S_AbilityItem(DWORD code) {
    return MAKE(abilityitem_t, .code = code, .ability = code ? FindAbilityForCommand(GetClassName(code)) : NULL);
}

/* Dispatch is synchronous and retains the concrete row and authored rawcode in the typed payload. */
BZ_ABILITY_PROC(S_AbilityMessage) {
    ability_t const *ability = call && call->item ? call->item->ability : NULL;
    return ability && ability->proc ? ability->proc(ent, msg, call) : false;
}

void S_EnableAbility(LPEDICT ent, DWORD code) {
    abilityitem_t item = S_AbilityItem(code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    if (item.ability) S_AbilityMessage(ent, A_ENABLE, &call);
}

void S_DisableAbility(LPEDICT ent, DWORD code) {
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
    changed = MAKE(abilityCall_t, .item = &item, .level = (DWORD)S_AbilityMessage(ent, A_LEVEL, &query));
    S_AbilityMessage(ent, A_LEVEL_CHANGED, &changed);
}

/* The selected rawcode is shared state; each procedure owns its autocast policy and side effects. */
BOOL G_UnitAutocastIsOn(LPEDICT ent, DWORD code) {
    abilityitem_t item = S_AbilityItem(code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    return ent && code && ent->autocast_code == code && item.ability && (item.ability->flags & AB_AUTOCAST) &&
        G_UnitAbilityLevel(ent, code) && S_AbilityMessage(ent, A_AUTOCAST_ON, &call);
}

/* Keeping the alias through the UI and scheduler preserves authored cost, range and effect data. */
BOOL G_SetUnitAutocast(LPEDICT ent, DWORD code, BOOL enabled) {
    abilityitem_t item = S_AbilityItem(code), old;
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .enabled = enabled);
    if (!ent || !item.ability || !(item.ability->flags & AB_AUTOCAST) ||
        (enabled && !G_UnitAbilityLevel(ent, code))) return false;
    old = S_AbilityItem(ent->autocast_code);
    if (!enabled && old.code != code) return true;
    abilityCall_t prev = MAKE(abilityCall_t, .item = &old, .enabled = false);
    BOOL switched = enabled && old.code && old.code != code;
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
BOOL G_TryUnitAutocast(LPEDICT ent) {
    if (!ent || !(ent->aiflags & AI_AUTOCAST_ACTIVE)) return false;
    abilityitem_t item = S_AbilityItem(ent->autocast_code);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    return G_UnitAutocastIsOn(ent, item.code) && S_AbilityMessage(ent, A_AUTOCAST_ACQUIRE, &call);
}

DWORD FindAbilityIndex(LPCSTR classname) {
    FOR_LOOP(i, game.num_abilities) {
        if (!abilitylist[i].classname)
            continue;
        if (!strcmp(abilitylist[i].classname, classname))
            return i;
    }
    return 255;
}

/* Shared casts and bespoke commands expose the same capability to HUD and item callers. */
BOOL S_AbilityHasCommand(ability_t const *ability) {
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
    FOR_LOOP(i, game.num_abilities) {
        ability_t *entry = &abilitylist[i];
        abilityitem_t item = MAKE(abilityitem_t, .code = strlen(entry->classname) == 4 ? FS_SLKKey(entry->classname) : 0,
                                  .ability = entry);
        abilityCall_t call = MAKE(abilityCall_t, .item = &item, .classname = entry->classname);
        if (!entry->proc) gi.error("InitAbilities: %s has no procedure", entry->classname);
        entry->proc(NULL, A_INIT, &call);
        if (entry->flags & AB_INNATE) innate_items[num_innate++] = item;
        if (entry->flags & AB_UPDATE) {
            DWORD n;
            for (n = 0; n < num_updates && ability_updates[n] != entry->proc; n++) {}
            if (n == num_updates) ability_updates[num_updates++] = entry->proc;
        }
    }
}

ability_t const *GetAbilityByIndex(DWORD index) {
    if (index >= game.num_abilities)
        return NULL;
    return abilitylist + index;
}

DWORD GetAbilityIndex(abilityProc_t proc) {
    FOR_LOOP(i, game.num_abilities) {
        if (abilitylist[i].proc == proc) {
            return i;
        }
    }
    return 255;
}
