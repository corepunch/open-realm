#include "g_local.h"

#define WC3_BLIGHT_PATH_CELL 32.0f
#define WC3_BLIGHT_TERRAIN_CELL 128.0f

static DWORD G_BlightCellCount(void) { return level.blight.width * level.blight.height; }

static DWORD G_BlightConnectedMask(void) {
    DWORD mask = 0;
    FOR_LOOP(i, game.max_clients) {
        DWORD const player = game.clients[i].ps.number;
        if (game.clients[i].connected && player < MAX_PLAYERS) mask |= 1u << player;
    }
    return mask;
}

#ifdef WC3_DEBUG_BLIGHT
static DWORD G_BlightSetCellCount(void) {
    DWORD count = 0;
    if (!level.blight.cells) return 0;
    FOR_LOOP(i, G_BlightCellCount()) count += level.blight.cells[i] != 0;
    return count;
}
#endif

static BOOL G_BlightCell(LPCVECTOR2 point, LPDWORD x, LPDWORD y) {
    FLOAT fx, fy;
    if (!point || !level.blight.cells || !level.blight.width || !level.blight.height) return false;
    if (point->x < level.blight.bounds.min.x || point->y < level.blight.bounds.min.y ||
        point->x >= level.blight.bounds.max.x || point->y >= level.blight.bounds.max.y) return false;
    fx = (point->x - level.blight.bounds.min.x) / WC3_BLIGHT_PATH_CELL;
    fy = (point->y - level.blight.bounds.min.y) / WC3_BLIGHT_PATH_CELL;
    *x = MIN((DWORD)fx, level.blight.width - 1); *y = MIN((DWORD)fy, level.blight.height - 1);
    return true;
}

static BOOL G_SetBlightCell(LONG x, LONG y, BOOL add) {
    DWORD index;
    if (x < 0 || y < 0 || x >= (LONG)level.blight.width || y >= (LONG)level.blight.height) return false;
    index = (DWORD)x + (DWORD)y * level.blight.width;
    if (level.blight.cells[index] == (BYTE)(add != 0)) return false;
    level.blight.cells[index] = add ? 1 : 0;
    if (level.blight.dirty_rows) level.blight.dirty_rows[y] |= G_BlightConnectedMask();
    return true;
}

static DWORD G_SetBlightCorner(FLOAT x, FLOAT y, BOOL add) {
    VECTOR2 sample;
    DWORD ux, uy;
    LONG cx, cy;
    DWORD changed = 0;
    sample.x = x; sample.y = y;
    if (!G_BlightCell(&sample, &ux, &uy)) return 0;
    /* One 128-unit terrain corner owns the surrounding 4x4 32-unit pathing cells. */
    cx = (LONG)ux - 2; cy = (LONG)uy - 2;
    FOR_LOOP(ix, 4) FOR_LOOP(iy, 4)
        changed += G_SetBlightCell(cx + (LONG)ix, cy + (LONG)iy, add);
    return changed;
}

static FLOAT G_SnapBlightCorner(FLOAT value, FLOAT minimum) {
    return minimum + floorf((value - minimum) / WC3_BLIGHT_TERRAIN_CELL) * WC3_BLIGHT_TERRAIN_CELL;
}

void G_BlightShutdown(void) {
    SAFE_DELETE(level.blight.cells, gi.MemFree);
    SAFE_DELETE(level.blight.dirty_rows, gi.MemFree);
    memset(&level.blight, 0, sizeof(level.blight));
}

void G_BlightInit(void) {
    DWORD cells;
#ifdef WC3_DEBUG_BLIGHT
    DWORD seeded = 0, unavailable = 0;
#endif
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
#ifdef WC3_DEBUG_BLIGHT
            unavailable++;
#endif
            fprintf(stderr, "WC3 Blight: pathing flags unavailable at (%.1f,%.1f); leaving cell clear\n",
                    sample.x, sample.y);
            continue;
        }
        level.blight.cells[index] = (flags & WC3_PATH_BLIGHTED) != 0;
#ifdef WC3_DEBUG_BLIGHT
        seeded += level.blight.cells[index] != 0;
#endif
    }
#ifdef WC3_DEBUG_BLIGHT
    fprintf(stderr, "WC3_BLIGHT grid init bounds=(%.1f,%.1f)-(%.1f,%.1f) dimensions=%ux%u cells=%u seeded=%u unavailable=%u\n",
            level.blight.bounds.min.x, level.blight.bounds.min.y,
            level.blight.bounds.max.x, level.blight.bounds.max.y,
            (unsigned)level.blight.width, (unsigned)level.blight.height,
            (unsigned)cells, (unsigned)seeded, (unsigned)unavailable);
#endif
}

BOOL G_IsPointBlighted(LPCVECTOR2 point) {
    DWORD x, y;
    return G_BlightCell(point, &x, &y) && level.blight.cells[x + y * level.blight.width] != 0;
}

void G_SetBlightPoint(LPCVECTOR2 point, BOOL add) {
    FLOAT x, y;
    if (!point || !level.blight.cells) return;
    x = G_SnapBlightCorner(point->x, level.blight.bounds.min.x);
    y = G_SnapBlightCorner(point->y, level.blight.bounds.min.y);
#ifdef WC3_DEBUG_BLIGHT
    DWORD const changed = G_SetBlightCorner(x, y, add);
    fprintf(stderr, "WC3_BLIGHT point op=%s requested=(%.1f,%.1f) snapped=(%.1f,%.1f) changed=%u total=%u\n",
            add ? "add" : "remove", point->x, point->y, x, y,
            (unsigned)changed, (unsigned)G_BlightSetCellCount());
#else
    G_SetBlightCorner(x, y, add);
#endif
}

void G_SetBlightRadius(LPCVECTOR2 point, FLOAT radius, BOOL add) {
    FLOAT min_x, min_y, max_x, max_y;
    DWORD changed = 0, corners = 0;
    if (!point || !level.blight.cells || radius < 0.0f) return;
    min_x = G_SnapBlightCorner(point->x - radius, level.blight.bounds.min.x);
    min_y = G_SnapBlightCorner(point->y - radius, level.blight.bounds.min.y);
    max_x = point->x + radius; max_y = point->y + radius;
    for (FLOAT y = min_y; y <= max_y; y += WC3_BLIGHT_TERRAIN_CELL)
        for (FLOAT x = min_x; x <= max_x; x += WC3_BLIGHT_TERRAIN_CELL) {
            FLOAT const dx = x - point->x, dy = y - point->y;
            if (dx * dx + dy * dy <= radius * radius) {
                corners++; changed += G_SetBlightCorner(x, y, add);
            }
        }
#ifdef WC3_DEBUG_BLIGHT
    fprintf(stderr, "WC3_BLIGHT radius op=%s origin=(%.1f,%.1f) radius=%.1f corners=%u changed=%u total=%u\n",
            add ? "add" : "remove", point->x, point->y, radius,
            (unsigned)corners, (unsigned)changed, (unsigned)G_BlightSetCellCount());
#endif
}

void G_SetBlightRect(LPCBOX2 rect, BOOL add) {
    FLOAT min_x, min_y;
    DWORD changed = 0, corners = 0;
    if (!rect || !level.blight.cells) return;
    min_x = G_SnapBlightCorner(rect->min.x, level.blight.bounds.min.x);
    min_y = G_SnapBlightCorner(rect->min.y, level.blight.bounds.min.y);
    for (FLOAT y = min_y; y <= rect->max.y; y += WC3_BLIGHT_TERRAIN_CELL)
        for (FLOAT x = min_x; x <= rect->max.x; x += WC3_BLIGHT_TERRAIN_CELL)
            if (x >= rect->min.x && y >= rect->min.y) {
                corners++; changed += G_SetBlightCorner(x, y, add);
            }
#ifdef WC3_DEBUG_BLIGHT
    fprintf(stderr, "WC3_BLIGHT rect op=%s bounds=(%.1f,%.1f)-(%.1f,%.1f) corners=%u changed=%u total=%u\n",
            add ? "add" : "remove", rect->min.x, rect->min.y, rect->max.x, rect->max.y,
            (unsigned)corners, (unsigned)changed, (unsigned)G_BlightSetCellCount());
#endif
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
    if (size != expected || (size && !data)) return false;
    if (size) memcpy(level.blight.cells, data, size);
    if (level.blight.dirty_rows) {
        DWORD const mask = G_BlightConnectedMask();
        FOR_LOOP(y, level.blight.height) level.blight.dirty_rows[y] |= mask;
    }
#ifdef WC3_DEBUG_BLIGHT
    fprintf(stderr, "WC3_BLIGHT state restore bytes=%u active=%u\n",
            (unsigned)size, (unsigned)G_BlightSetCellCount());
#endif
    return true;
}

void G_BlightMarkClientFull(LPEDICT ent) {
    DWORD player;
    if (!ent || !ent->client || !level.blight.dirty_rows) return;
    player = ent->client->ps.number;
    if (player >= MAX_PLAYERS) return;
    FOR_LOOP(y, level.blight.height) level.blight.dirty_rows[y] |= 1u << player;
}

BOOL G_BlightDatagramPending(LPEDICT ent) {
    DWORD player;
    if (!ent || !ent->client || !level.blight.dirty_rows) return false;
    player = ent->client->ps.number;
    if (player >= MAX_PLAYERS) return false;
    FOR_LOOP(y, level.blight.height) if (level.blight.dirty_rows[y] & (1u << player)) return true;
    return false;
}

static DWORD G_BlightPackRows(LPBYTE out, DWORD capacity, DWORD first_row, DWORD row_count) {
    DWORD const cells = level.blight.width * row_count;
    DWORD const bytes = (cells + 7) / 8;

    if (!out || !cells) return 0;
    if (bytes > capacity) return 0;
    memset(out, 0, bytes);
    FOR_LOOP(index, cells)
        if (level.blight.cells[first_row * level.blight.width + index]) out[index >> 3] |= 1u << (index & 7);
    return bytes;
}

DWORD G_BlightWriteDatagram(LPEDICT ent, LPBYTE data, DWORD size) {
    DWORD player, first = 0, rows, max_rows, available, payload_bytes;
    wc3BlightChunk_t chunk;
    BYTE *payload;

    if (!data || !G_BlightDatagramPending(ent) || size < sizeof(chunk) + 1) return 0;
    player = ent->client->ps.number;
    while (first < level.blight.height && !(level.blight.dirty_rows[first] & (1u << player))) first++;
    if (first == level.blight.height) return 0;
    available = size - sizeof(chunk);
    max_rows = MIN(level.blight.height - first, (available * 8) / level.blight.width);
    if (!max_rows) return 0;
    rows = 1;
    while (rows < max_rows && (level.blight.dirty_rows[first + rows] & (1u << player))) rows++;
    payload = data + sizeof(chunk);
    while (rows && !(payload_bytes = G_BlightPackRows(payload, available, first, rows))) rows--;
    if (!rows) return 0;
    chunk = (wc3BlightChunk_t){
        .width = (USHORT)level.blight.width, .height = (USHORT)level.blight.height,
        .first_row = (USHORT)first, .row_count = (USHORT)rows,
        .payload_bytes = (USHORT)payload_bytes,
        .min_x = level.blight.bounds.min.x, .min_y = level.blight.bounds.min.y,
        .cell_size = WC3_BLIGHT_PATH_CELL,
    };
    memcpy(data, &chunk, sizeof(chunk));
    FOR_LOOP(y, rows) level.blight.dirty_rows[first + y] &= ~(1u << player);
#ifdef WC3_DEBUG_BLIGHT
    fprintf(stderr, "WC3_BLIGHT datagram player=%u rows=%u..%u payload=%u active=%u\n",
            (unsigned)player, (unsigned)first, (unsigned)(first + rows - 1),
            (unsigned)payload_bytes, (unsigned)G_BlightSetCellCount());
#endif
    return sizeof(chunk) + payload_bytes;
}
