#ifdef WC3_SC2API

#include "sc2api_server.h"
#include "sc2api_data.h"
#include "sc2api_game.h"

#define SC2API_RESPONSE_CAPACITY (16u * 1024u * 1024u)
#define SC2API_REQUEST_QUEUE_LIMIT 64u

/* Numeric values are the upstream SC2APIProtocol.Status enum. */
enum {
    SC2_STATUS_LAUNCHED = 1,
    SC2_STATUS_INIT_GAME = 2,
    SC2_STATUS_IN_GAME = 3,
    SC2_STATUS_ENDED = 5,
    SC2_STATUS_QUIT = 6,
};

typedef enum {
    SC2_ASYNC_NONE,
    SC2_ASYNC_CREATE,
    SC2_ASYNC_RESTART,
    SC2_ASYNC_STEP,
    SC2_ASYNC_OBSERVATION,
} sc2AsyncType_t;

typedef struct sc2RequestNode_s {
    struct sc2RequestNode_s *next;
    DWORD size;
    BYTE data[];
} sc2RequestNode_t;

static struct {
    DWORD status;
    BOOL realtime;
    BOOL joined;
    BOOL observer;
    BOOL raw;
    DWORD player; /* zero-based WC3 map player */
    sc2AsyncType_t async;
    DWORD async_id;
    BOOL async_has_id;
    BOOL response_has_id;
    DWORD requested_steps;
    DWORD observation_loop;
    PATHSTR map_path;
    sc2RequestNode_t *queue_head, *queue_tail;
    DWORD queue_count;
    LPBYTE response;
    BOOL transport_connected;
    BOOL queue_overflow;
    BOOL results_reported;
    uint64_t dead_units[MAX_ENTITIES];
    DWORD dead_unit_count;
    BOOL camera_set;
    VECTOR3 camera; /* SC2 board coordinates; external-only virtual camera. */
} session = { .status = SC2_STATUS_LAUNCHED };

static void sc2_free_request(sc2RequestNode_t *node) {
    if (node) gi.MemFree(node);
}

static void sc2_clear_queue(void) {
    sc2RequestNode_t *node = session.queue_head;
    while (node) {
        sc2RequestNode_t *next = node->next;
        sc2_free_request(node);
        node = next;
    }
    session.queue_head = session.queue_tail = NULL;
    session.queue_count = 0;
}

static void sc2_reset_connection_state(void) {
    LPBYTE response = session.response;
    BOOL connected = session.transport_connected;

    sc2_clear_queue();
    memset(&session, 0, sizeof(session));
    session.status = SC2_STATUS_LAUNCHED;
    session.response = response;
    session.transport_connected = connected;
}

void WC3_SC2API_RecordDeath(LPCEDICT ent) {
    DWORD owner;
    BOOL known = false;
    uint64_t tag;

    if (!ent || !ent->inuse) return;
    tag = WC3_SC2API_UnitTag(ent);
    owner = ent->s.player;
    if (session.joined && tag && session.dead_unit_count < MAX_ENTITIES) {
        /* Own losses are always known. Other deaths must be actively visible to
         * the represented player; alliance alone must not leak an unseen ally's
         * death through Raw Event.dead_units. Shared vision naturally reaches the
         * hover-visible path. */
        known = owner == session.player || G_FowPlayerCanHoverEntity(session.player, ent);
        if (known) {
            BOOL duplicate = false;
            FOR_LOOP(i, session.dead_unit_count) if (session.dead_units[i] == tag) { duplicate = true; break; }
            if (!duplicate) session.dead_units[session.dead_unit_count++] = tag;
        }
    }
    /* Warcraft resurrection can restore this same edict/JASS handle. SC2 dead-unit
     * tags are terminal identities, so retire the adapter tag at every gameplay
     * death; a later resurrection is exposed as a fresh SC2 incarnation. */
    WC3_SC2API_AdvanceUnitTagLifetime((LPEDICT)ent);
}

static void sc2_enqueue_request(BYTE const *data, DWORD size) {
    sc2RequestNode_t *node;
    if (!data || !size) return;
    if (session.queue_count >= SC2API_REQUEST_QUEUE_LIMIT) { session.queue_overflow = true; return; }
    node = gi.MemAlloc(sizeof(*node) + size);
    if (!node) { session.queue_overflow = true; return; }
    node->next = NULL;
    node->size = size;
    memcpy(node->data, data, size);
    if (session.queue_tail) session.queue_tail->next = node;
    else session.queue_head = node;
    session.queue_tail = node;
    session.queue_count++;
}

static sc2RequestNode_t *sc2_pop_request(void) {
    sc2RequestNode_t *node = session.queue_head;
    if (!node) return NULL;
    session.queue_head = node->next;
    if (!session.queue_head) session.queue_tail = NULL;
    node->next = NULL;
    session.queue_count--;
    return node;
}

static BOOL sc2_ensure_response_buffer(void) {
    if (!session.response) session.response = gi.MemAlloc(SC2API_RESPONSE_CAPACITY);
    return session.response != NULL;
}

static wc3Sc2PbMessageMark_t sc2_response_begin(wc3Sc2PbWriter_t *writer, DWORD field) {
    WC3_SC2API_PbWriterInit(writer, session.response, SC2API_RESPONSE_CAPACITY);
    return WC3_SC2API_PbBeginMessage(writer, field);
}

static BOOL sc2_response_finish(wc3Sc2PbWriter_t *writer, wc3Sc2PbMessageMark_t mark,
                                DWORD id, LPCSTR error) {
    if (!WC3_SC2API_PbEndMessage(writer, mark)) return false;
    if (session.response_has_id) WC3_SC2API_PbWriteVarintField(writer, 97, id);
    if (error && *error) WC3_SC2API_PbWriteStringField(writer, 98, error);
    WC3_SC2API_PbWriteVarintField(writer, 99, session.status);
    if (writer->failed) return false;
    return WC3_SC2API_TransportSend(writer->data, writer->size);
}

static BOOL sc2_send_empty(DWORD response_field, DWORD id, LPCSTR error) {
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t mark;
    if (!sc2_ensure_response_buffer()) return false;
    mark = sc2_response_begin(&writer, response_field);
    return sc2_response_finish(&writer, mark, id, error);
}

static BOOL sc2_read_string(wc3Sc2PbReader_t *reader, LPSTR out, DWORD out_size) {
    BYTE const *data;
    DWORD size;
    if (!out || !out_size || !WC3_SC2API_PbReadBytes(reader, &data, &size)) return false;
    size = MIN(size, out_size - 1);
    memcpy(out, data, size);
    out[size] = '\0';
    return true;
}

static BOOL sc2_parse_point2d(wc3Sc2PbReader_t *reader, LPVECTOR2 point) {
    DWORD field, wire;
    if (!reader || !point) return false;
    *point = (VECTOR2){ 0 };
    while (WC3_SC2API_PbNext(reader, &field, &wire)) {
        if ((field == 1 || field == 2) && wire == 5) {
            FLOAT value;
            if (!WC3_SC2API_PbReadFloat(reader, &value)) return false;
            if (field == 1) point->x = value; else point->y = value;
        } else if (!WC3_SC2API_PbSkip(reader, wire)) return false;
    }
    return !reader->failed;
}

static BOOL sc2_parse_point(wc3Sc2PbReader_t *reader, LPVECTOR3 point) {
    DWORD field, wire;
    if (!reader || !point) return false;
    *point = (VECTOR3){ 0 };
    while (WC3_SC2API_PbNext(reader, &field, &wire)) {
        if (field >= 1 && field <= 3 && wire == 5) {
            FLOAT value;
            if (!WC3_SC2API_PbReadFloat(reader, &value)) return false;
            if (field == 1) point->x = value;
            else if (field == 2) point->y = value;
            else point->z = value;
        } else if (!WC3_SC2API_PbSkip(reader, wire)) return false;
    }
    return !reader->failed;
}

static void sc2_write_point2d(wc3Sc2PbWriter_t *writer, DWORD field, LPCVECTOR2 point) {
    wc3Sc2PbMessageMark_t mark = WC3_SC2API_PbBeginMessage(writer, field);
    WC3_SC2API_PbWriteFloatField(writer, 1, point->x);
    WC3_SC2API_PbWriteFloatField(writer, 2, point->y);
    WC3_SC2API_PbEndMessage(writer, mark);
}

static void sc2_write_point(wc3Sc2PbWriter_t *writer, DWORD field, LPCVECTOR3 point) {
    wc3Sc2PbMessageMark_t mark = WC3_SC2API_PbBeginMessage(writer, field);
    WC3_SC2API_PbWriteFloatField(writer, 1, point->x);
    WC3_SC2API_PbWriteFloatField(writer, 2, point->y);
    WC3_SC2API_PbWriteFloatField(writer, 3, point->z);
    WC3_SC2API_PbEndMessage(writer, mark);
}

static void sc2_write_pointi(wc3Sc2PbWriter_t *writer, DWORD field, wc3Sc2PointI_t const *point) {
    wc3Sc2PbMessageMark_t mark = WC3_SC2API_PbBeginMessage(writer, field);
    WC3_SC2API_PbWriteSInt32Field(writer, 1, point->x);
    WC3_SC2API_PbWriteSInt32Field(writer, 2, point->y);
    WC3_SC2API_PbEndMessage(writer, mark);
}

static void sc2_write_size2di(wc3Sc2PbWriter_t *writer, DWORD field, wc3Sc2Size2DI_t const *size) {
    wc3Sc2PbMessageMark_t mark = WC3_SC2API_PbBeginMessage(writer, field);
    WC3_SC2API_PbWriteSInt32Field(writer, 1, size->x);
    WC3_SC2API_PbWriteSInt32Field(writer, 2, size->y);
    WC3_SC2API_PbEndMessage(writer, mark);
}

static void sc2_write_image(wc3Sc2PbWriter_t *writer, DWORD field, wc3Sc2ImageData_t const *image) {
    wc3Sc2PbMessageMark_t mark = WC3_SC2API_PbBeginMessage(writer, field);
    wc3Sc2Size2DI_t size = { image->width, image->height };
    WC3_SC2API_PbWriteSInt32Field(writer, 1, (LONG)image->bits_per_pixel);
    sc2_write_size2di(writer, 2, &size);
    WC3_SC2API_PbWriteBytesField(writer, 3, image->data, image->data_size);
    WC3_SC2API_PbEndMessage(writer, mark);
}

static void sc2_write_start_raw(wc3Sc2PbWriter_t *writer, wc3Sc2StartRaw_t const *raw) {
    sc2_write_size2di(writer, 1, &raw->map_size);
    sc2_write_image(writer, 2, &raw->pathing_grid);
    sc2_write_image(writer, 3, &raw->terrain_height);
    sc2_write_image(writer, 4, &raw->placement_grid);
    {
        wc3Sc2PbMessageMark_t rect = WC3_SC2API_PbBeginMessage(writer, 5);
        sc2_write_pointi(writer, 1, &raw->playable_area.p0);
        sc2_write_pointi(writer, 2, &raw->playable_area.p1);
        WC3_SC2API_PbEndMessage(writer, rect);
    }
    FOR_LOOP(i, raw->start_location_count) sc2_write_point2d(writer, 6, &raw->start_locations[i]);
}

static void sc2_write_player_common(wc3Sc2PbWriter_t *writer, wc3Sc2PlayerCommon_t const *common) {
    WC3_SC2API_PbWriteVarintField(writer, 1, common->player_id);
    WC3_SC2API_PbWriteVarintField(writer, 2, common->minerals);
    WC3_SC2API_PbWriteVarintField(writer, 3, common->vespene);
    WC3_SC2API_PbWriteVarintField(writer, 4, common->food_cap);
    WC3_SC2API_PbWriteVarintField(writer, 5, common->food_used);
    WC3_SC2API_PbWriteVarintField(writer, 6, common->food_army);
    WC3_SC2API_PbWriteVarintField(writer, 7, common->food_workers);
    WC3_SC2API_PbWriteVarintField(writer, 8, common->idle_worker_count);
    WC3_SC2API_PbWriteVarintField(writer, 9, common->army_count);
}

static void sc2_write_raw_unit(wc3Sc2PbWriter_t *writer, wc3Sc2RawUnit_t const *unit) {
    WC3_SC2API_PbWriteVarintField(writer, 1, unit->display_type);
    WC3_SC2API_PbWriteVarintField(writer, 2, unit->alliance);
    WC3_SC2API_PbWriteVarintField(writer, 3, unit->tag);
    WC3_SC2API_PbWriteVarintField(writer, 4, unit->unit_type);
    WC3_SC2API_PbWriteSInt32Field(writer, 5, unit->owner);
    sc2_write_point(writer, 6, &unit->pos);
    WC3_SC2API_PbWriteFloatField(writer, 7, unit->facing);
    WC3_SC2API_PbWriteFloatField(writer, 8, unit->radius);
    if (unit->has_build_progress) WC3_SC2API_PbWriteFloatField(writer, 9, unit->build_progress);
    WC3_SC2API_PbWriteVarintField(writer, 10, unit->cloak);
    WC3_SC2API_PbWriteBoolField(writer, 11, unit->is_selected);
    FOR_LOOP(i, unit->buff_count) WC3_SC2API_PbWriteVarintField(writer, 27, unit->buff_ids[i]);
    if (!unit->has_live_data) return;
    WC3_SC2API_PbWriteFloatField(writer, 14, unit->health);
    WC3_SC2API_PbWriteFloatField(writer, 15, unit->health_max);
    WC3_SC2API_PbWriteFloatField(writer, 17, unit->energy);
    WC3_SC2API_PbWriteSInt32Field(writer, 18, unit->mineral_contents);
    WC3_SC2API_PbWriteSInt32Field(writer, 19, unit->vespene_contents);
    WC3_SC2API_PbWriteBoolField(writer, 20, unit->is_flying);
    WC3_SC2API_PbWriteFloatField(writer, 37, unit->energy_max);
    WC3_SC2API_PbWriteBoolField(writer, 39, unit->is_active);
    if (unit->has_attack_upgrade_level) WC3_SC2API_PbWriteSInt32Field(writer, 40, unit->attack_upgrade_level);
    if (unit->has_armor_upgrade_level) WC3_SC2API_PbWriteSInt32Field(writer, 41, unit->armor_upgrade_level);
    FOR_LOOP(i, unit->order_count) {
        wc3Sc2PbMessageMark_t order = WC3_SC2API_PbBeginMessage(writer, 22);
        WC3_SC2API_PbWriteVarintField(writer, 1, unit->orders[i].ability_id);
        if (unit->orders[i].has_point)
            sc2_write_point(writer, 2, &(VECTOR3){ unit->orders[i].point.x, unit->orders[i].point.y, 0 });
        else if (unit->orders[i].has_target_unit)
            WC3_SC2API_PbWriteVarintField(writer, 3, unit->orders[i].target_unit_tag);
        WC3_SC2API_PbEndMessage(writer, order);
    }
    FOR_LOOP(i, unit->passenger_count) {
        wc3Sc2PassengerUnit_t const *passenger = &unit->passengers[i];
        wc3Sc2PbMessageMark_t p = WC3_SC2API_PbBeginMessage(writer, 24);
        WC3_SC2API_PbWriteVarintField(writer, 1, passenger->tag);
        WC3_SC2API_PbWriteFloatField(writer, 2, passenger->health);
        WC3_SC2API_PbWriteFloatField(writer, 3, passenger->health_max);
        WC3_SC2API_PbWriteFloatField(writer, 5, passenger->energy);
        WC3_SC2API_PbWriteVarintField(writer, 6, passenger->unit_type);
        WC3_SC2API_PbWriteFloatField(writer, 8, passenger->energy_max);
        WC3_SC2API_PbEndMessage(writer, p);
    }
    WC3_SC2API_PbWriteSInt32Field(writer, 25, unit->cargo_space_taken);
    WC3_SC2API_PbWriteSInt32Field(writer, 26, unit->cargo_space_max);
    FOR_LOOP(i, unit->rally_target_count) {
        wc3Sc2RallyTarget_t const *rally = &unit->rally_targets[i];
        wc3Sc2PbMessageMark_t target = WC3_SC2API_PbBeginMessage(writer, 45);
        sc2_write_point(writer, 1, &rally->point);
        if (rally->has_tag) WC3_SC2API_PbWriteVarintField(writer, 2, rally->tag);
        WC3_SC2API_PbEndMessage(writer, target);
    }
}

static void sc2_write_available_map(LPCSTR path, void *userData) {
    wc3Sc2PbWriter_t *writer = userData;
    if (!writer || writer->failed || !path || !*path) return;
    WC3_SC2API_PbWriteStringField(writer, 1, path);
}

static BOOL sc2_send_available_maps(DWORD id) {
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t mark;

    if (!sc2_ensure_response_buffer()) return false;
    mark = sc2_response_begin(&writer, 17);
    if (gi.ListMaps) gi.ListMaps(sc2_write_available_map, &writer);
    return sc2_response_finish(&writer, mark, id, NULL);
}

static BOOL sc2_send_ping(DWORD id) {
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t mark;
    if (!sc2_ensure_response_buffer()) return false;
    mark = sc2_response_begin(&writer, 19);
    WC3_SC2API_PbWriteStringField(&writer, 1, "OpenRealm Warcraft III SC2API");
    WC3_SC2API_PbWriteStringField(&writer, 2, "warcraft-3");
    WC3_SC2API_PbWriteVarintField(&writer, 3, 0);
    WC3_SC2API_PbWriteVarintField(&writer, 4, 0);
    return sc2_response_finish(&writer, mark, id, NULL);
}

static BOOL sc2_send_create_response(DWORD id, DWORD error, LPCSTR details) {
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t mark;
    if (!sc2_ensure_response_buffer()) return false;
    mark = sc2_response_begin(&writer, 1);
    if (error) WC3_SC2API_PbWriteVarintField(&writer, 1, error);
    if (details && *details) WC3_SC2API_PbWriteStringField(&writer, 2, details);
    return sc2_response_finish(&writer, mark, id, NULL);
}

static BOOL sc2_send_join_response(DWORD id, DWORD player_id, DWORD error, LPCSTR details) {
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t mark;
    if (!sc2_ensure_response_buffer()) return false;
    mark = sc2_response_begin(&writer, 2);
    if (player_id) WC3_SC2API_PbWriteVarintField(&writer, 1, player_id);
    if (error) WC3_SC2API_PbWriteVarintField(&writer, 2, error);
    if (details && *details) WC3_SC2API_PbWriteStringField(&writer, 3, details);
    return sc2_response_finish(&writer, mark, id, NULL);
}

static BOOL sc2_send_step_response(DWORD id) {
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t mark;
    if (!sc2_ensure_response_buffer()) return false;
    mark = sc2_response_begin(&writer, 12);
    WC3_SC2API_PbWriteVarintField(&writer, 1, level.framenum);
    return sc2_response_finish(&writer, mark, id, NULL);
}

static BOOL sc2_send_game_info(DWORD id) {
    wc3Sc2GameInfo_t info;
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t response;
    LPBYTE pathing = NULL, terrain = NULL, placement = NULL;
    wc3Sc2StartRaw_t start_raw;

    if (!WC3_SC2API_FillGameInfo(&info) || !sc2_ensure_response_buffer()) return sc2_send_empty(9, id, "game info unavailable");
    response = sc2_response_begin(&writer, 9);
    WC3_SC2API_PbWriteStringField(&writer, 1, info.map_name ? info.map_name : "");
    WC3_SC2API_PbWriteStringField(&writer, 2, info.local_map_path ? info.local_map_path : "");
    FOR_LOOP(i, info.player_count) {
        wc3Sc2PlayerInfo_t const *player = &info.players[i];
        wc3Sc2PbMessageMark_t item = WC3_SC2API_PbBeginMessage(&writer, 3);
        WC3_SC2API_PbWriteVarintField(&writer, 1, player->player_id);
        WC3_SC2API_PbWriteVarintField(&writer, 2, player->type);
        WC3_SC2API_PbWriteVarintField(&writer, 3, player->race_requested);
        if (!session.observer && player->player_id == session.player + 1)
            WC3_SC2API_PbWriteVarintField(&writer, 4, player->race_actual);
        WC3_SC2API_PbWriteStringField(&writer, 6, player->player_name ? player->player_name : "");
        WC3_SC2API_PbEndMessage(&writer, item);
    }
    if (session.raw) {
        DWORD path_size = WC3_SC2API_PathingDataSize();
        DWORD terrain_size = WC3_SC2API_TerrainHeightDataSize();
        DWORD placement_size = WC3_SC2API_PlacementDataSize();
        pathing = gi.MemAlloc(path_size); terrain = gi.MemAlloc(terrain_size); placement = gi.MemAlloc(placement_size);
        if (pathing && terrain && placement &&
            WC3_SC2API_FillStartRaw(pathing, path_size, terrain, terrain_size, placement, placement_size, &start_raw)) {
            wc3Sc2PbMessageMark_t raw = WC3_SC2API_PbBeginMessage(&writer, 4);
            sc2_write_start_raw(&writer, &start_raw);
            WC3_SC2API_PbEndMessage(&writer, raw);
        }
    }
    {
        wc3Sc2PbMessageMark_t options = WC3_SC2API_PbBeginMessage(&writer, 5);
        WC3_SC2API_PbWriteBoolField(&writer, 1, session.raw);
        WC3_SC2API_PbEndMessage(&writer, options);
    }
    SAFE_DELETE(pathing, gi.MemFree); SAFE_DELETE(terrain, gi.MemFree); SAFE_DELETE(placement, gi.MemFree);
    return sc2_response_finish(&writer, response, id, NULL);
}

static BOOL sc2_update_terminal_status(void) {
    wc3Sc2PlayerResult_t result;

    if (!session.joined || session.status == SC2_STATUS_ENDED) return session.status == SC2_STATUS_ENDED;
    if (!WC3_SC2API_FillPlayerResult(session.player, &result)) return false;
    session.status = SC2_STATUS_ENDED;
    return true;
}

static BOOL sc2_send_observation(DWORD id) {
    wc3Sc2Observation_t observation;
    wc3Sc2RawUnit_t *units = NULL;
    LPBYTE visibility = NULL, creep = NULL;
    DWORD unit_capacity = session.raw ? globals.num_edicts : 0;
    DWORD visibility_size = session.raw ? WC3_SC2API_VisibilityDataSize() : 0;
    DWORD creep_size = session.raw ? WC3_SC2API_CreepDataSize() : 0;
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t response, obs, raw, player_raw;

    if (!session.joined || !sc2_ensure_response_buffer()) return sc2_send_empty(10, id, "not joined");
    if (unit_capacity) units = gi.MemAlloc(sizeof(*units) * unit_capacity);
    if (visibility_size) visibility = gi.MemAlloc(visibility_size);
    if (creep_size) creep = gi.MemAlloc(creep_size);
    if ((unit_capacity && !units) || (visibility_size && !visibility) || (creep_size && !creep) ||
        (session.raw
            ? !WC3_SC2API_BuildObservationWithMapState(session.player, &observation, units, unit_capacity,
                                                       visibility, visibility_size, creep, creep_size)
            : !WC3_SC2API_BuildObservation(session.player, &observation, NULL, 0))) {
        SAFE_DELETE(units, gi.MemFree); SAFE_DELETE(visibility, gi.MemFree); SAFE_DELETE(creep, gi.MemFree);
        return sc2_send_empty(10, id, "observation unavailable");
    }

    response = sc2_response_begin(&writer, 10);
    obs = WC3_SC2API_PbBeginMessage(&writer, 3);
    {
        wc3Sc2PbMessageMark_t common = WC3_SC2API_PbBeginMessage(&writer, 1);
        sc2_write_player_common(&writer, &observation.player_common);
        WC3_SC2API_PbEndMessage(&writer, common);
    }
    if (session.raw) {
        raw = WC3_SC2API_PbBeginMessage(&writer, 5);
        player_raw = WC3_SC2API_PbBeginMessage(&writer, 1);
        if (session.camera_set) sc2_write_point(&writer, 2, &session.camera);
        FOR_LOOP(i, observation.upgrade_count) WC3_SC2API_PbWriteVarintField(&writer, 3, observation.upgrade_ids[i]);
        WC3_SC2API_PbEndMessage(&writer, player_raw);
        FOR_LOOP(i, observation.raw_unit_count) {
            wc3Sc2PbMessageMark_t unit = WC3_SC2API_PbBeginMessage(&writer, 2);
            sc2_write_raw_unit(&writer, &units[i]);
            WC3_SC2API_PbEndMessage(&writer, unit);
        }
        if (observation.has_map_state) {
            wc3Sc2PbMessageMark_t map_state = WC3_SC2API_PbBeginMessage(&writer, 3);
            if (observation.map_state.has_visibility) sc2_write_image(&writer, 1, &observation.map_state.visibility);
            if (observation.map_state.has_creep) sc2_write_image(&writer, 2, &observation.map_state.creep);
            WC3_SC2API_PbEndMessage(&writer, map_state);
        }
        if (session.dead_unit_count) {
            wc3Sc2PbMessageMark_t event = WC3_SC2API_PbBeginMessage(&writer, 4);
            FOR_LOOP(i, session.dead_unit_count)
                WC3_SC2API_PbWriteVarintField(&writer, 1, session.dead_units[i]);
            WC3_SC2API_PbEndMessage(&writer, event);
        }
        WC3_SC2API_PbEndMessage(&writer, raw);
    }
    WC3_SC2API_PbWriteVarintField(&writer, 9, observation.game_loop);
    WC3_SC2API_PbEndMessage(&writer, obs);

    if (sc2_update_terminal_status() && !session.results_reported) {
        FOR_LOOP(player, MAX_PLAYERS) {
            wc3Sc2PlayerResult_t result;
            if (!WC3_SC2API_FillPlayerResult(player, &result)) continue;
            wc3Sc2PbMessageMark_t item = WC3_SC2API_PbBeginMessage(&writer, 4);
            WC3_SC2API_PbWriteVarintField(&writer, 1, result.player_id);
            WC3_SC2API_PbWriteVarintField(&writer, 2, result.result);
            WC3_SC2API_PbEndMessage(&writer, item);
        }
        session.results_reported = true;
    }

    SAFE_DELETE(units, gi.MemFree); SAFE_DELETE(visibility, gi.MemFree); SAFE_DELETE(creep, gi.MemFree);
    {
        BOOL const sent = sc2_response_finish(&writer, response, id, NULL);
        if (sent && session.raw) session.dead_unit_count = 0;
        return sent;
    }
}

static DWORD sc2_ability_target_type(ability_t const *ability, LPCSTR order_name) {
    if (!ability) {
        if (order_name && (!strcmp(order_name, "smart") || !strcmp(order_name, "attack") || !strcmp(order_name, "rally"))) return 4;
        if (order_name && (!strcmp(order_name, "move") || !strcmp(order_name, "patrol") || !strcmp(order_name, "attackground"))) return 2;
        return 1; /* AbilityData.Target.None */
    }
    switch (ability->target_type) {
        case SPELL_TARGET_POINT: return 2;
        case SPELL_TARGET_UNIT: return 3;
        case SPELL_TARGET_UNIT_OR_POINT: return 4;
        default: return 1;
    }
}

static void sc2_write_ability_data(wc3Sc2PbWriter_t *writer, DWORD ability_id, LPCSTR name,
                                   ability_t const *ability, DWORD ability_code, BOOL is_building) {
    wc3Sc2PbMessageMark_t item;
    LPCSTR object_name = ability_code ? G_ObjectName(ability_code) : NULL;
    LPCSTR friendly = object_name && *object_name ? object_name : name;
    FLOAT cast_range = 0.0f;

    if (!writer || !ability_id) return;
    if (ability_code) cast_range = WC3_SC2API_WorldToBoardDistance(MAX(0.0f, G_AbilityLevel(ability_code, 1)->range));
    item = WC3_SC2API_PbBeginMessage(writer, 1);
    WC3_SC2API_PbWriteVarintField(writer, 1, ability_id);
    WC3_SC2API_PbWriteStringField(writer, 2, name ? name : "");
    WC3_SC2API_PbWriteStringField(writer, 4, friendly ? friendly : "");
    WC3_SC2API_PbWriteStringField(writer, 5, friendly ? friendly : "");
    WC3_SC2API_PbWriteBoolField(writer, 8, true);
    WC3_SC2API_PbWriteVarintField(writer, 9, is_building ? 2 : sc2_ability_target_type(ability, name));
    if (ability && (ability->flags & AB_AUTOCAST)) WC3_SC2API_PbWriteBoolField(writer, 11, true);
    if (is_building) WC3_SC2API_PbWriteBoolField(writer, 12, true);
    if (cast_range > 0.0f) WC3_SC2API_PbWriteFloatField(writer, 15, cast_range);
    WC3_SC2API_PbEndMessage(writer, item);
}

static BOOL sc2_order_definition_has_id(DWORD wanted) {
    FOR_LOOP(i, G_OrderDefinitionCount()) {
        DWORD id = 0;
        if (G_OrderDefinitionInfo(i, &id, NULL, NULL) && id == wanted) return true;
    }
    return false;
}

static BOOL sc2_send_data(DWORD id, BOOL abilities, BOOL units, BOOL upgrades, BOOL buffs, BOOL effects) {
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t response;
    if (!sc2_ensure_response_buffer()) return false;
    response = sc2_response_begin(&writer, 13);

    if (abilities) {
        FOR_LOOP(i, G_OrderDefinitionCount()) {
            DWORD order_id = 0, ability_code = 0;
            LPCSTR name = NULL;
            abilityitem_t item;
            if (!G_OrderDefinitionInfo(i, &order_id, &name, &ability_code) || !order_id || !name) continue;
            item = ability_code ? S_AbilityItem(ability_code) : (abilityitem_t){ 0 };
            sc2_write_ability_data(&writer, order_id, name, item.ability, ability_code, false);
        }
        {
            DWORD capacity = WC3_SC2API_UnitTypeDataCapacity();
            wc3Sc2UnitTypeData_t *rows = capacity ? gi.MemAlloc(sizeof(*rows) * capacity) : NULL;
            DWORD count = rows ? WC3_SC2API_BuildUnitTypeData(rows, capacity) : 0;
            FOR_LOOP(i, count) {
                if (!rows[i].has_ability_id || sc2_order_definition_has_id(rows[i].unit_type_id)) continue;
                sc2_write_ability_data(&writer, rows[i].unit_type_id, rows[i].name, NULL, 0, rows[i].is_structure);
            }
            SAFE_DELETE(rows, gi.MemFree);
        }
        {
            DWORD capacity = WC3_SC2API_UpgradeDataCapacity();
            wc3Sc2UpgradeData_t *rows = capacity ? gi.MemAlloc(sizeof(*rows) * capacity) : NULL;
            DWORD count = rows ? WC3_SC2API_BuildUpgradeData(rows, capacity) : 0;
            FOR_LOOP(i, count) {
                if (sc2_order_definition_has_id(rows[i].ability_id)) continue;
                sc2_write_ability_data(&writer, rows[i].ability_id, rows[i].name, NULL, 0, false);
            }
            SAFE_DELETE(rows, gi.MemFree);
        }
    }
    if (units) {
        DWORD capacity = WC3_SC2API_UnitTypeDataCapacity();
        wc3Sc2UnitTypeData_t *rows = capacity ? gi.MemAlloc(sizeof(*rows) * capacity) : NULL;
        DWORD count = rows ? WC3_SC2API_BuildUnitTypeData(rows, capacity) : 0;
        FOR_LOOP(i, count) {
            wc3Sc2UnitTypeData_t const *row = &rows[i];
            wc3Sc2PbMessageMark_t item = WC3_SC2API_PbBeginMessage(&writer, 2);
            WC3_SC2API_PbWriteVarintField(&writer, 1, row->unit_type_id);
            WC3_SC2API_PbWriteStringField(&writer, 2, row->name ? row->name : "");
            WC3_SC2API_PbWriteBoolField(&writer, 3, row->available);
            WC3_SC2API_PbWriteVarintField(&writer, 4, row->cargo_size);
            if (row->is_structure) WC3_SC2API_PbWriteVarintField(&writer, 8, 8); /* Attribute.Structure */
            if (row->is_heroic) WC3_SC2API_PbWriteVarintField(&writer, 8, 10); /* Attribute.Heroic */
            WC3_SC2API_PbWriteFloatField(&writer, 9, row->movement_speed);
            WC3_SC2API_PbWriteFloatField(&writer, 10, row->armor);
            FOR_LOOP(w, row->weapon_count) {
                wc3Sc2WeaponData_t const *weapon = &row->weapons[w];
                wc3Sc2PbMessageMark_t weapon_msg = WC3_SC2API_PbBeginMessage(&writer, 11);
                WC3_SC2API_PbWriteVarintField(&writer, 1, weapon->target);
                WC3_SC2API_PbWriteFloatField(&writer, 2, weapon->damage);
                WC3_SC2API_PbWriteVarintField(&writer, 4, weapon->attacks);
                WC3_SC2API_PbWriteFloatField(&writer, 5, weapon->range);
                WC3_SC2API_PbWriteFloatField(&writer, 6, weapon->speed);
                WC3_SC2API_PbEndMessage(&writer, weapon_msg);
            }
            WC3_SC2API_PbWriteVarintField(&writer, 12, MAX(0, row->mineral_cost));
            WC3_SC2API_PbWriteVarintField(&writer, 13, MAX(0, row->vespene_cost));
            WC3_SC2API_PbWriteFloatField(&writer, 14, row->food_required);
            /* Building/train action ids use the stable WC3 unit rawcode. Neutral
             * destructables are observation-only catalog rows and have no build ability. */
            if (row->has_ability_id) WC3_SC2API_PbWriteVarintField(&writer, 15, row->unit_type_id);
            WC3_SC2API_PbWriteVarintField(&writer, 16, row->race);
            WC3_SC2API_PbWriteFloatField(&writer, 17, row->build_time_seconds);
            WC3_SC2API_PbWriteFloatField(&writer, 18, row->food_provided);
            WC3_SC2API_PbWriteFloatField(&writer, 25, row->sight_range);
            WC3_SC2API_PbEndMessage(&writer, item);
        }
        SAFE_DELETE(rows, gi.MemFree);
    }
    if (upgrades) {
        DWORD capacity = WC3_SC2API_UpgradeDataCapacity();
        wc3Sc2UpgradeData_t *rows = capacity ? gi.MemAlloc(sizeof(*rows) * capacity) : NULL;
        DWORD count = rows ? WC3_SC2API_BuildUpgradeData(rows, capacity) : 0;
        FOR_LOOP(i, count) {
            wc3Sc2UpgradeData_t const *row = &rows[i];
            wc3Sc2PbMessageMark_t item = WC3_SC2API_PbBeginMessage(&writer, 3);
            WC3_SC2API_PbWriteVarintField(&writer, 1, row->upgrade_id);
            WC3_SC2API_PbWriteStringField(&writer, 2, row->name ? row->name : "");
            WC3_SC2API_PbWriteVarintField(&writer, 3, MAX(0, row->mineral_cost));
            WC3_SC2API_PbWriteVarintField(&writer, 4, MAX(0, row->vespene_cost));
            WC3_SC2API_PbWriteFloatField(&writer, 5, row->research_time);
            WC3_SC2API_PbWriteVarintField(&writer, 6, row->ability_id);
            WC3_SC2API_PbEndMessage(&writer, item);
        }
        SAFE_DELETE(rows, gi.MemFree);
    }
    if (buffs) {
        DWORD capacity = WC3_SC2API_BuffDataCapacity();
        wc3Sc2BuffData_t *rows = capacity ? gi.MemAlloc(sizeof(*rows) * capacity) : NULL;
        DWORD count = rows ? WC3_SC2API_BuildBuffData(rows, capacity) : 0;
        FOR_LOOP(i, count) {
            wc3Sc2PbMessageMark_t item = WC3_SC2API_PbBeginMessage(&writer, 4);
            WC3_SC2API_PbWriteVarintField(&writer, 1, rows[i].buff_id);
            WC3_SC2API_PbWriteStringField(&writer, 2, rows[i].name ? rows[i].name : "");
            WC3_SC2API_PbEndMessage(&writer, item);
        }
        SAFE_DELETE(rows, gi.MemFree);
    }
    return sc2_response_finish(&writer, response, id,
                               effects ? "EffectData is not supported" : NULL);
}

static wc3Sc2ActionResult_t sc2_parse_raw_command(wc3Sc2PbReader_t *reader) {
    wc3Sc2RawUnitCommand_t command = { 0 };
    uint64_t *tags = NULL;
    DWORD tag_count = 0, tag_capacity = MAX(1, globals.max_edicts);
    DWORD field, wire;
    wc3Sc2ActionResult_t aggregate = WC3_SC2_ACTION_SUCCESS;

    tags = gi.MemAlloc(sizeof(*tags) * tag_capacity);
    if (!tags) return WC3_SC2_ACTION_ERROR;
    while (WC3_SC2API_PbNext(reader, &field, &wire)) {
        if (field == 1 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(reader, &value)) break; command.ability_id = (DWORD)value;
        } else if (field == 2 && wire == 2) {
            wc3Sc2PbReader_t point; if (!WC3_SC2API_PbReadSubmessage(reader, &point) || !sc2_parse_point2d(&point, &command.target_point)) break;
            command.target_type = WC3_SC2_TARGET_POINT;
        } else if (field == 3 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(reader, &value)) break; command.target_unit_tag = value; command.target_type = WC3_SC2_TARGET_UNIT;
        } else if (field == 4 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(reader, &value)) break;
            if (tag_count < tag_capacity) tags[tag_count++] = value;
            else aggregate = WC3_SC2_ACTION_ERROR;
        } else if (field == 5 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(reader, &value)) break; command.queue_command = value != 0;
        } else if (!WC3_SC2API_PbSkip(reader, wire)) break;
    }
    if (reader->failed || !command.ability_id || !tag_count) aggregate = WC3_SC2_ACTION_ERROR;
    if (aggregate == WC3_SC2_ACTION_SUCCESS) {
        FOR_LOOP(i, tag_count) {
            wc3Sc2ActionResult_t result;
            command.unit_tag = tags[i];
            result = WC3_SC2API_IssueRawUnitCommand(session.player, &command);
            if (result != WC3_SC2_ACTION_SUCCESS && aggregate == WC3_SC2_ACTION_SUCCESS) aggregate = result;
        }
    }
    gi.MemFree(tags);
    return aggregate;
}

static wc3Sc2ActionResult_t sc2_parse_raw_camera(wc3Sc2PbReader_t *reader) {
    DWORD field, wire;
    BOOL has_center = false;
    VECTOR3 center = { 0 };
    while (WC3_SC2API_PbNext(reader, &field, &wire)) {
        if (field == 1 && wire == 2) {
            wc3Sc2PbReader_t point;
            if (!WC3_SC2API_PbReadSubmessage(reader, &point) || !sc2_parse_point(&point, &center)) break;
            has_center = true;
        } else if (!WC3_SC2API_PbSkip(reader, wire)) break;
    }
    if (reader->failed || !has_center) return WC3_SC2_ACTION_ERROR;
    session.camera = center;
    session.camera_set = true;
    return WC3_SC2_ACTION_SUCCESS;
}

static wc3Sc2ActionResult_t sc2_parse_raw_autocast(wc3Sc2PbReader_t *reader) {
    uint64_t *tags = NULL;
    DWORD tag_count = 0, tag_capacity = MAX(1, globals.max_edicts);
    DWORD field, wire, ability_id = 0;
    wc3Sc2ActionResult_t result;

    tags = gi.MemAlloc(sizeof(*tags) * tag_capacity);
    if (!tags) return WC3_SC2_ACTION_ERROR;
    while (WC3_SC2API_PbNext(reader, &field, &wire)) {
        if (field == 1 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(reader, &value)) break; ability_id = (DWORD)value;
        } else if (field == 2 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(reader, &value)) break;
            if (tag_count < tag_capacity) tags[tag_count++] = value; else reader->failed = true;
        } else if (!WC3_SC2API_PbSkip(reader, wire)) break;
    }
    result = reader->failed || !ability_id || !tag_count ? WC3_SC2_ACTION_ERROR :
        WC3_SC2API_ToggleRawAutocast(session.player, ability_id, tags, tag_count);
    gi.MemFree(tags);
    return result;
}

static wc3Sc2ActionResult_t sc2_parse_action(wc3Sc2PbReader_t *reader) {
    DWORD field, wire;
    wc3Sc2ActionResult_t result = WC3_SC2_ACTION_NOT_SUPPORTED;
    while (WC3_SC2API_PbNext(reader, &field, &wire)) {
        if (field == 1 && wire == 2) {
            wc3Sc2PbReader_t raw;
            DWORD raw_field, raw_wire;
            if (!WC3_SC2API_PbReadSubmessage(reader, &raw)) return WC3_SC2_ACTION_ERROR;
            while (WC3_SC2API_PbNext(&raw, &raw_field, &raw_wire)) {
                if (raw_field == 1 && raw_wire == 2) {
                    wc3Sc2PbReader_t command;
                    if (!WC3_SC2API_PbReadSubmessage(&raw, &command)) return WC3_SC2_ACTION_ERROR;
                    result = sc2_parse_raw_command(&command);
                } else if (raw_field == 2 && raw_wire == 2) {
                    wc3Sc2PbReader_t camera;
                    if (!WC3_SC2API_PbReadSubmessage(&raw, &camera)) return WC3_SC2_ACTION_ERROR;
                    result = sc2_parse_raw_camera(&camera);
                } else if (raw_field == 3 && raw_wire == 2) {
                    wc3Sc2PbReader_t autocast;
                    if (!WC3_SC2API_PbReadSubmessage(&raw, &autocast)) return WC3_SC2_ACTION_ERROR;
                    result = sc2_parse_raw_autocast(&autocast);
                } else if (!WC3_SC2API_PbSkip(&raw, raw_wire)) return WC3_SC2_ACTION_ERROR;
            }
        } else if (!WC3_SC2API_PbSkip(reader, wire)) return WC3_SC2_ACTION_ERROR;
    }
    return reader->failed ? WC3_SC2_ACTION_ERROR : result;
}

static BOOL sc2_send_action_response(DWORD id, wc3Sc2PbReader_t *request) {
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t response;
    DWORD field, wire;
    if (!session.joined || session.observer || !session.raw) return sc2_send_empty(11, id, "raw actions unavailable for this session");
    if (!sc2_ensure_response_buffer()) return false;
    response = sc2_response_begin(&writer, 11);
    while (WC3_SC2API_PbNext(request, &field, &wire)) {
        if (field == 1 && wire == 2) {
            wc3Sc2PbReader_t action;
            wc3Sc2ActionResult_t result;
            if (!WC3_SC2API_PbReadSubmessage(request, &action)) break;
            result = sc2_parse_action(&action);
            WC3_SC2API_PbWriteVarintField(&writer, 1, result);
        } else if (!WC3_SC2API_PbSkip(request, wire)) break;
    }
    if (request->failed) return sc2_send_empty(11, id, "invalid action request");
    return sc2_response_finish(&writer, response, id, NULL);
}

static DWORD sc2_placement_result(buildPlacementResult_t result) {
    switch (result) {
        case PLACE_OK: return 1; /* Success */
        case PLACE_OUT_OF_BOUNDS: return 44; /* CantBuildLocationInvalid */
        case PLACE_REQUIRES_BLIGHT: return 42; /* CantBuildOnThat */
        case PLACE_TOO_CLOSE_TO_GOLD_MINE: return 47; /* CantBuildTooCloseToResources */
        case PLACE_REQUIRED_PARENT_MISSING: return 40; /* BuildTechRequirementsNotMet */
        case PLACE_INVALID_BUILDING: return 2; /* NotSupported */
        default: return 41; /* CantFindPlacementLocation */
    }
}

static BOOL sc2_query_button_available(LPEDICT unit, gameCommandButton_t const *button,
                                       BOOL ignore_resource_requirements) {
    LPGAMECLIENT client = WC3_SC2API_PlayerClient(session.player);
    DWORD rawcode = 0;
    buildCommandState_t state;

    if (!unit || !button || button->disabled || !button->command[0] || !client) return false;
    /* SC2's ignore_resource_requirements ignores food/mineral/gas costs and
     * cooldowns, but not energy. Preserve Warcraft mana and tech gating. */
    if (button->manacost > unit->mana.value) return false;
    if (!ignore_resource_requirements && button->cooldown > 0.0f) return false;
    if (strlen(button->command) != 4) return true;
    rawcode = FS_SLKKey(button->command);

    if (G_WorkerCanBuild(unit, rawcode)) {
        state = G_GetBuildCommandState(client, unit, rawcode, NULL, 0);
    } else if (G_ProducerCanTrain(unit, rawcode)) {
        state = G_GetTrainCommandState(client, unit, rawcode, NULL, 0);
    } else if (G_ProducerCanResearch(unit, rawcode)) {
        state = G_GetResearchCommandState(client, unit, rawcode, NULL, NULL, 0);
    } else if (G_ProducerCanUpgrade(unit, rawcode)) {
        buildingUpgradeCommandParams_t params = { .client = client, .producer = unit, .unit_id = rawcode };
        state = G_GetBuildingUpgradeCommandState(&params);
    } else {
        return true;
    }
    if (state == BUILD_COMMAND_UNAFFORDABLE) return ignore_resource_requirements;
    return state == BUILD_COMMAND_AVAILABLE;
}

static void sc2_query_available_abilities(wc3Sc2PbWriter_t *writer, uint64_t tag,
                                          BOOL ignore_resource_requirements) {
    LPEDICT unit = WC3_SC2API_ResolveUnitTag(tag);
    gameCommandButton_t buttons[32];
    BYTE count = 0;
    wc3Sc2PbMessageMark_t response = WC3_SC2API_PbBeginMessage(writer, 2);
    if (unit && G_UnitCanControl(WC3_SC2API_PlayerClient(session.player), unit)) {
        count = G_GetCommandButtons(unit, buttons, (BYTE)(sizeof(buttons) / sizeof(buttons[0])));
        FOR_LOOP(i, count) {
            DWORD ability_id;
            wc3Sc2PbMessageMark_t ability;
            if (!sc2_query_button_available(unit, &buttons[i], ignore_resource_requirements)) continue;
            ability_id = G_OrderId(buttons[i].command);
            if (!ability_id) continue;
            ability = WC3_SC2API_PbBeginMessage(writer, 1);
            WC3_SC2API_PbWriteSInt32Field(writer, 1, (LONG)ability_id);
            {
                ability_t const *definition = FindAbilityForCommand(buttons[i].command);
                DWORD const rawcode = strlen(buttons[i].command) == 4 ? FS_SLKKey(buttons[i].command) : 0;
                BOOL const building = rawcode && G_UnitBalance(rawcode)->id == rawcode && G_UnitIsBuilding(rawcode);
                if (building || sc2_ability_target_type(definition, buttons[i].command) == 2)
                    WC3_SC2API_PbWriteBoolField(writer, 2, true);
            }
            WC3_SC2API_PbEndMessage(writer, ability);
        }
        /* WC3 workers expose a Build submenu on the normal command card, while
         * SC2 QueryAvailableAbilities returns concrete placement abilities.
         * Publish the worker's authored build rawcodes here using the same
         * authoritative state check and resource-ignore policy as the submenu. */
        if (G_UnitProfile(unit->class_id) && G_UnitProfile(unit->class_id)->builds) {
            LPCSTR builds = G_UnitProfile(unit->class_id)->builds;
            LPGAMECLIENT client = WC3_SC2API_PlayerClient(session.player);
            PARSE_LIST(builds, build, parse_segment) {
                DWORD rawcode;
                buildCommandState_t state;
                wc3Sc2PbMessageMark_t ability;
                if (strlen(build) != 4) continue;
                rawcode = FS_SLKKey(build);
                state = G_GetBuildCommandState(client, unit, rawcode, NULL, 0);
                if (state != BUILD_COMMAND_AVAILABLE &&
                    !(ignore_resource_requirements && state == BUILD_COMMAND_UNAFFORDABLE)) continue;
                ability = WC3_SC2API_PbBeginMessage(writer, 1);
                WC3_SC2API_PbWriteSInt32Field(writer, 1, (LONG)rawcode);
                WC3_SC2API_PbWriteBoolField(writer, 2, true);
                WC3_SC2API_PbEndMessage(writer, ability);
            }
        }
        WC3_SC2API_PbWriteVarintField(writer, 3, unit->class_id);
    }
    WC3_SC2API_PbWriteVarintField(writer, 2, tag);
    WC3_SC2API_PbEndMessage(writer, response);
}

static FLOAT sc2_query_pathing_distance(wc3Sc2PbReader_t *query) {
    DWORD field, wire;
    VECTOR2 start_board = { 0 }, end_board = { 0 };
    BOOL has_start = false, has_end = false;
    uint64_t unit_tag = 0;
    FLOAT radius = 0.0f;
    BYTE blocked_flags = CM_PATHING_UNWALKABLE;

    while (WC3_SC2API_PbNext(query, &field, &wire)) {
        if (field == 1 && wire == 2) {
            wc3Sc2PbReader_t point;
            if (!WC3_SC2API_PbReadSubmessage(query, &point)) break;
            has_start = sc2_parse_point2d(&point, &start_board);
        } else if (field == 2 && wire == 0) {
            if (!WC3_SC2API_PbReadVarint(query, &unit_tag)) break;
        } else if (field == 3 && wire == 2) {
            wc3Sc2PbReader_t point;
            if (!WC3_SC2API_PbReadSubmessage(query, &point)) break;
            has_end = sc2_parse_point2d(&point, &end_board);
        } else if (!WC3_SC2API_PbSkip(query, wire)) break;
    }
    if (query->failed || !has_end) return 0.0f;

    VECTOR2 end_world = WC3_SC2API_BoardToWorldPoint(&end_board);
    VECTOR2 start_world;
    if (unit_tag) {
        LPEDICT unit = WC3_SC2API_ResolveUnitTag(unit_tag);
        /* A remembered tag must not become a fog-of-war position oracle. SC2
         * pathing-by-unit starts from the unit's current observed position, so
         * require the unit to be actively visible to this perspective. */
        if (!unit || !G_FowPlayerCanSeeEntity(session.player, unit) ||
            !G_FowPlayerCanHoverEntity(session.player, unit)) return 0.0f;
        start_world = unit->s.origin2;
        radius = MAX(0.0f, unit->collision);
        blocked_flags = (unit->aiflags & AI_FLYING) ? CM_PATHING_UNFLYABLE : CM_PATHING_UNWALKABLE;
    } else if (has_start) {
        start_world = WC3_SC2API_BoardToWorldPoint(&start_board);
    } else {
        return 0.0f;
    }

    return WC3_SC2API_WorldToBoardDistance(
        CM_PathDistanceForRadiusFlags(&start_world, &end_world, radius, blocked_flags));
}

static BOOL sc2_send_query(DWORD id, wc3Sc2PbReader_t *request) {
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t response;
    DWORD field, wire;
    BOOL ignore_resource_requirements = false;
    wc3Sc2PbReader_t scan;
    if (!session.joined || !sc2_ensure_response_buffer()) return sc2_send_empty(14, id, "not joined");
    scan = *request;
    while (WC3_SC2API_PbNext(&scan, &field, &wire)) {
        if (field == 4 && wire == 0) {
            uint64_t value = 0;
            if (!WC3_SC2API_PbReadVarint(&scan, &value)) break;
            ignore_resource_requirements = value != 0;
        } else if (!WC3_SC2API_PbSkip(&scan, wire)) break;
    }
    response = sc2_response_begin(&writer, 14);
    while (WC3_SC2API_PbNext(request, &field, &wire)) {
        if (field == 1 && wire == 2) {
            wc3Sc2PbReader_t pathing;
            FLOAT distance;
            wc3Sc2PbMessageMark_t item;
            if (!WC3_SC2API_PbReadSubmessage(request, &pathing)) break;
            distance = sc2_query_pathing_distance(&pathing);
            item = WC3_SC2API_PbBeginMessage(&writer, 1);
            WC3_SC2API_PbWriteFloatField(&writer, 1, distance);
            WC3_SC2API_PbEndMessage(&writer, item);
        } else if (field == 2 && wire == 2) {
            wc3Sc2PbReader_t ability_request;
            DWORD qfield, qwire; uint64_t tag = 0;
            if (!WC3_SC2API_PbReadSubmessage(request, &ability_request)) break;
            while (WC3_SC2API_PbNext(&ability_request, &qfield, &qwire)) {
                if (qfield == 1 && qwire == 0) WC3_SC2API_PbReadVarint(&ability_request, &tag);
                else if (!WC3_SC2API_PbSkip(&ability_request, qwire)) break;
            }
            sc2_query_available_abilities(&writer, tag, ignore_resource_requirements);
        } else if (field == 3 && wire == 2) {
            wc3Sc2PbReader_t placement;
            DWORD qfield, qwire; uint64_t ability = 0, tag = 0; VECTOR2 board = { 0 }; BOOL has_point = false;
            DWORD result = 2;
            if (!WC3_SC2API_PbReadSubmessage(request, &placement)) break;
            while (WC3_SC2API_PbNext(&placement, &qfield, &qwire)) {
                if (qfield == 1 && qwire == 0) WC3_SC2API_PbReadVarint(&placement, &ability);
                else if (qfield == 2 && qwire == 2) { wc3Sc2PbReader_t p; if (WC3_SC2API_PbReadSubmessage(&placement, &p)) has_point = sc2_parse_point2d(&p, &board); }
                else if (qfield == 3 && qwire == 0) WC3_SC2API_PbReadVarint(&placement, &tag);
                else if (!WC3_SC2API_PbSkip(&placement, qwire)) break;
            }
            if (ability && has_point) {
                LPEDICT builder = tag ? WC3_SC2API_ResolveUnitTag(tag) : NULL;
                VECTOR2 world = WC3_SC2API_BoardToWorldPoint(&board), snapped;
                if (!builder || G_UnitCanControl(WC3_SC2API_PlayerClient(session.player), builder))
                    result = sc2_placement_result(G_EvaluateBuildPlacement(builder, (DWORD)ability, &world, &snapped));
                else result = WC3_SC2_ACTION_CANT_CONTROL_UNIT;
            }
            wc3Sc2PbMessageMark_t item = WC3_SC2API_PbBeginMessage(&writer, 3);
            WC3_SC2API_PbWriteVarintField(&writer, 1, result);
            WC3_SC2API_PbEndMessage(&writer, item);
        } else if (field == 4 && wire == 0) {
            uint64_t value;
            if (!WC3_SC2API_PbReadVarint(request, &value)) break;
        } else if (!WC3_SC2API_PbSkip(request, wire)) break;
    }
    return sc2_response_finish(&writer, response, id,
                               request->failed ? "invalid query request" : NULL);
}

static void sc2_handle_create(DWORD id, wc3Sc2PbReader_t *request) {
    PATHSTR map = { 0 };
    DWORD field, wire, player_setup_count = 0;
    BOOL map_data = false, battlenet_map = false;
    BOOL realtime = false, seed_set = false, disable_fog = false;
    DWORD seed = 0;

    while (WC3_SC2API_PbNext(request, &field, &wire)) {
        if (field == 1 && wire == 2) {
            wc3Sc2PbReader_t local;
            DWORD lfield, lwire;
            if (!WC3_SC2API_PbReadSubmessage(request, &local)) break;
            while (WC3_SC2API_PbNext(&local, &lfield, &lwire)) {
                if (lfield == 1 && lwire == 2) sc2_read_string(&local, map, sizeof(map));
                else if (lfield == 7 && lwire == 2) { BYTE const *bytes; DWORD size; if (WC3_SC2API_PbReadBytes(&local, &bytes, &size)) map_data = size != 0; }
                else if (!WC3_SC2API_PbSkip(&local, lwire)) break;
            }
        } else if (field == 2 && wire == 2) {
            BYTE const *bytes; DWORD size; if (WC3_SC2API_PbReadBytes(request, &bytes, &size)) battlenet_map = size != 0;
        } else if (field == 3 && wire == 2) {
            wc3Sc2PbReader_t setup; if (!WC3_SC2API_PbReadSubmessage(request, &setup)) break; player_setup_count++;
        } else if (field == 4 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(request, &value)) break; disable_fog = value != 0;
        } else if (field == 5 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(request, &value)) break; seed = (DWORD)value; seed_set = true;
        } else if (field == 6 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(request, &value)) break; realtime = value != 0;
        } else if (!WC3_SC2API_PbSkip(request, wire)) break;
    }
    if (request->failed) { sc2_send_create_response(id, 2, "invalid create_game request"); return; }
    if (battlenet_map) { sc2_send_create_response(id, 4, "BattleNet maps are not supported"); return; }
    if (map_data) { sc2_send_create_response(id, 3, "inline map_data is not supported"); return; }
    if (!map[0]) { sc2_send_create_response(id, 1, "map_path is required"); return; }
    if (!player_setup_count) { sc2_send_create_response(id, 6, "player_setup is required"); return; }
    if (disable_fog) { sc2_send_create_response(id, 7, "disable_fog is not supported"); return; }
    if (!gi.RequestMap || !gi.RequestMap(map)) { sc2_send_create_response(id, 2, "map path could not be resolved"); return; }
    if (seed_set) srand(seed);
    strlcpy(session.map_path, map, sizeof(session.map_path));
    session.realtime = realtime;
    session.joined = false;
    session.observer = false;
    session.raw = false;
    session.results_reported = false;
    session.camera_set = false;
    session.async = SC2_ASYNC_CREATE;
    session.async_id = id;
    session.async_has_id = session.response_has_id;
}

static BOOL sc2_parse_interface_options(wc3Sc2PbReader_t *options, BOOL *raw, BOOL *unsupported) {
    DWORD field, wire;
    *raw = false; *unsupported = false;
    while (WC3_SC2API_PbNext(options, &field, &wire)) {
        if (field == 1 && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(options, &value)) return false; *raw = value != 0;
        } else if ((field == 2 || field == 5 || field == 7 || field == 8 || field == 9) && wire == 0) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(options, &value)) return false; if (value) *unsupported = true;
        } else if (field == 6 && wire == 0) {
            /* Raw commands enter the authoritative order system directly and never
             * mutate the local HUD selection, so either raw_affects_selection value
             * has the same observable result for an external-only session. */
            uint64_t ignored; if (!WC3_SC2API_PbReadVarint(options, &ignored)) return false;
        } else if ((field == 3 || field == 4) && wire == 2) {
            wc3Sc2PbReader_t ignored; if (!WC3_SC2API_PbReadSubmessage(options, &ignored)) return false; *unsupported = true;
        } else if (!WC3_SC2API_PbSkip(options, wire)) return false;
    }
    return !options->failed;
}

static void sc2_handle_join(DWORD id, wc3Sc2PbReader_t *request) {
    DWORD field, wire;
    uint64_t requested_race = 0, observed_player_id = 0;
    BOOL has_race = false, has_observer = false, has_options = false, raw = false, unsupported = false;
    wc3Sc2GameInfo_t info;
    DWORD selected_player = UINT32_MAX;

    while (WC3_SC2API_PbNext(request, &field, &wire)) {
        if (field == 1 && wire == 0) { if (!WC3_SC2API_PbReadVarint(request, &requested_race)) break; has_race = true; }
        else if (field == 2 && wire == 0) { if (!WC3_SC2API_PbReadVarint(request, &observed_player_id)) break; has_observer = true; }
        else if (field == 3 && wire == 2) {
            wc3Sc2PbReader_t options; if (!WC3_SC2API_PbReadSubmessage(request, &options)) break;
            has_options = sc2_parse_interface_options(&options, &raw, &unsupported);
        } else if (!WC3_SC2API_PbSkip(request, wire)) break;
    }
    if (request->failed || (!has_race && !has_observer)) { sc2_send_join_response(id, 0, 1, "participation is required"); return; }
    if (!has_options) { sc2_send_join_response(id, 0, 3, "InterfaceOptions are required"); return; }
    if (!raw || unsupported) { sc2_send_join_response(id, 0, 7, "only the Raw interface is supported"); return; }
    if (!WC3_SC2API_FillGameInfo(&info)) { sc2_send_join_response(id, 0, 9, "no game is loaded"); return; }

    if (has_observer) {
        if (!observed_player_id || observed_player_id > MAX_PLAYERS || !WC3_SC2API_PlayerClient((DWORD)observed_player_id - 1)) {
            sc2_send_join_response(id, 0, 2, "invalid observed_player_id"); return;
        }
        selected_player = (DWORD)observed_player_id - 1;
        session.observer = true;
    } else {
        FOR_LOOP(i, info.player_count) {
            wc3Sc2PlayerInfo_t const *player = &info.players[i];
            if (player->type != WC3_SC2_PLAYER_PARTICIPANT) continue;
            if (selected_player == UINT32_MAX) selected_player = player->player_id - 1;
            if (requested_race >= WC3_SC2_RACE_HUMAN && requested_race <= WC3_SC2_RACE_NIGHT_ELF &&
                player->race_actual == (wc3Sc2Race_t)requested_race) {
                selected_player = player->player_id - 1;
                break;
            }
        }
        session.observer = false;
    }
    if (selected_player == UINT32_MAX || !WC3_SC2API_PlayerClient(selected_player)) {
        sc2_send_join_response(id, 0, 5, "no compatible Warcraft participant slot"); return;
    }
    session.player = selected_player;
    session.raw = true;
    session.joined = true;
    session.results_reported = false;
    session.status = SC2_STATUS_IN_GAME;
    sc2_send_join_response(id, selected_player + 1, 0, NULL);
}

static void sc2_handle_data(DWORD id, wc3Sc2PbReader_t *request) {
    DWORD field, wire; BOOL abilities = false, units = false, upgrades = false, buffs = false, effects = false;
    while (WC3_SC2API_PbNext(request, &field, &wire)) {
        if (wire == 0 && field >= 1 && field <= 5) {
            uint64_t value; if (!WC3_SC2API_PbReadVarint(request, &value)) break;
            if (field == 1) abilities = value != 0;
            else if (field == 2) units = value != 0;
            else if (field == 3) upgrades = value != 0;
            else if (field == 4) buffs = value != 0;
            else if (field == 5) effects = value != 0;
        } else if (!WC3_SC2API_PbSkip(request, wire)) break;
    }
    if (request->failed) sc2_send_empty(13, id, "invalid data request");
    else sc2_send_data(id, abilities, units, upgrades, buffs, effects);
}

static void sc2_handle_observation(DWORD id, wc3Sc2PbReader_t *request) {
    DWORD field, wire, requested_loop = 0;
    BOOL has_loop = false, disable_fog = false;

    while (WC3_SC2API_PbNext(request, &field, &wire)) {
        if (field == 1 && wire == 0) {
            uint64_t value;
            if (!WC3_SC2API_PbReadVarint(request, &value)) break;
            disable_fog = value != 0;
        } else if (field == 2 && wire == 0) {
            uint64_t value;
            if (!WC3_SC2API_PbReadVarint(request, &value)) break;
            requested_loop = (DWORD)MIN(value, UINT32_MAX);
            has_loop = true;
        } else if (!WC3_SC2API_PbSkip(request, wire)) break;
    }
    if (request->failed) { sc2_send_empty(10, id, "invalid observation request"); return; }
    if (disable_fog) { sc2_send_empty(10, id, "disable_fog observations are not supported"); return; }
    if (!session.joined || (session.status != SC2_STATUS_IN_GAME && session.status != SC2_STATUS_ENDED)) {
        sc2_send_empty(10, id, "observation is unavailable in the current state");
        return;
    }
    if (session.realtime && has_loop && requested_loop > level.framenum && session.status == SC2_STATUS_IN_GAME) {
        session.observation_loop = requested_loop;
        session.async = SC2_ASYNC_OBSERVATION;
        session.async_id = id;
        session.async_has_id = session.response_has_id;
        return;
    }
    sc2_send_observation(id);
}

static void sc2_handle_step(DWORD id, wc3Sc2PbReader_t *request) {
    DWORD field, wire, count = 1;
    while (WC3_SC2API_PbNext(request, &field, &wire)) {
        if (field == 1 && wire == 0) { uint64_t value; if (!WC3_SC2API_PbReadVarint(request, &value)) break; count = (DWORD)MIN(value, UINT32_MAX); }
        else if (!WC3_SC2API_PbSkip(request, wire)) break;
    }
    if (request->failed) { sc2_send_empty(12, id, "invalid step request"); return; }
    if (session.realtime) { sc2_send_empty(12, id, "step is unavailable in realtime mode"); return; }
    if (!session.joined || session.status != SC2_STATUS_IN_GAME) { sc2_send_empty(12, id, "not in game"); return; }
    if (!count) { sc2_send_step_response(id); return; }
    session.requested_steps = count;
    session.async = SC2_ASYNC_STEP;
    session.async_id = id;
    session.async_has_id = session.response_has_id;
}

static void sc2_handle_restart(DWORD id) {
    LPCSTR path = level.map_path[0] ? level.map_path : session.map_path;
    if (!path || !*path || !gi.RequestMap || !gi.RequestMap(path)) {
        wc3Sc2PbWriter_t writer; wc3Sc2PbMessageMark_t response;
        if (!sc2_ensure_response_buffer()) return;
        response = sc2_response_begin(&writer, 3);
        WC3_SC2API_PbWriteVarintField(&writer, 1, 1); /* LaunchError */
        WC3_SC2API_PbWriteStringField(&writer, 2, "no restartable map");
        sc2_response_finish(&writer, response, id, NULL);
        return;
    }
    strlcpy(session.map_path, path, sizeof(session.map_path));
    session.results_reported = false;
    session.camera_set = false;
    session.async = SC2_ASYNC_RESTART;
    session.async_id = id;
    session.async_has_id = session.response_has_id;
}

static void sc2_dispatch_request(BYTE const *data, DWORD size) {
    wc3Sc2PbReader_t outer, request = { 0 };
    DWORD field, wire, request_field = 0, id = 0;
    BOOL have_request = false, have_id = false;
    WC3_SC2API_PbReaderInit(&outer, data, size);
    while (WC3_SC2API_PbNext(&outer, &field, &wire)) {
        if (field == 97 && wire == 0) { uint64_t value; if (!WC3_SC2API_PbReadVarint(&outer, &value)) break; id = (DWORD)value; have_id = true; }
        else if (field >= 1 && field <= 22 && wire == 2 && !have_request) {
            if (!WC3_SC2API_PbReadSubmessage(&outer, &request)) break;
            request_field = field; have_request = true;
        } else if (!WC3_SC2API_PbSkip(&outer, wire)) break;
    }
    session.response_has_id = have_id;
    if (outer.failed || !have_request) { sc2_send_empty(19, id, "invalid or missing request"); return; }

    switch (request_field) {
        case 1:
            if (session.status != SC2_STATUS_LAUNCHED && session.status != SC2_STATUS_ENDED)
                sc2_send_empty(1, id, "create_game is invalid in the current state");
            else sc2_handle_create(id, &request);
            break;
        case 2:
            if (session.status != SC2_STATUS_INIT_GAME)
                sc2_send_empty(2, id, "join_game requires init_game state");
            else sc2_handle_join(id, &request);
            break;
        case 3:
            if (session.status != SC2_STATUS_ENDED)
                sc2_send_empty(3, id, "restart_game requires ended state");
            else sc2_handle_restart(id);
            break;
        case 5:
            if (session.status != SC2_STATUS_IN_GAME) sc2_send_empty(5, id, "leave_game requires in_game state");
            else {
                session.joined = false; session.observer = false; session.raw = false; session.status = SC2_STATUS_LAUNCHED;
                sc2_send_empty(5, id, NULL);
            }
            break;
        case 8:
            session.status = SC2_STATUS_QUIT; sc2_send_empty(8, id, NULL); if (gi.RequestQuit) gi.RequestQuit(); break;
        case 9:
            if (session.status != SC2_STATUS_IN_GAME && session.status != SC2_STATUS_ENDED)
                sc2_send_empty(9, id, "game_info is unavailable in the current state");
            else sc2_send_game_info(id);
            break;
        case 10: sc2_handle_observation(id, &request); break;
        case 11:
            if (session.status != SC2_STATUS_IN_GAME) sc2_send_empty(11, id, "action requires in_game state");
            else sc2_send_action_response(id, &request);
            break;
        case 12: sc2_handle_step(id, &request); break;
        case 13:
            if (session.status != SC2_STATUS_IN_GAME && session.status != SC2_STATUS_ENDED)
                sc2_send_empty(13, id, "data is unavailable in the current state");
            else sc2_handle_data(id, &request);
            break;
        case 14:
            if (session.status != SC2_STATUS_IN_GAME && session.status != SC2_STATUS_ENDED)
                sc2_send_empty(14, id, "query is unavailable in the current state");
            else sc2_send_query(id, &request);
            break;
        case 17: sc2_send_available_maps(id); break;
        case 19: sc2_send_ping(id); break;
        default: sc2_send_empty(request_field, id, "request is not supported by OpenRealm"); break;
    }
}

BOOL WC3_SC2API_ExternalActive(void) {
    LPCSTR value = gi.CvarString ? gi.CvarString("sc2_api", "0") : "0";
    return value && atoi(value) != 0;
}

BOOL WC3_SC2API_ExternalOwnsClock(void) {
    return WC3_SC2API_ExternalActive() && session.joined && !session.realtime && session.status == SC2_STATUS_IN_GAME;
}

DWORD WC3_SC2API_ExternalFrame(void) {
    DWORD steps = 0;
    sc2RequestNode_t *node;
    if (!WC3_SC2API_ExternalActive()) return 0;
    WC3_SC2API_TransportPoll(sc2_enqueue_request);
    if (session.queue_overflow) {
        session.queue_overflow = false;
        WC3_SC2API_TransportShutdown();
    }
    {
        BOOL const connected = WC3_SC2API_TransportConnected();
        if (session.transport_connected && !connected) {
            session.transport_connected = false;
            sc2_reset_connection_state();
        }
        session.transport_connected = connected;
    }
    sc2_update_terminal_status();
    if (session.async == SC2_ASYNC_OBSERVATION) {
        if (level.framenum >= session.observation_loop || session.status == SC2_STATUS_ENDED) {
            DWORD const id = session.async_id;
            session.response_has_id = session.async_has_id;
            session.async = SC2_ASYNC_NONE;
            session.async_id = 0;
            session.async_has_id = false;
            session.observation_loop = 0;
            sc2_send_observation(id);
        } else {
            return 0;
        }
    }
    if (session.async != SC2_ASYNC_NONE) {
        if (session.async == SC2_ASYNC_STEP && session.requested_steps) {
            steps = session.requested_steps;
            session.requested_steps = 0;
        }
        return steps;
    }
    while (session.async == SC2_ASYNC_NONE && (node = sc2_pop_request()) != NULL) {
        sc2_dispatch_request(node->data, node->size);
        sc2_free_request(node);
    }
    if (session.async == SC2_ASYNC_STEP && session.requested_steps) {
        steps = session.requested_steps;
        session.requested_steps = 0;
    }
    return steps;
}

BOOL WC3_SC2API_ExternalCanAdvance(void) {
    sc2_update_terminal_status();
    return session.status == SC2_STATUS_IN_GAME;
}

void WC3_SC2API_ExternalStepComplete(DWORD steps) {
    DWORD id;
    (void)steps;
    if (session.async != SC2_ASYNC_STEP) return;
    id = session.async_id;
    session.response_has_id = session.async_has_id;
    session.async = SC2_ASYNC_NONE;
    session.async_id = 0;
    session.async_has_id = false;
    sc2_update_terminal_status();
    sc2_send_step_response(id);
}

void WC3_SC2API_ExternalMapComplete(LPCSTR map, BOOL success) {
    DWORD id;
    sc2AsyncType_t async = session.async;
    if (async != SC2_ASYNC_CREATE && async != SC2_ASYNC_RESTART) return;
    id = session.async_id;
    session.response_has_id = session.async_has_id;
    session.async = SC2_ASYNC_NONE;
    session.async_id = 0;
    session.async_has_id = false;
    if (success) {
        WC3_SC2API_ResetUnitTags();
        session.dead_unit_count = 0;
        if (map) strlcpy(session.map_path, map, sizeof(session.map_path));
    }
    if (async == SC2_ASYNC_CREATE) {
        session.status = success ? SC2_STATUS_INIT_GAME : SC2_STATUS_LAUNCHED;
        sc2_send_create_response(id, success ? 0 : 2, success ? NULL : "map load failed");
    } else {
        wc3Sc2PbWriter_t writer; wc3Sc2PbMessageMark_t response;
        if (!sc2_ensure_response_buffer()) return;
        session.status = success ? SC2_STATUS_IN_GAME : SC2_STATUS_ENDED;
        response = sc2_response_begin(&writer, 3);
        if (!success) { WC3_SC2API_PbWriteVarintField(&writer, 1, 1); WC3_SC2API_PbWriteStringField(&writer, 2, "map reload failed"); }
        sc2_response_finish(&writer, response, id, NULL);
    }
}

void WC3_SC2API_ServerShutdown(void) {
    sc2_clear_queue();
    WC3_SC2API_TransportShutdown();
    SAFE_DELETE(session.response, gi.MemFree);
    memset(&session, 0, sizeof(session));
    session.status = SC2_STATUS_LAUNCHED;
}

#endif
