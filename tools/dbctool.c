/*
 * dbctool — Read, inspect, and write WoW DBC (WDBC) files.
 *
 * Read commands:
 *   info          Print header fields.
 *   dump          Print records as tab-separated uint32 values.
 *   get  r f      Print field f of row r as uint32.
 *   str  r f      Print field f of row r as string.
 *
 * Write commands:
 *   create <out.dbc> <fields> <record_size>
 *                 Create an empty DBC file.
 *   set <file.dbc> <row> <field> <value>
 *                 Set a uint32 field on an existing DBC loaded in memory.
 *   setstr <file.dbc> <row> <field> <string>
 *                 Set a string field (value stored in string block).
 *   save <file.dbc>
 *                 Write the in-memory DBC to disk.
 *
 * Combined workflow:
 *   dbctool create test.dbc 3 12
 *   dbctool set test.dbc 0 0 1
 *   dbctool setstr test.dbc 0 1 "Hello"
 *   dbctool set test.dbc 0 2 42
 *   dbctool save test.dbc
 */
#include "tool_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define DBC_MAGIC 0x43424457u

typedef struct {
    uint32_t magic, records, fields, record_size, string_size;
} dbc_header_t;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

static BYTE *load_file(const char *path, size_t *size_out);

/* -------------------------------------------------------------------------
 * Dynamic string block
 * ---------------------------------------------------------------------- */

typedef struct {
    char  *buf;
    uint32_t size;
    uint32_t used;
} strblock_t;

static void sb_init(strblock_t *sb) {
    sb->buf  = malloc(4096);
    sb->size = 4096;
    sb->used = 1; /* offset 0 = empty string */
    sb->buf[0] = '\0';
}

static uint32_t sb_add(strblock_t *sb, const char *str) {
    if (!str || !*str) return 0;
    uint32_t len = (uint32_t)strlen(str) + 1;
    while (sb->used + len > sb->size) {
        sb->size *= 2;
        sb->buf = realloc(sb->buf, sb->size);
    }
    uint32_t off = sb->used;
    memcpy(sb->buf + sb->used, str, len);
    sb->used += len;
    return off;
}

/* -------------------------------------------------------------------------
 * In-memory DBC (for write/create/set/save workflow)
 * ---------------------------------------------------------------------- */

typedef struct {
    dbc_header_t hdr;
    BYTE        *records;  /* raw record bytes */
    strblock_t   strings;
    bool         dirty;
} dbc_mem_t;

static void dbc_mem_init(dbc_mem_t *m, uint32_t fields, uint32_t record_size) {
    memset(m, 0, sizeof(*m));
    m->hdr.magic       = DBC_MAGIC;
    m->hdr.fields      = fields;
    m->hdr.record_size = record_size;
    m->records         = NULL;
    sb_init(&m->strings);
    m->dirty = true;
}

static void dbc_mem_free(dbc_mem_t *m) {
    free(m->records);
    free(m->strings.buf);
    memset(m, 0, sizeof(*m));
}

static BYTE *dbc_mem_row(dbc_mem_t *m, uint32_t row) {
    if (row >= m->hdr.records) {
        uint32_t new_count = row + 1;
        m->records = realloc(m->records, (size_t)new_count * m->hdr.record_size);
        memset(m->records + m->hdr.records * m->hdr.record_size, 0,
               (size_t)(new_count - m->hdr.records) * m->hdr.record_size);
        m->hdr.records = new_count;
    }
    return m->records + row * m->hdr.record_size;
}

static void dbc_mem_set_u32(dbc_mem_t *m, uint32_t row, uint32_t field, uint32_t value) {
    BYTE *rec = dbc_mem_row(m, row);
    memcpy(rec + field * 4, &value, 4);
    m->dirty = true;
}

static void dbc_mem_set_str(dbc_mem_t *m, uint32_t row, uint32_t field, const char *value) {
    uint32_t off = sb_add(&m->strings, value);
    dbc_mem_set_u32(m, row, field, off);
}

static int dbc_mem_save(dbc_mem_t *m, const char *path) {
    /* Trim record buffer to actual count */
    BYTE *trimmed = realloc(m->records, (size_t)m->hdr.records * m->hdr.record_size);
    if (trimmed) m->records = trimmed;
    m->hdr.string_size = m->strings.used;

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Cannot write: %s\n", path); return 1; }
    fwrite(&m->hdr, 1, 20, f);
    fwrite(m->records, m->hdr.record_size, m->hdr.records, f);
    fwrite(m->strings.buf, 1, m->strings.used, f);
    fclose(f);
    printf("Wrote %u records, %u fields, %u bytes/record, %u string bytes → %s\n",
           m->hdr.records, m->hdr.fields, m->hdr.record_size, m->strings.used, path);
    return 0;
}

/* -------------------------------------------------------------------------
 * Persistent in-memory DBC cache (for set/setstr/save across invocations)
 * We use a temp file to persist state between dbctool calls.
 * ---------------------------------------------------------------------- */

static const char *dbc_cache_path(const char *target) {
    static char path[512];
    snprintf(path, sizeof(path), "/tmp/_dbctool_%s.dat", strrchr(target, '/') ? strrchr(target, '/') + 1 : target);
    return path;
}

static bool dbc_cache_save(const char *target, dbc_mem_t *m) {
    const char *path = dbc_cache_path(target);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    /* Update header with actual string block size */
    m->hdr.string_size = m->strings.used;
    fwrite(&m->hdr, 1, 20, f);
    if (m->hdr.records > 0 && m->records)
        fwrite(m->records, m->hdr.record_size, m->hdr.records, f);
    if (m->strings.used > 0 && m->strings.buf)
        fwrite(m->strings.buf, 1, m->strings.used, f);
    fclose(f);
    return true;
}

static bool dbc_cache_load(const char *target, dbc_mem_t *m) {
    const char *path = dbc_cache_path(target);
    size_t sz = 0;
    BYTE *data = load_file(path, &sz);
    if (!data) return false;
    if (sz < 20 || memcmp(data, "WDBC", 4)) { free(data); return false; }
    memcpy(&m->hdr, data, 20);
    size_t body = (size_t)m->hdr.records * m->hdr.record_size;
    if (20 + body + m->hdr.string_size > sz) { free(data); return false; }
    m->records = malloc(body);
    memcpy(m->records, data + 20, body);
    sb_init(&m->strings);
    /* Copy string block */
    free(m->strings.buf);
    m->strings.buf = malloc(m->hdr.string_size ? m->hdr.string_size : 1);
    m->strings.size = m->hdr.string_size ? m->hdr.string_size : 1;
    m->strings.used = m->hdr.string_size;
    memcpy(m->strings.buf, data + 20 + body, m->hdr.string_size);
    free(data);
    return true;
}

/* -------------------------------------------------------------------------
 * Read helpers
 * ---------------------------------------------------------------------- */

static uint32_t dbc_u32(const BYTE *rec, uint32_t field) {
    uint32_t v; memcpy(&v, rec + field * 4, 4); return v;
}

static const char *dbc_str(const BYTE *sb, uint32_t ssize, uint32_t off) {
    if (off == 0 || off >= ssize) return "";
    return (const char *)sb + off;
}

static bool dbc_parse(const BYTE *data, size_t data_size,
                      dbc_header_t *hdr_out,
                      const BYTE **rb_out, const BYTE **sb_out) {
    if (!data || data_size < 20) {
        fprintf(stderr, "DBC too small (%zu bytes)\n", data_size);
        return false;
    }
    dbc_header_t h;
    memcpy(&h, data, 20);
    if (h.magic != DBC_MAGIC) {
        fprintf(stderr, "Not a DBC file (magic 0x%08x)\n", h.magic);
        return false;
    }
    size_t body_size = (size_t)h.records * h.record_size;
    if (20 + body_size + h.string_size > data_size) {
        fprintf(stderr, "DBC truncated (need %zu, have %zu)\n",
                20 + body_size + h.string_size, data_size);
        return false;
    }
    *hdr_out = h;
    *rb_out  = data + 20;
    *sb_out  = data + 20 + body_size;
    return true;
}

/* -------------------------------------------------------------------------
 * File/MPQ loaders
 * ---------------------------------------------------------------------- */

static BYTE *load_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); fprintf(stderr, "Empty file: %s\n", path); return NULL; }
    BYTE *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); fprintf(stderr, "Out of memory\n"); return NULL; }
    if ((long)fread(buf, 1, (size_t)sz, f) != sz) {
        fclose(f); free(buf); fprintf(stderr, "Read error: %s\n", path); return NULL;
    }
    fclose(f);
    *size_out = (size_t)sz;
    return buf;
}

static BYTE *load_mpq(HANDLE archive, const char *arc_path, size_t *size_out) {
    char path[512];
    strncpy(path, arc_path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    Tool_NormalizeSlashes(path, '\\');
    Tool_TrimEdgeSlashes(path);
    HANDLE file;
    if (!SFileOpenFileEx(archive, path, SFILE_OPEN_FROM_MPQ, &file)) {
        fprintf(stderr, "Cannot open in archive: %s\n", path);
        return NULL;
    }
    DWORD sz = SFileGetFileSize(file, NULL);
    BYTE *buf = malloc(sz ? sz : 1);
    if (!buf) { SFileCloseFile(file); fprintf(stderr, "Out of memory\n"); return NULL; }
    DWORD total = 0;
    while (total < sz) {
        DWORD got = 0;
        if (!SFileReadFile(file, buf + total, sz - total, &got, NULL) || got == 0) break;
        total += got;
    }
    SFileCloseFile(file);
    if (total != sz) { free(buf); fprintf(stderr, "Read error (got %u of %u)\n", (unsigned)total, (unsigned)sz); return NULL; }
    *size_out = sz;
    return buf;
}

/* -------------------------------------------------------------------------
 * Read commands
 * ---------------------------------------------------------------------- */

static int cmd_info(const dbc_header_t *h) {
    printf("records=%u\n", h->records);
    printf("fields=%u\n", h->fields);
    printf("record_size=%u\n", h->record_size);
    printf("string_block_size=%u\n", h->string_size);
    return 0;
}

static int cmd_dump(const dbc_header_t *h, const BYTE *rb, uint32_t max_rows) {
    uint32_t rows = (max_rows && max_rows < h->records) ? max_rows : h->records;
    uint32_t cols = h->record_size / 4;
    for (uint32_t r = 0; r < rows; r++) {
        const BYTE *rec = rb + r * h->record_size;
        for (uint32_t c = 0; c < cols; c++) {
            if (c) putchar('\t');
            printf("%u", dbc_u32(rec, c));
        }
        putchar('\n');
    }
    return 0;
}

static int cmd_get(const dbc_header_t *h, const BYTE *rb, uint32_t row, uint32_t field) {
    if (row >= h->records) { fprintf(stderr, "Row %u out of range (%u)\n", row, h->records); return 1; }
    uint32_t cols = h->record_size / 4;
    if (field >= cols) { fprintf(stderr, "Field %u out of range (%u)\n", field, cols); return 1; }
    printf("%u\n", dbc_u32(rb + row * h->record_size, field));
    return 0;
}

static int cmd_str(const dbc_header_t *h, const BYTE *rb, const BYTE *sb,
                   uint32_t row, uint32_t field) {
    if (row >= h->records) { fprintf(stderr, "Row %u out of range (%u)\n", row, h->records); return 1; }
    uint32_t cols = h->record_size / 4;
    if (field >= cols) { fprintf(stderr, "Field %u out of range (%u)\n", field, cols); return 1; }
    uint32_t off = dbc_u32(rb + row * h->record_size, field);
    printf("%s\n", dbc_str(sb, h->string_size, off));
    return 0;
}

/* -------------------------------------------------------------------------
 * Write commands
 * ---------------------------------------------------------------------- */

static void cmd_create_usage(void) {
    fprintf(stderr,
        "Usage: dbctool create <output.dbc> <fields> <record_size>\n"
        "  Creates an empty DBC with the given structure.\n"
        "  record_size must be a multiple of 4 (4 bytes per field).\n");
}

static int cmd_create(int argc, char **argv) {
    if (argc < 3) { cmd_create_usage(); return 1; }
    const char *path  = argv[0];
    uint32_t fields   = (uint32_t)strtoul(argv[1], NULL, 10);
    uint32_t rec_size = (uint32_t)strtoul(argv[2], NULL, 10);
    if (fields == 0 || rec_size == 0 || rec_size % 4 != 0 || rec_size / 4 < fields) {
        fprintf(stderr, "Invalid: fields=%u record_size=%u\n", fields, rec_size);
        return 1;
    }
    dbc_mem_t m;
    dbc_mem_init(&m, fields, rec_size);
    m.hdr.records = 0;
    /* Save both the file and the cache so set/setstr can find it */
    dbc_cache_save(path, &m);
    int rc = dbc_mem_save(&m, path);
    dbc_mem_free(&m);
    return rc;
}

static int cmd_set(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "Usage: dbctool set <file.dbc> <row> <field> <value>\n"); return 1; }
    const char *path = argv[0];
    uint32_t row   = (uint32_t)strtoul(argv[1], NULL, 10);
    uint32_t field = (uint32_t)strtoul(argv[2], NULL, 10);
    uint32_t value = (uint32_t)strtoul(argv[3], NULL, 10);
    dbc_mem_t m;
    if (!dbc_cache_load(path, &m)) {
        /* Try loading the actual DBC file */
        size_t sz = 0;
        BYTE *data = load_file(path, &sz);
        if (data && sz >= 20 && memcmp(data, "WDBC", 4) == 0) {
            dbc_header_t h; const BYTE *rb, *sb;
            if (dbc_parse(data, sz, &h, &rb, &sb)) {
                dbc_mem_init(&m, h.fields, h.record_size);
                m.hdr.records = h.records;
                size_t body = (size_t)h.records * h.record_size;
                free(m.records);
                m.records = malloc(body);
                memcpy(m.records, rb, body);
                free(m.strings.buf);
                m.strings.buf = malloc(h.string_size ? h.string_size : 1);
                m.strings.size = h.string_size ? h.string_size : 1;
                m.strings.used = h.string_size;
                memcpy(m.strings.buf, sb, h.string_size);
            }
            free(data);
        }
        if (!m.records) {
            fprintf(stderr, "Cannot load DBC: %s (run 'create' first)\n", path);
            return 1;
        }
    }
    if (field >= m.hdr.record_size / 4) {
        fprintf(stderr, "Field %u out of range (record has %u fields)\n", field, m.hdr.record_size / 4);
        dbc_mem_free(&m);
        return 1;
    }
    dbc_mem_set_u32(&m, row, field, value);
    dbc_cache_save(path, &m);
    dbc_mem_free(&m);
    return 0;
}

static int cmd_setstr(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "Usage: dbctool setstr <file.dbc> <row> <field> <string>\n"); return 1; }
    const char *path  = argv[0];
    uint32_t row      = (uint32_t)strtoul(argv[1], NULL, 10);
    uint32_t field    = (uint32_t)strtoul(argv[2], NULL, 10);
    const char *value = argv[3];
    dbc_mem_t m;
    if (!dbc_cache_load(path, &m)) {
        size_t sz = 0;
        BYTE *data = load_file(path, &sz);
        if (data && sz >= 20 && memcmp(data, "WDBC", 4) == 0) {
            dbc_header_t h; const BYTE *rb, *sb;
            if (dbc_parse(data, sz, &h, &rb, &sb)) {
                dbc_mem_init(&m, h.fields, h.record_size);
                m.hdr.records = h.records;
                size_t body = (size_t)h.records * h.record_size;
                free(m.records);
                m.records = malloc(body);
                memcpy(m.records, rb, body);
                free(m.strings.buf);
                m.strings.buf = malloc(h.string_size ? h.string_size : 1);
                m.strings.size = h.string_size ? h.string_size : 1;
                m.strings.used = h.string_size;
                memcpy(m.strings.buf, sb, h.string_size);
            }
            free(data);
        }
        if (!m.records) {
            fprintf(stderr, "Cannot load DBC: %s (run 'create' first)\n", path);
            return 1;
        }
    }
    if (field >= m.hdr.record_size / 4) {
        fprintf(stderr, "Field %u out of range\n", field);
        dbc_mem_free(&m);
        return 1;
    }
    dbc_mem_set_str(&m, row, field, value);
    dbc_cache_save(path, &m);
    dbc_mem_free(&m);
    return 0;
}

static int cmd_save(int argc, char **argv) {
    if (argc < 1) { fprintf(stderr, "Usage: dbctool save <file.dbc>\n"); return 1; }
    const char *path = argv[0];
    dbc_mem_t m;
    if (!dbc_cache_load(path, &m)) {
        fprintf(stderr, "No cached DBC to save\n");
        return 1;
    }
    int rc = dbc_mem_save(&m, path);
    dbc_mem_free(&m);
    remove(dbc_cache_path(path));
    return rc;
}

/* -------------------------------------------------------------------------
 * Usage
 * ---------------------------------------------------------------------- */

static void usage(void) {
    fprintf(stderr,
        "Usage:\n"
        "Read commands (require -mpq or -file):\n"
        "  dbctool -mpq <archive.mpq> info  <DBFilesClient\\File.dbc>\n"
        "  dbctool -mpq <archive.mpq> dump  <DBFilesClient\\File.dbc> [max-rows]\n"
        "  dbctool -mpq <archive.mpq> get   <DBFilesClient\\File.dbc> <row> <field>\n"
        "  dbctool -mpq <archive.mpq> str   <DBFilesClient\\File.dbc> <row> <field>\n"
        "  dbctool -file <file.dbc>   info\n"
        "  dbctool -file <file.dbc>   dump [max-rows]\n"
        "  dbctool -file <file.dbc>   get  <row> <field>\n"
        "  dbctool -file <file.dbc>   str  <row> <field>\n"
        "\n"
        "Write commands (no -mpq/-file prefix):\n"
        "  dbctool create <out.dbc> <fields> <record_size>\n"
        "  dbctool set    <file.dbc> <row> <field> <uint32_value>\n"
        "  dbctool setstr <file.dbc> <row> <field> <string>\n"
        "  dbctool save   <file.dbc>\n"
        "\n"
        "Examples:\n"
        "  dbctool -mpq data/world-of-warcraft/dbc.MPQ info DBFilesClient\\\\CharSections.dbc\n"
        "  dbctool -file /tmp/CharSections.dbc dump\n"
        "\n"
        "  # Create a 2-field DBC with 1 record:\n"
        "  dbctool create /tmp/test.dbc 2 8\n"
        "  dbctool set /tmp/test.dbc 0 0 42\n"
        "  dbctool setstr /tmp/test.dbc 0 1 \"Hello World\"\n"
        "  dbctool save /tmp/test.dbc\n"
        "  dbctool -file /tmp/test.dbc dump\n");
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

int main(int argc, char **argv) {
    const char *mpq_path  = NULL;
    const char *file_path = NULL;
    const char *dbc_path  = NULL;
    const char *cmd       = NULL;
    int         argbase   = 0;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-mpq") == 0 && i + 1 < argc) {
            mpq_path = argv[++i];
        } else if (strcmp(argv[i], "-file") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else {
            argbase = i;
            break;
        }
        i++;
    }

    /* Write commands (no -mpq/-file prefix) */
    if (!mpq_path && !file_path && argbase < argc) {
        cmd = argv[argbase];
        argbase++;
        if (strcmp(cmd, "create") == 0) {
            return cmd_create(argc - argbase, argv + argbase);
        } else if (strcmp(cmd, "set") == 0) {
            return cmd_set(argc - argbase, argv + argbase);
        } else if (strcmp(cmd, "setstr") == 0) {
            return cmd_setstr(argc - argbase, argv + argbase);
        } else if (strcmp(cmd, "save") == 0) {
            return cmd_save(argc - argbase, argv + argbase);
        }
        /* Fall through to read commands if not a write command */
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage();
        return 1;
    }

    if (!mpq_path && !file_path) { usage(); return 1; }

    /* Read commands */
    if (mpq_path) {
        if (argbase + 2 > argc) { usage(); return 1; }
        cmd      = argv[argbase];
        dbc_path = argv[argbase + 1];
        argbase += 2;
    } else {
        if (argbase + 1 > argc) { usage(); return 1; }
        cmd     = argv[argbase];
        argbase += 1;
    }

    BYTE  *data = NULL;
    size_t data_size = 0;
    if (mpq_path) {
        HANDLE archive;
        if (!SFileOpenArchive(mpq_path, 0, 0, &archive)) {
            fprintf(stderr, "Cannot open archive: %s\n", mpq_path);
            return 1;
        }
        data = load_mpq(archive, dbc_path, &data_size);
        SFileCloseArchive(archive);
    } else {
        data = load_file(file_path, &data_size);
    }
    if (!data) return 1;

    dbc_header_t  hdr;
    const BYTE   *rb, *sb;
    if (!dbc_parse(data, data_size, &hdr, &rb, &sb)) { free(data); return 1; }

    int rc = 0;
    if (strcmp(cmd, "info") == 0) {
        rc = cmd_info(&hdr);
    } else if (strcmp(cmd, "dump") == 0) {
        uint32_t max_rows = 0;
        if (argbase < argc) {
            char *end = NULL;
            unsigned long v = strtoul(argv[argbase], &end, 10);
            if (!end || *end != '\0') { fprintf(stderr, "Invalid max-rows: %s\n", argv[argbase]); free(data); return 1; }
            max_rows = (uint32_t)v;
        }
        rc = cmd_dump(&hdr, rb, max_rows);
    } else if (strcmp(cmd, "get") == 0) {
        if (argbase + 2 > argc) { usage(); free(data); return 1; }
        uint32_t row   = (uint32_t)strtoul(argv[argbase],     NULL, 10);
        uint32_t field = (uint32_t)strtoul(argv[argbase + 1], NULL, 10);
        rc = cmd_get(&hdr, rb, row, field);
    } else if (strcmp(cmd, "str") == 0) {
        if (argbase + 2 > argc) { usage(); free(data); return 1; }
        uint32_t row   = (uint32_t)strtoul(argv[argbase],     NULL, 10);
        uint32_t field = (uint32_t)strtoul(argv[argbase + 1], NULL, 10);
        rc = cmd_str(&hdr, rb, sb, row, field);
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage();
        rc = 1;
    }

    free(data);
    return rc;
}
