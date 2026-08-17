/*
 * stb_dbc.h — Single-header reader for classic-era WoW client databases (WDBC).
 *
 * Both the UI, game, renderer, and common modules read DBC files, and each
 * used to reimplement the same little-endian reads, "WDBC" header validation,
 * and string-block access. This header is the shared, dependency-free subset.
 *
 * Everything is `static inline` so the header can be included from any
 * translation unit (ui, game, renderer, common, sound) without a separate
 * implementation file and without `-Wunused-function` noise in targets that
 * only use part of the API.
 *
 * The header is a pure memory-buffer parser: it performs no file I/O and no
 * allocation. Callers read the file with their own FS/RI/gi handle and hand the
 * resident buffer here.
 *
 * Typical use:
 *
 *     #include "common/stb_dbc.h"
 *
 *     LPBYTE data; int size = ri.FS_ReadFile("DBFilesClient\\Map.dbc", (void **)&data);
 *     stbDbc_t h;
 *     if (data && Stb_DbcValid(data, size, &h)) {
 *         BYTE const *records = Stb_DbcRecords(data);
 *         BYTE const *strings = Stb_DbcStrings(data, &h);
 *         BYTE const *rec = Stb_DbcFindID(data, &h, id);
 *         LPCSTR name = Stb_DbcString(strings, h.string_size, Stb_DbcField(&h, rec, 1));
 *     }
 */
#ifndef stb_dbc_h
#define stb_dbc_h

#include "common/shared.h"
#include <stddef.h>

/* -------------------------------------------------------------------------- */
/* WDBC header (magic byte excluded)                                           */
/* -------------------------------------------------------------------------- */
typedef struct {
    DWORD records;
    DWORD fields;
    DWORD record_size;
    DWORD string_size;
} stbDbc_t;

/* "WDBC" read back as a little-endian 32-bit word. */
#define STB_DBC_MAGIC 0x43424457u

/* -------------------------------------------------------------------------- */
/* Scalar reads                                                                */
/* -------------------------------------------------------------------------- */
static inline DWORD Stb_DbcRead32(BYTE const *p) {
    return ((DWORD)p[0]) | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static inline FLOAT Stb_DbcReadFloat(BYTE const *p) {
    FLOAT value;
    memcpy(&value, p, sizeof(value));
    return value;
}

/* -------------------------------------------------------------------------- */
/* Header validation                                                           */
/* -------------------------------------------------------------------------- */
/* Validates the magic and envelope, filling *header. Classic files may report a
 * logical field count larger than record_size / 4, so only record_size >= 4 is
 * required here; callers bounds-check each accessed field via Stb_DbcField. */
static inline BOOL Stb_DbcValid(BYTE const *data, DWORD size, stbDbc_t *header) {
    if (!data || !header || size <= 20 || memcmp(data, "WDBC", 4) != 0) {
        return false;
    }
    header->records = Stb_DbcRead32(data + 4);
    header->fields = Stb_DbcRead32(data + 8);
    header->record_size = Stb_DbcRead32(data + 12);
    header->string_size = Stb_DbcRead32(data + 16);
    if (!header->fields || header->record_size < sizeof(DWORD) ||
        20 + header->records * header->record_size + header->string_size > size) {
        return false;
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* Block pointers                                                              */
/* -------------------------------------------------------------------------- */
static inline BYTE const *Stb_DbcRecords(BYTE const *data) {
    return data + 20;
}

static inline BYTE const *Stb_DbcStrings(BYTE const *data, stbDbc_t const *header) {
    return data + 20 + header->records * header->record_size;
}

static inline BYTE const *Stb_DbcRecordAt(BYTE const *records, DWORD index, DWORD record_size) {
    return records + index * record_size;
}

/* -------------------------------------------------------------------------- */
/* Field and string access                                                     */
/* -------------------------------------------------------------------------- */
/* Bounds-checked 32-bit field access; returns 0 when the field index exceeds
 * the logical field count or the physical record. */
static inline DWORD Stb_DbcField(stbDbc_t const *header, BYTE const *record, DWORD field) {
    if (!header || !record || field >= header->fields ||
        field * sizeof(DWORD) + sizeof(DWORD) > header->record_size) {
        return 0;
    }
    return Stb_DbcRead32(record + field * sizeof(DWORD));
}

/* Resolve a string-block offset; offset 0 (null string) and out-of-range
 * offsets both return NULL. */
static inline LPCSTR Stb_DbcString(BYTE const *strings, DWORD string_size, DWORD offset) {
    if (!strings || !offset || offset >= string_size) {
        return NULL;
    }
    return (LPCSTR)(strings + offset);
}

/* -------------------------------------------------------------------------- */
/* Lookup                                                                      */
/* -------------------------------------------------------------------------- */
/* Linear scan for the record whose field 0 equals `id`; returns the record or
 * NULL. For hot lookups the renderer keeps its own FNV-1a index on top of this. */
static inline BYTE const *Stb_DbcFindID(BYTE const *data, stbDbc_t const *header, DWORD id) {
    BYTE const *records;
    DWORD i;
    if (!data || !header) {
        return NULL;
    }
    records = Stb_DbcRecords(data);
    for (i = 0; i < header->records; i++) {
        BYTE const *record = records + i * header->record_size;
        if (Stb_DbcRead32(record) == id) {
            return record;
        }
    }
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Table-driven row decode                                                     */
/* -------------------------------------------------------------------------- */
/* Fill a caller's struct array from a DBC record block using a small schema
 * table (the `{ name, offsetof(struct, field), type }` pattern from
 * stb_fdf.h). Each entry maps a DBC column index to a struct field. The parser
 * performs no I/O and no allocation; callers read the file and keep the buffer
 * (and its string block) alive for the lifetime of the decoded rows. */
typedef enum {
    STB_DBC_U32,  /* 4-byte little-endian int column */
    STB_DBC_STR,  /* 4-byte string-block offset, resolved to a pointer */
} stbDbcFieldType_t;

typedef struct {
    DWORD column;       /* DBC column index (0-based); byte offset = column * 4 */
    ptrdiff_t offset;   /* offsetof(Rec, field) */
    stbDbcFieldType_t type;
} stbDbcField_t;

static inline void Stb_DbcParseRows(BYTE const *records, DWORD count, DWORD record_size,
                                    BYTE const *strings, DWORD string_size,
                                    stbDbcField_t const *schema, DWORD schema_count,
                                    void *out, DWORD out_stride) {
    FOR_LOOP(r, count) {
        BYTE const *record = records + r * record_size;
        BYTE *dest = (BYTE *)out + r * out_stride;
        FOR_LOOP(f, schema_count) {
            stbDbcField_t const *s = &schema[f];
            DWORD byte_offset = s->column * sizeof(DWORD);
            if (byte_offset + sizeof(DWORD) > record_size) continue;
            if (s->type == STB_DBC_U32)
                *(DWORD *)(dest + s->offset) = Stb_DbcRead32(record + byte_offset);
            else
                *(LPCSTR *)(dest + s->offset) = Stb_DbcString(strings, string_size, Stb_DbcRead32(record + byte_offset));
        }
    }
}

#endif /* stb_dbc_h */
