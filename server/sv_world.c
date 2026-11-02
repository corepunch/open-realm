#include "server.h"

#define AREA_DEPTH 6
#define AREA_NODES 128

#define STRUCT_FROM_LINK(l,t,m) ((t *)((uint8_t *)l - (long long)&(((t *)0)->m)))
#define EDICT_FROM_AREA(l) STRUCT_FROM_LINK(l,edict_t,area)
#define GET_AXIS(vec, axis) (*((float const *)(vec)+axis))
#define SET_AXIS(vec, axis, value) (*((float *)(vec)+axis))=value

KNOWN_AS(areanode_s, areaNode_t);

struct areanode_s {
    uint32_t axis;  // -1 = leaf node
    uint32_t depth; // for debug
    box2_t bounds;
    float dist;
    struct areanode_s *children[2];
//    link_t trigger_edicts;
    link_t solid_edicts;
};

static areaNode_t sv_areanodes[AREA_NODES];
static uint32_t sv_numareanodes;

void ClearLink (link_t *l) {
    l->prev = l->next = l;
}

void RemoveLink (link_t *l) {
    l->next->prev = l->prev;
    l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before) {
    l->next = before;
    l->prev = before->prev;
    l->prev->next = l;
    l->next->prev = l;
}

areaNode_t *SV_CreateAreaNode(uint32_t depth, vec2_t const *mins, vec2_t const *maxs) {
    areaNode_t *anode = &sv_areanodes[sv_numareanodes++];
    vec2_t size = Vector2_sub(maxs, mins);
    vec2_t mins1 = *mins, mins2 = *mins, maxs1 = *maxs, maxs2 = *maxs;

    ClearLink (&anode->solid_edicts);
 
    anode->bounds = MAKE(box2_t, *mins, *maxs);
    anode->depth = depth;

    if (depth == AREA_DEPTH) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = NULL;
        return anode;
    }
        
    anode->axis = size.x < size.y;
    anode->dist = 0.5 * (GET_AXIS(maxs, anode->axis) + GET_AXIS(mins, anode->axis));
    
    SET_AXIS(&maxs1, anode->axis, anode->dist);
    SET_AXIS(&mins2, anode->axis, anode->dist);

    anode->children[0] = SV_CreateAreaNode(depth+1, &mins2, &maxs2);
    anode->children[1] = SV_CreateAreaNode(depth+1, &mins1, &maxs1);
    
    return anode;
}

/* The Quake lists remain the encounter-order authority. A secondary hash grid
 * selects overlapping candidates; node preorder and link serial restore that
 * exact order before predicates run. One entry per edict makes link/unlink
 * constant-time on average, independent of radius or overlapping cell count. */
#define AREA_GRID_LEVELS 128
#define AREA_GRID_CELL 256.0

typedef struct {
    edict_t *ent;
    uint64_t serial;
    box2_t bounds;
    int32_t x, y;
    uint32_t next, previous, bucket;
    uint16_t node, level;
} areaGridEntry_t;
typedef struct {
    areaGridEntry_t *entries, **candidates;
    uint32_t *buckets, capacity, mask, exceptions;
    uint32_t levels[AREA_GRID_LEVELS], nodes[AREA_NODES];
    uint64_t serial, revision, active_levels[2];
    bool querying;
} areaGrid_t;
static areaGrid_t sv_area_grid;
#ifdef BZ_TESTS
static uint32_t area_grid_visits, area_list_visits, area_grid_queries;
#endif

void SV_ShutdownWorld(void) {
    free(sv_area_grid.entries); free(sv_area_grid.candidates); free(sv_area_grid.buckets);
    memset(&sv_area_grid, 0, sizeof(sv_area_grid));
}

static void SV_AreaGridClear(void) {
    SV_ShutdownWorld();
    if (!ge || ge->max_edicts <= 0) return;
    uint32_t capacity = ge->max_edicts, buckets = 1;
    if (capacity > UINT32_MAX / 4) { Com_Error(ERR_FATAL, "Area grid capacity overflow"); abort(); }
    while (buckets < capacity * 2) buckets <<= 1;
    sv_area_grid.entries = calloc(capacity, sizeof(*sv_area_grid.entries));
    sv_area_grid.candidates = malloc((size_t)capacity * 2 * sizeof(*sv_area_grid.candidates));
    sv_area_grid.buckets = calloc(buckets, sizeof(*sv_area_grid.buckets));
    if (!sv_area_grid.entries || !sv_area_grid.candidates || !sv_area_grid.buckets) {
        Com_Error(ERR_FATAL, "Cannot allocate server area grid"); abort();
    }
    sv_area_grid.capacity = capacity;
    sv_area_grid.mask = buckets - 1;
}

static uint32_t SV_AreaGridHash(int32_t x, int32_t y, uint32_t level) {
    uint32_t hash = (uint32_t)x * 0x9e3779b9u ^ (uint32_t)y * 0x85ebca6bu ^ level * 0xc2b2ae35u;
    hash ^= hash >> 16; hash *= 0x7feb352du; hash ^= hash >> 15;
    return hash & sv_area_grid.mask;
}

static areaGridEntry_t *SV_AreaGridEntry(edict_t *ent) {
    uintptr_t offset = (uintptr_t)ent - (uintptr_t)ge->edicts;
    if (ge->edict_size <= 0 || offset % ge->edict_size || offset / ge->edict_size >= sv_area_grid.capacity) {
        Com_Error(ERR_FATAL, "Area grid requires an edict from the game export"); abort();
    }
    return sv_area_grid.entries + offset / ge->edict_size;
}

static void SV_AreaGridUnlink(edict_t *ent) {
    if (!sv_area_grid.entries) return;
    areaGridEntry_t *entry = SV_AreaGridEntry(ent);
    if (!entry->ent) return;
    uint32_t *head = entry->level == AREA_GRID_LEVELS ? &sv_area_grid.exceptions : sv_area_grid.buckets + entry->bucket;
    if (entry->previous) sv_area_grid.entries[entry->previous - 1].next = entry->next;
    else *head = entry->next;
    if (entry->next) sv_area_grid.entries[entry->next - 1].previous = entry->previous;
    if (entry->level < AREA_GRID_LEVELS && !--sv_area_grid.levels[entry->level])
        sv_area_grid.active_levels[entry->level / 64] &= ~(UINT64_C(1) << (entry->level % 64));
    sv_area_grid.nodes[entry->node]--;
    entry->ent = NULL;
    sv_area_grid.revision++;
}

static void SV_AreaGridPublish(areaGridEntry_t *entry, edict_t *ent, uint16_t node) {
    /* Every public link advances encounter order, including unchanged bounds.
     * Queries whose predicates relink entities also depend on this revision. */
    if (sv_area_grid.serial == UINT64_MAX) { Com_Error(ERR_FATAL, "Area grid link serial exhausted"); abort(); }
    entry->ent = ent; entry->serial = ++sv_area_grid.serial; entry->node = node;
    sv_area_grid.revision++;
}

static void SV_AreaGridLink(edict_t *ent, areaNode_t const *node) {
    if (!sv_area_grid.entries) SV_AreaGridClear();
    areaGridEntry_t *entry = SV_AreaGridEntry(ent);
    uint32_t index = (uint32_t)(entry - sv_area_grid.entries) + 1, level = 0, bucket = 0;
    box2_t const *bounds = &ent->bounds;
    double x = ((double)bounds->min.x + bounds->max.x) * 0.5;
    double y = ((double)bounds->min.y + bounds->max.y) * 0.5;
    double span = MAX((double)bounds->max.x - bounds->min.x, (double)bounds->max.y - bounds->min.y);
    double size = AREA_GRID_CELL;
    int32_t cell_x = 0, cell_y = 0;
    bool valid = isfinite(x) && isfinite(y) && isfinite(span) &&
        bounds->min.x <= bounds->max.x && bounds->min.y <= bounds->max.y;
    if (valid) {
        while (span > size || fabs(x / size) > INT32_MAX - 2.0 || fabs(y / size) > INT32_MAX - 2.0) {
            size *= 2; level++;
        }
        assert(level < AREA_GRID_LEVELS);
        cell_x = (int32_t)floor(x / size); cell_y = (int32_t)floor(y / size);
        bucket = SV_AreaGridHash(cell_x, cell_y, level);
    } else level = AREA_GRID_LEVELS; /* Preserve the old comparisons for exceptional bounds. */
    bool same_cell = entry->ent && entry->level == level &&
        (!valid || (entry->x == cell_x && entry->y == cell_y));
    uint16_t node_index = (uint16_t)(node - sv_areanodes);
    if (!same_cell) {
        SV_AreaGridUnlink(ent);
        entry->level = level; entry->x = cell_x; entry->y = cell_y; entry->bucket = bucket;
        uint32_t *head = valid ? sv_area_grid.buckets + bucket : &sv_area_grid.exceptions;
        entry->previous = 0; entry->next = *head;
        if (*head) sv_area_grid.entries[*head - 1].previous = index;
        *head = index;
        if (valid && !sv_area_grid.levels[level]++)
            sv_area_grid.active_levels[level / 64] |= UINT64_C(1) << (level % 64);
        sv_area_grid.nodes[node_index]++;
    } else if (entry->node != node_index) {
        sv_area_grid.nodes[entry->node]--;
        sv_area_grid.nodes[node_index]++;
    }
    /* Bucket order is unobservable. Keep its membership when possible, but
     * always advance the canonical Quake-list order and mutation revision. */
    entry->bounds = *bounds;
    SV_AreaGridPublish(entry, ent, node_index);
}

void SV_ClearWorld(void) {
    SV_AreaGridClear();
    memset(sv_areanodes, 0, sizeof(sv_areanodes));
    sv_numareanodes = 0;
    box2_t bounds = ge->GetWorldBounds();
    SV_CreateAreaNode(0, &bounds.min, &bounds.max);
}

void SV_UnlinkEntity(edict_t *ent) {
    if (!ent->area.prev)
        return;        // not linked in anywhere
    SV_AreaGridUnlink(ent);
    RemoveLink(&ent->area);
    ent->area.prev = ent->area.next = NULL;
}

void SV_LinkEntity(edict_t *ent) {
    bool linked = ent->area.prev != NULL;
    if (linked) {
        RemoveLink(&ent->area);
        ent->area.prev = ent->area.next = NULL;
    }
    if (ent == ge->edicts || !ent->inuse) {
        if (linked) SV_AreaGridUnlink(ent);
        return;
    }

    vec2_t const size = { ent->collision, ent->collision };
    vec2_t const eps = { 1, 1 };
    
    ent->areanum = 0;
    ent->bounds.min = Vector2_sub(&ent->s.origin2, &size);
    ent->bounds.max = Vector2_add(&ent->s.origin2, &size);

    // because movement is clipped an epsilon away from an actual edge,
    // we must fully check even when bounding boxes don't quite touch
    ent->bounds.min = Vector2_sub(&ent->bounds.min, &eps);
    ent->bounds.max = Vector2_add(&ent->bounds.max, &eps);

    if (linked && sv_area_grid.entries) {
        areaGridEntry_t *entry = SV_AreaGridEntry(ent);
        box2_t const *old = &entry->bounds;
        if (entry->ent == ent && old->min.x == ent->bounds.min.x &&
            old->min.y == ent->bounds.min.y && old->max.x == ent->bounds.max.x &&
            old->max.y == ent->bounds.max.y) {
            areaNode_t *node = sv_areanodes + entry->node;
            InsertLinkBefore(&ent->area, &node->solid_edicts);
            ent->areabounds = node->bounds;
            SV_AreaGridPublish(entry, ent, entry->node);
            return;
        }
    }

    areaNode_t *node = sv_areanodes;
    while (1) {
        if (node->axis == -1)
            break;
        if (GET_AXIS(&ent->bounds.min, node->axis) > node->dist)
            node = node->children[0];
        else if (GET_AXIS(&ent->bounds.max, node->axis) < node->dist)
            node = node->children[1];
        else
            break; // crosses the node
    }
    InsertLinkBefore(&ent->area, &node->solid_edicts);
    ent->areabounds = node->bounds;
    SV_AreaGridLink(ent, node);
}

typedef struct {
    box2_t bounds;
    edict_t * *list;
    uint32_t maxcount;
    uint32_t count;
    bool (*pred)(edict_t const *);
} areaworker_t;

void SV_AreaEdicts_r(areaNode_t const *node, areaworker_t *worker) {
    link_t const *start = &node->solid_edicts;
    
    for (link_t const *l = start->next; l != start; l = l->next) {
        edict_t *check = EDICT_FROM_AREA(l);
#ifdef BZ_TESTS
        area_list_visits++;
#endif

        if (   check->bounds.min.x > worker->bounds.max.x
            || check->bounds.min.y > worker->bounds.max.y
            || check->bounds.max.x < worker->bounds.min.x
            || check->bounds.max.y < worker->bounds.min.y)
            continue; // not touching

        if (worker->count == worker->maxcount) {
            fprintf(stdout, "SV_AreaEdicts: MAXCOUNT\n");
            return;
        }

        if (!worker->pred || worker->pred(check)) {
            worker->list[worker->count++] = check;
        }
    }

    if (node->axis == -1)
        return; // terminal node

    // recurse down both sides
    if (GET_AXIS(&worker->bounds.max, node->axis) > node->dist)
        SV_AreaEdicts_r(node->children[0], worker);
    
    if (GET_AXIS(&worker->bounds.min, node->axis) < node->dist)
        SV_AreaEdicts_r(node->children[1], worker);
}

static bool SV_AreaTouches(box2_t const *a, box2_t const *b) {
    return !(a->min.x > b->max.x || a->min.y > b->max.y || a->max.x < b->min.x || a->max.y < b->min.y);
}

/* Nodes were allocated in this preorder. Strict split tests preserve the old
 * behavior for point queries exactly on an area boundary. */
static uint32_t SV_AreaGridNodes(areaNode_t const *node, box2_t const *bounds, bool *visited) {
    uint32_t index = (uint32_t)(node - sv_areanodes), count = sv_area_grid.nodes[index];
    visited[index] = true;
    if (node->axis != UINT32_MAX) {
        if (GET_AXIS(&bounds->max, node->axis) > node->dist) count += SV_AreaGridNodes(node->children[0], bounds, visited);
        if (GET_AXIS(&bounds->min, node->axis) < node->dist) count += SV_AreaGridNodes(node->children[1], bounds, visited);
    }
    return count;
}

/* Stable fixed-width radix ordering is linear in the candidate count. First
 * order by serial, then by node preorder. Subtracting the minimum avoids passes
 * over shared high serial bytes after a long-running game. */
static void SV_AreaGridSort(uint32_t count) {
    if (count < 2) return;
    areaGridEntry_t **in = sv_area_grid.candidates, **out = in + sv_area_grid.capacity;
    uint64_t minimum = in[0]->serial, maximum = minimum;
    bool one_node = true;
    for (uint32_t i = 1; i < count; i++) {
        minimum = MIN(minimum, in[i]->serial); maximum = MAX(maximum, in[i]->serial);
        one_node &= in[i]->node == in[0]->node;
    }
    uint64_t range = maximum - minimum;
    for (uint32_t shift = 0; shift < 64 && (range >> shift); shift += 8) {
        uint32_t offsets[256] = {0}, sum = 0;
        for (uint32_t i = 0; i < count; i++) offsets[((in[i]->serial - minimum) >> shift) & 255]++;
        for (uint32_t i = 0; i < 256; i++) { uint32_t n = offsets[i]; offsets[i] = sum; sum += n; }
        for (uint32_t i = 0; i < count; i++) out[offsets[((in[i]->serial - minimum) >> shift) & 255]++] = in[i];
        areaGridEntry_t **swap = in; in = out; out = swap;
    }
    if (!one_node) {
        uint32_t offsets[AREA_NODES] = {0}, sum = 0;
        for (uint32_t i = 0; i < count; i++) offsets[in[i]->node]++;
        for (uint32_t i = 0; i < AREA_NODES; i++) { uint32_t n = offsets[i]; offsets[i] = sum; sum += n; }
        for (uint32_t i = 0; i < count; i++) out[offsets[in[i]->node]++] = in[i];
        in = out;
    }
    if (in != sv_area_grid.candidates) memcpy(sv_area_grid.candidates, in, count * sizeof(*in));
}

/* A predicate may relink another object. Continue through the authoritative
 * lists after the already-observed object, without repeating predicates or
 * omitting objects that just entered the query. Nested queries use those same
 * lists and cannot overwrite this query's candidate storage. */
static void SV_AreaGridResume(areaworker_t *worker, bool const *visited, uint32_t first_node, link_t const *next) {
    for (uint32_t n = first_node; n < sv_numareanodes; n++) {
        if (!visited[n]) continue;
        link_t const *head = &sv_areanodes[n].solid_edicts;
        for (link_t const *link = n == first_node ? next : head->next; link != head; link = link->next) {
            edict_t *ent = EDICT_FROM_AREA(link);
            if (!SV_AreaTouches(&ent->bounds, &worker->bounds)) continue;
            if (worker->count == worker->maxcount) { fprintf(stdout, "SV_AreaEdicts: MAXCOUNT\n"); return; }
            if (!worker->pred || worker->pred(ent)) worker->list[worker->count++] = ent;
        }
    }
}

typedef struct { int32_t x0, y0, x1, y1; uint32_t level; } areaGridRange_t;
static bool SV_AreaGridQuery(areaworker_t *worker) {
    if (!sv_area_grid.entries || sv_area_grid.querying) return false;
    box2_t const *box = &worker->bounds;
    if (!isfinite(box->min.x) || !isfinite(box->min.y) || !isfinite(box->max.x) || !isfinite(box->max.y) ||
        box->min.x > box->max.x || box->min.y > box->max.y) return false;
    bool visited[AREA_NODES] = {0};
    uint32_t population = SV_AreaGridNodes(sv_areanodes, box, visited), range_count = 0;
    areaGridRange_t ranges[AREA_GRID_LEVELS];
    double cells = 0;
    for (uint32_t word = 0; word < 2; word++) for (uint64_t active = sv_area_grid.active_levels[word]; active; active &= active - 1) {
        uint32_t level = word * 64 + (uint32_t)__builtin_ctzll(active);
        double size = ldexp(AREA_GRID_CELL, level);
        double x0 = floor(((double)box->min.x - size * 0.5) / size);
        double y0 = floor(((double)box->min.y - size * 0.5) / size);
        double x1 = floor(((double)box->max.x + size * 0.5) / size);
        double y1 = floor(((double)box->max.y + size * 0.5) / size);
        if (x0 < INT32_MIN || y0 < INT32_MIN || x1 >= INT32_MAX || y1 >= INT32_MAX) return false;
        cells += (x1 - x0 + 1) * (y1 - y0 + 1);
        /* Whole-world and tiny-population queries are cheaper on the original
         * lists. This is a cost choice, not a different result limit. */
        if (cells >= population) return false;
        ranges[range_count++] = (areaGridRange_t){x0, y0, x1, y1, level};
    }
    uint32_t count = 0, visits = 0;
    for (uint32_t r = 0; r < range_count; r++) {
        areaGridRange_t const *range = ranges + r;
        for (int32_t y = range->y0; y <= range->y1; y++) for (int32_t x = range->x0; x <= range->x1; x++) {
            uint32_t bucket = SV_AreaGridHash(x, y, range->level);
            for (uint32_t i = sv_area_grid.buckets[bucket]; i; i = sv_area_grid.entries[i - 1].next) {
                areaGridEntry_t *entry = sv_area_grid.entries + i - 1;
#ifdef BZ_TESTS
                area_grid_visits++;
#endif
                /* Bound hashing/collision work by the alternative list scan. */
                if (++visits >= population) return false;
                if (entry->level == range->level && entry->x == x && entry->y == y && visited[entry->node] &&
                    SV_AreaTouches(&entry->ent->bounds, box)) sv_area_grid.candidates[count++] = entry;
            }
        }
    }
    for (uint32_t i = sv_area_grid.exceptions; i; i = sv_area_grid.entries[i - 1].next) {
        areaGridEntry_t *entry = sv_area_grid.entries + i - 1;
        if (visited[entry->node] && SV_AreaTouches(&entry->ent->bounds, box)) sv_area_grid.candidates[count++] = entry;
    }
    SV_AreaGridSort(count);
    sv_area_grid.querying = true;
#ifdef BZ_TESTS
    area_grid_queries++;
#endif
    for (uint32_t i = 0; i < count; i++) {
        areaGridEntry_t const *entry = sv_area_grid.candidates[i];
        edict_t *ent = entry->ent;
        uint32_t node = entry->node;
        uint64_t revision = sv_area_grid.revision;
        if (worker->count == worker->maxcount) { fprintf(stdout, "SV_AreaEdicts: MAXCOUNT\n"); break; }
        if (!worker->pred || worker->pred(ent)) worker->list[worker->count++] = ent;
        if (revision != sv_area_grid.revision) {
            SV_AreaGridResume(worker, visited, node, ent->area.next);
            break;
        }
    }
    sv_area_grid.querying = false;
    return true;
}

uint32_t SV_AreaEdicts(box2_t const *area, edict_t * *list, uint32_t maxcount, bool (*pred)(edict_t const *)) {
    areaworker_t w = {
        .bounds = *area,
        .list = list,
        .count = 0,
        .maxcount = maxcount,
        .pred = pred,
    };
    if (!SV_AreaGridQuery(&w)) SV_AreaEdicts_r(sv_areanodes, &w);
    return w.count;
}

#ifdef BZ_TESTS
#include "shared/test.h"
#define AREA_TEST_ENTITIES 4097
static box2_t area_test_bounds(void) { return (box2_t){{-32768,-32768},{32768,32768}}; }
static uint32_t area_test_trace[AREA_TEST_ENTITIES], area_test_trace_count;
static bool area_test_mutate, area_test_nested;

static bool area_test_filter(edict_t const *ent) {
    area_test_trace[area_test_trace_count++] = ent->s.number;
    if (area_test_nested) {
        area_test_nested = false;
        edict_t *nested[AREA_TEST_ENTITIES];
        box2_t box = {{-4096,-4096},{-3800,-3800}};
        uint32_t count = SV_AreaEdicts(&box, nested, AREA_TEST_ENTITIES, NULL);
        T_ASSERT(count > 0);
    }
    if (area_test_mutate) {
        area_test_mutate = false;
        edict_t *other = EDICT_NUM(AREA_TEST_ENTITIES - 1);
        other->s.origin2 = ent->s.origin2;
        SV_LinkEntity(other);
    }
    return ent->s.number % 3 != 0;
}

static void area_test_populate(bool mixed) {
    SV_ClearWorld();
    memset(ge->edicts, 0, (size_t)ge->edict_size * ge->max_edicts);
    for (int i = 1; i < ge->num_edicts; i++) {
        edict_t *ent = EDICT_NUM(i);
        ent->inuse = true; ent->s.number = i;
        ent->s.origin2 = (vec2_t){-8192 + ((i - 1) % 64) * 128, -8192 + ((i - 1) / 64) * 128};
        ent->collision = mixed && i % 97 == 0 ? 700 : 8;
        SV_LinkEntity(ent);
    }
}

TEST(server_area, hierarchical_grid_preserves_order_limits_relinks_and_nested_queries) {
    struct game_export *saved_export = ge, fixture = *ge;
    areaNode_t saved_nodes[AREA_NODES]; memcpy(saved_nodes, sv_areanodes, sizeof(saved_nodes));
    uint32_t saved_count = sv_numareanodes;
    areaGrid_t saved_grid = sv_area_grid;
    edict_t *edicts = calloc(AREA_TEST_ENTITIES, sizeof(*edicts));
    T_NOT_NULL(edicts); if (!edicts) return;
    fixture.edicts = edicts; fixture.edict_size = sizeof(*edicts);
    fixture.num_edicts = fixture.max_edicts = AREA_TEST_ENTITIES;
    fixture.GetWorldBounds = area_test_bounds;
    sv_area_grid = (areaGrid_t){0}; ge = &fixture;
    area_test_populate(true);
    edict_t *actual[AREA_TEST_ENTITIES], *expected[AREA_TEST_ENTITIES];
    uint32_t expected_trace[AREA_TEST_ENTITIES], seed = 9137;
    uint32_t queries = area_grid_queries;
    for (uint32_t iteration = 0; iteration < 180; iteration++) {
        seed = seed * 1664525u + 1013904223u;
        float x = -9000 + (int)(seed % 11000);
        seed = seed * 1664525u + 1013904223u;
        float y = -9000 + (int)(seed % 11000);
        float radius = iteration % 7 == 0 ? 2000 : 180;
        box2_t box = {{x-radius,y-radius},{x+radius,y+radius}};
        if (iteration < 3) box = (box2_t){{-8192 + 8192 * (int)iteration, -8192},{-8192 + 8192 * (int)iteration, 0}};
        if (iteration == 3) box = (box2_t){{-100000,-100000},{100000,100000}};
        area_test_trace_count = 0;
        areaworker_t oracle = {box, expected, AREA_TEST_ENTITIES, 0, area_test_filter};
        SV_AreaEdicts_r(sv_areanodes, &oracle);
        uint32_t trace_count = area_test_trace_count;
        memcpy(expected_trace, area_test_trace, trace_count * sizeof(*expected_trace));
        area_test_trace_count = 0;
        uint32_t count = SV_AreaEdicts(&box, actual, AREA_TEST_ENTITIES, area_test_filter);
        T_EQ(count, oracle.count); T_EQ(area_test_trace_count, trace_count);
        T_ASSERT(!memcmp(actual, expected, count * sizeof(*actual)));
        T_ASSERT(!memcmp(area_test_trace, expected_trace, trace_count * sizeof(*expected_trace)));
        edict_t *changed = edicts + 1 + seed % (AREA_TEST_ENTITIES - 1);
        SV_UnlinkEntity(changed);
        if (iteration & 1) {
            changed->s.origin2.x += 33.25f; changed->s.origin2.y -= 17.5f;
            SV_LinkEntity(changed);
        }
    }
    T_ASSERT(area_grid_queries > queries);
    /* Identical geometry still moves the object to the end of its Quake list.
     * Bounds are server-owned: a caller's stale bounds cannot poison reuse. */
    area_test_populate(false);
    box2_t all = {{-100000,-100000},{100000,100000}};
    for (unsigned i = 1; i <= 32; i++) {
        edict_t *ent = edicts + i;
        areaGridEntry_t before = *SV_AreaGridEntry(ent);
        uint64_t revision = sv_area_grid.revision, serial = sv_area_grid.serial;
        ent->bounds = (box2_t){{0,0},{0,0}};
        ent->areabounds = ent->bounds;
        SV_LinkEntity(ent);
        areaGridEntry_t const *after = SV_AreaGridEntry(ent);
        T_EQ(after->serial, serial + 1);
        T_EQ(sv_area_grid.revision, revision + 1);
        T_EQ(after->bucket, before.bucket); T_EQ(after->node, before.node);
        T_ASSERT(!memcmp(&ent->bounds, &before.bounds, sizeof(ent->bounds)));
        T_ASSERT(!memcmp(&ent->areabounds, &sv_areanodes[before.node].bounds, sizeof(ent->areabounds)));
        areaworker_t oracle = {all, expected, AREA_TEST_ENTITIES, 0, NULL};
        SV_AreaEdicts_r(sv_areanodes, &oracle);
        T_EQ(SV_AreaEdicts(&all, actual, AREA_TEST_ENTITIES, NULL), oracle.count);
        T_ASSERT(!memcmp(actual, expected, oracle.count * sizeof(*actual)));
    }
    /* Negative/out-of-world coordinates and exceptional float comparisons. */
    edicts[1].s.origin2 = (vec2_t){-70000, 40000}; SV_LinkEntity(edicts + 1);
    edicts[2].s.origin2 = (vec2_t){NAN, -4096}; SV_LinkEntity(edicts + 2);
    edicts[3].collision = 100000; SV_LinkEntity(edicts + 3);
    box2_t cases[] = {{{-70010,39990},{-69990,40010}}, {{-4100,-4100},{-3900,-3900}}, {{NAN,-4100},{NAN,-3900}}};
    FOR_LOOP(i, 3) {
        areaworker_t oracle = {cases[i], expected, AREA_TEST_ENTITIES, 0, NULL};
        SV_AreaEdicts_r(sv_areanodes, &oracle);
        uint32_t count = SV_AreaEdicts(cases + i, actual, AREA_TEST_ENTITIES, NULL);
        T_EQ(count, oracle.count); T_ASSERT(!memcmp(actual, expected, count * sizeof(*actual)));
    }
    /* Predicate rejection must happen before the output limit is applied. */
    area_test_populate(false);
    box2_t query = {{-4200,-4200},{-3700,-3700}};
    areaworker_t limited = {query, expected, 3, 0, area_test_filter};
    area_test_trace_count = 0; SV_AreaEdicts_r(sv_areanodes, &limited);
    uint32_t trace_count = area_test_trace_count;
    memcpy(expected_trace, area_test_trace, trace_count * sizeof(*expected_trace));
    area_test_trace_count = 0;
    T_EQ(SV_AreaEdicts(&query, actual, 3, area_test_filter), limited.count);
    T_ASSERT(!memcmp(actual, expected, limited.count * sizeof(*actual)));
    T_EQ(area_test_trace_count, trace_count);
    T_ASSERT(!memcmp(area_test_trace, expected_trace, trace_count * sizeof(*expected_trace)));
    /* Run identical nested/mutating predicates against fresh identical lists. */
    area_test_populate(false);
    area_test_trace_count = 0; area_test_nested = area_test_mutate = true;
    areaworker_t mutation = {query, expected, AREA_TEST_ENTITIES, 0, area_test_filter};
    SV_AreaEdicts_r(sv_areanodes, &mutation);
    trace_count = area_test_trace_count;
    memcpy(expected_trace, area_test_trace, trace_count * sizeof(*expected_trace));
    area_test_populate(false);
    area_test_trace_count = 0; area_test_nested = area_test_mutate = true;
    T_EQ(SV_AreaEdicts(&query, actual, AREA_TEST_ENTITIES, area_test_filter), mutation.count);
    T_ASSERT(!memcmp(actual, expected, mutation.count * sizeof(*actual)));
    T_EQ(area_test_trace_count, trace_count);
    T_ASSERT(!memcmp(area_test_trace, expected_trace, trace_count * sizeof(*expected_trace)));
    /* Selective queries must actually prune, not merely return the same list. */
    area_test_populate(false);
    query = (box2_t){{-4100,-4100},{-4090,-4090}};
    uint32_t list_before = area_list_visits, grid_before = area_grid_visits;
    areaworker_t selective = {query, expected, AREA_TEST_ENTITIES, 0, NULL};
    SV_AreaEdicts_r(sv_areanodes, &selective);
    uint32_t list_work = area_list_visits - list_before;
    queries = area_grid_queries;
    T_EQ(SV_AreaEdicts(&query, actual, AREA_TEST_ENTITIES, NULL), selective.count);
    T_EQ(area_grid_queries, queries + 1);
    T_ASSERT(area_grid_visits - grid_before < list_work / 8);
    T_ASSERT(!memcmp(actual, expected, selective.count * sizeof(*actual)));
    /* Exercise every serial byte; order cannot depend on serial fitting an
     * int or on the number of links since the map was loaded. */
    sv_area_grid.serial = UINT64_MAX - 100;
    SV_LinkEntity(edicts + 2081); SV_LinkEntity(edicts + 2082);
    query = (box2_t){{-4200,-4200},{-3700,-3700}};
    areaworker_t long_running = {query, expected, AREA_TEST_ENTITIES, 0, NULL};
    SV_AreaEdicts_r(sv_areanodes, &long_running);
    T_EQ(SV_AreaEdicts(&query, actual, AREA_TEST_ENTITIES, NULL), long_running.count);
    T_ASSERT(!memcmp(actual, expected, long_running.count * sizeof(*actual)));
    free(sv_area_grid.entries); free(sv_area_grid.candidates); free(sv_area_grid.buckets);
    sv_area_grid = saved_grid; ge = saved_export;
    memcpy(sv_areanodes, saved_nodes, sizeof(saved_nodes)); sv_numareanodes = saved_count;
    free(edicts);
}
#endif
