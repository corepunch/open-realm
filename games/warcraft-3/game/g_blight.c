#include "g_local.h"

#define WC3_BLIGHT_PATH_CELL 32.0f // world units; authoritative Blight cell width; used by simulation and network mask
#define WC3_BLIGHT_TERRAIN_CELL 128.0f // world units; authored terrain-corner spacing; used by Blight rasterization

static uint32_t G_BlightCellCount(void) { return level.blight.width * level.blight.height; }

static uint32_t G_BlightConnectedMask(void) {
    uint32_t mask = 0;
    FOR_LOOP(i, game.max_clients) {
        uint32_t const player = game.clients[i].ps.number;
        if (game.clients[i].connected && player < MAX_PLAYERS) mask |= 1u << player;
    }
    return mask;
}

static bool G_BlightCell(LPCVECTOR2 point, uint32_t * x, uint32_t * y) {
    if (!point || !level.blight.cells) return false;
    return TerrainMask_CellForPoint(level.blight.bounds.min, WC3_BLIGHT_PATH_CELL, level.blight.width, level.blight.height, point, x, y);
}

static bool G_SetBlightCell(int32_t x, int32_t y, bool add, uint32_t mask) {
    uint32_t index;
    if (x < 0 || y < 0 || x >= (int32_t)level.blight.width || y >= (int32_t)level.blight.height) return false;
    index = (uint32_t)x + (uint32_t)y * level.blight.width;
    if (level.blight.cells[index] == (uint8_t)(add != 0)) return false;
    level.blight.cells[index] = add ? 1 : 0;
    if (level.blight.dirty_rows) level.blight.dirty_rows[y] |= mask;
    return true;
}

static uint32_t G_SetBlightCorner(float x, float y, bool add, uint32_t mask) {
    VECTOR2 sample;
    uint32_t ux, uy;
    int32_t cx, cy;
    uint32_t changed = 0;
    sample.x = x; sample.y = y;
    if (!G_BlightCell(&sample, &ux, &uy)) return 0;
    /* One 128-unit terrain corner owns the surrounding 4x4 32-unit pathing cells. */
    cx = (int32_t)ux - 2; cy = (int32_t)uy - 2;
    FOR_LOOP(ix, 4) FOR_LOOP(iy, 4)
        changed += G_SetBlightCell(cx + (int32_t)ix, cy + (int32_t)iy, add, mask);
    return changed;
}

static float G_SnapBlightCorner(float value, float minimum) {
    return minimum + floorf((value - minimum) / WC3_BLIGHT_TERRAIN_CELL) * WC3_BLIGHT_TERRAIN_CELL;
}

static bool G_BlightDestructableFootprintBlighted(LPCEDICT ent) {
    pathTex_t const *pathtex;

    if (!ent || !G_IsDestructable(ent) || ent->destructable.dead) return false;
    pathtex = ent->destructable.alive_pathtex;
    if (!pathtex || !pathtex->width || !pathtex->height)
        return G_IsPointBlighted(&ent->s.origin2);
    FOR_LOOP(y, pathtex->height) FOR_LOOP(x, pathtex->width) {
        VECTOR2 sample;
        if (!pathtex->map[x + y * pathtex->width].b) continue;
        sample.x = ent->s.origin2.x + ((float)x + 0.5f - (float)pathtex->width * 0.5f) * WC3_BLIGHT_PATH_CELL;
        sample.y = ent->s.origin2.y + ((float)y + 0.5f - (float)pathtex->height * 0.5f) * WC3_BLIGHT_PATH_CELL;
        if (!G_IsPointBlighted(&sample)) return false;
    }
    return true;
}

void G_BlightMarkDestructable(LPEDICT ent) {
    DestructableData_t const *data;
    PATHSTR blight_texture;
    cstring_t dot;

    if (!ent || !G_IsDestructable(ent) || ent->destructable.blighted) return;
    ent->destructable.blighted = true;
    ent->vertex_color = MAKE(COLOR32, 120, 185, 72, 255);
    ent->vertex_color_set = true;
    data = ent->data.DestructableData;
    if (!data || !data->textureFile || !*data->textureFile || !strcmp(data->textureFile, "_")) return;
    dot = strrchr(data->textureFile, '.');
    if (dot && dot - data->textureFile >= 6 && !strncasecmp(dot - 6, "Blight", 6)) return;
    if (!dot && strlen(data->textureFile) >= 6 && !strcasecmp(data->textureFile + strlen(data->textureFile) - 6, "Blight")) return;
    if (dot)
        snprintf(blight_texture, sizeof(blight_texture), "%.*sBlight%s",
                 (int)(dot - data->textureFile), data->textureFile, dot);
    else
        snprintf(blight_texture, sizeof(blight_texture), "%sBlight", data->textureFile);
    ent->s.image = gi.ImageIndex(blight_texture);
    if (!ent->s.image)
        fprintf(stderr, "G_BlightMarkDestructable: unresolved Blight texture '%s' for %.4s\n",
                blight_texture, (cstring_t)&ent->class_id);
}

void G_BlightInitializeDestructable(LPEDICT ent) {
    if (G_BlightDestructableFootprintBlighted(ent)) G_BlightMarkDestructable(ent);
}

void G_BlightUpdateDestructables(LPCBOX2 region) {
    BOX2 expanded;

    if (!region) return;
    expanded = *region;
    expanded.min.x -= WC3_BLIGHT_TERRAIN_CELL;
    expanded.min.y -= WC3_BLIGHT_TERRAIN_CELL;
    expanded.max.x += WC3_BLIGHT_TERRAIN_CELL;
    expanded.max.y += WC3_BLIGHT_TERRAIN_CELL;
    FILTER_EDICTS(ent, ent->inuse && G_IsDestructable(ent) &&
        !ent->destructable.blighted && Box2_containsPoint(&expanded, &ent->s.origin2)) {
        if (G_BlightDestructableFootprintBlighted(ent)) G_BlightMarkDestructable(ent);
    }
}

void G_BlightShutdown(void) {
    SAFE_DELETE(level.blight.cells, gi.MemFree);
    SAFE_DELETE(level.blight.dirty_rows, gi.MemFree);
    memset(&level.blight, 0, sizeof(level.blight));
}

void G_BlightInit(void) {
    uint32_t cells;
    uint32_t unavailable = 0;
    VECTOR2 sample;

    G_BlightShutdown();
    level.blight.bounds = CM_GetWorldBounds();
    level.blight.width = MAX(1u, (uint32_t)ceilf((level.blight.bounds.max.x - level.blight.bounds.min.x) / WC3_BLIGHT_PATH_CELL));
    level.blight.height = MAX(1u, (uint32_t)ceilf((level.blight.bounds.max.y - level.blight.bounds.min.y) / WC3_BLIGHT_PATH_CELL));
    if (level.blight.width > USHRT_MAX || level.blight.height > USHRT_MAX) {
        fprintf(stderr, "G_BlightInit: Blight grid %ux%u exceeds datagram dimensions\n",
                (unsigned)level.blight.width, (unsigned)level.blight.height);
        G_BlightShutdown(); return;
    }
    cells = G_BlightCellCount();
    level.blight.cells = gi.MemAlloc(cells);
    if (!level.blight.cells) {
        fprintf(stderr, "G_BlightInit: failed to allocate %u-cell Blight grid\n", (unsigned)cells);
        G_BlightShutdown(); return;
    }
    memset(level.blight.cells, 0, cells);
    level.blight.dirty_rows = gi.MemAlloc(level.blight.height * sizeof(*level.blight.dirty_rows));
    if (!level.blight.dirty_rows) {
        fprintf(stderr, "G_BlightInit: failed to allocate %u Blight dirty rows\n", (unsigned)level.blight.height);
        G_BlightShutdown(); return;
    }
    memset(level.blight.dirty_rows, 0, level.blight.height * sizeof(*level.blight.dirty_rows));
    FOR_LOOP(y, level.blight.height) FOR_LOOP(x, level.blight.width) {
        uint8_t flags = 0;
        uint32_t const index = x + y * level.blight.width;
        sample.x = level.blight.bounds.min.x + ((float)x + 0.5f) * WC3_BLIGHT_PATH_CELL;
        sample.y = level.blight.bounds.min.y + ((float)y + 0.5f) * WC3_BLIGHT_PATH_CELL;
        if (!CM_GetPathingFlagsAt(&sample, &flags)) {
            unavailable++;
            continue;
        }
        level.blight.cells[index] = (flags & WC3_PATH_BLIGHTED) != 0;
    }
    BLIGHT_LOG("grid init dimensions=%ux%u cells=%u unavailable=%u\n",
            (unsigned)level.blight.width, (unsigned)level.blight.height,
            (unsigned)cells, (unsigned)unavailable);
    if (unavailable)
        fprintf(stderr, "G_BlightInit: pathing flags unavailable for %u Blight cells; leaving them clear\n",
                (unsigned)unavailable);
}

bool G_IsPointBlighted(LPCVECTOR2 point) {
    uint32_t x, y;
    return G_BlightCell(point, &x, &y) && level.blight.cells[x + y * level.blight.width] != 0;
}

void G_SetBlightPoint(LPCVECTOR2 point, bool add) {
    float x, y;
    uint32_t mask;
    if (!point || !level.blight.cells) return;
    mask = G_BlightConnectedMask();
    x = G_SnapBlightCorner(point->x, level.blight.bounds.min.x);
    y = G_SnapBlightCorner(point->y, level.blight.bounds.min.y);
    G_SetBlightCorner(x, y, add, mask);
    if (add) {
        BOX2 region = { { x - WC3_BLIGHT_TERRAIN_CELL, y - WC3_BLIGHT_TERRAIN_CELL },
                        { x + WC3_BLIGHT_TERRAIN_CELL, y + WC3_BLIGHT_TERRAIN_CELL } };
        G_BlightUpdateDestructables(&region);
    }
}

void G_SetBlightRadius(LPCVECTOR2 point, float radius, bool add) {
    float min_x, min_y, max_x, max_y;
    uint32_t mask;
    if (!point || !level.blight.cells || radius < 0.0f) return;
    mask = G_BlightConnectedMask();
    min_x = G_SnapBlightCorner(point->x - radius, level.blight.bounds.min.x);
    min_y = G_SnapBlightCorner(point->y - radius, level.blight.bounds.min.y);
    max_x = point->x + radius; max_y = point->y + radius;
    for (float y = min_y; y <= max_y; y += WC3_BLIGHT_TERRAIN_CELL)
        for (float x = min_x; x <= max_x; x += WC3_BLIGHT_TERRAIN_CELL) {
            float const dx = x - point->x, dy = y - point->y;
            if (dx * dx + dy * dy <= radius * radius) G_SetBlightCorner(x, y, add, mask);
        }
    if (add) {
        BOX2 region = { { point->x - radius, point->y - radius },
                        { point->x + radius, point->y + radius } };
        G_BlightUpdateDestructables(&region);
    }
}

void G_SetBlightRect(LPCBOX2 rect, bool add) {
    float min_x, min_y;
    uint32_t mask;
    if (!rect || !level.blight.cells) return;
    mask = G_BlightConnectedMask();
    min_x = G_SnapBlightCorner(rect->min.x, level.blight.bounds.min.x);
    min_y = G_SnapBlightCorner(rect->min.y, level.blight.bounds.min.y);
    for (float y = min_y; y <= rect->max.y; y += WC3_BLIGHT_TERRAIN_CELL)
        for (float x = min_x; x <= rect->max.x; x += WC3_BLIGHT_TERRAIN_CELL)
            if (x >= rect->min.x && y >= rect->min.y) G_SetBlightCorner(x, y, add, mask);
    if (add) G_BlightUpdateDestructables(rect);
}

uint32_t G_GetBlightStateSize(void) { return level.blight.cells ? G_BlightCellCount() : 0; }

bool G_GetBlightState(uint8_t * out, uint32_t size) {
    uint32_t const expected = G_GetBlightStateSize();
    if (size != expected || (size && !out)) return false;
    if (size) memcpy(out, level.blight.cells, size);
    return true;
}

bool G_SetBlightState(uint8_t const *data, uint32_t size) {
    uint32_t const expected = G_GetBlightStateSize();
    uint32_t const mask = G_BlightConnectedMask();
    if (size != expected || (size && !data)) return false;
    if (size) memcpy(level.blight.cells, data, size);
    if (level.blight.dirty_rows) FOR_LOOP(y, level.blight.height) level.blight.dirty_rows[y] |= mask;
    return true;
}

void G_BlightMarkClientFull(LPEDICT ent) {
    uint32_t player;
    if (!ent || !ent->client || !level.blight.dirty_rows) return;
    player = ent->client->ps.number;
    if (player >= MAX_PLAYERS) return;
    FOR_LOOP(y, level.blight.height) level.blight.dirty_rows[y] |= 1u << player;
    level.blight.sweep_row[player] = 0;
}

static bool G_BlightSweepDue(uint32_t player) {
    (void)player;
    if (!level.blight.width || !level.blight.height) return false;
    if (level.framenum == 0 || level.framenum % BLIGHT_SWEEP_INTERVAL) return false;
    return true;
}

bool G_BlightDatagramPending(LPEDICT ent) {
    uint32_t player;
    if (!ent || !ent->client || !level.blight.dirty_rows) return false;
    player = ent->client->ps.number;
    if (player >= MAX_PLAYERS) return false;
    FOR_LOOP(y, level.blight.height) if (level.blight.dirty_rows[y] & (1u << player)) return true;
    return G_BlightSweepDue(player);
}

typedef struct { uint32_t first_row, width; } blightPackCtx_t;

static uint8_t G_BlightPackBit(uint32_t index, void *ctx) {
    blightPackCtx_t *c = ctx;
    return level.blight.cells[c->first_row * c->width + index] ? 1 : 0;
}

static uint32_t G_BlightPackRows(uint8_t * out, uint32_t capacity, uint32_t first_row, uint32_t row_count) {
    blightPackCtx_t c = { first_row, level.blight.width };
    uint32_t const bits = level.blight.width * row_count;
    uint32_t n;
    if (!out || !bits) return 0;
    n = MSG_EncodeRLE(out, capacity, bits, G_BlightPackBit, &c);
    if (n) return n;
    /* RLE has no worst-case bound (an alternating row costs ~1 byte per bit); the bitpack escape
     * always fits one row inside the datagram reservation, so dirty/sweep progress never stalls. */
    return MSG_EncodeBitpack(out, capacity, bits, G_BlightPackBit, &c);
}

uint32_t G_BlightWriteDatagram(LPEDICT ent, uint8_t * data, uint32_t size) {
    uint32_t player, first = 0, rows, max_rows, available, payload_bytes;
    terrainMaskChunk_t chunk;
    uint8_t *payload;
    bool sweep = false;

    /* Smallest RLE payload is [init][run]. */
    if (!data || !G_BlightDatagramPending(ent) || size < sizeof(chunk) + 2) return 0;
    player = ent->client->ps.number;
    while (first < level.blight.height && !(level.blight.dirty_rows[first] & (1u << player))) first++;
    if (first < level.blight.height) {
        available = size - sizeof(chunk);
        /* Start from the whole contiguous dirty run; RLE usually compresses coherent Blight far below
         * bitpack density, and the shrink loop below absorbs overflow one row at a time. */
        max_rows = level.blight.height - first;
        if (!max_rows) return 0;
        rows = 1;
        while (rows < max_rows && (level.blight.dirty_rows[first + rows] & (1u << player))) rows++;
    } else {
        if (!G_BlightSweepDue(player)) return 0;
        first = level.blight.sweep_row[player] % level.blight.height;
        available = MIN(size - sizeof(chunk), BLIGHT_SWEEP_BYTES);
        /* Same optimistic start: one coherent sweep band can now cover far more rows per BLIGHT_SWEEP_BYTES. */
        max_rows = level.blight.height - first;
        if (!max_rows) { level.blight.sweep_row[player] = 0; return 0; }
        rows = max_rows;
        sweep = true;
    }
    payload = data + sizeof(chunk);
    while (rows && !(payload_bytes = G_BlightPackRows(payload, available, first, rows))) rows--;
    if (!rows) return 0;
    chunk = (terrainMaskChunk_t){
        .width = (uint16_t)level.blight.width, .height = (uint16_t)level.blight.height,
        .first_row = (uint16_t)first, .row_count = (uint16_t)rows,
        .payload_bytes = (uint16_t)payload_bytes, .reserved = 0,
        .min_x = level.blight.bounds.min.x, .min_y = level.blight.bounds.min.y,
        .cell_size = WC3_BLIGHT_PATH_CELL,
    };
    memcpy(data, &chunk, sizeof(chunk));
    if (sweep) level.blight.sweep_row[player] = (first + rows) % level.blight.height;
    else FOR_LOOP(y, rows) level.blight.dirty_rows[first + y] &= ~(1u << player);
    BLIGHT_LOG("datagram player=%u rows=%u..%u payload=%u sweep=%u\n",
            (unsigned)player, (unsigned)first, (unsigned)(first + rows - 1),
            (unsigned)payload_bytes, (unsigned)sweep);
    return sizeof(chunk) + payload_bytes;
}
