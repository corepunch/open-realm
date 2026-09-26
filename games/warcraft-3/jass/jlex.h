#ifndef jlex_h
#define jlex_h

#include "../common/shared.h"

#ifndef WORD_EXTRACTOR_DEFINED
#define WORD_EXTRACTOR_DEFINED
KNOWN_AS(word_extractor, PARSER);

struct word_extractor {
    cstring_t buffer;
    cstring_t start;
    const char* delimiters;
    bool error;
    bool eat_quotes;
};
#endif

cstring_t parse_token(LPPARSER p);
cstring_t jlex_parse_token(LPPARSER p); /* libjass entry; game TU may shadow parse_token via stb_fdf */
cstring_t parse_segment(LPPARSER p);
cstring_t parse_segment2(LPPARSER p);
cstring_t peek_token(LPPARSER p);
bool eat_token(LPPARSER p, cstring_t value);
void parser_error(LPPARSER parser) ;
void *find_in_array(void const *array, long sizeofelem, cstring_t name);

#endif
