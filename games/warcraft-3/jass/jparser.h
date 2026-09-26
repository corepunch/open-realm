#ifndef jparser_h
#define jparser_h

#include "jlex.h"

KNOWN_AS(token, token_t);

typedef enum {
    TT_UNKNOWN,
    TT_VALUE,
    TT_FUNCTION,
    TT_TYPEDEF,
    TT_VARDECL,
    TT_GLOBAL,
    TT_IDENTIFIER,
    TT_ARRAYACCESS,
    TT_CALL,
    TT_INTEGER,
    TT_REAL,
    TT_STRING,
    TT_FOURCC,
    TT_BOOLEAN,
    TT_IF,
    TT_SET,
    TT_LOOP,
    TT_ELSE,
    TT_EXITWHEN,
    TT_RETURN,
} TOKENTYPE;

enum {
    TF_NATIVE = 1,
    TF_CONSTANT = 2,
    TF_ARRAY = 4,
    TF_FUNCTION = 8,
    TF_DEBUG = 16,
};

struct token {
    string_t primary;
    string_t secondary;
    TOKENTYPE type;
    uint32_t flags;
    token_t * init;
    token_t * body;
    token_t * next;
    token_t * args;
    token_t * condition;
    token_t * elseblock;
    token_t * index;
};

token_t * JASS_ParseTokens(wordExtractor_t * p);
token_t * GALAXY_ParseTokens(wordExtractor_t * p);
void JASS_FreeTokens(token_t * tokens);

#endif
