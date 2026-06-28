#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include "common/common.h"
#include "parser.h"

#define MAX_SEGMENT_SIZE 1024

BOOL eat_token(LPPARSER p, LPCSTR value) {
    LPCSTR tok = peek_token(p);
    if (!strcmp(tok, value)) {
        parse_token(p);
        return true;
    } else {
        return false;
    }
}

static void skip_space_and_comments(LPPARSER p) {
    DWORD guard = 0;
    for (;;) {
        if (++guard > 10000) { fprintf(stderr, "[HANG] skip_space_and_comments\n"); abort(); }
        while (isspace((unsigned char)*p->buffer)) {
            ++p->buffer;
        }

        if (p->buffer[0] == '/' && p->buffer[1] == '/') {
            p->buffer += 2;
            while (*p->buffer && *p->buffer != '\n') {
                ++p->buffer;
            }
            continue;
        }

        if (p->buffer[0] == '/' && p->buffer[1] == '*') {
            p->buffer += 2;
            while (*p->buffer) {
                if (p->buffer[0] == '*' && p->buffer[1] == '/') {
                    p->buffer += 2;
                    break;
                }
                ++p->buffer;
            }
            continue;
        }

        break;
    }
}

static void rtrim_segment(LPSTR segment) {
    LPSTR end = segment + strlen(segment);

    while (end > segment && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
}

LPCSTR parse_token(LPPARSER p) {
    static char word[MAX_SEGMENT_SIZE];
    
    skip_space_and_comments(p);
    
    if (*p->buffer == '\"') {
        LPCSTR closingQuote = strchr(p->buffer+1, '"');
        size_t stringLength = closingQuote-p->buffer+1;
        if (p->eat_quotes) {
            p->buffer++;
            stringLength -= 2;
        }
        memcpy(word, p->buffer, stringLength);
        word[stringLength] = '\0';
        p->buffer = ++closingQuote;
//        printf("%s\n", word);
        return word;
    } else if (strchr(p->delimiters, *p->buffer)) {
        word[0] = *(p->buffer++);
        word[1] = '\0';
        return word;
    } else {
        size_t segmentLength = 0;
        while (*p->buffer &&
           (!isspace(*p->buffer) && strchr(p->delimiters, *p->buffer) == NULL) &&
               segmentLength < MAX_SEGMENT_SIZE - 1) {
            word[segmentLength++] = *(p->buffer++);
        }
        word[segmentLength] = '\0'; // Null-terminate the segment
        return word;
    }
}

LPCSTR peek_token(LPPARSER p) {
    PARSER tmp = *p;
    LPCSTR token = parse_token(p);
    *p = tmp;
    return token;
}

LPCSTR parse_segment(LPPARSER p) {
    static char segment[MAX_SEGMENT_SIZE];
    memset(segment, 0, MAX_SEGMENT_SIZE);
    if (*p->buffer == '\0')
        return NULL;
    skip_space_and_comments(p);
    if (*p->buffer == '\0')
        return NULL;
    LPCSTR start = p->buffer;
    if (*p->buffer == '\"') {
        ++start;
        p->buffer = strchr(start, '\"');
        memcpy(segment, start, p->buffer - start);
        segment[p->buffer - start] = '\0';
        p->buffer = strchr(p->buffer, ',');
    } else {
        p->buffer = strchr(p->buffer, ',');
        if (p->buffer) {
            memcpy(segment, start, p->buffer - start);
            segment[p->buffer - start] = '\0'; // Null-terminate the segment
        } else {
            strcpy(segment, start);
            p->buffer = start + strlen(start);
            return segment;
        }
    }
    ++p->buffer;
    return segment;
}

LPCSTR parse_segment2(LPPARSER p) {
    static char segment[MAX_SEGMENT_SIZE];
    LPSTR out = segment;
    BOOL quoted = false;
    BOOL have_segment = false;
    DWORD guard = 0;

    memset(segment, 0, MAX_SEGMENT_SIZE);
    if (*p->buffer == '\0')
        return NULL;

    skip_space_and_comments(p);
    if (*p->buffer == '\0')
        return NULL;

    while (*p->buffer) {
        if (++guard > 100000) { fprintf(stderr, "[HANG] parse_segment2\n"); abort(); }
        if (!quoted && *p->buffer == ',') {
            ++p->buffer;
            break;
        }

        if (!quoted && p->buffer[0] == '/' && p->buffer[1] == '/') {
            p->buffer += 2;
            while (*p->buffer && *p->buffer != '\n') {
                ++p->buffer;
            }
            if (have_segment) {
                skip_space_and_comments(p);
                if (*p->buffer == ',') {
                    ++p->buffer;
                }
                break;
            }
            skip_space_and_comments(p);
            continue;
        }

        if (!quoted && p->buffer[0] == '/' && p->buffer[1] == '*') {
            p->buffer += 2;
            while (*p->buffer) {
                if (p->buffer[0] == '*' && p->buffer[1] == '/') {
                    p->buffer += 2;
                    break;
                }
                ++p->buffer;
            }
            if (have_segment) {
                skip_space_and_comments(p);
                if (*p->buffer == ',') {
                    ++p->buffer;
                }
                break;
            }
            skip_space_and_comments(p);
            continue;
        }

        if (*p->buffer == '"') {
            quoted = !quoted;
        }
        if (out < segment + MAX_SEGMENT_SIZE - 1) {
            *out++ = *p->buffer;
        }
        if (!isspace((unsigned char)*p->buffer)) {
            have_segment = true;
        }
        ++p->buffer;
    }
    *out = '\0';
    rtrim_segment(segment);
    return segment;
}



void parser_error(LPPARSER parser) {
    parser->error = true;
}

void *find_in_array(void *array, long sizeofelem, LPCSTR name) {
    LPSTR str = array;
    while (*(LPCSTR *)str) {
        LPCSTR value = *(LPCSTR *)str;
        if (!strcmp(value, name)) {
            return str;
        }
        str += sizeofelem;
    }
    return NULL;
}
