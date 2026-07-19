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

static DWORD SheetParseTokens(TCHAR *buffer) {
    DWORD numtokens = 1;
    for (TCHAR *it = buffer; *it != '\0'; it++) {
        if (*it == ';') {
            numtokens++;
            *it = '\0';
        }
    }
    return numtokens;
}

static TCHAR *GetToken(TCHAR *buffer, DWORD index) {
    while (index > 0) {
        buffer += strlen(buffer) + 1;
        index--;
    }
    return buffer;
}

//int text_size = 0;

static void FS_FillSheetCell(DWORD x, DWORD y, LPSTR text) {
    current_cell->column = x;
    current_cell->row = y;
    current_cell->next = current_cell + 1;
    current_cell->text = current_text;
    for (char *instr = text; *instr; instr++) {
        if (*instr == '"' || *instr == '\r' || *instr == '\n')
            continue;
        *(current_text++) = *instr;
    }
    *(current_text++) = '\0';
    previous_cell = current_cell;
    current_cell++;
//    if (y == 2) {
//    if (strstr(cell->text, "hhou")) {
//        printf("\t%d %s\n", x, cell->text);
//    }
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

static sheetRow_t *FS_ParseSLK_Buffer(LPCSTR buffer)
{
    LPCSTR line;
    LPSHEET start = current_cell;
    DWORD X = 1;
    DWORD Y = 1;
    char czBuffer[MAX_SHEET_LINE];

    if (!buffer) {
        return NULL;
    }

    while (*buffer) {
        line = buffer;
        while (*buffer && *buffer != '\n' && *buffer != '\r') {
            buffer++;
        }

        size_t len = (size_t)(buffer - line);
        if (len >= sizeof(czBuffer)) {
            len = sizeof(czBuffer) - 1;
        }
        memcpy(czBuffer, line, len);
        czBuffer[len] = '\0';

        if (czBuffer[0] == 'C' || czBuffer[0] == 'F') {
            DWORD const numTokens = SheetParseTokens(czBuffer);
            for (DWORD i = 1; i < numTokens; i++) {
                TCHAR *token = GetToken(czBuffer, i);
                switch (*token) {
                    case 'X':
                        X = atoi(token + 1);
                        break;
                    case 'Y':
                        Y = atoi(token + 1);
                        break;
                    case 'K':
                        FS_FillSheetCell(X, Y, token + 1);
                        break;
                }
            }
        }

        while (*buffer == '\r' || *buffer == '\n') {
            buffer++;
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
