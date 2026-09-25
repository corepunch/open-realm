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
    DWORD player_id; /* one-based Warcraft player number */
    wc3Sc2PlayerType_t type;
    wc3Sc2Race_t race_requested;
    wc3Sc2Race_t race_actual;
    LPCSTR player_name;
} wc3Sc2PlayerInfo_t;

typedef struct {
    LPCSTR map_name;
    LPCSTR local_map_path;
    wc3Sc2PlayerInfo_t players[MAX_PLAYERS];
    DWORD player_count;
} wc3Sc2GameInfo_t;

/* Numeric values match SC2APIProtocol.Result. */
typedef enum {
    WC3_SC2_RESULT_VICTORY = 1,
    WC3_SC2_RESULT_DEFEAT = 2,
    WC3_SC2_RESULT_TIE = 3,
    WC3_SC2_RESULT_UNDECIDED = 4,
} wc3Sc2Result_t;

typedef struct {
    DWORD player_id;
    wc3Sc2Result_t result;
} wc3Sc2PlayerResult_t;

/* Protocol-independent subset of ResponseObservation/Observation. The caller
 * owns raw_units storage so the game adapter never introduces protobuf memory
 * ownership into simulation code. */
typedef struct {
    DWORD game_loop;
    wc3Sc2PlayerCommon_t player_common;
    DWORD upgrade_ids[MAX_PLAYER_TECH_STATE];
    DWORD upgrade_count;
    DWORD raw_unit_count;
    BOOL has_map_state;
    wc3Sc2MapState_t map_state;
} wc3Sc2Observation_t;

BOOL WC3_SC2API_FillGameInfo(wc3Sc2GameInfo_t *out);
BOOL WC3_SC2API_FillPlayerResult(DWORD player, wc3Sc2PlayerResult_t *out);
BOOL WC3_SC2API_BuildObservation(DWORD player, wc3Sc2Observation_t *out,
                                 wc3Sc2RawUnit_t *raw_units, DWORD max_raw_units);
BOOL WC3_SC2API_BuildObservationWithMapState(DWORD player, wc3Sc2Observation_t *out,
                                             wc3Sc2RawUnit_t *raw_units, DWORD max_raw_units,
                                             LPBYTE visibility_data, DWORD visibility_capacity,
                                             LPBYTE creep_data, DWORD creep_capacity);

#endif
