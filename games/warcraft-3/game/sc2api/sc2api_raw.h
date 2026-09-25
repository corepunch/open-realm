#ifndef WC3_SC2API_RAW_H
#define WC3_SC2API_RAW_H

#include "sc2api_compat.h"

/* Numeric values match Blizzard s2client-proto/raw.proto. */
typedef enum {
    WC3_SC2_DISPLAY_VISIBLE = 1,
    WC3_SC2_DISPLAY_SNAPSHOT = 2,
    WC3_SC2_DISPLAY_HIDDEN = 3,
    WC3_SC2_DISPLAY_PLACEHOLDER = 4,
} wc3Sc2DisplayType_t;

typedef enum {
    WC3_SC2_ALLIANCE_SELF = 1,
    WC3_SC2_ALLIANCE_ALLY = 2,
    WC3_SC2_ALLIANCE_NEUTRAL = 3,
    WC3_SC2_ALLIANCE_ENEMY = 4,
} wc3Sc2Alliance_t;

typedef enum {
    WC3_SC2_CLOAK_UNKNOWN = 0,
    WC3_SC2_CLOAKED = 1,
    WC3_SC2_CLOAKED_DETECTED = 2,
    WC3_SC2_NOT_CLOAKED = 3,
    WC3_SC2_CLOAKED_ALLIED = 4,
} wc3Sc2CloakState_t;

typedef struct {
    uint64_t tag;
    FLOAT health, health_max;
    FLOAT energy, energy_max;
    DWORD unit_type;
} wc3Sc2PassengerUnit_t;

typedef struct {
    VECTOR3 point;
    BOOL has_tag;
    uint64_t tag;
} wc3Sc2RallyTarget_t;

/* Subset of SC2APIProtocol.Unit represented without protobuf ownership. Fields
 * guarded by has_live_data must be omitted by serializers for fog snapshots. */
typedef struct {
    wc3Sc2DisplayType_t display_type;
    wc3Sc2Alliance_t alliance;
    uint64_t tag;
    DWORD unit_type;
    LONG owner;
    VECTOR3 pos;
    FLOAT facing;
    FLOAT radius;
    BOOL has_build_progress;
    FLOAT build_progress;
    wc3Sc2CloakState_t cloak;
    BOOL is_selected;

    BOOL has_live_data;
    FLOAT health;
    FLOAT health_max;
    FLOAT energy;
    FLOAT energy_max;
    LONG mineral_contents; /* Warcraft lumber; normally zero for unit entities. */
    LONG vespene_contents; /* Warcraft gold; used by Gold Mine resource entities. */
    BOOL is_flying;
    BOOL is_active;
    BOOL has_attack_upgrade_level;
    LONG attack_upgrade_level;
    BOOL has_armor_upgrade_level;
    LONG armor_upgrade_level;
    DWORD buff_ids[MAX_UNIT_STATUSES];
    DWORD buff_count;
    wc3Sc2PassengerUnit_t passengers[MAX_CARGO];
    DWORD passenger_count;
    LONG cargo_space_taken;
    LONG cargo_space_max;
    wc3Sc2RallyTarget_t rally_targets[1];
    DWORD rally_target_count;

    struct {
        DWORD ability_id;
        BOOL has_point;
        VECTOR2 point;
        BOOL has_target_unit;
        uint64_t target_unit_tag;
    } orders[MAX_UNIT_ORDER_QUEUE + 1];
    DWORD order_count;
} wc3Sc2RawUnit_t;

typedef enum {
    WC3_SC2_TARGET_NONE,
    WC3_SC2_TARGET_POINT,
    WC3_SC2_TARGET_UNIT,
} wc3Sc2RawTargetType_t;

/* One resolved unit from SC2APIProtocol.ActionRawUnitCommand. The protobuf
 * adapter may receive several unit_tags in one Action, but batching/aggregation
 * belongs to that adapter because ResponseAction.result is per Action. Keeping
 * this game-owned primitive single-unit avoids inventing wire semantics here. */
typedef struct {
    DWORD ability_id; /* Warcraft order id exposed through SC2 ability_id. */
    uint64_t unit_tag;
    BOOL queue_command;
    wc3Sc2RawTargetType_t target_type;
    VECTOR2 target_point;
    uint64_t target_unit_tag;
} wc3Sc2RawUnitCommand_t;

/* Numeric values are the matching Blizzard ActionResult entries. */
typedef enum {
    WC3_SC2_ACTION_SUCCESS = 1,
    WC3_SC2_ACTION_NOT_SUPPORTED = 2,
    WC3_SC2_ACTION_ERROR = 3,
    WC3_SC2_ACTION_CANT_QUEUE = 4,
    WC3_SC2_ACTION_NOT_ENOUGH_MINERALS = 9,  /* Warcraft lumber. */
    WC3_SC2_ACTION_NOT_ENOUGH_VESPENE = 10,  /* Warcraft gold. */
    WC3_SC2_ACTION_NOT_ENOUGH_FOOD = 13,
    WC3_SC2_ACTION_NOT_ENOUGH_ENERGY = 17,
    WC3_SC2_ACTION_CANT_TARGET = 36,
    WC3_SC2_ACTION_BUILD_TECH_REQUIREMENTS = 40,
    WC3_SC2_ACTION_CANT_FIND_PLACEMENT = 41,
    WC3_SC2_ACTION_CANT_BUILD_ON_THAT = 42,
    WC3_SC2_ACTION_CANT_BUILD_LOCATION_INVALID = 44,
    WC3_SC2_ACTION_CANT_BUILD_TOO_CLOSE_TO_RESOURCES = 47,
    WC3_SC2_ACTION_MUST_TARGET_VISIBLE_UNIT = 83,
    WC3_SC2_ACTION_CANT_CONTROL_UNIT = 87,
} wc3Sc2ActionResult_t;

BOOL WC3_SC2API_FillRawUnit(DWORD player, LPCEDICT ent, wc3Sc2RawUnit_t *out);
DWORD WC3_SC2API_BuildRawUnits(DWORD player, wc3Sc2RawUnit_t *out, DWORD max_out);
wc3Sc2ActionResult_t WC3_SC2API_IssueRawUnitCommand(DWORD player,
                                                     wc3Sc2RawUnitCommand_t const *command);
wc3Sc2ActionResult_t WC3_SC2API_ToggleRawAutocast(DWORD player, DWORD ability_id,
                                                   uint64_t const *unit_tags, DWORD unit_count);

#endif
