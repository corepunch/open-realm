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
    float health, health_max;
    float energy, energy_max;
    uint32_t unit_type;
} wc3Sc2PassengerUnit_t;

typedef struct {
    vec3_t point;
    bool has_tag;
    uint64_t tag;
} wc3Sc2RallyTarget_t;

/* Subset of SC2APIProtocol.Unit represented without protobuf ownership. Fields
 * guarded by has_live_data must be omitted by serializers for fog snapshots. */
typedef struct {
    wc3Sc2DisplayType_t display_type;
    wc3Sc2Alliance_t alliance;
    uint64_t tag;
    uint32_t unit_type;
    int32_t owner;
    vec3_t pos;
    float facing;
    float radius;
    bool has_build_progress;
    float build_progress;
    wc3Sc2CloakState_t cloak;
    bool is_selected;

    bool has_live_data;
    float health;
    float health_max;
    float energy;
    float energy_max;
    int32_t mineral_contents; /* Warcraft lumber; normally zero for unit entities. */
    int32_t vespene_contents; /* Warcraft gold; used by Gold Mine resource entities. */
    bool is_flying;
    bool is_active;
    bool has_attack_upgrade_level;
    int32_t attack_upgrade_level;
    bool has_armor_upgrade_level;
    int32_t armor_upgrade_level;
    uint32_t buff_ids[MAX_UNIT_STATUSES];
    uint32_t buff_count;
    wc3Sc2PassengerUnit_t passengers[MAX_CARGO];
    uint32_t passenger_count;
    int32_t cargo_space_taken;
    int32_t cargo_space_max;
    wc3Sc2RallyTarget_t rally_targets[1];
    uint32_t rally_target_count;

    struct {
        uint32_t ability_id;
        float progress;
        bool has_point;
        vec2_t point;
        bool has_target_unit;
        uint64_t target_unit_tag;
    } orders[MAX_UNIT_ORDER_QUEUE + 1];
    uint32_t order_count;
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
    uint32_t ability_id; /* Warcraft order id exposed through SC2 ability_id. */
    uint64_t unit_tag;
    bool queue_command;
    wc3Sc2RawTargetType_t target_type;
    vec2_t target_point;
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
    WC3_SC2_ACTION_CANT_SEE_BUILD_LOCATION = 45,
    WC3_SC2_ACTION_CANT_BUILD_TOO_CLOSE_TO_RESOURCES = 47,
    WC3_SC2_ACTION_MUST_TARGET_VISIBLE_UNIT = 83,
    WC3_SC2_ACTION_CANT_CONTROL_UNIT = 87,
} wc3Sc2ActionResult_t;

bool WC3_SC2API_FillRawUnit(uint32_t player, edict_t const *ent, wc3Sc2RawUnit_t *out);
uint32_t WC3_SC2API_BuildRawUnits(uint32_t player, wc3Sc2RawUnit_t *out, uint32_t max_out);
wc3Sc2ActionResult_t WC3_SC2API_IssueRawUnitCommand(uint32_t player,
                                                     wc3Sc2RawUnitCommand_t const *command);
wc3Sc2ActionResult_t WC3_SC2API_ToggleRawAutocast(uint32_t player, uint32_t ability_id,
                                                   uint64_t const *unit_tags, uint32_t unit_count);

bool WC3_SC2API_PlacementIsVisible(uint32_t player, uint32_t building, vec2_t const *point);
void WC3_SC2API_ResetFogMemory(void);
uint32_t WC3_SC2API_FogMemoryCount(uint32_t player);
void WC3_SC2API_CaptureFogMemory(uint32_t player);

#endif
