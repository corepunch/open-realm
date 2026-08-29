#include "jparser.h"
#include "jass.h"
#include <setjmp.h>

#define ALLOC(type) jass_alloc(sizeof(type))
#define FREE(val) SAFE_DELETE(val, jass_free)
#define PARSER(NAME, ...) static LPTOKEN NAME(LPPARSER p, ##__VA_ARGS__)

#define PARSER_THROW(...) do { \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    fflush(stderr); \
    parser_throw(); \
    longjmp(exception_env, 1); \
} while (0)

static jmp_buf exception_env;
static BOOL c_operators;
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

static BOOL token_in(LPCSTR tok, LPCSTR const *grammar, DWORD count) {
    FOR_LOOP(i, count)
        if (!strcmp(tok, grammar[i])) return true;
    return false;
}

BOOL is_multiplicative_operator(LPCSTR str) {
    static LPCSTR const grammar[] = { "*", "/" };
    return token_in(str, grammar, sizeof(grammar) / sizeof(*grammar));
}

BOOL is_additive_operator(LPPARSER p, LPCSTR str) {
    static LPCSTR const grammar[] = { "+", "-" };
    static LPCSTR const c_grammar[] = { "<<", ">>" };
    (void)p;
        return token_in(str, grammar, sizeof(grammar) / sizeof(*grammar)) ||
            (c_operators && token_in(str, c_grammar, sizeof(c_grammar) / sizeof(*c_grammar)));
}

BOOL is_compare_operator(LPCSTR str) {
    static LPCSTR const grammar[] = { ">", "<", "==", "!=", ">=", "<=" };
    return token_in(str, grammar, sizeof(grammar) / sizeof(*grammar));
}

BOOL is_logic_operator(LPPARSER p, LPCSTR str) {
    static LPCSTR const grammar[] = { "and", "or" };
    static LPCSTR const c_grammar[] = { "&&", "||", "|", "&", "^" };
    (void)p;
        return token_in(str, grammar, sizeof(grammar) / sizeof(*grammar)) ||
            (c_operators && token_in(str, c_grammar, sizeof(c_grammar) / sizeof(*c_grammar)));
}

LPCSTR jass_getoperator(LPCSTR str) {
    static struct { LPCSTR token, name; } const grammar[] = {
        { "+", "__add" }, { "-", "__sub" }, { "*", "__mul" }, { "/", "__div" },
        { "!=", "__ne" }, { "==", "__eq" }, { ">=", "__ge" }, { "<=", "__le" },
        { ">", "__gt" }, { "<", "__lt" }, { "and", "__and" }, { "or", "__or" },
        { "&&", "__and" }, { "||", "__or" }, { "<<", "__lsh" }, { ">>", "__rsh" },
        { "|", "__bor" }, { "&", "__band" }, { "^", "__xor" },
    };
    FOR_LOOP(i, sizeof(grammar) / sizeof(*grammar))
        if (!strcmp(str, grammar[i].token)) return grammar[i].name;
    return str;
}

void parser_throw(void) {
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
    LPCSTR operatorid = jass_getoperator(op);
    LPTOKEN t = alloc_token(TT_CALL);
    t->primary = strdup(operatorid);
    return t;
}

PARSER(parse_logical_expression);

/* Preserve each Galaxy array dimension so the VM can walk nested sparse arrays. */
static void parse_array_indices(LPPARSER p, LPTOKEN token) {
    LPTOKEN *next = &token->body;
    while (eat_token(p, "[")) {
        LPTOKEN access = token;
        if (token->index) {
            access = alloc_token(TT_ARRAYACCESS);
            *next = access; next = &access->body;
        }
        access->index = parse_logical_expression(p);
    }
}

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
    } else if (eat_token(p, "not") || (c_operators && eat_token(p, "!"))) {
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
        if (!strcmp(peek_token(p), "[")) { left->type = TT_ARRAYACCESS; parse_array_indices(p, left); }
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
    if (is_additive_operator(p, peek_token(p))) {
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
    if (is_logic_operator(p, peek_token(p))) {
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
            PARSER_THROW("broken if statement");
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
    c_operators = false;
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
        p->error = true;
        fprintf(stderr, "Parser Error\n");
        return NULL;
    }
}

/* =========================================================================
 * Galaxy scripting front-end
 *
 * Galaxy is syntactically C-style JASS: same trigger/unit/player model,
 * same types under the hood.  The parser produces the same TOKEN AST so the
 * VM, coroutine scheduler, and native dispatch are completely unchanged.
 *
 * Type name mapping applied at parse time:
 *   int   → integer    fixed  → real
 *   bool  → boolean    text   → string   (all other names pass through)
 *
 * Fixed-size arrays `type[N] var` are parsed as TT_GLOBAL / TT_VARDECL with
 * TF_ARRAY set and the declared size stored in token->index.  The VM treats
 * them identically to JASS's unbounded `type array var`.
 * ========================================================================= */

static const struct { LPCSTR name, value; } galaxy_types[] = {
    { "int", "integer" }, { "bool", "boolean" }, { "fixed", "real" }, { "text", "string" }, { NULL }
};

static LPCSTR galaxy_normalize_type(LPCSTR name) {
    for (DWORD i = 0; galaxy_types[i].name; i++)
        if (!strcmp(name, galaxy_types[i].name)) return galaxy_types[i].value;
    return name;
}

/* Consume all `[N]` dimension brackets (handles 1D, 2D, 3D, …). */
static void galaxy_eat_array_dims(LPPARSER p) {
    while (eat_token(p, "[")) {
        while (!eat_token(p, "]") && *peek_token(p)) parse_token(p);
    }
}

/* Two-token lookahead: returns true when the next tokens look like
 * "type [N]* varname" rather than "ident (" or "ident =". */
static BOOL galaxy_looks_like_decl(LPPARSER p) {
    PARSER saved = *p;
    if (!is_identifier(peek_token(p))) { return false; }
    parse_token(p);          /* consume type name */
    galaxy_eat_array_dims(p);/* skip [N]+ brackets */
    LPCSTR second = peek_token(p);
    BOOL result = is_identifier(second);
    *p = saved;
    return result;
}

/* Parse a C-style `(type name, type name, ...)` parameter list.
 * Caller has already consumed the opening `(`. */
PARSER(galaxy_parse_args) {
    if (eat_token(p, ")")) return NULL;
    LPTOKEN args = NULL;
    do {
        LPTOKEN arg  = alloc_token(TT_VARDECL);
        arg->primary = strdup(galaxy_normalize_type(parse_token(p)));   /* type */
        arg->secondary = read_identifier(p);                             /* name */
        PUSH_BACK(TOKEN, arg, args);
    } while (eat_token(p, ","));
    eat_token(p, ")");
    return args;
}

/* Parse `rettype name(args)` — shared by function defs and native decls. */
PARSER(galaxy_parse_function_decl) {
    LPTOKEN token  = alloc_token(TT_FUNCTION);
    token->secondary = strdup(galaxy_normalize_type(parse_token(p)));  /* return type */
    token->primary   = read_identifier(p);                              /* name        */
    if (!token->primary) token->primary = strdup("__unnamed");
    eat_token(p, "(");
    token->args = galaxy_parse_args(p);
    return token;
}

PARSER(galaxy_statement_return) {
    LPTOKEN ret = alloc_token(TT_RETURN);
    if (!eat_token(p, ";")) {
        ret->body = parse_logical_expression(p);
        eat_token(p, ";");
    }
    return ret;
}

/* Forward decl — galaxy_statement_if calls galaxy_parse_body_stmt. */
static BOOL galaxy_parse_body_stmt(LPPARSER p, LPTOKEN function);

PARSER(galaxy_statement_if) {
    LPTOKEN token = alloc_token(TT_IF);
    token->condition = parse_logical_expression(p);
    eat_token(p, "{");
    for (;;) {
        if (eat_token(p, "}")) break;
        if (!galaxy_parse_body_stmt(p, token)) { PARSER_THROW("broken if body"); }
    }
    /* Parse optional else / else-if chain. */
    LPTOKEN *chain = &token->elseblock;
    while (eat_token(p, "else")) {
        LPTOKEN branch = alloc_token(TT_ELSE);
        if (eat_token(p, "if")) {
            branch->condition = parse_logical_expression(p);
        }
        eat_token(p, "{");
        for (;;) {
            if (eat_token(p, "}")) break;
            if (!galaxy_parse_body_stmt(p, branch)) { PARSER_THROW("broken else body"); }
        }
        *chain = branch;
        chain = &branch->elseblock;
        if (!branch->condition) break;
    }
    return token;
}

/* `while (cond) { body }` maps to TT_LOOP with an injected TT_EXITWHEN
 * as the first body statement (same layout the VM expects). */
PARSER(galaxy_statement_while) {
    LPTOKEN loop = alloc_token(TT_LOOP);
    LPTOKEN cond = parse_logical_expression(p);
    /* exitwhen !cond */
    LPTOKEN not_cond = alloc_token(TT_CALL);
    not_cond->primary = strdup("__not");
    not_cond->args    = cond;
    LPTOKEN exitwhen  = alloc_token(TT_EXITWHEN);
    exitwhen->condition = not_cond;
    PUSH_BACK(TOKEN, exitwhen, loop->body);
    eat_token(p, "{");
    for (;;) {
        if (eat_token(p, "}")) break;
        if (!galaxy_parse_body_stmt(p, loop)) { PARSER_THROW("broken while body"); }
    }
    return loop;
}

PARSER(galaxy_statement_break) {
    LPTOKEN token = alloc_token(TT_EXITWHEN);
    token->condition = alloc_token(TT_BOOLEAN);
    token->condition->primary = strdup("true");
    eat_token(p, ";");
    return token;
}

PARSER(galaxy_statement_continue) {
    /* TODO: proper continue — skip remaining loop body and re-evaluate condition.
     * For now, treat as exitwhen(false) which is a parse-safe no-op at runtime. */
    LPTOKEN token = alloc_token(TT_EXITWHEN);
    token->condition = alloc_token(TT_BOOLEAN);
    token->condition->primary = strdup("false");
    eat_token(p, ";");
    return token;
}

/* Parse a local variable declaration: `type [N]* name [= expr];` */
PARSER(galaxy_parse_local) {
    LPTOKEN token    = alloc_token(TT_VARDECL);
    token->primary   = strdup(galaxy_normalize_type(parse_token(p)));
    if (eat_token(p, "[")) {
        token->flags |= TF_ARRAY;
        token->index  = alloc_token(TT_INTEGER);
        token->index->primary = strdup(parse_token(p));  /* first dimension size */
        eat_token(p, "]");
        /* Skip additional dimensions (2D, 3D, …) — flattened in JASS VM. */
        while (eat_token(p, "[")) { parse_token(p); eat_token(p, "]"); }
    }
    token->secondary = read_identifier(p);
    if (eat_token(p, "=")) {
        token->init = parse_logical_expression(p);
        eat_token(p, ";");
    } else {
        eat_token(p, ";");
    }
    return token;
}

/* Parse an assignment (`name [idx] = expr;`) or a call expression (`name(args);`). */
static LPTOKEN galaxy_parse_expression_stmt(LPPARSER p) {
    /* strdup immediately: parse_token returns a pointer to a static buffer that
     * subsequent eat_token / parse_token calls will overwrite. */
    LPSTR name = strdup(parse_token(p));

    /* Preserve every array index before the assignment operator. */
    TOKEN indices = { 0 };
    parse_array_indices(p, &indices);

    LPTOKEN result = NULL;
    if (eat_token(p, "=")) {
        LPTOKEN token    = alloc_token(TT_SET);
        token->secondary = name;   /* transfer ownership */
        token->index     = indices.index;
        token->body      = indices.body;
        token->init      = parse_logical_expression(p);
        eat_token(p, ";");
        result = token;
    } else if (eat_token(p, "(")) {
        LPTOKEN token  = alloc_token(TT_CALL);
        token->primary = name;   /* transfer ownership */
        if (!eat_token(p, ")")) {
            token->args = parse_logical_expression(p);  /* eats ) */
        }
        eat_token(p, ";");
        result = token;
    } else {
        eat_token(p, ";");
        LPTOKEN token  = alloc_token(TT_IDENTIFIER);
        token->primary = name;   /* transfer ownership */
        result = token;
    }
    return result;
}

/* Dispatch one statement inside a function body. */
static BOOL galaxy_parse_body_stmt(LPPARSER p, LPTOKEN function) {
    static parseClass_t statements[] = {
        { "if", galaxy_statement_if }, { "while", galaxy_statement_while }, { "return", galaxy_statement_return },
        { "break", galaxy_statement_break }, { "continue", galaxy_statement_continue }, { NULL }
    };
    if (!*peek_token(p)) return false;
    LPGRAMMARFUNC func = eat_keyword(p, statements);
    LPTOKEN stmt = NULL;
    if (func) {
        stmt = func(p);
    } else if (galaxy_looks_like_decl(p)) {
        stmt = galaxy_parse_local(p);
    } else {
        stmt = galaxy_parse_expression_stmt(p);
    }

    if (stmt) { PUSH_BACK(TOKEN, stmt, function->body); return true; }
    return false;
}

/* Skip all tokens up to and including the matching closing brace. */
static void galaxy_skip_function_body(LPPARSER p) {
    int depth = 1;
    LPCSTR tok;
    while (depth > 0 && *(tok = peek_token(p))) {
        parse_token(p);
        if (!strcmp(tok, "{")) depth++;
        else if (!strcmp(tok, "}")) depth--;
    }
}

/* `rettype name(args) { body }` — recovers from statement parse failures.
 * Galaxy forward declarations (`rettype name(args);`) are registered as
 * empty-body functions.  When the full implementation follows later in the
 * same file, find_function() returns the first match (the forward decl stub),
 * which effectively stubs out heavy CampaignLib/NativeLib initialisation that
 * the cutscene doesn't need.  MapScript.galaxy functions have full bodies only,
 * so they are always found correctly. */
PARSER(galaxy_keyword_function) {
    LPTOKEN function = galaxy_parse_function_decl(p);
    if (!eat_token(p, "{")) {
        /* Forward declaration — no body, empty function stub. */
        eat_token(p, ";");
        return function;
    }
    for (;;) {
        if (eat_token(p, "}")) break;
        if (!galaxy_parse_body_stmt(p, function)) {
            /* One statement failed; skip to the end of this function body. */
            fprintf(stderr, "broken function body\n");
            galaxy_skip_function_body(p);
            break;
        }
    }
    return function;
}

/* Top-level `[const] type [N]* name [= expr];` */
PARSER(galaxy_parse_global) {
    LPTOKEN token = alloc_token(TT_GLOBAL);
    if (eat_token(p, "const")) token->flags |= TF_CONSTANT;
    token->primary = strdup(galaxy_normalize_type(parse_token(p)));
    if (eat_token(p, "[")) {
        token->flags |= TF_ARRAY;
        token->index  = alloc_token(TT_INTEGER);
        token->index->primary = strdup(parse_token(p));
        eat_token(p, "]");
        while (eat_token(p, "[")) { parse_token(p); eat_token(p, "]"); }  /* N-D: skip extra */
    }
    token->secondary = read_identifier(p);
    if (!token->secondary) {
        /* Recovery: no valid name — skip to end of statement. */
        while (*peek_token(p) && strcmp(peek_token(p), ";")) parse_token(p);
        eat_token(p, ";");
        FREE(token);
        return NULL;
    }
    if (eat_token(p, "=")) {
        token->init = parse_logical_expression(p);
        eat_token(p, ";");
    } else {
        eat_token(p, ";");
    }
    return token;
}

/* `native rettype name(args);` */
PARSER(galaxy_parse_native) {
    LPTOKEN token = galaxy_parse_function_decl(p);
    token->flags |= TF_NATIVE;
    eat_token(p, ";");
    return token;
}

/* Peek ahead to decide: top-level function definition vs global variable.
 * If after (optional `[N]`) we see `name (`, it's a function; otherwise global. */
static LPTOKEN galaxy_parse_global_or_func(LPPARSER p) {
    PARSER saved = *p;
    parse_token(p);              /* consume return/type name */
    galaxy_eat_array_dims(p);   /* skip [N]+ brackets */
    parse_token(p);              /* consume function/variable name */
    BOOL is_func = eat_token(p, "(");
    *p = saved;
    return is_func ? galaxy_keyword_function(p) : galaxy_parse_global(p);
}

LPTOKEN GALAXY_ParseTokens(LPPARSER p) {
    c_operators = true;
    LPTOKEN tokens = NULL;
    if (setjmp(exception_env) == 0) {
        while (*peek_token(p)) {
            LPTOKEN token = NULL;
            LPCSTR  tok   = peek_token(p);
            if (!strcmp(tok, "native")) {
                parse_token(p);
                token = galaxy_parse_native(p);
            } else if (!strcmp(tok, "const")) {
                token = galaxy_parse_global(p);
            } else if (is_identifier(tok)) {
                token = galaxy_parse_global_or_func(p);
            } else {
                /* Unrecognized top-level token — skip until we find a `;` or
                 * until the next token looks like the start of a declaration.
                 * This handles stray tokens left by partial expression parsing. */
                while (*peek_token(p) && !is_identifier(peek_token(p)) &&
                       strcmp(peek_token(p), "native") &&
                       strcmp(peek_token(p), "const")) {
                    parse_token(p);
                }
            }
            if (token) { PUSH_BACK(TOKEN, token, tokens); }
        }
        return tokens;
    } else {
        FREE(tokens);
        p->error = true;
        return NULL;
    }
}

