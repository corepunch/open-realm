#include "jparser.h"
#include "jass.h"
#include <setjmp.h>

#define ALLOC(type) jass_alloc(sizeof(type))
#define FREE(val) SAFE_DELETE(val, jass_free)
#define wordExtractor_t(NAME, ...) static token_t * NAME(wordExtractor_t * p, ##__VA_ARGS__)

#define PARSER_THROW(...) do { \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    fflush(stderr); \
    parser_throw(); \
    longjmp(exception_env, 1); \
} while (0)

static jmp_buf exception_env;
static bool c_operators;
typedef token_t * (*grammarFunc_t)(wordExtractor_t *);

typedef struct {
    cstring_t name;
    grammarFunc_t func;
} parseClass_t;

extern parseClass_t function_keywords[];

bool is_integer(cstring_t tok);
bool is_float(cstring_t tok);
bool is_identifier(cstring_t str);
bool is_string(cstring_t tok);
bool is_fourcc(cstring_t tok);

static bool token_in(cstring_t tok, cstring_t const *grammar, uint32_t count) {
    FOR_LOOP(i, count)
        if (!strcmp(tok, grammar[i])) return true;
    return false;
}

static uint32_t parser_line(wordExtractor_t * p) {
    uint32_t line = 1;
    for (cstring_t cur = p->start; cur && cur < p->buffer; cur++) {
        if (*cur == '\n' || *cur == '\r') {
            if (*cur == '\r' && cur[1] == '\n') cur++;
            line++;
        }
    }
    return line;
}

bool is_multiplicative_operator(cstring_t str) {
    static cstring_t const grammar[] = { "*", "/" };
    return token_in(str, grammar, sizeof(grammar) / sizeof(*grammar));
}

bool is_additive_operator(wordExtractor_t * p, cstring_t str) {
    static cstring_t const grammar[] = { "+", "-" };
    static cstring_t const c_grammar[] = { "<<", ">>" };
    (void)p;
        return token_in(str, grammar, sizeof(grammar) / sizeof(*grammar)) ||
            (c_operators && token_in(str, c_grammar, sizeof(c_grammar) / sizeof(*c_grammar)));
}

bool is_compare_operator(cstring_t str) {
    static cstring_t const grammar[] = { ">", "<", "==", "!=", ">=", "<=" };
    return token_in(str, grammar, sizeof(grammar) / sizeof(*grammar));
}

bool is_logic_operator(wordExtractor_t * p, cstring_t str) {
    static cstring_t const grammar[] = { "and", "or" };
    static cstring_t const c_grammar[] = { "&&", "||", "|", "&", "^" };
    (void)p;
        return token_in(str, grammar, sizeof(grammar) / sizeof(*grammar)) ||
            (c_operators && token_in(str, c_grammar, sizeof(c_grammar) / sizeof(*c_grammar)));
}

cstring_t jass_getoperator(cstring_t str) {
    static struct { cstring_t token, name; } const grammar[] = {
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

string_t read_identifier(wordExtractor_t * p) {
    if (is_identifier(peek_token(p))) {
        return strdup(parse_token(p));
    } else {
        return NULL;
    }
}

static grammarFunc_t eat_keyword(wordExtractor_t * p, parseClass_t *keywords) {
    for (parseClass_t *cl = keywords; cl->name; cl++) {
        if (eat_token(p, cl->name)) {
            return cl->func;
        }
    }
    return NULL;
}

static bool parse_body(wordExtractor_t * p, token_t * function) {
    token_t * token = NULL;
    grammarFunc_t func = eat_keyword(p, function_keywords);
    if (func && (token = func(p))) {
        PUSH_BACK(token_t, token, function->body);
    } else {
        PARSER_THROW("error parsing function at line %u near '%s'", parser_line(p), peek_token(p));
    }
    return true;
}

static token_t * alloc_token(TOKENTYPE type) {
    token_t * token = ALLOC(token_t);
    token->type = type;
    return token;
}

/* Parsed programs own every AST edge and string; runtime declarations borrow those strings until VM close. */
void JASS_FreeTokens(token_t * tokens) {
    while (tokens) {
        token_t * next = tokens->next;
        JASS_FreeTokens(tokens->init);
        JASS_FreeTokens(tokens->body);
        JASS_FreeTokens(tokens->args);
        JASS_FreeTokens(tokens->condition);
        JASS_FreeTokens(tokens->elseblock);
        JASS_FreeTokens(tokens->index);
        free(tokens->primary);
        free(tokens->secondary);
        jass_free(tokens);
        tokens = next;
    }
}

//PARSER(parse_identifier) {
//    token_t * token = alloc_token(TT_IDENTIFIER);
//    token->primary = read_identifier(p);
//    return token;
//}

wordExtractor_t(keyword_type) {
    token_t * token = alloc_token(TT_TYPEDEF);
    token->primary = read_identifier(p);
    if (eat_token(p, "extends")) {
        token->secondary = read_identifier(p);
    } else {
        PARSER_THROW("EXTENDS expected");
    }
    return token;
}

wordExtractor_t(parse_args) {
    if (eat_token(p, "nothing")) {
        return NULL;
    }
    token_t * args = NULL;
    while (!args || eat_token(p, ",")) {
        token_t * arg = alloc_token(TT_VARDECL);
        arg->primary = read_identifier(p);
        arg->secondary = read_identifier(p);
        PUSH_BACK(token_t, arg, args);
    }
    return args;
}

wordExtractor_t(parse_function_decl) {
    token_t * token = alloc_token(TT_FUNCTION);
    token->primary = read_identifier(p);
    if (eat_token(p, "takes")) {
        token->args = parse_args(p);
    }
    if (eat_token(p, "returns")) {
        token->secondary = read_identifier(p);
    }
    return token;
}

wordExtractor_t(keyword_function);

wordExtractor_t(keyword_native) {
    token_t * token = parse_function_decl(p);
    token->flags |= TF_NATIVE;
    return token;
}

wordExtractor_t(keyword_constant) {
    if (eat_token(p, "native")) {
        token_t * token = keyword_native(p);
        token->flags |= TF_CONSTANT;
        return token;
    }
    if (eat_token(p, "function")) {
        token_t * token = keyword_function(p);
        token->flags |= TF_CONSTANT;
        return token;
    }
    PARSER_THROW("expected native or function after constant");
}

static void jass_remove_quotes(string_t str, char quote) {
    size_t len = strlen(str);
    if (len >= 2 && str[0] == quote && str[len - 1] == quote) {
        memmove(str, str + 1, len - 2);
        str[len - 2] = '\0';
    }
}

token_t * alloc_ident_token(wordExtractor_t * p, TOKENTYPE tt) {
    token_t * t = alloc_token(tt);
    t->primary = strdup(parse_token(p));
    return t;
}

token_t * parse_operator_token(wordExtractor_t * p) {
    UINAME op = { 0 };
    strlcpy(op, parse_token(p), sizeof(op));
    cstring_t operatorid = jass_getoperator(op);
    token_t * t = alloc_token(TT_CALL);
    t->primary = strdup(operatorid);
    return t;
}

wordExtractor_t(parse_logical_expression);

/* Preserve each Galaxy array dimension so the VM can walk nested sparse arrays. */
static void parse_array_indices(wordExtractor_t * p, token_t * token) {
    token_t * *next = &token->body;
    while (eat_token(p, "[")) {
        token_t * access = token;
        if (token->index) {
            access = alloc_token(TT_ARRAYACCESS);
            *next = access; next = &access->body;
        }
        access->index = parse_logical_expression(p);
    }
}

wordExtractor_t(read_single_identifier) {
    cstring_t tok = peek_token(p);
    token_t * left = NULL;
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

wordExtractor_t(parse_multiplicative_expression) {
    token_t * left = read_single_identifier(p);
    if (is_multiplicative_operator(peek_token(p))) {
        token_t * oper = parse_operator_token(p);
        token_t * right = parse_multiplicative_expression(p);
        PUSH_BACK(token_t, left, oper->args);
        PUSH_BACK(token_t, right, oper->args);
        return oper;
    }
    return left;
}

wordExtractor_t(parse_additive_expression) {
    token_t * left = parse_multiplicative_expression(p);
    if (is_additive_operator(p, peek_token(p))) {
        token_t * oper = parse_operator_token(p);
        token_t * right = parse_additive_expression(p);
        PUSH_BACK(token_t, left, oper->args);
        PUSH_BACK(token_t, right, oper->args);
        return oper;
    }
    return left;
}

wordExtractor_t(parse_comparison_expression) {
    token_t * left = parse_additive_expression(p);
    if (is_compare_operator(peek_token(p))) {
        token_t * oper = parse_operator_token(p);
        token_t * right = parse_comparison_expression(p);
        PUSH_BACK(token_t, left, oper->args);
        PUSH_BACK(token_t, right, oper->args);
        return oper;
    }
    return left;
}

wordExtractor_t(parse_logical_expression) {
    token_t * left = parse_comparison_expression(p);
    if (is_logic_operator(p, peek_token(p))) {
        token_t * oper = parse_operator_token(p);
        token_t * right = parse_logical_expression(p);
        PUSH_BACK(token_t, left, oper->args);
        PUSH_BACK(token_t, right, oper->args);
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

wordExtractor_t(keyword_globals) {
    token_t * globals = NULL;
    while (!eat_token(p, "endglobals")) {
        token_t * token = alloc_token(TT_GLOBAL);
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
        PUSH_BACK(token_t, token, globals);
    }
    return globals;
}

wordExtractor_t(statement_set) {
    token_t * token = alloc_token(TT_SET);
    token->secondary = read_identifier(p);
    if (eat_token(p, "[")) {
        token->index = parse_logical_expression(p);
    }
    if (eat_token(p, "=")) {
        token->init = parse_logical_expression(p);
    }
    return token;
}

wordExtractor_t(statement_call) {
    return parse_logical_expression(p);
}

wordExtractor_t(statement_local) {
    token_t * token = alloc_token(TT_VARDECL);
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

wordExtractor_t(statement_if) {
    token_t * token = alloc_token(TT_IF);
    token_t * target = token;
    token->condition = parse_logical_expression(p);
    if (!eat_token(p, "then")) {
        FREE(token);
        PARSER_THROW("THEN expected at line %u near '%s'", parser_line(p), peek_token(p));
    }
    while (!eat_token(p, "endif")) {
        if (eat_token(p, "elseif")) {
            token_t * next = alloc_token(TT_ELSE);
            next->condition = parse_logical_expression(p);
            if (!eat_token(p, "then")) {
                FREE(token);
                PARSER_THROW("THEN expected at line %u near '%s'", parser_line(p), peek_token(p));
            }
            target->elseblock = next;
            target = next;
        } else if (eat_token(p, "else")) {
            token_t * next = alloc_token(TT_ELSE);
            target->elseblock = next;
            target = next;
        } else if (!parse_body(p, target)) {
            FREE(token);
            PARSER_THROW("broken if statement");
        }
    }
    return token;
}

wordExtractor_t(statement_exitwhen) {
    token_t * token = alloc_token(TT_EXITWHEN);
    token->condition = parse_logical_expression(p);
    return token;
}

wordExtractor_t(statement_loop) {
    token_t * loop = alloc_token(TT_LOOP);
    while (!eat_token(p, "endloop")) {
        if (eat_token(p, "exitwhen")) {
            token_t * exitwhen = statement_exitwhen(p);
            PUSH_BACK(token_t, exitwhen, loop->body);
        } else if (!parse_body(p, loop)) {
            FREE(loop);
            return NULL;
        }
    }
    return loop;
}

wordExtractor_t(statement_return) {
    token_t * ret = alloc_token(TT_RETURN);
    ret->body = parse_logical_expression(p);
    return ret;
}

/* Retail JASS parses debug-prefixed statements but excludes them from release execution. */
wordExtractor_t(statement_debug) {
    grammarFunc_t func = eat_keyword(p, function_keywords);
    token_t * token = func ? func(p) : NULL;
    if (!token) PARSER_THROW("invalid debug statement at line %u near '%s'", parser_line(p), peek_token(p));
    token->flags |= TF_DEBUG;
    return token;
}

parseClass_t function_keywords[] = {
    { "set", statement_set },
    { "call", statement_call },
    { "local", statement_local },
    { "if", statement_if },
    { "loop", statement_loop },
    { "return", statement_return },
    { "exitwhen", statement_exitwhen },
    { "debug", statement_debug },
    { NULL },
};

wordExtractor_t(keyword_function) {
    token_t * function = parse_function_decl(p);
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

token_t * JASS_ParseTokens(wordExtractor_t * p) {
    c_operators = false;
    token_t * tokens = NULL;
    if (setjmp(exception_env) == 0) {
        token_t * token = NULL;
        while (*peek_token(p)) {
            grammarFunc_t func = eat_keyword(p, global_keywords);
            if (!func) {
                PARSER_THROW("unknown keyword");
            }
            /* An empty globals/endglobals block yields no declarations.
             * It is valid (retail AI scripts use it) and contributes no tokens. */
            token = func(p);
            if (token) {
                PUSH_BACK(token_t, token, tokens);
            }
        }
        return tokens;
    } else {
        JASS_FreeTokens(tokens);
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

static const struct { cstring_t name, value; } galaxy_types[] = {
    { "int", "integer" }, { "bool", "boolean" }, { "fixed", "real" }, { "text", "string" }, { NULL }
};

static cstring_t galaxy_normalize_type(cstring_t name) {
    for (uint32_t i = 0; galaxy_types[i].name; i++)
        if (!strcmp(name, galaxy_types[i].name)) return galaxy_types[i].value;
    return name;
}

/* Consume all `[N]` dimension brackets (handles 1D, 2D, 3D, …). */
static void galaxy_eat_array_dims(wordExtractor_t * p) {
    while (eat_token(p, "[")) {
        while (!eat_token(p, "]") && *peek_token(p)) parse_token(p);
    }
}

/* Two-token lookahead: returns true when the next tokens look like
 * "type [N]* varname" rather than "ident (" or "ident =". */
static bool galaxy_looks_like_decl(wordExtractor_t * p) {
    wordExtractor_t saved = *p;
    if (!is_identifier(peek_token(p))) { return false; }
    parse_token(p);          /* consume type name */
    galaxy_eat_array_dims(p);/* skip [N]+ brackets */
    cstring_t second = peek_token(p);
    bool result = is_identifier(second);
    *p = saved;
    return result;
}

/* Parse a C-style `(type name, type name, ...)` parameter list.
 * Caller has already consumed the opening `(`. */
wordExtractor_t(galaxy_parse_args) {
    if (eat_token(p, ")")) return NULL;
    token_t * args = NULL;
    do {
        token_t * arg  = alloc_token(TT_VARDECL);
        arg->primary = strdup(galaxy_normalize_type(parse_token(p)));   /* type */
        arg->secondary = read_identifier(p);                             /* name */
        PUSH_BACK(token_t, arg, args);
    } while (eat_token(p, ","));
    eat_token(p, ")");
    return args;
}

/* Parse `rettype name(args)` — shared by function defs and native decls. */
wordExtractor_t(galaxy_parse_function_decl) {
    token_t * token  = alloc_token(TT_FUNCTION);
    token->secondary = strdup(galaxy_normalize_type(parse_token(p)));  /* return type */
    token->primary   = read_identifier(p);                              /* name        */
    if (!token->primary) token->primary = strdup("__unnamed");
    eat_token(p, "(");
    token->args = galaxy_parse_args(p);
    return token;
}

wordExtractor_t(galaxy_statement_return) {
    token_t * ret = alloc_token(TT_RETURN);
    if (!eat_token(p, ";")) {
        ret->body = parse_logical_expression(p);
        eat_token(p, ";");
    }
    return ret;
}

/* Forward decl — galaxy_statement_if calls galaxy_parse_body_stmt. */
static bool galaxy_parse_body_stmt(wordExtractor_t * p, token_t * function);

wordExtractor_t(galaxy_statement_if) {
    token_t * token = alloc_token(TT_IF);
    token->condition = parse_logical_expression(p);
    eat_token(p, "{");
    for (;;) {
        if (eat_token(p, "}")) break;
        if (!galaxy_parse_body_stmt(p, token)) { PARSER_THROW("broken if body"); }
    }
    /* Parse optional else / else-if chain. */
    token_t * *chain = &token->elseblock;
    while (eat_token(p, "else")) {
        token_t * branch = alloc_token(TT_ELSE);
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
wordExtractor_t(galaxy_statement_while) {
    token_t * loop = alloc_token(TT_LOOP);
    token_t * cond = parse_logical_expression(p);
    /* exitwhen !cond */
    token_t * not_cond = alloc_token(TT_CALL);
    not_cond->primary = strdup("__not");
    not_cond->args    = cond;
    token_t * exitwhen  = alloc_token(TT_EXITWHEN);
    exitwhen->condition = not_cond;
    PUSH_BACK(token_t, exitwhen, loop->body);
    eat_token(p, "{");
    for (;;) {
        if (eat_token(p, "}")) break;
        if (!galaxy_parse_body_stmt(p, loop)) { PARSER_THROW("broken while body"); }
    }
    return loop;
}

wordExtractor_t(galaxy_statement_break) {
    token_t * token = alloc_token(TT_EXITWHEN);
    token->condition = alloc_token(TT_BOOLEAN);
    token->condition->primary = strdup("true");
    eat_token(p, ";");
    return token;
}

wordExtractor_t(galaxy_statement_continue) {
    /* TODO: proper continue — skip remaining loop body and re-evaluate condition.
     * For now, treat as exitwhen(false) which is a parse-safe no-op at runtime. */
    token_t * token = alloc_token(TT_EXITWHEN);
    token->condition = alloc_token(TT_BOOLEAN);
    token->condition->primary = strdup("false");
    eat_token(p, ";");
    return token;
}

/* Parse a local variable declaration: `[const] type [N]* name [= expr];` */
wordExtractor_t(galaxy_parse_local) {
    token_t * token    = alloc_token(TT_VARDECL);
    /* Consume the qualifier first; previously const int declared a variable named int and lost the real name. */
    if (eat_token(p, "const")) token->flags |= TF_CONSTANT;
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
static token_t * galaxy_parse_expression_stmt(wordExtractor_t * p) {
    /* strdup immediately: parse_token returns a pointer to a static buffer that
     * subsequent eat_token / parse_token calls will overwrite. */
    string_t name = strdup(parse_token(p));

    /* Preserve every array index before the assignment operator. */
    token_t indices = { 0 };
    parse_array_indices(p, &indices);

    token_t * result = NULL;
    if (eat_token(p, "=")) {
        token_t * token    = alloc_token(TT_SET);
        token->secondary = name;   /* transfer ownership */
        token->index     = indices.index;
        token->body      = indices.body;
        token->init      = parse_logical_expression(p);
        eat_token(p, ";");
        result = token;
    } else if (eat_token(p, "(")) {
        token_t * token  = alloc_token(TT_CALL);
        token->primary = name;   /* transfer ownership */
        if (!eat_token(p, ")")) {
            token->args = parse_logical_expression(p);  /* eats ) */
        }
        eat_token(p, ";");
        result = token;
    } else {
        eat_token(p, ";");
        token_t * token  = alloc_token(TT_IDENTIFIER);
        token->primary = name;   /* transfer ownership */
        result = token;
    }
    return result;
}

/* Dispatch one statement inside a function body. */
static bool galaxy_parse_body_stmt(wordExtractor_t * p, token_t * function) {
    static parseClass_t statements[] = {
        { "if", galaxy_statement_if }, { "while", galaxy_statement_while }, { "return", galaxy_statement_return },
        { "break", galaxy_statement_break }, { "continue", galaxy_statement_continue }, { NULL }
    };
    if (!*peek_token(p)) return false;
    grammarFunc_t func = eat_keyword(p, statements);
    token_t * stmt = NULL;
    if (func) {
        stmt = func(p);
    } else if (galaxy_looks_like_decl(p)) {
        stmt = galaxy_parse_local(p);
    } else {
        stmt = galaxy_parse_expression_stmt(p);
    }

    if (stmt) { PUSH_BACK(token_t, stmt, function->body); return true; }
    return false;
}

/* Skip all tokens up to and including the matching closing brace. */
static void galaxy_skip_function_body(wordExtractor_t * p) {
    int depth = 1;
    cstring_t tok;
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
wordExtractor_t(galaxy_keyword_function) {
    token_t * function = galaxy_parse_function_decl(p);
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
wordExtractor_t(galaxy_parse_global) {
    token_t * token = alloc_token(TT_GLOBAL);
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
wordExtractor_t(galaxy_parse_native) {
    token_t * token = galaxy_parse_function_decl(p);
    token->flags |= TF_NATIVE;
    eat_token(p, ";");
    return token;
}

/* Peek ahead to decide: top-level function definition vs global variable.
 * If after (optional `[N]`) we see `name (`, it's a function; otherwise global. */
static token_t * galaxy_parse_global_or_func(wordExtractor_t * p) {
    wordExtractor_t saved = *p;
    parse_token(p);              /* consume return/type name */
    galaxy_eat_array_dims(p);   /* skip [N]+ brackets */
    parse_token(p);              /* consume function/variable name */
    bool is_func = eat_token(p, "(");
    *p = saved;
    return is_func ? galaxy_keyword_function(p) : galaxy_parse_global(p);
}

token_t * GALAXY_ParseTokens(wordExtractor_t * p) {
    c_operators = true;
    token_t * tokens = NULL;
    if (setjmp(exception_env) == 0) {
        while (*peek_token(p)) {
            token_t * token = NULL;
            cstring_t  tok   = peek_token(p);
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
            if (token) { PUSH_BACK(token_t, token, tokens); }
        }
        return tokens;
    } else {
        JASS_FreeTokens(tokens);
        p->error = true;
        return NULL;
    }
}

