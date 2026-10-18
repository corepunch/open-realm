#ifndef WC3_SC2API_GAME_H
#define WC3_SC2API_GAME_H

#include "sc2api_map.h"
#include "sc2api_raw.h"

/* Numeric values match SC2APIProtocol.PlayerType. Warcraft neutral/rescuable
 * owners are simulation owners, not SC2API participants, and are omitted from
 * ResponseGameInfo.player_info. */
typedef enum {
    WC3_SC2_PLAYER_PARTICIPANT = 1,
    WC3_SC2_PLAYER_COMPUTER = 2,
    WC3_SC2_PLAYER_OBSERVER = 3,
} wc3Sc2PlayerType_t;

typedef struct {
    uint32_t player_id; /*one-based Warcraft player number */
    wc3Sc2PlayerType_t type;
    wc3Sc2Race_t race_requested;
    wc3Sc2Race_t race_actual;
    cstring_t player_name;
} wc3Sc2PlayerInfo_t;

typedef struct {
    cstring_t map_name;
    cstring_t local_map_path;
    wc3Sc2PlayerInfo_t players[MAX_PLAYERS];
    uint32_t player_count;
} wc3Sc2GameInfo_t;

/* Numeric values match SC2APIProtocol.Result. */
typedef enum {
    WC3_SC2_RESULT_VICTORY = 1,
    WC3_SC2_RESULT_DEFEAT = 2,
    WC3_SC2_RESULT_TIE = 3,
    WC3_SC2_RESULT_UNDECIDED = 4,
} wc3Sc2Result_t;

typedef struct {
    uint32_t player_id;
    wc3Sc2Result_t result;
} wc3Sc2PlayerResult_t;

/* Protocol-independent subset of ResponseObservation/Observation. The caller
 * owns raw_units storage so the game adapter never introduces protobuf memory
 * ownership into simulation code. */
typedef struct {
    uint32_t game_loop;
    wc3Sc2PlayerCommon_t player_common;
    uint32_t upgrade_ids[MAX_PLAYER_TECH_STATE];
    uint32_t upgrade_count;
    uint32_t raw_unit_count;
    bool has_map_state;
    wc3Sc2MapState_t map_state;
} wc3Sc2Observation_t;

bool WC3_SC2API_FillGameInfo(wc3Sc2GameInfo_t *out);
bool WC3_SC2API_FillPlayerResult(uint32_t player, wc3Sc2PlayerResult_t *out);
bool WC3_SC2API_BuildObservation(uint32_t player, wc3Sc2Observation_t *out,
                                 wc3Sc2RawUnit_t *raw_units, uint32_t max_raw_units);
bool WC3_SC2API_BuildObservationWithMapState(uint32_t player, wc3Sc2Observation_t *out,
                                             wc3Sc2RawUnit_t *raw_units, uint32_t max_raw_units,
                                             uint8_t *visibility_data, uint32_t visibility_capacity,
                                             uint8_t *creep_data, uint32_t creep_capacity);

#endif
