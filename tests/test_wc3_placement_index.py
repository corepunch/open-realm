"""Query-local placement pruning preserves the recovered scalar ring selector."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "games/warcraft-3/common/wc3_pathing_placement.h"

typedef struct {
    wc3FineBox_t rectangles[48], witness;
    uint32_t count, visits, phase;
    bool report;
} fixture_t;
static bool cell(void const *data, wc3FinePoint_t point) {
    fixture_t *f = (fixture_t *)data;
    f->visits++;
    for (uint32_t i = 0; i < f->count; i++) {
        wc3FineBox_t box = f->rectangles[i];
        if (point.x >= box.min.x && point.x < box.max.x && point.y >= box.min.y && point.y < box.max.y) {
            if (f->report) f->witness = box;
            return false;
        }
    }
    return true;
}
static bool admit(void const *data, float const *point) {
    fixture_t const *f = data;
    return !f->phase || (((uint32_t)(int)point[0] ^ (uint32_t)(int)point[1]) % f->phase) != 0;
}
static uint32_t seed = 713;
static uint32_t random_word(void) { return seed = seed * 1664525u + 1013904223u; }
static int compare(fixture_t *f, wc3FinePlacement_t *query) {
    float expected[2], actual[2];
    f->report = false;
    bool a = wc3_fine_place(query, expected);
    f->report = true;
    bool b = wc3_fine_place_indexed(query, actual, &f->witness);
    if (a != b || memcmp(expected, actual, sizeof(actual))) {
        fprintf(stderr, "placement mismatch seed=%u cls=%u limit=%u expected=%d %08x %08x actual=%d %08x %08x\n",
                seed, query->footprint.cls, query->limit, a, wc3_float_bits(expected[0]), wc3_float_bits(expected[1]),
                b, wc3_float_bits(actual[0]), wc3_float_bits(actual[1]));
        return 1;
    }
    return 0;
}
int main(void) {
    for (unsigned i = 0; i < 12000; i++) {
        fixture_t f = {.count = random_word() % 48, .phase = random_word() % 5};
        for (unsigned r = 0; r < f.count; r++) {
            int x = (int)(random_word() % 81) - 40, y = (int)(random_word() % 81) - 40;
            f.rectangles[r] = (wc3FineBox_t){{x,y},{x + 1 + (int)(random_word()%17), y + 1 + (int)(random_word()%17)}};
        }
        wc3FinePlacement_t query = {.point = {(int)(random_word()%81)-40 + .125f, (int)(random_word()%81)-40 + .875f},
            .limit = random_word()%33, .footprint = {.cls = random_word()%4, .cell = cell, .data = &f},
            .admit = admit, .integer_result = !!(i & 1)};
        if (compare(&f, &query)) return 1;
    }
    /* A single broad witness removes all 3969 ring centers after one read. */
    fixture_t f = {.count = 1, .rectangles = {{{-100,-100},{100,100}}}};
    wc3FinePlacement_t query = {.point = {-1.125f, 3.75f}, .limit = 32,
        .footprint = {.cls = 3, .cell = cell, .data = &f}, .admit = admit};
    float out[2];
    f.visits = 0;
    if (wc3_fine_place(&query, out)) return 2;
    uint32_t original = f.visits;
    f.visits = 0; f.report = true;
    if (wc3_fine_place_indexed(&query, out, &f.witness) || f.visits != 1 || original != 3969) return 3;
    printf("12000 identical placements; blocked-window cell reads %u -> %u\n", original, f.visits);
    return 0;
}
'''


class PlacementIndexTests(unittest.TestCase):
    def test_scalar_order_and_blocker_pruning(self):
        with tempfile.TemporaryDirectory(prefix="wc3-placement-index-") as directory:
            source = Path(directory) / "check.c"
            source.write_text(SOURCE)
            for optimization in ("-O0", "-O2"):
                binary = Path(directory) / optimization[1:]
                subprocess.run(["cc", "-std=c11", optimization, "-fsanitize=undefined",
                                "-fno-sanitize-recover=all", "-I", str(ROOT), str(source),
                                "-o", str(binary)], check=True, capture_output=True, text=True)
                result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
                self.assertIn("3969 -> 1", result.stdout)


if __name__ == "__main__":
    unittest.main()
