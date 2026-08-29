#include "g_local.h"
#include "g_metadata.h"
#include "common/stb_slk.h"
#include "g_unitrow.h"

typedef struct sheet_tail_cache_entry_s {
    sheetRow_t *rows;
    sheetRow_t *tail;
    struct sheet_tail_cache_entry_s *next;
} sheet_tail_cache_entry_t;

static sheet_tail_cache_entry_t *sheet_tail_cache = NULL;

sheetRow_t *G_SheetTail(sheetRow_t *rows)
{
    sheet_tail_cache_entry_t *entry;
    sheet_tail_cache_entry_t *new_entry;
    sheetRow_t *tail;

    if (!rows) {
        return NULL;
    }

    for (entry = sheet_tail_cache; entry; entry = entry->next) {
        if (entry->rows == rows) {
            return entry->tail;
        }
    }

    tail = rows;
    while (tail->next) {
        tail = tail->next;
    }

    new_entry = (sheet_tail_cache_entry_t *)malloc(sizeof(*new_entry));
    if (!new_entry) {
        return tail;
    }
    new_entry->rows = rows;
    new_entry->tail = tail;
    new_entry->next = sheet_tail_cache;
    sheet_tail_cache = new_entry;
    return tail;
}

LPCSTR config_files[] = {
    "Units\\OrcAbilityStrings.txt",
    "Units\\HumanUnitFunc.txt",
    "Units\\OrcUpgradeFunc.txt",
    "Units\\CommandFunc.txt",
    "Units\\UndeadUpgradeFunc.txt",
    "Units\\CommonAbilityStrings.txt",
    "Units\\CommandStrings.txt",
    "Units\\UndeadUnitFunc.txt",
    "Units\\OrcUpgradeStrings.txt",
    "Units\\CommonAbilityFunc.txt",
    "Units\\CampaignUnitStrings.txt",
    "Units\\HumanAbilityFunc.txt",
    "Units\\ItemFunc.txt",
    "Units\\NeutralAbilityFunc.txt",
    "Units\\Telemetry.txt",
    "Units\\ItemStrings.txt",
    "Units\\NightElfUnitStrings.txt",
    "Units\\UnitGlobalStrings.txt",
    "Units\\UndeadAbilityFunc.txt",
    "Units\\ItemAbilityFunc.txt",
    "Units\\HumanUpgradeFunc.txt",
    "Units\\CampaignUnitFunc.txt",
    "Units\\NeutralUnitStrings.txt",
    "Units\\NeutralAbilityStrings.txt",
    "Units\\UndeadAbilityStrings.txt",
    "Units\\OrcUnitStrings.txt",
    "Units\\NightElfUpgradeStrings.txt",
    "Units\\OrcUnitFunc.txt",
    "Units\\NightElfUnitFunc.txt",
    "Units\\HumanUpgradeStrings.txt",
    "Units\\ItemAbilityStrings.txt",
    "Units\\HumanUnitStrings.txt",
    "Units\\NightElfUpgradeFunc.txt",
    "Units\\NeutralUnitFunc.txt",
    "Units\\HumanAbilityStrings.txt",
    "Units\\OrcAbilityFunc.txt",
    "Units\\NightElfAbilityStrings.txt",
    "Units\\UndeadUnitStrings.txt",
    "Units\\NightElfAbilityFunc.txt",
    "Units\\MiscData.txt",
    "Units\\UndeadUpgradeStrings.txt",
    NULL
};

LPCSTR profile_files[] = {
    "Units\\CampaignUnitFunc.txt",
    "Units\\CampaignUnitStrings.txt",
    "Units\\HumanUnitFunc.txt",
    "Units\\HumanUnitStrings.txt",
    "Units\\NeutralUnitFunc.txt",
    "Units\\NeutralUnitStrings.txt",
    "Units\\NightElfUnitFunc.txt",
    "Units\\NightElfUnitStrings.txt",
    "Units\\OrcUnitFunc.txt",
    "Units\\OrcUnitStrings.txt",
    "Units\\UndeadUnitFunc.txt",
    "Units\\UndeadUnitStrings.txt",
    // optionals
    "Units\\UnitSkin.txt",
    "Units\\UnitWeaponsFunc.txt",
    "Units\\UnitWeaponsSkin.txt",
    NULL
};

static sheetRow_t *abilityConfigs = NULL;
static sheetRow_t *abilityConfigsTail = NULL;
static sheetRow_t *commandFuncConfig = NULL;
static sheetRow_t *commandStringsConfig = NULL;
static sheetRow_t *abilityConfigTables[64];
static DWORD abilityConfigTableCount = 0;

static void AppendSheetRows(sheetRow_t **head, sheetRow_t **tail, sheetRow_t *rows)
{
    sheetRow_t *rows_tail;

    if (!rows) {
        return;
    }

    rows_tail = G_SheetTail(rows);

    if (*tail) {
        (*tail)->next = rows;
    } else {
        *head = rows;
    }
    *tail = rows_tail;
}

sheetMetaData_t *G_FindMetaData(sheetMetaData_t *metadatas, LPCSTR name) {
    for (sheetMetaData_t *d = metadatas; d->id; d++) {
        if (!strcmp(d->id, name)) {
            return d;
        }
    }
    return NULL;
}

/* A field code that no metadata entry maps is a programming error, not missing
 * unit data: the accessor silently resolves to NULL/0. That is how stat bugs
 * hid for so long ("umpc" mana, "udfc" armor, "uinc"/"ustc"/"uagc" attributes).
 * Warn once per code so any such gap surfaces the first time it is read. */
static void warn_unregistered_field(LPCSTR name) {
    static char seen[64][8];
    static DWORD count;

    for (DWORD i = 0; i < count; i++) {
        if (!strcmp(seen[i], name)) {
            return;
        }
    }
    if (count < 64) {
        strncpy(seen[count], name, sizeof(seen[0]) - 1);
        count++;
    }
    fprintf(stderr,
            "WARNING: unit-data field code '%s' is not registered in the metadata "
            "table; it silently reads as 0. Add it to UnitsMetaData[] (g_metadata.h).\n",
            name);
}

LPCSTR UnitStringField(sheetMetaData_t *metadatas, DWORD unit_id, LPCSTR name) {
    FOR_LOOP(n, level.mapinfo->num_userCreatedUnits) {
        if (level.mapinfo->userCreatedUnits[n].newUnitID == unit_id) {
            unit_id = level.mapinfo->userCreatedUnits[n].originalUnitID;
        }
    }
    sheetMetaData_t *metadata = G_FindMetaData(metadatas, name);
    if (!metadata) {
        warn_unregistered_field(name);
        return NULL;
    }
    if (metadata->table) {
        return FS_FindSheetCell(metadata->table, GetClassName(unit_id), metadata->field);
    }
    return NULL;
}

LONG UnitIntegerField(sheetMetaData_t *metadatas, DWORD unit_id, LPCSTR name) {
    LPCSTR str = UnitStringField(metadatas, unit_id, name);
    return str ? atoi(str) : 0;
}

BOOL UnitBooleanField(sheetMetaData_t *metadatas, DWORD unit_id, LPCSTR name) {
    LPCSTR str = UnitStringField(metadatas, unit_id, name);
    return str && (atoi(str) != 0 || !strcmp(str, "TRUE"));
}

FLOAT UnitRealField(sheetMetaData_t *metadatas, DWORD unit_id, LPCSTR name) {
    LPCSTR str = UnitStringField(metadatas, unit_id, name);
    return str ? atof(str) : 0;
}

/* =========================================================================
 * DDX schemas — one entry per SLK column consumed at runtime.
 * =========================================================================*/
static slkField_t const balance_schema[] = {
    { "sortBalance",      offsetof(UnitBalance_t, sortBalance),     STB_SLK_STR   },
    { "sort2",            offsetof(UnitBalance_t, sort2),           STB_SLK_STR   },
    { "comment(s)",       offsetof(UnitBalance_t, comments),        STB_SLK_STR   },
    { "abilTest",         offsetof(UnitBalance_t, abilTest),        STB_SLK_BOOL  },
    { "InBeta",           offsetof(UnitBalance_t, InBeta),          STB_SLK_BOOL  },
    { "level",            offsetof(UnitBalance_t, level),           STB_SLK_INT   },
    { "goldcost",         offsetof(UnitBalance_t, goldCost),        STB_SLK_INT   },
    { "lumbercost",       offsetof(UnitBalance_t, lumberCost),      STB_SLK_INT   },
    { "goldRep",          offsetof(UnitBalance_t, goldRep),         STB_SLK_INT   },
    { "lumberRep",        offsetof(UnitBalance_t, lumberRep),       STB_SLK_INT   },
    { "fmade",            offsetof(UnitBalance_t, foodMade),        STB_SLK_INT   },
    { "fused",            offsetof(UnitBalance_t, foodUsed),        STB_SLK_INT   },
    { "bountydice",       offsetof(UnitBalance_t, goldBountyDice),  STB_SLK_INT   },
    { "bountysides",      offsetof(UnitBalance_t, goldBountySides), STB_SLK_INT   },
    { "bountyplus",       offsetof(UnitBalance_t, goldBountyBase),  STB_SLK_INT   },
    { "lumberbountydice", offsetof(UnitBalance_t, lumberBountyDice),STB_SLK_INT   }, /* TFT */
    { "lumberbountysides",offsetof(UnitBalance_t, lumberBountySides),STB_SLK_INT  }, /* TFT */
    { "lumberbountyplus", offsetof(UnitBalance_t, lumberBountyBase),STB_SLK_INT   }, /* TFT */
    { "stockMax",         offsetof(UnitBalance_t, stockMax),        STB_SLK_INT   },
    { "stockRegen",       offsetof(UnitBalance_t, stockRegen),      STB_SLK_INT   },
    { "stockStart",       offsetof(UnitBalance_t, stockStart),      STB_SLK_INT   },
    { "HP",               offsetof(UnitBalance_t, baseHealth),      STB_SLK_INT   },
    { "realHP",           offsetof(UnitBalance_t, maxHealth),       STB_SLK_FLOAT },
    { "regenHP",          offsetof(UnitBalance_t, healthRegen),     STB_SLK_FLOAT },
    { "regenType",        offsetof(UnitBalance_t, healthRegenType), STB_SLK_STR   },
    { "manaN",            offsetof(UnitBalance_t, baseMana),        STB_SLK_INT   },
    { "realM",            offsetof(UnitBalance_t, maxMana),         STB_SLK_FLOAT },
    { "mana0",            offsetof(UnitBalance_t, initialMana),     STB_SLK_FLOAT },
    { "regenMana",        offsetof(UnitBalance_t, manaRegen),       STB_SLK_FLOAT },
    { "def",              offsetof(UnitBalance_t, baseArmor),       STB_SLK_INT   },
    { "defUp",            offsetof(UnitBalance_t, armorPerUpgrade), STB_SLK_INT   },
    { "realdef",          offsetof(UnitBalance_t, armor),           STB_SLK_FLOAT },
    { "defType",          offsetof(UnitBalance_t, defenseType),     STB_SLK_STR   },
    { "spd",              offsetof(UnitBalance_t, speed),           STB_SLK_FLOAT },
    { "maxSpd",           offsetof(UnitBalance_t, maxSpeed),        STB_SLK_FLOAT }, /* TFT */
    { "minSpd",           offsetof(UnitBalance_t, minSpeed),        STB_SLK_FLOAT }, /* TFT */
    { "bldtm",            offsetof(UnitBalance_t, buildTime),       STB_SLK_INT   },
    { "sight",            offsetof(UnitBalance_t, sightRadius),     STB_SLK_FLOAT },
    { "nsight",           offsetof(UnitBalance_t, nightSightRadius),STB_SLK_FLOAT },
    { "STR",              offsetof(UnitBalance_t, strength),        STB_SLK_INT   },
    { "INT",              offsetof(UnitBalance_t, intelligence),    STB_SLK_INT   },
    { "AGI",              offsetof(UnitBalance_t, agility),         STB_SLK_INT   },
    { "STRplus",          offsetof(UnitBalance_t, strengthPerLevel),STB_SLK_FLOAT },
    { "INTplus",          offsetof(UnitBalance_t, intelligencePerLevel), STB_SLK_FLOAT },
    { "AGIplus",          offsetof(UnitBalance_t, agilityPerLevel), STB_SLK_FLOAT },
    { "Primary",          offsetof(UnitBalance_t, primaryAttribute),STB_SLK_STR   },
    { "upgrades",         offsetof(UnitBalance_t, upgrades),        STB_SLK_STR   },
    { "collision",        offsetof(UnitBalance_t, collision),       STB_SLK_FLOAT }, /* TFT */
    { "isbldg",           offsetof(UnitBalance_t, isBuilding),      STB_SLK_BOOL  }, /* TFT */
    { "nbrandom",         offsetof(UnitBalance_t, nbrandom),        STB_SLK_INT   }, /* TFT */
    { "preventPlace",     offsetof(UnitBalance_t, preventPlace),    STB_SLK_STR   }, /* TFT */
    { "reptm",            offsetof(UnitBalance_t, reptm),           STB_SLK_INT   }, /* TFT */
    { "repulse",          offsetof(UnitBalance_t, repulse),         STB_SLK_INT   }, /* TFT */
    { "repulseGroup",     offsetof(UnitBalance_t, repulseGroup),    STB_SLK_INT   }, /* TFT */
    { "repulseParam",     offsetof(UnitBalance_t, repulseParam),    STB_SLK_FLOAT }, /* TFT */
    { "repulsePrio",      offsetof(UnitBalance_t, repulsePrio),     STB_SLK_INT   }, /* TFT */
    { "requirePlace",     offsetof(UnitBalance_t, requirePlace),    STB_SLK_STR   }, /* TFT */
    { "tilesets",         offsetof(UnitBalance_t, tilesets),        STB_SLK_STR   }, /* TFT */
    { "type",             offsetof(UnitBalance_t, type),            STB_SLK_STR   }, /* TFT */
    { NULL, 0, 0 }
};

static slkField_t const data_schema[] = {
    { "sort",               offsetof(UnitData_t, sort),             STB_SLK_STR   },
    { "comment(s)",         offsetof(UnitData_t, comments),         STB_SLK_STR   },
    { "version",            offsetof(UnitData_t, version),          STB_SLK_INT   },
    { "valid",              offsetof(UnitData_t, valid),            STB_SLK_BOOL  },
    { "InBeta",             offsetof(UnitData_t, InBeta),           STB_SLK_BOOL  },
    { "buffRadius",         offsetof(UnitData_t, buffRadius),       STB_SLK_FLOAT },
    { "buffType",           offsetof(UnitData_t, buffType),         STB_SLK_STR   },
    { "targType",           offsetof(UnitData_t, targetType),       STB_SLK_STR   },
    { "canSleep",           offsetof(UnitData_t, canSleep),         STB_SLK_BOOL  },
    { "canFlee",            offsetof(UnitData_t, canFlee),          STB_SLK_BOOL  }, /* TFT */
    { "canBuildOn",         offsetof(UnitData_t, canBuildOn),       STB_SLK_BOOL  }, /* TFT */
    { "isBuildOn",          offsetof(UnitData_t, isBuildOn),        STB_SLK_BOOL  }, /* TFT */
    { "cargoSize",          offsetof(UnitData_t, cargoSize),        STB_SLK_INT   },
    { "death",              offsetof(UnitData_t, death),            STB_SLK_FLOAT },
    { "deathType",          offsetof(UnitData_t, deathType),        STB_SLK_INT   },
    { "fatLOS",             offsetof(UnitData_t, useExtendedLineOfSight), STB_SLK_BOOL  },
    { "formation",          offsetof(UnitData_t, formationRank),    STB_SLK_INT   },
    { "moveFloor",          offsetof(UnitData_t, moveFloor),        STB_SLK_FLOAT },
    { "moveHeight",         offsetof(UnitData_t, moveHeight),       STB_SLK_FLOAT },
    { "movetp",             offsetof(UnitData_t, moveTypeName),     STB_SLK_STR   },
    { "turnRate",           offsetof(UnitData_t, turnRate),         STB_SLK_FLOAT },
    { "nameCount",          offsetof(UnitData_t, nameCount),        STB_SLK_INT   },
    { "orientInterp",       offsetof(UnitData_t, orientationInterpolation), STB_SLK_INT   },
    { "pathTex",            offsetof(UnitData_t, pathingTexture),   STB_SLK_STR   },
    { "points",             offsetof(UnitData_t, points),           STB_SLK_INT   },
    { "prio",               offsetof(UnitData_t, priority),         STB_SLK_INT   },
    { "propWin",            offsetof(UnitData_t, propWin),          STB_SLK_FLOAT },
    { "race",               offsetof(UnitData_t, race),             STB_SLK_STR   },
    { "requireWaterRadius", offsetof(UnitData_t, requireWaterRadius),STB_SLK_FLOAT}, /* TFT */
    { "threat",             offsetof(UnitData_t, threat),           STB_SLK_INT   },
    { "castbsw",            offsetof(UnitData_t, castBackSwing),    STB_SLK_FLOAT }, /* ROC */
    { "castpt",             offsetof(UnitData_t, castPoint),        STB_SLK_FLOAT }, /* ROC */
    { "collision",          offsetof(UnitData_t, collision),        STB_SLK_FLOAT }, /* ROC */
    { "impactZ",            offsetof(UnitData_t, impactHeight),     STB_SLK_FLOAT }, /* ROC */
    { "launchX",            offsetof(UnitData_t, launchOffsetX),    STB_SLK_FLOAT }, /* ROC */
    { "launchY",            offsetof(UnitData_t, launchOffsetY),    STB_SLK_FLOAT }, /* ROC */
    { "launchZ",            offsetof(UnitData_t, launchOffsetZ),    STB_SLK_FLOAT }, /* ROC */
    { "type",               offsetof(UnitData_t, unitClassification), STB_SLK_STR  }, /* ROC */
    { NULL, 0, 0 }
};

static slkField_t const ui_schema[] = {
    { "sortUI",           offsetof(UnitUI_t, sortUI),           STB_SLK_STR   },
    { "InBeta",           offsetof(UnitUI_t, InBeta),           STB_SLK_BOOL  },
    { "file",             offsetof(UnitUI_t, modelFile),        STB_SLK_STR   },
    { "modelScale",       offsetof(UnitUI_t, modelScale),       STB_SLK_FLOAT },
    { "blend",            offsetof(UnitUI_t, blend),            STB_SLK_FLOAT },
    { "red",              offsetof(UnitUI_t, tintRed),          STB_SLK_INT   },
    { "green",            offsetof(UnitUI_t, tintGreen),        STB_SLK_INT   },
    { "blue",             offsetof(UnitUI_t, tintBlue),         STB_SLK_INT   },
    { "teamColor",        offsetof(UnitUI_t, teamColor),        STB_SLK_INT   },
    { "customTeamColor",  offsetof(UnitUI_t, customTeamColor),  STB_SLK_BOOL  },
    { "hostilePal",       offsetof(UnitUI_t, hostilePal),       STB_SLK_BOOL  },
    { "scale",            offsetof(UnitUI_t, selectionScale),   STB_SLK_FLOAT },
    { "selZ",             offsetof(UnitUI_t, selectionCircleHeight), STB_SLK_FLOAT },
    { "shadowH",          offsetof(UnitUI_t, shadowHeight),     STB_SLK_FLOAT },
    { "shadowW",          offsetof(UnitUI_t, shadowWidth),      STB_SLK_FLOAT },
    { "shadowX",          offsetof(UnitUI_t, shadowCenterX),    STB_SLK_FLOAT },
    { "shadowY",          offsetof(UnitUI_t, shadowCenterY),    STB_SLK_FLOAT },
    { "unitShadow",       offsetof(UnitUI_t, unitShadowTexture), STB_SLK_STR  },
    { "selCircOnWater",   offsetof(UnitUI_t, selectionCircleOnWater), STB_SLK_BOOL  }, /* TFT */
    { "shadowOnWater",    offsetof(UnitUI_t, waterShadow),      STB_SLK_BOOL  }, /* TFT */
    { "scaleBull",        offsetof(UnitUI_t, scaleProjectiles), STB_SLK_BOOL  },
    { "elevPts",          offsetof(UnitUI_t, elevationSamplePoints), STB_SLK_INT   },
    { "elevRad",          offsetof(UnitUI_t, elevationSampleRadius), STB_SLK_FLOAT },
    { "fogRad",           offsetof(UnitUI_t, fogOfWarSampleRadius), STB_SLK_FLOAT },
    { "occH",             offsetof(UnitUI_t, occluderHeight),   STB_SLK_FLOAT },
    { "run",              offsetof(UnitUI_t, animationRunSpeed), STB_SLK_FLOAT },
    { "walk",             offsetof(UnitUI_t, animationWalkSpeed), STB_SLK_FLOAT },
    { "maxPitch",         offsetof(UnitUI_t, maxPitchDegrees),   STB_SLK_FLOAT },
    { "maxRoll",          offsetof(UnitUI_t, maxRollDegrees),    STB_SLK_FLOAT },
    { "unitSound",        offsetof(UnitUI_t, soundLabel),       STB_SLK_STR   },
    { "name",             offsetof(UnitUI_t, name),             STB_SLK_STR   },
    { "uberSplat",        offsetof(UnitUI_t, groundTexture),    STB_SLK_STR   },
    { "buildingShadow",   offsetof(UnitUI_t, buildingShadowTexture), STB_SLK_STR   },
    { "special",          offsetof(UnitUI_t, special),          STB_SLK_STR   },
    { "armor",            offsetof(UnitUI_t, armorType),        STB_SLK_INT   },
    { "unitClass",        offsetof(UnitUI_t, unitClass),        STB_SLK_INT   },
    { "nbmmIcon",         offsetof(UnitUI_t, neutralBuildingMinimapIcon), STB_SLK_BOOL  },
    { "inEditor",         offsetof(UnitUI_t, inEditor),         STB_SLK_BOOL  },
    { "hiddenInEditor",   offsetof(UnitUI_t, hiddenInEditor),   STB_SLK_BOOL  },
    { "dropItems",        offsetof(UnitUI_t, dropItems),        STB_SLK_BOOL  },
    { "useClickHelper",   offsetof(UnitUI_t, useClickHelper),   STB_SLK_BOOL  },
    { "campaign",         offsetof(UnitUI_t, campaign),         STB_SLK_BOOL  }, /* TFT */
    { "fileVerFlags",     offsetof(UnitUI_t, fileVerFlags),     STB_SLK_INT   }, /* TFT */
    { "hideHeroBar",      offsetof(UnitUI_t, hideHeroBar),      STB_SLK_BOOL  }, /* TFT */
    { "hideHeroDeathMsg", offsetof(UnitUI_t, hideHeroDeathMsg), STB_SLK_BOOL  }, /* TFT */
    { "hideHeroMinimap",  offsetof(UnitUI_t, hideHeroMinimap),  STB_SLK_BOOL  }, /* TFT */
    { "hideOnMinimap",    offsetof(UnitUI_t, hideOnMinimap),    STB_SLK_BOOL  }, /* TFT */
    { "tilesetSpecific",  offsetof(UnitUI_t, tilesetSpecific),  STB_SLK_BOOL  },
    { "requirePlace",     offsetof(UnitUI_t, requirePlace),     STB_SLK_STR   }, /* ROC */
    { "preventPlace",     offsetof(UnitUI_t, preventPlace),     STB_SLK_STR   }, /* ROC */
    { "tilesets",         offsetof(UnitUI_t, tilesets),         STB_SLK_STR   }, /* ROC */
    { "nbrandom",         offsetof(UnitUI_t, nbrandom),         STB_SLK_INT   }, /* ROC */
    { "weap1",            offsetof(UnitUI_t, weaponSlot1),      STB_SLK_INT   },
    { "weap2",            offsetof(UnitUI_t, weaponSlot2),      STB_SLK_INT   },
    { "isbldg",           offsetof(UnitUI_t, isBuilding),       STB_SLK_BOOL  }, /* ROC */
    { NULL, 0, 0 }
};

static slkField_t const weapons_schema[] = {
    { "sortWeap",       offsetof(UnitWeapons_t, sortWeap),      STB_SLK_STR   },
    { "sort2",          offsetof(UnitWeapons_t, sort2),         STB_SLK_STR   },
    { "comment(s)",     offsetof(UnitWeapons_t, comments),      STB_SLK_STR   },
    { "InBeta",         offsetof(UnitWeapons_t, InBeta),        STB_SLK_BOOL  },
    { "acquire",       offsetof(UnitWeapons_t, acquisitionRange), STB_SLK_FLOAT },
    { "atkType1",      offsetof(UnitWeapons_t, attack1.attackType), STB_SLK_STR   },
    { "dice1",         offsetof(UnitWeapons_t, attack1.damageDice), STB_SLK_INT   },
    { "sides1",        offsetof(UnitWeapons_t, attack1.damageSides), STB_SLK_INT   },
    { "dmgplus1",      offsetof(UnitWeapons_t, attack1.damageBase), STB_SLK_INT   },
    { "dmgpt1",        offsetof(UnitWeapons_t, attack1.damagePoint), STB_SLK_FLOAT },
    { "backSw1",       offsetof(UnitWeapons_t, attack1.backswingPoint), STB_SLK_FLOAT },
    { "cool1",         offsetof(UnitWeapons_t, attack1.cooldown), STB_SLK_FLOAT },
    { "mincool1",      offsetof(UnitWeapons_t, attack1.minCooldown), STB_SLK_FLOAT },
    { "mindmg1",       offsetof(UnitWeapons_t, attack1.minDamage), STB_SLK_FLOAT },
    { "avgdmg1",       offsetof(UnitWeapons_t, attack1.averageDamage), STB_SLK_FLOAT },
    { "maxdmg1",       offsetof(UnitWeapons_t, attack1.maxDamage), STB_SLK_FLOAT },
    { "RngTst",        offsetof(UnitWeapons_t, attack1.rangeTest), STB_SLK_STR   },
    { "DmgUpg",        offsetof(UnitWeapons_t, attack1.damageUpgrade), STB_SLK_STR   },
    { "dmod1",         offsetof(UnitWeapons_t, attack1.damageModifier), STB_SLK_STR   },
    { "DPS",           offsetof(UnitWeapons_t, attack1.damagePerSecond), STB_SLK_FLOAT },
    { "dmgUp1",        offsetof(UnitWeapons_t, attack1.damageUpgradeAmount), STB_SLK_INT   },
    { "damageLoss1",   offsetof(UnitWeapons_t, attack1.damageLossFactor), STB_SLK_FLOAT },
    { "rangeN1",       offsetof(UnitWeapons_t, attack1.range), STB_SLK_FLOAT },
    { "RngBuff1",      offsetof(UnitWeapons_t, attack1.rangeBuffer), STB_SLK_FLOAT },
    { "Farea1",        offsetof(UnitWeapons_t, attack1.areaFull), STB_SLK_FLOAT },
    { "Harea1",        offsetof(UnitWeapons_t, attack1.areaMedium), STB_SLK_FLOAT },
    { "Qarea1",        offsetof(UnitWeapons_t, attack1.areaSmall), STB_SLK_FLOAT },
    { "Hfact1",        offsetof(UnitWeapons_t, attack1.factorMedium), STB_SLK_FLOAT },
    { "Qfact1",        offsetof(UnitWeapons_t, attack1.factorSmall), STB_SLK_FLOAT },
    { "spillDist1",    offsetof(UnitWeapons_t, attack1.spillDistance), STB_SLK_FLOAT },
    { "spillRadius1",  offsetof(UnitWeapons_t, attack1.spillRadius), STB_SLK_FLOAT },
    { "splashTargs1",  offsetof(UnitWeapons_t, attack1.areaTargets), STB_SLK_INT   },
    { "targCount1",    offsetof(UnitWeapons_t, attack1.maxTargets), STB_SLK_INT   },
    { "targs1",        offsetof(UnitWeapons_t, attack1.targetsAllowed), STB_SLK_INT   },
    { "weapTp1",       offsetof(UnitWeapons_t, attack1.weaponType), STB_SLK_STR   },
    { "weapType1",     offsetof(UnitWeapons_t, attack1.weaponSound), STB_SLK_INT   },
    { "showUI1",       offsetof(UnitWeapons_t, attack1.showUI), STB_SLK_BOOL  }, /* TFT */
    { "atkType2",      offsetof(UnitWeapons_t, attack2.attackType), STB_SLK_STR   },
    { "dice2",         offsetof(UnitWeapons_t, attack2.damageDice), STB_SLK_INT   },
    { "sides2",        offsetof(UnitWeapons_t, attack2.damageSides), STB_SLK_INT   },
    { "dmgplus2",      offsetof(UnitWeapons_t, attack2.damageBase), STB_SLK_INT   },
    { "dmgpt2",        offsetof(UnitWeapons_t, attack2.damagePoint), STB_SLK_FLOAT },
    { "backSw2",       offsetof(UnitWeapons_t, attack2.backswingPoint), STB_SLK_FLOAT },
    { "cool2",         offsetof(UnitWeapons_t, attack2.cooldown), STB_SLK_FLOAT },
    { "mincool2",      offsetof(UnitWeapons_t, attack2.minCooldown), STB_SLK_FLOAT },
    { "mindmg2",       offsetof(UnitWeapons_t, attack2.minDamage), STB_SLK_FLOAT },
    { "avgdmg2",       offsetof(UnitWeapons_t, attack2.averageDamage), STB_SLK_FLOAT },
    { "maxdmg2",       offsetof(UnitWeapons_t, attack2.maxDamage), STB_SLK_FLOAT },
    { "RngTst2",       offsetof(UnitWeapons_t, attack2.rangeTest), STB_SLK_STR   },
    { "dmgUp2",        offsetof(UnitWeapons_t, attack2.damageUpgradeAmount), STB_SLK_INT   },
    { "damageLoss2",   offsetof(UnitWeapons_t, attack2.damageLossFactor), STB_SLK_FLOAT },
    { "rangeN2",       offsetof(UnitWeapons_t, attack2.range), STB_SLK_FLOAT },
    { "RngBuff2",      offsetof(UnitWeapons_t, attack2.rangeBuffer), STB_SLK_FLOAT },
    { "Farea2",        offsetof(UnitWeapons_t, attack2.areaFull), STB_SLK_FLOAT },
    { "Harea2",        offsetof(UnitWeapons_t, attack2.areaMedium), STB_SLK_FLOAT },
    { "Qarea2",        offsetof(UnitWeapons_t, attack2.areaSmall), STB_SLK_FLOAT },
    { "Hfact2",        offsetof(UnitWeapons_t, attack2.factorMedium), STB_SLK_FLOAT },
    { "Qfact2",        offsetof(UnitWeapons_t, attack2.factorSmall), STB_SLK_FLOAT },
    { "spillDist2",    offsetof(UnitWeapons_t, attack2.spillDistance), STB_SLK_FLOAT },
    { "spillRadius2",  offsetof(UnitWeapons_t, attack2.spillRadius), STB_SLK_FLOAT },
    { "splashTargs2",  offsetof(UnitWeapons_t, attack2.areaTargets), STB_SLK_INT   },
    { "targCount2",    offsetof(UnitWeapons_t, attack2.maxTargets), STB_SLK_INT   },
    { "targs2",        offsetof(UnitWeapons_t, attack2.targetsAllowed), STB_SLK_INT   },
    { "weapTp2",       offsetof(UnitWeapons_t, attack2.weaponType), STB_SLK_STR   },
    { "weapType2",     offsetof(UnitWeapons_t, attack2.weaponSound), STB_SLK_INT   },
    { "showUI2",       offsetof(UnitWeapons_t, attack2.showUI), STB_SLK_BOOL  }, /* TFT */
    { "weapsOn",       offsetof(UnitWeapons_t, attacksEnabled), STB_SLK_INT   },
    { "minRange",      offsetof(UnitWeapons_t, minimumAttackRange), STB_SLK_FLOAT },
    { "castbsw",       offsetof(UnitWeapons_t, castBackSwing), STB_SLK_FLOAT }, /* TFT */
    { "castpt",        offsetof(UnitWeapons_t, castPoint), STB_SLK_FLOAT }, /* TFT */
    { "impactSwimZ",   offsetof(UnitWeapons_t, impactSwimZ),  STB_SLK_FLOAT }, /* TFT */
    { "impactZ",       offsetof(UnitWeapons_t, impactHeight),  STB_SLK_FLOAT }, /* TFT */
    { "launchSwimZ",   offsetof(UnitWeapons_t, launchSwimZ),  STB_SLK_FLOAT }, /* TFT */
    { "launchX",       offsetof(UnitWeapons_t, attackLaunchX), STB_SLK_FLOAT }, /* TFT */
    { "launchY",       offsetof(UnitWeapons_t, attackLaunchY), STB_SLK_FLOAT }, /* TFT */
    { "launchZ",       offsetof(UnitWeapons_t, attackLaunchZ), STB_SLK_FLOAT }, /* TFT */
    { NULL, 0, 0 }
};

static slkField_t const abil_schema[] = {
    { "sortAbil",     offsetof(UnitAbilities_t, sortAbil),     STB_SLK_STR  },
    { "comment(s)",   offsetof(UnitAbilities_t, comments),     STB_SLK_STR  },
    { "abilList",     offsetof(UnitAbilities_t, abilList),     STB_SLK_STR  },
    { "heroAbilList", offsetof(UnitAbilities_t, heroAbilList), STB_SLK_STR  },
    { "auto",         offsetof(UnitAbilities_t, auto_),        STB_SLK_BOOL },
    { "InBeta",       offsetof(UnitAbilities_t, InBeta),       STB_SLK_BOOL },
    { NULL, 0, 0 }
};

#define AB_F(NAME, FIELD, LEVEL, TYPE) { NAME, offsetof(AbilityData_t, FIELD[LEVEL]), TYPE }
#define AB_D(NAME, LEVEL, SLOT) { NAME, offsetof(AbilityData_t, data[LEVEL][SLOT]), STB_SLK_FLOAT }
static slkField_t const ability_schema[] = {
    { "code",        offsetof(AbilityData_t, code),        STB_SLK_FOURCC },
    { "uberAlias",   offsetof(AbilityData_t, uberAlias),   STB_SLK_FOURCC }, /* ROC */
    { "comments",    offsetof(AbilityData_t, comments),    STB_SLK_STR    },
    { "version",     offsetof(AbilityData_t, version),     STB_SLK_INT    }, /* TFT */
    { "useInEditor", offsetof(AbilityData_t, useInEditor), STB_SLK_BOOL   },
    { "hero",        offsetof(AbilityData_t, hero),        STB_SLK_BOOL   },
    { "item",        offsetof(AbilityData_t, item),        STB_SLK_BOOL   },
    { "sort",        offsetof(AbilityData_t, sort),        STB_SLK_STR    },
    { "race",        offsetof(AbilityData_t, race),        STB_SLK_STR    }, /* TFT */
    { "checkDep",    offsetof(AbilityData_t, checkDep),    STB_SLK_BOOL   },
    { "levels",      offsetof(AbilityData_t, levels),      STB_SLK_INT    },
    { "reqLevel",    offsetof(AbilityData_t, reqLevel),    STB_SLK_INT    },
    { "levelSkip",   offsetof(AbilityData_t, levelSkip),   STB_SLK_INT    }, /* TFT */
    { "priority",    offsetof(AbilityData_t, priority),    STB_SLK_INT    }, /* TFT */
    { "targs",       offsetof(AbilityData_t, targs[0]),    STB_SLK_STR    }, /* ROC */
    AB_F("targs1", targs, 0, STB_SLK_STR), AB_F("targs2", targs, 1, STB_SLK_STR),
    AB_F("targs3", targs, 2, STB_SLK_STR), AB_F("targs4", targs, 3, STB_SLK_STR),
    AB_F("Cast1", cast, 0, STB_SLK_FLOAT), AB_F("Cast2", cast, 1, STB_SLK_FLOAT),
    AB_F("Cast3", cast, 2, STB_SLK_FLOAT), AB_F("Cast4", cast, 3, STB_SLK_FLOAT),
    AB_F("Dur1", dur, 0, STB_SLK_FLOAT), AB_F("Dur2", dur, 1, STB_SLK_FLOAT),
    AB_F("Dur3", dur, 2, STB_SLK_FLOAT), AB_F("Dur4", dur, 3, STB_SLK_FLOAT),
    AB_F("HeroDur1", heroDur, 0, STB_SLK_FLOAT), AB_F("HeroDur2", heroDur, 1, STB_SLK_FLOAT),
    AB_F("HeroDur3", heroDur, 2, STB_SLK_FLOAT), AB_F("HeroDur4", heroDur, 3, STB_SLK_FLOAT),
    AB_F("Cool1", cool, 0, STB_SLK_FLOAT), AB_F("Cool2", cool, 1, STB_SLK_FLOAT),
    AB_F("Cool3", cool, 2, STB_SLK_FLOAT), AB_F("Cool4", cool, 3, STB_SLK_FLOAT),
    AB_F("Cost1", cost, 0, STB_SLK_FLOAT), AB_F("Cost2", cost, 1, STB_SLK_FLOAT),
    AB_F("Cost3", cost, 2, STB_SLK_FLOAT), AB_F("Cost4", cost, 3, STB_SLK_FLOAT),
    AB_F("Area1", area, 0, STB_SLK_FLOAT), AB_F("Area2", area, 1, STB_SLK_FLOAT),
    AB_F("Area3", area, 2, STB_SLK_FLOAT), AB_F("Area4", area, 3, STB_SLK_FLOAT),
    AB_F("Rng1", range, 0, STB_SLK_FLOAT), AB_F("Rng2", range, 1, STB_SLK_FLOAT),
    AB_F("Rng3", range, 2, STB_SLK_FLOAT), AB_F("Rng4", range, 3, STB_SLK_FLOAT),
    AB_D("Data11", 0, 0), AB_D("Data12", 0, 1), AB_D("Data13", 0, 2), AB_D("Data14", 0, 3),
    AB_D("Data21", 1, 0), AB_D("Data22", 1, 1), AB_D("Data23", 1, 2), AB_D("Data24", 1, 3),
    AB_D("Data31", 2, 0), AB_D("Data32", 2, 1), AB_D("Data33", 2, 2), AB_D("Data34", 2, 3),
    AB_D("DataA1", 0, 0), AB_D("DataB1", 0, 1), AB_D("DataC1", 0, 2), AB_D("DataD1", 0, 3),
    AB_D("DataE1", 0, 4), AB_D("DataF1", 0, 5), AB_D("DataG1", 0, 6), AB_D("DataH1", 0, 7), AB_D("DataI1", 0, 8),
    AB_D("DataA2", 1, 0), AB_D("DataB2", 1, 1), AB_D("DataC2", 1, 2), AB_D("DataD2", 1, 3),
    AB_D("DataE2", 1, 4), AB_D("DataF2", 1, 5), AB_D("DataG2", 1, 6), AB_D("DataH2", 1, 7), AB_D("DataI2", 1, 8),
    AB_D("DataA3", 2, 0), AB_D("DataB3", 2, 1), AB_D("DataC3", 2, 2), AB_D("DataD3", 2, 3),
    AB_D("DataE3", 2, 4), AB_D("DataF3", 2, 5), AB_D("DataG3", 2, 6), AB_D("DataH3", 2, 7), AB_D("DataI3", 2, 8),
    AB_D("DataA4", 3, 0), AB_D("DataB4", 3, 1), AB_D("DataC4", 3, 2), AB_D("DataD4", 3, 3),
    AB_D("DataE4", 3, 4), AB_D("DataF4", 3, 5), AB_D("DataG4", 3, 6), AB_D("DataH4", 3, 7), AB_D("DataI4", 3, 8),
    AB_F("UnitID1", unitID, 0, STB_SLK_FOURCC), AB_F("UnitID2", unitID, 1, STB_SLK_FOURCC),
    AB_F("UnitID3", unitID, 2, STB_SLK_FOURCC), AB_F("UnitID4", unitID, 3, STB_SLK_FOURCC),
    AB_F("BuffID1", buffID, 0, STB_SLK_STR), AB_F("BuffID2", buffID, 1, STB_SLK_STR),
    AB_F("BuffID3", buffID, 2, STB_SLK_STR), AB_F("BuffID4", buffID, 3, STB_SLK_STR),
    AB_F("EfctID1", efctID, 0, STB_SLK_STR), AB_F("EfctID2", efctID, 1, STB_SLK_STR),
    AB_F("EfctID3", efctID, 2, STB_SLK_STR), AB_F("EfctID4", efctID, 3, STB_SLK_STR),
    { "CastCheck",    offsetof(AbilityData_t, castCheck),    STB_SLK_STR }, /* ROC */
    { "DurCheck",     offsetof(AbilityData_t, durCheck),     STB_SLK_STR }, /* ROC */
    { "HeroDurCheck", offsetof(AbilityData_t, heroDurCheck), STB_SLK_STR }, /* ROC */
    { "CoolCheck",    offsetof(AbilityData_t, coolCheck),    STB_SLK_STR }, /* ROC */
    { "CostCheck",    offsetof(AbilityData_t, costCheck),    STB_SLK_STR }, /* ROC */
    { "AreaCheck",    offsetof(AbilityData_t, areaCheck),    STB_SLK_STR }, /* ROC */
    { "RngCheck",     offsetof(AbilityData_t, rangeCheck),   STB_SLK_STR }, /* ROC */
    { "InBeta",       offsetof(AbilityData_t, InBeta),       STB_SLK_BOOL }, /* TFT */
    { NULL, 0, 0 }
};
#undef AB_D
#undef AB_F

#define DOOD_VERT(NAME, VERTEX, CHANNEL) { NAME, offsetof(Doodads_t, vert[VERTEX][CHANNEL]), STB_SLK_INT }
static slkField_t const doodad_schema[] = {
    { "category",          offsetof(Doodads_t, category),          STB_SLK_STR   },
    { "tilesets",          offsetof(Doodads_t, tilesets),          STB_SLK_STR   },
    { "tilesetSpecific",   offsetof(Doodads_t, tilesetSpecific),   STB_SLK_BOOL  },
    { "dir",               offsetof(Doodads_t, dir),               STB_SLK_STR   }, /* ROC */
    { "file",              offsetof(Doodads_t, file),              STB_SLK_STR   },
    { "comment",           offsetof(Doodads_t, comment),           STB_SLK_STR   },
    { "name",              offsetof(Doodads_t, Name),              STB_SLK_STR   }, /* ROC */
    { "Name",              offsetof(Doodads_t, Name),              STB_SLK_STR   }, /* TFT */
    { "doodClass",         offsetof(Doodads_t, doodClass),         STB_SLK_STR   },
    { "soundLoop",         offsetof(Doodads_t, soundLoop),         STB_SLK_STR   },
    { "selSize",           offsetof(Doodads_t, selSize),           STB_SLK_FLOAT },
    { "defScale",          offsetof(Doodads_t, defScale),          STB_SLK_FLOAT },
    { "minScale",          offsetof(Doodads_t, minScale),          STB_SLK_FLOAT },
    { "maxScale",          offsetof(Doodads_t, maxScale),          STB_SLK_FLOAT },
    { "canPlaceRandScale", offsetof(Doodads_t, canPlaceRandScale), STB_SLK_BOOL  },
    { "useClickHelper",    offsetof(Doodads_t, useClickHelper),    STB_SLK_BOOL  },
    { "ignoreModelClick",  offsetof(Doodads_t, ignoreModelClick),  STB_SLK_BOOL  }, /* TFT */
    { "maxPitch",          offsetof(Doodads_t, maxPitch),          STB_SLK_FLOAT },
    { "maxRoll",           offsetof(Doodads_t, maxRoll),           STB_SLK_FLOAT },
    { "visRadius",         offsetof(Doodads_t, visRadius),         STB_SLK_FLOAT },
    { "walkable",          offsetof(Doodads_t, walkable),          STB_SLK_BOOL  },
    { "numVar",            offsetof(Doodads_t, numVar),            STB_SLK_INT   },
    { "onCliffs",          offsetof(Doodads_t, onCliffs),          STB_SLK_BOOL  },
    { "onWater",           offsetof(Doodads_t, onWater),           STB_SLK_BOOL  },
    { "floats",            offsetof(Doodads_t, floats),            STB_SLK_BOOL  },
    { "shadow",            offsetof(Doodads_t, shadow),            STB_SLK_STR   },
    { "showInFog",         offsetof(Doodads_t, showInFog),         STB_SLK_BOOL  },
    { "animInFog",         offsetof(Doodads_t, animInFog),         STB_SLK_BOOL  },
    { "fixedRot",          offsetof(Doodads_t, fixedRot),          STB_SLK_FLOAT },
    { "pathTex",           offsetof(Doodads_t, pathTex),           STB_SLK_STR   },
    { "showInMM",          offsetof(Doodads_t, showInMM),          STB_SLK_BOOL  },
    { "useMMColor",        offsetof(Doodads_t, useMMColor),        STB_SLK_BOOL  },
    { "MMRed",             offsetof(Doodads_t, MMRed),             STB_SLK_INT   },
    { "MMGreen",           offsetof(Doodads_t, MMGreen),           STB_SLK_INT   },
    { "MMBlue",            offsetof(Doodads_t, MMBlue),            STB_SLK_INT   },
    DOOD_VERT("vertR01", 0, 0), DOOD_VERT("vertG01", 0, 1), DOOD_VERT("vertB01", 0, 2),
    DOOD_VERT("vertR02", 1, 0), DOOD_VERT("vertG02", 1, 1), DOOD_VERT("vertB02", 1, 2),
    DOOD_VERT("vertR03", 2, 0), DOOD_VERT("vertG03", 2, 1), DOOD_VERT("vertB03", 2, 2),
    DOOD_VERT("vertR04", 3, 0), DOOD_VERT("vertG04", 3, 1), DOOD_VERT("vertB04", 3, 2),
    DOOD_VERT("vertR05", 4, 0), DOOD_VERT("vertG05", 4, 1), DOOD_VERT("vertB05", 4, 2),
    DOOD_VERT("vertR06", 5, 0), DOOD_VERT("vertG06", 5, 1), DOOD_VERT("vertB06", 5, 2),
    DOOD_VERT("vertR07", 6, 0), DOOD_VERT("vertG07", 6, 1), DOOD_VERT("vertB07", 6, 2),
    DOOD_VERT("vertR08", 7, 0), DOOD_VERT("vertG08", 7, 1), DOOD_VERT("vertB08", 7, 2),
    DOOD_VERT("vertR09", 8, 0), DOOD_VERT("vertG09", 8, 1), DOOD_VERT("vertB09", 8, 2),
    DOOD_VERT("vertR10", 9, 0), DOOD_VERT("vertG10", 9, 1), DOOD_VERT("vertB10", 9, 2),
    { "UserList",          offsetof(Doodads_t, UserList),          STB_SLK_STR   }, /* TFT */
    { "InBeta",           offsetof(Doodads_t, InBeta),            STB_SLK_BOOL  },
    { "version",          offsetof(Doodads_t, version),           STB_SLK_INT   }, /* TFT */
    { NULL, 0, 0 }
};
#undef DOOD_VERT

static slkField_t const uber_schema[] = {
    { "Name",       offsetof(UberSplatData_t, Name),       STB_SLK_STR   },
    { "comment",    offsetof(UberSplatData_t, comment),    STB_SLK_STR   },
    { "Dir",        offsetof(UberSplatData_t, Dir),        STB_SLK_STR   },
    { "file",       offsetof(UberSplatData_t, file),       STB_SLK_STR   },
    { "BlendMode",  offsetof(UberSplatData_t, BlendMode),  STB_SLK_STR   },
    { "Scale",      offsetof(UberSplatData_t, Scale),      STB_SLK_FLOAT },
    { "BirthTime",  offsetof(UberSplatData_t, BirthTime),  STB_SLK_FLOAT },
    { "PauseTime",  offsetof(UberSplatData_t, PauseTime),  STB_SLK_FLOAT },
    { "Decay",      offsetof(UberSplatData_t, Decay),      STB_SLK_FLOAT },
    { "StartR",     offsetof(UberSplatData_t, StartR),     STB_SLK_FLOAT },
    { "StartG",     offsetof(UberSplatData_t, StartG),     STB_SLK_FLOAT },
    { "StartB",     offsetof(UberSplatData_t, StartB),     STB_SLK_FLOAT },
    { "StartA",     offsetof(UberSplatData_t, StartA),     STB_SLK_FLOAT },
    { "MiddleR",    offsetof(UberSplatData_t, MiddleR),    STB_SLK_FLOAT },
    { "MiddleG",    offsetof(UberSplatData_t, MiddleG),    STB_SLK_FLOAT },
    { "MiddleB",    offsetof(UberSplatData_t, MiddleB),    STB_SLK_FLOAT },
    { "MiddleA",    offsetof(UberSplatData_t, MiddleA),    STB_SLK_FLOAT },
    { "EndR",       offsetof(UberSplatData_t, EndR),       STB_SLK_FLOAT },
    { "EndG",       offsetof(UberSplatData_t, EndG),       STB_SLK_FLOAT },
    { "EndB",       offsetof(UberSplatData_t, EndB),       STB_SLK_FLOAT },
    { "EndA",       offsetof(UberSplatData_t, EndA),       STB_SLK_FLOAT },
    { "Sound",      offsetof(UberSplatData_t, Sound),      STB_SLK_STR   },
    { "version",    offsetof(UberSplatData_t, version),    STB_SLK_INT   }, /* TFT */
    { "InBeta",     offsetof(UberSplatData_t, InBeta),     STB_SLK_BOOL  }, /* TFT */
    { NULL, 0, 0 }
};

static slkField_t const sound_schema[] = {
    { "",               offsetof(UnitAckSounds_t, name),           STB_SLK_STR   },
    { "FileNames",      offsetof(UnitAckSounds_t, FileNames),      STB_SLK_STR   },
    { "DirectoryBase",  offsetof(UnitAckSounds_t, DirectoryBase),  STB_SLK_STR   },
    { "Volume",         offsetof(UnitAckSounds_t, Volume),         STB_SLK_FLOAT },
    { "Pitch",          offsetof(UnitAckSounds_t, Pitch),          STB_SLK_FLOAT },
    { "PitchVariance",  offsetof(UnitAckSounds_t, PitchVariance),  STB_SLK_FLOAT },
    { "Priority",       offsetof(UnitAckSounds_t, Priority),       STB_SLK_FLOAT },
    { "Channel",        offsetof(UnitAckSounds_t, Channel),        STB_SLK_STR   },
    { "Flags",          offsetof(UnitAckSounds_t, Flags),          STB_SLK_STR   },
    { "MinDistance",    offsetof(UnitAckSounds_t, MinDistance),    STB_SLK_FLOAT },
    { "MaxDistance",    offsetof(UnitAckSounds_t, MaxDistance),    STB_SLK_FLOAT },
    { "DistanceCutoff", offsetof(UnitAckSounds_t, DistanceCutoff), STB_SLK_FLOAT },
    { "InsideAngle",    offsetof(UnitAckSounds_t, InsideAngle),    STB_SLK_FLOAT }, /* ROC */
    { "OutsideAngle",   offsetof(UnitAckSounds_t, OutsideAngle),   STB_SLK_FLOAT }, /* ROC */
    { "OutsideVolume",  offsetof(UnitAckSounds_t, OutsideVolume),  STB_SLK_FLOAT }, /* ROC */
    { "OrientationX",   offsetof(UnitAckSounds_t, OrientationX),   STB_SLK_FLOAT }, /* ROC */
    { "OrientationY",   offsetof(UnitAckSounds_t, OrientationY),   STB_SLK_FLOAT }, /* ROC */
    { "OrientationZ",   offsetof(UnitAckSounds_t, OrientationZ),   STB_SLK_FLOAT }, /* ROC */
    { "EAXFlags",       offsetof(UnitAckSounds_t, EAXFlags),       STB_SLK_STR   },
    { "InBeta",        offsetof(UnitAckSounds_t, InBeta),         STB_SLK_BOOL  }, /* TFT */
    { "version",       offsetof(UnitAckSounds_t, version),        STB_SLK_INT   }, /* TFT */
    { NULL, 0, 0 }
};

static slkField_t const item_schema[] = {
    { "scriptname",  offsetof(ItemData_t, scriptname),  STB_SLK_STR   },
    { "version",     offsetof(ItemData_t, version),     STB_SLK_INT   },
    { "InBeta",      offsetof(ItemData_t, InBeta),      STB_SLK_BOOL  },
    { "file",        offsetof(ItemData_t, file),        STB_SLK_STR   },
    { "abilList",    offsetof(ItemData_t, abilList),    STB_SLK_STR   },
    { "class",       offsetof(ItemData_t, itemClass),   STB_SLK_STR   }, /* TFT */
    { "itemClass",   offsetof(ItemData_t, itemClass),   STB_SLK_STR   }, /* ROC alias */
    { "armor",       offsetof(ItemData_t, armor),       STB_SLK_INT   },
    { "goldcost",    offsetof(ItemData_t, goldcost),    STB_SLK_INT   },
    { "lumbercost",  offsetof(ItemData_t, lumbercost),  STB_SLK_INT   },
    { "HP",          offsetof(ItemData_t, HP),          STB_SLK_INT   },
    { "Level",       offsetof(ItemData_t, level),       STB_SLK_INT   },
    { "prio",        offsetof(ItemData_t, prio),        STB_SLK_INT   },
    { "stockMax",    offsetof(ItemData_t, stockMax),    STB_SLK_INT   },
    { "stockRegen",  offsetof(ItemData_t, stockRegen),  STB_SLK_INT   },
    { "stockStart",  offsetof(ItemData_t, stockStart),  STB_SLK_INT   },
    { "uses",        offsetof(ItemData_t, uses),        STB_SLK_INT   },
    { "scale",       offsetof(ItemData_t, scale),       STB_SLK_FLOAT }, /* TFT */
    { "selSize",     offsetof(ItemData_t, selectionSize), STB_SLK_FLOAT }, /* TFT */
    { "droppable",   offsetof(ItemData_t, droppable),   STB_SLK_BOOL  },
    { "drop",        offsetof(ItemData_t, drop),        STB_SLK_BOOL  },
    { "perishable",  offsetof(ItemData_t, perishable),  STB_SLK_BOOL  },
    { "sellable",    offsetof(ItemData_t, sellable),    STB_SLK_BOOL  }, /* TFT */
    { "pawnable",    offsetof(ItemData_t, pawnable),    STB_SLK_BOOL  }, /* TFT */
    { "usable",      offsetof(ItemData_t, usable),      STB_SLK_BOOL  },
    { "pickRandom",  offsetof(ItemData_t, pickRandom),  STB_SLK_BOOL  }, /* TFT */
    { "powerup",     offsetof(ItemData_t, powerup),     STB_SLK_BOOL  }, /* TFT */
    { "morph",       offsetof(ItemData_t, morph),       STB_SLK_BOOL  }, /* TFT */
    { "ignoreCD",    offsetof(ItemData_t, ignoreCD),    STB_SLK_BOOL  }, /* TFT */
    { "cooldownID",  offsetof(ItemData_t, cooldownID),  STB_SLK_STR   }, /* TFT */
    { "oldLevel",    offsetof(ItemData_t, oldLevel),    STB_SLK_INT   }, /* TFT */
    { "colorR",      offsetof(ItemData_t, colorR),      STB_SLK_INT   }, /* TFT */
    { "colorG",      offsetof(ItemData_t, colorG),      STB_SLK_INT   }, /* TFT */
    { "colorB",      offsetof(ItemData_t, colorB),      STB_SLK_INT   }, /* TFT */
    { "comment",     offsetof(ItemData_t, displayName), STB_SLK_STR   }, /* item display name */
    { NULL, 0, 0 }
};

static slkField_t const dest_schema[] = {
    { "category",         offsetof(DestructableData_t, category),         STB_SLK_STR   },
    { "tilesets",         offsetof(DestructableData_t, tilesets),         STB_SLK_STR   },
    { "comment",          offsetof(DestructableData_t, comment),          STB_SLK_STR   },
    { "doodClass",        offsetof(DestructableData_t, doodClass),        STB_SLK_STR   },
    { "version",          offsetof(DestructableData_t, version),          STB_SLK_INT   },
    { "InBeta",           offsetof(DestructableData_t, InBeta),           STB_SLK_BOOL  },
    { "file",             offsetof(DestructableData_t, file),             STB_SLK_STR   },
    { "Name",             offsetof(DestructableData_t, displayName),      STB_SLK_STR   }, /* TFT */
    { "name",             offsetof(DestructableData_t, displayName),      STB_SLK_STR   }, /* ROC alias */
    { "EditorSuffix",     offsetof(DestructableData_t, EditorSuffix),     STB_SLK_STR   }, /* TFT */
    { "texFile",          offsetof(DestructableData_t, textureFile),      STB_SLK_STR   },
    { "texID",            offsetof(DestructableData_t, texID),            STB_SLK_STR   },
    { "shadow",           offsetof(DestructableData_t, shadow),           STB_SLK_STR   },
    { "pathTex",          offsetof(DestructableData_t, pathingTexture),   STB_SLK_STR   },
    { "pathTexDeath",     offsetof(DestructableData_t, deathPathingTexture), STB_SLK_STR   },
    { "deathSnd",         offsetof(DestructableData_t, deathSnd),         STB_SLK_STR   },
    { "targType",         offsetof(DestructableData_t, targetType),       STB_SLK_STR   },
    { "portraitmodel",    offsetof(DestructableData_t, portraitmodel),    STB_SLK_STR   }, /* TFT */
    { "UserList",         offsetof(DestructableData_t, UserList),         STB_SLK_STR   }, /* TFT */
    { "HP",               offsetof(DestructableData_t, maxHealth),        STB_SLK_INT   },
    { "armor",            offsetof(DestructableData_t, armor),            STB_SLK_INT   },
    { "numVar",           offsetof(DestructableData_t, numVar),           STB_SLK_INT   },
    { "selSize",          offsetof(DestructableData_t, selSize),          STB_SLK_FLOAT },
    { "minScale",         offsetof(DestructableData_t, minScale),         STB_SLK_FLOAT },
    { "maxScale",         offsetof(DestructableData_t, maxScale),         STB_SLK_FLOAT },
    { "maxPitch",         offsetof(DestructableData_t, maxPitch),         STB_SLK_FLOAT },
    { "maxRoll",          offsetof(DestructableData_t, maxRoll),          STB_SLK_FLOAT },
    { "radius",           offsetof(DestructableData_t, radius),           STB_SLK_FLOAT },
    { "fogRadius",        offsetof(DestructableData_t, fogRadius),        STB_SLK_FLOAT },
    { "occH",             offsetof(DestructableData_t, occluderHeight),   STB_SLK_FLOAT },
    { "flyH",             offsetof(DestructableData_t, flyHeight),        STB_SLK_FLOAT },
    { "cliffHeight",      offsetof(DestructableData_t, cliffHeight),      STB_SLK_FLOAT },
    { "fogVis",           offsetof(DestructableData_t, fogVisible),       STB_SLK_BOOL  },
    { "fatLOS",           offsetof(DestructableData_t, fatLOS),           STB_SLK_BOOL  },
    { "walkable",         offsetof(DestructableData_t, walkable),         STB_SLK_BOOL  },
    { "onCliffs",         offsetof(DestructableData_t, onCliffs),         STB_SLK_BOOL  },
    { "onWater",          offsetof(DestructableData_t, onWater),          STB_SLK_BOOL  },
    { "canPlaceDead",     offsetof(DestructableData_t, canPlaceDead),     STB_SLK_BOOL  },
    { "canPlaceRandScale",offsetof(DestructableData_t, canPlaceRandScale),STB_SLK_BOOL  },
    { "lightweight",      offsetof(DestructableData_t, lightweight),      STB_SLK_BOOL  },
    { "tilesetSpecific",  offsetof(DestructableData_t, tilesetSpecific),  STB_SLK_BOOL  },
    { "useClickHelper",   offsetof(DestructableData_t, useClickHelper),   STB_SLK_BOOL  },
    { "showInMM",         offsetof(DestructableData_t, showInMM),         STB_SLK_BOOL  },
    { "useMMColor",       offsetof(DestructableData_t, useMMColor),       STB_SLK_BOOL  },
    { "fixedRot",         offsetof(DestructableData_t, fixedRot),         STB_SLK_BOOL  },
    { "selectable",       offsetof(DestructableData_t, selectable),       STB_SLK_BOOL  }, /* TFT */
    { "MMRed",            offsetof(DestructableData_t, MMRed),            STB_SLK_INT   },
    { "MMGreen",          offsetof(DestructableData_t, MMGreen),          STB_SLK_INT   },
    { "MMBlue",           offsetof(DestructableData_t, MMBlue),           STB_SLK_INT   },
    { "buildTime",        offsetof(DestructableData_t, buildTime),        STB_SLK_INT   }, /* TFT */
    { "repairTime",       offsetof(DestructableData_t, repairTime),       STB_SLK_INT   }, /* TFT */
    { "goldRep",          offsetof(DestructableData_t, goldRep),          STB_SLK_INT   }, /* TFT */
    { "lumberRep",        offsetof(DestructableData_t, lumberRep),        STB_SLK_INT   }, /* TFT */
    { "selcircsize",      offsetof(DestructableData_t, selcircsize),      STB_SLK_FLOAT }, /* TFT */
    { "colorR",           offsetof(DestructableData_t, colorR),           STB_SLK_INT   }, /* TFT */
    { "colorG",           offsetof(DestructableData_t, colorG),           STB_SLK_INT   }, /* TFT */
    { "colorB",           offsetof(DestructableData_t, colorB),           STB_SLK_INT   }, /* TFT */
    { "dir",              offsetof(DestructableData_t, modelDirectory),   STB_SLK_STR   }, /* ROC */
    { NULL, 0, 0 }
};

/* =========================================================================
 * Decoded row arrays and lookup indexes (allocated at InitUnitData time).
 * =========================================================================*/
UnitBalance_t *g_UnitBalance; DWORD g_UnitBalanceCount; static slkIndex_t balance_idx;
UnitData_t *g_UnitData; DWORD g_UnitDataCount; static slkIndex_t data_idx;
UnitUI_t *g_UnitUI; DWORD g_UnitUICount; static slkIndex_t ui_idx;
UnitWeapons_t *g_UnitWeapons; DWORD g_UnitWeaponsCount; static slkIndex_t weapons_idx;
UnitAbilities_t *g_UnitAbilities; DWORD g_UnitAbilitiesCount; static slkIndex_t abil_idx;
AbilityData_t *g_AbilityData; DWORD g_AbilityDataCount; static slkIndex_t ability_idx;
Doodads_t *g_Doodads; DWORD g_DoodadsCount; static slkIndex_t doodad_idx;
UberSplatData_t *g_UberSplatData; DWORD g_UberSplatDataCount; static slkIndex_t uber_idx;
UnitAckSounds_t *g_UnitAckSounds; DWORD g_UnitAckSoundsCount;
ItemData_t *g_ItemData; DWORD g_ItemDataCount; static slkIndex_t item_idx;
DestructableData_t *g_DestructableData; DWORD g_DestructableDataCount; static slkIndex_t dest_idx;

typedef struct {
    LPCSTR name, path;
    slkField_t const *schema;
    size_t row_size;
    void **rows;
    DWORD *count;
    slkIndex_t *idx;
#ifdef BZ_TESTS
    sheetRow_t *source;
#endif
} slkStore_t;

static slkStore_t slk_stores[] = {
    { "UnitBalance", "Units\\UnitBalance.slk", balance_schema, sizeof(*g_UnitBalance), (void **)&g_UnitBalance, &g_UnitBalanceCount, &balance_idx },
    { "UnitData", "Units\\UnitData.slk", data_schema, sizeof(*g_UnitData), (void **)&g_UnitData, &g_UnitDataCount, &data_idx },
    { "UnitUI", "Units\\UnitUI.slk", ui_schema, sizeof(*g_UnitUI), (void **)&g_UnitUI, &g_UnitUICount, &ui_idx },
    { "UnitWeapons", "Units\\UnitWeapons.slk", weapons_schema, sizeof(*g_UnitWeapons), (void **)&g_UnitWeapons, &g_UnitWeaponsCount, &weapons_idx },
    { "UnitAbilities", "Units\\UnitAbilities.slk", abil_schema, sizeof(*g_UnitAbilities), (void **)&g_UnitAbilities, &g_UnitAbilitiesCount, &abil_idx },
    { "AbilityData", "Units\\AbilityData.slk", ability_schema, sizeof(*g_AbilityData), (void **)&g_AbilityData, &g_AbilityDataCount, &ability_idx },
    { "Doodads", "Doodads\\Doodads.slk", doodad_schema, sizeof(*g_Doodads), (void **)&g_Doodads, &g_DoodadsCount, &doodad_idx },
    { "UberSplatData", "Splats\\UberSplatData.slk", uber_schema, sizeof(*g_UberSplatData), (void **)&g_UberSplatData, &g_UberSplatDataCount, &uber_idx },
    { "UnitAckSounds", "UI\\SoundInfo\\UnitAckSounds.slk", sound_schema, sizeof(*g_UnitAckSounds), (void **)&g_UnitAckSounds, &g_UnitAckSoundsCount, NULL },
    { "ItemData", "Units\\ItemData.slk", item_schema, sizeof(*g_ItemData), (void **)&g_ItemData, &g_ItemDataCount, &item_idx },
    { "DestructableData", "Units\\DestructableData.slk", dest_schema, sizeof(*g_DestructableData), (void **)&g_DestructableData, &g_DestructableDataCount, &dest_idx },
};

/* Decode `head` rows into a typed flat array and build a sorted FOURCC index.
 * Rebuilds *rows_out (freeing any old allocation) from the NULL-terminated schema.
 * Called both at startup and when a test replaces one typed table's source. */
static void RebuildDDXFromRows(slkIndex_t *idx, slkField_t const *schema,
                               void **rows_out, DWORD *count_out, size_t row_size,
                               sheetRow_t const *head) {
    FS_SLKFreeIndex(idx); FS_SLKFreeRows(schema, *rows_out, *count_out, row_size); *rows_out = NULL; *count_out = 0;
    if (!head) return;
    DWORD n = 0; FOR_EACH_LIST(sheetRow_t const, r, head) n++;
    *rows_out = calloc(n, row_size);
    if (!*rows_out) { fprintf(stderr, "SLK: OOM for %u rows\n", n); return; }
    *count_out = n;
    DWORD i = 0;
    FOR_EACH_LIST(sheetRow_t const, r, head) {
        BYTE *row = (BYTE *)*rows_out + i++ * row_size;
        *(DWORD *)row = FS_SLKKey(r->name);
        FS_SLKDecodeRow(r, schema, row);
    }
    FS_SLKBuildIndex(idx, head, *rows_out, n, row_size);
}

/* Parse SLK from disk, decode and index it, and warn once per column that has
 * no DDX schema entry. The raw rows remain parser-owned source data. */
static sheetRow_t *ParseSLKInto(LPCSTR path, slkIndex_t *idx, slkField_t const *schema, size_t row_size,
                                void **rows_out, DWORD *count_out) {
    sheetRow_t *head = FS_ParseSLK(path);
    if (!head) { fprintf(stderr, "SLK: failed to load '%s'\n", path); return NULL; }
    RebuildDDXFromRows(idx, schema, rows_out, count_out, row_size, head);
    /* Warn once per first-row column not covered by the schema (dev aid). */
    if (head && head->fields) {
        FOR_EACH_LIST(sheetField_t const, f, head->fields) {
            bool found = false;
            for (slkField_t const *s = schema; s->column; s++)
                if (!strcasecmp(f->name, s->column)) { found = true; break; }
            if (!found)
                fprintf(stderr, "SLK DDX: column '%s' in '%s' has no schema entry\n",
                        f->name, path);
        }
    }
    return head;
}

/* Tests replace a typed table and restore its original parser-owned source. */
#ifdef BZ_TESTS
sheetRow_t *G_SetSLKRows(LPCSTR slk, sheetRow_t *table) {
    FOR_LOOP(i, sizeof(slk_stores) / sizeof(*slk_stores)) {
        slkStore_t *store = slk_stores + i;
        if (!strcmp(slk, store->name)) {
            sheetRow_t *old = store->source;
            store->source = table;
            RebuildDDXFromRows(store->idx, store->schema, store->rows, store->count, store->row_size, table);
            return old;
        }
    }
    fprintf(stderr, "SLK: unknown typed table '%s'\n", slk);
    return NULL;
}
#endif

/* =========================================================================
 * Public lookup functions.
 * Map-created units remap their ID to the base unit ID, matching the
 * per-call remap that UnitStringField() performs for Profile fallback.
 * =========================================================================*/
static DWORD ResolveUnitID(DWORD id) {
    if (!level.mapinfo) return id;
    FOR_LOOP(n, level.mapinfo->num_userCreatedUnits) {
        if (level.mapinfo->userCreatedUnits[n].newUnitID == id)
            return level.mapinfo->userCreatedUnits[n].originalUnitID;
    }
    return id;
}

UnitBalance_t const *G_UnitBalance(DWORD id) { static UnitBalance_t zero; UnitBalance_t *row = FS_SLKLookup(&balance_idx, ResolveUnitID(id)); return row ? row : &zero; }
UnitData_t const *G_UnitData(DWORD id) { static UnitData_t zero; UnitData_t *row = FS_SLKLookup(&data_idx, ResolveUnitID(id)); return row ? row : &zero; }
UnitUI_t const *G_UnitUI(DWORD id) { static UnitUI_t zero; UnitUI_t *row = FS_SLKLookup(&ui_idx, ResolveUnitID(id)); return row ? row : &zero; }
UnitWeapons_t const *G_UnitWeapons(DWORD id) { static UnitWeapons_t zero; UnitWeapons_t *row = FS_SLKLookup(&weapons_idx, ResolveUnitID(id)); return row ? row : &zero; }
UnitAbilities_t const *G_UnitAbil(DWORD id) { static UnitAbilities_t zero; UnitAbilities_t *row = FS_SLKLookup(&abil_idx, ResolveUnitID(id)); return row ? row : &zero; }
AbilityData_t const *G_AbilityData(DWORD id) { static AbilityData_t zero; AbilityData_t *row = FS_SLKLookup(&ability_idx, id); return row ? row : &zero; }
AbilityData_t const *G_AbilityDataName(LPCSTR name) { return G_AbilityData(FS_SLKKey(name)); }
DWORD G_AbilityCode(DWORD id) { DWORD code = G_AbilityData(id)->code; return code ? code : id; }
DWORD G_AbilityCodeName(LPCSTR name) { return G_AbilityCode(FS_SLKKey(name)); }

/* Tooltip markup names authored AbilityData columns, so reflect through the
 * same DDX schema while gameplay continues to use typed fields directly. */
LPCSTR G_AbilityDataText(LPCSTR name, LPCSTR column) {
    static char text[4][32]; static DWORD cursor;
    AbilityData_t const *row = G_AbilityDataName(name);
    LPSTR out = text[cursor++ & 3];
    for (slkField_t const *field = ability_schema; field->column; field++) {
        BYTE const *value;
        if (strcasecmp(column, field->column)) continue;
        value = (BYTE const *)row + field->offset;
        if (field->type == BZ_FIELD_CSTR) return *(LPCSTR const *)value;
        if (field->type == BZ_FIELD_FLOAT) snprintf(out, 32, "%g", *(FLOAT const *)value);
        else if (field->type == BZ_FIELD_FOURCC) snprintf(out, 32, "%.4s", (LPCSTR)value);
        else snprintf(out, 32, "%u", *(DWORD const *)value);
        return out;
    }
    return NULL;
}
Doodads_t const *G_Doodad(DWORD id) { static Doodads_t zero; Doodads_t *row = FS_SLKLookup(&doodad_idx, id); return row ? row : &zero; }
UberSplatData_t const *G_UberSplat(DWORD id) { static UberSplatData_t zero; UberSplatData_t *row = FS_SLKLookup(&uber_idx, id); return row ? row : &zero; }
UnitAckSounds_t const *G_UnitAckSound(LPCSTR name) {
    static UnitAckSounds_t zero;
    FOR_LOOP(i, g_UnitAckSoundsCount) if (!strcmp(g_UnitAckSounds[i].name, name)) return g_UnitAckSounds + i;
    return &zero;
}
ItemData_t const *G_ItemData(DWORD id) { static ItemData_t zero; ItemData_t *row = FS_SLKLookup(&item_idx, ResolveUnitID(id)); return row ? row : &zero; }
ItemData_t const *G_ItemDataRows(DWORD *count) { *count = g_ItemDataCount; return g_ItemData; }
DestructableData_t const *G_DestructableData(DWORD id) { static DestructableData_t zero; DestructableData_t *row = FS_SLKLookup(&dest_idx, ResolveUnitID(id)); return row ? row : &zero; }

/* TFT stores collision in UnitBalance; ROC stores it in UnitData. */
FLOAT G_UnitCollision(DWORD id) {
    UnitBalance_t const *b = G_UnitBalance(id);
    if (b && b->collision > 0.f) return b->collision;
    UnitData_t const *d = G_UnitData(id);
    return d ? d->collision : 0.f;
}

/* Unit classification moved from UnitData (ROC) to UnitBalance (TFT). */
LONG G_UnitClassification(DWORD id) {
    UnitBalance_t const *b = G_UnitBalance(id);
    LPCSTR type = b ? b->type : NULL;
    if (!type || !type[0]) {
        UnitData_t const *d = G_UnitData(id);
        type = d ? d->unitClassification : NULL;
    }
    return type ? atoi(type) : 0;
}

/* Cast timings moved from UnitData (ROC) to UnitWeapons (TFT). */
FLOAT G_UnitCastBackSwing(DWORD id) {
    UnitWeapons_t const *w = G_UnitWeapons(id);
    if (w && w->castBackSwing != 0.f) return w->castBackSwing;
    UnitData_t const *d = G_UnitData(id);
    return d ? d->castBackSwing : 0.f;
}

/* Cast point follows the same ROC/TFT split as cast back-swing. */
FLOAT G_UnitCastPoint(DWORD id) {
    UnitWeapons_t const *w = G_UnitWeapons(id);
    if (w && w->castPoint != 0.f) return w->castPoint;
    UnitData_t const *d = G_UnitData(id);
    return d ? d->castPoint : 0.f;
}

/* Launch offsets moved from UnitData (ROC) to UnitWeapons (TFT). */
FLOAT G_UnitAttack1LaunchX(DWORD id) {
    UnitWeapons_t const *w = G_UnitWeapons(id);
    if (w && w->attackLaunchX != 0.f) return w->attackLaunchX;
    UnitData_t const *d = G_UnitData(id);
    return d ? d->launchOffsetX : 0.f;
}

/* Launch offsets moved from UnitData (ROC) to UnitWeapons (TFT). */
FLOAT G_UnitAttack1LaunchY(DWORD id) {
    UnitWeapons_t const *w = G_UnitWeapons(id);
    if (w && w->attackLaunchY != 0.f) return w->attackLaunchY;
    UnitData_t const *d = G_UnitData(id);
    return d ? d->launchOffsetY : 0.f;
}

/* Launch offsets moved from UnitData (ROC) to UnitWeapons (TFT). */
FLOAT G_UnitAttack1LaunchZ(DWORD id) {
    UnitWeapons_t const *w = G_UnitWeapons(id);
    if (w && w->attackLaunchZ != 0.f) return w->attackLaunchZ;
    UnitData_t const *d = G_UnitData(id);
    return d ? d->launchOffsetZ : 0.f;
}

/* Is-building moved from UnitUI (ROC) to UnitBalance (TFT). */
BOOL G_UnitIsBuilding(DWORD id) {
    UnitBalance_t const *b = G_UnitBalance(id);
    if (b && b->isBuilding) return true;
    UnitUI_t const *ui = G_UnitUI(id);
    return ui ? ui->isBuilding : false;
}

void InitUnitData(void) {
    sheetRow_t *Profile = NULL;
    sheetRow_t *profileTail = NULL;

    abilityConfigs = NULL;
    abilityConfigsTail = NULL;
    commandFuncConfig = NULL;
    commandStringsConfig = NULL;
    abilityConfigTableCount = 0;
    
    for (LPCSTR *config = config_files; *config; config++) {
        sheetRow_t *current = FS_ParseINI(*config);
        if (current) {
            if (abilityConfigTableCount < sizeof(abilityConfigTables) / sizeof(*abilityConfigTables)) {
                abilityConfigTables[abilityConfigTableCount++] = current;
            }
            AppendSheetRows(&abilityConfigs, &abilityConfigsTail, current);
            if (!strcmp(*config, "Units\\CommandFunc.txt")) {
                commandFuncConfig = current;
            } else if (!strcmp(*config, "Units\\CommandStrings.txt")) {
                commandStringsConfig = current;
            }
        }
    }
    for (LPCSTR *config = profile_files; *config; config++) {
        sheetRow_t *current = FS_ParseINI(*config);
        if (current) {
            AppendSheetRows(&Profile, &profileTail, current);
        }
    }
    /* Profile/INI fields (UNIT_NAME, UNIT_TRAINS, UNIT_BUILDS, Missileart, etc.)
     * still use the string-based lookup path. */
    for (sheetMetaData_t *data = UnitsMetaData; data->id; data++)
        if (!strcmp(data->slk, "Profile")) data->table = Profile;

    FOR_LOOP(i, sizeof(slk_stores) / sizeof(*slk_stores)) {
        slkStore_t *store = slk_stores + i;
        sheetRow_t *source = ParseSLKInto(store->path, store->idx, store->schema, store->row_size, store->rows, store->count);
    #ifdef BZ_TESTS
        store->source = source;
    #else
        (void)source;
    #endif
    }
}

void ShutdownUnitData(void) {
    FOR_LOOP(i, sizeof(slk_stores) / sizeof(*slk_stores)) {
        slkStore_t *store = slk_stores + i;
        FS_SLKFreeIndex(store->idx);
        FS_SLKFreeRows(store->schema, *store->rows, *store->count, store->row_size);
        *store->rows = NULL; *store->count = 0;
    #ifdef BZ_TESTS
        store->source = NULL;
    #endif
    }
}

static LPCSTR FindConfigField(sheetRow_t *sheet, LPCSTR row, LPCSTR column) {
    FOR_EACH_LIST(sheetRow_t const, srow, sheet) {
        if (strcmp(srow->name, row)) {
            continue;
        }
        FOR_EACH_LIST(sheetField_t const, scolumn, srow->fields) {
            if (!strcasecmp(scolumn->name, column)) {
                return scolumn->value;
            }
        }
    }
    return NULL;
}

LPCSTR FindConfigValue(LPCSTR category, LPCSTR field) {
    LPCSTR value;

    if (!strncmp(category, "Cmd", 3)) {
        if (commandFuncConfig) {
            value = FindConfigField(commandFuncConfig, category, field);
            if (value) {
                return value;
            }
        }
        if (commandStringsConfig) {
            value = FindConfigField(commandStringsConfig, category, field);
            if (value) {
                return value;
            }
        }
    }

    FOR_LOOP(i, abilityConfigTableCount) {
        value = FindConfigField(abilityConfigTables[i], category, field);
        if (value) {
            return value;
        }
    }
    return NULL;
}

LPCSTR GetClassName(DWORD class_id) {
    static char classname[5] = { 0 };
    memcpy(classname, &class_id, 4);
    return classname;
}
