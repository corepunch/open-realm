#ifndef jparser_h
#define jparser_h

#include "jlex.h"

KNOWN_AS(token, TOKEN);

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
};

struct token {
    LPSTR primary;
    LPSTR secondary;
    TOKENTYPE type;
    DWORD flags;
    LPTOKEN init;
    LPTOKEN body;
    LPTOKEN next;
    LPTOKEN args;
    LPTOKEN condition;
    LPTOKEN elseblock;
    LPTOKEN index;
};

LPTOKEN JASS_ParseTokens(LPPARSER p);
LPTOKEN GALAXY_ParseTokens(LPPARSER p);

/* Set to true when GALAXY_ParseTokens exits via PARSER_THROW (longjmp).
 * False when parsing succeeds (even if the result is an empty token list). */
extern volatile BOOL galaxy_parse_error;

#endif
