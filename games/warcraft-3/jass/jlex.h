#ifndef jlex_h
#define jlex_h

#include "../common/shared.h"

#ifndef WORD_EXTRACTOR_DEFINED
#define WORD_EXTRACTOR_DEFINED
KNOWN_AS(word_extractor, wordExtractor_t);

struct word_extractor {
    cstring_t buffer;
    cstring_t start;
    const char* delimiters;
    bool error;
    bool eat_quotes;
};
#endif

cstring_t parse_token(wordExtractor_t * p);
cstring_t jlex_parse_token(wordExtractor_t * p); /* libjass entry; game TU may shadow parse_token via stb_fdf */
cstring_t parse_segment(wordExtractor_t * p);
cstring_t parse_segment2(wordExtractor_t * p);
cstring_t peek_token(wordExtractor_t * p);
bool eat_token(wordExtractor_t * p, cstring_t value);
void parser_error(wordExtractor_t * parser) ;
void *find_in_array(void const *array, long sizeofelem, cstring_t name);

#endif
