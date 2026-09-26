#include "jass.h"
#include "jvm.h"
#include "jopcodes.h"
#include "jparser.h"

#define TOKENFUNC(NAME) void write_##NAME(writer_t *w, token_t const *t)
#define TOKENEVAL(NAME) { #NAME, TT_##NAME, write_##NAME }

KNOWN_AS(vmWriter, writer_t);

typedef char vmBuffer_t[1024 * 1024];

static vmBuffer_t vmBuffers[4];

struct vmBuffer {
    string_t data;
    uint32_t writecount;
    uint32_t buffersize;
};

struct vmWriter {
    struct vmBuffer global;
    struct vmBuffer data;
    struct vmBuffer text;
    struct vmBuffer init;
};

static bool jcode_atob(cstring_t str) {
    return !strcmp(str, "true");
}

void VM_Write(struct vmBuffer *buffer, char const *format, ...) {
    assert(buffer->writecount < buffer->buffersize);
    va_list args;
    va_start(args, format);
    vsnprintf(buffer->data + buffer->writecount, buffer->buffersize - buffer->writecount, format, args);
    va_end(args);
    uint32_t str_len = (uint32_t)strlen(buffer->data + buffer->writecount);
    buffer->data[buffer->writecount + str_len++] = '\n';
    buffer->writecount += str_len;
}

TOKENFUNC(RegularToken);
TOKENFUNC(TOKENS);
TOKENFUNC(SINGLETOKEN);

TOKENFUNC(Integer) {
    VM_Write(&w->text, "\tpushi %d", atoi(t->primary));
}

TOKENFUNC(Real) {
    VM_Write(&w->text, "\tpushf %f", atof(t->primary));
}

TOKENFUNC(String) {
//    return jass_pushstring(j, t->primary);
}

TOKENFUNC(Boolean) {
    VM_Write(&w->text, "\tpushb %d", jcode_atob(t->primary));

//    return jass_pushboolean(j, jcode_atob(t->primary));
}

TOKENFUNC(Identifier) {
//    jassFunc_t const *f = NULL;
//    jassVar_t const *v = NULL;
//    if (t->flags & TF_FUNCTION) {
//        if ((f = find_function(j, t->primary))) {
//            return jass_pushfunction(j, f);
//        } else {
//            return jass_pushnull(j);
//        }
//    } else if ((v = find_dict(j->globals, t->primary))) {
//        return jass_pushvalue(j, v);
//    } else if ((v = find_dict(jass_stackvalue(j, 0)->env.locals, t->primary))) {
//        return jass_pushvalue(j, v);
//    } else {
//        return jass_pushnull(j);
//    }
}

TOKENFUNC(FourCC) {
//    return jass_pushinteger(j, *(uint32_t *)t->primary);
}

TOKENFUNC(Call) {
    uint32_t num_args = 0;
    FOR_EACH_LIST(token_t, arg, t->args) {
        write_RegularToken(w, arg);
        num_args++;
    }
//    if (!strcmp(t->primary, "__mul")) {
//        VM_Write(&w->text, "\timul");
//    } else {
    VM_Write(&w->text, "\tbl _%s, %d", t->primary, num_args);
//    }

//    jassFunc_t const *f = NULL;
//    jassCFunction_t cf = NULL;
//    uint32_t stacksize = j->num_stack;
//    if (!strcmp(t->primary, "CommentString") && t->args) {
//        fprintf(stdout, "%s\n", t->args->primary);
//        return 0;
//    } else if ((f = find_function(j, t->primary))) {
//        uint32_t args = 0;
//        jass_pushfunction(j, f);
//        FOR_EACH_LIST(TOKEN, arg, t->args) {
//            jass_dotoken(j, arg);
//            args++;
//        }
//        jass_call(j, args);
//        return j->num_stack - stacksize;
//    } else if ((cf = find_cfunction(j, t->primary))) {
//        uint32_t args = 0;
//        jass_pushcfunction(j, cf);
//        FOR_EACH_LIST(TOKEN, arg, t->args) {
//            jass_dotoken(j, arg);
//            args++;
//        }
//        jass_call(j, args);
//        return j->num_stack - stacksize;
//    } else {
//        fprintf(stderr, "Can't find function %s\n", t->primary);
//        assert(false);
//        return 0;
//    }
//    return 0;
}


static struct {
    TOKENTYPE tokentype;
    void (*func)(writer_t *w, token_t const *t);
} compiler_token_types[] = {
    { TT_INTEGER, write_Integer },
    { TT_REAL, write_Real },
    { TT_STRING, write_String },
    { TT_BOOLEAN, write_Boolean },
    { TT_IDENTIFIER, write_Identifier },
    { TT_FOURCC, write_FourCC },
    { TT_CALL, write_Call },
};

TOKENFUNC(RegularToken) {
    if (!t) return;
    FOR_LOOP(idx, sizeof(compiler_token_types)/sizeof(*compiler_token_types)) {
        if (compiler_token_types[idx].tokentype == t->type) {
            compiler_token_types[idx].func(w, t);
            return;
        }
    }
//    assert(false);
    return;
}

TOKENFUNC(TYPEDEF) {
    VM_Write(&w->global, "_%s:", t->primary);
    VM_Write(&w->global, "\t.quad _%s", t->secondary);
    VM_Write(&w->global, "\t.quad 0");
    VM_Write(&w->global, "\t.asciz \"%s\"", t->primary);
}

void VM_InitValue(writer_t *w, token_t const *t, cstring_t name) {
    uint32_t start = w->text.writecount;
    write_RegularToken(w, t);
    uint32_t diff = w->text.writecount - start;
    memcpy(w->init.data + w->init.writecount, w->text.data + start, diff + 1);
    w->text.data[start] = '\0';
    w->text.writecount = start;
    w->init.writecount += diff;
    VM_Write(&w->init, "\tstr _%s", name);
}

TOKENFUNC(GLOBAL) {
    VM_Write(&w->global, "\t.globl _%s", t->secondary);
    VM_Write(&w->data, "_%s:", t->secondary);
    VM_Write(&w->data, "\t.quad 0");
    VM_Write(&w->data, "\t.quad _%s", t->primary);
    if (t->init) {
        VM_InitValue(w, t->init, t->secondary);
    }
}

TOKENFUNC(FUNCTION) {
    if (t->flags & TF_NATIVE) {
        VM_Write(&w->global, "\t.native %s", t->primary);
    } else {
        VM_Write(&w->global, "\t.globl %s", t->primary);
    }
//    VM_Write(&w->data, ".%s 0", t->primary);
}

struct {
    cstring_t name;
    TOKENTYPE type;
    void (*func)(writer_t *, token_t const *);
} token_writers[] = {
    TOKENEVAL(TYPEDEF),
    TOKENEVAL(FUNCTION),
//    TOKENEVAL(VARDECL),
    TOKENEVAL(GLOBAL),
//    TOKENEVAL(CALL),
//    TOKENEVAL(IF),
//    TOKENEVAL(SET),
//    TOKENEVAL(LOOP),
};

TOKENFUNC(SINGLETOKEN) {
    FOR_LOOP(index, sizeof(token_writers) / sizeof(*token_writers)) {
        if (t->type == token_writers[index].type) {
            token_writers[index].func(w, t);
            return;
        }
    }
    fprintf(stderr, "Can't evaluate t of type %d\n", t->type);
    assert(false);
}

TOKENFUNC(TOKENS) {
    FOR_EACH_LIST(token_t const, tok, t) {
//        if (tok->type == TT_RETURN) {
//            jass_setreturn(j);
//            jass_dotoken(j, tok->body);
//        } else {
            write_SINGLETOKEN(w, tok);
//        }
    }
}

vmprogram_t VM_Compile(token_t const *t) {
    memset(vmBuffers, 0, sizeof(vmBuffers));
    writer_t w = {
        .global = {
            .data = vmBuffers[0],
            .writecount = 0,
            .buffersize = sizeof(vmBuffers[0]),
        },
        .data = {
            .data = vmBuffers[1],
            .writecount = 0,
            .buffersize = sizeof(vmBuffers[1]),
        },
        .text = {
            .data = vmBuffers[2],
            .writecount = 0,
            .buffersize = sizeof(vmBuffers[2]),
        },
        .init = {
            .data = vmBuffers[3],
            .writecount = 0,
            .buffersize = sizeof(vmBuffers[3]),
        }
    };
    write_TOKENS(&w, t);
    vmprogram_t program = { vmBuffers[0], 0 };
    return program;
}

#undef TOKENFUNC
#undef TOKENEVAL
