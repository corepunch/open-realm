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
#define STB_SLK_INT    BZ_FIELD_U32    /* atoi() → DWORD/LONG field             */
#define STB_SLK_FLOAT  BZ_FIELD_FLOAT  /* atof() → FLOAT field                  */
#define STB_SLK_BOOL   BZ_FIELD_BOOL   /* "1"/"TRUE" → BOOL field               */
#define STB_SLK_STR    BZ_FIELD_CSTR   /* owned string; freed with FS_SLKFreeRows */
#define STB_SLK_FOURCC BZ_FIELD_FOURCC /* 4-char text → DWORD via memcpy (LE)   */

/* Schema entry — one SLK/INI key → one struct field. */
typedef struct slkField_s {
    LPCSTR        column;  /* SLK column header (Y=1 text) or INI key name */
    ptrdiff_t     offset;  /* offsetof(RowStruct, field)                   */
    bzFieldType_t type;
    LPCSTR        id;      /* optional four-character object-data field ID */
} slkField_t;

/* Stateful typed-table cache, matching stbDbcCache_t ownership.  `source` is
 * parser-private; consumers only read decoded rows or query the index. */
typedef struct {
    void *source;
    void *rows;
    slkField_t const *schema;
    DWORD count, row_stride;
    ptrdiff_t key_offset;
    BOOL has_key;
} stbSlkCache_t;

/* INI files are runtime-keyed dictionaries, so keep their parser state opaque
 * and expose lookup rather than pretending every file has a fixed row type. */
typedef struct { void *source; } stbIniCache_t;

#define STB_SLK_ROW(cache, T, idx) \
    ((T const *)((BYTE const *)(cache).rows + (size_t)(idx) * (cache).row_stride))

BOOL Stb_SlkCacheLoad(stbSlkCache_t *cache, LPCSTR filename, slkField_t const *schema, DWORD row_stride);
BOOL Stb_SlkCacheLoadBuffer(stbSlkCache_t *cache, LPCSTR buffer, slkField_t const *schema, DWORD row_stride);
BOOL Stb_IniCacheLoad(stbIniCache_t *cache, LPCSTR filename);
BOOL Stb_IniCacheLoadFiles(stbIniCache_t *cache, LPCSTR const *filenames);
BOOL Stb_IniCacheDecode(stbIniCache_t const *ini, stbSlkCache_t *cache,
                        slkField_t const *schema, DWORD row_stride);
LPCSTR Stb_IniCacheFind(stbIniCache_t const *cache, LPCSTR section, LPCSTR key);
void Stb_IniCacheFree(stbIniCache_t *cache);

/* -------------------------------------------------------------------------
 * FOURCC key helpers — convert a 4-char unit-code string to a DWORD for
 * hash / sort keys.
 * -------------------------------------------------------------------------*/
static inline DWORD FS_SLKKey(LPCSTR name) {
    DWORD key = 0;
    if (name) {
        size_t n = strlen(name);
        memcpy(&key, name, n < 4 ? n : 4);
    }
    return key;
}

/* Release every owned string in a typed row array, then release the array. */
static inline void FS_SLKFreeRows(slkField_t const *schema, void *rows, DWORD count, size_t stride) {
    if (!schema || !rows) return;
    FOR_LOOP(i, count) {
        for (slkField_t const *field = schema; field->column; field++) {
            bool first = true;
            for (slkField_t const *prev = schema; prev < field; prev++)
                if (prev->type == BZ_FIELD_CSTR && prev->offset == field->offset) { first = false; break; }
            if (first && field->type == BZ_FIELD_CSTR)
                free(*(void **)((BYTE *)rows + i * stride + field->offset));
        }
    }
    free(rows);
}

static inline void Stb_SlkCacheFree(stbSlkCache_t *cache) {
    if (!cache) return;
    FS_SLKFreeRows(cache->schema, cache->rows, cache->count, cache->row_stride);
    memset(cache, 0, sizeof(*cache));
}

/* -------------------------------------------------------------------------
 * Sorted lookup index — parallel key[] / row[] arrays for binary search.
 * Not heap-owning: rows[] points into the caller's decoded array.
 * -------------------------------------------------------------------------*/
typedef struct { DWORD *keys; void **rows; DWORD count; } slkIndex_t;

/* Build the sorted index from a decoded row array whose first field is its
 * FOURCC key.  Typed SLK rows own their identity; parser rows stay private. */
static inline void FS_SLKBuildIndex(slkIndex_t *idx, void *rows_base, DWORD n, size_t stride) {
    if (!idx || !rows_base || !n) return;
    idx->keys = (DWORD *)malloc(n * sizeof(DWORD));
    idx->rows = (void **)malloc(n * sizeof(void *));
    if (!idx->keys || !idx->rows) {
        fprintf(stderr, "SLK: out of memory building %u-row index\n", n);
        free(idx->keys); free(idx->rows);
        idx->keys = NULL; idx->rows = NULL; idx->count = 0;
        return;
    }
    FOR_LOOP(i, n) {
        idx->rows[i] = (BYTE *)rows_base + i * stride;
        idx->keys[i] = *(DWORD *)idx->rows[i];
    }
    idx->count = n;
    /* Sort both arrays together by key using an index-sort. */
    /* Simple insertion sort — ~1000 units, negligible at load time. */
    for (DWORD j = 1; j < idx->count; j++) {
        DWORD tk = idx->keys[j]; void *tv = idx->rows[j];
        DWORD k = j;
        while (k > 0 && idx->keys[k-1] > tk) {
            idx->keys[k] = idx->keys[k-1];
            idx->rows[k] = idx->rows[k-1];
            k--;
        }
        idx->keys[k] = tk; idx->rows[k] = tv;
    }
}

/* Binary search for a FOURCC key; returns the matching row or NULL. */
static inline void *FS_SLKLookup(slkIndex_t const *idx, DWORD key) {
    if (!idx || !idx->keys || !idx->count) return NULL;
    DWORD lo = 0, hi = idx->count;
    while (lo < hi) {
        DWORD mid = (lo + hi) >> 1;
        if      (idx->keys[mid] < key) lo = mid + 1;
        else if (idx->keys[mid] > key) hi = mid;
        else return idx->rows[mid];
    }
    return NULL;
}

static inline void *Stb_SlkCacheFind(stbSlkCache_t const *cache, DWORD key) {
    if (!cache || !cache->rows || !cache->has_key) return NULL;
    FOR_LOOP(i, cache->count) {
        BYTE *row = (BYTE *)cache->rows + i * cache->row_stride;
        if (*(DWORD *)(row + cache->key_offset) == key) return row;
    }
    return NULL;
}

static inline void FS_SLKFreeIndex(slkIndex_t *idx) {
    if (!idx) return;
    free(idx->keys); free(idx->rows);
    idx->keys = NULL; idx->rows = NULL; idx->count = 0;
}

#endif /* stb_slk_h */
