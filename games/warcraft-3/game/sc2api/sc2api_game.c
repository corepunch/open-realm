#ifdef WC3_SC2API

#include "sc2api_game.h"

static BOOL sc2api_player_type(playerType_t type, wc3Sc2PlayerType_t *out) {
    if (!out) return false;
    switch (type) {
        case kPlayerTypeHuman:
            *out = WC3_SC2_PLAYER_PARTICIPANT;
            return true;
        case kPlayerTypeComputer:
            *out = WC3_SC2_PLAYER_COMPUTER;
            return true;
        default:
            return false;
    }
}

BOOL WC3_SC2API_FillGameInfo(wc3Sc2GameInfo_t *out) {
    if (!out || !level.mapinfo) return false;
    memset(out, 0, sizeof(*out));
    out->map_name = level.setup.name[0] ? level.setup.name : G_LevelString(level.mapinfo->mapName);
    out->local_map_path = level.map_path;

    FOR_LOOP(player, MAX_PLAYERS) {
        LPCMAPPLAYER map_player = &level.mapinfo->players[player];
        wc3Sc2PlayerInfo_t *info;
        wc3Sc2PlayerType_t type;
        LPGAMECLIENT client;

        if (!map_player->used || !sc2api_player_type(map_player->playerType, &type)) continue;
        if (out->player_count >= MAX_PLAYERS) break;
        info = &out->players[out->player_count++];
        client = WC3_SC2API_PlayerClient(player);
        info->player_id = player + 1;
        info->type = type;
        info->race_requested = WC3_SC2API_RequestedRaceFromPlayerRace(map_player->playerRace);
        info->race_actual = WC3_SC2API_RaceFromPlayerRace(client ? client->ps.race : map_player->playerRace);
        info->player_name = client && client->jass.name[0]
            ? client->jass.name
            : G_LevelString(map_player->playerName);
    }
    return true;
}

BOOL WC3_SC2API_FillPlayerResult(DWORD player, wc3Sc2PlayerResult_t *out) {
    LPGAMECLIENT client;
    DWORD wc3_result;

    if (!out || !(client = WC3_SC2API_PlayerClient(player)) || !client->jass.removed) return false;
    wc3_result = client->ps.stats[PLAYERSTATE_GAME_RESULT];
    memset(out, 0, sizeof(*out));
    out->player_id = player + 1;
    switch (wc3_result) {
        case 0: out->result = WC3_SC2_RESULT_VICTORY; break;
        case 1: out->result = WC3_SC2_RESULT_DEFEAT; break;
        case 2: out->result = WC3_SC2_RESULT_TIE; break;
        default: out->result = WC3_SC2_RESULT_UNDECIDED; break;
    }
    return true;
}

BOOL WC3_SC2API_BuildObservation(DWORD player, wc3Sc2Observation_t *out,
                                 wc3Sc2RawUnit_t *raw_units, DWORD max_raw_units) {
    LPGAMECLIENT client;

    if (!out || (!raw_units && max_raw_units)) return false;
    memset(out, 0, sizeof(*out));
    if (!WC3_SC2API_FillPlayerCommon(player, &out->player_common) ||
        !(client = WC3_SC2API_PlayerClient(player))) return false;
    out->game_loop = level.framenum;
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        playerTechState_t const *tech = &client->tech[i];
        if (!tech->id || tech->researched <= 0 || out->upgrade_count >= MAX_PLAYER_TECH_STATE) continue;
        out->upgrade_ids[out->upgrade_count++] = tech->id;
    }
    out->raw_unit_count = WC3_SC2API_BuildRawUnits(player, raw_units, max_raw_units);
    return true;
}

BOOL WC3_SC2API_BuildObservationWithMapState(DWORD player, wc3Sc2Observation_t *out,
                                             wc3Sc2RawUnit_t *raw_units, DWORD max_raw_units,
                                             LPBYTE visibility_data, DWORD visibility_capacity,
                                             LPBYTE creep_data, DWORD creep_capacity) {
    if (!WC3_SC2API_BuildObservation(player, out, raw_units, max_raw_units)) return false;
    if (!WC3_SC2API_FillMapState(player, visibility_data, visibility_capacity,
                                 creep_data, creep_capacity, &out->map_state)) {
        return false;
    }
    out->has_map_state = out->map_state.has_visibility || out->map_state.has_creep;
    return true;
}

#endif
