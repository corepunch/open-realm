#include "g_local.h"

#define FOW_INVALID_CELL 0xffffffffu
#define FOW_PATHING_PIXEL_SIZE 32.0f
#define FOW_TREE_DILATION_CELLS 1
#define FOW_BLOCKER_LIGHT_MARGIN_CELLS 1
#define G_FOW_CELL_INDEX(x, y) ((y) * level.fow.width + (x))
#define G_FOW_SET_VISIBLE_CELL(grid, x, y) do { \
    DWORD fow_index_ = G_FOW_CELL_INDEX((DWORD)(x), (DWORD)(y)); \
    if (!(grid)->visible[fow_index_]) { \
        (grid)->visible[fow_index_] = 1; \
        (grid)->visible_rows[(DWORD)(y)] = 1; \
        if ((grid)->dirty_visible_rows) { \
            (grid)->dirty_visible_rows[(DWORD)(y)] = 1; \
        } \
    } \
    if (!(grid)->explored[fow_index_]) { \
        (grid)->explored[fow_index_] = 1; \
        if ((grid)->dirty_explored_rows) { \
            (grid)->dirty_explored_rows[(DWORD)(y)] = 1; \
        } \
    } \
} while (0)

static DWORD g_fow_blocker_hash;
static DWORD g_fow_blocker_count;
static BOOL g_fow_blockers_valid;
static BOOL g_fow_blockers_dirty = true;
#ifdef WC3_FOW_PACKED_MASK
static BOOL g_fow_fast;
#endif

static DWORD G_FowCellCount(void) {
    return level.fow.width * level.fow.height;
}

static DWORD G_FowCellIndex(DWORD x, DWORD y) {
    return G_FOW_CELL_INDEX(x, y);
}

static BOOL G_FowReady(void) {
    return level.fow.width > 0 && level.fow.height > 0;
}

static BOOL G_FowSharedVision(DWORD viewer, DWORD owner) {
    if (viewer >= MAX_PLAYERS || owner >= MAX_PLAYERS) {
        return false;
    }
    return viewer == owner ||
           (level.alliances[viewer][owner] & (1 << ALLIANCE_SHARED_VISION)) ||
           (level.alliances[viewer][owner] & (1 << ALLIANCE_SHARED_VISION_FORCED));
}

DWORD G_FowWorldToCellX(FLOAT x) {
    if (!G_FowReady()) {
        return FOW_INVALID_CELL;
    }
    int cell = (int)floorf((x - level.fow.bounds.min.x) / (FLOAT)FOW_CELL_SIZE);
    if (cell < 0) {
        return 0;
    }
    if ((DWORD)cell >= level.fow.width) {
        return level.fow.width - 1;
    }
    return (DWORD)cell;
}

DWORD G_FowWorldToCellY(FLOAT y) {
    if (!G_FowReady()) {
        return FOW_INVALID_CELL;
    }
    int cell = (int)floorf((y - level.fow.bounds.min.y) / (FLOAT)FOW_CELL_SIZE);
    if (cell < 0) {
        return 0;
    }
    if ((DWORD)cell >= level.fow.height) {
        return level.fow.height - 1;
    }
    return (DWORD)cell;
}

static void G_FowSetVisible(fowPlayerGrid_t *grid, DWORD x, DWORD y) {
    if (!grid || !grid->visible || !grid->explored ||
        x >= level.fow.width || y >= level.fow.height) {
        return;
    }

    G_FOW_SET_VISIBLE_CELL(grid, x, y);
}

static BOOL G_FowStateValid(DWORD state) { return state && state <= WC3_FOG_STATE_VISIBLE && !(state & (state - 1)); }

typedef struct {
    DWORD x, y, state;
    int cells;
} FOGDISK;
typedef FOGDISK *LPFOGDISK;
typedef FOGDISK const *LPCFOGDISK;

/* Scripted fog states own both planes, so the three JASS states need no parallel cinematic map. */
static void G_FowSetCellState(fowPlayerGrid_t *grid, DWORD index, DWORD state) {
    DWORD y, x;
    if (!grid || !grid->visible || !grid->explored || index >= G_FowCellCount()) return;
    y = index / level.fow.width;
    x = index - y * level.fow.width;
#ifdef WC3_FOW_PACKED_MASK
    if (g_fow_fast) {
        WORD *visible = grid->packed_visible + (x >> 4) + y * grid->packed_stride;
        WORD *explored = grid->packed_explored + (x >> 4) + y * grid->packed_stride;
        WORD bit = (WORD)(1u << (x & 15));
        /* Scripted fog writes must update the packed planes read in fast mode, not only the legacy byte planes. */
        if (state == WC3_FOG_STATE_VISIBLE) *visible |= bit, *explored |= bit;
        else if (state == WC3_FOG_STATE_FOGGED) *visible &= ~bit, *explored |= bit;
        else *visible &= ~bit, *explored &= ~bit;
    }
#endif
    if (state == WC3_FOG_STATE_VISIBLE) {
        G_FOW_SET_VISIBLE_CELL(grid, x, y);
        return;
    }
    if (grid->visible[index]) {
        grid->visible[index] = 0;
        if (grid->dirty_visible_rows) grid->dirty_visible_rows[y] = 1;
    }
    if (state == WC3_FOG_STATE_FOGGED) {
        if (!grid->explored[index]) {
            grid->explored[index] = 1;
            if (grid->dirty_explored_rows) grid->dirty_explored_rows[y] = 1;
        }
        return;
    }
    if (state == WC3_FOG_STATE_MASKED && grid->explored[index]) {
        grid->explored[index] = 0;
        if (grid->dirty_explored_rows) grid->dirty_explored_rows[y] = 1;
    }
}

static void G_FowSetBlocked(DWORD x, DWORD y) {
    DWORD index;

    if (!level.fow.blocked || x >= level.fow.width || y >= level.fow.height) {
        return;
    }
    index = G_FowCellIndex(x, y);
    if (!level.fow.blocked[index]) {
        level.fow.blocked[index] = 1;
        level.fow.num_blocked++;
    }
}

static void G_FowSetBlockedDilated(DWORD x, DWORD y, int dilation) {
    for (int dy = -dilation; dy <= dilation; dy++) {
        int by = (int)y + dy;
        if (by < 0 || by >= (int)level.fow.height) {
            continue;
        }
        for (int dx = -dilation; dx <= dilation; dx++) {
            int bx = (int)x + dx;
            if (bx < 0 || bx >= (int)level.fow.width) {
                continue;
            }
            G_FowSetBlocked((DWORD)bx, (DWORD)by);
        }
    }
}

static void G_FowClearVisible(fowPlayerGrid_t *grid) {
    if (!grid || !grid->visible || !grid->visible_rows || !grid->dirty_visible_rows) {
        return;
    }
    FOR_LOOP(y, level.fow.height) {
        if (!grid->visible_rows[y]) continue;
        /* Visibility writers mark occupied rows, so clearing no longer scans every cell of a mostly hidden map. */
        memset(grid->visible + y * level.fow.width, 0, level.fow.width);
#ifdef WC3_FOW_PACKED_MASK
        memset(grid->packed_visible + y * grid->packed_stride, 0, grid->packed_stride * sizeof(*grid->packed_visible));
#endif
        grid->visible_rows[y] = 0;
        grid->dirty_visible_rows[y] = 1;
    }
}

static BOOL G_FowAnyBlockedInBox(int minx, int miny, int maxx, int maxy) {
    if (!level.fow.blocked || !level.fow.num_blocked) {
        return false;
    }

    minx = MAX(minx, 0);
    miny = MAX(miny, 0);
    maxx = MIN(maxx, (int)level.fow.width - 1);
    maxy = MIN(maxy, (int)level.fow.height - 1);
    for (int y = miny; y <= maxy; y++) {
        BYTE const *row = level.fow.blocked + y * level.fow.width;
        for (int x = minx; x <= maxx; x++) {
            if (row[x]) {
                return true;
            }
        }
    }
    return false;
}

static int G_FowRadiusCells(FLOAT radius) {
    return MAX(1, (int)ceilf(radius / (FLOAT)FOW_CELL_SIZE));
}

/* Circular trigger/modifier state writes reuse the ordinary fog-grid rasterization. */
static void G_FowSetDiskState(fowPlayerGrid_t *grid, LPCFOGDISK disk) {
    int radius_sq = disk->cells * disk->cells;

    for (int dy = -disk->cells; dy <= disk->cells; dy++) {
        int y = (int)disk->y + dy;
        int max_dx;
        if (y < 0 || y >= (int)level.fow.height) {
            continue;
        }
        max_dx = (int)sqrtf((FLOAT)(radius_sq - dy * dy));
        for (int dx = -max_dx; dx <= max_dx; dx++) {
            int x = (int)disk->x + dx;
            if (x < 0 || x >= (int)level.fow.width) {
                continue;
            }
            G_FowSetCellState(grid, G_FowCellIndex((DWORD)x, (DWORD)y), disk->state);
        }
    }
}
static void G_FowRevealDisk(fowPlayerGrid_t *grid, DWORD cx, DWORD cy, int radius_cells) {
    FOGDISK disk = { cx, cy, WC3_FOG_STATE_VISIBLE, radius_cells };
    G_FowSetDiskState(grid, &disk);
}

#ifdef WC3_FOW_PACKED_MASK
/* Apply retail's packed horizontal spans; the byte plane is materialized once after all revealers. */
static void G_FowRevealPacked(fowPlayerGrid_t *grid, DWORD cx, DWORD cy, int radius_cells) {
    static WORD const bit[16] = {
        0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
        0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
    };
    int radius_sq = radius_cells * radius_cells;

    for (int dy = -radius_cells; dy <= radius_cells; dy++) {
        int y = (int)cy + dy;
        int max_dx;
        int min_x;
        int max_x;

        if (y < 0 || y >= (int)level.fow.height) continue;
        max_dx = (int)sqrtf((FLOAT)(radius_sq - dy * dy));
        min_x = MAX(0, (int)cx - max_dx);
        max_x = MIN((int)level.fow.width - 1, (int)cx + max_dx);
        for (int x = min_x; x <= max_x;) {
            int word = x >> 4;
            int first = x & 15;
            int last = MIN(15, max_x - (word << 4));
            WORD mask = 0;

            for (int bit_index = first; bit_index <= last; bit_index++) mask |= bit[bit_index];
            grid->packed_visible[word + y * grid->packed_stride] |= mask;
            grid->packed_explored[word + y * grid->packed_stride] |= mask;
            grid->visible_rows[y] = 1;
            grid->dirty_visible_rows[y] = 1;
            grid->dirty_explored_rows[y] = 1;
            x = (word + 1) << 4;
        }
    }
}

static void G_FowRevealPackedBox(fowPlayerGrid_t *grid, DWORD x0, DWORD y0, DWORD x1, DWORD y1) {
    FOR_LOOP(y, y1 - y0 + 1) {
        DWORD const row = y + y0;
        DWORD x = x0;
        while (x <= x1) {
            DWORD const word = x >> 4;
            DWORD const first = x & 15;
            DWORD const last = MIN(15, x1 - (word << 4));
            WORD mask = 0;
            for (DWORD bit_index = first; bit_index <= last; bit_index++) mask |= (WORD)(1u << bit_index);
            grid->packed_visible[word + row * grid->packed_stride] |= mask;
            grid->packed_explored[word + row * grid->packed_stride] |= mask;
            grid->visible_rows[row] = grid->dirty_visible_rows[row] = 1;
            grid->dirty_explored_rows[row] = 1;
            x = (word + 1) << 4;
        }
    }
}

static BOOL G_FowPackedAt(WORD const *plane, fowPlayerGrid_t const *grid, DWORD x, DWORD y) {
    return plane[(x >> 4) + y * grid->packed_stride] & (1u << (x & 15));
}
#endif

static void G_FowCastLight(fowPlayerGrid_t *grid,
                           int cx,
                           int cy,
                           int row,
                           FLOAT start,
                           FLOAT end,
                           int radius,
                           int xx,
                           int xy,
                           int yx,
                           int yy)
{
    int radius_sq = radius * radius;

    if (start < end) {
        return;
    }

    for (int distance = row; distance <= radius; distance++) {
        BOOL blocked = false;
        FLOAT next_start = start;
        int delta_y = -distance;

        for (int delta_x = -distance; delta_x <= 0; delta_x++) {
            int x = cx + delta_x * xx + delta_y * xy;
            int y = cy + delta_x * yx + delta_y * yy;
            FLOAT left_slope = ((FLOAT)delta_x - 0.5f) / ((FLOAT)delta_y + 0.5f);
            FLOAT right_slope = ((FLOAT)delta_x + 0.5f) / ((FLOAT)delta_y - 0.5f);
            BOOL in_bounds;
            BOOL cell_blocked;

            if (start < right_slope) {
                continue;
            }
            if (end > left_slope) {
                break;
            }

            in_bounds = x >= 0 && y >= 0 &&
                        x < (int)level.fow.width && y < (int)level.fow.height;
            cell_blocked = in_bounds &&
                           level.fow.blocked[G_FOW_CELL_INDEX((DWORD)x, (DWORD)y)] != 0;
            if (in_bounds && delta_x * delta_x + delta_y * delta_y <= radius_sq)
            {
                G_FOW_SET_VISIBLE_CELL(grid, x, y);
            }

            if (blocked) {
                if (cell_blocked) {
                    next_start = right_slope;
                    continue;
                }
                blocked = false;
                start = next_start;
            } else if (cell_blocked && distance < radius) {
                blocked = true;
                G_FowCastLight(grid,
                               cx,
                               cy,
                               distance + 1,
                               start,
                               left_slope,
                               radius,
                               xx,
                               xy,
                               yx,
                               yy);
                next_start = right_slope;
            }
        }
        if (blocked) {
            break;
        }
    }
}

static void G_FowRevealShadowcast(fowPlayerGrid_t *grid, DWORD cx, DWORD cy, int radius_cells) {
    static int const mult[8][4] = {
        { 1,  0,  0,  1 },
        { 0,  1,  1,  0 },
        { 0, -1,  1,  0 },
        { -1, 0,  0,  1 },
        { -1, 0,  0, -1 },
        { 0, -1, -1,  0 },
        { 0,  1, -1,  0 },
        { 1,  0,  0, -1 },
    };

    G_FowSetVisible(grid, cx, cy);
    FOR_LOOP(octant, 8) {
        G_FowCastLight(grid,
                       (int)cx,
                       (int)cy,
                       1,
                       1.0f,
                       0.0f,
                       radius_cells,
                       mult[octant][0],
                       mult[octant][1],
                       mult[octant][2],
                       mult[octant][3]);
    }
}

static BOOL G_FowHasVisibleNeighbor(fowPlayerGrid_t *grid, int x, int y, int margin) {
    int margin_sq = margin * margin;

    for (int dy = -margin; dy <= margin; dy++) {
        int ny = y + dy;
        if (ny < 0 || ny >= (int)level.fow.height) {
            continue;
        }
        for (int dx = -margin; dx <= margin; dx++) {
            int nx = x + dx;
            DWORD index;

            if (nx < 0 || nx >= (int)level.fow.width) {
                continue;
            }
            if (dx * dx + dy * dy > margin_sq) {
                continue;
            }
            index = G_FOW_CELL_INDEX((DWORD)nx, (DWORD)ny);
            if (grid->visible[index]) {
                return true;
            }
        }
    }
    return false;
}

/* Commit only marked blockers; the old second square walk revisited over 10K cells per Human02 update. */
static void G_FowCommitRimCells(fowPlayerGrid_t *grid, DWORD count) {
    FOR_LOOP(i, count) {
        DWORD const index = level.fow.rim_cells[i];
        DWORD const y = index / level.fow.width;
        DWORD const x = index - y * level.fow.width;

        grid->visible[index] = 0;
        G_FOW_SET_VISIBLE_CELL(grid, x, y);
    }
}

static void G_FowRevealBlockerRim(fowPlayerGrid_t *grid, DWORD cx, DWORD cy, int radius_cells) {
    int margin = FOW_BLOCKER_LIGHT_MARGIN_CELLS;
    int max_radius = radius_cells + margin;
    int max_radius_sq = max_radius * max_radius;
    DWORD rim_count = 0;

    for (int dy = -max_radius; dy <= max_radius; dy++) {
        int y = (int)cy + dy;
        if (y < 0 || y >= (int)level.fow.height) {
            continue;
        }
        for (int dx = -max_radius; dx <= max_radius; dx++) {
            int x = (int)cx + dx;
            DWORD index;

            if (x < 0 || x >= (int)level.fow.width) {
                continue;
            }
            if (dx * dx + dy * dy > max_radius_sq) {
                continue;
            }

            index = G_FOW_CELL_INDEX((DWORD)x, (DWORD)y);
            if (!level.fow.blocked[index]) {
                continue;
            }
            if (!grid->visible[index] &&
                G_FowHasVisibleNeighbor(grid, x, y, margin))
            {
                grid->visible[index] = 2;
                level.fow.rim_cells[rim_count++] = index;
            }
        }
    }
    G_FowCommitRimCells(grid, rim_count);
}

static void G_FowRevealCircle(DWORD player, LPCEDICT ent, FLOAT radius) {
    fowPlayerGrid_t *grid;
    DWORD cx, cy;
    int radius_cells;

    if (player >= MAX_PLAYERS || !ent || radius <= 0.0f || !G_FowReady()) {
        return;
    }

    grid = &level.fow.players[player];
    cx = G_FowWorldToCellX(ent->s.origin.x);
    cy = G_FowWorldToCellY(ent->s.origin.y);
    if (cx == FOW_INVALID_CELL || cy == FOW_INVALID_CELL) {
        return;
    }

    radius_cells = G_FowRadiusCells(radius);
#ifdef WC3_FOW_PACKED_MASK
    /* This removable experiment mirrors retail's packed-word mask shape but
     * intentionally trades blocker precision for bounded reveal work. */
    if (g_fow_fast) {
        G_FowRevealPacked(grid, cx, cy, radius_cells);
        return;
    }
#endif
    if (G_FowAnyBlockedInBox((int)cx - radius_cells,
                             (int)cy - radius_cells,
                             (int)cx + radius_cells,
                             (int)cy + radius_cells))
    {
        G_FowRevealShadowcast(grid, cx, cy, radius_cells);
        G_FowRevealBlockerRim(grid, cx, cy, radius_cells);
    } else {
        G_FowRevealDisk(grid, cx, cy, radius_cells);
    }
}

/* MiscData owns the dawn/dusk thresholds. The same authoritative simulation
 * time drives sight, regeneration, JASS game state and future presentation. */
BOOL G_IsNight(void) {
    FLOAT const time = G_GetTimeOfDay();
    return !(time >= game.constants.dawnTimeGameHours &&
             time < game.constants.duskTimeGameHours);
}

static FLOAT G_FowEntitySightRadius(LPCEDICT ent) {
    FLOAT day;
    FLOAT night;

    if (!ent) {
        return 0.0f;
    }
    day = ent->runtime.sight_radius.day;
    night = ent->runtime.sight_radius.night;
    /* Use the day or night sight radius based on time of day, rather than
     * always taking the larger of the two. */
    if (night <= 0.0f) night = day;
    if (day <= 0.0f) day = night;
    return G_IsNight() ? night : day;
}

static BOOL G_FowEntityIsRevealer(LPCEDICT ent) {
    if (!ent || !ent->inuse || ent->s.player >= MAX_PLAYERS) {
        return false;
    }
    if (ent->svflags & SVF_NOCLIENT) {
        return false;
    }
    if (ent->s.renderfx & RF_HIDDEN) {
        return false;
    }
    if (M_IsDead((LPEDICT)ent)) {
        return false;
    }
    return G_FowEntitySightRadius(ent) > 0.0f;
}

static BOOL G_FowEntityIsBlocker(LPCEDICT ent) {
    if (!ent || !ent->inuse || !(ent->s.flags & EF_FOW_BLOCKER)) {
        return false;
    }
    if (ent->s.renderfx & RF_HIDDEN) {
        return false;
    }
    if (M_IsDead((LPEDICT)ent)) {
        return false;
    }
    return true;
}

static DWORD G_FowHashMix(DWORD hash, DWORD value) {
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

static DWORD G_FowHashFloat(DWORD hash, FLOAT value) {
    DWORD bits;

    memcpy(&bits, &value, sizeof(bits));
    return G_FowHashMix(hash, bits);
}

static DWORD G_FowHashPointer(DWORD hash, void const *ptr) {
    DWORD_PTR value = (DWORD_PTR)ptr;

    hash = G_FowHashMix(hash, (DWORD)value);
    return G_FowHashMix(hash, (DWORD)(value >> 16 >> 16));
}

/* Blocker owners call this after a lifecycle change so steady updates avoid hashing every edict. */
void G_FowMarkBlockersDirty(void) { g_fow_blockers_dirty = true; }

static BOOL G_FowBlockersChanged(void) {
    DWORD hash = 2166136261u;
    DWORD count = 0;

    if (!g_fow_blockers_dirty) return false;
    g_fow_blockers_dirty = false;
    FOR_LOOP(i, globals.num_edicts) {
        LPCEDICT ent = &g_edicts[i];

        if (!G_FowEntityIsBlocker(ent)) {
            continue;
        }

        count++;
        hash = G_FowHashMix(hash, i);
        hash = G_FowHashMix(hash, ent->s.flags);
        hash = G_FowHashMix(hash, ent->s.renderfx);
        hash = G_FowHashFloat(hash, ent->s.origin.x);
        hash = G_FowHashFloat(hash, ent->s.origin.y);
        hash = G_FowHashFloat(hash, ent->s.radius);
        hash = G_FowHashFloat(hash, ent->s.scale);
        hash = G_FowHashFloat(hash, ent->collision);
        hash = G_FowHashFloat(hash, ent->health.value);
        hash = G_FowHashMix(hash, ent->class_id);
        hash = G_FowHashMix(hash, ent->targtype);
        hash = G_FowHashPointer(hash, ent->pathtex);
        if (ent->pathtex) {
            hash = G_FowHashMix(hash, ent->pathtex->width);
            hash = G_FowHashMix(hash, ent->pathtex->height);
        }
    }

    if (g_fow_blockers_valid &&
        g_fow_blocker_hash == hash &&
        g_fow_blocker_count == count)
    {
        return false;
    }

    g_fow_blocker_hash = hash;
    g_fow_blocker_count = count;
    g_fow_blockers_valid = true;
    return true;
}

static int G_FowBlockerDilation(LPCEDICT ent) {
    if (ent->targtype == TARG_TREE) {
        return FOW_TREE_DILATION_CELLS;
    }
    if (!(ent->svflags & SVF_MONSTER) &&
        ent->data.DestructableData->occluderHeight > 0.0f)
    {
        return FOW_TREE_DILATION_CELLS;
    }
    return 0;
}

static BOOL G_FowMarkBlockerPathTex(LPCEDICT ent, int dilation) {
    pathTex_t const *pathtex = ent->pathtex;
    FLOAT scale;
    BOOL marked = false;

    if (!pathtex || !pathtex->width || !pathtex->height) {
        return false;
    }

    scale = MAX(ent->s.scale, 0.01f);
    FOR_LOOP(py, pathtex->height) {
        FOR_LOOP(px, pathtex->width) {
            COLOR32 const *pixel = &pathtex->map[px + py * pathtex->width];
            FLOAT x;
            FLOAT y;
            DWORD cx;
            DWORD cy;

            if (!pixel->b) {
                continue;
            }

            x = ent->s.origin.x +
                ((FLOAT)px + 0.5f - (FLOAT)pathtex->width * 0.5f) *
                FOW_PATHING_PIXEL_SIZE * scale;
            y = ent->s.origin.y +
                ((FLOAT)py + 0.5f - (FLOAT)pathtex->height * 0.5f) *
                FOW_PATHING_PIXEL_SIZE * scale;
            cx = G_FowWorldToCellX(x);
            cy = G_FowWorldToCellY(y);
            if (cx == FOW_INVALID_CELL || cy == FOW_INVALID_CELL) {
                continue;
            }
            G_FowSetBlockedDilated(cx, cy, dilation);
            marked = true;
        }
    }
    return marked;
}

static void G_FowMarkBlocker(LPCEDICT ent) {
    DWORD cx;
    DWORD cy;
    FLOAT radius;
    int radius_cells;
    int dilation;

    if (!G_FowEntityIsBlocker(ent)) {
        return;
    }

    dilation = G_FowBlockerDilation(ent);
    if (G_FowMarkBlockerPathTex(ent, dilation)) {
        return;
    }

    cx = G_FowWorldToCellX(ent->s.origin.x);
    cy = G_FowWorldToCellY(ent->s.origin.y);
    if (cx == FOW_INVALID_CELL || cy == FOW_INVALID_CELL) {
        return;
    }

    G_FowSetBlockedDilated(cx, cy, dilation);
    radius = MAX(ent->s.radius, ent->collision);
    radius_cells = (int)floorf(radius / (FLOAT)FOW_CELL_SIZE);
    if (radius_cells <= 0) {
        return;
    }

    for (int dy = -radius_cells; dy <= radius_cells; dy++) {
        int y = (int)cy + dy;
        if (y < 0 || y >= (int)level.fow.height) {
            continue;
        }
        for (int dx = -radius_cells; dx <= radius_cells; dx++) {
            int x = (int)cx + dx;
            if (x < 0 || x >= (int)level.fow.width) {
                continue;
            }
            if (dx * dx + dy * dy <= radius_cells * radius_cells) {
                G_FowSetBlockedDilated((DWORD)x, (DWORD)y, dilation);
            }
        }
    }
}

static void G_FowRebuildBlockers(void) {
    if (!level.fow.blocked) {
        return;
    }
    memset(level.fow.blocked, 0, G_FowCellCount());
    level.fow.num_blocked = 0;
    FOR_LOOP(i, globals.num_edicts) {
        G_FowMarkBlocker(&g_edicts[i]);
    }
}

/* Reveal directly into connected viewer grids; source-owner grids are irrelevant when nobody consumes them. */
static void G_FowRevealForViewers(LPCEDICT ent, FLOAT radius, DWORD viewers) {
    FOR_LOOP(viewer, MAX_PLAYERS)
        if (viewers & (1u << viewer))
            G_FowRevealCircle(viewer, ent, radius);
}

/* --- Fog modifiers ------------------------------------------------------- *
 * Direct state writes and started modifiers use the same three-state cell
 * contract. Modifiers are applied after unit sight so their state persists. */
#define MAX_FOG_MODIFIERS 256 // handles; bounded active map-script fog modifiers

static LPFOGMODIFIER g_fog_modifiers[MAX_FOG_MODIFIERS];
static DWORD g_num_fog_modifiers;

static void G_FowApplyModifierForPlayer(DWORD player, LPCFOGMODIFIER mod);

/* Start is observable immediately in Warcraft scripts. This matters for the
 * common reveal pattern that starts and destroys/stops a VISIBLE modifier in
 * the same trigger turn: exploration must still be recorded even if the
 * modifier is gone before the next simulation fog update. */
static void G_FowApplyModifierImmediately(LPCFOGMODIFIER mod) {
    if (!mod || !G_FowReady() || !G_FowStateValid(mod->state) ||
        mod->player >= MAX_PLAYERS) {
        return;
    }
    FOR_LOOP(viewer, MAX_PLAYERS) {
        if (viewer != mod->player &&
            (!mod->use_shared_vision || !G_FowSharedVision(viewer, mod->player))) {
            continue;
        }
        G_FowApplyModifierForPlayer(viewer, mod);
    }
}

void G_FogModifierStart(LPFOGMODIFIER mod) {
    if (!mod) {
        return;
    }
    mod->started = true;
    FOR_LOOP(i, g_num_fog_modifiers) {
        if (g_fog_modifiers[i] == mod) {
            return;
        }
    }
    if (g_num_fog_modifiers < MAX_FOG_MODIFIERS) {
        g_fog_modifiers[g_num_fog_modifiers++] = mod;
        G_FowApplyModifierImmediately(mod);
    }
}

void G_FogModifierStop(LPFOGMODIFIER mod) {
    if (!mod) {
        return;
    }
    mod->started = false;
    FOR_LOOP(i, g_num_fog_modifiers) {
        if (g_fog_modifiers[i] == mod) {
            g_fog_modifiers[i] = g_fog_modifiers[--g_num_fog_modifiers];
            return;
        }
    }
}

/* Rectangular writes use the same cell-state contract as circular reveals. */
static void G_FowSetBoxState(fowPlayerGrid_t *grid, LPCBOX2 box, DWORD state) {
    DWORD x0 = G_FowWorldToCellX(box->min.x);
    DWORD y0 = G_FowWorldToCellY(box->min.y);
    DWORD x1 = G_FowWorldToCellX(box->max.x);
    DWORD y1 = G_FowWorldToCellY(box->max.y);

    if (x0 == FOW_INVALID_CELL || y0 == FOW_INVALID_CELL ||
        x1 == FOW_INVALID_CELL || y1 == FOW_INVALID_CELL) {
        return;
    }
#ifdef WC3_FOW_PACKED_MASK
    if (g_fow_fast && state == WC3_FOG_STATE_VISIBLE) {
        G_FowRevealPackedBox(grid, x0, y0, x1, y1);
        return;
    }
#endif
    for (DWORD y = y0; y <= y1; y++) {
        for (DWORD x = x0; x <= x1; x++) G_FowSetCellState(grid, G_FowCellIndex(x, y), state);
    }
}

/* Immediate JASS writes persist in the target grid even when no client currently consumes it. */
void G_FowSetStateRect(LPCFOGWRITE fog, LPCBOX2 box) {
    if (!fog || fog->player >= MAX_PLAYERS || !box ||
        !G_FowReady() || !G_FowStateValid(fog->state))
        return;
    FOR_LOOP(viewer, MAX_PLAYERS) {
        if (viewer != fog->player && (!fog->shared || !G_FowSharedVision(viewer, fog->player)))
            continue;
        G_FowSetBoxState(&level.fow.players[viewer], box, fog->state);
    }
}

/* Radius and location natives share one authoritative circular state path. */
void G_FowSetStateRadius(LPCFOGWRITE fog, LPCVECTOR2 center, FLOAT radius) {
    DWORD cx, cy;
    int cells;
    if (!fog || fog->player >= MAX_PLAYERS || !center ||
        !G_FowReady() || !G_FowStateValid(fog->state))
        return;
    cx = G_FowWorldToCellX(center->x);
    cy = G_FowWorldToCellY(center->y);
    if (cx == FOW_INVALID_CELL || cy == FOW_INVALID_CELL)
        return;
    cells = G_FowRadiusCells(radius);
    FOGDISK disk = { cx, cy, fog->state, cells };
    FOR_LOOP(viewer, MAX_PLAYERS) {
        if (viewer != fog->player && (!fog->shared || !G_FowSharedVision(viewer, fog->player)))
            continue;
        G_FowSetDiskState(&level.fow.players[viewer], &disk);
    }
}

static void G_FowApplyModifierForPlayer(DWORD player, LPCFOGMODIFIER mod) {
    fowPlayerGrid_t *grid = &level.fow.players[player];
    if (mod->is_rect) {
        G_FowSetBoxState(grid, &mod->rect, mod->state);
    } else {
        DWORD cx = G_FowWorldToCellX(mod->center.x);
        DWORD cy = G_FowWorldToCellY(mod->center.y);
        if (cx == FOW_INVALID_CELL || cy == FOW_INVALID_CELL) {
            return;
        }
#ifdef WC3_FOW_PACKED_MASK
        if (g_fow_fast && mod->state == WC3_FOG_STATE_VISIBLE)
            G_FowRevealPacked(grid, cx, cy, G_FowRadiusCells(mod->radius));
        else
#endif
            G_FowSetDiskState(grid, &(FOGDISK){ cx, cy, mod->state, G_FowRadiusCells(mod->radius) });
    }
}

static void G_FowApplyModifiers(DWORD viewers) {
    FOR_LOOP(i, g_num_fog_modifiers) {
        LPCFOGMODIFIER mod = g_fog_modifiers[i];
        if (!mod || !mod->started || !G_FowStateValid(mod->state) ||
            mod->player >= MAX_PLAYERS) {
            continue;
        }
        FOR_LOOP(viewer, MAX_PLAYERS)
            if ((viewers & (1u << viewer)) &&
                (viewer == mod->player || (mod->use_shared_vision && G_FowSharedVision(viewer, mod->player))))
                G_FowApplyModifierForPlayer(viewer, mod);
    }
}

void G_FowShutdown(void) {
    FOR_LOOP(player, MAX_PLAYERS) {
        fowPlayerGrid_t *grid = &level.fow.players[player];
        SAFE_DELETE(grid->visible, gi.MemFree);
        SAFE_DELETE(grid->explored, gi.MemFree);
        SAFE_DELETE(grid->visible_rows, gi.MemFree);
        SAFE_DELETE(grid->dirty_visible_rows, gi.MemFree);
        SAFE_DELETE(grid->dirty_explored_rows, gi.MemFree);
#ifdef WC3_FOW_PACKED_MASK
        SAFE_DELETE(grid->packed_visible, gi.MemFree);
        SAFE_DELETE(grid->packed_explored, gi.MemFree);
        grid->packed_stride = 0;
#endif
    }
    SAFE_DELETE(level.fow.blocked, gi.MemFree);
    SAFE_DELETE(level.fow.rim_cells, gi.MemFree);
    memset(&level.fow, 0, sizeof(level.fow));
    memset(g_fog_modifiers, 0, sizeof(g_fog_modifiers));
    g_num_fog_modifiers = 0;
    g_fow_blocker_hash = 0;
    g_fow_blocker_count = 0;
    g_fow_blockers_valid = false;
    g_fow_blockers_dirty = true;
}

void G_FowInit(void) {
    DWORD cells;

    G_FowShutdown();
    g_fow_blockers_valid = false;
    g_fow_blockers_dirty = true;
    level.fow.bounds = CM_GetWorldBounds();
    level.fow.width = (DWORD)ceilf((level.fow.bounds.max.x - level.fow.bounds.min.x) / (FLOAT)FOW_CELL_SIZE);
    level.fow.height = (DWORD)ceilf((level.fow.bounds.max.y - level.fow.bounds.min.y) / (FLOAT)FOW_CELL_SIZE);
    level.fow.width = MAX(level.fow.width, 1);
    level.fow.height = MAX(level.fow.height, 1);
    cells = G_FowCellCount();
    level.fow.blocked = gi.MemAlloc(cells);
    level.fow.rim_cells = gi.MemAlloc(cells * sizeof(*level.fow.rim_cells));
    ARRAY_COUNT(level.fow.rim_cells) = cells;
    if (!level.fow.blocked || !level.fow.rim_cells) {
        fprintf(stderr, "G_FowInit: failed to allocate %u-cell blocker grid and rim list\n", cells);
        G_FowShutdown();
        return;
    }
    memset(level.fow.blocked, 0, cells);

    FOR_LOOP(player, MAX_PLAYERS) {
        fowPlayerGrid_t *grid = &level.fow.players[player];
        grid->visible = gi.MemAlloc(cells);
        grid->explored = gi.MemAlloc(cells);
        grid->visible_rows = gi.MemAlloc(level.fow.height);
#ifdef WC3_FOW_PACKED_MASK
        grid->packed_stride = (level.fow.width + 15) >> 4;
        grid->packed_visible = gi.MemAlloc(grid->packed_stride * level.fow.height * sizeof(*grid->packed_visible));
        grid->packed_explored = gi.MemAlloc(grid->packed_stride * level.fow.height * sizeof(*grid->packed_explored));
#endif
        grid->dirty_visible_rows = gi.MemAlloc(level.fow.height);
        grid->dirty_explored_rows = gi.MemAlloc(level.fow.height);
        if (!grid->visible || !grid->explored || !grid->visible_rows ||
#ifdef WC3_FOW_PACKED_MASK
            !grid->packed_visible ||
            !grid->packed_explored ||
#endif
            !grid->dirty_visible_rows || !grid->dirty_explored_rows) {
            G_FowShutdown();
            return;
        }
        memset(grid->visible, 0, cells);
        memset(grid->explored, 0, cells);
#ifdef WC3_FOW_PACKED_MASK
        memset(grid->packed_visible, 0, grid->packed_stride * level.fow.height * sizeof(*grid->packed_visible));
        memset(grid->packed_explored, 0, grid->packed_stride * level.fow.height * sizeof(*grid->packed_explored));
#endif
        memset(grid->visible_rows, 0, level.fow.height);
        memset(grid->dirty_visible_rows, 1, level.fow.height);
        memset(grid->dirty_explored_rows, 1, level.fow.height);
    }
}

/* Mark a player grid as consumed before its first authoritative update. */
void G_FowConnectPlayer(DWORD player) {
    if (player < MAX_PLAYERS)
        level.fow.players[player].client_connected = true;
}

void G_FowUpdate(void) {
    DWORD owner_viewers[MAX_PLAYERS] = { 0 };
    DWORD viewers = 0;

    if (!G_FowReady()) {
        return;
    }

    FOR_LOOP(player, MAX_PLAYERS)
        if (level.fow.players[player].client_connected)
            viewers |= 1u << player;
    if (!viewers)
        return;
#ifdef WC3_FOW_PACKED_MASK
    g_fow_fast = atoi(gi.CvarString("wc3_fow_fast", "0"));
#endif
    FOR_LOOP(owner, MAX_PLAYERS)
        FOR_LOOP(viewer, MAX_PLAYERS)
            if ((viewers & (1u << viewer)) && G_FowSharedVision(viewer, owner))
                owner_viewers[owner] |= 1u << viewer;

    if (G_FowBlockersChanged()) {
        G_FowRebuildBlockers();
    }
    FOR_LOOP(player, MAX_PLAYERS) {
        fowPlayerGrid_t *grid = &level.fow.players[player];
        if (!(viewers & (1u << player))) {
            continue;
        }
        G_FowClearVisible(grid);
    }

    FOR_LOOP(i, globals.num_edicts) {
        LPCEDICT ent = &g_edicts[i];
        FLOAT radius;

        if (ent->s.player >= MAX_PLAYERS || !owner_viewers[ent->s.player] || !G_FowEntityIsRevealer(ent)) {
            continue;
        }
        radius = G_FowEntitySightRadius(ent);
        G_FowRevealForViewers(ent, radius, owner_viewers[ent->s.player]);
    }

    G_FowApplyModifiers(viewers);
}

/* FogEnable(false) reveals the whole map for this player, units included
   (matches WC3 cinematic behavior). RDF_NOFOG is the client-visual flag set by
   the FogEnable native; honor it for server-side unit visibility too, otherwise
   units in the (still-fogged) cinematic area are never networked and the scene
   renders without its actors. */
static BOOL G_FowPlayerFogDisabled(DWORD player) {
    LPGAMECLIENT client = G_GetPlayerClientByNumber(player);
    return client && (client->ps.rdflags & RDF_NOFOG);
}

/* Hover information is interactive gameplay state, so unlike explored
 * scenery it is exposed only while the entity is actively visible. */
BOOL G_FowPlayerCanHoverEntity(DWORD player, LPCEDICT ent) {
    DWORD x, y, index;
    fowPlayerGrid_t const *grid;

    if (!ent || player >= MAX_PLAYERS || !G_FowReady()) {
        return true;
    }
    if (ent->s.player < MAX_PLAYERS && G_FowSharedVision(player, ent->s.player)) {
        return true;
    }
    if (G_FowPlayerFogDisabled(player)) {
        return true;
    }
    x = G_FowWorldToCellX(ent->s.origin.x);
    y = G_FowWorldToCellY(ent->s.origin.y);
    if (x == FOW_INVALID_CELL || y == FOW_INVALID_CELL) {
        return false;
    }
    index = y * level.fow.width + x;
    grid = &level.fow.players[player];
#ifdef WC3_FOW_PACKED_MASK
    if (g_fow_fast)
        return G_FowPackedAt(grid->packed_visible, grid, x, y);
#endif
    return grid->visible && grid->visible[index] != 0;
}

BOOL G_FowPlayerCanSeeEntity(DWORD player, LPCEDICT ent) {
    DWORD x, y, index;
    fowPlayerGrid_t const *grid;

    if (!ent || player >= MAX_PLAYERS || !G_FowReady()) {
        return true;
    }
    if (ent->s.player < MAX_PLAYERS && G_FowSharedVision(player, ent->s.player)) {
        return true;
    }
    if (G_FowPlayerFogDisabled(player)) {
        return true;
    }
    x = G_FowWorldToCellX(ent->s.origin.x);
    y = G_FowWorldToCellY(ent->s.origin.y);
    if (x == FOW_INVALID_CELL || y == FOW_INVALID_CELL) {
        return false;
    }
    index = y * level.fow.width + x;
    grid = &level.fow.players[player];
    /* Explored scenery stays shrouded after sight leaves; sending unexplored map-wide doodads saturated snapshots. */
    if ((ent->svflags & SVF_STATIC_SCENERY) || (ent->runtime.flags & UNIT_BALANCE_BUILDING)) {
#ifdef WC3_FOW_PACKED_MASK
        if (g_fow_fast)
            return G_FowPackedAt(grid->packed_explored, grid, x, y);
#endif
        return grid->explored && grid->explored[index] != 0;
    }
#ifdef WC3_FOW_PACKED_MASK
    if (g_fow_fast)
        return G_FowPackedAt(grid->packed_visible, grid, x, y);
#endif
    return grid->visible && grid->visible[index] != 0;
}

static BYTE *G_FowPlaneForFlags(fowPlayerGrid_t *grid, DWORD flags, DWORD plane) {
    if (plane == FOW_MSG_VISIBLE_PLANE && (flags & FOW_MSG_VISIBLE_PLANE)) {
        return grid->visible;
    }
    if (plane == FOW_MSG_EXPLORED_PLANE && (flags & FOW_MSG_EXPLORED_PLANE)) {
        return grid->explored;
    }
    return NULL;
}

static DWORD G_FowPackRows(fowPlayerGrid_t *grid,
                           DWORD flags,
                           DWORD first_row,
                           DWORD row_count,
                           BYTE *payload,
                           DWORD payload_size)
{
    DWORD out = 0;
    DWORD planes[] = { FOW_MSG_VISIBLE_PLANE, FOW_MSG_EXPLORED_PLANE };
    BOOL started = false;
    BYTE current = 0;
    BYTE run = 0;

    if (!payload || payload_size < 2) {
        return 0;
    }

    FOR_LOOP(plane_index, sizeof(planes) / sizeof(planes[0])) {
        BYTE *plane = G_FowPlaneForFlags(grid, flags, planes[plane_index]);
        if (!plane) {
            continue;
        }
        FOR_LOOP(row, row_count) {
            DWORD y = first_row + row;
            FOR_LOOP(x, level.fow.width) {
                BYTE value;
#ifdef WC3_FOW_PACKED_MASK
                if (g_fow_fast && plane == grid->visible)
                    value = G_FowPackedAt(grid->packed_visible, grid, x, y);
                else if (g_fow_fast && plane == grid->explored)
                    value = G_FowPackedAt(grid->packed_explored, grid, x, y);
                else
#endif
                    value = plane[y * level.fow.width + x] ? 1 : 0;

                if (!started) {
                    payload[out++] = value;
                    current = value;
                    run = 1;
                    started = true;
                    continue;
                }

                if (value == current) {
                    if (run == 255) {
                        if (out >= payload_size) {
                            return 0;
                        }
                        payload[out++] = 255;
                        run = 0;
                    }
                    run++;
                    continue;
                }

                if (out >= payload_size) {
                    return 0;
                }
                payload[out++] = run;
                if (run == 255) {
                    if (out >= payload_size) {
                        return 0;
                    }
                    payload[out++] = 0;
                }
                current = value;
                run = 1;
            }
        }
    }

    if (!started || out >= payload_size) {
        return 0;
    }
    payload[out++] = run;
    return out;
}

static void G_FowWriteRows(LPEDICT ent, DWORD player, DWORD flags, DWORD first_row, DWORD row_count) {
    BYTE payload[FOW_CHUNK_TARGET_BYTES];
    DWORD plane_count = 0;
    DWORD payload_bytes;
    pfWriteData_t data;

    if (!ent || player >= MAX_PLAYERS || !G_FowReady() || row_count == 0 ||
        first_row >= level.fow.height) {
        return;
    }
    row_count = MIN(row_count, level.fow.height - first_row);
    if (flags & FOW_MSG_VISIBLE_PLANE) {
        plane_count++;
    }
    if (flags & FOW_MSG_EXPLORED_PLANE) {
        plane_count++;
    }
    if (plane_count == 0 || 1 + level.fow.width * row_count * plane_count > sizeof(payload)) {
        return;
    }

    payload_bytes = G_FowPackRows(&level.fow.players[player],
                                  flags,
                                  first_row,
                                  row_count,
                                  payload,
                                  sizeof(payload));
    if (!payload_bytes) {
        return;
    }

    gi.Write(PF_BYTE, &(LONG){ svc_fogofwar });
    gi.Write(PF_BYTE, &(LONG){ flags | FOW_MSG_RLE });
    gi.Write(PF_SHORT, &(LONG){ level.fow.width });
    gi.Write(PF_SHORT, &(LONG){ level.fow.height });
    gi.Write(PF_SHORT, &(LONG){ first_row });
    gi.Write(PF_SHORT, &(LONG){ row_count });
    gi.Write(PF_SHORT, &(LONG){ payload_bytes });
    data = (pfWriteData_t){ payload, payload_bytes };
    gi.Write(PF_DATA, &data);
    gi.unicast(ent);
}

static DWORD G_FowRowsPerChunk(DWORD flags) {
    DWORD plane_count = 0;

    if (flags & FOW_MSG_VISIBLE_PLANE) {
        plane_count++;
    }
    if (flags & FOW_MSG_EXPLORED_PLANE) {
        plane_count++;
    }
    if (!level.fow.width || plane_count == 0) {
        return 1;
    }
    return MAX(1, (FOW_CHUNK_TARGET_BYTES - 1) / (level.fow.width * plane_count));
}

void G_FowSendFull(LPEDICT ent) {
    DWORD player;
    DWORD rows_per_chunk;

    if (!ent || !ent->client || !G_FowReady()) {
        return;
    }
    player = ent->client->ps.number;
    if (player >= MAX_PLAYERS) {
        return;
    }
    G_FowConnectPlayer(player);
    rows_per_chunk = G_FowRowsPerChunk(FOW_MSG_VISIBLE_PLANE | FOW_MSG_EXPLORED_PLANE);
    for (DWORD row = 0; row < level.fow.height; row += rows_per_chunk) {
        G_FowWriteRows(ent,
                       player,
                       FOW_MSG_FULL | FOW_MSG_VISIBLE_PLANE | FOW_MSG_EXPLORED_PLANE,
                       row,
                       MIN(rows_per_chunk, level.fow.height - row));
    }
}

static void G_FowSendDirtyPlane(LPEDICT ent,
                                DWORD player,
                                BYTE *dirty_rows,
                                DWORD plane_flag)
{
    DWORD rows_per_chunk;
    DWORD row = 0;

    if (!dirty_rows) {
        return;
    }
    rows_per_chunk = G_FowRowsPerChunk(plane_flag);
    while (row < level.fow.height) {
        while (row < level.fow.height && !dirty_rows[row]) {
            row++;
        }
        if (row >= level.fow.height) {
            break;
        }
        DWORD first = row;
        DWORD count = 0;
        while (row < level.fow.height && dirty_rows[row] && count < rows_per_chunk) {
            dirty_rows[row] = 0;
            row++;
            count++;
        }
        G_FowWriteRows(ent, player, plane_flag, first, count);
    }
}

void G_FowSendDeltas(void) {
    if (!G_FowReady()) {
        return;
    }

    FOR_LOOP(player, MIN((DWORD)game.max_clients, (DWORD)MAX_PLAYERS)) {
        LPEDICT ent = G_GetPlayerEntityByNumber(player);
        if (!level.fow.players[player].client_connected || !ent || !ent->client) {
            continue;
        }
        G_FowSendDirtyPlane(ent,
                            player,
                            level.fow.players[player].dirty_visible_rows,
                            FOW_MSG_VISIBLE_PLANE);
        G_FowSendDirtyPlane(ent,
                            player,
                            level.fow.players[player].dirty_explored_rows,
                            FOW_MSG_EXPLORED_PLANE);
    }
}
