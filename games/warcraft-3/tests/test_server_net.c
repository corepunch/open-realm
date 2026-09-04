#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>

#include "test.h"

#include "common/shared.h"
#include "../../../common/net.h"
#include "../../../client/client.h"
#include "server/server.h"

void test_client_stubs_init(void);
void test_client_stubs_clear_cvars(void);
void test_client_stubs_set_cvar(LPCSTR name, LPCSTR value);
struct game_import gi;

/* External symbols referenced by sv_init.c but unused in these tests. */
void SV_InitGameProgs(void) {}
void SV_ClearWorld(void) {}
bool CM_LoadMap(LPCSTR mapFilename) { (void)mapFilename; return true; }
DWORD CM_GetMapChecksum(void) { return 0x1234; }
LPDOODAD CM_GetDoodads(void) { return NULL; }
static LPMAPINFO test_mapinfo;
LPCMAPINFO CM_GetMapInfo(void) { return test_mapinfo; }
FLOAT CM_GetHeightAtPoint(FLOAT x, FLOAT y) { (void)x; (void)y; return 0.0f; }
VECTOR2 CM_GetNormalizedMapPosition(FLOAT x, FLOAT y) { return (VECTOR2){ x, y }; }
VECTOR2 CM_GetDenormalizedMapPosition(FLOAT x, FLOAT y) { return (VECTOR2){ x, y }; }
BOX2 CM_GetWorldBounds(void) { return (BOX2){ .min = {0,0}, .max = {TILE_SIZE * 4.0f, TILE_SIZE * 3.0f} }; }
HANDLE FS_FindFirstFile(LPCSTR mask, SFILE_FIND_DATA *findData) {
    (void)mask;
    (void)findData;
    return NULL;
}
BOOL FS_FindNextFile(HANDLE find, SFILE_FIND_DATA *findData) {
    (void)find;
    (void)findData;
    return false;
}
BOOL FS_FindClose(HANDLE find) {
    (void)find;
    return true;
}

static void test_run_frame(void) {
}

static LPCSTR test_theme_value(LPCSTR filename) {
    return filename;
}

static HANDLE test_mem_alloc(long size) {
    return MemAlloc(size);
}

static void test_mem_free(HANDLE mem) {
    MemFree(mem);
}

static int test_model_index(LPCSTR name) {
    (void)name;
    return 0;
}

static void test_customize_entity(DWORD player, LPCEDICT ent, LPENTITYSTATE state) {
    (void)player; (void)ent; (void)state;
}

static int test_image_index(LPCSTR name) {
    (void)name;
    return 0;
}

static int test_font_index(LPCSTR name, DWORD fontSize) {
    (void)name;
    (void)fontSize;
    return 0;
}

static void reset_test_gi(void) {
    memset(&gi, 0, sizeof(gi));
    gi.MemAlloc = test_mem_alloc;
    gi.MemFree = test_mem_free;
    gi.ModelIndex = test_model_index;
    gi.ImageIndex = test_image_index;
    gi.FontIndex = test_font_index;
    gi.ClearWorld = SV_ClearWorld;
    gi.ApplyLobbySettings = SV_ApplyLobbySettings;
}

TEST(server_net, pause_publishes_client_render_state) {
    test_client_stubs_clear_cvars();
    memset(&sv, 0, sizeof(sv)); memset(&svs, 0, sizeof(svs));
    SV_SetPaused(true);
    T_ASSERT(sv.paused); T_EQ(Cvar_Integer("paused", 0), 1);
    SV_SetPaused(false);
    T_ASSERT(!sv.paused); T_EQ(Cvar_Integer("paused", 1), 0);
}

void SV_ExecuteUserCommand(LPSIZEBUF msg, LPCLIENT client) { (void)msg; (void)client; }
void SV_HandleUnitUIRequest(LPCLIENT client, LPSIZEBUF msg) { (void)client; (void)msg; }

static struct game_export test_ge;
static edict_t test_edicts[MAX_CLIENT_ENTITIES];
static DWORD test_game_shutdowns;
static DWORD test_camera_calls;
static LPEDICT test_camera_ent;
static VECTOR2 test_camera_pos;

static void test_set_camera(LPEDICT ent, LPCVECTOR2 position) {
    test_camera_calls++; test_camera_ent = ent; test_camera_pos = *position;
}

static void test_spawn_entities(void);

static bool test_load_map(LPCSTR mapFilename) {
    if (!CM_LoadMap(mapFilename)) {
        return false;
    }
    SV_ApplyLobbySettings((LPMAPINFO)CM_GetMapInfo());
    SV_ClearWorld();
    test_spawn_entities();
    return true;
}

static void test_spawn_entities(void) {
}

static void test_game_shutdown(void) {
    test_game_shutdowns++;
}

static void reset_server_state(int max_players) {
    memset(&sv, 0, sizeof(sv));
    memset(&svs, 0, sizeof(svs));
    memset(&test_ge, 0, sizeof(test_ge));
    memset(test_edicts, 0, sizeof(test_edicts));
    test_game_shutdowns = 0;
    test_mapinfo = NULL;
    SZ_Init(&sv.multicast, sv.multicast_buf, sizeof(sv.multicast_buf));
    test_ge.max_clients = max_players;
    test_ge.max_edicts = MAX_CLIENT_ENTITIES;
    test_ge.edict_size = sizeof(edict_t);
    test_ge.edicts = test_edicts;
    test_ge.num_edicts = max_players;
    test_ge.RunFrame = test_run_frame;
    test_ge.ClientSetCameraPosition = test_set_camera;
    test_ge.GetThemeValue = test_theme_value;
    test_ge.LoadMap = test_load_map;
    test_ge.GetWorldBounds = CM_GetWorldBounds;
    test_ge.Shutdown = test_game_shutdown;
    test_ge.CustomizeEntity = test_customize_entity;
    ge = &test_ge;
    reset_test_gi();
}

TEST(server_net, camera_packet_waits_for_spawned_client_edict) {
    BYTE data[16];
    sizeBuf_t msg = { data, sizeof(data), 0, 0 };
    LPCLIENT client;

    reset_server_state(1);
    client = &svs.clients[0]; client->state = cs_connected;
    test_camera_calls = 0; test_camera_ent = NULL; test_camera_pos = MAKE(VECTOR2, 0, 0);
    MSG_WriteByte(&msg, clc_camera_position); MSG_WriteFloat(&msg, 12.0f); MSG_WriteFloat(&msg, -34.0f);
    SV_ParseClientMessage(&msg, client);
    T_EQ(test_camera_calls, 0);

    SZ_Clear(&msg); client->state = cs_spawned; client->edict = &test_edicts[0];
    MSG_WriteByte(&msg, clc_camera_position); MSG_WriteFloat(&msg, 12.0f); MSG_WriteFloat(&msg, -34.0f);
    msg.readcount = 0;
    SV_ParseClientMessage(&msg, client);
    T_EQ(test_camera_calls, 1); T_ASSERT(test_camera_ent == &test_edicts[0]);
    T_FEQ(test_camera_pos.x, 12.0f, 0.001f); T_FEQ(test_camera_pos.y, -34.0f, 0.001f);
}

static int open_client_socket(void) {
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0)
        return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s);
        return -1;
    }
    return s;
}

static BOOL bind_server_socket(unsigned short port) {
    char text[16];

    snprintf(text, sizeof(text), "%u", (unsigned)port);
    test_client_stubs_set_cvar("game_port", text);
    NET_Config(false);
    NET_Config(true);
    return NET_IsConfigured(NS_SERVER);
}

static void send_connect_oob(int sock, unsigned short server_port) {
    enum {
        MAX_CONNECT_DATAGRAM_SIZE = 64,
        OOB_HEADER_SIZE = 4,
        CONNECT_TEXT_SIZE = 7
    };
    BYTE datagram[MAX_CONNECT_DATAGRAM_SIZE];
    DWORD msg_len = OOB_HEADER_SIZE + CONNECT_TEXT_SIZE;
    int oob_marker = -1;
    memcpy(datagram, &oob_marker, sizeof(oob_marker));
    memcpy(datagram + 4, "connect", 7);

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons(server_port);

    (void)sendto(sock, datagram, msg_len, 0, (struct sockaddr *)&to, sizeof(to));
}

static void send_info_oob(int sock, unsigned short server_port) {
    enum {
        MAX_INFO_DATAGRAM_SIZE = 64,
        OOB_HEADER_SIZE = 4,
        INFO_TEXT_SIZE = 4
    };
    BYTE datagram[MAX_INFO_DATAGRAM_SIZE];
    DWORD msg_len = OOB_HEADER_SIZE + INFO_TEXT_SIZE;
    int oob_marker = -1;
    memcpy(datagram, &oob_marker, sizeof(oob_marker));
    memcpy(datagram + 4, "info", 4);

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons(server_port);

    (void)sendto(sock, datagram, msg_len, 0, (struct sockaddr *)&to, sizeof(to));
}

static BOOL recv_client_connect_oob(int sock) {
    enum {
        MAX_RECV_RETRIES = 40,
        RECV_POLL_DELAY_US = 5000
    };
    BYTE datagram[128];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    FOR_LOOP(i, MAX_RECV_RETRIES) {
        int r = recvfrom(sock, datagram, sizeof(datagram), 0, (struct sockaddr *)&from, &fromlen);
        if (r > 0) {
            if (r >= 4 + 14 && memcmp(datagram + 4, "client_connect", 14) == 0)
                return true;
            return false;
        }
        usleep(RECV_POLL_DELAY_US);
    }
    return false;
}

static void pump_server_connects(void) {
    enum {
        MAX_PACKETS_PER_PUMP = 64,
        MAX_EMPTY_POLLS = 40,
        RECV_POLL_DELAY_US = 5000,
        MIN_CONNECT_MSG_SIZE = 11
    };
    BYTE msg_buf[MAX_MSGLEN];
    sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t from;
    DWORD empty_polls = 0;
    int r;

    for (DWORD packets = 0; packets < MAX_PACKETS_PER_PUMP && empty_polls < MAX_EMPTY_POLLS;) {
        r = NET_GetPacket(NS_SERVER, &from, &msg);
        if (!r) {
            empty_polls++;
            usleep(RECV_POLL_DELAY_US);
            continue;
        }
        empty_polls = 0;
        packets++;
        if (r >= MIN_CONNECT_MSG_SIZE) {
            int hdr = 0;
            memcpy(&hdr, msg.data, sizeof(hdr));
            if (hdr == -1 && memcmp(msg.data + 4, "connect", 7) == 0)
                SV_DirectConnect(&from, "");
        }
    }
}

static void pump_server_connectionless(void) {
    enum {
        MAX_PACKETS_PER_PUMP = 64,
        MAX_EMPTY_POLLS = 40,
        RECV_POLL_DELAY_US = 5000,
    };
    BYTE msg_buf[MAX_MSGLEN];
    sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t from;
    DWORD empty_polls = 0;
    int r;

    for (DWORD packets = 0; packets < MAX_PACKETS_PER_PUMP && empty_polls < MAX_EMPTY_POLLS;) {
        r = NET_GetPacket(NS_SERVER, &from, &msg);
        if (!r) {
            empty_polls++;
            usleep(RECV_POLL_DELAY_US);
            continue;
        }
        empty_polls = 0;
        packets++;
        SV_ConnectionlessPacket(&from, &msg);
    }
}

static BOOL recv_info_oob(int sock, LPSTR out, DWORD out_size) {
    enum {
        MAX_RECV_RETRIES = 40,
        RECV_POLL_DELAY_US = 5000
    };
    BYTE datagram[512];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    if (out && out_size > 0) {
        out[0] = '\0';
    }
    FOR_LOOP(i, MAX_RECV_RETRIES) {
        int r = recvfrom(sock, datagram, sizeof(datagram), 0, (struct sockaddr *)&from, &fromlen);
        if (r > 4) {
            DWORD len = MIN((DWORD)(r - 4), out_size ? out_size - 1 : 0);
            if (out && out_size > 0) {
                memcpy(out, datagram + 4, len);
                out[len] = '\0';
            }
            return memcmp(datagram + 4, "info", 4) == 0;
        }
        usleep(RECV_POLL_DELAY_US);
    }
    return false;
}

static void drain_client_packets(void) {
    BYTE msg_buf[MAX_MSGLEN];
    sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t from;

    while (NET_GetPacket(NS_CLIENT, &from, &msg)) {
    }
}

TEST(server_net, runtime_configstring_change_marks_value_for_reliable_resync) {
    DWORD const index = CS_GENERAL + 7;

    reset_server_state(1);
    sv.syncstrings[index] = true;
    SV_SetConfigString(index, "LateRuntimeName", sizeof("LateRuntimeName"));

    T_STREQ(sv.configstrings[index], "LateRuntimeName");
    T_ASSERT(!sv.syncstrings[index]);
}

TEST(server_net, findindex_new_slot_marks_value_for_reliable_resync) {
    int first, second;

    reset_server_state(1);
    first = SV_FontIndex("ReviewFont", 12);
    T_ASSERT(first > 0);
    sv.syncstrings[CS_FONTS + first] = true;
    second = SV_FontIndex("ReviewFontBold", 13);

    T_EQ(second, first + 1);
    T_STREQ(sv.configstrings[CS_FONTS + second], "ReviewFontBold,13");
    T_ASSERT(!sv.syncstrings[CS_FONTS + second]);
}

TEST(server_net, udp_multi_client_connects_register_distinct_slots) {
    int c1 = open_client_socket();
    int c2 = open_client_socket();
    T_ASSERT(c1 >= 0 && c2 >= 0);
    T_ASSERT(bind_server_socket(PORT_SERVER + 9));
    reset_server_state(8);

    send_connect_oob(c1, PORT_SERVER + 9);
    send_connect_oob(c2, PORT_SERVER + 9);
    pump_server_connects();

    T_EQ(svs.num_clients, 2);
    T_EQ(svs.clients[0].state, cs_connected);
    T_EQ(svs.clients[1].state, cs_connected);
    T_ASSERT(recv_client_connect_oob(c1));
    T_ASSERT(recv_client_connect_oob(c2));

    if (c1 >= 0) close(c1);
    if (c2 >= 0) close(c2);
    NET_Shutdown();
}

TEST(server_net, udp_connect_honors_ge_max_clients_limit) {
    int c1 = open_client_socket();
    int c2 = open_client_socket();
    int c3 = open_client_socket();
    T_ASSERT(c1 >= 0 && c2 >= 0 && c3 >= 0);
    T_ASSERT(bind_server_socket(PORT_SERVER + 10));
    reset_server_state(2);

    send_connect_oob(c1, PORT_SERVER + 10);
    send_connect_oob(c2, PORT_SERVER + 10);
    send_connect_oob(c3, PORT_SERVER + 10);
    pump_server_connects();

    T_EQ(svs.num_clients, 2);
    T_ASSERT(recv_client_connect_oob(c1));
    T_ASSERT(recv_client_connect_oob(c2));
    T_ASSERT(!recv_client_connect_oob(c3));

    if (c1 >= 0) close(c1);
    if (c2 >= 0) close(c2);
    if (c3 >= 0) close(c3);
    NET_Shutdown();
}

TEST(server_net, lan_info_query_returns_discoverable_server_metadata) {
    int c1 = open_client_socket();
    char info[512];
    T_ASSERT(c1 >= 0);
    T_ASSERT(bind_server_socket(PORT_SERVER + 11));
    reset_server_state(8);
    sv.state = ss_game;
    snprintf(sv.configstrings[CS_WORLD], sizeof(sv.configstrings[CS_WORLD]),
             "Maps\\Melee\\TwinRivers.w3m");
    svs.num_clients = 1;
    svs.clients[0].state = cs_spawned;

    send_info_oob(c1, PORT_SERVER + 11);
    pump_server_connectionless();

    T_ASSERT(recv_info_oob(c1, info, sizeof(info)));
    T_ASSERT(strstr(info, "\\hostname\\OpenWarcraft3") != NULL);
    T_ASSERT(strstr(info, "\\mapname\\Maps/Melee/TwinRivers.w3m") != NULL);
    T_ASSERT(strstr(info, "\\players\\1") != NULL);
    T_ASSERT(strstr(info, "\\maxplayers\\8") != NULL);
    T_ASSERT(strstr(info, "\\speed\\2") != NULL);

    if (c1 >= 0) close(c1);
    NET_Shutdown();
}

TEST(server_net, lan_info_query_returns_lobby_metadata) {
    int c1 = open_client_socket();
    char info[512];
    T_ASSERT(c1 >= 0);
    T_ASSERT(bind_server_socket(PORT_SERVER + 12));
    reset_server_state(8);
    sv.state = ss_lobby;
    snprintf(sv.configstrings[CS_WORLD], sizeof(sv.configstrings[CS_WORLD]),
             "Maps\\Melee\\TwinRivers.w3m");
    svs.num_clients = 1;
    svs.clients[0].state = cs_connected;

    send_info_oob(c1, PORT_SERVER + 12);
    pump_server_connectionless();

    T_ASSERT(recv_info_oob(c1, info, sizeof(info)));
    T_ASSERT(strstr(info, "\\mapname\\Maps/Melee/TwinRivers.w3m") != NULL);
    T_ASSERT(strstr(info, "\\players\\1") != NULL);

    if (c1 >= 0) close(c1);
    NET_Shutdown();
}

TEST(server_net, lobby_team_selection_expands_map_forces) {
    MAPINFO info;
    lobbySlot_t slot;

    reset_server_state(4);
    memset(&info, 0, sizeof(info));
    info.num_teams = 1;
    info.teams = MemAlloc(sizeof(*info.teams));
    memset(info.teams, 0, sizeof(*info.teams));
    info.teams[0].playerMasks = 0x0f;
    FOR_LOOP(i, 4) {
        info.players[i].used = true;
        info.players[i].playerType = kPlayerTypeHuman;
        info.players[i].playerRace = kPlayerRaceHuman;
    }
    test_mapinfo = &info;
    sv.state = ss_lobby;

    memset(&svs.lobby, 0, sizeof(svs.lobby));
    svs.lobby.active = true;
    snprintf(svs.lobby.map_path, sizeof(svs.lobby.map_path), "Maps\\Melee\\Test.w3m");
    SV_LobbySetConfig(2, 2, "Test");
    memset(&slot, 0, sizeof(slot));
    slot.visible = true;
    slot.client = MAX_CLIENTS;
    slot.map_player = 0;
    slot.type = LOBBY_SLOT_HUMAN;
    slot.race = kPlayerRaceOrc;
    slot.team = 0;
    slot.color = 4;
    snprintf(slot.name, sizeof(slot.name), "Host");
    SV_LobbySetSlot(0, &slot);
    svs.lobby.slots[0].occupied = true;
    svs.lobby.slots[0].client = 0;
    slot.map_player = 1;
    slot.type = LOBBY_SLOT_COMPUTER;
    slot.race = kPlayerRaceUndead;
    slot.team = 3;
    slot.color = 7;
    snprintf(slot.name, sizeof(slot.name), "Computer");
    SV_LobbySetSlot(1, &slot);

    SV_Map("Maps\\Melee\\Test.w3m");

    T_EQ(info.num_teams, 4);
    T_ASSERT((info.teams[0].playerMasks & (1u << 0)) != 0);
    T_ASSERT((info.teams[0].playerMasks & (1u << 1)) == 0);
    T_ASSERT((info.teams[3].playerMasks & (1u << 1)) != 0);
    T_EQ(info.players[0].playerRace, kPlayerRaceOrc);
    T_EQ(info.players[1].playerType, kPlayerTypeComputer);
    T_EQ(info.players[1].playerRace, kPlayerRaceUndead);
    T_EQ(info.players[1].color, 7);
    T_STREQ(info.players[0].playerName, "Host");

    SV_Shutdown();
    SAFE_DELETE(info.teams, MemFree);
    test_mapinfo = NULL;
}

TEST(server_net, local_map_uses_loopback_without_udp) {
    MAPINFO info;

    NET_Shutdown();
    reset_server_state(4);
    memset(&info, 0, sizeof(info));
    test_mapinfo = &info;

    SV_Map("Maps\\Melee\\Test.w3m");

    T_EQ(sv.state, ss_game);
    T_STREQ(sv.configstrings[CS_MAXCLIENTS], "4");
    T_STREQ(sv.configstrings[CS_MAPCHECKSUM], "4660");
    T_EQ(svs.num_clients, 1);
    T_EQ(svs.clients[0].netchan.remote_address.type, NA_LOOPBACK);
    T_ASSERT(!NET_IsConfigured(NS_CLIENT));
    T_ASSERT(!NET_IsConfigured(NS_SERVER));

    SV_Shutdown();
    test_mapinfo = NULL;
}

TEST(server_net, duplicate_loopback_connect_replies_without_allocating_client) {
    BYTE data[MAX_MSGLEN];
    sizeBuf_t msg = { data, sizeof(data), 0, 0 };
    netadr_t loopback = { .type = NA_LOOPBACK }, from;

    NET_Shutdown(); reset_server_state(1); SV_ClientConnect(); drain_client_packets();
    SV_DirectConnect(&loopback, "\\name\\Player");
    T_EQ(svs.num_clients, 1); T_ASSERT(NET_GetPacket(NS_CLIENT, &from, &msg));
    T_EQ(*(int *)msg.data, -1); T_ASSERT(!strcmp((char *)msg.data + 4, "client_connect"));
    SV_Shutdown(); NET_Shutdown();
}

TEST(server_net, server_snapshot_ring_scales_to_client_capacity) {
    reset_server_state(4);

    SV_InitGame();
    /* History is required; the old test omitted it before the per-frame packet budget was reduced. */
    T_EQ(svs.num_client_entities, (DWORD)(test_ge.max_clients * MAX_PACKET_ENTITIES * UPDATE_BACKUP));
    T_ASSERT(svs.num_client_entities < (DWORD)(UPDATE_BACKUP * test_ge.max_clients * MAX_GAME_ENTITIES));
    T_ASSERT(svs.client_entities != NULL);
}

TEST(server_net, snapshot_overflow_keeps_nearest_entities_in_wire_order) {
    static struct client_s game_client;
    LPCLIENT client;
    LPCLIENTFRAME frame;

    reset_server_state(1);
    SV_InitGame();
    client = &svs.clients[0]; frame = &client->frames[0];
    memset(&game_client, 0, sizeof(game_client));
    test_edicts[0].client = &game_client; client->edict = &test_edicts[0];
    test_ge.num_edicts = MAX_PACKET_ENTITIES + 3;
    for (int i = 1; i < test_ge.num_edicts; i++) {
        test_edicts[i].inuse = true;
        test_edicts[i].s.number = i; test_edicts[i].s.model = 1;
        test_edicts[i].s.origin.x = (FLOAT)(test_ge.num_edicts - i);
    }

    SV_BuildClientFrame(client);

    T_EQ(frame->num_entities, MAX_PACKET_ENTITIES);
    FOR_LOOP(i, frame->num_entities)
        T_EQ(svs.client_entities[frame->first_entity + i].number, i + 3);
    SV_Shutdown();
}

TEST(server_net, snapshot_owner_only_entity_reaches_only_owner) {
    static struct client_s game_clients[2];
    LPCLIENT owner, other;
    LPCLIENTFRAME frame;

    reset_server_state(2);
    SV_InitGame();
    owner = &svs.clients[0]; other = &svs.clients[1];
    memset(game_clients, 0, sizeof(game_clients));
    test_edicts[0].client = &game_clients[0]; owner->edict = &test_edicts[0];
    test_edicts[1].client = &game_clients[1]; other->edict = &test_edicts[1];
    game_clients[0].ps.number = 0; game_clients[1].ps.number = 1;
    test_ge.num_edicts = 3;
    test_edicts[2].inuse = true; test_edicts[2].s.number = 2; test_edicts[2].s.model = 1;
    test_edicts[2].s.player = 0; test_edicts[2].svflags = SVF_OWNER_ONLY;

    SV_BuildClientFrame(owner);
    frame = &owner->frames[0];
    T_EQ(frame->num_entities, 1);
    T_EQ(svs.client_entities[frame->first_entity].number, 2);
    SV_BuildClientFrame(other);
    T_EQ(other->frames[0].num_entities, 0);
    SV_Shutdown();
}

TEST(server_net, lobby_start_preserves_connected_clients) {
    MAPINFO info;
    lobbySlot_t slot;
    netadr_t remote = { NA_IP, { 127, 0, 0, 1 }, { 0 }, htons(PORT_SERVER + 13) };

    NET_Shutdown();
    test_client_stubs_set_cvar("game_port", "28040");
    reset_server_state(4);
    memset(&info, 0, sizeof(info));
    test_mapinfo = &info;

    SV_StartLobby("Maps\\Melee\\Test.w3m");
    T_EQ(sv.state, ss_lobby);
    T_EQ(svs.num_clients, 1);
    T_ASSERT(!NET_IsConfigured(NS_CLIENT));
    T_ASSERT(NET_IsConfigured(NS_SERVER));
    SV_LobbySetConfig(2, 2, "Test");
    memset(&slot, 0, sizeof(slot));
    slot.visible = true;
    slot.client = MAX_CLIENTS;
    slot.map_player = 0;
    slot.type = LOBBY_SLOT_HUMAN;
    slot.race = kPlayerRaceHuman;
    slot.team = 0;
    slot.color = 0;
    snprintf(slot.name, sizeof(slot.name), "Host");
    SV_LobbySetSlot(0, &slot);
    slot.map_player = 1;
    slot.type = LOBBY_SLOT_OPEN;
    slot.race = kPlayerRaceOrc;
    slot.team = 1;
    slot.color = 1;
    snprintf(slot.name, sizeof(slot.name), "Open");
    SV_LobbySetSlot(1, &slot);
    SV_DirectConnect(&remote, "\\name\\Remote");
    T_EQ(svs.num_clients, 2);
    T_EQ(svs.clients[1].playernum, 1);

    SV_Map("Maps\\Melee\\Test.w3m");

    T_EQ(sv.state, ss_game);
    T_EQ(svs.num_clients, 2);
    T_EQ(test_game_shutdowns, 0);
    T_EQ(svs.clients[0].state, cs_connected);
    T_EQ(svs.clients[0].netchan.remote_address.type, NA_LOOPBACK);
    T_EQ(svs.clients[1].state, cs_connected);
    T_EQ(svs.clients[1].netchan.remote_address.type, NA_IP);
    T_EQ(svs.clients[1].netchan.remote_address.port, remote.port);
    T_EQ(svs.clients[1].playernum, 1);
    T_STREQ(svs.clients[1].name, "Remote");

    SV_Shutdown();
    NET_Shutdown();
    test_mapinfo = NULL;
}

TEST(server_net, lobby_start_same_map_is_noop) {
    MAPINFO info;
    lobbySlot_t slot;
    netadr_t remote = { NA_IP, { 127, 0, 0, 1 }, { 0 }, htons(PORT_SERVER + 14) };

    NET_Shutdown();
    test_client_stubs_set_cvar("game_port", "28041");
    reset_server_state(4);
    memset(&info, 0, sizeof(info));
    test_mapinfo = &info;

    SV_StartLobby("Maps\\Melee\\Test.w3m");
    SV_LobbySetConfig(2, 2, "Test");
    memset(&slot, 0, sizeof(slot));
    slot.visible = true;
    slot.client = MAX_CLIENTS;
    slot.map_player = 0;
    slot.type = LOBBY_SLOT_HUMAN;
    slot.race = kPlayerRaceHuman;
    snprintf(slot.name, sizeof(slot.name), "Host");
    SV_LobbySetSlot(0, &slot);
    slot.map_player = 1;
    slot.type = LOBBY_SLOT_OPEN;
    slot.race = kPlayerRaceOrc;
    snprintf(slot.name, sizeof(slot.name), "Open");
    SV_LobbySetSlot(1, &slot);
    SV_DirectConnect(&remote, "\\name\\Remote");
    T_EQ(svs.num_clients, 2);

    SV_StartLobby("Maps\\Melee\\Test.w3m");

    T_EQ(sv.state, ss_lobby);
    T_STREQ(sv.configstrings[CS_WORLD], "Maps\\Melee\\Test.w3m");
    T_EQ(svs.num_clients, 2);
    T_EQ(svs.clients[1].state, cs_connected);
    T_EQ(svs.clients[1].netchan.remote_address.port, remote.port);

    SV_Shutdown();
    NET_Shutdown();
    test_mapinfo = NULL;
}

TEST(server_net, lobby_rejects_remote_when_slots_full) {
    MAPINFO info;
    lobbySlot_t slot;
    netadr_t remote = { NA_IP, { 127, 0, 0, 1 }, { 0 }, htons(PORT_SERVER + 15) };

    NET_Shutdown();
    test_client_stubs_set_cvar("game_port", "28042");
    reset_server_state(4);
    memset(&info, 0, sizeof(info));
    test_mapinfo = &info;

    SV_StartLobby("Maps\\Melee\\Test.w3m");
    SV_LobbySetConfig(2, 1, "Test");
    memset(&slot, 0, sizeof(slot));
    slot.visible = true;
    slot.client = MAX_CLIENTS;
    slot.map_player = 0;
    slot.type = LOBBY_SLOT_HUMAN;
    slot.race = kPlayerRaceHuman;
    snprintf(slot.name, sizeof(slot.name), "Host");
    SV_LobbySetSlot(0, &slot);

    T_EQ(svs.num_clients, 1);
    SV_DirectConnect(&remote, "\\name\\Remote");
    T_EQ(svs.num_clients, 1);

    SV_Shutdown();
    NET_Shutdown();
    test_mapinfo = NULL;
}

TEST(server_net, lobby_setup_message_round_trips_slot_table) {
    BYTE msg_buf[MAX_MSGLEN];
    sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    LPCLIENT cl;
    char text[128];

    reset_server_state(4);
    sv.state = ss_lobby;
    svs.num_clients = 1;
    cl = &svs.clients[0];
    cl->state = cs_connected;
    cl->lobby_slot = 1;
    SZ_Init(&cl->netchan.message, cl->netchan.message_buf, MAX_MSGLEN);
    memset(&svs.lobby, 0, sizeof(svs.lobby));
    svs.lobby.active = true;
    snprintf(svs.lobby.map_path, sizeof(svs.lobby.map_path), "Maps\\Melee\\Test.w3m");
    snprintf(svs.lobby.map_name, sizeof(svs.lobby.map_name), "Test Map");
    svs.lobby.game_speed = 3;
    svs.lobby.slot_count = 2;
    svs.lobby.revision = 7;
    svs.lobby.slots[1].visible = true;
    svs.lobby.slots[1].occupied = true;
    svs.lobby.slots[1].client = 0;
    svs.lobby.slots[1].map_player = 4;
    svs.lobby.slots[1].type = LOBBY_SLOT_HUMAN;
    svs.lobby.slots[1].race = kPlayerRaceNightElf;
    svs.lobby.slots[1].team = 2;
    svs.lobby.slots[1].color = 6;
    snprintf(svs.lobby.slots[1].name, sizeof(svs.lobby.slots[1].name), "Remote");

    SV_LobbyWriteSetup(cl);
    msg.data = cl->netchan.message.data;
    msg.cursize = cl->netchan.message.cursize;
    msg.readcount = 0;

    T_EQ(MSG_ReadByte(&msg), svc_gamecmd);
    MSG_ReadString(&msg, text);
    T_STREQ(text, "lobby_setup");
    T_ASSERT(MSG_ReadShort(&msg) > 0);
    MSG_ReadString(&msg, text);
    T_STREQ(text, "Maps\\Melee\\Test.w3m");
    MSG_ReadString(&msg, text);
    T_STREQ(text, "Test Map");
    T_EQ(MSG_ReadByte(&msg), 3);
    T_EQ(MSG_ReadByte(&msg), 2);
    T_EQ(MSG_ReadByte(&msg), 1);
    T_EQ(MSG_ReadLong(&msg), 7);
    FOR_LOOP(i, 1) {
        T_EQ(MSG_ReadByte(&msg), 0);
        T_EQ(MSG_ReadByte(&msg), 0);
        T_EQ(MSG_ReadByte(&msg), 255);
        T_EQ(MSG_ReadByte(&msg), 255);
        T_EQ(MSG_ReadByte(&msg), 0);
        T_EQ(MSG_ReadByte(&msg), 0);
        T_EQ(MSG_ReadByte(&msg), 0);
        T_EQ(MSG_ReadByte(&msg), 0);
        MSG_ReadString(&msg, text);
        T_STREQ(text, "");
    }
    T_EQ(MSG_ReadByte(&msg), 1);
    T_EQ(MSG_ReadByte(&msg), 1);
    T_EQ(MSG_ReadByte(&msg), 0);
    T_EQ(MSG_ReadByte(&msg), 4);
    T_EQ(MSG_ReadByte(&msg), LOBBY_SLOT_HUMAN);
    T_EQ(MSG_ReadByte(&msg), kPlayerRaceNightElf);
    T_EQ(MSG_ReadByte(&msg), 2);
    T_EQ(MSG_ReadByte(&msg), 6);
    MSG_ReadString(&msg, text);
    T_STREQ(text, "Remote");
}

TEST(server_net, multicast_syncs_updates_to_all_connected_clients) {
    BYTE payload[] = { 0x11, 0x22, 0x33, 0x44 };
    VECTOR3 origin = { 0, 0, 0 };
    reset_server_state(4);
    SZ_Init(&sv.multicast, sv.multicast_buf, sizeof(sv.multicast_buf));
    FOR_LOOP(i, 3) {
        SZ_Init(&svs.clients[i].netchan.message,
                svs.clients[i].netchan.message_buf, MAX_MSGLEN);
    }
    svs.num_clients = 3;

    SZ_Write(&sv.multicast, payload, sizeof(payload));
    SV_Multicast(&origin, MULTICAST_ALL_R);

    FOR_LOOP(i, 3) {
        T_EQ(svs.clients[i].netchan.message.cursize, (int)sizeof(payload));
        T_ASSERT(memcmp(svs.clients[i].netchan.message.data, payload, sizeof(payload)) == 0);
    }
    T_EQ(sv.multicast.cursize, 0);
}

TEST(server_net, lobby_chat_broadcasts_to_connected_clients) {
    BYTE msg_buf[MAX_MSGLEN];
    sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t from;
    char text[512];

    NET_Shutdown();
    reset_server_state(4);
    drain_client_packets();
    sv.state = ss_lobby;
    svs.num_clients = 2;
    FOR_LOOP(i, svs.num_clients) {
        svs.clients[i].state = cs_connected;
        svs.clients[i].netchan.remote_address.type = NA_LOOPBACK;
        SZ_Init(&svs.clients[i].netchan.message,
                svs.clients[i].netchan.message_buf,
                MAX_MSGLEN);
    }

    SV_LobbyBroadcastChatFrom(0, "Host", "hello team");

    FOR_LOOP(i, svs.num_clients) {
        T_ASSERT(NET_GetPacket(NS_CLIENT, &from, &msg));
        T_EQ(MSG_ReadByte(&msg), svc_gamecmd);
        MSG_ReadString(&msg, text);
        T_STREQ(text, "lobby_chat");
        T_ASSERT(MSG_ReadShort(&msg) > 0);
        T_EQ(MSG_ReadByte(&msg), i == 0 ? 1 : 0);
        MSG_ReadString(&msg, text);
        T_STREQ(text, "Host: hello team");
    }
}
