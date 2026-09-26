/* Shared mutable collections; handles retain identity, copies own their storage. */
#define SC2_MAX_GROUPS 2048
#define SC2_MAX_LOOPS 128

typedef struct { int32_t *items; int count, capacity; } sc2GGroup_t;
static sc2GGroup_t sc2_unit_groups[SC2_MAX_GROUPS], sc2_player_groups[SC2_MAX_GROUPS];
static int32_t sc2_unit_group_n = 1, sc2_player_group_n = 1;
typedef struct { jass_t *owner; int kind, index; sc2GGroup_t snapshot; } sc2GLoop_t;
static sc2GLoop_t sc2_loops[SC2_MAX_LOOPS];
static int sc2_loop_n;

static void sc2_group_append(jass_t *j, sc2GGroup_t *g, int32_t value) {
    for (int i = 0; i < g->count; i++) if (g->items[i] == value) return;
    if (g->count == g->capacity) {
        int capacity = g->capacity ? g->capacity * 2 : 8;
        int32_t *items = realloc(g->items, capacity * sizeof(*items));
        if (!items) { jass_rterror(j, "Galaxy collection allocation failed"); return; }
        g->items = items; g->capacity = capacity;
    }
    g->items[g->count++] = value;
}
static void sc2_group_remove(sc2GGroup_t *g, int32_t value) {
    for (int i = 0; i < g->count; i++) if (g->items[i] == value) {
        memmove(g->items + i, g->items + i + 1, (--g->count - i) * sizeof(*g->items)); return;
    }
}
static int32_t sc2_group_new(jass_t *j, sc2GGroup_t *groups, int32_t *count, sc2GGroup_t const *source) {
    if (*count >= SC2_MAX_GROUPS) { jass_rterror(j, "Galaxy group table full"); return 0; }
    int32_t h = (*count)++;
    if (source) for (int i = 0; i < source->count; i++) sc2_group_append(j, &groups[h], source->items[i]);
    return h;
}
static sc2GLoop_t *sc2_loop_current(jass_t *j, int kind) {
    for (int i = sc2_loop_n - 1; i >= 0; i--)
        if (sc2_loops[i].owner == j && sc2_loops[i].kind == kind) return &sc2_loops[i];
    return NULL;
}
static void sc2_loop_begin(jass_t *j, int kind, sc2GGroup_t const *group) {
    if (sc2_loop_n == SC2_MAX_LOOPS) { jass_rterror(j, "Galaxy loop stack full"); return; }
    sc2GLoop_t *loop = &sc2_loops[sc2_loop_n++];
    *loop = (sc2GLoop_t){ .owner = j, .kind = kind };
    if (group) for (int i = 0; i < group->count; i++) sc2_group_append(j, &loop->snapshot, group->items[i]);
}
static void sc2_loop_end(jass_t *j, int kind) {
    sc2GLoop_t *loop = sc2_loop_current(j, kind);
    if (!loop) return;
    int index = (int)(loop - sc2_loops);
    free(loop->snapshot.items);
    memmove(loop, loop + 1, (--sc2_loop_n - index) * sizeof(*loop));
}
static void sc2_collections_reset(void) {
    for (int i = 0; i < SC2_MAX_GROUPS; i++) {
        free(sc2_unit_groups[i].items); free(sc2_player_groups[i].items);
    }
    for (int i = 0; i < sc2_loop_n; i++) free(sc2_loops[i].snapshot.items);
    memset(sc2_unit_groups, 0, sizeof(sc2_unit_groups));
    memset(sc2_player_groups, 0, sizeof(sc2_player_groups));
    sc2_unit_group_n = sc2_player_group_n = 1; sc2_loop_n = 0;
}
