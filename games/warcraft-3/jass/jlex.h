#ifndef jlex_h
#define jlex_h

#include "../common/shared.h"

#ifndef WORD_EXTRACTOR_DEFINED
#define WORD_EXTRACTOR_DEFINED
KNOWN_AS(word_extractor, PARSER);

struct word_extractor {
    LPCSTR buffer;
    const char* delimiters;
    BOOL error;
    BOOL eat_quotes;
};
#endif

LPCSTR parse_token(LPPARSER p);
LPCSTR parse_segment(LPPARSER p);
LPCSTR parse_segment2(LPPARSER p);
LPCSTR peek_token(LPPARSER p);
BOOL eat_token(LPPARSER p, LPCSTR value);
void parser_error(LPPARSER parser) ;
void *find_in_array(void const *array, long sizeofelem, LPCSTR name);

#endif
