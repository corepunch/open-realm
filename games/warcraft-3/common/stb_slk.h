/*
 * stb_slk.h — Schema-driven SLK / INI row decoder for Warcraft III.
 *
 * Mirrors stb_dbc.h's stbDbcField_t / Stb_DbcParseRows pattern but for
 * string-keyed columnar data (sheetRow_t linked lists produced by
 * FS_ParseSLK / FS_ParseINI).  Source readers still own file I/O and the
 * linked-list representation; this header converts that representation into
 * typed C structs for direct struct-field access instead of per-call
 * FS_FindSheetCell + atoi / atof.
 *
 * Typical use — decode:
 *
 *   static slkField_t const balance_schema[] = {
 *       { "realHP",    offsetof(UnitBalance_t, realHP),    BZ_FIELD_FLOAT },
 *       { "spd",       offsetof(UnitBalance_t, spd),       BZ_FIELD_FLOAT },
 *       { "regenType", offsetof(UnitBalance_t, regenType), BZ_FIELD_CSTR  },
 *   };
 *   UnitBalance_t row = {0};
 *   FS_SLKDecodeRow(slk_row, balance_schema,
 *       sizeof(balance_schema)/sizeof(balance_schema[0]), &row);
 *
 * Typical use — index (after decoding all rows into a flat array `rows`):
 *
 *   slkIndex_t idx = {0};
 *   FS_SLKBuildIndex(&idx, head, rows, n, sizeof(UnitBalance_t));
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

/* -------------------------------------------------------------------------
 * Schema entry — one SLK column → one struct field.
 * Mirrors stbDbcField_t but with a string column key.
 * -------------------------------------------------------------------------*/
typedef struct {
    LPCSTR        column;  /* SLK column header (Y=1 text) or INI key name */
    ptrdiff_t     offset;  /* offsetof(RowStruct, field)                   */
    bzFieldType_t type;
} slkField_t;

/* -------------------------------------------------------------------------
 * FS_SLKDecodeRow — fill one struct from a sheetRow_t using a schema.
 *
 * schema is NULL-terminated (last entry has .column == NULL), following the
 * Quake 2 zero-sentinel field-table convention.  For each field in `row`,
 * a linear scan finds the first matching schema entry (case-insensitive) and
 * writes the converted value into *out at the given offset.  Unmatched row
 * fields are silently ignored; unmatched schema entries leave the struct
 * field at its initialised value (caller should zero the struct first).
 * -------------------------------------------------------------------------*/
static inline void FS_SLKDecodeRow(sheetRow_t const *row,
                                   slkField_t const *schema,
                                   void *out) {
    if (!row || !schema || !out) return;
    for (slkField_t const *s = schema; s->column; s++) {
        if (*s->column) continue;
        if (s->type == BZ_FIELD_CSTR) {
            size_t len = strlen(row->name); LPSTR value = (LPSTR)malloc(len + 1);
            if (!value) { fprintf(stderr, "SLK: out of memory copying row name '%s'\n", row->name); continue; }
            memcpy(value, row->name, len + 1); *(LPSTR *)((BYTE *)out + s->offset) = value;
        }
    }
    FOR_EACH_LIST(sheetField_t const, f, row->fields) {
        for (slkField_t const *s = schema; s->column; s++) {
            if (strcasecmp(f->name, s->column)) continue;
            BYTE *dst = (BYTE *)out + s->offset;
            switch (s->type) {
            case BZ_FIELD_U32:
                *(DWORD *)dst = f->value ? (DWORD)atoi(f->value) : 0; break;
            case BZ_FIELD_FLOAT:
                *(FLOAT *)dst = f->value ? (FLOAT)atof(f->value) : 0.f; break;
            case BZ_FIELD_BOOL:
                *(BOOL *)dst = f->value &&
                    (atoi(f->value) != 0 || !strcmp(f->value, "TRUE")); break;
            case BZ_FIELD_CSTR: {
                size_t len = f->value ? strlen(f->value) : 0;
                LPSTR value = (LPSTR)malloc(len + 1);
                if (!value) { fprintf(stderr, "SLK: out of memory copying field '%s'\n", f->name); break; }
                memcpy(value, f->value ? f->value : "", len + 1);
                free(*(void **)dst); *(LPSTR *)dst = value; break;
            }
            case BZ_FIELD_FOURCC: {
                DWORD k = 0;
                if (f->value) { size_t n = strlen(f->value); memcpy(&k, f->value, n < 4 ? n : 4); }
                *(DWORD *)dst = k; break;
            }
            default: break;
            }
            break; /* matched — move to next field */
        }
    }
}

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

/* -------------------------------------------------------------------------
 * Sorted lookup index — parallel key[] / row[] arrays for binary search.
 * Not heap-owning: rows[] points into the caller's decoded array.
 * -------------------------------------------------------------------------*/
typedef struct { DWORD *keys; void **rows; DWORD count; } slkIndex_t;

/* Build the sorted index from a decoded row array `rows_base` (flat, stride
 * bytes between rows) paired with the original sheetRow_t list (for keys).
 * Assumes one-to-one correspondence between `head` and `rows_base[0..n-1]`. */
static inline void FS_SLKBuildIndex(slkIndex_t *idx, sheetRow_t const *head,
                                    void *rows_base, DWORD n, size_t stride) {
    if (!idx || !head || !rows_base || !n) return;
    idx->keys = (DWORD *)malloc(n * sizeof(DWORD));
    idx->rows = (void **)malloc(n * sizeof(void *));
    if (!idx->keys || !idx->rows) {
        fprintf(stderr, "SLK: out of memory building %u-row index\n", n);
        free(idx->keys); free(idx->rows);
        idx->keys = NULL; idx->rows = NULL; idx->count = 0;
        return;
    }
    DWORD i = 0;
    FOR_EACH_LIST(sheetRow_t const, r, head) {
        if (i >= n) break;
        idx->keys[i] = FS_SLKKey(r->name);
        idx->rows[i] = (BYTE *)rows_base + i * stride;
        i++;
    }
    idx->count = i;
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

static inline void FS_SLKFreeIndex(slkIndex_t *idx) {
    if (!idx) return;
    free(idx->keys); free(idx->rows);
    idx->keys = NULL; idx->rows = NULL; idx->count = 0;
}

#endif /* stb_slk_h */
