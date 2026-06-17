#include "tool_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * DBC header: magic 'WDBC', then records/fields/record_size/string_block_size
 * ---------------------------------------------------------------------- */

#define DBC_MAGIC 0x43424457u /* 'WDBC' little-endian */

typedef struct {
    uint32_t magic, records, fields, record_size, string_size;
} dbc_header_t;

static void usage(void) {
    fprintf(stderr,
        "Usage:\n"
        "  dbctool -mpq <archive.mpq> info <DBFilesClient\\File.dbc>\n"
        "  dbctool -mpq <archive.mpq> dump <DBFilesClient\\File.dbc> [max-rows]\n"
        "  dbctool -mpq <archive.mpq> get  <DBFilesClient\\File.dbc> <row> <field>\n"
        "  dbctool -mpq <archive.mpq> str  <DBFilesClient\\File.dbc> <row> <field>\n"
        "  dbctool -file <file.dbc>   info\n"
        "  dbctool -file <file.dbc>   dump [max-rows]\n"
        "  dbctool -file <file.dbc>   get  <row> <field>\n"
        "  dbctool -file <file.dbc>   str  <row> <field>\n"
        "\n"
        "Commands:\n"
        "  info          Print header: record count, field count, record size, string block size.\n"
        "  dump          Print all records as tab-separated uint32 fields, one row per line.\n"
        "                Optional max-rows limits output.\n"
        "  get  r f      Print field f of row r (0-based) as uint32.\n"
        "  str  r f      Print field f of row r as a string (field is a string-block offset).\n"
        "\n"
        "Examples:\n"
        "  dbctool -mpq data/world-of-warcraft/Data/patch.mpq info DBFilesClient\\\\ChrRaces.dbc\n"
        "  dbctool -mpq data/world-of-warcraft/Data/patch.mpq dump DBFilesClient\\\\ChrClasses.dbc 10\n"
        "  dbctool -mpq data/world-of-warcraft/Data/patch.mpq get  DBFilesClient\\\\ChrRaces.dbc 0 0\n"
        "  dbctool -mpq data/world-of-warcraft/Data/patch.mpq str  DBFilesClient\\\\ChrRaces.dbc 0 17\n"
        "  dbctool -file /tmp/ChrRaces.dbc dump\n");
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

/* -------------------------------------------------------------------------
 * Load DBC from a raw byte buffer; does not take ownership.
 * ---------------------------------------------------------------------- */

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
 * Load from filesystem path
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
        fclose(f); free(buf);
        fprintf(stderr, "Read error: %s\n", path);
        return NULL;
    }
    fclose(f);
    *size_out = (size_t)sz;
    return buf;
}

/* -------------------------------------------------------------------------
 * Load from MPQ archive
 * ---------------------------------------------------------------------- */

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
    if (total != sz) {
        free(buf);
        fprintf(stderr, "Read error for %s (got %u of %u)\n", path, (unsigned)total, (unsigned)sz);
        return NULL;
    }
    *size_out = sz;
    return buf;
}

/* -------------------------------------------------------------------------
 * Commands
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
    if (row >= h->records) { fprintf(stderr, "Row %u out of range (%u records)\n", row, h->records); return 1; }
    uint32_t cols = h->record_size / 4;
    if (field >= cols) { fprintf(stderr, "Field %u out of range (%u fields in record)\n", field, cols); return 1; }
    printf("%u\n", dbc_u32(rb + row * h->record_size, field));
    return 0;
}

static int cmd_str(const dbc_header_t *h, const BYTE *rb, const BYTE *sb,
                   uint32_t row, uint32_t field) {
    if (row >= h->records) { fprintf(stderr, "Row %u out of range (%u records)\n", row, h->records); return 1; }
    uint32_t cols = h->record_size / 4;
    if (field >= cols) { fprintf(stderr, "Field %u out of range (%u fields in record)\n", field, cols); return 1; }
    uint32_t off = dbc_u32(rb + row * h->record_size, field);
    printf("%s\n", dbc_str(sb, h->string_size, off));
    return 0;
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

    /* Parse flags: -mpq <path> or -file <path> */
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

    if (!mpq_path && !file_path) { usage(); return 1; }

    /* For MPQ mode the DBC archive path comes before the command;
       for file mode the command comes first. */
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

    /* Load raw bytes */
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
