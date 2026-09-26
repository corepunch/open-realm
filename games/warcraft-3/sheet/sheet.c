#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

#include "common/common.h"
#include "common/stb_slk.h"

#define MAX_INI_LINE 1024
#define MAX_SHEET_COLUMNS 256

typedef struct SheetCell {
    string_t text;
    uint16_t column;
    uint16_t row;
    sheet_t * next;
} sheetCell_t;

typedef struct sheet_field_s {
    cstring_t name, value;
    struct sheet_field_s *next;
} sheetField_t;

typedef struct sheet_row_s {
    cstring_t name;
    sheetField_t *fields;
    struct sheet_row_s *next;
} sheetRow_t;

typedef struct sheet_table_s {
    sheetRow_t *rows;
    sheetRow_t *tail;
    struct sheet_table_s *next;
    void *allocation;
} sheetTable_t;

// TODO: allocate these as needed, this is only PoC and will only work for 1 level
static sheetCell_t cells[1024 * 1024] = { 0 };
static sheetRow_t rows[1024 * 1024] = { 0 };
static sheetField_t fields[1024 * 1024] = { 0 };
static char text_buffer[8 * 1024 * 1024] = { 0 };
static string_t current_text = text_buffer;
static sheet_t * current_cell = cells;
static sheet_t * previous_cell = cells;
static sheetRow_t *current_row = rows;
static sheetField_t *current_field = fields;

static sheetTable_t *FS_MakeTable(sheetRow_t *rows, sheetRow_t *tail)
{
    sheetTable_t *table = (sheetTable_t *)malloc(sizeof(*table));

    if (!table) {
        fprintf(stderr, "Sheet: out of memory creating table handle\n");
        return NULL;
    }
    table->rows = rows;
    table->tail = tail;
    table->next = NULL;
    table->allocation = NULL;
    return table;
}

/* Scan one semicolon-delimited SLK record field from *p up to end.
 * For K fields, strips surrounding quotes and decodes "" → literal ".
 * Advances *p past the field and any trailing semicolon.
 * Returns false when no field remains. */
static bool ScanSLKField(cstring_t *p, cstring_t end, char *out, size_t cap) {
    char *dst = out, *dst_end = out + cap - 1;
    cstring_t s = *p;
    bool is_k;

    if (s >= end || !*s || *s == '\r' || *s == '\n') return false;

    is_k = (*s == 'K');
    if (dst < dst_end) *dst++ = *s++;

    if (is_k && s < end && *s == '"') {
        s++; /* skip opening quote */
        while (s < end && *s && *s != '\r' && *s != '\n') {
            if (*s == '"') {
                if (s + 1 < end && s[1] == '"') { if (dst < dst_end) *dst++ = '"'; s += 2; } /* "" → " */
                else { s++; break; } /* closing quote */
            } else { if (dst < dst_end) *dst++ = *s++; }
        }
    } else {
        while (s < end && *s && *s != ';' && *s != '\r' && *s != '\n') {
            if (dst < dst_end) *dst++ = *s++;
        }
    }
    *dst = '\0';
    if (s < end && *s == ';') s++;
    *p = s;
    return true;
}

//int text_size = 0;

static void FS_FillSheetCell(uint32_t x, uint32_t y, cstring_t text) {
    size_t len, remaining;

    if (!text) return;
    if (current_cell >= cells + sizeof(cells) / sizeof(cells[0])) {
        fprintf(stderr, "SLK: cell pool exhausted at row=%u col=%u\n", y, x);
        return;
    }
    len = strlen(text);
    remaining = (size_t)((text_buffer + sizeof(text_buffer)) - current_text);
    if (len + 1 > remaining) {
        fprintf(stderr, "SLK: text arena exhausted at row=%u col=%u\n", y, x);
        return;
    }
    current_cell->column = (uint16_t)x;
    current_cell->row = (uint16_t)y;
    current_cell->next = current_cell + 1;
    current_cell->text = current_text;
    memcpy(current_text, text, len);
    current_text[len] = '\0';
    current_text += len + 1;
    previous_cell = current_cell;
    current_cell++;
}

static sheetTable_t *FS_MakeRowsFromSheet(sheet_t * sheet) {
    cstring_t columns[256] = { 0 };
    sheetRow_t *start = NULL;
    sheetRow_t *last_row = NULL;
    sheetRow_t **rows_by_number = NULL;
    uint32_t num_rows = 0;

    FOR_EACH_LIST(sheet_t, cell, sheet) {
        if (cell->row == 1) {
            if (cell->column < MAX_SHEET_COLUMNS) {
                columns[cell->column] = cell->text;
            }
        }
        num_rows = MAX(num_rows, cell->row);
    }
    if (num_rows < 2) {
        return NULL;
    }

    rows_by_number = (sheetRow_t **)calloc(num_rows + 1, sizeof(sheetRow_t *));
    if (!rows_by_number) {
        return NULL;
    }

    FOR_EACH_LIST(sheet_t, cell, sheet) {
        sheetRow_t *row;

        if (cell->row <= 1 || cell->row > num_rows) {
            continue;
        }

        row = rows_by_number[cell->row];
        if (!row) {
            row = current_row++;
            memset(row, 0, sizeof(*row));
            rows_by_number[cell->row] = row;
        }

        if (cell->column == 1) {
            row->name = cell->text;
        } else if (cell->column < MAX_SHEET_COLUMNS && columns[cell->column]) {
            current_field->name = columns[cell->column];
            current_field->value = cell->text;
            ADD_TO_LIST(current_field, row->fields);
            current_field++;
        }
    }

    FOR_LOOP(row_num, num_rows + 1) {
        sheetRow_t *row = rows_by_number[row_num];

        if (!row || !row->name) {
            continue;
        }

        if (!start) {
            start = row;
        } else {
            last_row->next = row;
        }
        last_row = row;
    }

    if (last_row) {
        last_row->next = NULL;
    }

    free(rows_by_number);
    return FS_MakeTable(start, last_row);
}

static sheetTable_t *FS_ParseSLK_Buffer(cstring_t buffer)
{
    sheet_t * start = current_cell;
    uint32_t X = 1, Y = 1;
    char field[MAX_SHEET_LINE];

    if (!buffer) return NULL;

    while (*buffer) {
        cstring_t line_start = buffer, line_end;
        char rectype;
        cstring_t p;

        while (*buffer && *buffer != '\n' && *buffer != '\r') buffer++;
        line_end = buffer;
        while (*buffer == '\r' || *buffer == '\n') buffer++;

        if (line_start == line_end) continue;

        rectype = line_start[0];
        if (rectype != 'C' && rectype != 'F') continue;

        p = line_start + 1;
        if (p < line_end && *p == ';') p++;

        while (ScanSLKField(&p, line_end, field, sizeof(field))) {
            switch (field[0]) {
            case 'X': X = (uint32_t)atoi(field + 1); break;
            case 'Y': Y = (uint32_t)atoi(field + 1); break;
            case 'K':
                if (rectype == 'C' && X >= 1 && Y >= 1)
                    FS_FillSheetCell(X, Y, field + 1);
                break;
            }
        }
    }

    if (start != current_cell) {
        previous_cell->next = NULL;
        return FS_MakeRowsFromSheet(start);
    }
    return NULL;
}


static sheetTable_t *FS_ParseSLK(cstring_t fileName) {
    string_t buffer = FS_ReadFileIntoString(fileName);
    sheetTable_t *sheet;
    if (!buffer) return NULL;
    sheet = FS_ParseSLK_Buffer(buffer);
    FS_FreeFileString(buffer);
    return sheet;
}

static cstring_t FS_FindSheetCell(sheetTable_t const *sheet, cstring_t row, cstring_t column) {
    for (; sheet; sheet = sheet->next) {
        FOR_EACH_LIST(sheetRow_t const, srow, sheet->rows) {
            if (strcmp(srow->name, row))
                continue;
            FOR_EACH_LIST(sheetField_t const, scolumn, srow->fields) {
                if (strcasecmp(scolumn->name, column))
                    continue;
                return scolumn->value;
            }
        }
    }
    return NULL;
}

static sheetTable_t *FS_ParseINI_Buffer(cstring_t buffer) {
    size_t size, lines = 1, row_capacity = 0, row_count = 0, field_count = 0;
    size_t text_capacity, text_overhead, allocation_size;
    sheetTable_t *table;
    sheetRow_t *rows, *section = NULL, *last_row = NULL;
    sheetField_t *fields;
    char *text;
    cstring_t p, end;

    if (!buffer) return NULL;
    size = strlen(buffer);
    for (size_t i = 0; i < size; i++) {
        if (buffer[i] == '\r') {
            lines++;
            if (i + 1 < size && buffer[i + 1] == '\n') i++;
        } else if (buffer[i] == '\n') lines++;
        if (buffer[i] == '[') row_capacity++;
    }
    if (row_capacity == SIZE_MAX || lines > (SIZE_MAX - row_capacity - 1) / 2) return NULL;
    text_overhead = row_capacity + lines * 2 + 1;
    if (size > SIZE_MAX - text_overhead) return NULL;
    text_capacity = size + text_overhead;
    if (lines > (SIZE_MAX - sizeof(*table)) / sizeof(*fields)) return NULL;
    allocation_size = sizeof(*table) + lines * sizeof(*fields);
    if (row_capacity > (SIZE_MAX - allocation_size) / sizeof(*rows)) return NULL;
    allocation_size += row_capacity * sizeof(*rows);
    if (text_capacity > SIZE_MAX - allocation_size) return NULL;
    allocation_size += text_capacity;
    table = calloc(1, allocation_size);
    if (!table) return NULL;
    rows = (sheetRow_t *)(table + 1);
    fields = (sheetField_t *)(rows + row_capacity);
    text = (char *)(fields + lines);
    table->allocation = table;
    table->rows = rows;

    p = buffer; end = buffer + size;
    while (p < end) {
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end) break;
        if (p[0] == '/' && p + 1 < end && p[1] == '/') {
            while (p < end && *p != '\n' && *p != '\r') p++;
        } else if (*p == '[') {
            cstring_t name_start, name_end;
            p++; name_start = p;
            while (p < end && *p != ']' && *p != '\n' && *p != '\r') p++;
            name_end = p;
            section = &rows[row_count++];
            if (last_row) last_row->next = section;
            else table->rows = section;
            last_row = section; table->tail = section;
            section->name = text;
            memcpy(text, name_start, (size_t)(name_end - name_start));
            text[name_end - name_start] = '\0'; text += (name_end - name_start) + 1;
            if (p < end && *p == ']') p++;
        } else {
            cstring_t line_start = p, line_end, eq;
            while (p < end && *p != '\n' && *p != '\r') p++;
            line_end = p;
            eq = memchr(line_start, '=', (size_t)(line_end - line_start));
            if (eq && section) {
                cstring_t key_end = eq, value_start = eq + 1;
                sheetField_t *field = &fields[field_count++];
                while (key_end > line_start && key_end[-1] == ' ') key_end--;
                while (value_start < line_end && *value_start == ' ') value_start++;
                field->name = text;
                memcpy(text, line_start, (size_t)(key_end - line_start));
                text[key_end - line_start] = '\0'; text += (key_end - line_start) + 1;
                field->value = text;
                memcpy(text, value_start, (size_t)(line_end - value_start));
                text[line_end - value_start] = '\0'; text += (line_end - value_start) + 1;
                /* Preserve the original parser's prepend order: lookup returns
                 * the first key, so the last authored assignment wins. */
                field->next = section->fields;
                section->fields = field;
            }
        }
    }
    return row_count ? table : (free(table), NULL);
}

static sheetTable_t *FS_ParseINI(cstring_t fileName) {
    string_t buffer = FS_ReadFileIntoString(fileName);
    sheetTable_t *config;
    if (!buffer) return NULL;
    config = FS_ParseINI_Buffer(buffer);
    if (!config) fprintf(stderr, "Failed to parse %s\n", fileName);
    FS_FreeFileString(buffer);
    return config;
}

static void FS_AppendSheetTable(sheetTable_t **head, sheetTable_t **tail, sheetTable_t *sheet)
{
    if (!sheet)
        return;
    if (*tail)
        (*tail)->next = sheet;
    else
        *head = sheet;
    *tail = sheet;
    while ((*tail)->next)
        *tail = (*tail)->next;
}

static void SheetSetTypedField(uint8_t *dst, bzFieldType_t type, cstring_t value, cstring_t field_name)
{
    switch (type) {
    case BZ_FIELD_U32: *(uint32_t *)dst = value ? (uint32_t)atoi(value) : 0; break;
    case BZ_FIELD_FLOAT: *(float *)dst = value ? (float)atof(value) : 0.f; break;
    case BZ_FIELD_BOOL: *(bool *)dst = value && (atoi(value) != 0 || !strcasecmp(value, "true")); break;
    case BZ_FIELD_CSTR: {
        size_t len = value ? strlen(value) : 0;
        string_t str = (string_t)malloc(len + 1);
        if (!str) { fprintf(stderr, "SLK: out of memory copying field '%s'\n", field_name ? field_name : ""); break; }
        memcpy(str, value ? value : "", len + 1);
        free(*(void **)dst);
        *(string_t *)dst = str;
        break;
    }
    case BZ_FIELD_FOURCC: {
        uint32_t key = 0;
        if (value) {
            size_t n = strlen(value);
            memcpy(&key, value, n < 4 ? n : 4);
        }
        *(uint32_t *)dst = key;
        break;
    }
    default: break;
    }
}

static void *FS_LoadSheetTyped(sheetTable_t const *sheet, slkField_t const *schema, size_t row_size, uint32_t *count_out)
{
    uint32_t capacity = 0, out_count = 0;
    cstring_t *seen_names;
    uint8_t *rows_out;

    if (count_out)
        *count_out = 0;
    if (!sheet || !schema || !row_size)
        return NULL;

    for (sheetTable_t const *table = sheet; table; table = table->next)
        FOR_EACH_LIST(sheetRow_t const, row, table->rows) if (row->name && row->name[0]) capacity++;
    if (!capacity)
        return NULL;

    seen_names = (cstring_t *)calloc(capacity, sizeof(*seen_names));
    rows_out = (uint8_t *)calloc(capacity, row_size);
    if (!seen_names || !rows_out) {
        fprintf(stderr, "SLK: out of memory allocating %u decoded rows\n", capacity);
        free(seen_names);
        free(rows_out);
        return NULL;
    }

    for (sheetTable_t const *table = sheet; table; table = table->next) {
        FOR_EACH_LIST(sheetRow_t const, row, table->rows) {
            uint8_t *dst;
            bool seen = false;

            if (!row->name || !row->name[0])
                continue;
            FOR_LOOP(i, out_count) {
                if (!strcmp(seen_names[i], row->name)) {
                    seen = true;
                    break;
                }
            }
            if (seen)
                continue;

            seen_names[out_count] = row->name;
            dst = rows_out + out_count * row_size;

            for (slkField_t const *field = schema; field->column; field++) {
                cstring_t value;
                uint8_t *field_dst = dst + field->offset;

                if (!field->column[0]) {
                    SheetSetTypedField(field_dst, field->type, row->name, field->column);
                    continue;
                }
                value = FS_FindSheetCell(sheet, row->name, field->column);
                if (!value) value = field->default_value;
                if (!value)
                    continue;
                SheetSetTypedField(field_dst, field->type, value, field->column);
            }
            out_count++;
        }
    }

    free(seen_names);
    if (!out_count) {
        free(rows_out);
        return NULL;
    }
    if (count_out)
        *count_out = out_count;
    return rows_out;
}

/* Typed SLK rows are fully malloc-owned after FS_LoadSheetTyped — the static
 * pools are only scratch space during parsing.  Reset cursors so they can be
 * reused by the next load without exhausting the fixed-size arenas. */
#define SHEET_POOL_SAVE() \
    sheet_t *     _saved_cell  = current_cell;  \
    sheet_t *     _saved_prev  = previous_cell; \
    sheetRow_t *_saved_row   = current_row;   \
    sheetField_t *_saved_fld = current_field; \
    string_t       _saved_text  = current_text

#define SHEET_POOL_RESTORE() \
    current_cell  = _saved_cell;  \
    previous_cell = _saved_prev;  \
    current_row   = _saved_row;   \
    current_field = _saved_fld;   \
    current_text  = _saved_text

uint32_t Stb_SlkLoad(cstring_t filename, slkField_t const *schema, void **dest, uint32_t row_stride) {
    SHEET_POOL_SAVE();
    uint32_t count = 0;
    sheetTable_t *sheet = FS_ParseSLK(filename);
    if (sheet && dest && schema && row_stride)
        *dest = FS_LoadSheetTyped(sheet, schema, row_stride, &count);
    SHEET_POOL_RESTORE();
    return count;
}

uint32_t Stb_SlkLoadBuffer(cstring_t buffer, slkField_t const *schema, void **dest, uint32_t row_stride) {
    SHEET_POOL_SAVE();
    uint32_t count = 0;
    sheetTable_t *sheet = FS_ParseSLK_Buffer(buffer);
    if (sheet && dest && schema && row_stride)
        *dest = FS_LoadSheetTyped(sheet, schema, row_stride, &count);
    SHEET_POOL_RESTORE();
    return count;
}

bool Stb_IniCacheLoad(stbIniCache_t *cache, cstring_t filename) {
    if (!cache || !filename) return false;
    Stb_IniCacheFree(cache);
    cache->source = FS_ParseINI(filename);
    return cache->source != NULL;
}

bool Stb_IniCacheLoadBuffer(stbIniCache_t *cache, cstring_t buffer) {
    if (!cache || !buffer) return false;
    Stb_IniCacheFree(cache);
    cache->source = FS_ParseINI_Buffer(buffer);
    return cache->source != NULL;
}

bool Stb_IniCacheLoadFiles(stbIniCache_t *cache, cstring_t const *filenames) {
    sheetTable_t *head = NULL, *tail = NULL;
    if (!cache || !filenames) return false;
    Stb_IniCacheFree(cache);
    for (; *filenames; filenames++) FS_AppendSheetTable(&head, &tail, FS_ParseINI(*filenames));
    cache->source = head;
    return head != NULL;
}

uint32_t Stb_IniDecode(stbIniCache_t const *ini, slkField_t const *schema, void **dest, uint32_t row_stride) {
    uint32_t count = 0;
    if (!ini || !ini->source || !dest || !schema || !row_stride) return 0;
    *dest = FS_LoadSheetTyped(ini->source, schema, row_stride, &count);
    return count;
}

cstring_t Stb_IniCacheFind(stbIniCache_t const *cache, cstring_t section, cstring_t key) {
    return cache ? FS_FindSheetCell(cache->source, section, key) : NULL;
}

void Stb_IniCacheFree(stbIniCache_t *cache) {
    sheetTable_t *table = cache ? cache->source : NULL;
    while (table) {
        sheetTable_t *next = table->next;
        free(table->allocation ? table->allocation : table);
        table = next;
    }
    if (cache) cache->source = NULL;
}
