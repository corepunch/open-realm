/*
 * stb_slk.h — Schema-driven SLK / INI row decoder for Warcraft III.
 *
 * Mirrors stb_dbc.h's stbDbcField_t / Stb_DbcParseRows pattern but for
 * string-keyed columnar data decoded through slkField_t DDX schemas.
 *
 * Typical use — index (after decoding all rows into a flat array `rows`):
 *
 *   slkIndex_t idx = {0};
 *   FS_SLKBuildIndex(&idx, rows, n, sizeof(UnitBalance_t));
 *   UnitBalance_t *r = FS_SLKLookup(&idx, unit_fourcc);
 *
 * The index uses a sorted key array with binary search; callers must
 * keep the decoded row array alive (it is NOT owned by slkIndex_t).
 * Call FS_SLKFreeIndex to release the key/ptr arrays.
 */
#ifndef stb_slk_h
#define stb_slk_h

#include "common/shared.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Field type aliases matching stb_dbc.h's STB_DBC_* names.
 * -------------------------------------------------------------------------*/
#define STB_SLK_INT    BZ_FIELD_U32    /* atoi() → uint32_t/int32_t field             */
#define STB_SLK_FLOAT  BZ_FIELD_FLOAT  /* atof() → float field                  */
#define STB_SLK_BOOL   BZ_FIELD_BOOL   /* "1"/"true" → bool field               */
#define STB_SLK_STR    BZ_FIELD_CSTR   /* owned string; freed with FS_SLKFreeRows */
#define STB_SLK_FOURCC BZ_FIELD_FOURCC /* 4-char text → uint32_t via memcpy (LE)   */

/* Schema entry — one SLK/INI key → one struct field. */
typedef struct slkField_s {
    cstring_t        column;  /* SLK column header (Y=1 text) or INI key name */
    ptrdiff_t     offset;  /* offsetof(RowStruct, field)                   */
    bzFieldType_t type;
    cstring_t        id;      /* optional four-character object-data field ID */
    cstring_t        default_value; /* typed value used when the source omits the field */
} slkField_t;

/* INI files are runtime-keyed dictionaries, so keep their parser state opaque
 * and expose lookup rather than pretending every file has a fixed row type. */
typedef struct { void *source; } stbIniCache_t;

/* Load SLK from file → allocate *dest, return count (0 on failure). */
uint32_t Stb_SlkLoad(cstring_t filename, slkField_t const *schema, void **dest, uint32_t row_stride);
/* Load SLK from in-memory buffer → allocate *dest, return count (0 on failure). */
uint32_t Stb_SlkLoadBuffer(cstring_t buffer, slkField_t const *schema, void **dest, uint32_t row_stride);
bool Stb_IniCacheLoad(stbIniCache_t *cache, cstring_t filename);
bool Stb_IniCacheLoadBuffer(stbIniCache_t *cache, cstring_t buffer);
bool Stb_IniCacheLoadFiles(stbIniCache_t *cache, cstring_t const *filenames);
/* Decode an INI cache into a typed row array → allocate *dest, return count. */
uint32_t Stb_IniDecode(stbIniCache_t const *ini, slkField_t const *schema, void **dest, uint32_t row_stride);
cstring_t Stb_IniCacheFind(stbIniCache_t const *cache, cstring_t section, cstring_t key);
/* Release parsed INI tables and clear the cache; safe for a zero-initialized cache. */
void Stb_IniCacheFree(stbIniCache_t *cache);

/* -------------------------------------------------------------------------
 * FOURCC key helpers — convert a 4-char unit-code string to a uint32_t for
 * hash / sort keys.
 * -------------------------------------------------------------------------*/
static inline uint32_t FS_SLKKey(cstring_t name) {
    uint32_t key = 0;
    if (name) {
        size_t n = strlen(name);
        memcpy(&key, name, n < 4 ? n : 4);
    }
    return key;
}

/* Release every owned string in a typed row array, then release the array. */
static inline void FS_SLKFreeRows(slkField_t const *schema, void *rows, uint32_t count, size_t stride) {
    if (!schema || !rows) return;
    FOR_LOOP(i, count) {
        for (slkField_t const *field = schema; field->column; field++) {
            bool first = true;
            for (slkField_t const *prev = schema; prev < field; prev++)
                if (prev->type == BZ_FIELD_CSTR && prev->offset == field->offset) { first = false; break; }
            if (first && field->type == BZ_FIELD_CSTR)
                free(*(void **)((uint8_t *)rows + i * stride + field->offset));
        }
    }
    free(rows);
}

/* -------------------------------------------------------------------------
 * Sorted lookup index — parallel key[] / row[] arrays for binary search.
 * Not heap-owning: rows[] points into the caller's decoded array.
 * -------------------------------------------------------------------------*/
typedef struct { uint32_t *keys; void **rows; uint32_t count; } slkIndex_t;

/* Build the sorted index from a decoded row array whose first field is its
 * FOURCC key.  Typed SLK rows own their identity; parser rows stay private. */
static inline void FS_SLKBuildIndex(slkIndex_t *idx, void *rows_base, uint32_t n, size_t stride) {
    if (!idx || !rows_base || !n) return;
    idx->keys = (uint32_t *)malloc(n * sizeof(uint32_t));
    idx->rows = (void **)malloc(n * sizeof(void *));
    if (!idx->keys || !idx->rows) {
        fprintf(stderr, "SLK: out of memory building %u-row index\n", n);
        free(idx->keys); free(idx->rows);
        idx->keys = NULL; idx->rows = NULL; idx->count = 0;
        return;
    }
    FOR_LOOP(i, n) {
        idx->rows[i] = (uint8_t *)rows_base + i * stride;
        idx->keys[i] = *(uint32_t *)idx->rows[i];
    }
    idx->count = n;
    /* Sort both arrays together by key using an index-sort. */
    /* Simple insertion sort — ~1000 units, negligible at load time. */
    for (uint32_t j = 1; j < idx->count; j++) {
        uint32_t tk = idx->keys[j]; void *tv = idx->rows[j];
        uint32_t k = j;
        while (k > 0 && idx->keys[k-1] > tk) {
            idx->keys[k] = idx->keys[k-1];
            idx->rows[k] = idx->rows[k-1];
            k--;
        }
        idx->keys[k] = tk; idx->rows[k] = tv;
    }
}

/* Binary search for a FOURCC key; returns the matching row or NULL. */
static inline void *FS_SLKLookup(slkIndex_t const *idx, uint32_t key) {
    if (!idx || !idx->keys || !idx->count) return NULL;
    uint32_t lo = 0, hi = idx->count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        if      (idx->keys[mid] < key) lo = mid + 1;
        else if (idx->keys[mid] > key) hi = mid;
        else return idx->rows[mid];
    }
    return NULL;
}

static inline void FS_SLKFreeIndex(slkIndex_t *idx) {
    if (!idx) return;
    free(idx->keys); free(idx->rows);
    idx->keys = NULL; idx->rows = NULL; idx->count = 0;
}

#endif /* stb_slk_h */
