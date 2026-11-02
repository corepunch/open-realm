"""Retained adaptive identities match full-clear searches, including epoch wrap."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "games/warcraft-3/common/wc3_pathing_adaptive.h"

static uint32_t seed = 9187;
static uint32_t random_word(void) { return seed = seed * 1664525u + 1013904223u; }
static void compare(wc3AccSearch_t *reference, wc3AccSearch_t *retained, wc3AccRequest_t *request) {
    int expected = wc3_acc_search(reference, request);
    int actual = wc3_acc_search(retained, request);
    wc3FineSearch_t *a = &reference->work, *b = &retained->work;
    assert(actual == expected);
    assert(a->count == b->count && a->queued == b->queued && a->pops == b->pops);
    assert(a->reopens == b->reopens && a->stale == b->stale);
    assert(a->nearest == b->nearest && a->dist2 == b->dist2);
    if (a->count) assert(!memcmp(a->nodes, b->nodes, a->count * sizeof(*a->nodes)));
    if (a->queued) assert(!memcmp(a->heap + 1, b->heap + 1, a->queued * sizeof(*a->heap)));
    if (a->count) assert(!memcmp(reference->levels, retained->levels, a->count));
    if (a->count) assert(!memcmp(reference->source_ids, retained->source_ids, a->count));
    if (a->count) assert(!memcmp(reference->gate_ids, retained->gate_ids, a->count));
}
int main(void) {
    wc3AccSearch_t reference = {0}, retained = {.reuse_indices = true};
    uint8_t *classes[4];
    for (unsigned level = 0; level < 4; level++) {
        unsigned side = 128 >> level, count = side * side;
        classes[level] = calloc(count, 1);
        reference.maps[level] = (wc3AccMap_t){side, side, classes[level], malloc(count * sizeof(int))};
        retained.maps[level] = (wc3AccMap_t){side, side, classes[level], malloc(count * sizeof(int))};
    }
    for (unsigned iteration = 0; iteration < 6000; iteration++) {
        if (!(iteration % 23)) {
            /* Alternate open promotion-heavy maps and mixed subdivision maps.
             * Replacing classes between requests exercises transient blockers. */
            for (unsigned y = 0; y < 128; y++) for (unsigned x = 0; x < 128; x++)
                classes[0][y * 128 + x] = iteration % 46 && random_word() % 9 == 0;
            for (unsigned level = 1; level < 4; level++) {
                unsigned side = 128 >> level;
                for (unsigned y = 0; y < side; y++) for (unsigned x = 0; x < side; x++) {
                    unsigned blocked = 0, clear = 0;
                    for (unsigned dy = 0; dy < 2; dy++) for (unsigned dx = 0; dx < 2; dx++) {
                        unsigned value = classes[level - 1][(y * 2 + dy) * side * 2 + x * 2 + dx];
                        blocked += value == 1; clear += value == 0;
                    }
                    classes[level][y * side + x] = clear == 4 ? 0 : blocked == 4 ? 1 : 2;
                }
            }
        }
        wc3AccRequest_t request = {
            {(float)(random_word() % 128) + .25f, (float)(random_word() % 128) + .75f},
            {(float)(random_word() % 128) + .75f, (float)(random_word() % 128) + .25f},
            1 + (iteration & 1), iteration % 5 ? 400 : iteration % 37
        };
        if (iteration % 97 == 0) request.goal = request.start;
        if (iteration % 101 == 0) retained.index_epoch = 65534;
        compare(&reference, &retained, &request);
    }
    /* First publication keeps low-16-bit identity even beyond 65535 nodes. */
    wc3_acc_free(&retained);
    retained.reuse_indices = true; retained.index_epoch = 51;
    for (unsigned level = 0; level < 4; level++) {
        unsigned count = retained.maps[level].width * retained.maps[level].height;
        memset(retained.maps[level].indices, 0, count * sizeof(int));
        memset(classes[level], level ? 2 : 0, count);
    }
    wc3_fine_reserve(&retained.work, 65540, 0);
    retained.work.count = 65535;
    for (unsigned i = 0; i < 3; i++) {
        assert(wc3_acc_find(&retained, 0, (wc3FinePoint_t){i, 0}) == (int)((65535 + i) & 65535));
        assert(wc3_acc_find(&retained, 0, (wc3FinePoint_t){i, 0}) == (int)((65535 + i) & 65535));
    }
    wc3_acc_free(&reference); wc3_acc_free(&retained);
    for (unsigned level = 0; level < 4; level++) {
        free(classes[level]); free(reference.maps[level].indices); free(retained.maps[level].indices);
    }
    return 0;
}
'''


class AdaptiveEpochTests(unittest.TestCase):
    def test_complete_search_state_matches_full_clear(self):
        with tempfile.TemporaryDirectory(prefix="wc3-adaptive-epochs-") as directory:
            source = Path(directory) / "test.c"
            source.write_text(SOURCE)
            for optimization in ("-O0", "-O2"):
                binary = Path(directory) / optimization
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                "-Wno-unused-function", "-fsanitize=undefined",
                                "-fno-sanitize-recover=all", optimization, "-I", str(ROOT),
                                str(source), "-o", str(binary), "-lm"], check=True)
                subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
