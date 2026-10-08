#include "g_local.h"

#define FOW_INVALID_CELL 0xffffffffu
#define FOW_PATHING_PIXEL_SIZE 32.0f
#define FOW_TREE_DILATION_CELLS 1
#define FOW_BLOCKER_LIGHT_MARGIN_CELLS 1
/* Exact row-word coverage masks describe only the current ordered unit-sight
 * prefix. They are not final visibility and never include future sources. */
static uint64_t *fow_cover_planes[MAX_PLAYERS], *fow_cover_current;
static fowPlayerGrid_t *fow_cover_grid;
static uint32_t fow_cover_stride;
static void G_FowCoverCells(fowPlayerGrid_t *, uint32_t, uint32_t, uint8_t);
#define G_FOW_CELL_INDEX(x, y) ((y) * level.fow.width + (x))
#define G_FOW_SET_VISIBLE_CELL(grid, x, y) do { \
    uint32_t fow_index_ = G_FOW_CELL_INDEX((uint32_t)(x), (uint32_t)(y)); \
    if (!(grid)->visible[fow_index_]) { \
        (grid)->visible[fow_index_] = 1; \
        G_FowCoverCells((grid), (uint32_t)(x), (uint32_t)(y), 1); \
        (grid)->visible_rows[(uint32_t)(y)] = 1; \
        if ((grid)->dirty_visible_rows) { \
            (grid)->dirty_visible_rows[(uint32_t)(y)] = 1; \
        } \
    } \
    if (!(grid)->explored[fow_index_]) { \
        (grid)->explored[fow_index_] = 1; \
        if ((grid)->dirty_explored_rows) { \
            (grid)->dirty_explored_rows[(uint32_t)(y)] = 1; \
        } \
    } \
} while (0)

static uint32_t g_fow_blocker_hash;
static uint32_t g_fow_blocker_count;
static bool g_fow_blockers_valid;
static bool g_fow_blockers_dirty = true;
typedef struct { uint32_t x, y, viewers; int radius; } fowSight_t;
typedef struct { uint8_t *visible, *rows; } fowSightPlane_t;
static fowSight_t fow_sight[MAX_ENTITIES];
static fowSightPlane_t fow_sight_planes[MAX_PLAYERS];
static uint32_t fow_sight_count, fow_sight_valid;
/* Checkpoints include completed rim commits, never a union of independently
 * evaluated rims. Bound storage across all possible viewers to 32 MiB; larger
 * maps use wider source blocks, or the exact full replay if one plane exceeds
 * the per-viewer budget. Slots follow edict order, including inactive slots. */
#define FOW_PREFIX_MIN_BLOCK 256u
#define FOW_PREFIX_SLOTS ((MAX_ENTITIES + FOW_PREFIX_MIN_BLOCK - 1) / FOW_PREFIX_MIN_BLOCK)
#define FOW_PREFIX_PLAYER_BYTES (2u * 1024u * 1024u)
typedef struct { uint8_t *data; uint32_t end; } fowPrefix_t;
static fowPrefix_t fow_prefix[MAX_PLAYERS][FOW_PREFIX_SLOTS];
static uint32_t fow_prefix_block;
typedef struct {
    uint32_t x, y, x0, y0, width, height;
    int radius;
    uint64_t *effect;
    size_t effect_bytes;
    uint32_t word_x, word_width;
    bool valid;
} fowCast_t;
static fowCast_t fow_casts[MAX_ENTITIES];
static uint32_t fow_cast_count;
static uint8_t *fow_cast_blocked;
static uint64_t *fow_blocked_words, *fow_blocked_columns;
static uint32_t *fow_cast_changes;
/* Record only the source's geometry writes. Rim writes depend on the live
 * union and must never become part of a source's independent mask. */
static void G_FowRecordCell(fowCast_t *cast, uint32_t x, uint32_t y) {
    assert(x >= cast->x0 && x < cast->x0 + cast->width);
    assert(y >= cast->y0 && y < cast->y0 + cast->height);
    uint32_t index = (y - cast->y0) * cast->word_width + (x >> 6) - cast->word_x;
    cast->effect[index] |= UINT64_C(1) << (x & 63);
}

/* Row slopes depend only on distance, not source, radius, octant or viewer.
 * Prepare once on the game thread. Existing rows remain immutable until map
 * shutdown; geometry evaluation reads them and owns only its output mask. */
typedef struct { float left, right; int distance_sq; } fowRay_t;
static fowRay_t **fow_ray_rows;
static int fow_ray_distance;
static void G_FowPrepareRays(int radius) {
    if (radius <= fow_ray_distance) return;
    fowRay_t **rows = realloc(fow_ray_rows, ((size_t)radius + 1) * sizeof(*rows));
    if (!rows) { gi.error("FOW: cannot allocate ray row index"); abort(); }
    fow_ray_rows = rows;
    for (int distance = fow_ray_distance + 1; distance <= radius; distance++) {
        fowRay_t *row = malloc(((size_t)distance + 1) * sizeof(*row));
        if (!row) { gi.error("FOW: cannot allocate ray row"); abort(); }
        for (int column = 0; column <= distance; column++) {
            int dx = column - distance, dy = -distance;
            row[column] = (fowRay_t){
                ((float)dx - 0.5f) / ((float)dy + 0.5f),
                ((float)dx + 0.5f) / ((float)dy - 0.5f), dx * dx + dy * dy
            };
        }
        rows[distance] = row;
    }
    fow_ray_distance = radius;
}

/* Both slopes decrease across a row. Search with the original float
 * inequalities, including equality; do not approximate the interval with a
 * different division or round-to-cell conversion. */
static int G_FowRayBegin(fowRay_t const *row, int count, float start) {
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (start < row[mid].right) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
static int G_FowRayEnd(fowRay_t const *row, int count, float end) {
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (end > row[mid].left) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

typedef struct {
    fowCast_t *cast;
    uint8_t const *blocked;
    uint32_t width, height;
    fowRay_t *const *rows;
    uint64_t const *blocked_rows, *blocked_columns;
    uint32_t row_stride, column_stride;
} fowGeometry_t;

#ifdef BZ_TESTS
static uint32_t fow_geometry_cells, fow_geometry_spans;
#endif

typedef struct { int first, end, fixed; bool horizontal; } fowRaySpan_t;
static fowRaySpan_t G_FowRaySpan(fowGeometry_t const *geometry, int distance, int first, int limit,
                                 int xx, int xy, int yx, int yy) {
    int x = (int)geometry->cast->x - distance * xy;
    int y = (int)geometry->cast->y - distance * yy;
    int a = first - distance, b = limit - 1 - distance;
    fowRaySpan_t span = {0};
    span.horizontal = xx != 0;
    if (xx) {
        span.fixed = y;
        if (y < 0 || y >= (int)geometry->height) return span;
        span.first = MAX(0, MIN(x + a * xx, x + b * xx));
        span.end = MIN((int)geometry->width, MAX(x + a * xx, x + b * xx) + 1);
    } else {
        span.fixed = x;
        if (x < 0 || x >= (int)geometry->width) return span;
        span.first = MAX(0, MIN(y + a * yx, y + b * yx));
        span.end = MIN((int)geometry->height, MAX(y + a * yx, y + b * yx) + 1);
    }
    return span;
}

static bool G_FowRaySpanBlocked(fowGeometry_t const *geometry, fowRaySpan_t span) {
    if (span.first >= span.end) return false;
    uint64_t const *line = span.horizontal ? geometry->blocked_rows + span.fixed * geometry->row_stride :
        geometry->blocked_columns + span.fixed * geometry->column_stride;
    for (int word = span.first >> 6; word <= ((span.end - 1) >> 6); word++) {
        int lo = MAX(span.first, word * 64) - word * 64;
        int hi = MIN(span.end, word * 64 + 64) - word * 64;
        uint64_t mask = (UINT64_MAX >> (64 - (hi - lo))) << lo;
        if (line[word] & mask) return true;
    }
    return false;
}

static void G_FowRecordRaySpan(fowCast_t *cast, fowRaySpan_t span) {
    if (span.first >= span.end) return;
    if (span.horizontal) {
        uint64_t *row = cast->effect + (span.fixed - cast->y0) * cast->word_width;
        for (int word = span.first >> 6; word <= ((span.end - 1) >> 6); word++) {
            int lo = MAX(span.first, word * 64) - word * 64;
            int hi = MIN(span.end, word * 64 + 64) - word * 64;
            row[word - cast->word_x] |= (UINT64_MAX >> (64 - (hi - lo))) << lo;
        }
    } else {
        uint64_t bit = UINT64_C(1) << (span.fixed & 63);
        uint64_t *out = cast->effect + (span.first - cast->y0) * cast->word_width + (span.fixed >> 6) - cast->word_x;
        for (int y = span.first; y < span.end; y++, out += cast->word_width) *out |= bit;
    }
}

static void G_FowCastGeometry(fowGeometry_t const *geometry, int first_row,
                              float start, float end, int xx, int xy, int yx, int yy) {
    fowCast_t *cast = geometry->cast;
    int radius_sq = cast->radius * cast->radius;
    if (start < end) return;
    for (int distance = first_row; distance <= cast->radius; distance++) {
        bool blocked = false;
        float next_start = start;
        fowRay_t const *row = geometry->rows[distance];
        int first = G_FowRayBegin(row, distance + 1, start);
        int limit = G_FowRayEnd(row, distance + 1, end);
        if (first >= limit) continue;
        if (!G_FowRaySpanBlocked(geometry, G_FowRaySpan(geometry, distance, first, limit, xx, xy, yx, yy))) {
            /* No blocker can change this row's wedge or create recursion.
             * Clip only the writes to the circle; blockers outside the circle
             * were included in the query, as in the scalar algorithm. */
            int lo = first, hi = limit;
            while (lo < hi) {
                int mid = lo + (hi - lo) / 2;
                if (row[mid].distance_sq > radius_sq) lo = mid + 1;
                else hi = mid;
            }
            if (lo < limit) G_FowRecordRaySpan(cast, G_FowRaySpan(geometry, distance, lo, limit, xx, xy, yx, yy));
#ifdef BZ_TESTS
            fow_geometry_spans++;
#endif
            continue;
        }
        for (int column = first; column < limit; column++) {
            fowRay_t const *ray = row + column;
#ifdef BZ_TESTS
            fow_geometry_cells++;
#endif
            /* Unblocking earlier in this row can narrow start further. */
            if (start < ray->right) continue;
            int dx = column - distance, dy = -distance;
            int x = (int)cast->x + dx * xx + dy * xy;
            int y = (int)cast->y + dx * yx + dy * yy;
            bool in_bounds = x >= 0 && y >= 0 && x < (int)geometry->width && y < (int)geometry->height;
            bool cell_blocked = in_bounds && geometry->blocked[y * geometry->width + x] != 0;
            if (in_bounds && ray->distance_sq <= radius_sq) G_FowRecordCell(cast, x, y);
            if (blocked) {
                if (cell_blocked) { next_start = ray->right; continue; }
                blocked = false;
                start = next_start;
            } else if (cell_blocked && distance < cast->radius) {
                blocked = true;
                G_FowCastGeometry(geometry, distance + 1, start, ray->left, xx, xy, yx, yy);
                next_start = ray->right;
            }
        }
        if (blocked) break;
    }
}

static void G_FowBuildShadowGeometry(fowCast_t *cast) {
    static int const octants[8][4] = {
        {1,0,0,1}, {0,1,1,0}, {0,-1,1,0}, {-1,0,0,1},
        {-1,0,0,-1}, {0,-1,-1,0}, {0,1,-1,0}, {1,0,0,-1}
    };
    G_FowPrepareRays(cast->radius);
    fowGeometry_t geometry = {
        cast, level.fow.blocked, level.fow.width, level.fow.height, fow_ray_rows,
        fow_blocked_words, fow_blocked_columns, fow_cover_stride, (level.fow.height + 63) >> 6
    };
    G_FowRecordCell(cast, cast->x, cast->y);
    FOR_LOOP(i, 8)
        G_FowCastGeometry(&geometry, 1, 1.0f, 0.0f, octants[i][0], octants[i][1], octants[i][2], octants[i][3]);
}

/* A blocker edit only affects sources whose read rectangles overlap changed
 * cells. A summed-area table makes each invalidation query four integer reads,
 * including removals and overlapping blockers whose union did not change. */
static bool G_FowInvalidateCasts(void) {
    uint32_t width = level.fow.width, height = level.fow.height, stride = width + 1;
    size_t cells = (size_t)width * height;
    if (!fow_cast_blocked) {
        fow_cast_blocked = malloc(cells);
        if (!fow_cast_blocked) { gi.error("FOW: cannot allocate blocker snapshot"); abort(); }
        FOR_LOOP(i, fow_cast_count) fow_casts[i].valid = false;
    } else {
        if (!fow_cast_changes) {
            fow_cast_changes = calloc((size_t)stride * (height + 1), sizeof(*fow_cast_changes));
            if (!fow_cast_changes) { gi.error("FOW: cannot allocate blocker change index"); abort(); }
        }
        for (uint32_t y = 0; y < height; y++) {
            uint32_t row = 0;
            for (uint32_t x = 0; x < width; x++) {
                uint32_t index = y * width + x;
                row += fow_cast_blocked[index] != level.fow.blocked[index];
                fow_cast_changes[(y + 1) * stride + x + 1] = row + fow_cast_changes[y * stride + x + 1];
            }
        }
        /* Damage, overlapping blockers and authored changes can alter the
         * blocker witness without changing a single occluded cell. Neither
         * source geometry nor ordered visibility depends on that witness. */
        if (!fow_cast_changes[height * stride + width]) return false;
        FOR_LOOP(i, fow_cast_count) {
            fowCast_t *cast = fow_casts + i;
            if (!cast->valid) continue;
            uint32_t x0 = cast->x0, x1 = x0 + cast->width, y0 = cast->y0, y1 = y0 + cast->height;
            uint32_t count = fow_cast_changes[y1 * stride + x1] + fow_cast_changes[y0 * stride + x0] -
                fow_cast_changes[y0 * stride + x1] - fow_cast_changes[y1 * stride + x0];
            if (count) cast->valid = false;
        }
    }
    memcpy(fow_cast_blocked, level.fow.blocked, cells);
    size_t bytes = (size_t)fow_cover_stride * height * sizeof(uint64_t);
    if (!fow_blocked_words) fow_blocked_words = malloc(bytes);
    if (!fow_blocked_words) { gi.error("FOW: cannot allocate blocker words"); abort(); }
    memset(fow_blocked_words, 0, bytes);
    uint32_t column_stride = (height + 63) >> 6;
    size_t column_bytes = (size_t)column_stride * width * sizeof(uint64_t);
    if (!fow_blocked_columns) fow_blocked_columns = malloc(column_bytes);
    if (!fow_blocked_columns) { gi.error("FOW: cannot allocate blocker columns"); abort(); }
    memset(fow_blocked_columns, 0, column_bytes);
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++)
            if (level.fow.blocked[y * width + x]) {
                fow_blocked_words[y * fow_cover_stride + (x >> 6)] |= UINT64_C(1) << (x & 63);
                fow_blocked_columns[x * column_stride + (y >> 6)] |= UINT64_C(1) << (y & 63);
            }
    return true;
}
#ifdef BZ_TESTS
static uint32_t fow_sight_builds;
static uint32_t fow_cast_builds;
static bool fow_force_casts;
static uint32_t fow_source_replays, fow_prefix_skips;
static uint32_t fow_cover_skips;
uint32_t G_TestFowCoverSkips(bool reset) {
    uint32_t count = fow_cover_skips;
    if (reset) fow_cover_skips = 0;
    return count;
}
uint32_t G_TestFowSourceReplays(bool reset) {
    uint32_t count = fow_source_replays;
    if (reset) fow_source_replays = 0;
    return count;
}
uint32_t G_TestFowPrefixSkips(bool reset) {
    uint32_t count = fow_prefix_skips;
    if (reset) fow_prefix_skips = 0;
    return count;
}
void G_TestFowForceCasts(bool force) { fow_force_casts = force; fow_sight_valid = 0; }
uint32_t G_TestFowCastBuilds(bool reset) {
    uint32_t count = fow_cast_builds;
    if (reset) fow_cast_builds = 0;
    return count;
}
uint32_t G_TestFowSightBuilds(bool reset) {
    uint32_t count=fow_sight_builds;if(reset)fow_sight_builds=0;return count;
}
void G_TestFowInvalidateSight(void) { fow_sight_valid=0; }
#endif
#ifdef WC3_FOW_PACKED_MASK
static bool g_fow_fast;
#endif

static uint32_t G_FowCellCount(void) {
    return level.fow.width * level.fow.height;
}

static void G_FowCoverCells(fowPlayerGrid_t *grid, uint32_t x, uint32_t y, uint8_t bits) {
    if (grid != fow_cover_grid) return;
    uint32_t word = y * fow_cover_stride + (x >> 6), shift = x & 63;
    fow_cover_current[word] |= (uint64_t)bits << shift;
    if (shift && ((uint64_t)bits >> (64 - shift)))
        fow_cover_current[word + 1] |= (uint64_t)bits >> (64 - shift);
}

static void G_FowSelectCover(uint32_t player) {
    if (!fow_cover_planes[player]) {
        size_t bytes = (size_t)fow_cover_stride * level.fow.height * sizeof(uint64_t);
        fow_cover_planes[player] = gi.MemAlloc(bytes);
        if (!fow_cover_planes[player]) { gi.error("FOW: cannot allocate ordered coverage words"); abort(); }
        memset(fow_cover_planes[player], 0, bytes);
    }
    fow_cover_grid = &level.fow.players[player];
    fow_cover_current = fow_cover_planes[player];
}

/* Cold sources can avoid geometry entirely when their conservative read/write
 * rectangle is already visible. Prepared sources use their exact masks below. */
static bool G_FowSourceCovered(fowSight_t const *sight) {
    int margin = sight->radius + FOW_BLOCKER_LIGHT_MARGIN_CELLS;
    uint32_t x0 = MAX(0, (int)sight->x - margin), y0 = MAX(0, (int)sight->y - margin);
    uint32_t x1 = MIN((int)level.fow.width, (int)sight->x + margin + 1);
    uint32_t y1 = MIN((int)level.fow.height, (int)sight->y + margin + 1);
    for (uint32_t y = y0; y < y1; y++) {
        for (uint32_t word = x0 >> 6; word <= ((x1 - 1) >> 6); word++) {
            uint64_t have = fow_cover_current[y * fow_cover_stride + word];
            if (have == UINT64_MAX) continue;
            uint32_t first = MAX(x0, word * 64) - word * 64, end = MIN(x1, word * 64 + 64) - word * 64;
            uint64_t mask = (UINT64_MAX >> (64 - (end - first))) << first;
            if ((have & mask) != mask) return false;
        }
    }
    return true;
}

static bool G_FowSourceIsRedundant(fowCast_t const *cast) {
    _Static_assert(FOW_BLOCKER_LIGHT_MARGIN_CELLS == 1, "Packed rim adjacency must match the scalar neighborhood");
    uint64_t const *rim = cast->effect + cast->word_width * cast->height;
    FOR_LOOP(y, cast->height) {
        uint32_t row = cast->y0 + y;
        uint64_t const *have = fow_cover_current + row * fow_cover_stride + cast->word_x;
        uint64_t const *need = cast->effect + y * cast->word_width;
        FOR_LOOP(x, cast->word_width) {
            if ((have[x] & need[x]) != need[x]) return false;
            uint64_t pending = rim[y * cast->word_width + x] & ~have[x];
            if (!pending) continue;
            uint64_t const *word = have + x;
            uint64_t adjacent = (*word << 1) | (*word >> 1);
            if (cast->word_x + x) adjacent |= word[-1] >> 63;
            if (cast->word_x + x + 1 < fow_cover_stride) adjacent |= word[1] << 63;
            if (row) adjacent |= word[-(ptrdiff_t)fow_cover_stride];
            if (row + 1 < level.fow.height) adjacent |= word[fow_cover_stride];
            if (pending & adjacent) return false;
        }
    }
    return true;
}

/* Ascending X visits may propagate to the right through candidates, but never
 * revisit a missed candidate to the left. Doubling grows only uninterrupted
 * candidate runs. Above is already processed; below and right are still the
 * incoming row state. This is the scalar scan, not an undirected flood fill. */
static uint64_t G_FowRimWord(uint64_t candidates, uint64_t visible, uint64_t above,
                            uint64_t below, bool left, bool right) {
    candidates &= ~visible;
    uint64_t reached = candidates & (above | below | (visible << 1) | (visible >> 1) |
        (uint64_t)left | ((uint64_t)right << 63));
    uint64_t chain = candidates;
    for (uint32_t shift = 1; shift < 64; shift <<= 1) {
        reached |= chain & (reached << shift);
        chain &= chain << shift;
    }
    return reached;
}

static void G_FowWriteNewCells(fowPlayerGrid_t *grid, uint32_t word, uint32_t y, uint64_t bits) {
    while (bits) {
        uint32_t x = word * 64 + (uint32_t)__builtin_ctzll(bits);
        bits &= bits - 1;
        G_FOW_SET_VISIBLE_CELL(grid, x, y);
    }
}

static void G_FowApplyBaseWords(fowCast_t const *cast, fowPlayerGrid_t *grid) {
    FOR_LOOP(y, cast->height) {
        uint64_t const *base = cast->effect + y * cast->word_width;
        uint64_t const *have = fow_cover_current + (cast->y0 + y) * fow_cover_stride + cast->word_x;
        FOR_LOOP(x, cast->word_width)
            G_FowWriteNewCells(grid, cast->word_x + x, cast->y0 + y, base[x] & ~have[x]);
    }
}

static void G_FowApplyRimWords(fowCast_t const *cast, fowPlayerGrid_t *grid) {
    uint64_t const *rim = cast->effect + cast->word_width * cast->height;
    FOR_LOOP(y, cast->height) {
        uint32_t row = cast->y0 + y;
        uint64_t const *have = fow_cover_current + row * fow_cover_stride + cast->word_x;
        FOR_LOOP(x, cast->word_width) {
            uint64_t candidates = rim[y * cast->word_width + x] & ~have[x];
            if (!candidates) continue;
            uint64_t const *word = have + x;
            uint64_t above = row ? word[-(ptrdiff_t)fow_cover_stride] : 0;
            uint64_t below = row + 1 < level.fow.height ? word[fow_cover_stride] : 0;
            bool left = cast->word_x + x && (word[-1] >> 63);
            bool right = cast->word_x + x + 1 < fow_cover_stride && (word[1] & 1);
            uint64_t reached = G_FowRimWord(candidates, *word, above, below, left, right);
            /* The old temporary value 2 is observed only as nonzero within
             * this source. Committing in ascending order yields identical
             * visibility, exploration and dirty rows at the source boundary. */
            G_FowWriteNewCells(grid, cast->word_x + x, row, reached);
        }
    }
}

static uint32_t G_FowCellIndex(uint32_t x, uint32_t y) {
    return G_FOW_CELL_INDEX(x, y);
}

static bool G_FowReady(void) {
    return level.fow.width > 0 && level.fow.height > 0;
}

bool G_FowPlayersShareVision(uint32_t viewer, uint32_t owner) {
    if (viewer >= MAX_PLAYERS || owner >= MAX_PLAYERS) {
        return false;
    }
    /* SetPlayerAlliance(source, other, shared vision) means source shares its
     * sight with other.  The viewer therefore reads the owner's outgoing
     * alliance bit. */
    return viewer == owner ||
           (level.alliances[owner][viewer] & (1 << ALLIANCE_SHARED_VISION)) ||
           (level.alliances[owner][viewer] & (1 << ALLIANCE_SHARED_VISION_FORCED));
}

bool G_UnitSharesVisionWith(edict_t const *unit, uint32_t viewer) {
    return unit && viewer < MAX_PLAYERS && (unit->shared_vision & (1u << viewer));
}

void G_SetUnitSharedVision(edict_t *unit, uint32_t viewer, bool share) {
    if (!unit || viewer >= MAX_PLAYERS) return;
    if (share) unit->shared_vision |= 1u << viewer;
    else unit->shared_vision &= ~(1u << viewer);
}

void G_AddUnitForcedVisibility(edict_t *unit, uint32_t viewer) {
    if (unit && viewer < MAX_PLAYERS && unit->forced_visibility_count[viewer] != 0xffffu)
        unit->forced_visibility_count[viewer]++;
}

void G_RemoveUnitForcedVisibility(edict_t *unit, uint32_t viewer) {
    if (unit && viewer < MAX_PLAYERS && unit->forced_visibility_count[viewer])
        unit->forced_visibility_count[viewer]--;
}

bool G_UnitIsForcedVisibleToPlayer(edict_t const *unit, uint32_t viewer) {
    if (!unit || viewer >= MAX_PLAYERS) return false;
    FOR_LOOP(owner, MAX_PLAYERS)
        if (unit->forced_visibility_count[owner] && G_FowPlayersShareVision(viewer, owner)) return true;
    return false;
}

uint32_t G_FowWorldToCellX(float x) {
    if (!G_FowReady()) {
        return FOW_INVALID_CELL;
    }
    int cell = (int)floorf((x - level.fow.bounds.min.x) / (float)FOW_CELL_SIZE);
    if (cell < 0) {
        return 0;
    }
    if ((uint32_t)cell >= level.fow.width) {
        return level.fow.width - 1;
    }
    return (uint32_t)cell;
}

uint32_t G_FowWorldToCellY(float y) {
    if (!G_FowReady()) {
        return FOW_INVALID_CELL;
    }
    int cell = (int)floorf((y - level.fow.bounds.min.y) / (float)FOW_CELL_SIZE);
    if (cell < 0) {
        return 0;
    }
    if ((uint32_t)cell >= level.fow.height) {
        return level.fow.height - 1;
    }
    return (uint32_t)cell;
}

static void G_FowSetVisible(fowPlayerGrid_t *grid, uint32_t x, uint32_t y) {
    if (!grid || !grid->visible || !grid->explored ||
        x >= level.fow.width || y >= level.fow.height) {
        return;
    }

    G_FOW_SET_VISIBLE_CELL(grid, x, y);
}

static bool G_FowStateValid(uint32_t state) { return state && state <= WC3_FOG_STATE_VISIBLE && !(state & (state - 1)); }

typedef struct {
    uint32_t x, y, state;
    int cells;
} fogDisk_t;



/* Scripted fog states own both planes, so the three JASS states need no parallel cinematic map. */
static void G_FowSetCellState(fowPlayerGrid_t *grid, uint32_t index, uint32_t state) {
    uint32_t y, x;
    if (!grid || !grid->visible || !grid->explored || index >= G_FowCellCount()) return;
    y = index / level.fow.width;
    x = index - y * level.fow.width;
#ifdef WC3_FOW_PACKED_MASK
    if (g_fow_fast) {
        uint16_t *visible = grid->packed_visible + (x >> 4) + y * grid->packed_stride;
        uint16_t *explored = grid->packed_explored + (x >> 4) + y * grid->packed_stride;
        uint16_t bit = (uint16_t)(1u << (x & 15));
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

static void G_FowSetBlocked(uint32_t x, uint32_t y) {
    uint32_t index;

    if (!level.fow.blocked || x >= level.fow.width || y >= level.fow.height) {
        return;
    }
    index = G_FowCellIndex(x, y);
    if (!level.fow.blocked[index]) {
        level.fow.blocked[index] = 1;
        level.fow.num_blocked++;
    }
}

static void G_FowSetBlockedDilated(uint32_t x, uint32_t y, int dilation) {
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
            G_FowSetBlocked((uint32_t)bx, (uint32_t)by);
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

static bool G_FowAnyBlockedInBox(int minx, int miny, int maxx, int maxy) {
    if (!level.fow.blocked || !level.fow.num_blocked) {
        return false;
    }

    minx = MAX(minx, 0);
    miny = MAX(miny, 0);
    maxx = MIN(maxx, (int)level.fow.width - 1);
    maxy = MIN(maxy, (int)level.fow.height - 1);
    for (int y = miny; y <= maxy; y++) {
        uint8_t const *row = level.fow.blocked + y * level.fow.width;
        for (int x = minx; x <= maxx; x++) {
            if (row[x]) {
                return true;
            }
        }
    }
    return false;
}

static int G_FowRadiusCells(float radius) {
    return MAX(1, (int)ceilf(radius / (float)FOW_CELL_SIZE));
}

/* Circular trigger/modifier state writes reuse the ordinary fog-grid rasterization. */
static void G_FowSetDiskState(fowPlayerGrid_t *grid, fogDisk_t const *disk) {
    int radius_sq = disk->cells * disk->cells;

    for (int dy = -disk->cells; dy <= disk->cells; dy++) {
        int y = (int)disk->y + dy;
        int max_dx;
        if (y < 0 || y >= (int)level.fow.height) {
            continue;
        }
        max_dx = (int)sqrtf((float)(radius_sq - dy * dy));
        for (int dx = -max_dx; dx <= max_dx; dx++) {
            int x = (int)disk->x + dx;
            if (x < 0 || x >= (int)level.fow.width) {
                continue;
            }
            G_FowSetCellState(grid, G_FowCellIndex((uint32_t)x, (uint32_t)y), disk->state);
        }
    }
}
static void G_FowRevealDisk(fowPlayerGrid_t *grid, uint32_t cx, uint32_t cy, int radius_cells) {
    fogDisk_t disk = { cx, cy, WC3_FOG_STATE_VISIBLE, radius_cells };
    G_FowSetDiskState(grid, &disk);
}

#ifdef WC3_FOW_PACKED_MASK
/* Apply retail's packed horizontal spans; the byte plane is materialized once after all revealers. */
static void G_FowRevealPacked(fowPlayerGrid_t *grid, uint32_t cx, uint32_t cy, int radius_cells) {
    static uint16_t const bit[16] = {
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
        max_dx = (int)sqrtf((float)(radius_sq - dy * dy));
        min_x = MAX(0, (int)cx - max_dx);
        max_x = MIN((int)level.fow.width - 1, (int)cx + max_dx);
        for (int x = min_x; x <= max_x;) {
            int word = x >> 4;
            int first = x & 15;
            int last = MIN(15, max_x - (word << 4));
            uint16_t mask = 0;

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

static void G_FowRevealPackedBox(fowPlayerGrid_t *grid, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    FOR_LOOP(y, y1 - y0 + 1) {
        uint32_t const row = y + y0;
        uint32_t x = x0;
        while (x <= x1) {
            uint32_t const word = x >> 4;
            uint32_t const first = x & 15;
            uint32_t const last = MIN(15, x1 - (word << 4));
            uint16_t mask = 0;
            for (uint32_t bit_index = first; bit_index <= last; bit_index++) mask |= (uint16_t)(1u << bit_index);
            grid->packed_visible[word + row * grid->packed_stride] |= mask;
            grid->packed_explored[word + row * grid->packed_stride] |= mask;
            grid->visible_rows[row] = grid->dirty_visible_rows[row] = 1;
            grid->dirty_explored_rows[row] = 1;
            x = (word + 1) << 4;
        }
    }
}

static bool G_FowPackedAt(uint16_t const *plane, fowPlayerGrid_t const *grid, uint32_t x, uint32_t y) {
    return plane[(x >> 4) + y * grid->packed_stride] & (1u << (x & 15));
}
#endif

static void G_FowCastLight(fowPlayerGrid_t *grid,
                           int cx,
                           int cy,
                           int row,
                           float start,
                           float end,
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
        bool blocked = false;
        float next_start = start;
        int delta_y = -distance;

        for (int delta_x = -distance; delta_x <= 0; delta_x++) {
            int x = cx + delta_x * xx + delta_y * xy;
            int y = cy + delta_x * yx + delta_y * yy;
            float left_slope = ((float)delta_x - 0.5f) / ((float)delta_y + 0.5f);
            float right_slope = ((float)delta_x + 0.5f) / ((float)delta_y - 0.5f);
            bool in_bounds;
            bool cell_blocked;

            if (start < right_slope) {
                continue;
            }
            if (end > left_slope) {
                break;
            }

            in_bounds = x >= 0 && y >= 0 &&
                        x < (int)level.fow.width && y < (int)level.fow.height;
            cell_blocked = in_bounds &&
                           level.fow.blocked[G_FOW_CELL_INDEX((uint32_t)x, (uint32_t)y)] != 0;
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

static void G_FowRevealShadowcast(fowPlayerGrid_t *grid, uint32_t cx, uint32_t cy, int radius_cells) {
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

static bool G_FowHasVisibleNeighbor(fowPlayerGrid_t *grid, int x, int y, int margin) {
    int margin_sq = margin * margin;

    for (int dy = -margin; dy <= margin; dy++) {
        int ny = y + dy;
        if (ny < 0 || ny >= (int)level.fow.height) {
            continue;
        }
        for (int dx = -margin; dx <= margin; dx++) {
            int nx = x + dx;
            uint32_t index;

            if (nx < 0 || nx >= (int)level.fow.width) {
                continue;
            }
            if (dx * dx + dy * dy > margin_sq) {
                continue;
            }
            index = G_FOW_CELL_INDEX((uint32_t)nx, (uint32_t)ny);
            if (grid->visible[index]) {
                return true;
            }
        }
    }
    return false;
}

/* Commit only marked blockers; the old second square walk revisited over 10K cells per Human02 update. */
static void G_FowCommitRimCells(fowPlayerGrid_t *grid, uint32_t count) {
    FOR_LOOP(i, count) {
        uint32_t const index = level.fow.rim_cells[i];
        uint32_t const y = index / level.fow.width;
        uint32_t const x = index - y * level.fow.width;

        grid->visible[index] = 0;
        G_FOW_SET_VISIBLE_CELL(grid, x, y);
    }
}

static void G_FowRevealBlockerRim(fowPlayerGrid_t *grid, uint32_t cx, uint32_t cy, int radius_cells) {
    int margin = FOW_BLOCKER_LIGHT_MARGIN_CELLS;
    int max_radius = radius_cells + margin;
    int max_radius_sq = max_radius * max_radius;
    uint32_t rim_count = 0;

    for (int dy = -max_radius; dy <= max_radius; dy++) {
        int y = (int)cy + dy;
        if (y < 0 || y >= (int)level.fow.height) {
            continue;
        }
        for (int dx = -max_radius; dx <= max_radius; dx++) {
            int x = (int)cx + dx;
            uint32_t index;

            if (x < 0 || x >= (int)level.fow.width) {
                continue;
            }
            if (dx * dx + dy * dy > max_radius_sq) {
                continue;
            }

            index = G_FOW_CELL_INDEX((uint32_t)x, (uint32_t)y);
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

static void G_FowRevealCircle(uint32_t player, uint32_t cx, uint32_t cy, int radius_cells) {
#ifdef BZ_TESTS
    fow_sight_builds++;
#endif
    fowPlayerGrid_t *grid;
    grid = &level.fow.players[player];
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
bool G_IsNight(void) {
    float const time = G_GetTimeOfDay();
    return !(time >= game.constants.dawnTimeGameHours &&
             time < game.constants.duskTimeGameHours);
}

static float G_FowEntitySightRadius(edict_t const *ent) {
    float day;
    float night;

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

static bool G_FowEntityIsRevealer(edict_t const *ent) {
    if (!ent || !ent->inuse || ent->s.player >= MAX_PLAYERS) {
        return false;
    }
    if (ent->svflags & SVF_NOCLIENT) {
        return false;
    }
    /* Gameplay-invisible units still provide sight to their owner. Only
     * non-invisibility RF_HIDDEN states suppress a unit's fog reveal. */
    if ((ent->s.renderfx & RF_HIDDEN) &&
        S_UnitIsHiddenFromPlayer(ent, ent->s.player)) {
        return false;
    }
    if (M_IsDead((edict_t *)ent)) {
        return false;
    }
    return G_FowEntitySightRadius(ent) > 0.0f;
}

static bool G_FowEntityIsBlocker(edict_t const *ent) {
    if (!ent || !ent->inuse || !(ent->s.flags & EF_FOW_BLOCKER)) {
        return false;
    }
    if (ent->s.renderfx & RF_HIDDEN) {
        return false;
    }
    if (M_IsDead((edict_t *)ent)) {
        return false;
    }
    return true;
}

static uint32_t G_FowHashMix(uint32_t hash, uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

static uint32_t G_FowHashFloat(uint32_t hash, float value) {
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return G_FowHashMix(hash, bits);
}

static uint32_t G_FowHashPointer(uint32_t hash, void const *ptr) {
    uintptr_t value = (uintptr_t)ptr;

    hash = G_FowHashMix(hash, (uint32_t)value);
    return G_FowHashMix(hash, (uint32_t)(value >> 16 >> 16));
}

/* Blocker owners call this after a lifecycle change so steady updates avoid hashing every edict. */
void G_FowMarkBlockersDirty(void) { g_fow_blockers_dirty = true; }

static bool G_FowBlockersChanged(void) {
    uint32_t hash = 2166136261u;
    uint32_t count = 0;

    if (!g_fow_blockers_dirty) return false;
    g_fow_blockers_dirty = false;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t const *ent = &g_edicts[i];

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

static int G_FowBlockerDilation(edict_t const *ent) {
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

static bool G_FowMarkBlockerPathTex(edict_t const *ent, int dilation) {
    pathTex_t const *pathtex = ent->pathtex;
    float scale;
    bool marked = false;

    if (!pathtex || !pathtex->width || !pathtex->height) {
        return false;
    }

    scale = MAX(ent->s.scale, 0.01f);
    FOR_LOOP(py, pathtex->height) {
        FOR_LOOP(px, pathtex->width) {
            color32_t const *pixel = &pathtex->map[px + py * pathtex->width];
            float x;
            float y;
            uint32_t cx;
            uint32_t cy;

            if (!pixel->b) {
                continue;
            }

            x = ent->s.origin.x +
                ((float)px + 0.5f - (float)pathtex->width * 0.5f) *
                FOW_PATHING_PIXEL_SIZE * scale;
            y = ent->s.origin.y +
                ((float)py + 0.5f - (float)pathtex->height * 0.5f) *
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

static void G_FowMarkBlocker(edict_t const *ent) {
    uint32_t cx;
    uint32_t cy;
    float radius;
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
    radius_cells = (int)floorf(radius / (float)FOW_CELL_SIZE);
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
                G_FowSetBlockedDilated((uint32_t)x, (uint32_t)y, dilation);
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

/* Produce a clipped disk directly in world-aligned words. Integer correction
 * preserves the old dx*dx + dy*dy predicate even if sqrtf rounds at a boundary.
 * Rim construction intersects the shared blocker index, not individual cells. */
static void G_FowBuildDiskWords(fowCast_t const *cast, int radius, uint64_t *out, bool blocked) {
    FOR_LOOP(y, cast->height) {
        uint32_t row = cast->y0 + y;
        int dy = (int)row - (int)cast->y, remaining = radius * radius - dy * dy;
        if (remaining < 0) continue;
        int extent = (int)sqrtf((float)remaining);
        /* Ordinary disks historically truncate sqrtf. Blocker rims used
         * integer squared distances; retain each operation's own boundary. */
        if (blocked) {
            while (extent * extent > remaining) extent--;
            while ((extent + 1) * (extent + 1) <= remaining) extent++;
        }
        uint32_t first = MAX(0, (int)cast->x - extent);
        uint32_t end = MIN((int)level.fow.width, (int)cast->x + extent + 1);
        for (uint32_t word = first >> 6; word <= ((end - 1) >> 6); word++) {
            uint32_t lo = MAX(first, word * 64) - word * 64;
            uint32_t hi = MIN(end, word * 64 + 64) - word * 64;
            uint64_t bits = (UINT64_MAX >> (64 - (hi - lo))) << lo;
            if (blocked) bits &= fow_blocked_words[row * fow_cover_stride + word];
            out[y * cast->word_width + word - cast->word_x] = bits;
        }
    }
}

/* The unit-cast pass owns a current blocker-word index. Other scripted fog
 * paths keep their byte-grid query because they may run before its rebuild. */
static bool G_FowCastHasBlockers(fowCast_t const *cast) {
    uint32_t x0 = MAX(0, (int)cast->x - cast->radius);
    uint32_t end = MIN((int)level.fow.width, (int)cast->x + cast->radius + 1);
    uint32_t y0 = MAX(0, (int)cast->y - cast->radius);
    uint32_t y1 = MIN((int)level.fow.height, (int)cast->y + cast->radius + 1);
    if (!level.fow.num_blocked) return false;
    for (uint32_t y = y0; y < y1; y++)
        for (uint32_t word = x0 >> 6; word <= ((end - 1) >> 6); word++) {
            uint32_t lo = MAX(x0, word * 64) - word * 64;
            uint32_t hi = MIN(end, word * 64 + 64) - word * 64;
            uint64_t mask = (UINT64_MAX >> (64 - (hi - lo))) << lo;
            if (fow_blocked_words[y * fow_cover_stride + word] & mask) return true;
        }
    return false;
}

/* Compile independent geometry without touching viewer state. Publish the
 * completed base once, then evaluate the rim against the ordered live prefix. */
static void G_FowBuildCast(fowCast_t *cast, fowSight_t const *sight, fowPlayerGrid_t *grid) {
    int r = sight->radius, margin = r + FOW_BLOCKER_LIGHT_MARGIN_CELLS;
    cast->x = sight->x; cast->y = sight->y; cast->radius = r;
    cast->x0 = MAX(0, (int)sight->x - margin); cast->y0 = MAX(0, (int)sight->y - margin);
    cast->width = MIN((int)level.fow.width - 1, (int)sight->x + margin) - cast->x0 + 1;
    cast->height = MIN((int)level.fow.height - 1, (int)sight->y + margin) - cast->y0 + 1;
    cast->word_x = cast->x0 >> 6;
    cast->word_width = ((cast->x0 + cast->width + 63) >> 6) - cast->word_x;
    size_t words = (size_t)cast->word_width * cast->height;
    size_t bytes = words * 2 * sizeof(uint64_t);
    if (bytes != cast->effect_bytes) {
        uint64_t *effect = realloc(cast->effect, bytes);
        if (!effect) { gi.error("FOW: cannot allocate source effect mask"); abort(); }
        cast->effect = effect; cast->effect_bytes = bytes;
    }
    memset(cast->effect, 0, bytes);
#ifdef BZ_TESTS
    fow_cast_builds++; fow_sight_builds++;
#endif
    bool shadow = G_FowCastHasBlockers(cast);
    if (shadow) {
        G_FowBuildShadowGeometry(cast);
        G_FowBuildDiskWords(cast, margin, cast->effect + words, true);
    } else G_FowBuildDiskWords(cast, r, cast->effect, false);
    cast->valid = true;
    G_FowApplyBaseWords(cast, grid);
}

/* Expand eight packed cells into byte lanes. Set only zero lanes, retaining
 * every existing nonzero value exactly as the scalar visibility writer does. */
static void G_FowApplyByte(fowPlayerGrid_t *grid, point2_t cell, uint8_t bits) {
    uint32_t index = G_FOW_CELL_INDEX(cell.x, cell.y), count = MIN(8, level.fow.width - cell.x);
    if (count < 8) bits &= (1u << count) - 1;
    uint64_t mask = bits, visible = 0, explored = 0;
    mask = (mask | (mask << 28)) & UINT64_C(0x0000000f0000000f);
    mask = (mask | (mask << 14)) & UINT64_C(0x0003000300030003);
    mask = (mask | (mask << 7)) & UINT64_C(0x0101010101010101);
    if (count == 8) {
        memcpy(&visible, grid->visible + index, 8); memcpy(&explored, grid->explored + index, 8);
    } else {
        memcpy(&visible, grid->visible + index, count); memcpy(&explored, grid->explored + index, count);
    }
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    visible = __builtin_bswap64(visible); explored = __builtin_bswap64(explored);
#endif
    uint64_t low = UINT64_C(0x7f7f7f7f7f7f7f7f);
    uint64_t next_visible = visible | (mask & (~(((visible & low) + low) | visible | low) >> 7));
    uint64_t next_explored = explored | (mask & (~(((explored & low) + low) | explored | low) >> 7));
    if (visible != next_visible) {
        G_FowCoverCells(grid, cell.x, cell.y, bits);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        next_visible = __builtin_bswap64(next_visible);
#endif
        if (count == 8) memcpy(grid->visible + index, &next_visible, 8);
        else memcpy(grid->visible + index, &next_visible, count);
        grid->visible_rows[cell.y] = 1;
        if (grid->dirty_visible_rows) grid->dirty_visible_rows[cell.y] = 1;
    }
    if (explored != next_explored) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        next_explored = __builtin_bswap64(next_explored);
#endif
        if (count == 8) memcpy(grid->explored + index, &next_explored, 8);
        else memcpy(grid->explored + index, &next_explored, count);
        if (grid->dirty_explored_rows) grid->dirty_explored_rows[cell.y] = 1;
    }
}

/* Shadowcast writes are an independent union. Rim propagation is replayed
 * against the current grid, preserving the scalar scan and source order. */
static void G_FowRevealCached(uint32_t index, fowSight_t const *sight, fowPlayerGrid_t *grid) {
    fowCast_t *cast = fow_casts + index;
    fow_cast_count = MAX(fow_cast_count, index + 1);
    if (!cast->valid || cast->x != sight->x || cast->y != sight->y || cast->radius != sight->radius)
        G_FowBuildCast(cast, sight, grid);
    else G_FowApplyBaseWords(cast, grid);
    G_FowApplyRimWords(cast, grid);
}

/* Reveal directly into connected viewer grids; source-owner grids are irrelevant when nobody consumes them. */
static void G_FowRevealForViewers(uint32_t index, fowSight_t const *sight, uint32_t viewers) {
    bool cached = true;
#ifdef BZ_TESTS
    cached = !fow_force_casts;
#endif
#ifdef WC3_FOW_PACKED_MASK
    cached = cached && !g_fow_fast;
#endif
    FOR_LOOP(viewer, MAX_PLAYERS)
        if (viewers & (1u << viewer)) {
            G_FowSelectCover(viewer);
            fowCast_t const *cast = fow_casts + index;
            bool prepared = cast->valid && cast->x == sight->x && cast->y == sight->y && cast->radius == sight->radius;
            if (cached && (prepared ? G_FowSourceIsRedundant(cast) : G_FowSourceCovered(sight))) {
#ifdef BZ_TESTS
                fow_cover_skips++;
#endif
                continue;
            }
#ifdef BZ_TESTS
            fow_source_replays++;
#endif
            if (cached) G_FowRevealCached(index, sight, &level.fow.players[viewer]);
            else G_FowRevealCircle(viewer, sight->x, sight->y, sight->radius);
        }
}

/* Applying a prefix must also reproduce exploration and dirty-row writes.
 * Scripts may have masked exploration since this checkpoint was recorded. */
static void G_FowApplySightPlane(fowPlayerGrid_t *grid, uint8_t const *visible, uint8_t const *rows) {
    FOR_LOOP(y, level.fow.height) if (rows[y]) {
        uint32_t x = 0;
        for (; x + 8 <= level.fow.width; x += 8) {
            uint64_t lanes;
            memcpy(&lanes, visible + y * level.fow.width + x, sizeof(lanes));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            lanes = __builtin_bswap64(lanes);
#endif
            /* Completed sight contains only 0/1 byte lanes. Gather their low
             * bits into the existing eight-cell visibility/exploration writer. */
            if (lanes) G_FowApplyByte(grid, (point2_t){x, y}, (uint8_t)((lanes * UINT64_C(0x0102040810204080)) >> 56));
        }
        for (; x < level.fow.width; x++)
            if (visible[y * level.fow.width + x]) G_FOW_SET_VISIBLE_CELL(grid, x, y);
    }
}

static void G_FowReplayPrefixes(uint32_t player, uint32_t const *dirty, bool valid) {
    G_FowSelectCover(player);
    fowPlayerGrid_t *grid = &level.fow.players[player];
    uint32_t bit = 1u << player, cells = G_FowCellCount(), block = 0;
    uint32_t blocks = (globals.num_edicts + fow_prefix_block - 1) / fow_prefix_block;
    bool equal = valid;
    while (block < blocks) {
        fowPrefix_t *prefix = &fow_prefix[player][block];
        uint32_t end = MIN((block + 1) * fow_prefix_block, globals.num_edicts);
        if (equal && !(dirty[block] & bit) && prefix->end == end) {
            /* Identical input and unchanged ordered sources imply identical
             * output. Jump to the last clean checkpoint before the next dirty
             * block, restoring only that prefix, never the final future union. */
            do {
#ifdef BZ_TESTS
                fow_prefix_skips++;
#endif
                prefix = &fow_prefix[player][block++];
                if (block == blocks || (dirty[block] & bit)) break;
                end = MIN((block + 1) * fow_prefix_block, globals.num_edicts);
            } while (fow_prefix[player][block].end == end);
            G_FowApplySightPlane(grid, prefix->data, prefix->data + cells);
            continue;
        }
        for (uint32_t i = block * fow_prefix_block; i < end; i++) {
            if (!(i & 15u)) gi.FrameCheckpoint();
            if (fow_sight[i].viewers & bit) G_FowRevealForViewers(i, fow_sight + i, bit);
        }
        /* Equality is useful only before a clean block. Fully dirty sequences
         * update checkpoints without spending bandwidth comparing each one. */
        equal = valid && block + 1 < blocks && !(dirty[block + 1] & bit) &&
            prefix->end == end && !memcmp(prefix->data, grid->visible, cells);
        if (!prefix->data) {
            prefix->data = gi.MemAlloc((size_t)cells + level.fow.height);
            if (!prefix->data) { gi.error("FOW: cannot allocate ordered sight checkpoint"); abort(); }
        }
        memcpy(prefix->data, grid->visible, cells);
        memcpy(prefix->data + cells, grid->visible_rows, level.fow.height);
        prefix->end = end;
        block++;
    }
}

/* --- Fog modifiers ------------------------------------------------------- *
 * Direct state writes and started modifiers use the same three-state cell
 * contract. Modifiers are applied after unit sight so their state persists. */
#define MAX_FOG_MODIFIERS 256 // handles; bounded active map-script fog modifiers
#define MAX_SAVE_FOG_HANDLES 65536u // corrupt-save allocation bound; runtime records grow independently

static fogModifier_t *g_fog_modifiers[MAX_FOG_MODIFIERS];
static uint32_t g_num_fog_modifiers;

/* The VM borrows these map-owned records. Clearing a script variable must not
 * release a started modifier, and load must restore it before handle binding. */
typedef struct {
    fogModifier_t state;
    uint32_t id;
    bool inuse;
} fogModifierRecord_t;

static fogModifierRecord_t **fog_records;
static uint32_t fog_record_count, fog_record_capacity;

static void G_ClearFogModifierRegistry(void) {
    FOR_LOOP(i, fog_record_count) gi.MemFree(fog_records[i]);
    SAFE_DELETE(fog_records, gi.MemFree);
    fog_record_count = fog_record_capacity = 0;
    memset(g_fog_modifiers, 0, sizeof(g_fog_modifiers));
    g_num_fog_modifiers = 0;
}

fogModifier_t *G_FogModifierCreate(void) {
    if (fog_record_count == UINT32_MAX) return NULL;
    if (fog_record_count == fog_record_capacity) {
        uint32_t capacity = fog_record_capacity ? fog_record_capacity * 2 : 32;
        if (capacity <= fog_record_capacity) return NULL;
        fogModifierRecord_t **records = gi.MemAlloc((size_t)capacity * sizeof(*records));
        if (!records) return NULL;
        if (fog_record_count) memcpy(records, fog_records, (size_t)fog_record_count * sizeof(*records));
        SAFE_DELETE(fog_records, gi.MemFree);
        fog_records = records; fog_record_capacity = capacity;
    }
    fogModifierRecord_t *record = gi.MemAlloc(sizeof(*record));
    if (!record) return NULL;
    memset(record, 0, sizeof(*record)); record->inuse = true;
    record->id = fog_record_count;
    fog_records[fog_record_count++] = record;
    return &record->state;
}

bool G_FogModifierId(fogModifier_t const *mod, uint32_t *id) {
    fogModifierRecord_t const *record = (fogModifierRecord_t const *)mod;
    if (!record || record->id >= fog_record_count || fog_records[record->id] != record || !record->inuse) return false;
    *id = record->id;
    return true;
}

fogModifier_t *G_FogModifierById(uint32_t id) {
    return id < fog_record_count && fog_records[id]->inuse ? &fog_records[id]->state : NULL;
}

void G_FogModifierDestroy(fogModifier_t *mod) {
    uint32_t id;
    if (!G_FogModifierId(mod, &id)) return;
    G_FogModifierStop(mod);
    fog_records[id]->inuse = false;
}

/* Application order is authoritative: overlapping writes need not commute.
 * Save the active IDs separately, including modifiers unreferenced by JASS. */
bool G_WriteFogModifiers(FILE *file) {
    if (fog_record_count > MAX_SAVE_FOG_HANDLES || fwrite(&fog_record_count, sizeof(fog_record_count), 1, file) != 1) return false;
    FOR_LOOP(i, fog_record_count) {
        uint32_t inuse = fog_records[i]->inuse;
        if (fwrite(&inuse, sizeof(inuse), 1, file) != 1 ||
            fwrite(&fog_records[i]->state, sizeof(fogModifier_t), 1, file) != 1) return false;
    }
    if (fwrite(&g_num_fog_modifiers, sizeof(g_num_fog_modifiers), 1, file) != 1) return false;
    FOR_LOOP(i, g_num_fog_modifiers) {
        uint32_t id;
        if (!G_FogModifierId(g_fog_modifiers[i], &id) ||
            fwrite(&id, sizeof(id), 1, file) != 1) return false;
    }
    return true;
}

bool G_ReadFogModifiers(FILE *file) {
    uint32_t count, active;
    if (fread(&count, sizeof(count), 1, file) != 1 || count > MAX_SAVE_FOG_HANDLES) return false;
    G_ClearFogModifierRegistry();
    FOR_LOOP(i, count) {
        uint32_t inuse;
        fogModifier_t *mod = G_FogModifierCreate();
        if (!mod || fread(&inuse, sizeof(inuse), 1, file) != 1 || inuse > 1 ||
            fread(mod, sizeof(*mod), 1, file) != 1) goto fail;
        fog_records[i]->inuse = inuse;
        /* Consumer policy validates player/state on application. Preserve
         * even an inert authored value; load only validates membership. */
        if (mod->started && !inuse) goto fail;
    }
    if (fread(&active, sizeof(active), 1, file) != 1 || active > MAX_FOG_MODIFIERS) goto fail;
    FOR_LOOP(i, active) {
        uint32_t id;
        if (fread(&id, sizeof(id), 1, file) != 1) goto fail;
        fogModifier_t *mod = G_FogModifierById(id);
        if (!mod || !mod->started) goto fail;
        FOR_LOOP(j, g_num_fog_modifiers) if (g_fog_modifiers[j] == mod) goto fail;
        g_fog_modifiers[g_num_fog_modifiers++] = mod;
    }
    FOR_LOOP(i, count) {
        fogModifier_t *mod = &fog_records[i]->state;
        if (!mod->started) continue;
        bool found = false;
        FOR_LOOP(j, g_num_fog_modifiers) if (g_fog_modifiers[j] == mod) found = true;
        if (!found) goto fail;
    }
    return true;
fail:
    G_ClearFogModifierRegistry();
    return false;
}

static void G_FowApplyModifierForPlayer(uint32_t player, fogModifier_t const *mod);

/* Start is observable immediately in Warcraft scripts. This matters for the
 * common reveal pattern that starts and destroys/stops a VISIBLE modifier in
 * the same trigger turn: exploration must still be recorded even if the
 * modifier is gone before the next simulation fog update. */
static void G_FowApplyModifierImmediately(fogModifier_t const *mod) {
    if (!mod || !G_FowReady() || !G_FowStateValid(mod->state) ||
        mod->player >= MAX_PLAYERS) {
        return;
    }
    FOR_LOOP(viewer, MAX_PLAYERS) {
        if (viewer != mod->player &&
            (!mod->use_shared_vision || !G_FowPlayersShareVision(viewer, mod->player))) {
            continue;
        }
        G_FowApplyModifierForPlayer(viewer, mod);
    }
}

void G_FogModifierStart(fogModifier_t *mod) {
    uint32_t id;
    if (!G_FogModifierId(mod, &id)) {
        return;
    }
    FOR_LOOP(i, g_num_fog_modifiers) {
        if (g_fog_modifiers[i] == mod) {
            return;
        }
    }
    if (g_num_fog_modifiers < MAX_FOG_MODIFIERS) {
        mod->started = true;
        g_fog_modifiers[g_num_fog_modifiers++] = mod;
        G_FowApplyModifierImmediately(mod);
    }
}

void G_FogModifierStop(fogModifier_t *mod) {
    uint32_t id;
    if (!G_FogModifierId(mod, &id)) {
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
static void G_FowSetBoxState(fowPlayerGrid_t *grid, box2_t const *box, uint32_t state) {
    uint32_t x0 = G_FowWorldToCellX(box->min.x);
    uint32_t y0 = G_FowWorldToCellY(box->min.y);
    uint32_t x1 = G_FowWorldToCellX(box->max.x);
    uint32_t y1 = G_FowWorldToCellY(box->max.y);

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
    for (uint32_t y = y0; y <= y1; y++) {
        for (uint32_t x = x0; x <= x1; x++) G_FowSetCellState(grid, G_FowCellIndex(x, y), state);
    }
}

/* Immediate JASS writes persist in the target grid even when no client currently consumes it. */
void G_FowSetStateRect(fogWrite_t const *fog, box2_t const *box) {
    if (!fog || fog->player >= MAX_PLAYERS || !box ||
        !G_FowReady() || !G_FowStateValid(fog->state))
        return;
    FOR_LOOP(viewer, MAX_PLAYERS) {
        if (viewer != fog->player && (!fog->shared || !G_FowPlayersShareVision(viewer, fog->player)))
            continue;
        G_FowSetBoxState(&level.fow.players[viewer], box, fog->state);
    }
}

/* Radius and location natives share one authoritative circular state path. */
void G_FowSetStateRadius(fogWrite_t const *fog, vec2_t const *center, float radius) {
    uint32_t cx, cy;
    int cells;
    if (!fog || fog->player >= MAX_PLAYERS || !center ||
        !G_FowReady() || !G_FowStateValid(fog->state))
        return;
    cx = G_FowWorldToCellX(center->x);
    cy = G_FowWorldToCellY(center->y);
    if (cx == FOW_INVALID_CELL || cy == FOW_INVALID_CELL)
        return;
    cells = G_FowRadiusCells(radius);
    fogDisk_t disk = { cx, cy, fog->state, cells };
    FOR_LOOP(viewer, MAX_PLAYERS) {
        if (viewer != fog->player && (!fog->shared || !G_FowPlayersShareVision(viewer, fog->player)))
            continue;
        G_FowSetDiskState(&level.fow.players[viewer], &disk);
    }
}

static void G_FowApplyModifierForPlayer(uint32_t player, fogModifier_t const *mod) {
    fowPlayerGrid_t *grid = &level.fow.players[player];
    if (mod->is_rect) {
        G_FowSetBoxState(grid, &mod->rect, mod->state);
    } else {
        uint32_t cx = G_FowWorldToCellX(mod->center.x);
        uint32_t cy = G_FowWorldToCellY(mod->center.y);
        if (cx == FOW_INVALID_CELL || cy == FOW_INVALID_CELL) {
            return;
        }
#ifdef WC3_FOW_PACKED_MASK
        if (g_fow_fast && mod->state == WC3_FOG_STATE_VISIBLE)
            G_FowRevealPacked(grid, cx, cy, G_FowRadiusCells(mod->radius));
        else
#endif
            G_FowSetDiskState(grid, &(fogDisk_t){ cx, cy, mod->state, G_FowRadiusCells(mod->radius) });
    }
}

static void G_FowApplyModifiers(uint32_t viewers) {
    FOR_LOOP(i, g_num_fog_modifiers) {
        fogModifier_t const *mod = g_fog_modifiers[i];
        if (!mod || !mod->started || !G_FowStateValid(mod->state) ||
            mod->player >= MAX_PLAYERS) {
            continue;
        }
        FOR_LOOP(viewer, MAX_PLAYERS)
            if ((viewers & (1u << viewer)) &&
                (viewer == mod->player || (mod->use_shared_vision && G_FowPlayersShareVision(viewer, mod->player))))
                G_FowApplyModifierForPlayer(viewer, mod);
    }
}

void G_FowShutdown(void) {
    FOR_LOOP(i, fow_cast_count) free(fow_casts[i].effect);
    memset(fow_casts, 0, sizeof(fow_casts));
    fow_cast_count = 0;
    for (int distance = 1; distance <= fow_ray_distance; distance++) free(fow_ray_rows[distance]);
    free(fow_ray_rows); fow_ray_rows = NULL; fow_ray_distance = 0;
    free(fow_cast_blocked); free(fow_cast_changes); free(fow_blocked_words); free(fow_blocked_columns);
    fow_cast_blocked = NULL; fow_cast_changes = NULL; fow_blocked_words = NULL; fow_blocked_columns = NULL;
#ifdef BZ_TESTS
    fow_force_casts = false;
#endif
    FOR_LOOP(player, MAX_PLAYERS) {
        SAFE_DELETE(fow_cover_planes[player], gi.MemFree);
        FOR_LOOP(i, FOW_PREFIX_SLOTS) {
            SAFE_DELETE(fow_prefix[player][i].data, gi.MemFree);
            fow_prefix[player][i].end = 0;
        }
        SAFE_DELETE(fow_sight_planes[player].visible, gi.MemFree);
        SAFE_DELETE(fow_sight_planes[player].rows, gi.MemFree);
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
    G_ClearFogModifierRegistry();
    g_fow_blocker_hash = 0;
    g_fow_blocker_count = 0;
    g_fow_blockers_valid = false;
    g_fow_blockers_dirty = true;
    memset(fow_sight,0,sizeof(fow_sight));
    fow_sight_count=fow_sight_valid=0;
    fow_prefix_block = 0;
    fow_cover_grid = NULL; fow_cover_current = NULL; fow_cover_stride = 0;
}

void G_FowInit(void) {
    uint32_t cells;

    G_FowShutdown();
    g_fow_blockers_valid = false;
    g_fow_blockers_dirty = true;
    level.fow.bounds = CM_GetWorldBounds();
    level.fow.width = (uint32_t)ceilf((level.fow.bounds.max.x - level.fow.bounds.min.x) / (float)FOW_CELL_SIZE);
    level.fow.height = (uint32_t)ceilf((level.fow.bounds.max.y - level.fow.bounds.min.y) / (float)FOW_CELL_SIZE);
    level.fow.width = MAX(level.fow.width, 1);
    level.fow.height = MAX(level.fow.height, 1);
    cells = G_FowCellCount();
    fow_cover_stride = (level.fow.width + 63) >> 6;
    size_t prefix_bytes = (size_t)cells + level.fow.height;
    if (prefix_bytes <= FOW_PREFIX_PLAYER_BYTES) {
        fow_prefix_block = FOW_PREFIX_MIN_BLOCK;
        while (((MAX_ENTITIES + fow_prefix_block - 1) / fow_prefix_block) * prefix_bytes > FOW_PREFIX_PLAYER_BYTES)
            fow_prefix_block *= 2;
    }
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
void G_FowConnectPlayer(uint32_t player) {
    if (player < MAX_PLAYERS)
        level.fow.players[player].client_connected = true;
}

void G_FowUpdate(void) {
    uint32_t owner_viewers[MAX_PLAYERS] = { 0 };
    uint32_t dirty[FOW_PREFIX_SLOTS] = { 0 };
    uint32_t viewers = 0;
    bool reuse_sight=true;

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
    reuse_sight=!g_fow_fast;
#endif
    FOR_LOOP(owner, MAX_PLAYERS)
        FOR_LOOP(viewer, MAX_PLAYERS)
            if ((viewers & (1u << viewer)) && G_FowPlayersShareVision(viewer, owner))
                owner_viewers[owner] |= 1u << viewer;

    if (G_FowBlockersChanged()) {
        G_FowRebuildBlockers();
        if (G_FowInvalidateCasts()) fow_sight_valid=0;
    }
    if(!reuse_sight)fow_sight_valid=0;
    uint32_t changed=viewers&~fow_sight_valid;
    /* Exact ordered inputs avoid hash collisions and observe every mutation
     * without adding invalidation calls to gameplay owners. */
    uint32_t count=MAX(fow_sight_count,globals.num_edicts);
    FOR_LOOP(i,count) {
        fowSight_t next={0},old=fow_sight[i];
        if(i<globals.num_edicts) {
            edict_t const *ent=g_edicts+i;
            if(ent->s.player<MAX_PLAYERS && G_FowEntityIsRevealer(ent)) {
                next.viewers=owner_viewers[ent->s.player]|(ent->shared_vision&viewers);
                if(next.viewers) {
                    next.x=G_FowWorldToCellX(ent->s.origin.x);
                    next.y=G_FowWorldToCellY(ent->s.origin.y);
                    if(next.x==FOW_INVALID_CELL || next.y==FOW_INVALID_CELL)next=(fowSight_t){0};
                    else next.radius=G_FowRadiusCells(G_FowEntitySightRadius(ent));
                }
            }
        }
        uint32_t affected = next.viewers ^ old.viewers;
        if(next.x!=old.x || next.y!=old.y || next.radius!=old.radius)
            affected |= next.viewers | old.viewers;
        changed |= affected;
        if (fow_prefix_block) dirty[i / fow_prefix_block] |= affected;
        /* Source insertion/removal and viewer relationship changes currently
         * invalidate whole affected sequences. Geometry-only edits retain the
         * earlier checkpoints and independently dirty later blocks. */
        if (next.viewers != old.viewers) fow_sight_valid &= ~(next.viewers | old.viewers);
        fow_sight[i]=next;
    }
    changed |= viewers & ~fow_sight_valid;
    changed &= viewers;
    fow_sight_count=globals.num_edicts;
    FOR_LOOP(player, MAX_PLAYERS) {
        fowPlayerGrid_t *grid = &level.fow.players[player];
        if (!(viewers & (1u << player))) {
            continue;
        }
        G_FowClearVisible(grid);
        if (fow_cover_planes[player])
            memset(fow_cover_planes[player], 0, (size_t)fow_cover_stride * level.fow.height * sizeof(uint64_t));
    }

    bool prefixes = reuse_sight && fow_prefix_block;
#ifdef BZ_TESTS
    prefixes = prefixes && !fow_force_casts;
#endif
    if (prefixes) {
        FOR_LOOP(player, MAX_PLAYERS)
            if (changed & (1u << player))
                G_FowReplayPrefixes(player, dirty, (fow_sight_valid & (1u << player)) != 0);
    } else FOR_LOOP(i,globals.num_edicts) {
        if (!(i & 15u)) gi.FrameCheckpoint();
        uint32_t unit_viewers=fow_sight[i].viewers&changed;
        if(unit_viewers)G_FowRevealForViewers(i,fow_sight+i,unit_viewers);
    }
    if(reuse_sight)FOR_LOOP(player,MAX_PLAYERS) {
        if(!(viewers&(1u<<player)))continue;
        fowPlayerGrid_t *grid=&level.fow.players[player];
        fowSightPlane_t *plane=fow_sight_planes+player;
        if(changed&(1u<<player)) {
            if(!plane->visible) {
                plane->visible=gi.MemAlloc(G_FowCellCount());
                plane->rows=gi.MemAlloc(level.fow.height);
                if(!plane->visible || !plane->rows) {
                    gi.error("G_FowUpdate: unit sight cache allocation failed");abort();
                }
            }
            memcpy(plane->visible,grid->visible,G_FowCellCount());
            memcpy(plane->rows,grid->visible_rows,level.fow.height);
            fow_sight_valid|=1u<<player;
        } else {
            /* Exploration remains mutable script state. Reapply cached sight
             * before timed reveals/modifiers just as a fresh geometry pass does. */
            G_FowApplySightPlane(grid, plane->visible, plane->rows);
        }
    }

    fow_cover_grid = NULL; fow_cover_current = NULL;

    /* Timed spell reveals are not ordinary sight sources: Far Sight ignores
     * terrain line-of-sight blockers and must survive this frame's visible-grid
     * rebuild. Its save-safe thinker owns only lifetime/state; apply the disk
     * here, after unit sight and before script fog modifiers. */
    FOR_LOOP(i, globals.num_edicts) {
        edict_t const *ent = &g_edicts[i];
        if (!ent->inuse || ent->think != far_sight_think || ent->s.player >= MAX_PLAYERS ||
            G_Time() >= ent->spawn_time || ent->collision <= 0.0f) {
            continue;
        }
        G_FowSetStateRadius(&(fogWrite_t){ ent->s.player, WC3_FOG_STATE_VISIBLE, true },
                            &ent->s.origin2, ent->collision);
    }

    G_FowApplyModifiers(viewers);
}

/* FogEnable(false) reveals the whole map for this player, ordinary units
   included (matches WC3 cinematic behavior). Gameplay invisibility remains
   detector-gated. RDF_NOFOG is the client-visual flag set by the FogEnable
   native; honor it for server-side unit visibility too, otherwise units in the
   (still-fogged) cinematic area are never networked and the scene renders
   without its actors. */
static bool G_FowPlayerFogDisabled(uint32_t player) {
    gameClient_t *client = G_GetPlayerClientByNumber(player);
    return client && (client->ps.rdflags & RDF_NOFOG);
}

/* Hover information is interactive gameplay state, so unlike explored
 * scenery it is exposed only while the entity is actively visible. */
bool G_FowPlayerCanHoverEntity(uint32_t player, edict_t const *ent) {
    uint32_t x, y, index;
    fowPlayerGrid_t const *grid;

    if (!ent || player >= MAX_PLAYERS || !G_FowReady()) {
        return true;
    }
    if (ent->s.player < MAX_PLAYERS && G_FowPlayersShareVision(player, ent->s.player)) {
        return true;
    }
    if (G_UnitIsForcedVisibleToPlayer(ent, player)) return true;
    if (S_UnitIsInvisibleToPlayer(ent, player)) {
        return false;
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

/* Native23a760 uses mode4/flags0: ownership/shared vision bypass detection,
 * but not the target vision cell. Rendering may still expose owned units. */
bool G_FowPlayerCanTrackUnit(uint32_t player, edict_t const *ent) {
    if (!ent || player>=MAX_PLAYERS) return false;
    if (!G_FowReady()) return true;
    if (G_UnitIsForcedVisibleToPlayer(ent,player)) return true;
    if (S_UnitIsInvisibleToPlayer(ent,player)) return false;
    if (G_FowPlayerFogDisabled(player)) return true;
    uint32_t x=G_FowWorldToCellX(ent->s.origin.x),y=G_FowWorldToCellY(ent->s.origin.y);
    if (x==FOW_INVALID_CELL || y==FOW_INVALID_CELL) return false;
    fowPlayerGrid_t const *grid=&level.fow.players[player];
#ifdef WC3_FOW_PACKED_MASK
    if (g_fow_fast) return G_FowPackedAt(grid->packed_visible,grid,x,y);
#endif
    return grid->visible && grid->visible[y*level.fow.width+x]!=0;
}

bool G_FowPlayerCanSeeEntity(uint32_t player, edict_t const *ent) {
    uint32_t x, y, index;
    fowPlayerGrid_t const *grid;

    if (!ent || player >= MAX_PLAYERS || !G_FowReady()) {
        return true;
    }
    if (ent->s.player < MAX_PLAYERS && G_FowPlayersShareVision(player, ent->s.player)) {
        return true;
    }
    if (G_UnitIsForcedVisibleToPlayer(ent, player)) return true;
    if (S_UnitIsInvisibleToPlayer(ent, player)) {
        return false;
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

static uint8_t *G_FowPlaneForFlags(fowPlayerGrid_t *grid, uint32_t flags, uint32_t plane) {
    if (plane == FOW_MSG_VISIBLE_PLANE && (flags & FOW_MSG_VISIBLE_PLANE)) {
        return grid->visible;
    }
    if (plane == FOW_MSG_EXPLORED_PLANE && (flags & FOW_MSG_EXPLORED_PLANE)) {
        return grid->explored;
    }
    return NULL;
}

typedef struct { fowPlayerGrid_t *grid; uint8_t *planes[2]; uint32_t plane_count, width, first_row, row_count, plane_bits, x, y, plane_index; uint8_t *plane; } fowPackCtx_t;

static uint8_t G_FowPackBit(uint32_t index, void *ctx) {
    fowPackCtx_t *c = ctx;
    uint8_t v;
    (void)index; /* MSG_EncodeRLE reads sequentially, so x/y/plane track the position with no division. */
#ifdef WC3_FOW_PACKED_MASK
    if (g_fow_fast && c->plane == c->grid->visible) v = G_FowPackedAt(c->grid->packed_visible, c->grid, c->x, c->y);
    else if (g_fow_fast && c->plane == c->grid->explored) v = G_FowPackedAt(c->grid->packed_explored, c->grid, c->x, c->y);
    else
#endif
        v = c->plane[c->y * level.fow.width + c->x] ? 1 : 0;
    if (++c->x == c->width) {
        c->x = 0;
        if (++c->y == c->first_row + c->row_count) {
            c->y = c->first_row;
            if (c->plane_index + 1 < c->plane_count) c->plane = c->planes[++c->plane_index];
        }
    }
    return v;
}

static uint32_t G_FowPackRows(fowPlayerGrid_t *grid,
                           uint32_t flags,
                           uint32_t first_row,
                           uint32_t row_count,
                           uint8_t *payload,
                           uint32_t payload_size)
{
    uint32_t planes[] = { FOW_MSG_VISIBLE_PLANE, FOW_MSG_EXPLORED_PLANE };
    fowPackCtx_t c;
    if (!payload || payload_size < 2) return 0;
    c.grid = grid; c.plane_count = 0; c.width = level.fow.width; c.first_row = first_row;
    c.row_count = row_count; c.plane_bits = level.fow.width * row_count;
    c.x = 0; c.y = first_row; c.plane_index = 0; c.plane = NULL;
    FOR_LOOP(plane_index, sizeof(planes) / sizeof(planes[0])) {
        uint8_t *plane = G_FowPlaneForFlags(grid, flags, planes[plane_index]);
        if (plane) c.planes[c.plane_count++] = plane;
    }
    if (!c.plane_count || !c.plane_bits) return 0;
    c.plane = c.planes[0];
    return MSG_EncodeRLE(payload, payload_size, c.plane_bits * c.plane_count, G_FowPackBit, &c);
}

static void G_FowWriteRows(edict_t *ent, uint32_t player, uint32_t flags, uint32_t first_row, uint32_t row_count) {
    uint8_t payload[FOW_CHUNK_TARGET_BYTES];
    uint32_t plane_count = 0;
    uint32_t payload_bytes;
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

    gi.Write(PF_BYTE, &(int32_t){ svc_fogofwar });
    gi.Write(PF_BYTE, &(int32_t){ flags | FOW_MSG_RLE });
    gi.Write(PF_SHORT, &(int32_t){ level.fow.width });
    gi.Write(PF_SHORT, &(int32_t){ level.fow.height });
    gi.Write(PF_SHORT, &(int32_t){ first_row });
    gi.Write(PF_SHORT, &(int32_t){ row_count });
    gi.Write(PF_SHORT, &(int32_t){ payload_bytes });
    data = (pfWriteData_t){ payload, payload_bytes };
    gi.Write(PF_DATA, &data);
    gi.unicast(ent);
}

static uint32_t G_FowRowsPerChunk(uint32_t flags) {
    uint32_t plane_count = 0;

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

void G_FowSendFull(edict_t *ent) {
    uint32_t player;
    uint32_t rows_per_chunk;

    if (!ent || !ent->client || !G_FowReady()) {
        return;
    }
    player = ent->client->ps.number;
    if (player >= MAX_PLAYERS) {
        return;
    }
    G_FowConnectPlayer(player);
    rows_per_chunk = G_FowRowsPerChunk(FOW_MSG_VISIBLE_PLANE | FOW_MSG_EXPLORED_PLANE);
    for (uint32_t row = 0; row < level.fow.height; row += rows_per_chunk) {
        G_FowWriteRows(ent,
                       player,
                       FOW_MSG_FULL | FOW_MSG_VISIBLE_PLANE | FOW_MSG_EXPLORED_PLANE,
                       row,
                       MIN(rows_per_chunk, level.fow.height - row));
    }
}

static void G_FowSendDirtyPlane(edict_t *ent,
                                uint32_t player,
                                uint8_t *dirty_rows,
                                uint32_t plane_flag)
{
    uint32_t rows_per_chunk;
    uint32_t row = 0;

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
        uint32_t first = row;
        uint32_t count = 0;
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

    FOR_LOOP(player, MIN((uint32_t)game.max_clients, (uint32_t)MAX_PLAYERS)) {
        edict_t *ent = G_GetPlayerEntityByNumber(player);
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

#ifdef BZ_TESTS
#include "shared/test.h"
TEST(wc3_game, fow_byte_mask_matches_scalar_writes_and_preserves_row_tails) {
    uint32_t width = level.fow.width;
    level.fow.width = 8;
    for (uint32_t n = 1; n <= 8; n++) FOR_LOOP(bits, 256) {
        uint8_t visible[16], explored[16], expected_v[16], expected_e[16];
        uint8_t row = 0, dirty_v = 0, dirty_e = 0, want_v = 0, want_e = 0;
        uint8_t const values[] = {0,1,2,3,128,0,1,255};
        FOR_LOOP(i, 16) { visible[i] = values[i % 8]; explored[i] = values[(i + 3) % 8]; }
        memcpy(expected_v, visible, sizeof(visible)); memcpy(expected_e, explored, sizeof(explored));
        FOR_LOOP(i, n) if (bits & (1u << i)) {
            if (!expected_v[8 - n + i]) { expected_v[8 - n + i] = 1; want_v = 1; }
            if (!expected_e[8 - n + i]) { expected_e[8 - n + i] = 1; want_e = 1; }
        }
        fowPlayerGrid_t grid = {.visible = visible, .explored = explored, .visible_rows = &row,
            .dirty_visible_rows = &dirty_v, .dirty_explored_rows = &dirty_e};
        G_FowApplyByte(&grid, (point2_t){8 - n,0}, bits);
        T_EQ(memcmp(visible, expected_v, sizeof(visible)), 0);
        T_EQ(memcmp(explored, expected_e, sizeof(explored)), 0);
        T_EQ(row, want_v); T_EQ(dirty_v, want_v); T_EQ(dirty_e, want_e);
    }
    level.fow.width = width;
}
#endif

#ifdef BZ_TESTS
#include "shared/test.h"
TEST(wc3_fow, packed_identity_predicate_matches_scalar_rim_neighbors_across_words) {
    uint64_t *saved_cover = fow_cover_current;
    uint32_t saved_stride = fow_cover_stride, saved_height = level.fow.height;
    uint64_t cover[9] = {0}, effect[2] = {0};
    fow_cover_current = cover; fow_cover_stride = 3; level.fow.height = 3;
    for (uint32_t corner = 0; corner < 3; corner++) {
        fowCast_t cast = {.effect = effect, .word_x = corner, .y0 = corner, .word_width = 1, .height = 1};
        FOR_LOOP(cell, 64) {
            int cx = corner * 64 + cell, cy = corner;
            effect[0] = 0; effect[1] = UINT64_C(1) << cell;
            FOR_LOOP(point, 192 * 3) {
                uint32_t x = point % 192, y = point / 192, tile = y * 3 + (x >> 6);
                cover[tile] = UINT64_C(1) << (x & 63);
                int dx = (int)x - cx, dy = (int)y - cy;
                /* Independent scalar condition: an unseen candidate writes
                 * iff a cardinal neighbor is visible. A visible candidate is
                 * already satisfied and does not propagate a new write. */
                bool redundant = dx * dx + dy * dy != 1;
                T_EQ(G_FowSourceIsRedundant(&cast), redundant);
                cover[tile] = 0;
            }
            effect[0] = UINT64_C(1) << cell; effect[1] = 0;
            T_ASSERT(!G_FowSourceIsRedundant(&cast));
            cover[corner * 3 + corner] = effect[0];
            T_ASSERT(G_FowSourceIsRedundant(&cast));
            cover[corner * 3 + corner] = 0;
        }
    }
    fow_cover_current = saved_cover; fow_cover_stride = saved_stride; level.fow.height = saved_height;
}
#endif

#ifdef BZ_TESTS
TEST(wc3_fow, ordered_rim_word_matches_exhaustive_and_full_width_scalar_scans) {
    FOR_LOOP(candidates, 256) FOR_LOOP(seeds, 256) {
        uint64_t expected = 0;
        FOR_LOOP(bit, 8) {
            uint64_t mask = UINT64_C(1) << bit;
            if ((candidates & mask) && ((seeds & mask) || (bit && (expected & (mask >> 1))))) expected |= mask;
        }
        T_EQ(G_FowRimWord(candidates, 0, seeds, 0, false, false), expected);
    }
    uint64_t state = UINT64_C(0x1398af759327ce51);
    FOR_LOOP(sample, 20000) {
        uint64_t words[4];
        FOR_LOOP(i, 4) { state = state * UINT64_C(6364136223846793005) + 1; words[i] = state; }
        uint64_t candidates = words[0], visible = words[1], above = words[2], below = words[3];
        bool left = sample & 1, right = sample & 2;
        uint64_t running = visible, expected = 0;
        FOR_LOOP(bit, 64) {
            uint64_t mask = UINT64_C(1) << bit;
            if (!(candidates & mask) || (running & mask)) continue;
            bool neighbor = ((above | below) & mask) ||
                (bit ? (running & (mask >> 1)) != 0 : left) ||
                (bit < 63 ? (running & (mask << 1)) != 0 : right);
            if (neighbor) { running |= mask; expected |= mask; }
        }
        T_EQ(G_FowRimWord(candidates, visible, above, below, left, right), expected);
    }
    T_EQ(G_FowRimWord(UINT64_MAX, 0, UINT64_C(1) << 63, 0, false, false), UINT64_C(1) << 63);
    T_EQ(G_FowRimWord(UINT64_MAX, 0, 0, 0, true, false), UINT64_MAX);
}
#endif

#ifdef BZ_TESTS
TEST(wc3_fow, disk_words_preserve_distinct_float_disk_and_integer_rim_boundaries) {
    uint32_t width = level.fow.width, height = level.fow.height, stride = fow_cover_stride;
    uint64_t *blocked = fow_blocked_words;
    fowCast_t cast = { .x = 4097, .y = 1, .width = 8195, .height = 3, .word_width = 129 };
    uint64_t out[129 * 3] = {0}, blockers[129 * 3];
    memset(blockers, 255, sizeof(blockers));
    level.fow.width = 8195; level.fow.height = 3; fow_cover_stride = 129;
    fow_blocked_words = blockers;
    G_FowBuildDiskWords(&cast, 4097, out, false);
    /* sqrtf rounds sqrt(4097^2 - 1) to 4097. The original disk includes
     * these edge cells, while the integer-distance rim excludes them. */
    T_ASSERT(out[0] & 1); T_ASSERT(out[128] & 4);
    T_ASSERT(out[129] & 1); T_ASSERT(out[257] & 4);
    memset(out, 0, sizeof(out));
    G_FowBuildDiskWords(&cast, 4097, out, true);
    T_ASSERT(!(out[0] & 1)); T_ASSERT(!(out[128] & 4));
    T_ASSERT(out[129] & 1); T_ASSERT(out[257] & 4);
    level.fow.width = width; level.fow.height = height; fow_cover_stride = stride;
    fow_blocked_words = blocked;
}
#endif

#ifdef BZ_TESTS
TEST(wc3_fow, ray_interval_search_preserves_float_boundary_comparisons) {
    G_FowPrepareRays(64);
    for (int distance = 1; distance <= 64; distance++) {
        fowRay_t const *row = fow_ray_rows[distance];
        for (int column = 0; column <= distance; column++) {
            for (int direction = -1; direction <= 1; direction++) {
                float start = row[column].right, end = row[column].left;
                if (direction) {
                    start = nextafterf(start, direction < 0 ? -INFINITY : INFINITY);
                    end = nextafterf(end, direction < 0 ? -INFINITY : INFINITY);
                }
                int first = 0, limit = 0;
                while (first <= distance && start < row[first].right) first++;
                while (limit <= distance && !(end > row[limit].left)) limit++;
                T_EQ(G_FowRayBegin(row, distance + 1, start), first);
                T_EQ(G_FowRayEnd(row, distance + 1, end), limit);
            }
        }
    }
}
#endif

#ifdef BZ_TESTS
TEST(wc3_fow, unobstructed_ray_rows_emit_spans_without_cell_visits) {
    G_FowPrepareRays(24);
    uint8_t blocked[65 * 65] = {0};
    uint64_t rows[65 * 2] = {0}, columns[65 * 2] = {0}, output[65 * 2] = {0};
    fowCast_t cast = { .x = 32, .y = 32, .width = 65, .height = 65, .radius = 24,
        .effect = output, .word_width = 2 };
    fowGeometry_t geometry = { &cast, blocked, 65, 65, fow_ray_rows, rows, columns, 2, 2 };
    static int const octants[8][4] = {
        {1,0,0,1}, {0,1,1,0}, {0,-1,1,0}, {-1,0,0,1},
        {-1,0,0,-1}, {0,-1,-1,0}, {0,1,-1,0}, {1,0,0,-1}
    };
    uint32_t cells = fow_geometry_cells, spans = fow_geometry_spans;
    G_FowRecordCell(&cast, 32, 32);
    FOR_LOOP(i, 8) G_FowCastGeometry(&geometry, 1, 1.0f, 0.0f,
        octants[i][0], octants[i][1], octants[i][2], octants[i][3]);
    for (int y = 0; y < 65; y++) for (int x = 0; x < 65; x++) {
        int dx = x - 32, dy = y - 32;
        bool actual = (output[y * 2 + (x >> 6)] >> (x & 63)) & 1;
        T_EQ(actual, dx * dx + dy * dy <= 24 * 24);
    }
    T_EQ(fow_geometry_cells, cells);
    T_EQ(fow_geometry_spans - spans, 8 * 24);
}

TEST(wc3_fow, modifier_registry_rejects_duplicate_missing_and_truncated_active_ids) {
    G_FowShutdown();
    fogModifier_t *mod=G_FogModifierCreate();T_NOT_NULL(mod);if(!mod)return;
    *mod=(fogModifier_t){.player=0,.state=WC3_FOG_STATE_FOGGED,.radius=64};
    G_FogModifierStart(mod);
    /* An unused handle can contain a state that the fog consumer ignores.
     * Registry restoration must preserve it rather than invent admission. */
    T_NOT_NULL(G_FogModifierCreate());
    uint32_t id;T_ASSERT(G_FogModifierId(mod,&id));T_EQ(id,0);
    FILE *valid=tmpfile();T_NOT_NULL(valid);if(!valid)return;
    T_ASSERT(G_WriteFogModifiers(valid));long size=ftell(valid);
    unsigned char *bytes=malloc(size);T_NOT_NULL(bytes);if(!bytes){fclose(valid);return;}
    rewind(valid);T_EQ(fread(bytes,1,size,valid),(size_t)size);fclose(valid);
    FOR_LOOP(case_id,4) {
        FILE *bad=tmpfile();T_NOT_NULL(bad);if(!bad)break;
        uint32_t active=case_id==0 ? 2 : case_id==1 ? 0 : 1;
        uint32_t invalid=UINT32_MAX;
        size_t prefix=(size_t)size-2*sizeof(uint32_t);
        T_EQ(fwrite(bytes,1,prefix,bad),prefix);
        T_EQ(fwrite(&active,sizeof(active),1,bad),1);
        if(case_id!=1 && case_id!=3)T_EQ(fwrite(case_id==2 ? &invalid : &id,sizeof(id),1,bad),1);
        if(case_id==0)T_EQ(fwrite(&id,sizeof(id),1,bad),1);
        rewind(bad);T_ASSERT(!G_ReadFogModifiers(bad));T_NULL(G_FogModifierById(id));fclose(bad);
    }
    valid=tmpfile();T_NOT_NULL(valid);
    if(valid){T_EQ(fwrite(bytes,1,size,valid),(size_t)size);rewind(valid);T_ASSERT(G_ReadFogModifiers(valid));
        T_NOT_NULL(G_FogModifierById(id));T_EQ(g_num_fog_modifiers,1);fclose(valid);}
    free(bytes);G_FowShutdown();
}
#endif
