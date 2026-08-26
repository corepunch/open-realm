#ifndef SC2_UTILS_H
#define SC2_UTILS_H

#include "common/common.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static BOOL sc2_streqi(LPCSTR a, LPCSTR b) {
    return a && b && !strcasecmp(a, b);
}

static BOOL sc2_contains_i(LPCSTR text, LPCSTR needle) {
    char a[128], b[64];
    DWORD i;
    if (!text || !needle) return false;
    snprintf(a, sizeof(a), "%s", text);
    snprintf(b, sizeof(b), "%s", needle);
    for (i = 0; a[i]; i++) a[i] = (char)tolower((unsigned char)a[i]);
    for (i = 0; b[i]; i++) b[i] = (char)tolower((unsigned char)b[i]);
    return strstr(a, b) != NULL;
}

static BOOL sc2_has_extension_i(LPCSTR path, LPCSTR ext) {
    DWORD path_len;
    DWORD ext_len;

    if (!path || !ext) return false;
    path_len = (DWORD)strlen(path);
    ext_len = (DWORD)strlen(ext);
    return path_len >= ext_len && !strcasecmp(path + path_len - ext_len, ext);
}

static BOOL sc2_path_has_dir(LPCSTR path) {
    return path && (strchr(path, '\\') || strchr(path, '/'));
}

static void sc2_normalize_slashes(LPSTR path) {
    if (!path) return;
    for (char *p = path; *p; p++) if (*p == '/') *p = '\\';
}

static void sc2_append_extension(LPSTR path, DWORD size, LPCSTR ext) {
    if (!path || !ext || !*path || strchr(strrchr(path, '\\') ? strrchr(path, '\\') : path, '.')) return;
    strncat(path, ext, size - strlen(path) - 1);
}

static void sc2_camel_to_underscore(LPCSTR in, LPSTR out, DWORD out_size) {
    DWORD w = 0;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!in) return;
    for (DWORD i = 0; in[i] && w + 1 < out_size; i++) {
        BOOL split = i > 0 && isupper((unsigned char)in[i]) &&
                     (islower((unsigned char)in[i - 1]) ||
                      (in[i + 1] && islower((unsigned char)in[i + 1])));
        if (split && w + 1 < out_size) out[w++] = '_';
        out[w++] = in[i];
    }
    out[w] = '\0';
}

static DWORD sc2_hash32(LPCSTR str) {
    DWORD hash = 2166136261u;
    while (str && *str) hash = (hash ^ (BYTE)*str++) * 16777619u;
    return hash;
}

static BOOL sc2_parse_vec3(LPCSTR text, LPVECTOR3 out) {
    int count;

    if (!text || !out) return false;
    count = sscanf(text, "%f,%f,%f", &out->x, &out->y, &out->z);
    if (count < 2) return false;
    if (count == 2) out->z = 0.0f;
    return true;
}

static BOOL sc2_has_nonspace(LPCSTR text) {
    if (!text) return false;
    while (*text) {
        if (!isspace((unsigned char)*text)) return true;
        text++;
    }
    return false;
}

#endif
