/* ccccl_comptime.c — the only file in this repository ever passed to cccc.
 *
 * Reads a .lisp source file and lowers it to C, entirely inside the cccc
 * comptime VM, using the pure-C reader/lowering headers pulled in below via
 * `#include @comptime` (confirmed: ordinary static function bodies in an
 * @comptime-routed header are compiled into the comptime program and are
 * callable, including recursively -- see docs/ARCHITECTURE.md).
 *
 * Invocation (see docs/BUILDING.md for the full command):
 *
 *   cccc -c=generated --emit-only src/ccccl_comptime.c \
 *       -Iinclude/ccccl -Iruntime \
 *       -D CCCCL_LISP_PATH='"examples/append.lisp"' \
 *       -o build/append.gen.c
 *
 * `-D` rather than a source `#define` for the input path: comptime bodies
 * do not see ordinary source `#define`s (see MACROS.md's include-scoping
 * section), matching yas's own `-DYAS_AOT_SOURCE_PATH=...` workaround.
 *
 * `--emit-only`: without it, cccc's auto-captured `#include` text and its
 * own re-derived type declarations both land in the generated output and
 * collide -- see docs/ARCHITECTURE.md for the confirmed repro. The runtime
 * declarations this file's generated code needs are added back with
 * EmitDirective below.
 *
 * `examples/append_main.c` (with `main()`) is never passed to cccc at all:
 * `-c=generated` only serializes macro-touched content, not the whole
 * runtime TU (confirmed: a plain `main()` with no comptime involvement
 * produces empty `-c=generated` output). It is plain C, compiled and linked
 * by the system `cc` alongside this file's generated output and
 * runtime/ccccl_rt.c.
 */
#include "ccccl_rt.h"
#include @comptime "ccccl_plan.h"
#include @comptime "ccccl_reader.h"
#include @comptime "ccccl_lower.h"
#include @comptime <stdio.h>
#include @comptime <string.h>

#ifndef CCCCL_LISP_PATH
#define CCCCL_LISP_PATH "examples/append.lisp"
#endif

/* Replays ops[start, end) onto a local Node* stack, returning the single
 * Node* the range leaves behind (every op pushes exactly one value -- see
 * the file comment in ccccl_lower.h). CL_OP_COND recurses into its
 * clauses' own [pred_start,pred_end) / [val_start,val_end) sub-ranges;
 * building an AST node for a branch does not execute anything (it is just
 * a syntactic description of a call site), so this recursion is ordinary,
 * finite call-tree recursion bounded by the source program's own nesting --
 * not the eager-evaluation trap the file comment in ccccl_lower.h warns
 * against for a *real* interpreter over the same op list.
 *
 * `env_ref` is the Node* for this function's *extended* environment (its
 * own params already bound in -- see ccccl_compile below), not the raw
 * incoming `env` parameter: under environment-passing/dynamic scoping, a
 * lookup or a nested call must see this function's own bindings first. */
[[cccc::comptime]]
Node *ccccl_replay_range(CccclPlan *plan, CccclPlanFn *pf, CccclOp *ops,
                          Node *env_ref, int start, int end) {
    Node *stack[256];
    int sp = 0;
    int i;

    for (i = start; i < end; i++) {
        CccclOp *op = &ops[i];
        switch (op->kind) {
        case CL_OP_NIL:
            stack[sp++] = Quote("ccccl_get_nil()");
            break;
        case CL_OP_T:
            stack[sp++] = Quote("ccccl_get_t()");
            break;
        case CL_OP_QUOTE: {
            char buf[64];
            snprintf(buf, sizeof(buf), "ccccl_sym_%d()", op->a);
            stack[sp++] = Quote(buf);
            break;
        }
        case CL_OP_LOOKUP: {
            char buf[64];
            snprintf(buf, sizeof(buf), "ccccl_assoc(ccccl_sym_%d(), $1)", op->a);
            stack[sp++] = Quote(buf, env_ref);
            break;
        }
        case CL_OP_CAR: {
            Node *a = stack[--sp];
            stack[sp++] = Quote("ccccl_car($1)", a);
            break;
        }
        case CL_OP_CDR: {
            Node *a = stack[--sp];
            stack[sp++] = Quote("ccccl_cdr($1)", a);
            break;
        }
        case CL_OP_ATOM: {
            Node *a = stack[--sp];
            stack[sp++] = Quote("ccccl_atom($1)", a);
            break;
        }
        case CL_OP_EQ: {
            Node *b = stack[--sp];
            Node *a = stack[--sp];
            stack[sp++] = Quote("ccccl_eq($1, $2)", a, b);
            break;
        }
        case CL_OP_CONS: {
            Node *d = stack[--sp];
            Node *a = stack[--sp];
            stack[sp++] = Quote("ccccl_cons($1, $2)", a, d);
            break;
        }
        case CL_OP_COND: {
            int clause_start = op->a;
            int clause_count = op->b;
            Node *result = Quote("ccccl_get_nil()"); /* no clause matched */
            int ci;
            for (ci = clause_count - 1; ci >= 0; ci--) {
                CccclCondClause *c = &pf->conds[clause_start + ci];
                Node *pred = ccccl_replay_range(plan, pf, pf->cond_ops, env_ref, c->pred_start, c->pred_end);
                Node *val = ccccl_replay_range(plan, pf, pf->cond_ops, env_ref, c->val_start, c->val_end);
                result = Quote("(($1) != ccccl_get_nil()) ? ($2) : ($3)", pred, val, result);
            }
            stack[sp++] = result;
            break;
        }
        case CL_OP_CLOSURE: {
            CccclPlanFn *lam = &plan->fns[op->a];
            char buf[128];
            snprintf(buf, sizeof(buf), "ccccl_closure(%s, $1)", lam->c_name);
            stack[sp++] = Quote(buf, env_ref);
            break;
        }
        case CL_OP_ARGLIST: {
            int n = op->a;
            Node *items[16];
            Node *list;
            int k;
            for (k = n - 1; k >= 0; k--) items[k] = stack[--sp];
            list = Quote("ccccl_get_nil()");
            for (k = n - 1; k >= 0; k--) list = Quote("ccccl_cons($1, $2)", items[k], list);
            stack[sp++] = list;
            break;
        }
        case CL_OP_CALL: {
            Node *arglist = stack[--sp];
            Node *callee = stack[--sp];
            stack[sp++] = Quote("ccccl_apply($1, $2)", callee, arglist);
            break;
        }
        case CL_OP_CALL_DIRECT: {
            CccclPlanFn *callee_fn = &plan->fns[op->a];
            Node *arglist = stack[--sp];
            char buf[128];
            snprintf(buf, sizeof(buf), "%s($1, $2)", callee_fn->c_name);
            stack[sp++] = Quote(buf, arglist, env_ref);
            break;
        }
        }
    }
    return stack[sp - 1];
}

/* One memoized `ccccl_sym_<n>()` per interned symbol/quoted-atom, backed by
 * a file-scope static cache. GlobalVar + GlobalVarSetStatic is confirmed to
 * survive `-c=generated --emit-only` and to hold its value across calls
 * (see docs/ARCHITECTURE.md). */
[[cccc::comptime]]
void ccccl_gen_sym_fn(Type *lobj_ptr, int idx, const char *text) {
    char cache_name[32], fn_name[32];
    Obj *cache_var, *fn;
    Node *cache_ref, *str_lit;

    snprintf(cache_name, sizeof(cache_name), "ccccl_sym_cache_%d", idx);
    snprintf(fn_name, sizeof(fn_name), "ccccl_sym_%d", idx);

    cache_var = GlobalVar(cache_name, lobj_ptr);
    GlobalVarSetStatic(cache_var, 1);

    fn = MakeFunction(fn_name, lobj_ptr);
    cache_ref = MakeVarRef(cache_name);
    str_lit = MakeStringLiteral(text);
    WithFn(fn) {
        FunctionSetBody(fn, Quote(
            "{ if (!$1) $1 = ccccl_intern($2); return $1; }",
            cache_ref, str_lit));
    }
}

[[cccc::comptime]]
void ccccl_compile(void) {
    static CccclReader reader;
    static CccclPlan plan;
    CccclForm *forms[64];
    CccclPlanFn *toplevel_fns[64];
    Type *lobj_ty, *lobj_ptr;
    int n, i;

    EmitDirective("#include \"ccccl_rt.h\"");

    ccccl_reader_init(&reader);
    ccccl_plan_init(&plan);

    n = ccccl_read_file(&reader, CCCCL_LISP_PATH, forms, 64);
    if (n < 0) {
        MacroErrorAt(NULL, "ccccl: %s", reader.error);
        return;
    }
    /* Two passes: declare every toplevel define first (so ccccl_find_fn
     * resolves every name, including forward and mutually-recursive
     * references), then lower each body. See ccccl_declare_toplevel's
     * comment in ccccl_lower.h. */
    for (i = 0; i < n; i++) toplevel_fns[i] = ccccl_declare_toplevel(&plan, forms[i]);
    for (i = 0; i < n; i++) ccccl_lower_toplevel_body(&plan, forms[i], toplevel_fns[i]);
    if (plan.has_error) {
        MacroErrorAt(NULL, "ccccl: %s", plan.error);
        return;
    }

    lobj_ty = GetType("LObj");
    lobj_ptr = MakePointer(lobj_ty);

    /* Forward-declare every ccccl_sym_<n>() first: append's own body calls
     * them before their definitions otherwise appear in the generated
     * output (their emission order does not follow call order), which is
     * an implicit-declaration error under plain `cc`. */
    for (i = 0; i < plan.sym_count; i++) {
        char fn_name[32];
        Obj *proto;
        snprintf(fn_name, sizeof(fn_name), "ccccl_sym_%d", i);
        proto = FunctionPrototype(fn_name, lobj_ptr);
        PublishNode(proto);
    }
    for (i = 0; i < plan.sym_count; i++)
        ccccl_gen_sym_fn(lobj_ptr, i, plan.syms[i].text);

    /* Both passes below walk plan.fns[] in plain forward creation order.
     * cccc#956 fixed `-c=generated` to forward-declare a generated
     * function's callee (scanning each function's body as it's emitted)
     * regardless of MakeFunction/PublishNode order, so this no longer needs
     * to be worked around here -- covers a LAMBDA nested inside its
     * enclosing function (necessarily created after it) and true mutual
     * recursion between independently-defined toplevel functions alike. */
    {
        Obj *fn_objs[CL_MAX_FNS];
        for (i = 0; i < plan.fn_count; i++) {
            CccclPlanFn *pf = &plan.fns[i];
            fn_objs[i] = MakeFunction(pf->c_name, lobj_ptr);
            FunctionAddParam(fn_objs[i], "args", lobj_ptr);
            FunctionAddParam(fn_objs[i], "env", lobj_ptr);
            PublishNode(fn_objs[i]);
        }

        for (i = 0; i < plan.fn_count; i++) {
            CccclPlanFn *pf = &plan.fns[i];
            Obj *fn = fn_objs[i];
            WithFn(fn) {
                Node *args_ref = MakeParamRef(fn, "args");
                Node *env_ref = MakeParamRef(fn, "env");
                Node *paramlist;
                Node *env_local;
                int k;

                /* env_local = ccccl_bind_list((sym0 sym1 ...), args, env) */
                paramlist = Quote("ccccl_get_nil()");
                for (k = pf->param_count - 1; k >= 0; k--) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "ccccl_cons(ccccl_sym_%d(), $1)", pf->param_sym[k]);
                    paramlist = Quote(buf, paramlist);
                }
                env_local = Quote("ccccl_bind_list($1, $2, $3)", paramlist, args_ref, env_ref);

                if (pf->self_label_sym >= 0) {
                    char buf[160];
                    snprintf(buf, sizeof(buf),
                             "ccccl_bind(ccccl_sym_%d(), ccccl_closure(%s, $1), $1)",
                             pf->self_label_sym, pf->c_name);
                    env_local = Quote(buf, env_local);
                }

                {
                    Node *body = ccccl_replay_range(&plan, pf, pf->ops, env_local, 0, pf->op_count);
                    /* env_local is itself a Node* built once above; the
                     * generated body re-evaluates it inline at each of its
                     * uses, since env_local is never bound to a C local by
                     * this pass. Re-evaluating ccccl_bind_list is cheap
                     * (a handful of ccccl_cons calls) and correct -- it
                     * always rebuilds the same structural env. A named
                     * local variable holding it once would be strictly
                     * better and is a natural follow-on efficiency
                     * improvement, but is not needed for correctness. */
                    FunctionSetBody(fn, MakeReturn(body));
                }
            }
        }
    }
}

ccccl_compile();
