#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "common/common.h"

#define MAX_INI_LINE 1024
#define MAX_SHEET_COLUMNS 256

typedef struct SheetCell {
    LPSTR text;
    USHORT column;
    USHORT row;
    LPSHEET next;
} sheetCell_t;

typedef struct sheet_cache_entry_s {
    char *name;
    sheetRow_t *rows;
    sheetRow_t *tail;
    struct sheet_cache_entry_s *next;
} sheet_cache_entry_t;

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
static sheet_cache_entry_t *sheet_cache = NULL;
static sheetRow_t *last_parsed_sheet_tail = NULL;

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

static void NormalizeSheetKey(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0;

    if (!src || !dst || dst_size == 0) {
        return;
    }

    while (*src && i + 1 < dst_size) {
        char ch = *src++;
        if (ch == '/') {
            ch = '\\';
        } else if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch + 0x20);
        }
        dst[i++] = ch;
    }
    dst[i] = '\0';
}

static sheetRow_t *SheetCacheLookup(LPCSTR fileName, sheetRow_t **tailOut)
{
    char key[256];
    sheet_cache_entry_t *entry;

    NormalizeSheetKey(fileName, key, sizeof(key));
    for (entry = sheet_cache; entry; entry = entry->next) {
        if (!strcmp(entry->name, key)) {
            if (tailOut) {
                *tailOut = entry->tail;
            }
            return entry->rows;
        }
    }
    if (tailOut) {
        *tailOut = NULL;
    }
    return NULL;
}

static void SheetCacheStore(LPCSTR fileName, sheetRow_t *rows, sheetRow_t *tail)
{
    char key[256];
    sheet_cache_entry_t *entry;

    if (!rows) {
        return;
    }

    NormalizeSheetKey(fileName, key, sizeof(key));
    entry = (sheet_cache_entry_t *)malloc(sizeof(sheet_cache_entry_t));
    if (!entry) {
        return;
    }
    entry->name = (char *)malloc(strlen(key) + 1);
    if (!entry->name) {
        free(entry);
        return;
    }
    memcpy(entry->name, key, strlen(key) + 1);
    entry->rows = rows;
    entry->tail = tail ? tail : rows;
    entry->next = sheet_cache;
    sheet_cache = entry;
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

sheetRow_t *FS_MakeRowsFromSheet(LPSHEET sheet) {
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
    last_parsed_sheet_tail = last_row;
    return start;
}

sheetRow_t *FS_ParseSLK_Buffer(LPCSTR buffer)
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
    last_parsed_sheet_tail = NULL;
    return NULL;
}


sheetRow_t *FS_ParseSLK(LPCSTR fileName) {
    sheetRow_t *cachedTail = NULL;
    sheetRow_t *cached = SheetCacheLookup(fileName, &cachedTail);
    LPSTR buffer;
    sheetRow_t *rows;

    if (cached) {
        last_parsed_sheet_tail = cachedTail;
        return cached;
    }

    buffer = FS_ReadFileIntoString(fileName);
    if (!buffer) {
        return NULL;
    }
    rows = FS_ParseSLK_Buffer(buffer);
    FS_FreeFileString(buffer);
    if (rows) {
        SheetCacheStore(fileName, rows, last_parsed_sheet_tail);
    }
    return rows;
}

LPCSTR FS_FindSheetCell(sheetRow_t *sheet, LPCSTR row, LPCSTR column) {
    FOR_EACH_LIST(sheetRow_t const, srow, sheet) {
        if (strcmp(srow->name, row))
            continue;
        FOR_EACH_LIST(sheetField_t const, scolumn, srow->fields) {
            if (strcasecmp(scolumn->name, column))
                continue;
            return scolumn->value;
        }
    }
    return NULL;
}

static sheetRow_t *FS_ParseINI_Buffer(LPCSTR buffer) {
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
    if (current_row != start) {
        last_parsed_sheet_tail = section;
        if (section) {
            section->next = NULL;
        }
        return start;
    }

    last_parsed_sheet_tail = NULL;
    return NULL;
}

sheetRow_t *FS_ParseINI(LPCSTR fileName) {
    sheetRow_t *cachedTail = NULL;
    sheetRow_t *cached = SheetCacheLookup(fileName, &cachedTail);
    LPSTR buffer;

    if (cached) {
        last_parsed_sheet_tail = cachedTail;
        return cached;
    }

    buffer = FS_ReadFileIntoString(fileName);
    if (!buffer) {
        return NULL;
    }
//    printf("%")
    sheetRow_t *config = FS_ParseINI_Buffer(buffer);
    if (!config) {
        fprintf(stderr, "Failed to parse %s\n", fileName);
    } else {
        SheetCacheStore(fileName, config, last_parsed_sheet_tail);
    }
    FS_FreeFileString(buffer);
    return config;
}

sheetRow_t *FS_GetParsedSheetTail(void)
{
    return last_parsed_sheet_tail;
}
