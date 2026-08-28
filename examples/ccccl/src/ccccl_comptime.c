/* ccccl_comptime.c — the only file in this repository ever passed to cccc.
 *
 * Reads a .lisp source file and lowers it to C, entirely inside the cccc
 * comptime VM, using the pure-C reader/IR/lowering headers pulled in below
 * via `#include @comptime` (ordinary static function bodies in an
 * @comptime-routed header are compiled into the comptime program and are
 * callable, including recursively). `ccccl_rt.h` is pulled in with
 * `#include @shared`, not `@comptime` — that is what lets generated code
 * reference `ccccl_nil`/`ccccl_t` directly (a bare extern var from a
 * plain `#include` is rejected by `Quote()`'s identifier resolver; a
 * `@shared` one is not).
 *
 * Invocation:
 *
 *   cccc -c=generated src/ccccl_comptime.c \
 *       -Iinclude/ccccl -Iruntime \
 *       -D CCCCL_LISP_PATH='"examples/append.lisp"' \
 *       -o build/append.gen.c
 *
 * `-D` rather than a source `#define` for the input path: comptime bodies
 * do not see ordinary source `#define`s (see man/MACROS.md's
 * include-scoping section).
 *
 * `examples/NAME_main.c` (with `main()`) is never passed to cccc at all:
 * `-c=generated` only serializes macro-touched content, not the whole
 * runtime TU. It is plain C, compiled and linked by the system `cc`
 * alongside this file's generated output and runtime/ccccl_rt.c.
 *
 * Emission strategy: every function body is composed as nested `Quote()`
 * text, splicing sub-expression `Node*`s in with `$N` and variable-arity
 * argument lists with `$@N`+`NodeList` — never `WithBlock`/`BlockAddStmt`/
 * `MakeIf`. `continue`/`break` as bare `Quote("continue;")` text is
 * rejected ("stray continue") the moment that `Quote()` call is parsed,
 * before the resulting node is ever spliced anywhere — a *separately*
 * built statement node is parsed (and rejected, if unattached) in
 * isolation from wherever it later gets spliced, so neither keyword can
 * ever be made to work by embedding it inside a *later* template that
 * happens to also spell the enclosing loop. A self tail call is therefore
 * lowered as a repeat-flag loop instead (`ccccl_again`, set/tested by
 * ordinary assignments) rather than `continue` — see the self-tail branch
 * of `ccccl_emit_stmt` and the loop wrap in `ccccl_emit_function`.
 *
 * A `$N` splice is always parsed in *expression* position, even when the
 * `Node*` behind it is already a complete statement (an `if`, a `return`,
 * an assignment built by an earlier `Quote()` call) — so every statement
 * hole needs an explicit trailing `;` in the *template text itself*
 * (`"{ $1; $2; }"`, not `"{ $1 $2 }"`), or the parser desyncs one token
 * after the hole ("expected ';'"). Confirmed against a throwaway probe:
 * `Quote("{ $1 }", stmt)` fails, `Quote("{ $1; }", stmt)` succeeds, even
 * though `stmt` is already `;`-terminated on its own. `ccccl_emit_val`
 * builds a pure expression tree (leaf/primitive forms); `ccccl_emit_stmt`
 * is destination-passing (it assigns the expression's value into an
 * already-declared `dest`), used for the control-flow forms
 * (`IF`/`COND`/`LET`/`PROGN`) and for a self tail call, which becomes
 * parameter reassignment plus setting a repeat flag, inside a
 * `while (flag)` wrapping the whole function body, instead of a recursive
 * C call.
 */
#include @shared "ccccl_rt.h"
#include @comptime "ccccl_form.h"
#include @comptime "ccccl_ir.h"
#include @comptime "ccccl_lower.h"
#include @comptime < stdio.h>
#include @comptime < string.h>

#ifndef CCCCL_LISP_PATH
#define CCCCL_LISP_PATH "examples/append.lisp"
#endif

/* One memoized `ccccl_sym_<n>()` per interned symbol/quoted-atom, backed by
 * a file-scope static cache — survives `-c=generated` and holds its value
 * across calls (GlobalVar + GlobalVarSetStatic). */
[[cccc::comptime]]
void ccccl_gen_sym_fn(Type *lobj_ptr, int idx, const char *text) {
    char  cache_name[32], fn_name[32];
    Obj  *cache_var, *fn;
    Node *cache_ref, *str_lit;

    snprintf(cache_name, sizeof(cache_name), "ccccl_sym_cache_%d", idx);
    snprintf(fn_name, sizeof(fn_name), "ccccl_sym_%d", idx);

    cache_var = GlobalVar(cache_name, lobj_ptr);
    GlobalVarSetStatic(cache_var, 1);

    fn        = MakeFunction(fn_name, lobj_ptr);
    cache_ref = MakeVarRef(cache_name);
    str_lit   = MakeStringLiteral(text);
    WithFn(fn) {
        FunctionSetBody(fn,
                        Quote("{ if (!$1) $1 = ccccl_intern($2); return $1; }",
                              cache_ref, str_lit));
    }
}

[[cccc::comptime]]
Node *ccccl_emit_val(CccclPlan *plan, CccclPlanFn *fn, Node **bn,
                     Type *lobj_ptr, int idx);
[[cccc::comptime]]
Node *ccccl_emit_stmt(CccclPlan *plan, CccclPlanFn *fn, Node **bn,
                      Type *lobj_ptr, int idx, Node *dest, Node *again);

/* Folds `stmts[0..n)` followed by `trailer` into one nested compound
 * statement: `{ stmts[0]; { stmts[1]; { ... { trailer; } ... } } }`. Shared
 * by LET (bindings then body), PROGN (side-effect forms then the tail
 * value), and a self tail call (temp assignments, then param
 * reassignments, then setting the repeat flag). */
[[cccc::comptime]]
Node *ccccl_seq(Node **stmts, int n, Node *trailer) {
    Node *acc = trailer;
    int   i;
    for (i = n - 1; i >= 0; i--)
        acc = Quote("{ $1; $2; }", stmts[i], acc);
    return acc;
}

/* Builds a call to a generated function with a variable-arity argument
 * list: `Quote("<c_name>($@1)", NodeList(argv, argc))`. */
[[cccc::comptime]]
Node *ccccl_call_direct(const char *c_name, Node **argv, int argc) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s($@1)", c_name);
    return Quote(buf, NodeList(argv, argc));
}

/* Folds argv[0..argc) into a cons list, argv[0] at the head -- shared by
 * CK_APPLY's arglist and CK_LAMBDA's captures list. */
[[cccc::comptime]]
Node *ccccl_cons_list(Node **argv, int argc) {
    Node *list = Quote("ccccl_nil");
    int   i;
    for (i = argc - 1; i >= 0; i--)
        list = Quote("ccccl_cons($1, $2)", argv[i], list);
    return list;
}

/* CK_LAMBDA: builds the captures cons list (one entry per BIND_CAPTURE
 * binding, in binding-table order — must match ccccl_emit_function's own
 * `ccccl_nth(captures, k)` unpacking prologue, k = 0, 1, 2, ...) and the
 * closure expression itself. `bn` is the *enclosing* (currently being
 * emitted) function's own binding-node table — a capture's value is read
 * from there via its `capture_source` index. A LABEL'd lambda's
 * self-reference slot gets a `ccccl_nil` placeholder here, patched in by
 * `ccccl_closure_self` — see ccccl_rt.h. */
[[cccc::comptime]]
Node *ccccl_emit_lambda(CccclPlan *plan, Node **bn, int fi) {
    CccclPlanFn *lam = &plan->fns[fi];
    Node        *argv[CL_MAX_BINDINGS];
    int          n = 0, i, self_slot = -1;

    for (i = 0; i < lam->binding_count; i++) {
        if (lam->bindings[i].kind != BIND_CAPTURE)
            continue;
        if (i == lam->self_label_binding) {
            self_slot = n;
            argv[n++] = Quote("ccccl_nil");
        } else {
            argv[n++] = bn[lam->bindings[i].capture_source];
        }
    }
    {
        Node *caps = ccccl_cons_list(argv, n);
        char  buf[80];
        if (self_slot >= 0) {
            snprintf(buf, sizeof(buf), "ccccl_closure_self(%s, $1, %d)",
                     lam->c_name, self_slot);
        } else {
            snprintf(buf, sizeof(buf), "ccccl_closure(%s, $1)", lam->c_name);
        }
        return Quote(buf, caps);
    }
}

/* Pure-expression form. CK_IF/CK_COND/CK_LET/CK_PROGN never reach here —
 * they only ever appear in a tail position or a LET/PROGN's own
 * sequencing, both of which route through ccccl_emit_stmt. */
[[cccc::comptime]]
Node *ccccl_emit_val(CccclPlan *plan, CccclPlanFn *fn, Node **bn,
                     Type *lobj_ptr, int idx) {
    CccclExpr *e = &plan->exprs[idx];
#define VAL(k) ccccl_emit_val(plan, fn, bn, lobj_ptr, (k))
    switch (e->kind) {
        case CK_NIL:
            return Quote("ccccl_nil");
        case CK_T:
            return Quote("ccccl_t");
        case CK_INT:
            return Quote("ccccl_int($1)", MakeIntLiteral(e->ival));
        case CK_QUOTE_ATOM: {
            char buf[32];
            snprintf(buf, sizeof(buf), "ccccl_sym_%d()", e->a);
            return Quote(buf);
        }
        case CK_VAR:
            return bn[e->a];
        case CK_CAR:
            return Quote("ccccl_car($1)", VAL(e->a));
        case CK_CDR:
            return Quote("ccccl_cdr($1)", VAL(e->a));
        case CK_ATOM:
            return Quote("ccccl_atom($1)", VAL(e->a));
        case CK_PRINT:
            return Quote("ccccl_print_stdout($1)", VAL(e->a));
        case CK_EQ:
            return Quote("ccccl_eq($1, $2)", VAL(e->a), VAL(e->b));
        case CK_CONS:
            return Quote("ccccl_cons($1, $2)", VAL(e->a), VAL(e->b));
        case CK_ADD:
            return Quote("ccccl_add($1, $2)", VAL(e->a), VAL(e->b));
        case CK_SUB:
            return Quote("ccccl_sub($1, $2)", VAL(e->a), VAL(e->b));
        case CK_MUL:
            return Quote("ccccl_mul($1, $2)", VAL(e->a), VAL(e->b));
        case CK_DIV:
            return Quote("ccccl_div($1, $2)", VAL(e->a), VAL(e->b));
        case CK_MOD:
            return Quote("ccccl_mod($1, $2)", VAL(e->a), VAL(e->b));
        case CK_LT:
            return Quote("ccccl_num_lt($1, $2)", VAL(e->a), VAL(e->b));
        case CK_NUMEQ:
            return Quote("ccccl_num_eq($1, $2)", VAL(e->a), VAL(e->b));
        case CK_LAMBDA:
            return ccccl_emit_lambda(plan, bn, e->a);
        case CK_FN_VALUE: {
            char buf[80];
            snprintf(buf, sizeof(buf), "ccccl_closure(%s__thunk, ccccl_nil)",
                     plan->fns[e->a].c_name);
            return Quote(buf);
        }
        case CK_CALL_DIRECT: {
            Node *argv[CL_MAX_CHILDREN];
            int   i;
            for (i = 0; i < e->child_count; i++)
                argv[i] = VAL(e->children[i]);
            return ccccl_call_direct(plan->fns[e->a].c_name, argv,
                                     e->child_count);
        }
        case CK_APPLY: {
            Node *argv[CL_MAX_CHILDREN];
            int   i;
            for (i = 0; i < e->child_count; i++)
                argv[i] = VAL(e->children[i]);
            {
                Node *callee  = VAL(e->a);
                Node *arglist = ccccl_cons_list(argv, e->child_count);
                return Quote("ccccl_apply($1, $2)", callee, arglist);
            }
        }
        default:
            MacroErrorAt(NULL,
                         "ccccl: internal error: kind %d reached emit_val",
                         (int)e->kind);
            return Quote("ccccl_nil");
    }
#undef VAL
    (void)fn;
    (void)lobj_ptr;
}

/* Destination-passing form: returns a statement that assigns `idx`'s value
 * into `dest` (already declared -- MakeLocalVar, or a param/capture ref).
 * IF/COND/LET/PROGN recurse with the *same* dest, so a self tail call deep
 * inside any of them still reaches the loop-and-reassign case below. */
[[cccc::comptime]]
Node *ccccl_emit_stmt(CccclPlan *plan, CccclPlanFn *fn, Node **bn,
                      Type *lobj_ptr, int idx, Node *dest, Node *again) {
    CccclExpr *e = &plan->exprs[idx];

    if (e->kind == CK_CALL_DIRECT && e->is_self_tail) {
        static int g_tmp_counter = 0;
        Node      *stmts[2 * CL_MAX_CHILDREN];
        Node      *tmp[CL_MAX_CHILDREN];
        int        n = 0, i;
        for (i = 0; i < e->child_count; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "ccccl_tt_%d", g_tmp_counter++);
            tmp[i] = MakeLocalVar(buf, lobj_ptr);
            stmts[n++] =
                Quote("$1 = $2;", tmp[i],
                      ccccl_emit_val(plan, fn, bn, lobj_ptr, e->children[i]));
        }
        for (i = 0; i < e->child_count; i++)
            stmts[n++] = Quote("$1 = $2;", bn[i], tmp[i]);
        /* No literal `continue;`/`break;` here: Quote() rejects either as
         * "stray" unless it appears textually inside a real loop header
         * spelled out in the SAME Quote() call -- a spliced statement node
         * built by an earlier, separate Quote() call is parsed (and
         * rejected) before it is ever attached anywhere. Setting `again`
         * is an ordinary assignment instead, composes through ccccl_seq
         * like any other statement, and is equivalent to `continue` here
         * only because is_self_tail is set exclusively under is_tail (see
         * ccccl_lower.h) -- nothing in this function ever runs after a
         * self-tail-call site on its own path. */
        return ccccl_seq(stmts, n, Quote("$1 = 1;", again));
    }

    switch (e->kind) {
        case CK_IF:
            return Quote(
                "if (($1) != ccccl_nil) { $2; } else { $3; }",
                ccccl_emit_val(plan, fn, bn, lobj_ptr, e->a),
                ccccl_emit_stmt(plan, fn, bn, lobj_ptr, e->b, dest, again),
                ccccl_emit_stmt(plan, fn, bn, lobj_ptr, e->c, dest, again));
        case CK_COND: {
            Node *chain = Quote("$1 = ccccl_nil;", dest);
            int   ci;
            for (ci = e->child_count - 2; ci >= 0; ci -= 2) {
                Node *pred =
                    ccccl_emit_val(plan, fn, bn, lobj_ptr, e->children[ci]);
                Node *val = ccccl_emit_stmt(plan, fn, bn, lobj_ptr,
                                            e->children[ci + 1], dest, again);
                chain     = Quote("if (($1) != ccccl_nil) { $2; } else { $3; }",
                                  pred, val, chain);
            }
            return chain;
        }
        case CK_LET: {
            Node *stmts[CL_MAX_CHILDREN];
            int   i;
            for (i = 0; i < e->let_count; i++) {
                Node *loc = MakeLocalVar(
                    fn->bindings[e->let_bindings[i]].c_name, lobj_ptr);
                bn[e->let_bindings[i]] = loc;
                stmts[i]               = Quote(
                    "$1 = $2;", loc,
                    ccccl_emit_val(plan, fn, bn, lobj_ptr, e->children[i]));
            }
            return ccccl_seq(
                stmts, e->let_count,
                ccccl_emit_stmt(plan, fn, bn, lobj_ptr, e->a, dest, again));
        }
        case CK_PROGN: {
            Node *stmts[CL_MAX_CHILDREN];
            int   i;
            if (e->child_count == 0)
                return Quote("$1 = ccccl_nil;", dest);
            for (i = 0; i < e->child_count - 1; i++) {
                int           ci = e->children[i];
                CccclExprKind ck = plan->exprs[ci].kind;
                if (ck == CK_IF || ck == CK_COND || ck == CK_LET ||
                    ck == CK_PROGN) {
                    /* A control-flow form in non-tail position -- reachable
                     * only from a toplevel PROGN (a define body's PROGN
                     * never nests one non-last), and ccccl_emit_val has no
                     * case for these four. Route it through emit_stmt
                     * targeting `dest` itself: PROGN's value is its last
                     * form's, so these earlier writes into `dest` are dead
                     * stores the host compiler drops -- and reusing `dest`
                     * (already live via the tail child and the `return`)
                     * avoids a set-but-unused scratch local under -Wall. */
                    stmts[i] = ccccl_emit_stmt(plan, fn, bn, lobj_ptr, ci, dest,
                                               again);
                } else {
                    stmts[i] = Quote(
                        "$1;", ccccl_emit_val(plan, fn, bn, lobj_ptr, ci));
                }
            }
            return ccccl_seq(stmts, e->child_count - 1,
                             ccccl_emit_stmt(plan, fn, bn, lobj_ptr,
                                             e->children[e->child_count - 1],
                                             dest, again));
        }
        default:
            return Quote("$1 = $2;", dest,
                         ccccl_emit_val(plan, fn, bn, lobj_ptr, idx));
    }
}

/* Whether `idx` (in tail position within `fn`) reaches a self tail call
 * anywhere along the paths lowering actually marks tail: IF's two
 * branches, COND's clause values, LET's body, PROGN's last form. Anywhere
 * else a CK_CALL_DIRECT node's is_self_tail is never set (lowering only
 * sets it under is_tail), so those paths need no separate check. Decides
 * whether ccccl_emit_function wraps the whole body in `for (;;) { ... }`
 * -- the `continue;` self-tail-call emission has no loop to continue
 * without it. */
[[cccc::comptime]]
int ccccl_has_tail_call(CccclPlan *plan, int idx) {
    CccclExpr *e = &plan->exprs[idx];
    switch (e->kind) {
        case CK_CALL_DIRECT:
            return e->is_self_tail;
        case CK_IF:
            return ccccl_has_tail_call(plan, e->b) ||
                   ccccl_has_tail_call(plan, e->c);
        case CK_COND: {
            int ci;
            for (ci = 1; ci < e->child_count; ci += 2)
                if (ccccl_has_tail_call(plan, e->children[ci]))
                    return 1;
            return 0;
        }
        case CK_LET:
            return ccccl_has_tail_call(plan, e->a);
        case CK_PROGN:
            return e->child_count > 0 &&
                   ccccl_has_tail_call(plan, e->children[e->child_count - 1]);
        default:
            return 0;
    }
}

/* Creates the Obj for `pf` (real C params for a toplevel define; the
 * uniform `(captures, args)` closure entry point for a LAMBDA/LABEL) and,
 * for a toplevel define ever used as a value (CccclPlanFn::needs_thunk),
 * its `NAME__thunk` wrapper's Obj too. Both are PublishNode'd here, before
 * any body is set, so forward/mutually-recursive references resolve
 * regardless of plan.fns[] creation order (matches cccc's own
 * `-c=generated` forward-declaration scan of each function's body as it's
 * emitted). */
[[cccc::comptime]]
void ccccl_declare_fn_objs(CccclPlan *plan, Type *lobj_ptr, Obj **fn_objs,
                           Obj **thunk_objs) {
    int i;
    for (i = 0; i < plan->fn_count; i++) {
        CccclPlanFn *pf  = &plan->fns[i];
        Obj         *obj = MakeFunction(pf->c_name, lobj_ptr);
        if (pf->is_lambda) {
            FunctionAddParam(obj, "captures", lobj_ptr);
            FunctionAddParam(obj, "args", lobj_ptr);
        } else {
            int k;
            for (k = 0; k < pf->param_count; k++)
                FunctionAddParam(obj, pf->bindings[k].c_name, lobj_ptr);
        }
        if (pf->is_toplevel_body)
            FunctionSetStatic(obj, 1);
        PublishNode(obj);
        fn_objs[i]    = obj;

        thunk_objs[i] = NULL;
        if (pf->needs_thunk) {
            char thunk_name[96];
            Obj *thunk;
            snprintf(thunk_name, sizeof(thunk_name), "%s__thunk", pf->c_name);
            thunk = MakeFunction(thunk_name, lobj_ptr);
            FunctionSetStatic(thunk, 1);
            FunctionAddParam(thunk, "captures", lobj_ptr);
            FunctionAddParam(thunk, "args", lobj_ptr);
            PublishNode(thunk);
            thunk_objs[i] = thunk;
        }
    }
}

/* Fills in `fn_obj`'s body: entry prologue (real params need none; a
 * LAMBDA/LABEL unpacks its own params and captures into locals via
 * ccccl_nth), then the body expression via ccccl_emit_stmt into a `result`
 * local, wrapped in `for (;;) { ... }` only if a self tail call is
 * anywhere in tail position (ccccl_has_tail_call). */
[[cccc::comptime]]
void ccccl_emit_function(CccclPlan *plan, CccclPlanFn *pf, Obj *fn_obj,
                         Type *lobj_ptr) {
    Node *bn[CL_MAX_BINDINGS];
    Node *prologue[CL_MAX_BINDINGS];
    int   prologue_n = 0;
    int   i;
    WithFn(fn_obj) {
        Node *result   = MakeLocalVar("result", lobj_ptr);
        Node *args_ref = pf->is_lambda ? MakeParamRef(fn_obj, "args") : NULL;
        Node *caps_ref =
            pf->is_lambda ? MakeParamRef(fn_obj, "captures") : NULL;

        for (i = 0; i < pf->binding_count; i++) {
            CccclBinding *b = &pf->bindings[i];
            if (b->kind == BIND_PARAM) {
                if (pf->is_lambda) {
                    Node *loc = MakeLocalVar(b->c_name, lobj_ptr);
                    prologue[prologue_n++] =
                        Quote("$1 = ccccl_nth($2, $3);", loc, args_ref,
                              MakeIntLiteral(i));
                    bn[i] = loc;
                } else {
                    bn[i] = MakeParamRef(fn_obj, b->c_name);
                }
            } else if (b->kind == BIND_CAPTURE) {
                int k = 0, j;
                for (j = 0; j < i; j++)
                    if (pf->bindings[j].kind == BIND_CAPTURE)
                        k++;
                {
                    Node *loc = MakeLocalVar(b->c_name, lobj_ptr);
                    prologue[prologue_n++] =
                        Quote("$1 = ccccl_nth($2, $3);", loc, caps_ref,
                              MakeIntLiteral(k));
                    bn[i] = loc;
                }
            }
        }

        {
            int   has_tail = ccccl_has_tail_call(plan, pf->body);
            Node *again =
                has_tail ? MakeLocalVar("ccccl_again", GetType("int")) : NULL;
            Node *body_stmt = ccccl_emit_stmt(plan, pf, bn, lobj_ptr, pf->body,
                                              result, again);
            Node *loop = has_tail ? Quote("$1 = 1; while ($1) { $1 = 0; $2; }",
                                          again, body_stmt)
                                  : body_stmt;
            Node *full = ccccl_seq(prologue, prologue_n, loop);
            FunctionSetBody(fn_obj, Quote("{ $1; return $2; }", full, result));
        }
    }
}

/* A toplevel define's real C signature has no `(captures, args)` closure
 * entry point of its own -- the thunk unpacks `args` positionally and
 * calls the real function, so `ccccl_apply` can still call it when it's
 * used as a value (see CK_FN_VALUE). */
[[cccc::comptime]]
void ccccl_emit_thunk(CccclPlanFn *pf, Obj *thunk_obj, Type *lobj_ptr) {
    WithFn(thunk_obj) {
        Node *args_ref = MakeParamRef(thunk_obj, "args");
        Node *argv[CL_MAX_BINDINGS];
        int   i;
        for (i = 0; i < pf->param_count; i++)
            argv[i] = Quote("ccccl_nth($1, $2)", args_ref, MakeIntLiteral(i));
        FunctionSetBody(thunk_obj, MakeReturn(ccccl_call_direct(
                                       pf->c_name, argv, pf->param_count)));
    }
}

[[cccc::comptime]]
void ccccl_compile(void) {
    static CccclReader reader;
    static CccclPlan   plan;
    CccclForm         *forms[64];
    CccclPlanFn       *toplevel_fns[64];
    Obj               *fn_objs[CL_MAX_FNS];
    Obj               *thunk_objs[CL_MAX_FNS];
    Type              *lobj_ty, *lobj_ptr;
    CccclPlanFn       *prog_fn = NULL;
    int                n, i, have_prog = 0;

    ccccl_reader_init(&reader);
    ccccl_plan_init(&plan);

    n = ccccl_read_file(&reader, CCCCL_LISP_PATH, forms, 64);
    if (n < 0) {
        MacroErrorAt(NULL, "ccccl: %s", reader.error);
        return;
    }
    for (i = 0; i < n; i++)
        toplevel_fns[i] = ccccl_declare_toplevel(&plan, forms[i]);
    for (i = 0; i < n; i++)
        ccccl_lower_toplevel_body(&plan, forms[i], toplevel_fns[i]);

    /* "Program" mode: any toplevel form that isn't a `(define ...)` is an
     * executable form. Collect them into one synthesized `ccccl_toplevel`
     * function; a generated `main()` (emitted last, below) calls it. A file
     * with only defines stays "library" mode -- no ccccl_toplevel, no main,
     * driven by a hand-written host TU as before. */
    for (i = 0; i < n; i++)
        if (!ccccl_form_is_define(forms[i])) {
            have_prog = 1;
            break;
        }
    if (have_prog && !plan.has_error) {
        prog_fn = ccccl_new_toplevel_fn(&plan);
        ccccl_lower_toplevel_exprs(&plan, prog_fn, forms, n);
    }

    if (plan.has_error) {
        MacroErrorAt(NULL, "ccccl: %s", plan.error);
        return;
    }

    lobj_ty  = GetType("LObj");
    lobj_ptr = MakePointer(lobj_ty);

    /* Symbol cache accessors first -- every generated function body may
     * call them, so they must all be forward-declared before any body is
     * emitted (matches the old CL_OP_QUOTE forward-decl scheme). */
    for (i = 0; i < plan.sym_count; i++) {
        char fn_name[32];
        Obj *proto;
        snprintf(fn_name, sizeof(fn_name), "ccccl_sym_%d", i);
        proto = FunctionPrototype(fn_name, lobj_ptr);
        PublishNode(proto);
    }
    for (i = 0; i < plan.sym_count; i++)
        ccccl_gen_sym_fn(lobj_ptr, i, plan.syms[i].text);

    ccccl_declare_fn_objs(&plan, lobj_ptr, fn_objs, thunk_objs);
    for (i = 0; i < plan.fn_count; i++)
        ccccl_emit_function(&plan, &plan.fns[i], fn_objs[i], lobj_ptr);
    for (i = 0; i < plan.fn_count; i++)
        if (thunk_objs[i])
            ccccl_emit_thunk(&plan.fns[i], thunk_objs[i], lobj_ptr);

    /* Emitted last, so `ccccl_toplevel`'s own definition already precedes it
     * (no inserted forward declaration). `ccccl_rt_init` /
     * `ccccl_newline_stdout` resolve because ccccl_rt.h is `#include @shared`
     * -- the same reason PRINT can call `ccccl_print_stdout`. */
    if (prog_fn) {
        Obj *main_obj = MakeFunction("main", GetType("int"));
        WithFn(main_obj) {
            FunctionSetBody(main_obj,
                            Quote("{ ccccl_rt_init(); ccccl_toplevel(); "
                                  "ccccl_newline_stdout(); return 0; }"));
        }
    }
}

ccccl_compile();
