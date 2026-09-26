#ifndef __mapinfo_h__
#define __mapinfo_h__

#include "common/shared.h"

#define TILE_SIZE 128
#define SEGMENT_SIZE 32
//#define AREA_SIZE (TILE_SIZE * SEGMENT_SIZE)
#define PLAYER_NEUTRAL_AGGRESSIVE 12 // player slot; Warcraft III hostile neutral owner
#define PLAYER_NEUTRAL_VICTIM 13 // player slot; Warcraft III neutral victim owner
#define PLAYER_NEUTRAL_EXTRA 14 // player slot; Warcraft III extra neutral owner
#define PLAYER_NEUTRAL_PASSIVE 15 // player slot; Warcraft III passive neutral owner

typedef struct {
    float bounds[8];
    struct {
        /* W3I stores the four complements as left, right, bottom, top.
         * Keep this declaration in on-disk order: CM_ReadInfoInto reads the
         * complete mapCameraBounds_t directly from war3map.w3i. */
        int left, right, bottom, top;
    } complement;
} mapCameraBounds_t;

enum mapInfoFlags_t {
    hide_minimap_in_preview_screens = 0x0001,
    modify_ally_priorities = 0x0002,
    melee_map = 0x0004,
    playable_map_size_was_large_and_has_never_been_reduced_to_medium = 0x0008,
    masked_area_are_partially_visible = 0x0010,
    fixed_player_setting_for_custom_forces = 0x0020,
    use_custom_forces = 0x0040,
    use_custom_techtree = 0x0080,
    use_custom_abilities = 0x0100,
    use_custom_upgrades = 0x0200,
    map_properties_menu_opened_at_least_once_since_map_creation = 0x0400,
    show_water_waves_on_cliff_shores = 0x0800,
    show_water_waves_on_rolling_shores = 0x1000,
};

enum playerFlags_t {
    fixed_start_position = 0x0001,
};

typedef enum {
    kPlayerTypeNone,
    kPlayerTypeHuman,
    kPlayerTypeComputer,
    kPlayerTypeNeutral,
    kPlayerTypeRescuable
} playerType_t;

typedef enum {
    allied_force_1 = 0x0001,
    allied_victory = 0x0002,
    share_vision = 0x0004,
    share_unit_control = 0x0010,
    share_advanced_unit_control = 0x0020,
} forceFlags_t;

typedef struct mapPlayer_s {
//    uint32_t number;
    bool used;
    playerType_t playerType;
    playerRace_t playerRace;
    uint32_t flags;
    string_t playerName;
    vector2_t startingPosition;
    uint32_t allyLowPrioritiesFlags; // (bit "x"=1 -> set for player "x")
    uint32_t allyHighPrioritiesFlags; // (bit "x"=1 -> set for player "x")
    uint32_t enemyLowPrioritiesFlags; // 1.32+
    uint32_t enemyHighPrioritiesFlags; // 1.32+
    uint32_t color; // runtime only; war3map.w3i records are parsed field-by-field above
} mapPlayer_t;

typedef struct {
    uint32_t flags;
    uint32_t playerMasks; // (bit "x"=1 -> player "x" is in this team)
    string_t name;
} mapTeam_t;

typedef enum {
    upgrade_unavailable,
    upgrace_available,
    upgrade_researched
} upgradeAvailability_t;

typedef struct {
    uint32_t playerFlags; // (bit "x"=1 if this change applies for player "x")
    uint32_t upgradeID; // (as in UpgradeData.slk)
    uint32_t levelOfTheUpgrade; // for which the availability is changed (this is actually the level - 1, so 1 => 0)
    upgradeAvailability_t availability; // (0 = unavailable, 1 = available, 2 = researched)
} mapUpgradeAvailability_t;

typedef struct {
    uint32_t playerFlags; // (bit "x"=1 if this change applies for player "x")
    uint32_t techID; // (this can be an item, unit or ability)
    // there's no need for an availability value, if a tech-id is in this list, it means that it's not available
} mapTechAvailability_t;

typedef enum {
    group_unit_table,
    group_building_table,
    group_item_table
} mapRandomGroupPositionType_t;

typedef struct {
    mapRandomGroupPositionType_t type;
    uint32_t chanceItem; // (percentage)
    uint32_t itemID;
//    for each position are the unit/item id's for this line specified
//    this can also be random unit/item ids (see bottom of war3mapUnits.doo definition)
//    a unit/item id of 0x00000000 indicates that no unit/item is created
} mapRandomGroupPositionItem_t;

typedef struct {
    uint32_t chance;
    uint32_t *itemIDs; // one unit/item id for each position in the owning random table
} mapRandomUnit_t;

typedef struct {
    uint32_t tableNumber;
    string_t tableName;
    uint32_t num_positions;
    mapRandomGroupPositionType_t *positionTypes;
    uint32_t num_units;
    mapRandomUnit_t *units;
} mapRandomUnitTable_t;

typedef struct {
    uint32_t chance;
    uint32_t itemID;
} mapRandomItem_t;

typedef struct {
    uint32_t num_items;
    mapRandomItem_t *items;
} mapRandomItemSet_t;

typedef struct {
    uint32_t tableNumber;
    string_t tableName;
    uint32_t num_sets;
    mapRandomItemSet_t *sets;
} mapRandomItemTable_t;

#define MAX_TRIGSTR_LENGTH 1024

typedef struct trigstr {
    uint32_t id;
    char text[MAX_TRIGSTR_LENGTH];
    struct trigstr *next;
} mapTrigStr_t;

typedef enum {
    mod_int,
    mod_real,
    mod_unreal,
    mod_string,
    mod_bool,
    mod_char,
    mod_unitList,
    mod_itemList,
    mod_regenType,
    mod_attackType,
    mod_weaponType,
    mod_targetType,
    mod_moveType,
    mod_defenseType,
    mod_pathingTexture,
    mod_upgradeList,
    mod_stringList,
    mod_abilityList,
    mod_heroAbilityList,
    mod_missileArt,
    mod_attributeType,
    mod_attackBits,
} unitModificationType_t;

typedef struct {
    box2_t bounds;
    uint32_t weatherID;
} mapWeatherRegion_t;

typedef struct unitModification_t {
    uint32_t modID;
    unitModificationType_t type;
    uint32_t level;       /* w3a/w3q/w3d leveled fields; 0 for w3u */
    uint32_t dataPointer; /* W3A: 1..9 address DataA..DataI; 0 for non-Data fields */
    handle_t data;
} unitModification_t;

typedef struct {
    uint32_t originalUnitID; // from "Units\UnitData.slk"
    uint32_t newUnitID;
    uint16_t numbeOfModifications;
    unitModification_t *modifications;
} unitData_t;

struct mapInfo_s {
    uint32_t fileFormat; // file format version = 18
    uint32_t numberOfSaves;
    uint32_t editorVersion;
    uint32_t gameVersionMajor; // format 28+
    uint32_t gameVersionMinor; // format 28+
    uint32_t gameVersionPatch; // format 28+
    uint32_t gameVersionBuild; // format 28+
    string_t mapName;
    string_t mapAuthor;
    string_t mapDescription;
    string_t playersRecommended;
    mapCameraBounds_t cameraBounds; // as defined in the JASS file
    size2_t playableArea; // width E, height F, *note 1: map width = A + E + B, map height = C + F + D
    uint32_t flags;
    char mainGroundType; // Example: 'A'= Ashenvale, 'X' = City Dalaran
    uint32_t campaignBackgroundNumber; // (-1 = none)
    string_t loadingScreenModel; // TFT+
    string_t loadingScreenText;
    string_t loadingScreenTitle;
    string_t loadingScreenSubtitle;
    uint32_t loadingScreenNumber; // (-1 = none)
    uint32_t gameDataSet; // TFT+: mapGameDataSet_t (0 falls back from melee_map flag)
    string_t prologueScreenModel; // TFT+
    string_t prologueScreenText;
    string_t prologueScreenTitle;
    string_t prologueScreenSubtitle;
    uint32_t fogStyle; // TFT+
    float fogStartZ; // TFT+
    float fogEndZ; // TFT+
    float fogDensity; // TFT+
    color32_t fogColor; // TFT+
    uint32_t weatherID; // TFT+
    string_t soundEnvironment; // TFT+
    uint8_t lightEnvironmentTileset; // TFT+
    color32_t waterColor; // TFT+
    uint32_t scriptType; // format 28+
    uint32_t supportedModes; // format 31+
    uint32_t gameDataVersion; // format 31+
    uint32_t defaultZoomOverride; // format 32+
    uint32_t maximumZoomOverride; // format 32+
    uint32_t minimumZoomOverride; // format 33+
//    uint32_t num_players;
    uint32_t num_teams;
    uint32_t num_upgradeAvailabilities;
    uint32_t num_techAvailabilities;
    uint32_t num_randomUnits;
    uint32_t num_randomItems;
    uint32_t num_originalUnits;
    uint32_t num_userCreatedUnits;
    uint32_t num_originalItems;
    uint32_t num_userCreatedItems;
    uint32_t num_originalAbilities;
    uint32_t num_userCreatedAbilities;
    uint32_t num_weatherRegions;
    mapPlayer_t players[MAX_PLAYERS];
    mapTeam_t *teams;
    mapUpgradeAvailability_t *upgradeAvailabilities;
    mapTechAvailability_t *techAvailabilities;
    mapRandomUnitTable_t *randomUnits;
    mapRandomItemTable_t *randomItems;
    mapTrigStr_t *strings;
    unitData_t *originalUnits;
    unitData_t *userCreatedUnits;
    /* war3map.w3t uses the same simple object-modification record layout as
     * war3map.w3u; unitData_t keeps the existing parser-owned representation. */
    unitData_t *originalItems;
    unitData_t *userCreatedItems;
    unitData_t *originalAbilities; /* war3map.w3a original-table rows */
    unitData_t *userCreatedAbilities;
    mapWeatherRegion_t *weatherRegions;
    string_t mapscript;
};

#endif
