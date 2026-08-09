/* ccccl_plan.h — the Op list and Plan structures.
 *
 * Pure C, no cccc dependency and no dependency on the runtime (ccccl_rt.h):
 * this header is compiled twice — once by plain `cc` for tests/test_lower.c,
 * and once inside the cccc comptime VM via `#include @comptime` from
 * src/ccccl_comptime.c. Following yas's aot/plan.h (see docs/ARCHITECTURE.md).
 *
 * The invariant that lets a flat, postfix Op list stand in for a form tree
 * without recursion on the replay side: every op pushes exactly one value.
 * CL_OP_COND is the one exception a naive interpreter gets wrong — see the
 * header comment on ccccl_lower.h and tests/test_lower.c.
 *
 * Everything here is a fixed-size arena: no malloc, matching the runtime's
 * own placeholder-GC posture and keeping the comptime side of the compiler
 * (which cannot use libc's allocator inside the VM) uniform with the rest.
 */
#ifndef CCCCL_PLAN_H
#define CCCCL_PLAN_H

#ifdef __cplusplus
extern "C" {
#endif

#define CL_MAX_FNS 64
#define CL_MAX_PARAMS 16
#define CL_MAX_OPS 1024
#define CL_MAX_SYMS 256
#define CL_NAME_LEN 64
#define CL_ERROR_LEN 256

typedef enum CccclOpKind {
    CL_OP_NIL,          /* push ccccl_nil                                   */
    CL_OP_T,            /* push ccccl_t                                     */
    CL_OP_QUOTE,        /* a = const index -> push ccccl_const_<a>()        */
    CL_OP_LOOKUP,       /* a = sym index   -> push ccccl_assoc(SYM_a, env)  */
    CL_OP_CAR,          /* pop 1, push ccccl_car(top)                       */
    CL_OP_CDR,          /* pop 1, push ccccl_cdr(top)                       */
    CL_OP_ATOM,         /* pop 1, push ccccl_atom(top)                      */
    CL_OP_EQ,           /* pop 2, push ccccl_eq(a, b)                       */
    CL_OP_CONS,         /* pop 2, push ccccl_cons(a, d)                     */
    CL_OP_COND,         /* a = clause count; see the lazy-evaluation note   */
    CL_OP_CLOSURE,      /* a = fn index -> push ccccl_closure(fn_a, env)    */
    CL_OP_ARGLIST,      /* a = n; pops n, pushes a cons list (last arg first)*/
    CL_OP_CALL,         /* pops arglist then callee, pushes ccccl_apply(...) */
    CL_OP_CALL_DIRECT   /* a = fn index; pops arglist; f(arglist, env)      */
} CccclOpKind;

/* One op. For CL_OP_COND, `a` is the clause count and each clause occupies
 * two *sub-ranges* of ops recorded separately (see CccclCondClause) rather
 * than being inline in the flat list — this is what lets a lazy replayer
 * (the comptime Node* builder, and tests/test_lower.c's reference
 * interpreter) evaluate only the taken branch instead of every operand. */
typedef struct {
    CccclOpKind kind;
    int a, b;
} CccclOp;

/* A COND clause: [pred_start, pred_end) and [val_start, val_end) are op
 * index ranges into the owning CccclPlanFn's *cond_ops* array (not ops) --
 * see the comment on CccclPlanFn::cond_ops for why the two are separate. */
typedef struct {
    int pred_start, pred_end;
    int val_start, val_end;
} CccclCondClause;

#define CL_MAX_COND_CLAUSES 256

typedef struct {
    char lisp_name[CL_NAME_LEN];
    char c_name[CL_NAME_LEN + 32];
    int is_lambda; /* anonymous, gensym'd C name */

    int param_count;
    char param_lisp[CL_MAX_PARAMS][CL_NAME_LEN];
    int param_sym[CL_MAX_PARAMS]; /* index into plan->syms, one per param */

    /* This function's own env-binding: LABEL functions bind their own name
     * at entry (see docs/LOWERING.md). -1 if none. */
    int self_label_sym; /* index into plan->syms, or -1 */

    CccclOp ops[CL_MAX_OPS];
    int op_count;

    /* A COND clause's predicate/value expressions are lowered into this
     * *separate* pool, never into `ops[]` -- CccclCondClause's four indices
     * (below) index into cond_ops, not ops. This is load-bearing, not a
     * style choice: `ops[]` is walked by a single top-to-bottom linear pass
     * (every op pushes exactly one value, see ccccl_lower.h's file
     * comment), and if a clause's ops were interleaved inline into that
     * same array, the linear pass would execute them unconditionally on
     * its way past, in addition to the COND op's own lazy, conditional
     * re-dispatch into them -- redundant and harmless for the comptime
     * replay (building an unused AST node has no effect) but a genuine
     * infinite-recursion bug for a real interpreter (tests/test_lower.c),
     * since an unconditionally-executed recursive CL_OP_CALL_DIRECT inside
     * the untaken branch never terminates. Confirmed by hitting exactly
     * this bug against `append` before splitting the pools. */
    CccclOp cond_ops[CL_MAX_OPS];
    int cond_op_count;

    /* Where ccccl_emit currently appends: &ops/&op_count normally, swapped
     * to &cond_ops/&cond_op_count for the duration of lowering one clause's
     * predicate or value (see ccccl_lower_expr's COND case). A plain
     * save/restore around each nested lowering call, so nested CONDs (a
     * COND inside another COND's clause) work via ordinary C recursion. */
    CccclOp *emit_ops;
    int *emit_count;

    CccclCondClause conds[CL_MAX_COND_CLAUSES];
    int cond_count;
} CccclPlanFn;

typedef struct {
    char text[CL_NAME_LEN];
} CccclSym;

typedef struct {
    CccclPlanFn fns[CL_MAX_FNS];
    int fn_count;

    CccclSym syms[CL_MAX_SYMS];
    int sym_count;

    /* Set on the first unsupported form; naming the form. Once set, lowering
     * stops adding ops (a partial plan is never silently compiled). */
    char error[CL_ERROR_LEN];
    int has_error;
} CccclPlan;

static void ccccl_plan_init(CccclPlan *p) {
    p->fn_count = 0;
    p->sym_count = 0;
    p->error[0] = '\0';
    p->has_error = 0;
}

/* Not thread-safe, not reentrant across plans -- matches the arena-everywhere
 * posture: ccccl_comptime.c and test_lower.c each build exactly one plan. */
#ifdef __cplusplus
}
#endif

#endif /* CCCCL_PLAN_H */
