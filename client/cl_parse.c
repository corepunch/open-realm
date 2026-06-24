/*
 * cl_parse.c — Parse server-to-client messages.
 *
 * CL_ParseServerMessage() is the main dispatch loop: it reads each message
 * type byte and calls the appropriate handler.  The supported message types
 * are defined in src/common/common.h (svc_* constants).
 *
 * Entity state arrives as delta-compressed packets (svc_packetentities) that
 * are applied on top of the previous frame's state.  Player state, UI layout,
 * config strings and temporary effects each have their own message types.
 */
#include "client.h"
#ifdef SC2
#include "games/starcraft-2/common/sc2_map.h"
#endif

static LPCSTR CL_LobbySlotTypeName(lobbySlotType_t type) {
    switch (type) {
        case LOBBY_SLOT_OPEN: return "open";
        case LOBBY_SLOT_HUMAN: return "human";
        case LOBBY_SLOT_COMPUTER: return "computer";
        case LOBBY_SLOT_CLOSED: return "closed";
    }
    return "unknown";
}

static LPCSTR CL_PlayerRaceName(playerRace_t race) {
    switch (race) {
        case kPlayerRaceNone: return "none";
        case kPlayerRaceHuman: return "human";
        case kPlayerRaceOrc: return "orc";
        case kPlayerRaceUndead: return "undead";
        case kPlayerRaceNightElf: return "nightelf";
    }
    return "unknown";
}

/* Apply a stream of delta-encoded entity updates.  For each entity the server
 * sends only the fields that changed since the previous frame.  A U_REMOVE
 * flag signals that an entity should be removed from the local table. */
static void CL_ReadPacketEntities(LPSIZEBUF msg) {
    int count = 0;
    int previous = 0;
    int debug_entities = Cvar_Integer("cl_debug_entities", 0);
    int added = 0;
    int removed = 0;
    int changed = 0;

    while (true) {
        DWORD bits = 0;
        if (msg->readcount + sizeof(DWORD) + sizeof(WORD) > msg->cursize) {
            break;
        }
        int nument = MSG_ReadEntityBits(msg, &bits);
        if (nument == 0 && bits == 0)
            break;
        if (nument < 0 || nument >= MAX_CLIENT_ENTITIES) {
            fprintf(stderr,
                    "CL_ReadPacketEntities: bad entity %d bits=0x%x count=%d previous=%d read=%u size=%u frame=%d\n",
                    nument,
                    (unsigned)bits,
                    count,
                    previous,
                    (unsigned)msg->readcount,
                    (unsigned)msg->cursize,
                    cl.frame.serverframe);
            msg->readcount = msg->cursize;
            break;
        }
        previous = nument;
        count++;
        centity_t *ent = &cl.ents[nument];
        entityState_t old = ent->current;
        if (bits & (1u << U_REMOVE)) {
            if (debug_entities && old.model) {
                fprintf(stderr,
                        "CL entity remove frame=%d ent=%d model=%u class=%u origin=(%.1f %.1f %.1f)\n",
                        cl.frame.serverframe,
                        nument,
                        (unsigned)old.model,
                        (unsigned)old.class_id,
                        old.origin.x,
                        old.origin.y,
                        old.origin.z);
            }
            memset(ent, 0, sizeof(centity_t));
            removed++;
            continue;
        }
        ent->prev = ent->current;
        MSG_ReadDeltaEntity(msg, &ent->current, nument, bits);
        if (debug_entities) {
            if (!old.model && ent->current.model) {
                fprintf(stderr,
                        "CL entity add frame=%d ent=%d model=%u class=%u origin=(%.1f %.1f %.1f) radius=%.1f\n",
                        cl.frame.serverframe,
                        nument,
                        (unsigned)ent->current.model,
                        (unsigned)ent->current.class_id,
                        ent->current.origin.x,
                        ent->current.origin.y,
                        ent->current.origin.z,
                        ent->current.radius);
                added++;
            } else if (old.model && ent->current.model &&
                       (old.model != ent->current.model ||
                        old.class_id != ent->current.class_id)) {
                fprintf(stderr,
                        "CL entity change frame=%d ent=%d model=%u->%u class=%u->%u\n",
                        cl.frame.serverframe,
                        nument,
                        (unsigned)old.model,
                        (unsigned)ent->current.model,
                        (unsigned)old.class_id,
                        (unsigned)ent->current.class_id);
                changed++;
            }
        }
        if (ent->serverframe != cl.frame.serverframe - 1) {
            ent->prev = ent->current;
        }
        ent->serverframe = cl.frame.serverframe;
    }
    cl.num_entities = MAX_CLIENT_ENTITIES;
    if (debug_entities > 1 && (added || removed || changed)) {
        fprintf(stderr,
                "CL entity summary frame=%d packet=%d add=%d remove=%d change=%d\n",
                cl.frame.serverframe,
                count,
                added,
                removed,
                changed);
    }
}

static void CL_ParseConfigString(LPSIZEBUF msg) {
    int const index = MSG_ReadShort(msg);
    if (index == CS_STATUSBAR) {
        MSG_Read(msg, cl.configstrings[index], sizeof(*cl.configstrings));
    } else {
        MSG_ReadString(msg, cl.configstrings[index]);
    }
    if (index == CS_WORLD && cl.configstrings[index][0] &&
        strcmp(cl.loading_map, cl.configstrings[index])) {
        CL_BeginLoadingMap(cl.configstrings[index]);
    }
//    printf("%d %s\n", index, cl.configstrings[index]);
}

static void CL_ParseBaseline(LPSIZEBUF msg) {
    DWORD bits = 0;
    DWORD index = MSG_ReadEntityBits(msg, &bits);
    if (index >= MAX_CLIENT_ENTITIES) {
        fprintf(stderr, "CL_ParseBaseline: bad entity %u\n", (unsigned)index);
        msg->readcount = msg->cursize;
        return;
    }
    centity_t *cent = &cl.ents[index];
    memset(&cent->baseline, 0, sizeof(entityState_t));
    MSG_ReadDeltaEntity(msg, &cent->baseline, index, bits);
    memcpy(&cent->current, &cent->baseline, sizeof(entityState_t));
    memcpy(&cent->prev, &cent->baseline, sizeof(entityState_t));
}

/* Handle the svc_frame header: record the server frame number and time, then
 * snapshot the current entity states into their "prev" fields so the renderer
 * can interpolate between the previous and current positions. */
void CL_ParseFrame(LPSIZEBUF msg) {
    cl.frame.serverframe = MSG_ReadLong(msg);
    cl.frame.servertime = MSG_ReadLong(msg);
    cl.frame.oldclientframe = MSG_ReadLong(msg);
    cl.time = cl.frame.servertime;
    
    FOR_LOOP(index, MAX_CLIENT_ENTITIES) {
        centity_t *ce = &cl.ents[index];
        if (!ce->current.model)
            continue;
        ce->prev = ce->current;
    }
}

void CL_ParsePlayerInfo(LPSIZEBUF msg) {
    DWORD bits;
    DWORD plnum = MSG_ReadPlayerBits(msg, &bits);
    BOOL first_camera_sample = cl.viewDef.camerastate[0].znear <= 0 ||
                               cl.viewDef.camerastate[0].zfar <= 0;
    FLOAT znear;
    FLOAT zfar;
    MSG_ReadDeltaPlayerState(msg, &cl.playerstate, plnum, bits);
    VECTOR2 server_origin = cl.playerstate.origin;
    cls.state = ca_active;
    if (cl.playerstate.client_ui_state == CLIENT_UI_GAME) {
        CL_SetGameplayInput();
    }
    znear = cl.viewDef.camerastate[0].znear > 0 ? cl.viewDef.camerastate[0].znear : 100;
    zfar = cl.viewDef.camerastate[0].zfar > 0 ? cl.viewDef.camerastate[0].zfar : 5000;

    cl.viewDef.camerastate[1] = cl.viewDef.camerastate[0];
    cl.viewDef.camerastate[0].origin.x = server_origin.x;
    cl.viewDef.camerastate[0].origin.y = server_origin.y;
    cl.viewDef.camerastate[0].origin.z = 0;
    cl.viewDef.camerastate[0].viewquat = cl.playerstate.viewquat;
    cl.viewDef.camerastate[0].viewangles = cl.playerstate.viewangles;
    cl.viewDef.camerastate[0].distance = cl.playerstate.distance;
    cl.viewDef.camerastate[0].fov = cl.playerstate.fov;
    cl.viewDef.camerastate[0].znear = znear;
    cl.viewDef.camerastate[0].zfar = zfar;
#ifdef SC2
    {
        sc2MapCamera_t camera;

        SC2_MapDefaultCamera(&camera);
        cl.viewDef.camerastate[0].viewangles = (VECTOR3){ camera.pitch, camera.yaw, 0.0f };
        cl.viewDef.camerastate[0].viewquat = Quaternion_fromEuler(&cl.viewDef.camerastate[0].viewangles, ROTATE_ZYX);
        cl.viewDef.camerastate[0].distance = camera.distance;
        cl.viewDef.camerastate[0].fov = camera.fov;
        cl.viewDef.camerastate[0].znear = camera.znear;
        cl.viewDef.camerastate[0].zfar = camera.zfar;
        znear = camera.znear;
        zfar = camera.zfar;
    }
#endif

    if (first_camera_sample) {
        cl.viewDef.camerastate[1] = cl.viewDef.camerastate[0];
    } else {
        cl.viewDef.camerastate[1].znear = znear;
        cl.viewDef.camerastate[1].zfar = zfar;
    }

    if (cl.camera_prediction.active) {
        if (server_origin.x == cl.camera_prediction.origin.x &&
            server_origin.y == cl.camera_prediction.origin.y) {
            cl.camera_prediction.active = false;
        } else {
            cl.viewDef.camerastate[0].origin.x = cl.camera_prediction.origin.x;
            cl.viewDef.camerastate[0].origin.y = cl.camera_prediction.origin.y;
            cl.viewDef.camerastate[1].origin.x = cl.camera_prediction.origin.x;
            cl.viewDef.camerastate[1].origin.y = cl.camera_prediction.origin.y;
        }
    }
}

/* Receive an svc_layout message from the server.  The server serializes the
 * entire UI frame tree as a binary blob; the client stores the raw blob and
 * passes it to the renderer each frame without interpreting the contents. */
void CL_ParseLayout(LPSIZEBUF msg) {
    DWORD layer = MSG_ReadByte(msg);
    DWORD payload_size = 0;
    BOOL terminated = false;

    if (layer >= MAX_LAYOUT_LAYERS) {
        fprintf(stderr, "CL_ParseLayout: bad layer %u\n", (unsigned)layer);
        msg->readcount = msg->cursize;
        return;
    }

    if (ui.ClearLayoutLayer) {
        ui.ClearLayoutLayer(layer);
    }
    SAFE_DELETE(cl.layout[layer], MemFree);
    DWORD start = msg->readcount;
    while (true) {
        UIFRAME ent = { 0 };
        DWORD bits = 0;
        if (msg->readcount + sizeof(DWORD) + sizeof(WORD) > msg->cursize) {
            break;
        }
        DWORD nument = MSG_ReadEntityBits(msg, &bits);
        if (nument == 0 && bits == 0) {
            terminated = true;
            break;
        }
        MSG_ReadDeltaUIFrame(msg, &ent, nument, bits);
        if (msg->readcount + sizeof(WORD) > msg->cursize) {
            break;
        }
        ent.buffer.size = MSG_ReadShort(msg);
        if (msg->readcount > msg->cursize ||
            ent.buffer.size > msg->cursize - msg->readcount) {
            break;
        }

        msg->readcount += ent.buffer.size;
    }
    if (!terminated) {
        fprintf(stderr, "CL_ParseLayout: malformed layer %u\n", (unsigned)layer);
        msg->readcount = msg->cursize;
        return;
    }
    if (start > msg->cursize || msg->readcount > msg->cursize ||
        msg->readcount < start) { /* guard against malformed data and wraparound */
        msg->readcount = msg->cursize;
        return;
    }
    payload_size = msg->readcount - start;
    cl.layout[layer] = MemAlloc(sizeof(DWORD) + payload_size);
    memcpy(cl.layout[layer], &payload_size, sizeof(payload_size));
    memcpy((LPBYTE)cl.layout[layer] + sizeof(payload_size), msg->data + start, payload_size);
    if (ui.SetLayoutLayer) {
        ui.SetLayoutLayer(layer, cl.layout[layer]);
    }
}

void CL_ParseCursor(LPSIZEBUF msg) {
    DWORD bits = 0;
    SAFE_DELETE(cl.cursorEntity, MemFree);
    cl.cursorEntity = MemAlloc(sizeof(entityState_t));
    MSG_ReadEntityBits(msg, &bits);
    MSG_ReadDeltaEntity(msg, cl.cursorEntity, 0, bits);
    if (cl.cursorEntity->model == 0) {
        SAFE_DELETE(cl.cursorEntity, MemFree);
    }
}

void CL_ParseCursorSplat(LPSIZEBUF msg) {
    cl.cursor_splat.image = MSG_ReadShort(msg);
    cl.cursor_splat.radius = MSG_ReadFloat(msg);
    if (!cl.cursor_splat.image || cl.cursor_splat.radius <= 0.0f ||
        cl.cursor_splat.image >= MAX_IMAGES) {
        memset(&cl.cursor_splat, 0, sizeof(cl.cursor_splat));
    }
}

static BOOL CL_EnsureFogOfWarSize(DWORD width, DWORD height) {
    DWORD cells;

    if (!width || !height) {
        return false;
    }
    if (cl.fow.width == width && cl.fow.height == height &&
        cl.fow.visible && cl.fow.explored && cl.fow.texture)
    {
        return true;
    }

    SAFE_DELETE(cl.fow.visible, MemFree);
    SAFE_DELETE(cl.fow.explored, MemFree);
    SAFE_DELETE(cl.fow.texture, MemFree);
    cl.fow.width = width;
    cl.fow.height = height;
    cells = width * height;
    cl.fow.visible = MemAlloc(cells);
    cl.fow.explored = MemAlloc(cells);
    cl.fow.texture = MemAlloc(cells);
    if (!cl.fow.visible || !cl.fow.explored || !cl.fow.texture) {
        SAFE_DELETE(cl.fow.visible, MemFree);
        SAFE_DELETE(cl.fow.explored, MemFree);
        SAFE_DELETE(cl.fow.texture, MemFree);
        cl.fow.width = 0;
        cl.fow.height = 0;
        return false;
    }
    memset(cl.fow.visible, 0, cells);
    memset(cl.fow.explored, 0, cells);
    memset(cl.fow.texture, 0, cells);
    return true;
}

static void CL_ClearFogRows(BYTE *plane, DWORD first_row, DWORD row_count) {
    if (!plane || first_row >= cl.fow.height) {
        return;
    }
    row_count = MIN(row_count, cl.fow.height - first_row);
    memset(plane + first_row * cl.fow.width, 0, row_count * cl.fow.width);
}

static void CL_UpdateFogTexture(void) {
    DWORD cells = cl.fow.width * cl.fow.height;

    if (!cl.fow.texture || !cl.fow.visible || !cl.fow.explored) {
        return;
    }
    FOR_LOOP(i, cells) {
        cl.fow.texture[i] = cl.fow.visible[i] ? 255 : (cl.fow.explored[i] ? 128 : 0);
    }
    if (re.SetFogOfWarData) {
        re.SetFogOfWarData(cl.fow.width, cl.fow.height, cl.fow.texture);
    }
}

static BYTE *CL_FogPlaneForStreamIndex(DWORD flags, DWORD stream_index) {
    DWORD index = 0;

    if (flags & FOW_MSG_VISIBLE_PLANE) {
        if (stream_index == index) {
            return cl.fow.visible;
        }
        index++;
    }
    if (flags & FOW_MSG_EXPLORED_PLANE) {
        if (stream_index == index) {
            return cl.fow.explored;
        }
    }
    return NULL;
}

static BOOL CL_ValidateFogRLE(BYTE const *payload, DWORD payload_bytes, DWORD expected_bits) {
    DWORD bits = 0;

    if (!payload || payload_bytes < 2 || expected_bits == 0 ||
        (payload[0] != 0 && payload[0] != 1))
    {
        return false;
    }

    for (DWORD i = 1; i < payload_bytes; i++) {
        DWORD len = payload[i];
        if (bits == expected_bits) {
            return false;
        }
        bits += len;
        if (bits > expected_bits) {
            return false;
        }
    }
    return bits == expected_bits;
}

static void CL_UnpackFogRLE(BYTE const *payload,
                            DWORD payload_bytes,
                            DWORD flags,
                            DWORD first_row,
                            DWORD row_count)
{
    DWORD plane_bits = cl.fow.width * row_count;
    DWORD decoded = 0;
    BYTE value = payload[0] ? 1 : 0;

    for (DWORD i = 1; i < payload_bytes; i++) {
        DWORD len = payload[i];

        FOR_LOOP(j, len) {
            DWORD plane_index = decoded / plane_bits;
            DWORD bit_index = decoded % plane_bits;
            DWORD row = bit_index / cl.fow.width;
            DWORD x = bit_index % cl.fow.width;
            BYTE *plane = CL_FogPlaneForStreamIndex(flags, plane_index);

            if (plane) {
                plane[(first_row + row) * cl.fow.width + x] = value;
            }
            decoded++;
        }

        if (len != 255) {
            value = !value;
        }
    }
}

void CL_ParseFogOfWar(LPSIZEBUF msg) {
    DWORD flags = MSG_ReadByte(msg);
    DWORD width = MSG_ReadShort(msg);
    DWORD height = MSG_ReadShort(msg);
    DWORD first_row = MSG_ReadShort(msg);
    DWORD row_count = MSG_ReadShort(msg);
    DWORD payload_bytes = MSG_ReadShort(msg);
    DWORD plane_count = 0;
    DWORD expected_bits;
    BYTE const *payload;

    if (flags & FOW_MSG_VISIBLE_PLANE) {
        plane_count++;
    }
    if (flags & FOW_MSG_EXPLORED_PLANE) {
        plane_count++;
    }
    expected_bits = width * row_count * plane_count;
    if (!plane_count || !width || !height || !(flags & FOW_MSG_RLE) ||
        first_row >= height || row_count > height - first_row ||
        payload_bytes > msg->cursize - msg->readcount)
    {
        msg->readcount = MIN(msg->cursize, msg->readcount + payload_bytes);
        return;
    }
    payload = msg->data + msg->readcount;
    if (!CL_ValidateFogRLE(payload, payload_bytes, expected_bits)) {
        msg->readcount = MIN(msg->cursize, msg->readcount + payload_bytes);
        return;
    }
    if (!CL_EnsureFogOfWarSize(width, height)) {
        msg->readcount = MIN(msg->cursize, msg->readcount + payload_bytes);
        return;
    }

    if (flags & FOW_MSG_FULL) {
        CL_ClearFogRows(cl.fow.visible, first_row, row_count);
        CL_ClearFogRows(cl.fow.explored, first_row, row_count);
    }
    CL_UnpackFogRLE(payload, payload_bytes, flags, first_row, row_count);
    msg->readcount += payload_bytes;
    CL_UpdateFogTexture();
}

void CL_MirrorMessage(LPSIZEBUF msg) {
    char buf[256] = { 0 };
    MSG_ReadString(msg, buf);
    if (!strcmp(buf, "begin")) {
        return;
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteString(&cls.netchan.message, buf);
}

static void CL_ParseLobbySetup(LPSIZEBUF msg) {
    lobbyState_t state;
    int slot_count;
    int local_slot;

    memset(&state, 0, sizeof(state));
    state.active = true;
    MSG_ReadStringN(msg, state.map_path, sizeof(state.map_path));
    MSG_ReadStringN(msg, state.map_name, sizeof(state.map_name));
    state.game_speed = (DWORD)MSG_ReadByte(msg);
    slot_count = MSG_ReadByte(msg);
    local_slot = MSG_ReadByte(msg);
    state.revision = (DWORD)MSG_ReadLong(msg);
    if (slot_count < 0) {
        slot_count = 0;
    }
    if (slot_count > MAX_PLAYERS) {
        slot_count = MAX_PLAYERS;
    }
    state.slot_count = (DWORD)slot_count;
    state.local_slot = local_slot >= 0 && local_slot < MAX_PLAYERS ? (DWORD)local_slot : MAX_PLAYERS;
    FOR_LOOP(i, state.slot_count) {
        lobbySlot_t *slot = &state.slots[i];
        int client;
        int map_player;

        slot->visible = MSG_ReadByte(msg) != 0;
        slot->occupied = MSG_ReadByte(msg) != 0;
        client = MSG_ReadByte(msg);
        map_player = MSG_ReadByte(msg);
        slot->client = client >= 0 && client < MAX_CLIENTS ? (DWORD)client : MAX_CLIENTS;
        slot->map_player = map_player >= 0 && map_player < MAX_PLAYERS ? (DWORD)map_player : MAX_PLAYERS;
        slot->type = (lobbySlotType_t)MSG_ReadByte(msg);
        slot->race = (playerRace_t)MSG_ReadByte(msg);
        slot->team = (DWORD)MSG_ReadByte(msg);
        slot->color = (DWORD)MSG_ReadByte(msg);
        MSG_ReadStringN(msg, slot->name, sizeof(slot->name));
    }
    fprintf(stderr,
            "CL_ParseLobbySetup: map=\"%s\" name=\"%s\" speed=%u slots=%u local_slot=%u revision=%u\n",
            state.map_path,
            state.map_name,
            (unsigned)state.game_speed,
            (unsigned)state.slot_count,
            (unsigned)state.local_slot,
            (unsigned)state.revision);
    FOR_LOOP(i, state.slot_count) {
        lobbySlot_t const *slot = &state.slots[i];

        if (!slot->visible) {
            continue;
        }
        fprintf(stderr,
                "  client lobby slot %u%s: occupied=%d client=%u map_player=%u type=%s race=%s team=%u color=%u name=\"%s\"\n",
                (unsigned)i,
                i == state.local_slot ? " local" : "",
                slot->occupied ? 1 : 0,
                (unsigned)slot->client,
                (unsigned)slot->map_player,
                CL_LobbySlotTypeName(slot->type),
                CL_PlayerRaceName(slot->race),
                (unsigned)slot->team,
                (unsigned)slot->color,
                slot->name);
    }
    if (!state.map_path[0] || !ui.UpdateLobbySetup) {
        return;
    }
    ui.UpdateLobbySetup(&state);
}

static void CL_ParseLobbyChat(LPSIZEBUF msg) {
    char text[512] = { 0 };
    char command[sizeof(text) + 32];
    int own;

    own = MSG_ReadByte(msg);
    MSG_ReadStringN(msg, text, sizeof(text));
    if (!text[0]) {
        return;
    }
    snprintf(command, sizeof(command), "menu_game_setup_chat %u %s", own ? 1u : 0u, text);
    Cbuf_AddText(command);
    Cbuf_AddText("\n");
}

static void CL_ParseGameCommand(LPSIZEBUF msg) {
    char command[MAX_PATHLEN] = { 0 };
    sizeBuf_t payload;
    int payload_size;
    DWORD payload_start;

    MSG_ReadStringN(msg, command, sizeof(command));
    payload_size = MSG_ReadShort(msg);
    if (payload_size < 0 || msg->readcount + (DWORD)payload_size > msg->cursize) {
        msg->readcount = msg->cursize;
        return;
    }

    payload_start = msg->readcount;
    payload.data = msg->data + payload_start;
    payload.maxsize = (DWORD)payload_size;
    payload.cursize = (DWORD)payload_size;
    payload.readcount = 0;

    if (!strcmp(command, "lobby_setup")) {
        CL_ParseLobbySetup(&payload);
    } else if (!strcmp(command, "lobby_chat")) {
        CL_ParseLobbyChat(&payload);
    }

    msg->readcount = payload_start + (DWORD)payload_size;
}

/* Dispatch loop for a complete server message buffer.  Each iteration reads
 * one message-type byte and calls the matching handler.  An unknown type
 * stops processing and prints an error to stderr. */
void CL_ParseServerMessage(LPSIZEBUF msg) {
    BYTE pack_id = 0;
    while (MSG_Read(msg, &pack_id, 1)) {
        switch (pack_id) {
            case svc_bad:
                return;
            case svc_playerinfo:
                CL_ParsePlayerInfo(msg);
                break;
            case svc_spawnbaseline:
                CL_ParseBaseline(msg);
                break;
            case svc_packetentities:
                CL_ReadPacketEntities(msg);
                break;
            case svc_configstring:
                CL_ParseConfigString(msg);
                break;
            case svc_frame:
                CL_ParseFrame(msg);
                break;
            case svc_layout:
                CL_ParseLayout(msg);
                break;
            case svc_cursor:
                CL_ParseCursor(msg);
                break;
            case svc_cursor_splat:
                CL_ParseCursorSplat(msg);
                break;
            case svc_mirror:
                CL_MirrorMessage(msg);
                break;
            case svc_gamecmd:
                CL_ParseGameCommand(msg);
                break;
            case svc_fogofwar:
                CL_ParseFogOfWar(msg);
                break;
            case svc_temp_entity:
                CL_ParseTEnt(msg);
                break;                
            // Phase 8: Unit UI data
            case svc_unit_ui:
                CL_ParseUnitUI(msg);
                break;
            case svc_disconnect:
                CL_Disconnect("Server disconnected.", true);
                msg->readcount = msg->cursize;
                return;
            default:
                fprintf(stderr, "Unknown message %d\n", pack_id);
                return;
        }
    }
}
