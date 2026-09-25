#ifdef WC3_SC2API

#include "sc2api_map.h"
#include "games/warcraft-3/common/terrain.h"

/* SC2 feature/raw visibility images use the same categorical values:
 * 0=Hidden, 1=Fogged, 2=Visible. */
enum {
    WC3_SC2_VISIBILITY_HIDDEN = 0,
    WC3_SC2_VISIBILITY_FOGGED = 1,
    WC3_SC2_VISIBILITY_VISIBLE = 2,
};

static DWORD sc2api_bit_data_size(DWORD width, DWORD height) {
    DWORD cells;
    if (!width || !height || width > UINT_MAX / height) return 0;
    cells = width * height;
    return cells / 8u + ((cells & 7u) != 0);
}

static void sc2api_set_bit(LPBYTE data, DWORD index) {
    data[index >> 3] |= (BYTE)(1u << (7u - (index & 7u)));
}

FLOAT WC3_SC2API_BoardCellWorldSize(void) {
    FLOAT cell = CM_PathCellWorldSize();
    /* A missing WPM makes the generic routing helper report an effectively
     * unbounded cell. Warcraft's authored pathing/placement lattice is 32
     * world units, so retain that canonical scale for API metadata. */
    if (cell <= 0.0f || cell > (FLOAT)TILE_SIZE) cell = (FLOAT)SEGMENT_SIZE;
    return cell;
}

DWORD WC3_SC2API_MapWidth(void) {
    BOX2 const bounds = CM_GetWorldBounds();
    FLOAT const cell = WC3_SC2API_BoardCellWorldSize();
    FLOAT const width = bounds.max.x - bounds.min.x;
    if (width <= 0.0f || cell <= 0.0f) return 0;
    return MAX(1u, (DWORD)ceilf(width / cell));
}

DWORD WC3_SC2API_MapHeight(void) {
    BOX2 const bounds = CM_GetWorldBounds();
    FLOAT const cell = WC3_SC2API_BoardCellWorldSize();
    FLOAT const height = bounds.max.y - bounds.min.y;
    if (height <= 0.0f || cell <= 0.0f) return 0;
    return MAX(1u, (DWORD)ceilf(height / cell));
}

VECTOR2 WC3_SC2API_WorldToBoardPoint(LPCVECTOR2 point) {
    BOX2 const bounds = CM_GetWorldBounds();
    FLOAT const cell = WC3_SC2API_BoardCellWorldSize();
    if (!point || cell <= 0.0f) return (VECTOR2){ 0.0f, 0.0f };
    return (VECTOR2){
        (point->x - bounds.min.x) / cell,
        (point->y - bounds.min.y) / cell,
    };
}

VECTOR2 WC3_SC2API_BoardToWorldPoint(LPCVECTOR2 point) {
    BOX2 const bounds = CM_GetWorldBounds();
    FLOAT const cell = WC3_SC2API_BoardCellWorldSize();
    if (!point || cell <= 0.0f) return bounds.min;
    return (VECTOR2){
        bounds.min.x + point->x * cell,
        bounds.min.y + point->y * cell,
    };
}

FLOAT WC3_SC2API_WorldToBoardDistance(FLOAT distance) {
    FLOAT const cell = WC3_SC2API_BoardCellWorldSize();
    return cell > 0.0f ? distance / cell : 0.0f;
}

FLOAT WC3_SC2API_BoardToWorldDistance(FLOAT distance) {
    return distance * WC3_SC2API_BoardCellWorldSize();
}

FLOAT WC3_SC2API_WorldToBoardHeight(FLOAT height) {
    return WC3_SC2API_WorldToBoardDistance(height);
}

FLOAT WC3_SC2API_BoardToWorldHeight(FLOAT height) {
    return WC3_SC2API_BoardToWorldDistance(height);
}

DWORD WC3_SC2API_PathingDataSize(void) {
    return sc2api_bit_data_size(WC3_SC2API_MapWidth(), WC3_SC2API_MapHeight());
}

DWORD WC3_SC2API_TerrainHeightDataSize(void) {
    DWORD const width = WC3_SC2API_MapWidth(), height = WC3_SC2API_MapHeight();
    if (!width || !height || width > UINT_MAX / height) return 0;
    return width * height;
}

DWORD WC3_SC2API_PlacementDataSize(void) {
    return sc2api_bit_data_size(WC3_SC2API_MapWidth(), WC3_SC2API_MapHeight());
}

static BYTE sc2api_terrain_height(FLOAT world_height) {
    /* SC2 spatial terrain height uses board-space [-200, 200] encoded into
     * uint8 [0, 255]. Use the same scale for StartRaw terrain_height. */
    FLOAT board_height = WC3_SC2API_WorldToBoardHeight(world_height);
    FLOAT encoded;
    board_height = MAX(-200.0f, MIN(200.0f, board_height));
    encoded = (board_height + 200.0f) * (255.0f / 400.0f);
    return (BYTE)MAX(0, MIN(255, (LONG)floorf(encoded + 0.5f)));
}

static void sc2api_fill_playable_area(wc3Sc2StartRaw_t *out) {
    LONG left = 0, right = out->map_size.x, bottom = 0, top = out->map_size.y;
    FLOAT const cells_per_tile = (FLOAT)TILE_SIZE / WC3_SC2API_BoardCellWorldSize();

    if (level.mapinfo && cells_per_tile > 0.0f) {
        mapCameraBounds_t const *camera = &level.mapinfo->cameraBounds;
        left += (LONG)floorf(MAX(0, camera->complement.left) * cells_per_tile + 0.5f);
        right -= (LONG)floorf(MAX(0, camera->complement.right) * cells_per_tile + 0.5f);
        bottom += (LONG)floorf(MAX(0, camera->complement.bottom) * cells_per_tile + 0.5f);
        top -= (LONG)floorf(MAX(0, camera->complement.top) * cells_per_tile + 0.5f);
    }
    left = MAX(0, MIN(left, out->map_size.x));
    right = MAX(left, MIN(right, out->map_size.x));
    bottom = MAX(0, MIN(bottom, out->map_size.y));
    top = MAX(bottom, MIN(top, out->map_size.y));
    out->playable_area.p0 = (wc3Sc2PointI_t){ left, bottom };
    out->playable_area.p1 = (wc3Sc2PointI_t){ right, top };
}

static void sc2api_fill_start_locations(wc3Sc2StartRaw_t *out) {
    if (!level.mapinfo) return;
    FOR_LOOP(player, MAX_PLAYERS) {
        LPCMAPPLAYER map_player = &level.mapinfo->players[player];
        if (!map_player->used || (map_player->playerType != kPlayerTypeHuman &&
                                  map_player->playerType != kPlayerTypeComputer)) continue;
        if (out->start_location_count >= MAX_PLAYERS) break;
        out->start_locations[out->start_location_count++] =
            WC3_SC2API_WorldToBoardPoint(&map_player->startingPosition);
    }
}

BOOL WC3_SC2API_FillStartRaw(LPBYTE pathing_data, DWORD pathing_capacity,
                             LPBYTE terrain_height_data, DWORD terrain_height_capacity,
                             LPBYTE placement_data, DWORD placement_capacity,
                             wc3Sc2StartRaw_t *out) {
    DWORD const width = WC3_SC2API_MapWidth(), height = WC3_SC2API_MapHeight();
    DWORD const pathing_size = WC3_SC2API_PathingDataSize();
    DWORD const terrain_size = WC3_SC2API_TerrainHeightDataSize();
    DWORD const placement_size = WC3_SC2API_PlacementDataSize();
    BOX2 const bounds = CM_GetWorldBounds();
    FLOAT const cell = WC3_SC2API_BoardCellWorldSize();

    if (!out || !width || !height || !pathing_size || !terrain_size || !placement_size ||
        !pathing_data || pathing_capacity < pathing_size ||
        !terrain_height_data || terrain_height_capacity < terrain_size ||
        !placement_data || placement_capacity < placement_size) return false;

    memset(out, 0, sizeof(*out));
    memset(pathing_data, 0, pathing_size);
    memset(placement_data, 0, placement_size);
    out->map_size = (wc3Sc2Size2DI_t){ (LONG)width, (LONG)height };

    FOR_LOOP(y, height) FOR_LOOP(x, width) {
        DWORD const index = x + y * width;
        VECTOR2 const sample = {
            bounds.min.x + ((FLOAT)x + 0.5f) * cell,
            bounds.min.y + ((FLOAT)y + 0.5f) * cell,
        };
        BYTE flags = 0;
        BOOL const have_pathing = CM_GetPathingFlagsAt(&sample, &flags);
        if (!have_pathing || !(flags & WC3_PATH_UNWALKABLE)) sc2api_set_bit(pathing_data, index);
        if (!have_pathing || !(flags & (WC3_PATH_UNWALKABLE | WC3_PATH_UNBUILDABLE))) {
            sc2api_set_bit(placement_data, index);
        }
        terrain_height_data[index] = sc2api_terrain_height(CM_GetHeightAtPoint(sample.x, sample.y));
    }

    out->pathing_grid = (wc3Sc2ImageData_t){ 1, (LONG)width, (LONG)height, pathing_size, pathing_data };
    out->terrain_height = (wc3Sc2ImageData_t){ 8, (LONG)width, (LONG)height, terrain_size, terrain_height_data };
    out->placement_grid = (wc3Sc2ImageData_t){ 1, (LONG)width, (LONG)height, placement_size, placement_data };
    sc2api_fill_playable_area(out);
    sc2api_fill_start_locations(out);
    return true;
}

DWORD WC3_SC2API_VisibilityDataSize(void) {
    DWORD const width = WC3_SC2API_MapWidth(), height = WC3_SC2API_MapHeight();
    if (!width || !height || width > UINT_MAX / height) return 0;
    return width * height;
}

DWORD WC3_SC2API_CreepDataSize(void) {
    return sc2api_bit_data_size(WC3_SC2API_MapWidth(), WC3_SC2API_MapHeight());
}

BOOL WC3_SC2API_FillMapState(DWORD player,
                             LPBYTE visibility_data, DWORD visibility_capacity,
                             LPBYTE creep_data, DWORD creep_capacity,
                             wc3Sc2MapState_t *out) {
    DWORD const width = WC3_SC2API_MapWidth(), height = WC3_SC2API_MapHeight();
    DWORD const visibility_size = WC3_SC2API_VisibilityDataSize();
    DWORD const creep_size = WC3_SC2API_CreepDataSize();
    fowPlayerGrid_t const *grid;

    if (!out || player >= MAX_PLAYERS || !WC3_SC2API_PlayerClient(player) || !width || !height) return false;
    memset(out, 0, sizeof(*out));

    if (visibility_size && level.fow.width && level.fow.height) {
        if (!visibility_data || visibility_capacity < visibility_size) return false;
        grid = &level.fow.players[player];
        if (!grid->visible || !grid->explored) return false;
        FOR_LOOP(y, height) FOR_LOOP(x, width) {
            DWORD const sx = MIN(level.fow.width - 1,
                                 (DWORD)(((uint64_t)(2u * x + 1u) * level.fow.width) / (2u * width)));
            DWORD const sy = MIN(level.fow.height - 1,
                                 (DWORD)(((uint64_t)(2u * y + 1u) * level.fow.height) / (2u * height)));
            DWORD const source = sx + sy * level.fow.width;
            DWORD const dest = x + y * width;
            visibility_data[dest] = grid->visible[source] ? WC3_SC2_VISIBILITY_VISIBLE :
                                    grid->explored[source] ? WC3_SC2_VISIBILITY_FOGGED :
                                                             WC3_SC2_VISIBILITY_HIDDEN;
        }
        out->has_visibility = true;
        out->visibility = (wc3Sc2ImageData_t){ 8, (LONG)width, (LONG)height,
                                               visibility_size, visibility_data };
    }

    /* SC2's 1-bit ImageData is consumed by PySC2 through numpy.unpackbits,
     * which is MSB-first within each byte. Preserve that bit order here so a
     * normal SC2 image decoder reads Warcraft Blight as the creep layer. */
    if (creep_size && level.blight.width && level.blight.height && level.blight.cells) {
        if (!creep_data || creep_capacity < creep_size) return false;
        memset(creep_data, 0, creep_size);
        FOR_LOOP(y, height) FOR_LOOP(x, width) {
            DWORD const sx = MIN(level.blight.width - 1,
                                 (DWORD)(((uint64_t)(2u * x + 1u) * level.blight.width) / (2u * width)));
            DWORD const sy = MIN(level.blight.height - 1,
                                 (DWORD)(((uint64_t)(2u * y + 1u) * level.blight.height) / (2u * height)));
            if (level.blight.cells[sx + sy * level.blight.width]) {
                sc2api_set_bit(creep_data, x + y * width);
            }
        }
        out->has_creep = true;
        out->creep = (wc3Sc2ImageData_t){ 1, (LONG)width, (LONG)height, creep_size, creep_data };
    }

    return true;
}

#endif
