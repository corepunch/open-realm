#include "common/common.h"
#include <ctype.h>

static SHEETHOST sheet_host = { 0 };

bool ParserDone(parser_t *p) {
    return !*p->str || p->error;
}

bool ParserSingleLineComment(parser_t *p, cstring_t str) {
    return str[0] == '/' && str[1] == '/';
}

bool ParserSpace(parser_t *p, cstring_t str) {
    if (!p->reading_string) {
        if (isspace(*str)) return true;
        if (p->comma_space && *str == ',') return true;
        if (*str == '\"') return true;
    }
    return false;
}

void ParserError(parser_t *p) {
    p->error = true;
}

void FS_SetSheetHost(SHEETHOST const *host) {
    if (host) {
        sheet_host = *host;
    } else {
        memset(&sheet_host, 0, sizeof(sheet_host));
    }
}

string_t FS_ReadFileIntoString(cstring_t fileName) {
    handle_t raw = NULL;
    uint32_t fileSize = 0;
    string_t buffer;

    if (!sheet_host.ReadFile || !sheet_host.FreeFile || !sheet_host.MemAlloc || !sheet_host.MemFree) {
        return NULL;
    }
    raw = sheet_host.ReadFile(fileName, &fileSize);
    if (!raw) {
        return NULL;
    }
    buffer = sheet_host.MemAlloc((long)fileSize + 1);
    if (!buffer) {
        sheet_host.FreeFile(raw);
        return NULL;
    }
    if (fileSize > 0) {
        memcpy(buffer, raw, fileSize);
    }
    buffer[fileSize] = '\0';
    sheet_host.FreeFile(raw);
    return buffer;
}

void FS_FreeFileString(string_t buffer) {
    if (buffer && sheet_host.MemFree) {
        sheet_host.MemFree(buffer);
    }
}

string_t ParserGetToken(parser_t *p) {
    for (; !ParserDone(p); p->str++) {
        if (ParserSingleLineComment(p, p->str)) {
            *p->tok = '\0';
            for (; p->str[1] != '\n' && p->str[1] != '\0'; p->str++)
            p->tok = p->token;
            if (p->tok != p->token) {
                return p->token;
            }
        }
        if (ParserSpace(p, p->str)) {
            if (p->tok != p->token) {
                *p->tok = '\0';
                p->tok = p->token;
                return p->token;
            }
        } else {
            *(p->tok++) = *p->str;
        }
    }
    if (p->tok != p->token) {
        *p->tok = '\0';
        p->tok = p->token;
        return p->token;
    } else {
        return NULL;
    }
}
