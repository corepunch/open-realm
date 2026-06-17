/*
 * imgdiff - compare two images for the golden-image render test.
 *
 * Loads two images (PNG/TGA/etc via stb_image), requires matching dimensions,
 * and reports mean absolute per-channel difference (0-255), the max single-pixel
 * channel difference, and the fraction of pixels that differ beyond a small
 * tolerance. Exits non-zero if the mean difference exceeds --threshold, so it
 * can drive a regression test. Optionally writes a visual diff image.
 *
 * Standalone: links only stb (header-only) + libm, NOT the renderer (which also
 * defines STB_IMAGE_IMPLEMENTATION), so it has its own minimal Makefile rule.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "renderer/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "renderer/stb/stb_image_write.h"

static void usage(void) {
    fprintf(stderr,
        "Usage: imgdiff <a.png> <b.png> [--threshold <mean>] [--pixel-tol <0-255>] [--diff <out.png>]\n"
        "  Exit 0 if mean abs difference <= threshold (default 2.0), else 1.\n"
        "  --pixel-tol: per-channel delta counted as a differing pixel (default 8).\n"
        "  --diff: write an amplified difference image for inspection.\n");
}

int main(int argc, char **argv) {
    const char *path_a = NULL, *path_b = NULL, *diff_path = NULL;
    double threshold = 2.0;
    int pixel_tol = 8;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--threshold") && i + 1 < argc) {
            threshold = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--pixel-tol") && i + 1 < argc) {
            pixel_tol = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--diff") && i + 1 < argc) {
            diff_path = argv[++i];
        } else if (argv[i][0] == '-') {
            usage();
            return 2;
        } else if (!path_a) {
            path_a = argv[i];
        } else if (!path_b) {
            path_b = argv[i];
        } else {
            usage();
            return 2;
        }
    }
    if (!path_a || !path_b) { usage(); return 2; }

    int wa, ha, na, wb, hb, nb;
    unsigned char *a = stbi_load(path_a, &wa, &ha, &na, 4);
    if (!a) { fprintf(stderr, "imgdiff: cannot load '%s': %s\n", path_a, stbi_failure_reason()); return 2; }
    unsigned char *b = stbi_load(path_b, &wb, &hb, &nb, 4);
    if (!b) { fprintf(stderr, "imgdiff: cannot load '%s': %s\n", path_b, stbi_failure_reason()); stbi_image_free(a); return 2; }

    if (wa != wb || ha != hb) {
        fprintf(stderr, "imgdiff: DIMENSION MISMATCH %dx%d vs %dx%d\n", wa, ha, wb, hb);
        stbi_image_free(a); stbi_image_free(b);
        return 1;
    }

    size_t bytes = (size_t)wa * (size_t)ha * 4;
    double sum = 0.0;
    int maxd = 0;
    long diff_pixels = 0;
    unsigned char *diff = diff_path ? malloc(bytes) : NULL;

    for (size_t p = 0; p < bytes; p += 4) {
        int pixel_max = 0;
        for (int c = 0; c < 4; c++) {
            int d = abs((int)a[p + c] - (int)b[p + c]);
            sum += d;
            if (d > maxd) maxd = d;
            if (d > pixel_max) pixel_max = d;
            if (diff) diff[p + c] = (unsigned char)(d > 255 ? 255 : d * 4 > 255 ? 255 : d * 4);
        }
        if (diff) diff[p + 3] = 255;
        if (pixel_max > pixel_tol) diff_pixels++;
    }

    double mean = sum / (double)bytes;
    long total_pixels = (long)wa * (long)ha;
    double diff_frac = 100.0 * (double)diff_pixels / (double)total_pixels;

    if (diff) {
        stbi_write_png(diff_path, wa, ha, 4, diff, wa * 4);
        free(diff);
    }
    stbi_image_free(a);
    stbi_image_free(b);

    int pass = mean <= threshold;
    fprintf(stderr, "imgdiff: %s  mean=%.4f max=%d diff_pixels=%.3f%% (threshold mean<=%.4f) -> %s\n",
            path_b, mean, maxd, diff_frac, threshold, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
