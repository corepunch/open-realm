/*
 * ability_map — Map WC3 ability rawcodes to Game.dll C++ class names.
 *
 * Reads Game.dll RTTI class names and AbilityData.slk, then outputs
 * a complete mapping of rawcode → C++ classname for every ability.
 *
 * Usage:
 *   ability_map -dll data/Warcraft3demo/Game.dll -data 'data/Warcraft III'
 */
#include "tool_common.h"
#include "common/stb_slk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define RK(s) ((DWORD)((unsigned char)(s)[0] | ((unsigned char)(s)[1] << 8) | \
                       ((unsigned char)(s)[2] << 16) | ((unsigned char)(s)[3] << 24)))

/* ---- Minimal PE parser for RTTI extraction ---- */
static char **pe_rtti_strings(const char *path, size_t *out_count) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(fsize);
    if (!data) { fclose(f); return NULL; }
    fread(data, 1, fsize, f);
    fclose(f);

    if (fsize < 64 || *(WORD *)data != 0x5A4D) { free(data); return NULL; }

    /* Scan entire binary for MSVC RTTI ".?AVCAbility...@@" pattern. */
    size_t cap = 256, count = 0;
    char **names = malloc(cap * sizeof(char *));
    const char *p = data;
    const char *end = data + fsize;

    while (p < end - 4) {
        if (p[0] == '.' && p[1] == '?' && p[2] == 'A' && p[3] == 'V') {
            const char *start = p + 4;
            const char *at = start;
            while (at < end && *at != '@') at++;
            if (at + 1 < end && at[0] == '@' && at[1] == '@') {
                size_t len = at - start;
                if (len > 8 && len < 256 && !strncmp(start, "CAbility", 8)) {
                    if (count >= cap) { cap *= 2; names = realloc(names, cap * sizeof(char *)); }
                    names[count] = malloc(len + 1);
                    memcpy(names[count], start, len);
                    names[count][len] = '\0';
                    count++;
                }
            }
            p = start;
        } else {
            p++;
        }
    }
    free(data);

    /* Deduplicate and sort */
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; ) {
            if (!strcmp(names[i], names[j])) { free(names[j]); names[j] = names[count - 1]; count--; }
            else j++;
        }
    }
    for (size_t i = 1; i < count; i++) {
        char *key = names[i];
        size_t j = i;
        while (j > 0 && strcmp(names[j - 1], key) > 0) { names[j] = names[j - 1]; j--; }
        names[j] = key;
    }

    *out_count = count;
    return names;
}

/* ---- AbilityData struct (minimal) ---- */
typedef struct {
    DWORD id, code, uberAlias;
    LPCSTR comments, sort, race;
    LONG version, levels, reqLevel, levelSkip, priority;
    BOOL useInEditor, hero, item, checkDep, InBeta;
    LPCSTR targs[4];
    FLOAT cast[4], dur[4], heroDur[4], cool[4], cost[4], area[4], range[4];
    FLOAT data[4][9];
    DWORD dataId[4][9];
    DWORD unitID[4];
    LPCSTR buffID[4], efctID[4];
    LPCSTR castCheck, durCheck, heroDurCheck, coolCheck, costCheck, areaCheck, rangeCheck;
} AbilityData_t;

#define AB_F(N,F,L,T) { N, offsetof(AbilityData_t, F[L]), T }
#define AB_D(N,L,S) { N, offsetof(AbilityData_t, data[L][S]), STB_SLK_FLOAT }
#define AB_ID(N,L,S) { N, offsetof(AbilityData_t, dataId[L][S]), STB_SLK_FOURCC }
static slkField_t const ability_schema[] = {
    { "",            offsetof(AbilityData_t, id),          STB_SLK_FOURCC },
    { "code",        offsetof(AbilityData_t, code),        STB_SLK_FOURCC },
    { "uberAlias",   offsetof(AbilityData_t, uberAlias),   STB_SLK_FOURCC },
    { "comments",    offsetof(AbilityData_t, comments),    STB_SLK_STR    },
    { "version",     offsetof(AbilityData_t, version),     STB_SLK_INT    },
    { "hero",        offsetof(AbilityData_t, hero),        STB_SLK_BOOL   },
    { "item",        offsetof(AbilityData_t, item),        STB_SLK_BOOL   },
    { "sort",        offsetof(AbilityData_t, sort),        STB_SLK_STR    },
    { "race",        offsetof(AbilityData_t, race),        STB_SLK_STR    },
    { "levels",      offsetof(AbilityData_t, levels),      STB_SLK_INT    },
    { "reqLevel",    offsetof(AbilityData_t, reqLevel),    STB_SLK_INT    },
    { NULL, 0, 0 }
};
#undef AB_ID
#undef AB_D
#undef AB_F

/* ---- Retail rawcode → C++ classname mapping ----
 * The rawcode relationship is currently transcribed from retail analysis;
 * the supplied DLL's RTTI is used below to validate each classname. */
typedef struct { const char *rawcode; const char *classname; } rawcode_map_t;
static rawcode_map_t const classmap[] = {
    /* Human Hero */
    {"AHhb", "CAbilityHolyLight"},       {"AHad", "CAbilityAuraBrilliance"},
    {"AHwe", "CAbilityWaterElemental"},  {"AHbz", "CAbilityBlizzard"},
    {"AHtb", "CAbilityThunderBolt"},     {"AHca", "CAbilityPoisonAttack"},
    {"AHds", "CAbilityInvulnerable"},    {"AHmt", "CAbilityMassTeleport"},
    {"AHav", "CAbilityAttributeMod"},    {"AHbh", "CAbilityBash"},
    {"AHtc", "CAbilityThunderClap"},     {"AHre", "CAbilityRevive"},
    {"AHfa", "CAbilityPoisonAttack"},    {"AHab", "CAbilityAuraBrilliance"},
    {"AHbn", "CAbilitySpell"},           {"AHfs", "CAbilityRainOfFire"},
    {"AHdr", "CAbilitySpell"},           {"AHpx", "CAbilityMorph"},
    {"AHbu", "CAbilityHumanBuild"},      {"AHta", "CAbilitySpell"},
    /* Orc Hero */
    {"AOsf", "CAbilitySpiritWolf"},      {"AOmi", "CAbilityMirrorImage"},
    {"AOcr", "CAbilityCriticalStrike"},  {"AOwk", "CAbilityWindWalk"},
    {"AOww", "CAbilityWhirlwind"},       {"AOcl", "CAbilityChainLightning"},
    {"AOfs", "CAbilityFarSight"},        {"AOae", "CAbilityAuraEndurance"},
    {"AOsh", "CAbilityShockwave"},       {"AOws", "CAbilityWarStomp"},
    {"AOre", "CAbilityReincarnation"},   {"AOeq", "CAbilityEarthquake"},
    {"AOhw", "CAbilitySpell"},           {"AOhx", "CAbilitySpell"},
    {"AOsw", "CAbilityMorph"},           {"AOvd", "CAbilitySpell"},
    {"AObu", "CAbilityOrcBuild"},
    /* Night Elf Hero */
    {"AEbl", "CAbilityMove"},            {"AEfk", "CAbilityBounce"},
    {"AEsh", "CAbilityThunderBolt"},     {"AEim", "CAbilityImmolation"},
    {"Aeat", "CAbilityHarvest"},         {"Ambt", "CAbilityRegenMana"},
    {"Aroo", "CAbilityMorph"},           {"AEev", "CAbilityEvasion"},
    {"AEmb", "CAbilitySpell"},           {"AEme", "CAbilityMorph"},
    {"AEer", "CAbilityTarget"},          {"AEfn", "CAbilityForceOfNature"},
    {"AEah", "CAbilityAuraSpell"},       {"AEtq", "CAbilityAuraRegenLife"},
    {"AEst", "CAbilityButton"},          {"AEsf", "CAbilityWhirlwind"},
    {"AEar", "CAbilityAura"},            {"AEpa", "CAbilityPoisonAttack"},
    {"AEsv", "CAbilityMorph"},           {"AEbu", "CAbilityNightElfBuild"},
    /* Undead Hero */
    {"AUcs", "CAbilityCarrionSwarm"},    {"AUdc", "CAbilitySpell"},
    {"AUdp", "CAbilitySpell"},           {"AUau", "CAbilityAuraRegenLife"},
    {"AUsl", "CAbilityCreepSleep"},      {"AUav", "CAbilityAura"},
    {"AUan", "CAbilityRevenge"},         {"AUdd", "CAbilityRainOfFire"},
    {"AUfn", "CAbilityFrostNova"},       {"AUfa", "CAbilitySpell"},
    {"AUfu", "CAbilitySpell"},           {"AUdr", "CAbilitySpell"},
    {"AUin", "CAbilityStomp"},           {"AUcb", "CAbilityMorph"},
    {"AUim", "CAbilityStomp"},           {"AUls", "CAbilityCreepSleep"},
    {"AUts", "CAbilityAura"},            {"AUbu", "CAbilityUndeadBuild"},
    /* Neutral Hero */
    {"ANfb", "CAbilityCreepThunderBolt"},{"ANfs", "CAbilityRainOfFire"},
    {"ANdr", "CAbilitySpell"},           {"ANch", "CAbilitySpell"},
    {"ANrf", "CAbilityRainOfFire"},      {"ANin", "CAbilityCreepThunderBolt"},
    {"ANfd", "CAbilitySpell"},           {"ANdp", "CAbilityMassTeleport"},
    {"ANrc", "CAbilityRainOfFire"},      {"ANdc", "CAbilitySpell"},
    {"ANsl", "CAbilitySpell"},           {"ANcl", "CAbilitySpell"},
    {"ANst", "CAbilityStomp"},           {"ANsg", "CAbilityForceOfNature"},
    {"ANsq", "CAbilityForceOfNature"},   {"ANsw", "CAbilityForceOfNature"},
    {"ANba", "CAbilityPoisonAttack"},    {"ANsi", "CAbilitySpell"},
    {"ANfl", "CAbilityBounce"},          {"ANfa", "CAbilityPoisonAttack"},
    {"ANto", "CAbilityWhirlwind"},       {"ANms", "CAbilityAura"},
    {"ANbf", "CAbilityImmolation"},      {"ANdb", "CAbilityRevenge"},
    {"ANdh", "CAbilitySpell"},           {"ANef", "CAbilityButton"},
    {"ANdo", "CAbilitySpell"},           {"ANht", "CAbilityHealingWard"},
    {"ANca", "CAbilityChainLightning"},  {"ANab", "CAbilityButton"},
    {"ANcr", "CAbilityCriticalStrike"},  {"ANhs", "CAbilityHeal"},
    {"ANtm", "CAbilityAuraRegenLife"},   {"ANeg", "CAbilityEvasion"},
    {"ANcs", "CAbilitySpell"},           {"ANrg", "CAbilityRegenBase"},
    {"ANsy", "CAbilityButton"},          {"ANde", "CAbilityButton"},
    {"ANic", "CAbilityInnerFire"},       {"ANia", "CAbilityInnerFire"},
    {"ANso", "CAbilitySpell"},           {"ANlm", "CAbilityLightningShield"},
    {"ANvc", "CAbilitySpell"},           {"ANmo", "CAbilityBounce"},
    {"ANwm", "CAbilityWarStomp"},        {"ANbr", "CAbilityBash"},
    {"ANbs", "CAbilitySpell"},           {"ANpr", "CAbilityButton"},
    {"ANsa", "CAbilityBounce"},          {"ANss", "CAbilityBounce"},
    {"ANse", "CAbilitySpell"},           {"AGbu", "CAbilityNeutralBuild"},
    /* Unit abilities — Harvest/Build */
    {"Ahar", "CAbilityHarvest"},         {"Arep", "CAbilityRepair"},
    {"Agld", "CAbilityGoldMine"},        {"Agl2", "CAbilityGoldMine"},
    {"Abgm", "CAbilityGoldMineBase"},    {"Abli", "CAbilityButton"},
    {"Aaha", "CAbilityHarvest"},         {"Artn", "CAbilityReturn"},
    {"Awha", "CAbilityHarvest"},         {"Ahrl", "CAbilityHarvestLumber"},
    {"Aent", "CAbilityGoldMine"},        {"Aegm", "CAbilityGoldMine"},
    {"Amic", "CAbilityBaseBuild"},       {"Amil", "CAbilityMorph"},
    {"Acar", "CAbilityCargoHold"},       {"Abun", "CAbilityBunker"},
    {"Aenc", "CAbilityCargoHold"},       {"Aloa", "CAbilityCargoLoad"},
    {"Adro", "CAbilityCargoDrop"},       {"Adri", "CAbilityCargoDropInstant"},
    {"Astd", "CAbilityStandDown"},       {"Avul", "CAbilityInvulnerable"},
    {"AInv", "CAbilityInventory"},       {"Aneu", "CAbilityNeutralBuild"},
    {"Apit", "CAbilityBaseSell"},        {"Aall", "CAbilityButton"},
    {"Acoi", "CAbilityMorph"},           {"Apxf", "CAbilityPermImmolation"},
    {"Aren", "CAbilityRepair"},          {"Arst", "CAbilityRepair"},
    {"Amov", "CAbilityMove"},
    /* Unit abilities — Combat/Passive */
    {"Aadm", "CAbilityAutoDispelMagic"},{"Aalr", "CAbilityAlarm"},
    {"Aams", "CAbilityMagicImmunity"},   {"Aatk", "CAbilityAttack"},
    {"Aap1", "CAbilityOnFire"},          {"Aoar", "CAbilityAuraRegenLife"},
    {"Aabr", "CAbilityAuraRegenLife"},   {"Aakb", "CAbilityAuraCommand"},
    {"Abtl", "CAbilityBattlestations"},  {"Abrf", "CAbilityMorph"},
    {"Ablo", "CAbilityBloodlust"},       {"Acan", "CAbilityCannibalize"},
    {"Advc", "CAbilityDevourCargo"},     {"Acor", "CAbilityBounce"},
    {"Acoa", "CAbilityMorph"},           {"Acoh", "CAbilityMorph"},
    {"Acri", "CAbilityCripple"},         {"Acrs", "CAbilityAutoTargetSpell"},
    {"Acyc", "CAbilityMorph"},           {"Adef", "CAbilityDefend"},
    {"Adtn", "CAbilityButton"},          {"Adev", "CAbilityDevour"},
    {"Adis", "CAbilityDispelMagic"},     {"Afae", "CAbilityAutoTargetSpell"},
    {"Afla", "CAbilityButton"},          {"Afrz", "CAbilityFrostNova"},
    {"Ahea", "CAbilityHeal"},            {"Ahwd", "CAbilityHealingWard"},
    {"Ainf", "CAbilityInnerFire"},       {"Aivs", "CAbilityPermInvis"},
    {"Alsh", "CAbilityLightningShield"}, {"Amim", "CAbilityMagicImmunity"},
    {"Amgl", "CAbilityBounce"},          {"Andt", "CAbilityEvilEye"},
    {"ANpi", "CAbilityPermImmolation"},  {"Apiv", "CAbilityPermInvis"},
    {"Apoi", "CAbilityPoisonAttack"},    {"Aply", "CAbilitySimpleSpell"},
    {"Apos", "CAbilityCreepSleep"},      {"Aps2", "CAbilityCreepSleep"},
    {"Awar", "CAbilityBounce"},          {"Aprg", "CAbilityLightningPurge"},
    {"Arai", "CAbilitySimpleSpell"},     {"Arav", "CAbilityRavenForm"},
    {"Arej", "CAbilityRegenLife"},       {"Aroa", "CAbilityRoar"},
    {"Asac", "CAbilitySpell"},           {"Asal", "CAbilityButton"},
    {"Asds", "CAbilityButton"},          {"Aesn", "CAbilityButton"},
    {"Aeye", "CAbilityButton"},          {"Ashm", "CAbilityShadowMeld"},
    {"Aslo", "CAbilitySlow"},            {"Aspo", "CAbilityPoisonAttack"},
    {"Astn", "CAbilityMorph"},           {"Asth", "CAbilityBounce"},
    {"Atol", "CAbilityUpgrade"},         {"Auhf", "CAbilityCreepSleep"},
    {"Auns", "CAbilityButton"},          {"Aven", "CAbilityVenomSpear"},
    {"Aweb", "CAbilityAutoTargetSpell"},
    {"Afir", "CAbilityOnFire"},          {"Afih", "CAbilityOnFireHuman"},
    {"Afio", "CAbilityOnFireOrc"},       {"Afin", "CAbilityOnFireNightElf"},
    {"Afiu", "CAbilityOnFireUndead"},    {"Attu", "CAbilityButton"},
    {"ACtb", "CAbilityCreepThunderBolt"},{"ACtc", "CAbilityCreepThunderClap"},
    {"ACrn", "CAbilityCreepReincarnation"},{"ACsp", "CAbilityCreepSleep"},
    {"ACdv", "CAbilityCreepDevour"},     {"ACad", "CAbilityRevenge"},
    {"ACac", "CAbilityAuraCommand"},     {"Atru", "CAbilityTrueSight"},
    {"Agyv", "CAbilityTrueSight"},       {"Adts", "CAbilityMagicSentry"},
    {"Adt1", "CAbilityDetector"},        {"AHer", "CAbilityHero"},
    {"ARal", "CAbilityRally"},           {"Arev", "CAbilityRevive"},
    {"Arng", "CAbilityRevenge"},         {"Awrp", "CAbilityWarp"},
    {"Awan", "CAbilityWander"},          {"Amin", "CAbilityButton"},
    {"Amed", "CAbilityCargoDrop"},       {"Amel", "CAbilityCargoLoad"},
    {"ANbu", "CAbilityNeutralBuild"},    {"ANen", "CAbilityEnsnare"},
    {"ANre", "CAbilityNeutralRegenMana"},{"AAns", "CAbilityNeutralSpell"},
    {"Ansp", "CAbilityNeutralSpell"},    {"Asla", "CAbilitySleepAlways"},
    {"Asod", "CAbilitySimpleSpell"},     {"Assp", "CAbilitySimpleSpell"},
    {"Aspd", "CAbilitySimpleSpell"},     {"Aspa", "CAbilityAttack"},
    {"Aspi", "CAbilitySpiked"},          {"Asta", "CAbilityStasisTrap"},
    {"Alam", "CAbilitySpell"},           {"Achl", "CAbilityCargoLoad"},
    {"Adda", "CAbilityOnFire"},          {"Atlp", "CAbilityCargoLoad"},
    {"Atdp", "CAbilityCargoDrop"},       {"Aimp", "CAbilityBounce"},
    {"Aghi", "CAbilityGhost"},           {"Aeth", "CAbilityGhost"},
    {"Agyd", "CAbilitySimpleSpell"},     {"Agyb", "CAbilityBounce"},
    {"Anhe", "CAbilityHeal"},            {"Aret", "CAbilityRevive"},
    {"Aamk", "CAbilityAttackMod"},       {"Arsw", "CAbilityRevive"},
    {"Aabs", "CAbilityAura"},            {"Aast", "CAbilityAura"},
    {"Aap2", "CAbilityPoisonAttack"},    {"Aap3", "CAbilityPoisonAttack"},
    {"Aap4", "CAbilityPoisonAttack"},    {"Aasl", "CAbilitySlow"},
    {"Aave", "CAbilityStomp"},           {"Aawa", "CAbilityWarStomp"},
    {"Abof", "CAbilityAuraCommand"},     {"Absk", "CAbilityButton"},
    {"Sbsk", "CAbilityButton"},          {"Abds", "CAbilityMorph"},
    {"Abdl", "CAbilityCreepSleep"},      {"Abgs", "CAbilityGhost"},
    {"Abgl", "CAbilityGhost"},           {"ACbf", "CAbilityOnFire"},
    {"Abur", "CAbilityCreepSleep"},      {"Abdt", "CAbilityCreepSleep"},
    {"Sch5", "CAbilityCargoHold"},       {"Sch4", "CAbilityCargoHold"},
    {"Sch3", "CAbilityCargoHold"},       {"Achd", "CAbilityCargoHold"},
    {"Ache", "CAbilityCargoHold"},       {"Sca2", "CAbilityCargoHold"},
    {"Sca3", "CAbilityCargoHold"},       {"Sca4", "CAbilityCargoHold"},
    {"Sca5", "CAbilityCargoHold"},       {"Sca6", "CAbilityCargoHold"},
    {"Aclf", "CAbilityBounce"},          {"Acmg", "CAbilitySpell"},
    {"Acpf", "CAbilityPurge"},           {"Aco2", "CAbilityCargoLoad"},
    {"Aco3", "CAbilityCargoLoad"},       {"Adec", "CAbilityDefend"},
    {"Advm", "CAbilityDispelMagic"},     {"Adch", "CAbilitySpell"},
    {"Adcn", "CAbilitySpell"},           {"Aegr", "CAbilityTarget"},
    {"Aens", "CAbilityEnsnare"},         {"Aetl", "CAbilityButton"},
    {"Aetf", "CAbilityMorph"},           {"Aexh", "CAbilityButton"},
    {"ANfy", "CAbilityOnFire"},          {"Afbk", "CAbilityAura"},
    {"Aflk", "CAbilityButton"},          {"Afsh", "CAbilityButton"},
    {"Afzy", "CAbilitySpell"},           {"Afra", "CAbilityButton"},
    {"Afrb", "CAbilityButton"},          {"Agho", "CAbilityGhost"},
    {"Agra", "CAbilityPoisonAttack"},    {"Assk", "CAbilityButton"},
    {"Ahnl", "CAbilityHeal"},            {"Alit", "CAbilityLightningAttack"},
    {"Aliq", "CAbilityOnFire"},          {"Aloc", "CAbilityButton"},
    {"Amdf", "CAbilityAura"},            {"Amls", "CAbilityAura"},
    {"Amfl", "CAbilityFrostNova"},       {"ANmr", "CAbilityAura"},
    {"Afak", "CAbilitySpell"},           {"ANpa", "CAbilityPoisonAttack"},
    {"Apig", "CAbilityHarvestReturn"},   {"Apsh", "CAbilityStomp"},
    {"Aphx", "CAbilityMorph"},           {"Apts", "CAbilityButton"},
    {"Arbr", "CAbilityButton"},          {"Ahrp", "CAbilityRepair"},
    {"Arpb", "CAbilitySpell"},           {"Arpl", "CAbilitySpell"},
    {"Arpm", "CAbilitySpell"},           {"Arsk", "CAbilityMorph"},
    {"Argd", "CAbilityMove"},            {"Argl", "CAbilityHarvestReturn"},
    {"Arlm", "CAbilityReincarnation"},   {"Aroc", "CAbilityRainOfFire"},
    {"Aro1", "CAbilityRoar"},            {"Aro2", "CAbilityRoar"},
    {"Asid", "CAbilityButton"},          {"Asud", "CAbilitySpell"},
    {"ACtn", "CAbilityCreepThunderClap"},{"Ahid", "CAbilityShadowMeld"},
    {"Asps", "CAbilityCreepSleep"},      {"Asph", "CAbilityPoisonAttack"},
    {"Aspl", "CAbilitySpiritLink"},      {"Asb1", "CAbilitySlow"},
    {"Asb2", "CAbilitySlow"},            {"Asb3", "CAbilitySlow"},
    {"Srtt", "CAbilityMorph"},           {"Atau", "CAbilitySpell"},
    {"Atdg", "CAbilityStomp"},           {"Atsp", "CAbilityAura"},
    {"Atwa", "CAbilityWarStomp"},        {"Auco", "CAbilitySpell"},
    {"Avng", "CAbilityRevenge"},         {"Amec", "CAbilityButton"},
    {"Ashs", "CAbilitySpell"},           {"Aspb", "CAbilityBounce"},
    {"Aste", "CAbilityFigurineRockGolem"},
    /* Item abilities — Stat Bonuses */
    {"AIa1", "CAbilityAttackMod"},       {"AIa3", "CAbilityAttackMod"},
    {"AIa4", "CAbilityAttackMod"},       {"AIa6", "CAbilityAttackMod"},
    {"AIx5", "CAbilityAttackSpeedBonus"},{"AIx1", "CAbilityAttackSpeedBonus"},
    {"AIx2", "CAbilityAttackSpeedBonus"},{"AIx3", "CAbilityAttackSpeedBonus"},
    {"AIx4", "CAbilityAttackSpeedBonus"},
    {"AIs1", "CAbilityDamageBonusBase"}, {"AIs3", "CAbilityDamageBonusBase"},
    {"AIs4", "CAbilityDamageBonusBase"}, {"AIs6", "CAbilityDamageBonusBase"},
    {"AIi1", "CAbilityIntelligenceMod"}, {"AIi3", "CAbilityIntelligenceMod"},
    {"AIi4", "CAbilityIntelligenceMod"}, {"AIi6", "CAbilityIntelligenceMod"},
    {"AIva", "CAbilityAttackMod"},       {"AIbl", "CAbilityOnFire"},
    {"AId1", "CAbilityDefenseBonus"},    {"AId2", "CAbilityDefenseBonus"},
    {"AId3", "CAbilityDefenseBonus"},    {"AId4", "CAbilityDefenseBonus"},
    {"AId5", "CAbilityDefenseBonus"},
    {"AIgf", "CAbilityAttackSpeedBonus"},{"AIgu", "CAbilityStrengthMod"},
    {"AIfd", "CAbilityAgilityMod"},      {"AIff", "CAbilityDefenseBonus"},
    {"AIfr", "CAbilityMaxLifeBonus"},    {"AIfu", "CAbilityMaxManaBonus"},
    {"AIfh", "CAbilityAttackSpeedBonus"},{"AIfa", "CAbilityAgilityMod"},
    {"AIlf", "CAbilityAuraRegenMana"},   {"AIl1", "CAbilityRegenBase"},
    {"AIl2", "CAbilityRegenBase"},       {"AI2m", "CAbilityDefenseBonus"},
    {"AIpx", "CAbilityMaxLifeBonus"},
    /* Item abilities — Consumables */
    {"AIxm", "CAbilityTome"},            {"AIam", "CAbilityTome"},
    {"AIsm", "CAbilityTome"},
    {"AIhe", "CAbilityItemHeal"},        {"AIma", "CAbilityItemManaRestore"},
    {"AIat", "CAbilityAttackBonus"},     {"AIab", "CAbilityAttributeBonus"},
    {"AIim", "CAbilityTome"},            {"AIde", "CAbilityDefenseBonus"},
    {"AIml", "CAbilityMaxLifeBonus"},    {"AImm", "CAbilityMaxManaBonus"},
    {"AIfs", "CAbilityFigurine"},        {"AImi", "CAbilityTome"},
    {"AIem", "CAbilityExperienceMod"},   {"AIlm", "CAbilityLevelMod"},
    {"AIda", "CAbilityItemDefenseAoe"},  {"AIct", "CAbilityButton"},
    {"AItp", "CAbilityItemTownPortal"},  {"AIrs", "CAbilityItemReincarnation"},
    {"AIan", "CAbilitySimpleSpell"},     {"AIxs", "CAbilityMagicImmunity"},
    {"AIha", "CAbilityItemHealAoe"},     {"AIil", "CAbilityItemIllusion"},
    {"AIv1", "CAbilityItemInvis"},       {"AIvu", "CAbilityItemInvul"},
    {"AImr", "CAbilityItemManaRestoreAoe"},{"AIpm", "CAbilityButton"},
    {"AIrt", "CAbilityItemRecall"},      {"AIrm", "CAbilityItemRegenMana"},
    {"AIrc", "CAbilityItemReincarnation"},{"AIre", "CAbilityItemRestore"},
    {"AIra", "CAbilityItemRestoreAoe"},  {"AIsp", "CAbilityItemSpeed"},
    {"AIsa", "CAbilityItemSpeed"},       {"AIsx", "CAbilityAttackSpeedBonus"},
    {"AIms", "CAbilityMoveSpeedBonus"},  {"AIsi", "CAbilitySightBonus"},
    {"AIcf", "CAbilityImmolation"},      {"AIdm", "CAbilityBounce"},
    {"AIdi", "CAbilityItemDispelAoe"},   {"AIlp", "CAbilityLightningPurge"},
    {"AIaa", "CAbilityDamageBonusBase"}, {"AIdf", "CAbilityPoisonAttack"},
    {"AIfb", "CAbilityOnFire"},          {"AIzb", "CAbilityFrostNova"},
    {"AIob", "CAbilityFrostNova"},       {"AIlb", "CAbilityBounce"},
    {"Arel", "CAbilityAuraRegenLife"},   {"AIso", "CAbilitySimpleSpell"},
    {"Asou", "CAbilitySimpleSpell"},     {"AUds", "CAbilityMassTeleport"},
    {"AIfl", "CAbilityButton"},          {"AIfm", "CAbilityButton"},
    {"AIfo", "CAbilityButton"},          {"AIfn", "CAbilityButton"},
    {"AIfe", "CAbilityButton"},
    {"AIll", "CAbilityItemManaRestore"}, {"AIsb", "CAbilityItemHeal"},
    {"AIpb", "CAbilityItemManaRestore"}, {"Apo2", "CAbilityItemInvul"},
    {"AIta", "CAbilityItemTownPortal"},  {"AIh1", "CAbilityItemHeal"},
    {"AIh2", "CAbilityItemHeal"},        {"AIh3", "CAbilityItemHeal"},
    {"AIv2", "CAbilityItemInvis"},       {"AIm1", "CAbilityItemManaRestore"},
    {"AIm2", "CAbilityItemManaRestore"}, {"AIcd", "CAbilityItemTownPortal"},
    {"AImh", "CAbilityItemHeal"},        {"AImb", "CAbilityItemHeal"},
    {"AIbm", "CAbilityItemManaRestore"}, {"AIs2", "CAbilityItemSpeed"},
    {"AIrl", "CAbilityItemDefenseAoe"},  {"AIpr", "CAbilityItemInvul"},
    {"AIsl", "CAbilityItemSpeed"},       {"AIpl", "CAbilityItemManaRestore"},
    {"AIp1", "CAbilityItemTownPortal"},  {"AIp2", "CAbilityItemTownPortal"},
    {"AIp3", "CAbilityItemTownPortal"},  {"AIp4", "CAbilityItemTownPortal"},
    {"AIp5", "CAbilityItemTownPortal"},  {"AIp6", "CAbilityItemTownPortal"},
    {"AIgo", "CAbilityAttackMod"},       {"AIlu", "CAbilityItemHealAoe"},
    {"AIrv", "CAbilityItemHealAoe"},     {"AIdc", "CAbilityItemDefenseAoe"},
    {"AIwb", "CAbilityButton"},          {"AImo", "CAbilityItemManaRestoreAoe"},
    {"AIri", "CAbilityItemSpeed"},       {"AIsr", "CAbilityItemSpeed"},
    {"Ablp", "CAbilityItemHeal"},        {"AIpv", "CAbilityItemManaRestoreAoe"},
    {"AIrd", "CAbilityItemDispelAoe"},   {"AItb", "CAbilityButton"},
    {"AIrb", "CAbilityItemHealAoe"},     {"AId0", "CAbilityItemDispelAoe"},
    {"AImz", "CAbilityItemManaRestoreAoe"},{"AImv", "CAbilityItemManaRestoreAoe"},
    {"AIdd", "CAbilityItemHeal"},        {"AId8", "CAbilityItemTownPortal"},
    {"AId7", "CAbilityItemDispelAoe"},   {"AIlz", "CAbilityRegenBase"},
    {"AIhx", "CAbilityItemHeal"},        {"AIaz", "CAbilityItemDefenseAoe"},
    {"AIsh", "CAbilityItemTownPortal"},  {"AIcb", "CAbilityButton"},
    {"AIdn", "CAbilityMaxManaBonus"},
    /* Infrastructure (internal, not player-facing) */
    {"Abur", "CAbilityBuild"},           {"Amil", "CAbilityBuild"},
    {"Acmg", "CAbilityNeutral"},         {"Sch2", "CAbilityCargoHold"},
    {"Sca1", "CAbilityBounce"},          {"Ault", "CAbilityNightVision"},
    {"SCva", "CAbilityAuraRegenLife"},   {"AIco", "CAbilityButton"},
};
#define CLASSMAP_COUNT (sizeof(classmap) / sizeof(classmap[0]))

static const char *lookup_classname(const char *rawcode) {
    for (size_t i = 0; i < CLASSMAP_COUNT; i++)
        if (!strcmp(classmap[i].rawcode, rawcode)) return classmap[i].classname;
    return NULL;
}

static bool rtti_has_class(char **rtti_names, size_t rtti_count, const char *classname) {
    for (size_t i = 0; i < rtti_count; i++)
        if (!strcmp(rtti_names[i], classname)) return true;
    return false;
}

static void validate_classmap(char **rtti_names, size_t rtti_count) {
    if (!rtti_names) return;
    for (size_t i = 0; i < CLASSMAP_COUNT; i++)
        if (!rtti_has_class(rtti_names, rtti_count, classmap[i].classname))
            fprintf(stderr, "  classmap classname absent from RTTI: %s -> %s\n",
                    classmap[i].rawcode, classmap[i].classname);
}

static bool is_implemented(DWORD key) {
    static DWORD const implemented[] = {
        RK("AHhb"), RK("AHad"), RK("AHwe"), RK("AHbz"), RK("AHtb"), RK("AHca"),
        RK("AOsf"), RK("AOmi"), RK("AEbl"), RK("AEfk"), RK("AEsh"), RK("AEim"),
        RK("Aeat"), RK("Ambt"), RK("Aroo"), RK("AUcs"), RK("ANfb"), RK("ANfs"),
        RK("ANdr"), RK("ANch"), RK("AIco"), RK("ANcl"), RK("Ahar"), RK("Amic"),
        RK("Amil"), RK("Arep"), RK("Agld"), RK("Agl2"), RK("Abgm"), RK("Abli"),
        RK("Aaha"), RK("Artn"), RK("Awha"), RK("Ahrl"), RK("Aent"), RK("Aegm"),
        RK("Acar"), RK("Abun"), RK("Aenc"), RK("Aloa"), RK("Adro"), RK("Adri"),
        RK("Astd"), RK("Avul"), RK("AInv"), RK("Aneu"), RK("Apit"), RK("Aall"),
        RK("Acoi"), RK("Apxf"), RK("Aren"), RK("Arst"), RK("AIhe"), RK("AIma"),
        RK("AIat"), RK("AIab"), RK("AIim"), RK("AIsm"), RK("AIam"), RK("AIxm"),
        RK("AIde"), RK("AIml"), RK("AImm"), RK("AIfs"), RK("AImi"), RK("AIem"),
        RK("AIlm"), RK("AIda"), RK("AIct")
    };
    for (size_t i = 0; i < sizeof(implemented) / sizeof(implemented[0]); i++)
        if (implemented[i] == key) return true;
    return false;
}

/* Convert CamelCase to snake_case in-place. Skips leading "CAbility" prefix. */
static char snake_buf[128];
static const char *to_snake(const char *classname) {
    const char *p = classname;
    int out = 0;
    /* Skip "CAbility" prefix */
    if (!strncmp(p, "CAbility", 8)) p += 8;
    bool prev_upper = false;
    for (; *p && out < 126; p++) {
        bool is_upper = (*p >= 'A' && *p <= 'Z');
        if (is_upper && out > 0 && !prev_upper) snake_buf[out++] = '_';
        snake_buf[out++] = (*p >= 'A' && *p <= 'Z') ? *p + ('a' - 'A') : *p;
        prev_upper = is_upper;
    }
    if (!out) { strcpy(snake_buf, "unknown"); out = 7; }
    snake_buf[out] = '\0';
    return snake_buf;
}

static void add_cb(const char *path, void *ud) {
    HANDLE *a = (HANDLE *)ud;
    Tool_AddArchive(a, 8, path);
}

int main(int argc, char **argv) {
    HANDLE archives[8] = {0};
    LPCSTR data_dir = NULL;
    LPCSTR dll_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-data") && i + 1 < argc) data_dir = argv[++i];
        else if (!strcmp(argv[i], "-dll") && i + 1 < argc) dll_path = argv[++i];
        else if (!strcmp(argv[i], "-mpq") && i + 1 < argc) Tool_AddArchive(archives, 8, argv[++i]);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            fprintf(stderr, "Usage: %s [-data <dir>] [-dll <Game.dll>] [-mpq <file>]\n", argv[0]);
            return 0;
        }
    }

    /* Extract RTTI class names from Game.dll */
    size_t rtti_count = 0;
    char **rtti_names = NULL;
    if (dll_path) {
        rtti_names = pe_rtti_strings(dll_path, &rtti_count);
        if (!rtti_names) {
            fprintf(stderr, "Failed to extract RTTI from %s\n", dll_path);
            return 1;
        }
        fprintf(stderr, "Extracted %zu CAbility* class names from %s\n", rtti_count, dll_path);
        validate_classmap(rtti_names, rtti_count);
    }

    /* Open MPQ archives */
    if (data_dir) Tool_ForEachArchive(data_dir, add_cb, archives);
    size_t n = 0; for (; n < 8 && archives[n]; n++);
    if (n > 0) Tool_SetSheetHost(archives, n);

    /* Load AbilityData.slk */
    AbilityData_t *rows = NULL;
    DWORD count = Stb_SlkLoad("Units\\AbilityData.slk", ability_schema, (void **)&rows, sizeof(AbilityData_t));
    if (!count || !rows) { fprintf(stderr, "Failed to load AbilityData.slk\n"); return 1; }

    /* Load ability strings for names */
    LPCSTR str_files[] = {
        "Units\\HumanAbilityStrings.txt", "Units\\OrcAbilityStrings.txt",
        "Units\\UndeadAbilityStrings.txt", "Units\\NightElfAbilityStrings.txt",
        "Units\\NeutralAbilityStrings.txt", "Units\\CommonAbilityStrings.txt",
        "Units\\ItemAbilityStrings.txt", NULL
    };
    stbIniCache_t ini = {0};
    Stb_IniCacheLoadFiles(&ini, str_files);

    /* Process unique rawcodes */
    typedef struct { DWORD key; const char *name; const char *classname; bool hero; bool item; const char *sort; const char *race; } abil_t;
    abil_t *abilities = NULL;
    DWORD abil_count = 0, abil_cap = 0;

    for (DWORD i = 0; i < count; i++) {
        AbilityData_t *row = rows + i;
        if (!row->id) continue;
        DWORD code = row->code ? row->code : row->id;
        if (row->uberAlias) code = row->uberAlias;

        bool dup = false;
        for (DWORD j = 0; j < abil_count; j++) { if (abilities[j].key == code) { dup = true; break; } }
        if (dup) continue;

        char key5[5] = {0}; memcpy(key5, &code, 4);
        LPCSTR name = Stb_IniCacheFind(&ini, key5, "Name");
        const char *cn = lookup_classname(key5);

        if (abil_count >= abil_cap) { abil_cap = abil_cap ? abil_cap * 2 : 512; abilities = realloc(abilities, abil_cap * sizeof(abil_t)); }
        abilities[abil_count++] = (abil_t){ .key = code, .name = name ? name : row->comments, .classname = cn,
                                            .hero = row->hero != 0, .item = row->item != 0, .sort = row->sort, .race = row->race };
    }

    /* Output: grouped by implemented vs missing */
    DWORD missing = 0;
    printf("/* Missing WC3 abilities with Game.dll C++ class names */\n");
    printf("/* Generated by ability_map from Game.dll RTTI + AbilityData.slk */\n\n");

    for (DWORD i = 0; i < abil_count; i++) {
        abil_t *a = &abilities[i];
        bool done = is_implemented(a->key);
        if (done) continue;

        char raw5[5] = {0}; memcpy(raw5, &a->key, 4);
        const char *cn = a->classname ? a->classname : "CAbility";
        printf("// TODO: { \"%s\", &a_%s },  // %s — %s", raw5, to_snake(cn),
               a->name ? a->name : raw5, cn);
        if (a->hero) printf(" [HERO]");
        if (a->item) printf(" [ITEM]");
        if (a->race) printf(" %s", a->race);
        printf("\n");
        missing++;
    }

    fprintf(stderr, "Total: %lu | Implemented: %lu | Missing: %lu | RTTI classes: %zu\n",
            (unsigned long)abil_count, (unsigned long)(abil_count - missing), (unsigned long)missing, rtti_count);

    /* Print unmapped RTTI classes */
    bool *used = calloc(rtti_count, sizeof(bool));
    for (DWORD i = 0; i < abil_count; i++) {
        if (!abilities[i].classname) continue;
        for (size_t j = 0; j < rtti_count; j++) {
            if (!strcmp(rtti_names[j], abilities[i].classname)) { used[j] = true; break; }
        }
    }
    DWORD unused_count = 0;
    for (size_t j = 0; j < rtti_count; j++) {
        if (!used[j]) { fprintf(stderr, "  unmapped RTTI: %s\n", rtti_names[j]); unused_count++; }
    }
    fprintf(stderr, "Unmapped RTTI classes: %lu\n", (unsigned long)unused_count);

    free(used);
    for (size_t i = 0; i < rtti_count; i++) free(rtti_names[i]);
    free(rtti_names);
    free(abilities);
    Stb_IniCacheFree(&ini);
    FS_SLKFreeRows(ability_schema, rows, count, sizeof(AbilityData_t));
    return 0;
}
