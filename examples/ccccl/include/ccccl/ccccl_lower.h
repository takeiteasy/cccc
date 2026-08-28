/* ccccl_lower.h — form tree -> tree IR (ccccl_ir.h), with scope resolution.
 *
 * Pure C, header-only, fixed arenas — see ccccl_ir.h's file comment.
 *
 * Language surface (SectorLISP+, see README.md):
 *   QUOTE ATOM EQ CAR CDR CONS COND IF LET PROGN PRINT
 *   + - * / MOD < =
 *   LAMBDA, LABEL, toplevel DEFINE.
 *
 * Scoping: lexical. A toplevel `(define (f params...) body)` lowers to a
 * function whose C parameters ARE the Lisp params — real names, no env to
 * thread. A LAMBDA/LABEL keeps the uniform closure entry point
 * `LObj *NAME(LObj *captures, LObj *args)`, since it must be callable
 * through `ccccl_apply` from a call site that doesn't know its identity
 * statically; free variables are resolved at lowering time (resolve_symbol
 * below) and captured positionally, read back via `ccccl_captured` at the
 * generated function's entry (see src/ccccl_comptime.c).
 *
 * A symbol unresolved anywhere in its enclosing scope chain — including a
 * toplevel bare reference — is a lowering error (ccccl_lower_error), not a
 * silent NIL: this is the "real names" design's other half. A bare
 * reference to a *known toplevel function's* name in value position (not
 * as a call head) is not an error — it lowers to CK_FN_VALUE and marks
 * that function `needs_thunk` (see ccccl_ir.h), since a toplevel define's
 * real C signature has no closure entry point of its own to hand to
 * `ccccl_apply`.
 */
#ifndef CCCCL_LOWER_H
#define CCCCL_LOWER_H

#include "ccccl_ir.h"
#include "ccccl_form.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static void ccccl_lower_error(CccclPlan *p, const char *what) {
    if (!p->has_error) {
        snprintf(p->error, sizeof(p->error), "%s", what);
        p->has_error = 1;
    }
}

/* Name mangling: lowercase, then '-' -> '_', '?' -> "_p", '!' -> "_bang",
 * '*' -> "_star", '/' -> "_slash", '>' -> "_gt", '<' -> "_lt", '=' -> "_eq",
 * else '_'; a leading digit gets a '_' prefix. */
static void ccccl_mangle(const char *lisp_name, char *out, int out_cap) {
    int         oi = 0;
    const char *s  = lisp_name;
    if (*s >= '0' && *s <= '9' && oi < out_cap - 1)
        out[oi++] = '_';
    for (; *s && oi < out_cap - 1; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') {
            out[oi++] = (char)(c - 'A' + 'a');
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[oi++] = c;
            continue;
        }
        switch (c) {
            case '-':
                out[oi++] = '_';
                break;
            case '?':
                if (oi < out_cap - 2) {
                    out[oi++] = '_';
                    out[oi++] = 'p';
                }
                break;
            case '!':
                if (oi < out_cap - 5) {
                    memcpy(out + oi, "_bang", 5);
                    oi += 5;
                }
                break;
            case '*':
                if (oi < out_cap - 5) {
                    memcpy(out + oi, "_star", 5);
                    oi += 5;
                }
                break;
            case '/':
                if (oi < out_cap - 6) {
                    memcpy(out + oi, "_slash", 6);
                    oi += 6;
                }
                break;
            case '>':
                if (oi < out_cap - 3) {
                    memcpy(out + oi, "_gt", 3);
                    oi += 3;
                }
                break;
            case '<':
                if (oi < out_cap - 3) {
                    memcpy(out + oi, "_lt", 3);
                    oi += 3;
                }
                break;
            case '=':
                if (oi < out_cap - 3) {
                    memcpy(out + oi, "_eq", 3);
                    oi += 3;
                }
                break;
            default:
                out[oi++] = '_';
                break;
        }
    }
    out[oi] = '\0';
}

static int ccccl_intern_sym(CccclPlan *p, const char *name) {
    int i;
    for (i = 0; i < p->sym_count; i++)
        if (strcmp(p->syms[i].text, name) == 0)
            return i;
    if (p->sym_count >= CL_MAX_SYMS) {
        ccccl_lower_error(p, "symbol table exhausted");
        return 0;
    }
    {
        int idx = p->sym_count++;
        strncpy(p->syms[idx].text, name, sizeof(p->syms[idx].text) - 1);
        return idx;
    }
}

static int ccccl_find_fn(CccclPlan *p, const char *lisp_name) {
    int i;
    for (i = 0; i < p->fn_count; i++)
        if (!p->fns[i].is_lambda && strcmp(p->fns[i].lisp_name, lisp_name) == 0)
            return i;
    return -1;
}

static CccclPlanFn *ccccl_new_fn(CccclPlan *p, const char *lisp_name,
                                 int is_lambda) {
    CccclPlanFn *fn;
    if (p->fn_count >= CL_MAX_FNS) {
        ccccl_lower_error(p, "function table exhausted");
        return &p->fns[0];
    }
    fn                     = &p->fns[p->fn_count++];
    fn->binding_count      = 0;
    fn->param_count        = 0;
    fn->self_label_binding = -1;
    fn->body               = -1;
    fn->is_lambda          = is_lambda;
    fn->needs_thunk        = 0;
    if (lisp_name) {
        strncpy(fn->lisp_name, lisp_name, sizeof(fn->lisp_name) - 1);
        ccccl_mangle(lisp_name, fn->c_name, (int)sizeof(fn->c_name));
    } else {
        static int gensym_n = 0;
        snprintf(fn->lisp_name, sizeof(fn->lisp_name), "<lambda>");
        snprintf(fn->c_name, sizeof(fn->c_name), "ccccl_lambda_%d", gensym_n++);
    }
    return fn;
}

/* Adds a binding to `fn`, deduping its C spelling against every binding
 * already in `fn` (an numeric suffix on collision) -- this project's
 * examples never shadow, so this is a safety net, not exercised. */
static int ccccl_add_binding(CccclPlanFn *fn, const char *lisp_name,
                             CccclBindKind kind) {
    CccclBinding *b;
    char          mangled[CL_NAME_LEN];
    int           i, suffix = 0;
    if (fn->binding_count >= CL_MAX_BINDINGS)
        return 0; /* caller's plan->has_error path already tripped elsewhere */
    ccccl_mangle(lisp_name, mangled, sizeof(mangled));
    b = &fn->bindings[fn->binding_count];
    strncpy(b->lisp_name, lisp_name, sizeof(b->lisp_name) - 1);
    strncpy(b->c_name, mangled, sizeof(b->c_name) - 1);
    for (;;) {
        int collide = 0;
        for (i = 0; i < fn->binding_count; i++)
            if (strcmp(fn->bindings[i].c_name, b->c_name) == 0) {
                collide = 1;
                break;
            }
        if (!collide)
            break;
        snprintf(b->c_name, sizeof(b->c_name), "%s_%d", mangled, ++suffix);
    }
    b->kind           = kind;
    b->capture_source = -1;
    return fn->binding_count++;
}

/* Lowering-time-only scope chain: one Scope per function currently being
 * lowered (nested for a LAMBDA/LABEL inside another function's body),
 * threaded through the recursion on the C call stack -- never stored in
 * CccclPlan. `active` is a stack of `fn`'s own binding indices currently
 * visible (params, pushed once; LET locals, pushed/popped around the
 * LET's own body). */
typedef struct Scope {
    CccclPlanFn  *fn;
    struct Scope *parent;
    int           active[CL_MAX_BINDINGS];
    int           active_len;
} Scope;

/* Resolves `name` against `sc`'s own active bindings, most-recently-pushed
 * first (so LET shadowing works); on a miss, recurses into the enclosing
 * scope and, if found there, adds a new BIND_CAPTURE binding to `sc->fn`
 * (memoized by name) recording which of the *parent's* own bindings to
 * read at the closure's creation site. This is what makes an arbitrarily
 * (lexically) nested LAMBDA capture a grandparent's variable: each
 * intermediate scope gets its own capture binding, transitively, purely as
 * a side effect of this recursion -- see README.md's "Closures" section. */
static int resolve_symbol(CccclPlan *p, Scope *sc, const char *name) {
    int i;
    for (i = sc->active_len - 1; i >= 0; i--) {
        int bidx = sc->active[i];
        if (strcmp(sc->fn->bindings[bidx].lisp_name, name) == 0)
            return bidx;
    }
    if (!sc->parent)
        return -1;
    {
        int parent_bidx = resolve_symbol(p, sc->parent, name);
        if (parent_bidx < 0)
            return -1;
        for (i = 0; i < sc->fn->binding_count; i++)
            if (sc->fn->bindings[i].kind == BIND_CAPTURE &&
                strcmp(sc->fn->bindings[i].lisp_name, name) == 0)
                return i;
        {
            int idx = ccccl_add_binding(sc->fn, name, BIND_CAPTURE);
            sc->fn->bindings[idx].capture_source = parent_bidx;
            return idx;
        }
    }
}

static int ccccl_new_expr(CccclPlan *p, CccclExprKind kind) {
    CccclExpr *e;
    if (p->expr_count >= CL_MAX_EXPRS) {
        ccccl_lower_error(p, "expression arena exhausted");
        return 0;
    }
    e       = &p->exprs[p->expr_count];
    e->kind = kind;
    e->ival = 0;
    e->a = e->b = e->c = 0;
    e->child_count     = 0;
    e->let_count       = 0;
    e->is_self_tail    = 0;
    return p->expr_count++;
}

static int ccccl_lower_expr(CccclPlan *p, Scope *sc, CccclForm *e, int is_tail);

/* (quote FORM): FORM must be an atom, a number, or a list of these --
 * a nested list desugars recursively to CONS-of-QUOTE, so `(quote (a b))`
 * needs no dedicated runtime representation at all, only CK_CONS/
 * CK_QUOTE_ATOM the ordinary emitter already knows. Numbers and NIL/T are
 * self-evaluating -- quoting one is the identity. */
static int ccccl_lower_quote(CccclPlan *p, CccclForm *form) {
    if (form->kind == CL_FORM_INT) {
        int idx            = ccccl_new_expr(p, CK_INT);
        p->exprs[idx].ival = form->ival;
        return idx;
    }
    if (form->kind == CL_FORM_ATOM) {
        if (strcmp(form->atom, "NIL") == 0)
            return ccccl_new_expr(p, CK_NIL);
        if (strcmp(form->atom, "T") == 0)
            return ccccl_new_expr(p, CK_T);
        {
            int idx         = ccccl_new_expr(p, CK_QUOTE_ATOM);
            p->exprs[idx].a = ccccl_intern_sym(p, form->atom);
            return idx;
        }
    }
    /* PAIR */
    {
        int car_e       = ccccl_lower_quote(p, form->car);
        int cdr_e       = ccccl_lower_quote(p, form->cdr);
        int idx         = ccccl_new_expr(p, CK_CONS);
        p->exprs[idx].a = car_e;
        p->exprs[idx].b = cdr_e;
        return idx;
    }
}

static int ccccl_form_is_atom(CccclForm *f, const char *text) {
    return f->kind == CL_FORM_ATOM && strcmp(f->atom, text) == 0;
}

/* Lowers a LAMBDA/LABEL form's params+body into `fn` (already created by
 * the caller via ccccl_new_fn), linked into the scope chain via `parent`.
 * `label_name`, if non-NULL, additionally binds that name to the closure
 * itself inside its own body (see README.md's "LABEL" section and
 * ccccl_rt.h's ccccl_capture_set). */
static void ccccl_lower_fn_body(CccclPlan *p, CccclPlanFn *fn, Scope *parent,
                                CccclForm *params, CccclForm *body,
                                const char *label_name) {
    Scope sc;
    sc.fn         = fn;
    sc.parent     = parent;
    sc.active_len = 0;

    {
        CccclForm *cur = params;
        while (cur->kind == CL_FORM_PAIR) {
            int idx = ccccl_add_binding(fn, cur->car->atom, BIND_PARAM);
            sc.active[sc.active_len++] = idx;
            fn->param_count++;
            cur = cur->cdr;
        }
    }
    if (label_name) {
        fn->self_label_binding =
            ccccl_add_binding(fn, label_name, BIND_CAPTURE);
        sc.active[sc.active_len++] = fn->self_label_binding;
    }
    fn->body = ccccl_lower_expr(p, &sc, body, 1);
}

static int ccccl_lower_args(CccclPlan *p, Scope *sc, CccclForm *args,
                            int *out_children) {
    int        n   = 0;
    CccclForm *cur = args;
    while (cur->kind == CL_FORM_PAIR) {
        if (n >= CL_MAX_CHILDREN) {
            ccccl_lower_error(p, "too many arguments");
            return n;
        }
        out_children[n++] = ccccl_lower_expr(p, sc, cur->car, 0);
        cur               = cur->cdr;
    }
    return n;
}

/* out.a/b/c set for CK_CAR/CDR/ATOM/EQ/CONS/arith/IF; returns the new expr
 * index, or -1 if `kind` isn't one of the fixed-arity forms this covers. */
static int ccccl_lower_expr(CccclPlan *p, Scope *sc, CccclForm *e,
                            int is_tail) {
    if (p->has_error)
        return 0;

    if (e->kind == CL_FORM_INT) {
        int idx            = ccccl_new_expr(p, CK_INT);
        p->exprs[idx].ival = e->ival;
        return idx;
    }

    if (e->kind == CL_FORM_ATOM) {
        if (strcmp(e->atom, "NIL") == 0)
            return ccccl_new_expr(p, CK_NIL);
        if (strcmp(e->atom, "T") == 0)
            return ccccl_new_expr(p, CK_T);
        {
            int bidx = resolve_symbol(p, sc, e->atom);
            if (bidx >= 0) {
                int idx         = ccccl_new_expr(p, CK_VAR);
                p->exprs[idx].a = bidx;
                return idx;
            }
        }
        {
            int fi = ccccl_find_fn(p, e->atom);
            if (fi >= 0) {
                p->fns[fi].needs_thunk = 1;
                {
                    int idx         = ccccl_new_expr(p, CK_FN_VALUE);
                    p->exprs[idx].a = fi;
                    return idx;
                }
            }
        }
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "unbound variable: %s", e->atom);
            ccccl_lower_error(p, buf);
        }
        return 0;
    }

    /* e is a PAIR: (head ...rest) */
    {
        CccclForm *head = e->car;
        CccclForm *rest = e->cdr;

        if (head->kind != CL_FORM_ATOM)
            goto application;

#define UNARY(NAME, KIND)                                                      \
    if (strcmp(head->atom, NAME) == 0) {                                       \
        int idx         = ccccl_new_expr(p, KIND);                             \
        p->exprs[idx].a = ccccl_lower_expr(p, sc, rest->car, 0);               \
        return idx;                                                            \
    }
#define BINARY(NAME, KIND)                                                     \
    if (strcmp(head->atom, NAME) == 0) {                                       \
        int a           = ccccl_lower_expr(p, sc, rest->car, 0);               \
        int b           = ccccl_lower_expr(p, sc, rest->cdr->car, 0);          \
        int idx         = ccccl_new_expr(p, KIND);                             \
        p->exprs[idx].a = a;                                                   \
        p->exprs[idx].b = b;                                                   \
        return idx;                                                            \
    }

        if (strcmp(head->atom, "QUOTE") == 0)
            return ccccl_lower_quote(p, rest->car);

        UNARY("CAR", CK_CAR)
        UNARY("CDR", CK_CDR)
        UNARY("ATOM", CK_ATOM)
        UNARY("PRINT", CK_PRINT)
        BINARY("EQ", CK_EQ)
        BINARY("CONS", CK_CONS)
        BINARY("+", CK_ADD)
        BINARY("-", CK_SUB)
        BINARY("*", CK_MUL)
        BINARY("/", CK_DIV)
        BINARY("MOD", CK_MOD)
        BINARY("<", CK_LT)
        BINARY("=", CK_NUMEQ)
#undef UNARY
#undef BINARY

        if (strcmp(head->atom, "IF") == 0) {
            int idx = ccccl_new_expr(p, CK_IF);
            int cond_e, then_e, else_e;
            cond_e = ccccl_lower_expr(p, sc, rest->car, 0);
            then_e = ccccl_lower_expr(p, sc, rest->cdr->car, is_tail);
            else_e = ccccl_lower_expr(p, sc, rest->cdr->cdr->car, is_tail);
            p->exprs[idx].a = cond_e;
            p->exprs[idx].b = then_e;
            p->exprs[idx].c = else_e;
            return idx;
        }

        if (strcmp(head->atom, "COND") == 0) {
            int        idx    = ccccl_new_expr(p, CK_COND);
            CccclForm *clause = rest;
            int        n      = 0;
            while (clause->kind == CL_FORM_PAIR) {
                CccclForm *pair = clause->car;
                int        pred_e, val_e;
                if (n + 2 > CL_MAX_CHILDREN) {
                    ccccl_lower_error(p, "too many COND clauses");
                    return idx;
                }
                pred_e = ccccl_lower_expr(p, sc, pair->car, 0);
                val_e  = ccccl_lower_expr(p, sc, pair->cdr->car, is_tail);
                p->exprs[idx].children[n++] = pred_e;
                p->exprs[idx].children[n++] = val_e;
                clause                      = clause->cdr;
            }
            p->exprs[idx].child_count = n;
            return idx;
        }

        if (strcmp(head->atom, "LET") == 0) {
            CccclForm *bindings = rest->car;
            CccclForm *body     = rest->cdr->car;
            int        idx      = ccccl_new_expr(p, CK_LET);
            Scope      inner;
            int        n     = 0;
            inner.fn         = sc->fn;
            inner.parent     = sc->parent;
            inner.active_len = sc->active_len;
            memcpy(inner.active, sc->active, sizeof(int) * sc->active_len);

            {
                CccclForm *cur = bindings;
                while (cur->kind == CL_FORM_PAIR) {
                    CccclForm *b = cur->car; /* (name init) */
                    int        init_e;
                    int        bidx;
                    if (n >= CL_MAX_CHILDREN) {
                        ccccl_lower_error(p, "too many LET bindings");
                        return idx;
                    }
                    init_e = ccccl_lower_expr(p, sc, b->cdr->car, 0);
                    bidx = ccccl_add_binding(sc->fn, b->car->atom, BIND_LOCAL);
                    p->exprs[idx].children[n]     = init_e;
                    p->exprs[idx].let_bindings[n] = bidx;
                    n++;
                    inner.active[inner.active_len++] = bidx;
                    cur                              = cur->cdr;
                }
            }
            p->exprs[idx].child_count = n;
            p->exprs[idx].let_count   = n;
            p->exprs[idx].a = ccccl_lower_expr(p, &inner, body, is_tail);
            return idx;
        }

        if (strcmp(head->atom, "PROGN") == 0) {
            int        idx = ccccl_new_expr(p, CK_PROGN);
            CccclForm *cur = rest;
            int        n   = 0;
            while (cur->kind == CL_FORM_PAIR) {
                if (n >= CL_MAX_CHILDREN) {
                    ccccl_lower_error(p, "too many PROGN forms");
                    return idx;
                }
                p->exprs[idx].children[n++] = ccccl_lower_expr(
                    p, sc, cur->car, is_tail && cur->cdr->kind != CL_FORM_PAIR);
                cur = cur->cdr;
            }
            p->exprs[idx].child_count = n;
            return idx;
        }

        if (strcmp(head->atom, "LAMBDA") == 0) {
            CccclForm   *params = rest->car;
            CccclForm   *body   = rest->cdr->car;
            CccclPlanFn *lam    = ccccl_new_fn(p, NULL, 1);
            int          lam_fi = (int)(lam - p->fns);
            ccccl_lower_fn_body(p, lam, sc, params, body, NULL);
            {
                int idx         = ccccl_new_expr(p, CK_LAMBDA);
                p->exprs[idx].a = lam_fi;
                return idx;
            }
        }
        if (strcmp(head->atom, "LABEL") == 0) {
            const char *label_name = rest->car->atom;
            CccclForm  *lambda_form =
                rest->cdr->car; /* (LAMBDA (params) body) */
            CccclForm   *params = lambda_form->cdr->car;
            CccclForm   *body   = lambda_form->cdr->cdr->car;
            CccclPlanFn *lam    = ccccl_new_fn(p, NULL, 1);
            int          lam_fi = (int)(lam - p->fns);
            ccccl_lower_fn_body(p, lam, sc, params, body, label_name);
            {
                int idx         = ccccl_new_expr(p, CK_LAMBDA);
                p->exprs[idx].a = lam_fi;
                return idx;
            }
        }

    application:
        /* Application: (f a...) */
        if (head->kind == CL_FORM_ATOM) {
            int fi = ccccl_find_fn(p, head->atom);
            if (fi >= 0) {
                int idx         = ccccl_new_expr(p, CK_CALL_DIRECT);
                p->exprs[idx].a = fi;
                p->exprs[idx].child_count =
                    ccccl_lower_args(p, sc, rest, p->exprs[idx].children);
                p->exprs[idx].is_self_tail =
                    is_tail && fi == (int)(sc->fn - p->fns);
                return idx;
            }
        }
        /* (e a...) where e is itself an expression (e.g. a param bound to a
         * closure, or an inline LAMBDA) -- generic apply. */
        {
            int idx         = ccccl_new_expr(p, CK_APPLY);
            p->exprs[idx].a = ccccl_lower_expr(p, sc, head, 0);
            p->exprs[idx].child_count =
                ccccl_lower_args(p, sc, rest, p->exprs[idx].children);
            return idx;
        }
    }
}

/* Declares one toplevel `(define (f params...) body)` form: validates its
 * shape and creates its CccclPlanFn (so ccccl_find_fn resolves the name),
 * but does not lower its params/body yet. Split from lowering so
 * ccccl_compile can run this over every toplevel form first -- without
 * this, a body-lowering pass sees only the *earlier* defines' names via
 * ccccl_find_fn, so a forward reference (including true mutual recursion
 * between two defines) falls through to the generic runtime-apply path
 * instead of CL_OP_CALL_DIRECT. */
static CccclPlanFn *ccccl_declare_toplevel(CccclPlan *p, CccclForm *form) {
    if (p->has_error)
        return NULL;
    if (form->kind == CL_FORM_PAIR && ccccl_form_is_atom(form->car, "DEFINE")) {
        CccclForm  *sig  = form->cdr->car; /* (name params...) */
        const char *name = sig->car->atom;
        if (ccccl_find_fn(p, name) >= 0) {
            ccccl_lower_error(p, "duplicate toplevel define");
            return NULL;
        }
        return ccccl_new_fn(p, name, 0);
    }
    ccccl_lower_error(p, "expected (define (name params...) body) at toplevel");
    return NULL;
}

/* Lowers a toplevel define's params+body into the CccclPlanFn
 * ccccl_declare_toplevel already created for it. Must not call
 * ccccl_new_fn again -- it would reset fn's fields and orphan the declared
 * slot other defines may already reference by index. */
static void ccccl_lower_toplevel_body(CccclPlan *p, CccclForm *form,
                                      CccclPlanFn *fn) {
    if (p->has_error || !fn)
        return;
    {
        CccclForm *sig    = form->cdr->car; /* (name params...) */
        CccclForm *body   = form->cdr->cdr->car;
        CccclForm *params = sig->cdr;
        ccccl_lower_fn_body(p, fn, NULL, params, body, NULL);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* CCCCL_LOWER_H */
