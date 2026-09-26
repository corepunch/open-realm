#ifndef WOW_CONFIG_H
#define WOW_CONFIG_H

#include "common/shared.h"
#include <strings.h>

#define WOW_CS_MAPINFO (CS_MINIMAP + 1) // configstring slot; carries WoW loading title and preview using info-key encoding

/* Read one value from the Quake-style key/value configstring used by WoW player and map setup. */
static cstring_t Wow_InfoValueForKey(cstring_t str, cstring_t key, cstring_t fallback) {
    static char value[2][MAX_PATHLEN];
    static int value_index;
    char pkey[64], *out;
    cstring_t s = str;

    if (!s || !key || !*key) return fallback;
    value_index ^= 1;
    if (*s == '\\') s++;
    while (*s) {
        out = pkey;
        while (*s && *s != '\\') *out++ = *s++;
        *out = 0;
        if (*s) s++;
        out = value[value_index];
        while (*s && *s != '\\') *out++ = *s++;
        *out = 0;
        if (!strcasecmp(pkey, key)) return value[value_index];
        if (*s) s++;
    }
    return fallback;
}

#endif
