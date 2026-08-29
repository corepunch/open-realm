/*
 * stb_dbc.h — Single-header reader for classic-era WoW client databases (WDBC).
 *
 * UI, game, renderer, common, and sound all read DBC files, and each used to
 * reimplement the same little-endian reads, "WDBC" header validation,
 * string-block access, and lazy cache. This header is the shared subset.
 *
 * Everything is `static inline` so the header can be included from any
 * translation unit (ui, game, renderer, common, sound) without a separate
 * implementation file and without `-Wunused-function` noise in targets that
 * only use part of the API.
 *
 * Two layers:
 *
 * 1. A stateless, pure memory-buffer parser (`Stb_Dbc*`): no file I/O and no
 *    allocation. Callers read the file with their own FS/RI/gi handle and hand
 *    the resident buffer here.
 *
 * 2. A stateful cache (`Stb_DbcCache*`) for the load + decode + FNV-1a index
 *    pattern: one resident file image, one decoded struct array filled by a
 *    column→field schema table, and an optional integer hash index for hot
 *    lookups. The cache takes a tiny `stbDbcIO_t` function table so the header
 *    stays free of direct FS/allocator dependencies; each module adapts its own
 *    handle (ri / gi / uiimport / common FS). Module files keep only their
 *    structs and schemas (the "mapping") and thin typed finders on top.
 *
 * Typical use (stateless):
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
/* "WDBC" read back as a little-endian 32-bit word (see ID_WDBC in common/shared.h). */
typedef struct {
    DWORD records;
    DWORD fields;
    DWORD record_size;
    DWORD string_size;
} stbDbc_t;

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
    if (!data || !header || size <= 20 || *(DWORD const *)data != ID_WDBC) {
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
 * table (the `{ column, offsetof(struct, field), type }` pattern from
 * stb_fdf.h). Each entry maps one or more DBC columns to struct fields; the
 * optional `count` fills a contiguous struct array from consecutive columns, so
 * `{ 14, offsetof(Rec, texture), STB_DBC_STR, 8 }` decodes texture[0..7] from
 * columns 14..21 without eight lines. The parser performs no I/O and no
 * allocation; callers read the file and keep the buffer (and its string block)
 * alive for the lifetime of the decoded rows.
 *
 * This schema pattern is the required way to read a DBC — the recipe, the
 * per-version dispatch convention, and the anti-patterns it replaces are
 * documented in docs/games/world-of-warcraft/dbc-reference.md
 * ("Reading A DBC — The Schema Pattern"). */
#define STB_DBC_U32   BZ_FIELD_U32   /* 4-byte little-endian int column */
#define STB_DBC_STR   BZ_FIELD_CSTR  /* 4-byte string-block offset, resolved to a pointer */
#define STB_DBC_FLOAT BZ_FIELD_FLOAT /* 4-byte little-endian float column */

typedef struct {
    DWORD column;       /* first DBC column index (0-based); byte offset = column * 4 */
    ptrdiff_t offset;   /* offsetof(Rec, field); for an array, its base element */
    bzFieldType_t type;
    DWORD count;        /* consecutive columns mapped to consecutive array elements (0 = scalar) */
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
            DWORD n = s->count ? s->count : 1;
            DWORD esize = s->type == STB_DBC_U32 ? sizeof(DWORD) :
                          s->type == STB_DBC_FLOAT ? sizeof(FLOAT) :
                          s->type == STB_DBC_STR ? sizeof(LPCSTR) : 0;
            if (!esize) {
                if (!r) fprintf(stderr, "Stb_DbcParseRows: unsupported field type %d at schema %u\n", s->type, (unsigned)f);
                continue;
            }
            FOR_LOOP(e, n) {
                DWORD byte_offset = (s->column + e) * sizeof(DWORD);
                if (byte_offset + sizeof(DWORD) > record_size) break;
                if (s->type == STB_DBC_U32)
                    *(DWORD *)(dest + s->offset + e * esize) = Stb_DbcRead32(record + byte_offset);
                else if (s->type == STB_DBC_FLOAT)
                    *(FLOAT *)(dest + s->offset + e * esize) = Stb_DbcReadFloat(record + byte_offset);
                else if (s->type == STB_DBC_STR)
                    *(LPCSTR *)(dest + s->offset + e * esize) =
                        Stb_DbcString(strings, string_size, Stb_DbcRead32(record + byte_offset));
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Stateful cache                                                              */
/* -------------------------------------------------------------------------- */
/* Lazy load + decode + FNV-1a index over the stateless parser. Callers supply
 * an I/O table so this stays free of direct FS/allocator dependencies. Rows
 * decode into one alloc'd struct array; string fields keep pointing into the
 * resident file image, so keep the cache alive as long as rows are read. */

typedef struct {
    void *(*read)(LPCSTR filename, DWORD *size); /* resident buffer or NULL */
    void  (*free)(void *buffer);                 /* release a read() buffer */
    void *(*alloc)(size_t bytes);                /* row-array / index allocator */
    void  (*dealloc)(void *mem);                 /* release alloc() memory */
} stbDbcIO_t;

typedef struct {
    LPBYTE data;
    DWORD size, records, fields, record_size, string_size;
    BYTE const *records_base, *strings_base;
    int *index;
    DWORD index_capacity, index_field;
    void *rows;       /* decoded struct array (Stb_DbcParseRows output) */
    DWORD row_stride;
    BOOL tried, valid;
} stbDbcCache_t;

/* Typed accessor for a decoded row; `cache` is the stbDbcCache_t lvalue. */
#define STB_DBC_ROW(cache, T, idx) \
    ((T const *)((BYTE const *)(cache).rows + (size_t)(idx) * (cache).row_stride))

static inline BOOL Stb_DbcCacheLoad(stbDbcCache_t *c, LPCSTR filename, stbDbcIO_t const *io) {
    stbDbc_t h;
    DWORD size = 0;
    void *data;
    if (!c || !filename || !io || !io->read) return false;
    if (c->tried) return c->valid;
    c->tried = true;
    data = io->read(filename, &size);
    if (!data || size <= 20 || !Stb_DbcValid((BYTE const *)data, size, &h)) {
        if (data && io->free) io->free(data);
        return false;
    }
    c->data = data; c->size = size;
    c->records = h.records; c->fields = h.fields;
    c->record_size = h.record_size; c->string_size = h.string_size;
    c->records_base = Stb_DbcRecords((BYTE const *)data);
    c->strings_base = Stb_DbcStrings((BYTE const *)data, &h);
    c->valid = true;
    return true;
}

static inline BOOL Stb_DbcCacheDecode(stbDbcCache_t *c, stbDbcField_t const *schema, DWORD schema_count,
                                      DWORD row_stride, stbDbcIO_t const *io) {
    if (!c || !c->valid || c->rows) return c->rows != NULL;
    if (!io || !io->alloc) return false;
    c->rows = io->alloc((size_t)c->records * row_stride);
    if (!c->rows) {
        fprintf(stderr, "stb_dbc: unable to allocate %u decoded rows (%u-byte records)\n", c->records, row_stride);
        return false;
    }
    memset(c->rows, 0, (size_t)c->records * row_stride);
    Stb_DbcParseRows(c->records_base, c->records, c->record_size, c->strings_base, c->string_size,
                     schema, schema_count, c->rows, row_stride);
    c->row_stride = row_stride;
    return true;
}

static inline void Stb_DbcCacheFree(stbDbcCache_t *c, stbDbcIO_t const *io) {
    if (!c) return;
    if (io) {
        if (c->data && io->free) io->free(c->data);
        if (c->index && io->dealloc) io->dealloc(c->index);
        if (c->rows && io->dealloc) io->dealloc(c->rows);
    }
    memset(c, 0, sizeof(*c));
}

/* Raw field access is only used to build/query the FNV index; decoded consumers
 * read struct fields filled by Stb_DbcParseRows. */
static inline DWORD Stb_DbcCacheField(stbDbcCache_t const *c, BYTE const *record, DWORD field) {
    if (!c || !record || field >= c->fields || field * sizeof(DWORD) + sizeof(DWORD) > c->record_size)
        return 0;
    return Stb_DbcRead32(record + field * sizeof(DWORD));
}

static inline DWORD Stb_DbcFnv1a32(DWORD key) {
    DWORD hash = 2166136261u;
    FOR_LOOP(i, sizeof(key)) { hash ^= (key >> (i * 8)) & 0xffu; hash *= 16777619u; }
    return hash;
}

static inline BOOL Stb_DbcCacheBuildIndex(stbDbcCache_t *c, DWORD field, stbDbcIO_t const *io) {
    DWORD capacity = 1;
    int *index;
    if (!c || !c->valid || field >= c->fields || !io || !io->alloc) return false;
    while (capacity < c->records * 2u) capacity <<= 1;
    index = io->alloc(capacity * sizeof(*index));
    if (!index) {
        fprintf(stderr, "stb_dbc: unable to allocate field %u index (%u entries)\n", field, c->records);
        return false;
    }
    FOR_LOOP(i, capacity) index[i] = -1;
    FOR_LOOP(i, c->records) {
        BYTE const *record = c->records_base + i * c->record_size;
        DWORD key = Stb_DbcCacheField(c, record, field);
        DWORD slot = Stb_DbcFnv1a32(key) & (capacity - 1);
        while (index[slot] >= 0 && Stb_DbcCacheField(c, c->records_base + index[slot] * c->record_size, field) != key)
            slot = (slot + 1) & (capacity - 1);
        index[slot] = (int)i;
    }
    c->index = index; c->index_capacity = capacity; c->index_field = field;
    return true;
}

/* Return the row index for key (or -1); the caller must have loaded the DBC. */
static inline int Stb_DbcCacheFindKey(stbDbcCache_t *c, DWORD field, DWORD key, stbDbcIO_t const *io) {
    DWORD slot;
    if (!c || !c->valid || field >= c->fields) return -1;
    if (!c->index && !Stb_DbcCacheBuildIndex(c, field, io)) return -1;
    if (c->index_field != field) return -1;
    slot = Stb_DbcFnv1a32(key) & (c->index_capacity - 1);
    while (c->index[slot] >= 0) {
        BYTE const *record = c->records_base + c->index[slot] * c->record_size;
        if (Stb_DbcCacheField(c, record, field) == key) return c->index[slot];
        slot = (slot + 1) & (c->index_capacity - 1);
    }
    return -1;
}

static inline int Stb_DbcCacheFindID(stbDbcCache_t *c, DWORD id, stbDbcIO_t const *io) {
    return Stb_DbcCacheFindKey(c, 0, id, io);
}

/* Fill NULL string pointers with "" (some consumers index [0] directly). */
static inline void Stb_DbcNormalizeStrings(void *rows, DWORD count, DWORD stride,
                                           stbDbcField_t const *schema, DWORD schema_count) {
    FOR_LOOP(f, schema_count) if (schema[f].type == STB_DBC_STR) {
        DWORD n = schema[f].count ? schema[f].count : 1;
        FOR_LOOP(e, n) FOR_LOOP(i, count) {
            LPCSTR *p = (LPCSTR *)((BYTE *)rows + i * stride + schema[f].offset + e * sizeof(LPCSTR));
            if (!*p) *p = "";
        }
    }
}

#endif /* stb_dbc_h */
