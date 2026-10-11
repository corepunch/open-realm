#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

/* Long-journey parity diagnostic (docs/games/warcraft-3/retail-journey-parity.md).
 * tools/frida/make_wc3_journey_map.py writes one probe map that the original game and
 * this replay both load. The replay runs the actual map loader and probe timer at the
 * shipped server cadence and exports the probe's own hashtable rows. It needs local
 * retail data, so it runs only when WC3_JOURNEY_MAP and WC3_JOURNEY_OUT are set:
 *   openwarcraft3-tests -data "$WC3_DATA" +dedicated 1 +test wc3_journey.replay */
extern void (*test_preload_marker)(cstring_t);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestWorldBounds(box2_t const *);

static FILE *journey_out, *journey_commits;
static bool journey_complete;

static hashtableEntry_t const *journey_cell(hashtable_t const *table, int32_t row, int32_t col) {
    for (uint32_t i = table->num_entries; i-- > 0;)
        if (table->entries[i].parent == row && table->entries[i].child == col) return table->entries + i;
    return NULL;
}

/* One marker names the rows exported for a sampled tick; read them back as words. */
static void journey_marker(cstring_t text) {
    unsigned tick, first, count;
    if (!strcmp(text, "PATHMETA complete")) { journey_complete = true; return; }
    hashtable_t const *table = NULL;
    FOR_LOOP(i, MAX_HASHTABLES) if (level.hashtables[i].inuse) { table = level.hashtables + i; break; }
    if (sscanf(text, "PATHMETA unitdata row=%u", &first) == 1 && table) {
        /* Authored scalar probe: type, then ten public default/current reals. */
        hashtableEntry_t const *kind = journey_cell(table, first, 0); T_NOT_NULL(kind); if (!kind) return;
        fprintf(journey_out, "{\"unitdata\":%u,\"type\":%d,\"words\":[", first, kind->value.integer);
        for (int c = 1; c <= 10; c++) {
            hashtableEntry_t const *e = journey_cell(table, first, c); T_NOT_NULL(e);
            fprintf(journey_out, "%s%u", c > 1 ? "," : "", e ? wc3_float_bits(e->value.real) : 0u);
        }
        fprintf(journey_out, "]}\n");
        return;
    }
    unsigned cols;
    if (sscanf(text, "PATHMETA ints first=%u count=%u cols=%u", &first, &count, &cols) == 3 && table) {
        /* Generic integer rows (diplomacy probe). */
        for (unsigned row = first; row < first + count; row++) {
            fprintf(journey_out, "{\"ints\":%u,\"words\":[", row);
            for (unsigned c = 0; c < cols; c++) {
                hashtableEntry_t const *e = journey_cell(table, row, c); T_NOT_NULL(e);
                fprintf(journey_out, "%s%d", c ? "," : "", e ? e->value.integer : -1);
            }
            fprintf(journey_out, "]}\n");
        }
        return;
    }
    if (sscanf(text, "PATHMETA journey tick=%u first=%u count=%u", &tick, &first, &count) != 3) return;
    T_NOT_NULL(table); if (!table) return;
    for (unsigned row = first; row < first + count; row++) {
        hashtableEntry_t const *cells[6];
        FOR_LOOP(c, 6) { cells[c] = journey_cell(table, row, c); T_NOT_NULL(cells[c]); if (!cells[c]) return; }
        fprintf(journey_out, "{\"tick\":%d,\"case\":%d,\"words\":[%u,%u,%u,%u]}\n", cells[0]->value.integer,
                cells[1]->value.integer, (uint32_t)cells[2]->value.integer, wc3_float_bits(cells[3]->value.real),
                wc3_float_bits(cells[4]->value.real), wc3_float_bits(cells[5]->value.real));
    }
}

/* Fine-cell velocity words, keeping the original zero sign (wc3_div canonicalizes it). */
static void journey_commit(edict_t *u) {
    fprintf(journey_commits, "%d %08x %08x %08x %08x %08x %08x\n", (int)(u - g_edicts),
            wc3_float_bits(u->movement.pose_clock.time), wc3_float_bits(u->movement.fine_pose.x),
            wc3_float_bits(u->movement.fine_pose.y), wc3_float_bits(wc3_velocity_scale(u->movement.velocity.x, wc3_float(0x3d000000))),
            wc3_float_bits(wc3_velocity_scale(u->movement.velocity.y, wc3_float(0x3d000000))), wc3_float_bits(u->s.angle));
}

TEST(wc3_journey, replay) {
    char const *map = getenv("WC3_JOURNEY_MAP"), *out = getenv("WC3_JOURNEY_OUT"), *commits = getenv("WC3_JOURNEY_COMMITS");
    if (!map || !out) return;
    journey_out = fopen(out, "w"); T_NOT_NULL(journey_out); if (!journey_out) return;
    if (commits) journey_commits = fopen(commits, "w"), move_test_motion_commit = journey_commit;
    reset_entities(); setup_test_world();
    /* setup_test_world owns static terrain; the real loader owns heap terrain. */
    world.map = NULL; CM_SetupTestWorldBounds(NULL);
    journey_complete = false; test_preload_marker = journey_marker;
    T_ASSERT(globals.LoadMap(map));
    level.started = true;
    while (!journey_complete && level.time < 3600000) { level.time += FRAMETIME; globals.RunFrame(); }
    T_ASSERT(journey_complete);
    T_ASSERT(!jass_rterror_pending(level.vm));
    test_preload_marker = NULL; move_test_motion_commit = NULL;
    fclose(journey_out); journey_out = NULL;
    if (journey_commits) fclose(journey_commits), journey_commits = NULL;
    G_ReleaseLevel(); CM_W3ClearMapData();
    setup_test_world(); reset_entities();
}
#endif
