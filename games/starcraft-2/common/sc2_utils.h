#ifndef SC2_UTILS_H
#define SC2_UTILS_H

#include "common/common.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static bool sc2_streqi(cstring_t a, cstring_t b) {
    return a && b && !strcasecmp(a, b);
}

static bool sc2_contains_i(cstring_t text, cstring_t needle) {
    char a[128], b[64];
    uint32_t i;
    if (!text || !needle) return false;
    snprintf(a, sizeof(a), "%s", text);
    snprintf(b, sizeof(b), "%s", needle);
    for (i = 0; a[i]; i++) a[i] = (char)tolower((unsigned char)a[i]);
    for (i = 0; b[i]; i++) b[i] = (char)tolower((unsigned char)b[i]);
    return strstr(a, b) != NULL;
}

static bool sc2_has_extension_i(cstring_t path, cstring_t ext) {
    uint32_t path_len;
    uint32_t ext_len;

    if (!path || !ext) return false;
    path_len = (uint32_t)strlen(path);
    ext_len = (uint32_t)strlen(ext);
    return path_len >= ext_len && !strcasecmp(path + path_len - ext_len, ext);
}

static bool sc2_path_has_dir(cstring_t path) {
    return path && (strchr(path, '\\') || strchr(path, '/'));
}

static void sc2_normalize_slashes(string_t path) {
    if (!path) return;
    for (char *p = path; *p; p++) if (*p == '/') *p = '\\';
}

static void sc2_append_extension(string_t path, uint32_t size, cstring_t ext) {
    if (!path || !ext || !*path || strchr(strrchr(path, '\\') ? strrchr(path, '\\') : path, '.')) return;
    strncat(path, ext, size - strlen(path) - 1);
}

static void sc2_camel_to_underscore(cstring_t in, string_t out, uint32_t out_size) {
    uint32_t w = 0;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!in) return;
    for (uint32_t i = 0; in[i] && w + 1 < out_size; i++) {
        bool split = i > 0 && isupper((unsigned char)in[i]) &&
                     (islower((unsigned char)in[i - 1]) ||
                      (in[i + 1] && islower((unsigned char)in[i + 1])));
        if (split && w + 1 < out_size) out[w++] = '_';
        out[w++] = in[i];
    }
    out[w] = '\0';
}

static uint32_t sc2_hash32(cstring_t str) {
    uint32_t hash = 2166136261u;
    while (str && *str) hash = (hash ^ (uint8_t)*str++) * 16777619u;
    return hash;
}

static bool sc2_parse_vec3(cstring_t text, vector3_t * out) {
    int count;

    if (!text || !out) return false;
    count = sscanf(text, "%f,%f,%f", &out->x, &out->y, &out->z);
    if (count < 2) return false;
    if (count == 2) out->z = 0.0f;
    return true;
}

static bool sc2_has_nonspace(cstring_t text) {
    if (!text) return false;
    while (*text) {
        if (!isspace((unsigned char)*text)) return true;
        text++;
    }
    return false;
}

#endif
