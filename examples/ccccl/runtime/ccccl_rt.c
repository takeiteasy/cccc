/* ccccl_rt.c — the runtime universe. Plain C, no cccc dependency. */
#include "ccccl_rt_internal.h"

#include <stdlib.h>
#include <string.h>

static LObj g_cell_arena[CCCCL_RT_ARENA_CELLS];
static int  g_cell_next = 0;

static LObj g_sym_arena[CCCCL_RT_MAX_SYMS];
static int  g_sym_next = 0;

/* Backing storage for interned atom names (see the comment on LObjAtom in
 * ccccl_rt_internal.h for why this is a pointer into a pool rather than a
 * fixed-size buffer inside LObj itself: no length cap on symbol names,
 * LObj stays three words). Bump-allocated, never freed. */
#ifndef CCCCL_RT_NAME_POOL_SIZE
#define CCCCL_RT_NAME_POOL_SIZE 32768
#endif
static char   g_name_pool[CCCCL_RT_NAME_POOL_SIZE];
static size_t g_name_pool_next = 0;

static const char *pool_name(const char *name) {
    size_t len = strlen(name) + 1;
    if (g_name_pool_next + len > sizeof(g_name_pool)) {
        fprintf(stderr, "ccccl: name pool exhausted\n");
        exit(1);
    }
    char *dst = &g_name_pool[g_name_pool_next];
    memcpy(dst, name, len);
    g_name_pool_next += len;
    return dst;
}

LObj *ccccl_nil = NULL;
LObj *ccccl_t   = NULL;

static LObj *alloc_cell(void) {
    /* Fixed arena, never freed — see the file header and the "Garbage
     * collection" ticket for the intended replacement. */
    if (g_cell_next >= CCCCL_RT_ARENA_CELLS) {
        fprintf(stderr, "ccccl: cell arena exhausted\n");
        exit(1);
    }
    return &g_cell_arena[g_cell_next++];
}

LObj *ccccl_intern(const char *name) {
    int i;
    for (i = 0; i < g_sym_next; i++) {
        /* Linear scan — see the "Symbol table: linear scan to hash" ticket. */
        if (strcmp(g_sym_arena[i].as.atom.name, name) == 0)
            return &g_sym_arena[i];
    }
    if (g_sym_next >= CCCCL_RT_MAX_SYMS) {
        fprintf(stderr, "ccccl: symbol arena exhausted\n");
        exit(1);
    }
    LObj *s         = &g_sym_arena[g_sym_next++];
    s->tag          = CCCCL_ATOM;
    s->as.atom.name = pool_name(name);
    return s;
}

void ccccl_rt_init(void) {
    if (ccccl_nil)
        return; /* idempotent */
    ccccl_nil = ccccl_intern("NIL");
    ccccl_t   = ccccl_intern("T");
}

LObj *ccccl_get_nil(void) {
    return ccccl_nil;
}
LObj *ccccl_get_t(void) {
    return ccccl_t;
}

LObj *ccccl_cons(LObj *a, LObj *d) {
    LObj *c        = alloc_cell();
    c->tag         = CCCCL_PAIR;
    c->as.pair.car = a;
    c->as.pair.cdr = d;
    return c;
}

LObj *ccccl_car(LObj *x) {
    if (x->tag != CCCCL_PAIR)
        return ccccl_nil;
    return x->as.pair.car;
}

LObj *ccccl_cdr(LObj *x) {
    if (x->tag != CCCCL_PAIR)
        return ccccl_nil;
    return x->as.pair.cdr;
}

LObj *ccccl_atom(LObj *x) {
    return x->tag == CCCCL_PAIR ? ccccl_nil : ccccl_t;
}

LObj *ccccl_eq(LObj *a, LObj *b) {
    return a == b ? ccccl_t : ccccl_nil;
}

LObj *ccccl_bind(LObj *sym, LObj *val, LObj *env) {
    return ccccl_cons(ccccl_cons(sym, val), env);
}

LObj *ccccl_bind_list(LObj *syms, LObj *vals, LObj *env) {
    while (syms->tag == CCCCL_PAIR) {
        env  = ccccl_bind(ccccl_car(syms), ccccl_car(vals), env);
        syms = ccccl_cdr(syms);
        vals = ccccl_cdr(vals);
    }
    return env;
}

LObj *ccccl_assoc(LObj *sym, LObj *env) {
    while (env->tag == CCCCL_PAIR) {
        LObj *pair = ccccl_car(env);
        if (pair->tag == CCCCL_PAIR && ccccl_car(pair) == sym)
            return ccccl_cdr(pair);
        env = ccccl_cdr(env);
    }
    return ccccl_nil;
}

LObj *ccccl_closure(CccclFn fn, LObj *env) {
    LObj *c           = alloc_cell();
    c->tag            = CCCCL_FN;
    c->as.closure.fn  = fn;
    c->as.closure.env = env;
    return c;
}

LObj *ccccl_apply(LObj *f, LObj *args) {
    if (f->tag != CCCCL_FN) {
        fprintf(stderr, "ccccl: apply of a non-function\n");
        exit(1);
    }
    return f->as.closure.fn(args, f->as.closure.env);
}

void ccccl_print(LObj *x, FILE *out) {
    if (x->tag == CCCCL_ATOM) {
        fputs(x->as.atom.name, out);
    } else if (x->tag == CCCCL_FN) {
        fputs("#<closure>", out);
    } else {
        fputc('(', out);
        int first = 1;
        while (x->tag == CCCCL_PAIR) {
            if (!first)
                fputc(' ', out);
            first = 0;
            ccccl_print(ccccl_car(x), out);
            x = ccccl_cdr(x);
        }
        if (x != ccccl_nil) {
            fputs(" . ", out);
            ccccl_print(x, out);
        }
        fputc(')', out);
    }
}
