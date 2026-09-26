#include "jlex.h"
#include <ctype.h>

#define MAX_SEGMENT_SIZE 16384  /* Galaxy scripts contain long string literals */

bool eat_token(wordExtractor_t * p, cstring_t value) {
    cstring_t tok = peek_token(p);
    if (!strcmp(tok, value)) {
        parse_token(p);
        return true;
    } else {
        return false;
    }
}

cstring_t parse_token(wordExtractor_t * p) {
    static char word[MAX_SEGMENT_SIZE];
    while (isspace(*p->buffer)) ++p->buffer;
    if (*p->buffer == '\"' || *p->buffer == '\'') {
        char quote = *p->buffer;
        cstring_t closingQuote = strchr(p->buffer + 1, quote);
        size_t stringLength;
        if (!closingQuote) {
            strlcpy(word, p->buffer, MAX_SEGMENT_SIZE);
            p->buffer += strlen(p->buffer);
            return word;
        }
        stringLength = (size_t)(closingQuote - p->buffer + 1);
        if (p->eat_quotes) {
            p->buffer++;
            stringLength -= 2;
        }
        if (stringLength >= MAX_SEGMENT_SIZE) stringLength = MAX_SEGMENT_SIZE - 1;
        memcpy(word, p->buffer, stringLength);
        word[stringLength] = '\0';
        p->buffer = closingQuote + 1;
        return word;
    } else if (*p->buffer && strchr(p->delimiters, *p->buffer)) {
        word[0] = *(p->buffer++);
        word[1] = '\0';
        if ((*p->buffer == '=' && strchr("=<>!", word[0])) ||
            (*p->buffer == word[0] && strchr("<>&|", word[0]))) {
            word[1] = *(p->buffer++);
            word[2] = '\0';
        }
        return word;
    } else if (*p->buffer == '$' || isdigit((unsigned char)*p->buffer) || (*p->buffer == '.' && isdigit((unsigned char)p->buffer[1]))) {
        /* Minified JASS glues 851983then / 0x hex; keep $hex and 0xhex as one token. */
        size_t n = 0;
        if (*p->buffer == '$') {
            word[n++] = *(p->buffer++);
            while (isxdigit((unsigned char)*p->buffer) && n < MAX_SEGMENT_SIZE - 1)
                word[n++] = *(p->buffer++);
        } else if (p->buffer[0] == '0' && (p->buffer[1] == 'x' || p->buffer[1] == 'X')) {
            word[n++] = *(p->buffer++);
            word[n++] = *(p->buffer++);
            while (isxdigit((unsigned char)*p->buffer) && n < MAX_SEGMENT_SIZE - 1)
                word[n++] = *(p->buffer++);
        } else {
            while (isdigit((unsigned char)*p->buffer) && n < MAX_SEGMENT_SIZE - 1)
                word[n++] = *(p->buffer++);
            if (*p->buffer == '.' && n < MAX_SEGMENT_SIZE - 1) {
                word[n++] = *(p->buffer++);
                while (isdigit((unsigned char)*p->buffer) && n < MAX_SEGMENT_SIZE - 1)
                    word[n++] = *(p->buffer++);
            }
        }
        word[n] = '\0';
        return word;
    } else {
        size_t segmentLength = 0;
        /* Stop at quotes: minified JASS glues return"" and 'Hpal'or(x) without space.
         * Quotes are not jdo delimiters; the quote branches above read the literal. */
        while (*p->buffer &&
           (!isspace(*p->buffer) && *p->buffer != '"' && *p->buffer != '\'' && strchr(p->delimiters, *p->buffer) == NULL) &&
               segmentLength < MAX_SEGMENT_SIZE - 1) {
            word[segmentLength++] = *(p->buffer++);
        }
        word[segmentLength] = '\0'; // Null-terminate the segment
        return word;
    }
}

cstring_t jlex_parse_token(wordExtractor_t * p) { return parse_token(p); }

cstring_t peek_token(wordExtractor_t * p) {
    wordExtractor_t tmp = *p;
    cstring_t token = parse_token(p);
    *p = tmp;
    return token;
}

cstring_t parse_segment(wordExtractor_t * p) {
    static char segment[MAX_SEGMENT_SIZE];
    memset(segment, 0, MAX_SEGMENT_SIZE);
    if (*p->buffer == '\0')
        return NULL;
    while (isspace(*p->buffer))
        ++p->buffer;
    cstring_t start = p->buffer;
    if (*p->buffer == '"') {
        cstring_t closingQuote;
        cstring_t comma;
        size_t seglen;

        ++start;
        closingQuote = strchr(start, '"');
        if (!closingQuote) {
            seglen = MIN(strlen(start), MAX_SEGMENT_SIZE - 1);
            memcpy(segment, start, seglen);
            segment[seglen] = '\0';
            p->buffer = start + strlen(start);
            return segment;
        }
        seglen = (size_t)(closingQuote - start);
        if (seglen >= MAX_SEGMENT_SIZE) {
            seglen = MAX_SEGMENT_SIZE - 1;
        }
        memcpy(segment, start, seglen);
        segment[seglen] = '\0';
        comma = strchr(closingQuote + 1, ',');
        p->buffer = comma ? comma + 1 : closingQuote + 1;
        return segment;
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

cstring_t parse_segment2(wordExtractor_t * p) {
    static char segment[MAX_SEGMENT_SIZE];
    memset(segment, 0, MAX_SEGMENT_SIZE);
    if (*p->buffer == '\0')
        return NULL;
    while (isspace(*p->buffer))
        ++p->buffer;
    uint32_t num_quotes = 0;
    string_t out = segment;
    string_t const out_end = segment + MAX_SEGMENT_SIZE - 1;
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



void parser_error(wordExtractor_t * parser) {
    parser->error = true;
}

void *find_in_array(void const *array, long sizeofelem, cstring_t name) {
    string_t str = (string_t)array;
    while (*(cstring_t *)str) {
        cstring_t value = *(cstring_t *)str;
        if (!strcmp(value, name)) {
            return str;
        }
        str += sizeofelem;
    }
    return NULL;
}
