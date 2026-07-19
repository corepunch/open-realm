#include "jparser.h"
#include "jass.h"
#include <setjmp.h>

#define ALLOC(type) jass_alloc(sizeof(type))
#define FREE(val) SAFE_DELETE(val, jass_free)
#define PARSER(NAME, ...) static LPTOKEN NAME(LPPARSER p, ##__VA_ARGS__)

#define PARSER_THROW(...) \
fprintf(stderr, __VA_ARGS__); \
fprintf(stderr, "\n"); \
parser_throw(); \
longjmp(exception_env, 1);

static jmp_buf exception_env;
typedef LPTOKEN (*LPGRAMMARFUNC)(LPPARSER);

typedef struct {
    LPCSTR name;
    LPGRAMMARFUNC func;
} parseClass_t;

extern parseClass_t function_keywords[];

BOOL is_integer(LPCSTR tok);
BOOL is_float(LPCSTR tok);
BOOL is_identifier(LPCSTR str);
BOOL is_string(LPCSTR tok);
BOOL is_fourcc(LPCSTR tok);

BOOL is_multiplicative_operator(LPCSTR str) {
    return !strcmp(str, "*") || !strcmp(str, "/");
}

BOOL is_additive_operator(LPCSTR str) {
    return !strcmp(str, "+") || !strcmp(str, "-");
}

BOOL is_compare_operator(LPCSTR str) {
    return !strcmp(str, ">") || !strcmp(str, "<") || !strcmp(str, "=") || !strcmp(str, "!");
}

BOOL is_logic_operator(LPCSTR str) {
    return !strcmp(str, "and") || !strcmp(str, "or");
}

LPCSTR jass_getoperator(LPCSTR str) {
    if (!strcmp(str, "+")) return "__add";
    if (!strcmp(str, "-")) return "__sub";
    if (!strcmp(str, "*")) return "__mul";
    if (!strcmp(str, "/")) return "__div";
    if (!strcmp(str, "!=")) return "__ne";
    if (!strcmp(str, "==")) return "__eq";
    if (!strcmp(str, ">=")) return "__ge";
    if (!strcmp(str, "<=")) return "__le";
    if (!strcmp(str, ">")) return "__gt";
    if (!strcmp(str, "<")) return "__lt";
    if (!strcmp(str, "and")) return "__and";
    if (!strcmp(str, "or")) return "__or";
    return str;
}

void parser_throw(void) {
    assert(false);
}

LPSTR read_identifier(LPPARSER p) {
    if (is_identifier(peek_token(p))) {
        return strdup(parse_token(p));
    } else {
        return NULL;
    }
}

static LPGRAMMARFUNC eat_keyword(LPPARSER p, parseClass_t *keywords) {
    for (parseClass_t *cl = keywords; cl->name; cl++) {
        if (eat_token(p, cl->name)) {
            return cl->func;
        }
    }
    return NULL;
}

static BOOL parse_body(LPPARSER p, LPTOKEN function) {
    LPTOKEN token = NULL;
    LPGRAMMARFUNC func = eat_keyword(p, function_keywords);
    if (func && (token = func(p))) {
        PUSH_BACK(TOKEN, token, function->body);
    } else {
        PARSER_THROW("error parsing function");
    }
    return true;
}

static LPTOKEN alloc_token(TOKENTYPE type) {
    LPTOKEN token = ALLOC(TOKEN);
    token->type = type;
    return token;
}

//PARSER(parse_identifier) {
//    LPTOKEN token = alloc_token(TT_IDENTIFIER);
//    token->primary = read_identifier(p);
//    return token;
//}

PARSER(keyword_type) {
    LPTOKEN token = alloc_token(TT_TYPEDEF);
    token->primary = read_identifier(p);
    if (eat_token(p, "extends")) {
        token->secondary = read_identifier(p);
    } else {
        PARSER_THROW("EXTENDS expected");
    }
    return token;
}

PARSER(parse_args) {
    if (eat_token(p, "nothing")) {
        return NULL;
    }
    LPTOKEN args = NULL;
    while (!args || eat_token(p, ",")) {
        LPTOKEN arg = alloc_token(TT_VARDECL);
        arg->primary = read_identifier(p);
        arg->secondary = read_identifier(p);
        PUSH_BACK(TOKEN, arg, args);
    }
    return args;
}

PARSER(parse_function_decl) {
    LPTOKEN token = alloc_token(TT_FUNCTION);
    token->primary = read_identifier(p);
    if (eat_token(p, "takes")) {
        token->args = parse_args(p);
    }
    if (eat_token(p, "returns")) {
        token->secondary = read_identifier(p);
    }
    return token;
}

PARSER(keyword_native) {
    LPTOKEN token = parse_function_decl(p);
    token->flags |= TF_NATIVE;
    return token;
}

PARSER(keyword_constant) {
    if (eat_token(p, "native")) {
        LPTOKEN token = keyword_native(p);
        token->flags |= TF_CONSTANT;
        return token;
    } else {
        PARSER_THROW("expected native after constant");
    }
}

static void jass_remove_quotes(LPSTR str, char quote) {
    size_t len = strlen(str);
    if (len >= 2 && str[0] == quote && str[len - 1] == quote) {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
}

LPTOKEN alloc_ident_token(LPPARSER p, TOKENTYPE tt) {
    LPTOKEN t = alloc_token(tt);
    t->primary = strdup(parse_token(p));
    return t;
}

LPTOKEN parse_operator_token(LPPARSER p) {
    UINAME op = { 0 };
    strlcpy(op, parse_token(p), sizeof(op));
    if (eat_token(p, "=")) {
        op[1] = '=';
    }
    LPCSTR operatorid = jass_getoperator(op);
    LPTOKEN t = alloc_token(TT_CALL);
    t->primary = strdup(operatorid);
    return t;
}

PARSER(parse_logical_expression);

PARSER(read_single_identifier) {
    LPCSTR tok = peek_token(p);
    LPTOKEN left = NULL;
    if (eat_token(p, "function")) {
        left = alloc_ident_token(p, TT_IDENTIFIER);
        left->flags |= TF_FUNCTION;
    } else if (eat_token(p, "-")) {
        left = alloc_token(TT_CALL);
        left->primary = strdup("__unm");
        left->args = read_single_identifier(p);
    } else if (eat_token(p, "not")) {
        left = alloc_token(TT_CALL);
        left->primary = strdup("__not");
        left->args = read_single_identifier(p);
    } else if (eat_token(p, "(")) {
        left = parse_logical_expression(p);
    } else if (is_integer(tok)) {
        left = alloc_ident_token(p, TT_INTEGER);
    } else if (is_float(tok)) {
        left = alloc_ident_token(p, TT_REAL);
    } else if (is_string(tok)) {
        left = alloc_ident_token(p, TT_STRING);
        jass_remove_quotes(left->primary, '\"');
    } else if (is_fourcc(tok)) {
        left = alloc_ident_token(p, TT_FOURCC);
        jass_remove_quotes(left->primary, '\'');
    } else if (!strcmp(tok, "true") || !strcmp(tok, "false")) {
        left = alloc_ident_token(p, TT_BOOLEAN);
    } else if (is_identifier(tok)) {
        left = alloc_ident_token(p, TT_IDENTIFIER);
        if (eat_token(p, "(")) {
            left->type = TT_CALL;
            if (!eat_token(p, ")")) {
                left->args = parse_logical_expression(p);
            }
        }
        if (eat_token(p, "[")) {
            left->type = TT_ARRAYACCESS;
            left->index = parse_logical_expression(p);
        }
    } else {
        return NULL;
    }
    return left;
}

PARSER(parse_multiplicative_expression) {
    LPTOKEN left = read_single_identifier(p);
    if (is_multiplicative_operator(peek_token(p))) {
        LPTOKEN oper = parse_operator_token(p);
        LPTOKEN right = parse_multiplicative_expression(p);
        PUSH_BACK(TOKEN, left, oper->args);
        PUSH_BACK(TOKEN, right, oper->args);
        return oper;
    }
    return left;
}

PARSER(parse_additive_expression) {
    LPTOKEN left = parse_multiplicative_expression(p);
    if (is_additive_operator(peek_token(p))) {
        LPTOKEN oper = parse_operator_token(p);
        LPTOKEN right = parse_additive_expression(p);
        PUSH_BACK(TOKEN, left, oper->args);
        PUSH_BACK(TOKEN, right, oper->args);
        return oper;
    }
    return left;
}

PARSER(parse_comparison_expression) {
    LPTOKEN left = parse_additive_expression(p);
    if (is_compare_operator(peek_token(p))) {
        LPTOKEN oper = parse_operator_token(p);
        LPTOKEN right = parse_comparison_expression(p);
        PUSH_BACK(TOKEN, left, oper->args);
        PUSH_BACK(TOKEN, right, oper->args);
        return oper;
    }
    return left;
}

PARSER(parse_logical_expression) {
    LPTOKEN left = parse_comparison_expression(p);
    if (is_logic_operator(peek_token(p))) {
        LPTOKEN oper = parse_operator_token(p);
        LPTOKEN right = parse_logical_expression(p);
        PUSH_BACK(TOKEN, left, oper->args);
        PUSH_BACK(TOKEN, right, oper->args);
        return oper;
    }
    if (eat_token(p, ",")) {
        left->next = parse_logical_expression(p);
        return left;
    }
    if (eat_token(p, ")") || eat_token(p, "]")) {
        return left;
    }
    return left;
}

PARSER(keyword_globals) {
    LPTOKEN globals = NULL;
    while (!eat_token(p, "endglobals")) {
        LPTOKEN token = alloc_token(TT_GLOBAL);
        if (eat_token(p, "constant")) {
            token->flags |= TF_CONSTANT;
        }
        token->primary = read_identifier(p);
        if (eat_token(p, "array")) {
            token->flags |= TF_ARRAY;
        }
        token->secondary = read_identifier(p);
        if (eat_token(p, "=")) {
            token->init = parse_logical_expression(p);
        }
        PUSH_BACK(TOKEN, token, globals);
    }
    return globals;
}

PARSER(statement_set) {
    LPTOKEN token = alloc_token(TT_SET);
    token->secondary = read_identifier(p);
    if (eat_token(p, "[")) {
        token->index = parse_logical_expression(p);
    }
    if (eat_token(p, "=")) {
        token->init = parse_logical_expression(p);
    }
    return token;
}

PARSER(statement_call) {
    return parse_logical_expression(p);
}

PARSER(statement_local) {
    LPTOKEN token = alloc_token(TT_VARDECL);
    token->primary = read_identifier(p);
    if (eat_token(p, "array")) {
        token->flags |= TF_ARRAY;
    }
    token->secondary = read_identifier(p);
    if (eat_token(p, "=")) {
        token->init = parse_logical_expression(p);
    }
    return token;
}

PARSER(statement_if) {
    LPTOKEN token = alloc_token(TT_IF);
    LPTOKEN target = token;
    token->condition = parse_logical_expression(p);
    if (!eat_token(p, "then")) {
        FREE(token);
        PARSER_THROW("THEN expected");
    }
    while (!eat_token(p, "endif")) {
        if (eat_token(p, "elseif")) {
            LPTOKEN next = alloc_token(TT_ELSE);
            next->condition = parse_logical_expression(p);
            if (!eat_token(p, "then")) {
                FREE(token);
                PARSER_THROW("THEN expected");
            }
            target->elseblock = next;
            target = next;
        } else if (eat_token(p, "else")) {
            LPTOKEN next = alloc_token(TT_ELSE);
            target->elseblock = next;
            target = next;
        } else if (!parse_body(p, target)) {
            FREE(token);
            PARSER_THROW("broken if statement")
        }
    }
    return token;
}

PARSER(statement_exitwhen) {
    LPTOKEN token = alloc_token(TT_EXITWHEN);
    token->condition = parse_logical_expression(p);
    return token;
}

PARSER(statement_loop) {
    LPTOKEN loop = alloc_token(TT_LOOP);
    while (!eat_token(p, "endloop")) {
        if (eat_token(p, "exitwhen")) {
            LPTOKEN exitwhen = statement_exitwhen(p);
            PUSH_BACK(TOKEN, exitwhen, loop->body);
        } else if (!parse_body(p, loop)) {
            FREE(loop);
            return NULL;
        }
    }
    return loop;
}

PARSER(statement_return) {
    LPTOKEN ret = alloc_token(TT_RETURN);
    ret->body = parse_logical_expression(p);
    return ret;
}

parseClass_t function_keywords[] = {
    { "set", statement_set },
    { "call", statement_call },
    { "local", statement_local },
    { "if", statement_if },
    { "loop", statement_loop },
    { "return", statement_return },
    { "exitwhen", statement_exitwhen },
    { NULL },
};

PARSER(keyword_function) {
    LPTOKEN function = parse_function_decl(p);
    while (!eat_token(p, "endfunction")) {
        if (!parse_body(p, function)) {
            FREE(function);
            return NULL;
        }
    }
    return function;
}

static parseClass_t global_keywords[] = {
    { "globals", keyword_globals },
    { "function", keyword_function },
    { "type", keyword_type },
    { "native", keyword_native },
    { "constant", keyword_constant },
    { NULL },
};

LPTOKEN JASS_ParseTokens(LPPARSER p) {
    LPTOKEN tokens = NULL;
    if (setjmp(exception_env) == 0) {
        LPTOKEN token = NULL;
        while (*peek_token(p)) {
            LPGRAMMARFUNC func = eat_keyword(p, global_keywords);
            if (func && (token = func(p))) {
                PUSH_BACK(TOKEN, token, tokens);
            } else {
                PARSER_THROW("unknwon keyword");
            }
        }
        return tokens;
    } else {
        FREE(tokens);
        fprintf(stderr, "Parser Error\n");
        return NULL;
    }
}
