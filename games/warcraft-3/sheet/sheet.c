#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "common/common.h"
#include "games/warcraft-3/common/stb_slk.h"

#define MAX_INI_LINE 1024
#define MAX_SHEET_COLUMNS 256

typedef struct SheetCell {
    LPSTR text;
    USHORT column;
    USHORT row;
    LPSHEET next;
} sheetCell_t;

typedef struct sheet_field_s {
    LPCSTR name, value;
    struct sheet_field_s *next;
} sheetField_t;

typedef struct sheet_row_s {
    LPCSTR name;
    sheetField_t *fields;
    struct sheet_row_s *next;
} sheetRow_t;

typedef struct sheet_table_s {
    sheetRow_t *rows;
    sheetRow_t *tail;
    struct sheet_table_s *next;
} sheetTable_t;

// TODO: allocate these as needed, this is only PoC and will only work for 1 level
static sheetCell_t cells[1024 * 1024] = { 0 };
static sheetRow_t rows[1024 * 1024] = { 0 };
static sheetField_t fields[1024 * 1024] = { 0 };
static char text_buffer[8 * 1024 * 1024] = { 0 };
static LPSTR current_text = text_buffer;
static LPSHEET current_cell = cells;
static LPSHEET previous_cell = cells;
static sheetRow_t *current_row = rows;
static sheetField_t *current_field = fields;

static LPSTR SheetStoreTextRange(LPCSTR text, size_t len) {
    LPSTR out = current_text;
    size_t remaining;

    if (!text) {
        text = "";
        len = 0;
    }

    remaining = (size_t)((text_buffer + sizeof(text_buffer)) - current_text);
    if (remaining == 0) {
        return text_buffer + sizeof(text_buffer) - 1;
    }
    if (len >= remaining) {
        len = remaining - 1;
    }
    memcpy(current_text, text, len);
    current_text[len] = '\0';
    current_text += len + 1;
    return out;
}

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
    return table;
}

/* Scan one semicolon-delimited SLK record field from *p up to end.
 * For K fields, strips surrounding quotes and decodes "" → literal ".
 * Advances *p past the field and any trailing semicolon.
 * Returns false when no field remains. */
static bool ScanSLKField(LPCSTR *p, LPCSTR end, char *out, size_t cap) {
    char *dst = out, *dst_end = out + cap - 1;
    LPCSTR s = *p;
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

static void FS_FillSheetCell(DWORD x, DWORD y, LPCSTR text) {
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
    current_cell->column = (USHORT)x;
    current_cell->row = (USHORT)y;
    current_cell->next = current_cell + 1;
    current_cell->text = current_text;
    memcpy(current_text, text, len);
    current_text[len] = '\0';
    current_text += len + 1;
    previous_cell = current_cell;
    current_cell++;
}

static sheetTable_t *FS_MakeRowsFromSheet(LPSHEET sheet) {
    LPCSTR columns[256] = { 0 };
    sheetRow_t *start = NULL;
    sheetRow_t *last_row = NULL;
    sheetRow_t **rows_by_number = NULL;
    DWORD num_rows = 0;

    FOR_EACH_LIST(SHEET, cell, sheet) {
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

    FOR_EACH_LIST(SHEET, cell, sheet) {
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

static sheetTable_t *FS_ParseSLK_Buffer(LPCSTR buffer)
{
    LPSHEET start = current_cell;
    DWORD X = 1, Y = 1;
    char field[MAX_SHEET_LINE];

    if (!buffer) return NULL;

    while (*buffer) {
        LPCSTR line_start = buffer, line_end;
        char rectype;
        LPCSTR p;

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
            case 'X': X = (DWORD)atoi(field + 1); break;
            case 'Y': Y = (DWORD)atoi(field + 1); break;
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


static sheetTable_t *FS_ParseSLK(LPCSTR fileName) {
    LPSTR buffer = FS_ReadFileIntoString(fileName);
    sheetTable_t *sheet;
    if (!buffer) return NULL;
    sheet = FS_ParseSLK_Buffer(buffer);
    FS_FreeFileString(buffer);
    return sheet;
}

static LPCSTR FS_FindSheetCell(sheetTable_t const *sheet, LPCSTR row, LPCSTR column) {
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

static sheetTable_t *FS_ParseINI_Buffer(LPCSTR buffer) {
    LPCSTR p = buffer;
    sheetRow_t *start = current_row;
    sheetRow_t *section = NULL;
    while (true) {
        while (*p && isspace(*p)) p++;
        if (!*p)
            break;
        if (p[0] == '/' && p[1] == '/') {
            for (; *p != '\n' && *p != '\0'; p++);
        } else if (*p == '[') {
            LPCSTR nameStart;
            LPCSTR nameEnd;

            p++;
            nameStart = p;
            while (*p && *p != ']' && *p != '\n' && *p != '\r') {
                p++;
            }
            nameEnd = p;
            if (*p == ']') {
                p++;
            }
            if (section) {
                section->next = current_row;
            }
            section = current_row++;
            section->next = NULL;
            section->fields = NULL;
            section->name = SheetStoreTextRange(nameStart, (size_t)(nameEnd - nameStart));
        } else {
            LPCSTR lineStart = p;
            LPCSTR lineEnd;
            LPCSTR eq;

            while (*p != '\n' && *p != '\r' && *p != '\0') {
                p++;
            }
            lineEnd = p;
            eq = memchr(lineStart, '=', (size_t)(lineEnd - lineStart));
            if (eq && section) {
                LPCSTR keyEnd = eq;
                LPCSTR valueStart = eq + 1;

                while (keyEnd > lineStart && keyEnd[-1] == ' ') {
                    keyEnd--;
                }
                while (valueStart < lineEnd && *valueStart == ' ') {
                    valueStart++;
                }
//                printf("%s.%s %s\n", currentSec, line, eq);
                sheetField_t *field = current_field++;
                field->name = SheetStoreTextRange(lineStart, (size_t)(keyEnd - lineStart));
                field->value = SheetStoreTextRange(valueStart, (size_t)(lineEnd - valueStart));
                ADD_TO_LIST(field, section->fields);
            }
        }
    }
    if (current_row == start)
        return NULL;
    if (section)
        section->next = NULL;
    return FS_MakeTable(start, section);
}

static sheetTable_t *FS_ParseINI(LPCSTR fileName) {
    LPSTR buffer = FS_ReadFileIntoString(fileName);
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

static void SheetSetTypedField(BYTE *dst, bzFieldType_t type, LPCSTR value, LPCSTR field_name)
{
    switch (type) {
    case BZ_FIELD_U32: *(DWORD *)dst = value ? (DWORD)atoi(value) : 0; break;
    case BZ_FIELD_FLOAT: *(FLOAT *)dst = value ? (FLOAT)atof(value) : 0.f; break;
    case BZ_FIELD_BOOL: *(BOOL *)dst = value && (atoi(value) != 0 || !strcasecmp(value, "TRUE")); break;
    case BZ_FIELD_CSTR: {
        size_t len = value ? strlen(value) : 0;
        LPSTR str = (LPSTR)malloc(len + 1);
        if (!str) { fprintf(stderr, "SLK: out of memory copying field '%s'\n", field_name ? field_name : ""); break; }
        memcpy(str, value ? value : "", len + 1);
        free(*(void **)dst);
        *(LPSTR *)dst = str;
        break;
    }
    case BZ_FIELD_FOURCC: {
        DWORD key = 0;
        if (value) {
            size_t n = strlen(value);
            memcpy(&key, value, n < 4 ? n : 4);
        }
        *(DWORD *)dst = key;
        break;
    }
    default: break;
    }
}

static void *FS_LoadSheetTyped(sheetTable_t const *sheet, slkField_t const *schema, size_t row_size, DWORD *count_out)
{
    DWORD capacity = 0, out_count = 0;
    LPCSTR *seen_names;
    BYTE *rows_out;

    if (count_out)
        *count_out = 0;
    if (!sheet || !schema || !row_size)
        return NULL;

    for (sheetTable_t const *table = sheet; table; table = table->next)
        FOR_EACH_LIST(sheetRow_t const, row, table->rows) if (row->name && row->name[0]) capacity++;
    if (!capacity)
        return NULL;

    seen_names = (LPCSTR *)calloc(capacity, sizeof(*seen_names));
    rows_out = (BYTE *)calloc(capacity, row_size);
    if (!seen_names || !rows_out) {
        fprintf(stderr, "SLK: out of memory allocating %u decoded rows\n", capacity);
        free(seen_names);
        free(rows_out);
        return NULL;
    }

    for (sheetTable_t const *table = sheet; table; table = table->next) {
        FOR_EACH_LIST(sheetRow_t const, row, table->rows) {
            BYTE *dst;
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
                LPCSTR value;
                BYTE *field_dst = dst + field->offset;

                if (!field->column[0]) {
                    SheetSetTypedField(field_dst, field->type, row->name, field->column);
                    continue;
                }
                value = FS_FindSheetCell(sheet, row->name, field->column);
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

DWORD Stb_SlkLoad(LPCSTR filename, slkField_t const *schema, void **dest, DWORD row_stride) {
    DWORD count = 0;
    sheetTable_t *sheet = FS_ParseSLK(filename);
    if (!sheet || !dest || !schema || !row_stride) return 0;
    *dest = FS_LoadSheetTyped(sheet, schema, row_stride, &count);
    return count;
}

DWORD Stb_SlkLoadBuffer(LPCSTR buffer, slkField_t const *schema, void **dest, DWORD row_stride) {
    DWORD count = 0;
    sheetTable_t *sheet = FS_ParseSLK_Buffer(buffer);
    if (!sheet || !dest || !schema || !row_stride) return 0;
    *dest = FS_LoadSheetTyped(sheet, schema, row_stride, &count);
    return count;
}

BOOL Stb_IniCacheLoad(stbIniCache_t *cache, LPCSTR filename) {
    if (!cache || !filename) return false;
    cache->source = FS_ParseINI(filename);
    return cache->source != NULL;
}

BOOL Stb_IniCacheLoadFiles(stbIniCache_t *cache, LPCSTR const *filenames) {
    sheetTable_t *head = NULL, *tail = NULL;
    if (!cache || !filenames) return false;
    for (; *filenames; filenames++) FS_AppendSheetTable(&head, &tail, FS_ParseINI(*filenames));
    cache->source = head;
    return head != NULL;
}

DWORD Stb_IniDecode(stbIniCache_t const *ini, slkField_t const *schema, void **dest, DWORD row_stride) {
    DWORD count = 0;
    if (!ini || !ini->source || !dest || !schema || !row_stride) return 0;
    *dest = FS_LoadSheetTyped(ini->source, schema, row_stride, &count);
    return count;
}

LPCSTR Stb_IniCacheFind(stbIniCache_t const *cache, LPCSTR section, LPCSTR key) {
    return cache ? FS_FindSheetCell(cache->source, section, key) : NULL;
}

void Stb_IniCacheFree(stbIniCache_t *cache) {
    if (cache) cache->source = NULL;
}
