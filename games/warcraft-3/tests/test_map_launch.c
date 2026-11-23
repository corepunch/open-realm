/*
 * test_map_launch.c — whole-map launches through SV_Map in the engine binary.
 *
 * Each launch runs the real lobby resolver (SV_ApplyLobbySettings), the map's
 * config() and main(). The fixture maps come from tools/wc3fixturegen.py: W3I
 * players 0-3 are User slots, player 4 is Neutral, and config() sets User for
 * players 0-3 and Creep for player 4. main() asserts each player's controller
 * and slot state with BJassAssert and then runs MeleeStartingAI.
 *
 * The process_ suite prefix keeps these out of `*` runs: a launch replaces the
 * game state the in-engine wc3_* tests share. `make test-wc3-map-launch` runs
 * each launch in its own process.
 */
#include "server/server.h"
#include "shared/test.h"

bool G_TestLaunchedPlayer(uint32_t number, uint32_t *controller, bool *melee_ai);

enum { LAUNCH_USER = 0, LAUNCH_COMPUTER = 1, LAUNCH_CREEP = 4, LAUNCH_NONE = 5 };

static void expect_launched_player(uint32_t number, uint32_t controller, bool melee_ai) {
    uint32_t actual_controller = ~0u;
    bool actual_ai = !melee_ai;

    T_ASSERT(G_TestLaunchedPlayer(number, &actual_controller, &actual_ai));
    T_EQ(actual_controller, controller);
    T_EQ(actual_ai, melee_ai);
}

TEST(process_map_launch, lobby_slots_reach_main_and_only_computer_starts_ai) {
    static lobbySlotType_t const types[] = {
        LOBBY_SLOT_HUMAN, LOBBY_SLOT_COMPUTER, LOBBY_SLOT_CLOSED, LOBBY_SLOT_OPEN,
    };
    cstring_t map = "Maps/Launch-lobby.w3m";

    /* A single-player custom game goes through the local lobby. */
    T_ASSERT(SV_StartLocalLobby(map));
    T_EQ(sv.state, ss_lobby);
    SV_LobbySetConfig(2, 4, "Launch");
    for (uint32_t i = 0; i < 4; i++) {
        lobbySlot_t slot = { .visible = true, .client = MAX_CLIENTS, .map_player = i,
                             .type = types[i], .race = kPlayerRaceHuman, .team = 0, .color = i };
        SV_LobbySetSlot(i, &slot);
    }
    /* The dedicated host is the occupied Human slot; Open stays unoccupied. */
    T_ASSERT(svs.lobby.slots[0].occupied);
    T_ASSERT(!svs.lobby.slots[3].occupied);

    SV_Map(map);

    T_EQ(sv.state, ss_game);
    expect_launched_player(0, LAUNCH_USER, false);
    expect_launched_player(1, LAUNCH_COMPUTER, true);
    expect_launched_player(2, LAUNCH_NONE, false);
    expect_launched_player(3, LAUNCH_NONE, false);
    expect_launched_player(4, LAUNCH_CREEP, false);
}

TEST(process_map_launch, direct_launch_keeps_config_controllers_in_main) {
    cstring_t map = "Maps/Launch-direct.w3m";

    T_ASSERT(!svs.lobby.active);
    SV_Map(map);

    T_EQ(sv.state, ss_game);
    for (uint32_t i = 0; i < 4; i++) expect_launched_player(i, LAUNCH_USER, false);
    expect_launched_player(4, LAUNCH_CREEP, false);
}
