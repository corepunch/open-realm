#include "g_local.h"

#define WC3_BLIGHT_PATH_CELL 32.0f // world units; authoritative Blight cell width; used by simulation and network mask
#define WC3_BLIGHT_TERRAIN_CELL 128.0f // world units; authored terrain-corner spacing; used by Blight rasterization

static DWORD G_BlightCellCount(void) { return level.blight.width * level.blight.height; }

static DWORD G_BlightConnectedMask(void) {
    DWORD mask = 0;
    FOR_LOOP(i, game.max_clients) {
        DWORD const player = game.clients[i].ps.number;
        if (game.clients[i].connected && player < MAX_PLAYERS) mask |= 1u << player;
    }
    return mask;
}

static BOOL G_BlightCell(LPCVECTOR2 point, LPDWORD x, LPDWORD y) {
    if (!point || !level.blight.cells) return false;
    return TerrainMask_CellForPoint(level.blight.bounds.min, WC3_BLIGHT_PATH_CELL, level.blight.width, level.blight.height, point, x, y);
}

static BOOL G_SetBlightCell(LONG x, LONG y, BOOL add, DWORD mask) {
    DWORD index;
    if (x < 0 || y < 0 || x >= (LONG)level.blight.width || y >= (LONG)level.blight.height) return false;
    index = (DWORD)x + (DWORD)y * level.blight.width;
    if (level.blight.cells[index] == (BYTE)(add != 0)) return false;
    level.blight.cells[index] = add ? 1 : 0;
    if (level.blight.dirty_rows) level.blight.dirty_rows[y] |= mask;
    return true;
}

static DWORD G_SetBlightCorner(FLOAT x, FLOAT y, BOOL add, DWORD mask) {
    VECTOR2 sample;
    DWORD ux, uy;
    LONG cx, cy;
    DWORD changed = 0;
    sample.x = x; sample.y = y;
    if (!G_BlightCell(&sample, &ux, &uy)) return 0;
    /* One 128-unit terrain corner owns the surrounding 4x4 32-unit pathing cells. */
    cx = (LONG)ux - 2; cy = (LONG)uy - 2;
    FOR_LOOP(ix, 4) FOR_LOOP(iy, 4)
        changed += G_SetBlightCell(cx + (LONG)ix, cy + (LONG)iy, add, mask);
    return changed;
}

static FLOAT G_SnapBlightCorner(FLOAT value, FLOAT minimum) {
    return minimum + floorf((value - minimum) / WC3_BLIGHT_TERRAIN_CELL) * WC3_BLIGHT_TERRAIN_CELL;
}

static BOOL G_BlightDestructableFootprintBlighted(LPCEDICT ent) {
    pathTex_t const *pathtex;

    if (!ent || !G_IsDestructable(ent) || ent->destructable.dead) return false;
    pathtex = ent->destructable.alive_pathtex;
    if (!pathtex || !pathtex->width || !pathtex->height)
        return G_IsPointBlighted(&ent->s.origin2);
    FOR_LOOP(y, pathtex->height) FOR_LOOP(x, pathtex->width) {
        VECTOR2 sample;
        if (!pathtex->map[x + y * pathtex->width].b) continue;
        sample.x = ent->s.origin2.x + ((FLOAT)x + 0.5f - (FLOAT)pathtex->width * 0.5f) * WC3_BLIGHT_PATH_CELL;
        sample.y = ent->s.origin2.y + ((FLOAT)y + 0.5f - (FLOAT)pathtex->height * 0.5f) * WC3_BLIGHT_PATH_CELL;
        if (!G_IsPointBlighted(&sample)) return false;
    }
    return true;
}

void G_BlightMarkDestructable(LPEDICT ent) {
    DestructableData_t const *data;
    PATHSTR blight_texture;
    LPCSTR dot;

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
                blight_texture, (LPCSTR)&ent->class_id);
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
    DWORD cells;
    DWORD unavailable = 0;
    VECTOR2 sample;

    G_BlightShutdown();
    level.blight.bounds = CM_GetWorldBounds();
    level.blight.width = MAX(1u, (DWORD)ceilf((level.blight.bounds.max.x - level.blight.bounds.min.x) / WC3_BLIGHT_PATH_CELL));
    level.blight.height = MAX(1u, (DWORD)ceilf((level.blight.bounds.max.y - level.blight.bounds.min.y) / WC3_BLIGHT_PATH_CELL));
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
        BYTE flags = 0;
        DWORD const index = x + y * level.blight.width;
        sample.x = level.blight.bounds.min.x + ((FLOAT)x + 0.5f) * WC3_BLIGHT_PATH_CELL;
        sample.y = level.blight.bounds.min.y + ((FLOAT)y + 0.5f) * WC3_BLIGHT_PATH_CELL;
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

BOOL G_IsPointBlighted(LPCVECTOR2 point) {
    DWORD x, y;
    return G_BlightCell(point, &x, &y) && level.blight.cells[x + y * level.blight.width] != 0;
}

void G_SetBlightPoint(LPCVECTOR2 point, BOOL add) {
    FLOAT x, y;
    DWORD mask;
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

void G_SetBlightRadius(LPCVECTOR2 point, FLOAT radius, BOOL add) {
    FLOAT min_x, min_y, max_x, max_y;
    DWORD mask;
    if (!point || !level.blight.cells || radius < 0.0f) return;
    mask = G_BlightConnectedMask();
    min_x = G_SnapBlightCorner(point->x - radius, level.blight.bounds.min.x);
    min_y = G_SnapBlightCorner(point->y - radius, level.blight.bounds.min.y);
    max_x = point->x + radius; max_y = point->y + radius;
    for (FLOAT y = min_y; y <= max_y; y += WC3_BLIGHT_TERRAIN_CELL)
        for (FLOAT x = min_x; x <= max_x; x += WC3_BLIGHT_TERRAIN_CELL) {
            FLOAT const dx = x - point->x, dy = y - point->y;
            if (dx * dx + dy * dy <= radius * radius) G_SetBlightCorner(x, y, add, mask);
        }
    if (add) {
        BOX2 region = { { point->x - radius, point->y - radius },
                        { point->x + radius, point->y + radius } };
        G_BlightUpdateDestructables(&region);
    }
}

void G_SetBlightRect(LPCBOX2 rect, BOOL add) {
    FLOAT min_x, min_y;
    DWORD mask;
    if (!rect || !level.blight.cells) return;
    mask = G_BlightConnectedMask();
    min_x = G_SnapBlightCorner(rect->min.x, level.blight.bounds.min.x);
    min_y = G_SnapBlightCorner(rect->min.y, level.blight.bounds.min.y);
    for (FLOAT y = min_y; y <= rect->max.y; y += WC3_BLIGHT_TERRAIN_CELL)
        for (FLOAT x = min_x; x <= rect->max.x; x += WC3_BLIGHT_TERRAIN_CELL)
            if (x >= rect->min.x && y >= rect->min.y) G_SetBlightCorner(x, y, add, mask);
    if (add) G_BlightUpdateDestructables(rect);
}

DWORD G_GetBlightStateSize(void) { return level.blight.cells ? G_BlightCellCount() : 0; }

BOOL G_GetBlightState(LPBYTE out, DWORD size) {
    DWORD const expected = G_GetBlightStateSize();
    if (size != expected || (size && !out)) return false;
    if (size) memcpy(out, level.blight.cells, size);
    return true;
}

BOOL G_SetBlightState(BYTE const *data, DWORD size) {
    DWORD const expected = G_GetBlightStateSize();
    DWORD const mask = G_BlightConnectedMask();
    if (size != expected || (size && !data)) return false;
    if (size) memcpy(level.blight.cells, data, size);
    if (level.blight.dirty_rows) FOR_LOOP(y, level.blight.height) level.blight.dirty_rows[y] |= mask;
    return true;
}

void G_BlightMarkClientFull(LPEDICT ent) {
    DWORD player;
    if (!ent || !ent->client || !level.blight.dirty_rows) return;
    player = ent->client->ps.number;
    if (player >= MAX_PLAYERS) return;
    FOR_LOOP(y, level.blight.height) level.blight.dirty_rows[y] |= 1u << player;
    level.blight.sweep_row[player] = 0;
}

static BOOL G_BlightSweepDue(DWORD player) {
    (void)player;
    if (!level.blight.width || !level.blight.height) return false;
    if (level.framenum == 0 || level.framenum % BLIGHT_SWEEP_INTERVAL) return false;
    return true;
}

BOOL G_BlightDatagramPending(LPEDICT ent) {
    DWORD player;
    if (!ent || !ent->client || !level.blight.dirty_rows) return false;
    player = ent->client->ps.number;
    if (player >= MAX_PLAYERS) return false;
    FOR_LOOP(y, level.blight.height) if (level.blight.dirty_rows[y] & (1u << player)) return true;
    return G_BlightSweepDue(player);
}

typedef struct { DWORD first_row, width; } blightPackCtx_t;

static BYTE G_BlightPackBit(DWORD index, void *ctx) {
    blightPackCtx_t *c = ctx;
    return level.blight.cells[c->first_row * c->width + index] ? 1 : 0;
}

static DWORD G_BlightPackRows(LPBYTE out, DWORD capacity, DWORD first_row, DWORD row_count) {
    blightPackCtx_t c = { first_row, level.blight.width };
    DWORD const bits = level.blight.width * row_count;
    if (!out || !bits) return 0;
    return MSG_EncodeRLE(out, capacity, bits, G_BlightPackBit, &c);
}

DWORD G_BlightWriteDatagram(LPEDICT ent, LPBYTE data, DWORD size) {
    DWORD player, first = 0, rows, max_rows, available, payload_bytes, sweep_cap;
    terrainMaskChunk_t chunk;
    BYTE *payload;
    BOOL sweep = false;

    /* Smallest RLE payload is [init][run]. */
    if (!data || !G_BlightDatagramPending(ent) || size < sizeof(chunk) + 2) return 0;
    player = ent->client->ps.number;
    while (first < level.blight.height && !(level.blight.dirty_rows[first] & (1u << player))) first++;
    if (first < level.blight.height) {
        available = size - sizeof(chunk);
        /* Bitpack-scale row guess; the RLE shrink loop below absorbs overflow. */
        max_rows = MIN(level.blight.height - first, (available * 8) / level.blight.width);
        if (!max_rows) return 0;
        rows = 1;
        while (rows < max_rows && (level.blight.dirty_rows[first + rows] & (1u << player))) rows++;
    } else {
        if (!G_BlightSweepDue(player)) return 0;
        first = level.blight.sweep_row[player] % level.blight.height;
        available = MIN(size - sizeof(chunk), BLIGHT_SWEEP_BYTES);
        /* Bitpack-scale row guess; the RLE shrink loop below absorbs overflow. */
        max_rows = MIN(level.blight.height - first, (available * 8) / level.blight.width);
        if (!max_rows) { level.blight.sweep_row[player] = 0; return 0; }
        rows = max_rows;
        /* Sweep-band row cap at bitpack density. */
        sweep_cap = (BLIGHT_SWEEP_BYTES * 8) / level.blight.width;
        if (sweep_cap && rows > sweep_cap) rows = sweep_cap;
        sweep = true;
    }
    payload = data + sizeof(chunk);
    while (rows && !(payload_bytes = G_BlightPackRows(payload, available, first, rows))) rows--;
    if (!rows) return 0;
    chunk = (terrainMaskChunk_t){
        .width = (USHORT)level.blight.width, .height = (USHORT)level.blight.height,
        .first_row = (USHORT)first, .row_count = (USHORT)rows,
        .payload_bytes = (USHORT)payload_bytes, .reserved = 0,
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
