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
#include <stdlib.h>

#include "client.h"
#include "sound/s_local.h"
#include "ui_layout.h"
#ifdef SC2
#include "games/starcraft-2/common/sc2_map.h"
#endif

#ifndef WOW
/* Keep predicted camera targets inside the server-authored camera bounds. */
VECTOR2 CL_ClampCameraPosition(VECTOR2 position) {
#ifdef WC3
    if (cl.playerstate.camera_bounds.max.x > cl.playerstate.camera_bounds.min.x)
        position.x = MAX(cl.playerstate.camera_bounds.min.x, MIN(cl.playerstate.camera_bounds.max.x, position.x));
    if (cl.playerstate.camera_bounds.max.y > cl.playerstate.camera_bounds.min.y)
        position.y = MAX(cl.playerstate.camera_bounds.min.y, MIN(cl.playerstate.camera_bounds.max.y, position.y));
#endif
    return position;
}
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
void CL_AddActiveEntity(DWORD index) {
    if (index >= MAX_CLIENT_ENTITIES || !cl.ents[index].current.model) {
        return;
    }
    FOR_LOOP(i, cl.num_active) {
        if (cl.active_entities[i] == index) {
            return;
        }
    }
    cl.active_entities[cl.num_active++] = index;
}

void CL_RemoveActiveEntity(DWORD index) {
    FOR_LOOP(i, cl.num_active) {
        if (cl.active_entities[i] == index) {
            cl.active_entities[i] = cl.active_entities[--cl.num_active];
            return;
        }
    }
}

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
            /* Clear membership unconditionally: the current model may already be
             * zero (a prior delta replaced it with a sound/event), so gating on
             * old.model leaves a stale active-list entry behind. */
            CL_RemoveActiveEntity(nument);
            memset(&ent->current, 0, sizeof(ent->current));
            memset(&ent->prev, 0, sizeof(ent->prev));
            removed++;
            continue;
        }
        if (!old.model) {
            ent->current = ent->baseline;
        }
        ent->prev = ent->current;
        MSG_ReadDeltaEntity(msg, &ent->current, nument, bits);
        /* Keep the active list in sync with current.model on both transitions:
         * a model-less entity may gain a model (add) or lose it to a sound/event
         * (remove) without a U_REMOVE. */
        if (!old.model && ent->current.model) {
            CL_AddActiveEntity(nument);
        } else if (old.model && !ent->current.model) {
            CL_RemoveActiveEntity(nument);
        }
        if (ent->current.event)
            CL_EntityEvent(&ent->current);
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
    static PATHSTR last_world;
    PATHSTR olds;
    int const index = MSG_ReadShort(msg);
    if (index < 0 || index >= MAX_CONFIGSTRINGS) {
        Com_Error(ERR_DROP, "configstring %d > MAX_CONFIGSTRINGS", index);
        return;
    }
    olds[0] = '\0';
    if (index == CS_STATUSBAR) {
        MSG_Read(msg, cl.configstrings[index], sizeof(*cl.configstrings));
    } else {
        snprintf(olds, sizeof(olds), "%s", cl.configstrings[index]);
        MSG_ReadString(msg, cl.configstrings[index]);
    }
    if (index >= CS_GENERAL && index < CS_GENERAL + CS_MAX_NAMES / ENT_NAMES_PER_CS)
        entity_name_pool_decode(cl.configstrings[index]);
    if (cl.refresh_prepped)
        CL_UpdateConfigString(index, olds);
    if (index == CS_MINIMAP && cl.refresh_prepped && strcmp(olds, cl.configstrings[index]))
        CL_UpdateMinimapModel();
    if (index == CS_WORLD && cl.configstrings[index][0] &&
        strcmp(last_world, cl.configstrings[index])) {
        snprintf(last_world, sizeof(last_world), "%s", cl.configstrings[index]);
        CL_BeginLoadingMap(cl.configstrings[index]);
    }
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
    if (cent->current.model) {
        CL_AddActiveEntity(index);
    }
}

/* Handle the svc_frame header: record the server frame number and time, then
 * snapshot the current entity states into their "prev" fields so the renderer
 * can interpolate between the previous and current positions. */
void CL_ParseFrame(LPSIZEBUF msg) {
    cl.frame.serverframe = MSG_ReadLong(msg);
    cl.frame.servertime = MSG_ReadLong(msg);
    cl.frame.oldclientframe = MSG_ReadLong(msg);
    cl.time = cl.frame.servertime;
    
    if (cls.state != ca_active && cl.refresh_prepped) {
        cls.state = ca_active;
        cl.playerstate.client_ui_state = CLIENT_UI_GAME;
        SCR_EndLoadingPlaque();
        CL_SetGameplayInput();
    }
    
    FOR_LOOP(i, cl.num_active) {
        centity_t *ce = &cl.ents[cl.active_entities[i]];
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
    if (cl.playerstate.client_ui_state == CLIENT_UI_GAME &&
        cls.key_dest != key_console && cls.key_dest != key_menu) {
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
        cl.viewDef.camerastate[0].origin.z = CM_GetHeightAtPoint(server_origin.x, server_origin.y) + cl.playerstate.viewangles.z;
        /* Galaxy camera natives author these snapshot fields; replacing them here kept every cutscene on map defaults. */
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
#ifdef WC3
        cl.camera_prediction.origin = CL_ClampCameraPosition(cl.camera_prediction.origin);
#endif
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
 * passes it to the renderer each frame without interpreting the contents.
 *
 * Wire contract: ONE message carries exactly ONE layer — header (svc_layout +
 * layer byte), frames, then a terminator (LONG 0 + SHORT 0 read by
 * MSG_ReadEntityBits).  After the terminator the outer packet loop reads the
 * next byte as a new message id, so server frames written outside a layer (no
 * svc_layout header) are never attributed to any layer and are silently dropped
 * as an unknown message.  The server must open a layer before writing frames
 * (see UI_WriteStart/UI_WriteEnd in games/world-of-warcraft/game/g_ui.c). */
void CL_ParseLayout(LPSIZEBUF msg) {
    DWORD layer = MSG_ReadByte(msg);
    DWORD payload_size = 0;
    BOOL terminated = false;
    BOOL has_frames = false;

    if (layer >= MAX_LAYOUT_LAYERS) {
        fprintf(stderr, "CL_ParseLayout: bad layer %u\n", (unsigned)layer);
        msg->readcount = msg->cursize;
        return;
    }

    if (Cvar_Integer("ui_layout_debug", 0)) {
        fprintf(stderr, "UI_LAYOUT_DEBUG begin layer=%u packet_read=%u packet_size=%u uiflags=0x%08x hidden=%u\n",
            (unsigned)layer, (unsigned)msg->readcount, (unsigned)msg->cursize,
            (unsigned)cl.playerstate.uiflags,
            (unsigned)((cl.playerstate.uiflags & (1u << layer)) != 0));
    }

    SCR_ClearLayoutLayer(layer);
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
        has_frames = true;
        MSG_ReadDeltaUIFrame(msg, &ent, nument, bits);
        has_frames = true;
        if (msg->readcount + sizeof(BYTE) > msg->cursize) {
            break;
        }
        /* Buffer length is an unsigned wire byte; MSG_ReadByte retains signed-char sentinels for legacy callers. */
        ent.buffer.size = (BYTE)MSG_ReadByte(msg);
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
    /* An empty svc_layout is the wire-level clear operation.  Do not retain an
     * allocated terminator-only blob: callers use a non-NULL layer as evidence
     * that the layer exists, including generic modal ownership. */
    if (!has_frames) {
        if (Cvar_Integer("ui_layout_debug", 0)) {
            fprintf(stderr, "UI_LAYOUT_DEBUG clear layer=%u\n", (unsigned)layer);
        }
        return;
    }
    payload_size = msg->readcount - start;
    /* A terminator-only payload is the server's layer-clear operation. Keep
     * the client slot NULL so modal ownership and hit testing end immediately. */
    if (!has_frames) {
        return;
    }
    cl.layout[layer] = MemAlloc(sizeof(DWORD) + payload_size);
    memcpy(cl.layout[layer], &payload_size, sizeof(payload_size));
    memcpy((LPBYTE)cl.layout[layer] + sizeof(payload_size), msg->data + start, payload_size);
    SCR_SetLayoutLayer(layer, cl.layout[layer]);
    if (Cvar_Integer("ui_layout_debug", 0)) {
        fprintf(stderr, "UI_LAYOUT_DEBUG stored layer=%u payload=%u uiflags=0x%08x hidden=%u\n",
            (unsigned)layer, (unsigned)payload_size, (unsigned)cl.playerstate.uiflags,
            (unsigned)((cl.playerstate.uiflags & (1u << layer)) != 0));
    }
#ifdef BZ_TESTS
    {
        static BOOL quest_layout_screenshot_done;
        if (!quest_layout_screenshot_done && layer == LAYER_QUESTDIALOG
            && Cvar_Integer("wc3_quest_layout_test", 0)) {
            quest_layout_screenshot_done = true;
            Cbuf_AddText("screenshot 5\nquit\n");
        }
    }
#endif
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

/* Rebuild only the rows carried by this message; the old path rescanned the whole map after every RLE chunk. */
static void CL_UpdateFogTextureRows(DWORD first_row, DWORD row_count) {
    BYTE *texture, *visible, *explored;
    DWORD first, cells;

    if (!cl.fow.texture || !cl.fow.visible || !cl.fow.explored || first_row >= cl.fow.height) {
        return;
    }
    first = first_row * cl.fow.width;
    cells = MIN(row_count, cl.fow.height - first_row) * cl.fow.width;
    texture = cl.fow.texture + first;
    visible = cl.fow.visible + first;
    explored = cl.fow.explored + first;
    FOR_LOOP(i, cells)
        texture[i] = visible[i] ? 255 : (explored[i] ? 128 : 0);
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

/* Decode whole contiguous runs; the stream concatenates compact row ranges for each requested plane. */
static void CL_UnpackFogRLE(BYTE const *payload, DWORD payload_bytes, DWORD flags, DWORD first_row, DWORD row_count) {
    DWORD plane_bits = cl.fow.width * row_count;
    DWORD decoded = 0;
    BYTE value = payload[0] ? 1 : 0;

    for (DWORD i = 1; i < payload_bytes; i++) {
        DWORD remaining = payload[i];
        while (remaining) {
            DWORD plane_index = decoded / plane_bits;
            DWORD bit_index = decoded % plane_bits;
            DWORD count = MIN(remaining, plane_bits - bit_index);
            BYTE *plane = CL_FogPlaneForStreamIndex(flags, plane_index);
            if (plane) memset(plane + first_row * cl.fow.width + bit_index, value, count);
            decoded += count;
            remaining -= count;
        }
        if (payload[i] != 255) value = !value;
    }
}

/* Apply one validated wire chunk and report whether the caller must publish the assembled texture. */
static BOOL CL_ParseFogOfWar(LPSIZEBUF msg) {
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
        return false;
    }
    payload = msg->data + msg->readcount;
    if (!CL_ValidateFogRLE(payload, payload_bytes, expected_bits)) {
        msg->readcount = MIN(msg->cursize, msg->readcount + payload_bytes);
        return false;
    }
    if (!CL_EnsureFogOfWarSize(width, height)) {
        msg->readcount = MIN(msg->cursize, msg->readcount + payload_bytes);
        return false;
    }

    if (flags & FOW_MSG_FULL) {
        CL_ClearFogRows(cl.fow.visible, first_row, row_count);
        CL_ClearFogRows(cl.fow.explored, first_row, row_count);
    }
    CL_UnpackFogRLE(payload, payload_bytes, flags, first_row, row_count);
    msg->readcount += payload_bytes;
    CL_UpdateFogTextureRows(first_row, row_count);
    return true;
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

/* Apply an authoritative server selection to the client cache and refresh the active unit UI.
 * The payload is a packed array of entity numbers. A zero-byte payload clears
 * selection; malformed or oversized payloads are rejected without changing it. */
static void CL_ParseSetSelection(LPSIZEBUF msg) {
    DWORD count, selected = 0;

    if (!msg || (msg->cursize % sizeof(DWORD)) != 0) {
        fprintf(stderr, "CL: invalid set_selection payload (%u bytes)\n",
                msg ? (unsigned)msg->cursize : 0u);
        return;
    }
    count = msg->cursize / sizeof(DWORD);
    if (count > MAX_SELECTED_ENTITIES) {
        fprintf(stderr, "CL: oversized set_selection payload (%u entities)\n",
                (unsigned)count);
        return;
    }

    FOR_LOOP(i, count) {
        DWORD const number = (DWORD)MSG_ReadLong(msg);
        if (number > 0 && number < MAX_CLIENT_ENTITIES) {
            cl.selection.entity_nums[selected++] = number;
        }
    }
    cl.selection.num_selected = selected;
    if (ui.UpdateUnitUI) ui.UpdateUnitUI(0, NULL);
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

    if (ui.GameCommand) {
        ui.GameCommand(command, payload.data, payload.cursize);
    }
    if (!strcmp(command, "lobby_setup")) {
        CL_ParseLobbySetup(&payload);
    } else if (!strcmp(command, "lobby_chat")) {
        CL_ParseLobbyChat(&payload);
    } else if (!strcmp(command, "select")) {
        CL_ParseSetSelection(&payload);
    }

    msg->readcount = payload_start + (DWORD)payload_size;
}

/* Read the Quake 2 sound packet contract and resolve entity-relative origins
 * from the current client snapshot before handing playback to the mixer. */
static void CL_ParseSound(LPSIZEBUF msg) {
    DWORD flags = (DWORD)MSG_ReadByte(msg);
    int sound_index = MSG_ReadShort(msg), channel = 0, entity = 0;
    FLOAT volume = DEFAULT_SOUND_PACKET_VOLUME, attenuation = DEFAULT_SOUND_PACKET_ATTENUATION, timeofs = 0.0f;
    VECTOR3 origin = { 0 };
    LPCSTR path;

    if (flags & SND_VOLUME) volume = MSG_ReadByte(msg) / 255.0f;
    if (flags & SND_ATTENUATION) attenuation = MSG_ReadByte(msg) / 64.0f;
    if (flags & SND_OFFSET) timeofs = MSG_ReadByte(msg) / 1000.0f;
    if (flags & SND_ENT) {
        int packed = MSG_ReadShort(msg);
        entity = packed >> 3;
        channel = packed & 7;
        if (entity <= 0 || entity >= MAX_CLIENT_ENTITIES) {
            fprintf(stderr, "CL_ParseSound: bad entity=%d sound=%d\n", entity, sound_index);
            return;
        }
        origin = cl.ents[entity].current.origin;
    }
    if (flags & SND_POS) MSG_ReadPos(msg, &origin);
    if (sound_index <= 0 || sound_index >= MAX_SOUNDS) {
        fprintf(stderr, "CL_ParseSound: bad sound=%d flags=0x%x\n", sound_index, (unsigned)flags);
        return;
    }
    path = cl.configstrings[CS_SOUNDS + sound_index];
    if (path && path[0]) S_PlaySoundPacket(path, &origin, flags & (SND_POS | SND_ENT), channel, volume, attenuation, timeofs);
}

static void CL_ParseWindow(LPSIZEBUF msg) {
    uiWindowDef_t def;
    DWORD op = MSG_ReadByte(msg), start, frame_end, text_size, size;
    BOOL terminated = false;
    sizeBuf_t scan, validate;
    HANDLE layout;
    LPCSTR text;

    def.id = MSG_ReadLong(msg);
    if (op != UI_WINDOW_OPEN) {
        fprintf(stderr, "CL_ParseWindow: bad operation %u\n", (unsigned)op);
        msg->readcount = msg->cursize;
        return;
    }
    def.class_id = MSG_ReadLong(msg); def.flags = MSG_ReadLong(msg);
    start = msg->readcount;
    scan = *msg;
    while (scan.readcount + sizeof(DWORD) + sizeof(WORD) <= scan.cursize) {
        UIFRAME frame = { 0 };
        DWORD bits, number = MSG_ReadEntityBits(&scan, &bits);
        if (!number && !bits) { terminated = true; break; }
        if (!MSG_ReadDeltaUIWindowFrame(&scan, &frame, number, bits) || scan.readcount >= scan.cursize)
            goto malformed_window;
        DWORD payload = (BYTE)MSG_ReadByte(&scan);
        if (payload > scan.cursize - scan.readcount) goto malformed_window;
        scan.readcount += payload;
    }
    if (!terminated) goto malformed_window;
    frame_end = scan.readcount;
    if (scan.readcount + sizeof(DWORD) > scan.cursize) goto malformed_window;
    text_size = MSG_ReadLong(&scan);
    if (!text_size || text_size > scan.cursize - scan.readcount) goto malformed_window;
    text = (LPCSTR)(scan.data + scan.readcount);
    if (text[0]) goto malformed_window;
    validate = *msg; validate.cursize = frame_end; terminated = false;
    while (validate.readcount + sizeof(DWORD) + sizeof(WORD) <= validate.cursize) {
        UIFRAME frame = { 0 };
        DWORD bits, number = MSG_ReadEntityBits(&validate, &bits);
        if (!number && !bits) { terminated = true; break; }
        if (!MSG_ReadDeltaUIWindowFrame(&validate, &frame, number, bits) || validate.readcount >= validate.cursize)
            goto malformed_window;
        LPCSTR refs[] = { frame.text, frame.tooltip, frame.onclick };
        FOR_LOOP(i, 3) {
            DWORD offset = (DWORD)(uintptr_t)refs[i];
            if (offset && (offset >= text_size || !memchr(text + offset, '\0', text_size - offset))) goto malformed_window;
        }
        DWORD payload = (BYTE)MSG_ReadByte(&validate);
        if (payload > validate.cursize - validate.readcount) goto malformed_window;
        validate.readcount += payload;
    }
    if (!terminated || validate.readcount != frame_end) goto malformed_window;
    scan.readcount += text_size;
    size = scan.readcount - start;
    layout = MemAlloc(sizeof(DWORD) + size);
    memcpy(layout, &size, sizeof(size)); memcpy((LPBYTE)layout + sizeof(size), msg->data + start, size);
    msg->readcount = scan.readcount;
    CL_WindowOpen(&def, layout);
    return;

malformed_window:
    fprintf(stderr, "CL_ParseWindow: malformed window %u\n", (unsigned)def.id);
    msg->readcount = msg->cursize;
}

static void CL_ParseUIWindow(LPSIZEBUF msg) {
    char window_id[64];
    MSG_ReadStringN(msg, window_id, sizeof(window_id));
    int show = MSG_ReadByte(msg);
    if (ui.ShowWindow) ui.ShowWindow(window_id, show);
}

/* Dispatch loop for a complete server message buffer.  Each iteration reads
 * one message-type byte and calls the matching handler.  An unknown type
 * stops processing and prints an error to stderr. */
void CL_ParseServerMessage(LPSIZEBUF msg) {
    BYTE pack_id = 0;
    BOOL fow_dirty = false;
    while (MSG_Read(msg, &pack_id, 1)) {
        switch (pack_id) {
            case svc_bad:
                goto done;
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
            case svc_sound:
                CL_ParseSound(msg);
                break;
            case svc_minimap_ping:
                CL_ParseMinimapPing(msg);
                break;
            case svc_mirror:
                CL_MirrorMessage(msg);
                break;
            case svc_gamecmd:
                CL_ParseGameCommand(msg);
                break;
            case svc_fogofwar:
                fow_dirty |= CL_ParseFogOfWar(msg);
                break;
            case svc_temp_entity:
                CL_ParseTEnt(msg);
                break;                
            // Phase 8: Unit UI data
            case svc_unit_ui:
                CL_ParseUnitUI(msg);
                break;
            case svc_window:
                CL_ParseWindow(msg);
                break;
            case svc_ui_window:
                CL_ParseUIWindow(msg);
                break;
            case svc_disconnect:
                CL_Disconnect("Server disconnected.", true);
                msg->readcount = msg->cursize;
                goto done;
            default:
                fprintf(stderr, "Unknown message %d\n", pack_id);
                goto done;
        }
    }
done:
    /* FOW chunks are one logical update; publishing each chunk caused repeated full GPU texture definitions. */
    if (fow_dirty) re.SetFogOfWarData(cl.fow.width, cl.fow.height, cl.fow.texture);
}
