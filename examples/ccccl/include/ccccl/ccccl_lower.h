/* ccccl_lower.h — form tree -> flat postfix Op list.
 *
 * Pure C, header-only, fixed arenas -- see ccccl_plan.h's file comment.
 * Consumes CccclForm trees from ccccl_reader.h and appends CccclOps to a
 * CccclPlan (ccccl_plan.h).
 *
 * Language surface (strict SectorLISP, see docs/LANGUAGE.md):
 *   QUOTE ATOM EQ CAR CDR CONS COND, LAMBDA, LABEL, toplevel DEFINE.
 *
 * Scoping: environment-passing (dynamic scoping, see docs/ARCHITECTURE.md).
 * A callee's free variables resolve in the *caller's* live environment, so
 * CL_OP_CALL_DIRECT's replay must pass the caller's env, never a fresh one.
 *
 * CL_OP_COND and laziness: a COND clause's predicate and value expressions
 * are recorded as separate op sub-ranges (CccclCondClause), not inlined into
 * the flat op stream at the COND site. An *eager* interpreter over a flat
 * op list -- one that evaluates every operand before reaching the op that
 * consumes them -- sends any recursive COND-based function into unbounded
 * recursion, because the recursive call in an untaken branch still gets
 * evaluated first. This is exactly the trap yas's aot/tests/plan_test.c
 * documents. Both replayers in this project (the comptime Node* builder in
 * src/ccccl_comptime.c, and the reference interpreter in
 * tests/test_lower.c) evaluate a clause's predicate range, and only on a
 * true result its value range, exactly once, short-circuiting like the
 * generated C ternary chain does at runtime.
 */
#ifndef CCCCL_LOWER_H
#define CCCCL_LOWER_H

#include "ccccl_plan.h"
#include "ccccl_reader.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static void ccccl_lower_error(CccclPlan *p, const char *what) {
    if (!p->has_error) {
        snprintf(p->error, sizeof(p->error), "unsupported form: %s", what);
        p->has_error = 1;
    }
}

/* Name mangling, following yas's aot/plan.c table: lowercase, then
 * '-' -> '_', '?' -> "_p", '!' -> "_bang", '*' -> "_star", '/' -> "_slash",
 * '>' -> "_gt", '<' -> "_lt", '=' -> "_eq", else '_'; a leading digit gets a
 * '_' prefix. Collision detection is not implemented -- see the ticket
 * tracker entry "Name-mangling collision detection". */
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
        if (c >= 'a' && c <= 'z') {
            out[oi++] = c;
            continue;
        }
        if (c >= '0' && c <= '9') {
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

static int ccccl_form_is_atom(CccclForm *f, const char *text) {
    return f->kind == CL_FORM_ATOM && strcmp(f->atom, text) == 0;
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
        int i2;
        for (i2 = 0; i2 < CL_NAME_LEN && name[i2]; i2++)
            p->syms[idx].text[i2] = name[i2];
        p->syms[idx].text[i2] = '\0';
        return idx;
    }
}

/* A quoted atom and a symbol are the exact same runtime shape (both are
 * just interned LObj atoms) -- the only difference is that QUOTE skips the
 * env lookup a bare symbol would trigger. CL_OP_QUOTE therefore reuses the
 * symbol table (ccccl_intern_sym below) instead of a separate constant
 * table; its replay is `ccccl_sym_<n>()` with no ccccl_assoc call. Quoting
 * a list is not yet supported -- see the "expand the example corpus"
 * ticket -- and is rejected in ccccl_lower_expr's QUOTE case. */

/* Appends to whichever pool fn->emit_ops/emit_count currently points at --
 * fn->ops normally, fn->cond_ops while lowering a COND clause (see the
 * comment on CccclPlanFn::cond_ops in ccccl_plan.h). */
static CccclOp *ccccl_emit(CccclPlanFn *fn, CccclOpKind kind, int a, int b) {
    CccclOp *op;
    if (*fn->emit_count >= CL_MAX_OPS)
        return NULL; /* caller checks plan->has_error via ccccl_lower_error path
                      */
    op       = &fn->emit_ops[(*fn->emit_count)++];
    op->kind = kind;
    op->a    = a;
    op->b    = b;
    return op;
}

static int ccccl_find_param(CccclPlanFn *fn, const char *name) {
    int i;
    for (i = 0; i < fn->param_count; i++)
        if (strcmp(fn->param_lisp[i], name) == 0)
            return i;
    return -1;
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
    fn                 = &p->fns[p->fn_count++];
    fn->op_count       = 0;
    fn->cond_op_count  = 0;
    fn->emit_ops       = fn->ops;
    fn->emit_count     = &fn->op_count;
    fn->cond_count     = 0;
    fn->param_count    = 0;
    fn->is_lambda      = is_lambda;
    fn->self_label_sym = -1;
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

static void ccccl_lower_expr(CccclPlan *p, CccclPlanFn *fn, CccclForm *e);

/* Lowers a single argument list (already evaluated forms) into
 * CL_OP_ARGLIST. Args are pushed left-to-right then consed right-to-left by
 * the replayer, matching ordinary evaluation order. */
static void ccccl_lower_args(CccclPlan *p, CccclPlanFn *fn, CccclForm *args) {
    int        n   = 0;
    CccclForm *cur = args;
    while (cur->kind == CL_FORM_PAIR) {
        ccccl_lower_expr(p, fn, cur->car);
        n++;
        cur = cur->cdr;
    }
    ccccl_emit(fn, CL_OP_ARGLIST, n, 0);
}

static void ccccl_lower_lambda(CccclPlan *p, CccclForm *params, CccclForm *body,
                               CccclPlanFn *out_fn_slot_hint,
                               const char  *label_name);

static void ccccl_lower_expr(CccclPlan *p, CccclPlanFn *fn, CccclForm *e) {
    if (p->has_error)
        return;

    if (e->kind == CL_FORM_ATOM) {
        if (strcmp(e->atom, "NIL") == 0) {
            ccccl_emit(fn, CL_OP_NIL, 0, 0);
            return;
        }
        if (strcmp(e->atom, "T") == 0) {
            ccccl_emit(fn, CL_OP_T, 0, 0);
            return;
        }
        {
            int param = ccccl_find_param(fn, e->atom);
            int sym   = ccccl_intern_sym(p, e->atom);
            (void)param; /* lookup always goes through env at runtime (dynamic
                            scoping) */
            ccccl_emit(fn, CL_OP_LOOKUP, sym, 0);
        }
        return;
    }

    /* e is a PAIR: (head ...rest) */
    {
        CccclForm *head = e->car;
        CccclForm *rest = e->cdr;

        if (head->kind == CL_FORM_ATOM && strcmp(head->atom, "QUOTE") == 0) {
            if (rest->car->kind != CL_FORM_ATOM) {
                ccccl_lower_error(p, "(quote ...) of a list");
                return;
            }
            {
                int idx = ccccl_intern_sym(p, rest->car->atom);
                ccccl_emit(fn, CL_OP_QUOTE, idx, 0);
            }
            return;
        }
        if (head->kind == CL_FORM_ATOM && strcmp(head->atom, "CAR") == 0) {
            ccccl_lower_expr(p, fn, rest->car);
            ccccl_emit(fn, CL_OP_CAR, 0, 0);
            return;
        }
        if (head->kind == CL_FORM_ATOM && strcmp(head->atom, "CDR") == 0) {
            ccccl_lower_expr(p, fn, rest->car);
            ccccl_emit(fn, CL_OP_CDR, 0, 0);
            return;
        }
        if (head->kind == CL_FORM_ATOM && strcmp(head->atom, "ATOM") == 0) {
            ccccl_lower_expr(p, fn, rest->car);
            ccccl_emit(fn, CL_OP_ATOM, 0, 0);
            return;
        }
        if (head->kind == CL_FORM_ATOM && strcmp(head->atom, "EQ") == 0) {
            ccccl_lower_expr(p, fn, rest->car);
            ccccl_lower_expr(p, fn, rest->cdr->car);
            ccccl_emit(fn, CL_OP_EQ, 0, 0);
            return;
        }
        if (head->kind == CL_FORM_ATOM && strcmp(head->atom, "CONS") == 0) {
            ccccl_lower_expr(p, fn, rest->car);
            ccccl_lower_expr(p, fn, rest->cdr->car);
            ccccl_emit(fn, CL_OP_CONS, 0, 0);
            return;
        }
        if (head->kind == CL_FORM_ATOM && strcmp(head->atom, "COND") == 0) {
            CccclForm       *clause       = rest;
            int              clause_count = 0;
            CccclCondClause *slots        = fn->conds + fn->cond_count;
            /* Redirect emission into cond_ops for the duration of lowering
             * every clause -- see the comment on CccclPlanFn::cond_ops in
             * ccccl_plan.h for why this pool must stay separate from the
             * ordinary linear ops[] flow. Saved/restored around the whole
             * clause loop (not per-clause) so nested CONDs, which recurse
             * through ccccl_lower_expr and perform their own save/restore,
             * nest correctly via the C call stack. */
            CccclOp *saved_ops   = fn->emit_ops;
            int     *saved_count = fn->emit_count;
            fn->emit_ops         = fn->cond_ops;
            fn->emit_count       = &fn->cond_op_count;

            while (clause->kind == CL_FORM_PAIR) {
                CccclForm       *pair = clause->car;
                CccclCondClause *slot;
                if (fn->cond_count >= CL_MAX_COND_CLAUSES) {
                    ccccl_lower_error(p, "too many COND clauses");
                    fn->emit_ops   = saved_ops;
                    fn->emit_count = saved_count;
                    return;
                }
                slot             = &fn->conds[fn->cond_count++];
                slot->pred_start = fn->cond_op_count;
                ccccl_lower_expr(p, fn, pair->car);
                slot->pred_end  = fn->cond_op_count;
                slot->val_start = fn->cond_op_count;
                ccccl_lower_expr(p, fn, pair->cdr->car);
                slot->val_end = fn->cond_op_count;
                clause_count++;
                clause = clause->cdr;
            }

            fn->emit_ops   = saved_ops;
            fn->emit_count = saved_count;
            ccccl_emit(fn, CL_OP_COND, (int)(slots - fn->conds), clause_count);
            return;
        }
        if (head->kind == CL_FORM_ATOM && strcmp(head->atom, "LAMBDA") == 0) {
            CccclForm   *params = rest->car;
            CccclForm   *body   = rest->cdr->car;
            CccclPlanFn *lam    = ccccl_new_fn(p, NULL, 1);
            ccccl_lower_lambda(p, params, body, lam, NULL);
            ccccl_emit(fn, CL_OP_CLOSURE, (int)(lam - p->fns), 0);
            return;
        }
        if (head->kind == CL_FORM_ATOM && strcmp(head->atom, "LABEL") == 0) {
            const char *label_name = rest->car->atom;
            CccclForm  *lambda_form =
                rest->cdr->car; /* (LAMBDA (params) body) */
            CccclForm   *params = lambda_form->cdr->car;
            CccclForm   *body   = lambda_form->cdr->cdr->car;
            CccclPlanFn *lam    = ccccl_new_fn(p, NULL, 1);
            ccccl_lower_lambda(p, params, body, lam, label_name);
            ccccl_emit(fn, CL_OP_CLOSURE, (int)(lam - p->fns), 0);
            return;
        }

        /* Application: (f a...) */
        if (head->kind == CL_FORM_ATOM) {
            int fi = ccccl_find_fn(p, head->atom);
            if (fi >= 0) {
                ccccl_lower_args(p, fn, rest);
                ccccl_emit(fn, CL_OP_CALL_DIRECT, fi, 0);
                return;
            }
        }
        /* (e a...) where e is itself an expression (e.g. a param bound to a
         * closure, or an inline LAMBDA) -- generic apply. */
        ccccl_lower_expr(p, fn, head);
        ccccl_lower_args(p, fn, rest);
        ccccl_emit(fn, CL_OP_CALL, 0, 0);
    }
}

static void ccccl_lower_lambda(CccclPlan *p, CccclForm *params, CccclForm *body,
                               CccclPlanFn *fn, const char *label_name) {
    CccclForm *cur = params;
    while (cur->kind == CL_FORM_PAIR) {
        if (fn->param_count >= CL_MAX_PARAMS) {
            ccccl_lower_error(p, "too many parameters");
            return;
        }
        strncpy(fn->param_lisp[fn->param_count], cur->car->atom,
                sizeof(fn->param_lisp[fn->param_count]) - 1);
        fn->param_sym[fn->param_count] = ccccl_intern_sym(p, cur->car->atom);
        fn->param_count++;
        cur = cur->cdr;
    }
    if (label_name)
        fn->self_label_sym = ccccl_intern_sym(p, label_name);
    /* Body is an implicit PROGN in SectorLISP's LAMBDA, but this POC's
     * accepted subset (docs/LANGUAGE.md) only ever passes a single body
     * expression -- `body` here is that one form, not a list of forms. */
    ccccl_lower_expr(p, fn, body);
}

/* Declares one toplevel `(define (f params...) body)` form: validates its
 * shape and creates its CccclPlanFn (so ccccl_find_fn resolves the name),
 * but does not lower its body yet. Split from lowering so ccccl_compile can
 * run this over every toplevel form first -- see ccccl_lower_toplevel_body
 * and its file comment for why: without this, a body-lowering pass sees
 * only the *earlier* defines' names via ccccl_find_fn, so a forward
 * reference (including true mutual recursion between two defines) falls
 * through to the generic runtime-apply path instead of CL_OP_CALL_DIRECT. */
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

/* Lowers a toplevel define's body into the CccclPlanFn ccccl_declare_toplevel
 * already created for it. Must not call ccccl_new_fn again -- it would reset
 * fn's fields and orphan the declared slot other defines may already
 * reference by index. */
static void ccccl_lower_toplevel_body(CccclPlan *p, CccclForm *form,
                                      CccclPlanFn *fn) {
    if (p->has_error || !fn)
        return;
    CccclForm *sig    = form->cdr->car; /* (name params...) */
    CccclForm *body   = form->cdr->cdr->car;
    CccclForm *params = sig->cdr;
    ccccl_lower_lambda(p, params, body, fn, NULL);
}

#ifdef __cplusplus
}
#endif

#endif /* CCCCL_LOWER_H */
