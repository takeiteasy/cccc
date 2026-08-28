/* ccccl_rt.c — the runtime universe, implementation.
 *
 * Ordinary C, zero cccc dependency, linked into the final program by the
 * system `cc`. Fixed arenas, no malloc — matches the comptime side's own
 * no-libc-allocator posture and keeps this a small, readable worked
 * example rather than a real GC'd runtime.
 */
#include "ccccl_rt.h"
#include "ccccl_rt_internal.h"

#include <stdlib.h>
#include <string.h>

#ifndef CCCCL_RT_NAME_POOL_SIZE
#define CCCCL_RT_NAME_POOL_SIZE 32768
#endif

static char   g_name_pool[CCCCL_RT_NAME_POOL_SIZE];
static size_t g_name_pool_next = 0;

static LObj   g_sym_arena[CCCCL_RT_MAX_SYMS];
static int    g_sym_next = 0;

static LObj   g_cell_arena[CCCCL_RT_ARENA_CELLS];
static int    g_cell_next = 0;

static LObj   g_int_arena[CCCCL_RT_MAX_INTS];
static int    g_int_next = 0;

LObj         *ccccl_nil  = 0;
LObj         *ccccl_t    = 0;

static void ccccl_rt_fatal(const char *msg) {
    fprintf(stderr, "ccccl: %s\n", msg);
    exit(1);
}

/* Two-part variant, so callers can name an operator without `snprintf`.
 * `snprintf` is a fortify macro (`__builtin___snprintf_chk`) in the macOS
 * SDK headers; cccc's `-c=native` serializer emits its own plain
 * `int snprintf(...)` prototype for it when no shared TU pulls <stdio.h>
 * directly (a "program"-mode .lisp file has no hand-written host TU that
 * would), and that plain prototype then collides with the macro. `fprintf`
 * has no `_chk` form, so this path is safe where `snprintf` was not. */
static void ccccl_rt_fatal2(const char *a, const char *b) {
    fprintf(stderr, "ccccl: %s%s\n", a, b);
    exit(1);
}

static LObj *alloc_cell(void) {
    if (g_cell_next >= CCCCL_RT_ARENA_CELLS)
        ccccl_rt_fatal("cell arena exhausted");
    return &g_cell_arena[g_cell_next++];
}

static const char *pool_name(const char *name) {
    size_t len = strlen(name);
    char  *dst;
    if (g_name_pool_next + len + 1 > sizeof(g_name_pool))
        ccccl_rt_fatal("name pool exhausted");
    dst = &g_name_pool[g_name_pool_next];
    memcpy(dst, name, len + 1);
    g_name_pool_next += len + 1;
    return dst;
}

LObj *ccccl_intern(const char *name) {
    int i;
    for (i = 0; i < g_sym_next; i++)
        if (strcmp(g_sym_arena[i].as.atom.name, name) == 0)
            return &g_sym_arena[i];
    if (g_sym_next >= CCCCL_RT_MAX_SYMS)
        ccccl_rt_fatal("symbol arena exhausted");
    {
        LObj *s         = &g_sym_arena[g_sym_next++];
        s->tag          = CCCCL_ATOM;
        s->as.atom.name = pool_name(name);
        return s;
    }
}

LObj *ccccl_int(long v) {
    LObj *n;
    if (g_int_next >= CCCCL_RT_MAX_INTS)
        ccccl_rt_fatal("int arena exhausted");
    n          = &g_int_arena[g_int_next++];
    n->tag     = CCCCL_INT;
    n->as.ival = v;
    return n;
}

void ccccl_rt_init(void) {
    ccccl_nil = ccccl_intern("NIL");
    ccccl_t   = ccccl_intern("T");
}

LObj *ccccl_cons(LObj *a, LObj *d) {
    LObj *c        = alloc_cell();
    c->tag         = CCCCL_PAIR;
    c->as.pair.car = a;
    c->as.pair.cdr = d;
    return c;
}

LObj *ccccl_car(LObj *x) {
    return x->tag == CCCCL_PAIR ? x->as.pair.car : ccccl_nil;
}

LObj *ccccl_cdr(LObj *x) {
    return x->tag == CCCCL_PAIR ? x->as.pair.cdr : ccccl_nil;
}

LObj *ccccl_atom(LObj *x) {
    return x->tag == CCCCL_PAIR ? ccccl_nil : ccccl_t;
}

LObj *ccccl_eq(LObj *a, LObj *b) {
    if (a == b)
        return ccccl_t;
    if (a->tag == CCCCL_INT && b->tag == CCCCL_INT && a->as.ival == b->as.ival)
        return ccccl_t;
    return ccccl_nil;
}

static long ccccl_as_int(LObj *x, const char *who) {
    if (x->tag != CCCCL_INT)
        ccccl_rt_fatal2(who, ": not a number");
    return x->as.ival;
}

LObj *ccccl_add(LObj *a, LObj *b) {
    return ccccl_int(ccccl_as_int(a, "+") + ccccl_as_int(b, "+"));
}
LObj *ccccl_sub(LObj *a, LObj *b) {
    return ccccl_int(ccccl_as_int(a, "-") - ccccl_as_int(b, "-"));
}
LObj *ccccl_mul(LObj *a, LObj *b) {
    return ccccl_int(ccccl_as_int(a, "*") * ccccl_as_int(b, "*"));
}
LObj *ccccl_div(LObj *a, LObj *b) {
    long bv = ccccl_as_int(b, "/");
    if (bv == 0)
        ccccl_rt_fatal("/: division by zero");
    return ccccl_int(ccccl_as_int(a, "/") / bv);
}
LObj *ccccl_mod(LObj *a, LObj *b) {
    long bv = ccccl_as_int(b, "mod");
    if (bv == 0)
        ccccl_rt_fatal("mod: division by zero");
    return ccccl_int(ccccl_as_int(a, "mod") % bv);
}
LObj *ccccl_num_lt(LObj *a, LObj *b) {
    return ccccl_as_int(a, "<") < ccccl_as_int(b, "<") ? ccccl_t : ccccl_nil;
}
LObj *ccccl_num_eq(LObj *a, LObj *b) {
    return ccccl_as_int(a, "=") == ccccl_as_int(b, "=") ? ccccl_t : ccccl_nil;
}

LObj *ccccl_nth(LObj *list, int k) {
    while (k-- > 0)
        list = ccccl_cdr(list);
    return ccccl_car(list);
}

LObj *ccccl_closure(CccclFn fn, LObj *captures) {
    LObj *c                = alloc_cell();
    c->tag                 = CCCCL_CLOSURE;
    c->as.closure.fn       = fn;
    c->as.closure.captures = captures;
    return c;
}

LObj *ccccl_apply(LObj *f, LObj *args) {
    if (f->tag != CCCCL_CLOSURE)
        ccccl_rt_fatal("apply of a non-function");
    return f->as.closure.fn(f->as.closure.captures, args);
}

void ccccl_capture_set(LObj *captures, int k, LObj *v) {
    while (k-- > 0)
        captures = ccccl_cdr(captures);
    if (captures->tag == CCCCL_PAIR)
        captures->as.pair.car = v;
}

LObj *ccccl_closure_self(CccclFn fn, LObj *captures, int self_slot) {
    LObj *c = ccccl_closure(fn, captures);
    ccccl_capture_set(captures, self_slot, c);
    return c;
}

void ccccl_print(LObj *x, FILE *out) {
    if (x->tag == CCCCL_INT) {
        fprintf(out, "%ld", x->as.ival);
        return;
    }
    if (x->tag == CCCCL_CLOSURE) {
        fprintf(out, "#<closure>");
        return;
    }
    if (x->tag == CCCCL_ATOM) {
        fprintf(out, "%s", x->as.atom.name);
        return;
    }
    /* PAIR: print as a list, following cdrs; a non-NIL, non-PAIR tail
     * prints as a dotted pair. */
    fprintf(out, "(");
    for (;;) {
        ccccl_print(x->as.pair.car, out);
        x = x->as.pair.cdr;
        if (x == ccccl_nil)
            break;
        if (x->tag != CCCCL_PAIR) {
            fprintf(out, " . ");
            ccccl_print(x, out);
            break;
        }
        fprintf(out, " ");
    }
    fprintf(out, ")");
}

LObj *ccccl_print_stdout(LObj *x) {
    ccccl_print(x, stdout);
    return x;
}

void ccccl_newline_stdout(void) {
    fputc('\n', stdout);
}
