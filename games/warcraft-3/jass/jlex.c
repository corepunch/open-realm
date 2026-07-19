#include "jlex.h"
#include <ctype.h>

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

LPCSTR parse_token(LPPARSER p) {
    static char word[MAX_SEGMENT_SIZE];
    while (isspace(*p->buffer)) ++p->buffer;
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
    while (isspace(*p->buffer))
        ++p->buffer;
    LPCSTR start = p->buffer;
    if (*p->buffer == '\"') {
        ++start;
        p->buffer = strchr(start, '\"');
        if (!p->buffer) {
            p->buffer = start + strlen(start);
        }
        size_t seglen = (size_t)(p->buffer - start);
        if (seglen >= MAX_SEGMENT_SIZE) {
            seglen = MAX_SEGMENT_SIZE - 1;
        }
        memcpy(segment, start, seglen);
        segment[seglen] = '\0';
        p->buffer = strchr(p->buffer, ',');
    } else {
        p->buffer = strchr(p->buffer, ',');
        if (p->buffer) {
            size_t seglen = (size_t)(p->buffer - start);
            if (seglen >= MAX_SEGMENT_SIZE) {
                seglen = MAX_SEGMENT_SIZE - 1;
            }
            memcpy(segment, start, seglen);
            segment[seglen] = '\0'; // Null-terminate the segment
        } else {
            strlcpy(segment, start, MAX_SEGMENT_SIZE);
            p->buffer = start + strlen(start);
            return segment;
        }
    }
    ++p->buffer;
    return segment;
}

LPCSTR parse_segment2(LPPARSER p) {
    static char segment[MAX_SEGMENT_SIZE];
    memset(segment, 0, MAX_SEGMENT_SIZE);
    if (*p->buffer == '\0')
        return NULL;
    while (isspace(*p->buffer))
        ++p->buffer;
    DWORD num_quotes = 0;
    LPSTR out = segment;
    LPSTR const out_end = segment + MAX_SEGMENT_SIZE - 1;
    for (; *p->buffer; ++p->buffer) {
        if (*p->buffer == ',' && (num_quotes & 1) == 0) {
            ++p->buffer;
            break;
        }
        if (*p->buffer == '"')
            ++num_quotes;
        if (out < out_end) {
            *out++ = *p->buffer;
        }
    }
    *out = '\0';
    return segment;
}



void parser_error(LPPARSER parser) {
    parser->error = true;
}

void *find_in_array(void const *array, long sizeofelem, LPCSTR name) {
    LPSTR str = (LPSTR)array;
    while (*(LPCSTR *)str) {
        LPCSTR value = *(LPCSTR *)str;
        if (!strcmp(value, name)) {
            return str;
        }
        str += sizeofelem;
    }
    return NULL;
}
