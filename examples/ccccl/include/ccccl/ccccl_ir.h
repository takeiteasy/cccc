/* ccccl_ir.h — the tree IR: expressions, bindings, functions, the plan.
 *
 * Pure C, no cccc dependency and no dependency on the runtime (ccccl_rt.h) —
 * compiled twice, once by plain `cc`, once inside the cccc comptime VM via
 * `#include @comptime` from src/ccccl_comptime.c.
 *
 * A CccclExpr is an ordinary tree (unlike the old flat postfix-op design):
 * a COND clause's predicate and value are just child expressions, walked by
 * ordinary recursive descent. There is no eager/lazy-evaluation hazard to
 * design around here the way a flat op *list* had — a tree only ever visits
 * the branch the emitter actually recurses into.
 *
 * Every fixed-size arena here is sized for this project's own six examples,
 * not general-purpose use — see each `CL_MAX_*` constant.
 */
#ifndef CCCCL_IR_H
#define CCCCL_IR_H

#ifdef __cplusplus
extern "C" {
#endif

#define CL_MAX_FNS      64
#define CL_MAX_BINDINGS 64 /* per function: params + captures + lets */
#define CL_MAX_EXPRS    4096
#define CL_MAX_SYMS     256
#define CL_MAX_CHILDREN 16 /* per CK_PROGN/CK_COND-clause-list/call args */
#define CL_NAME_LEN     64
#define CL_ERROR_LEN    256

typedef enum CccclExprKind {
    CK_NIL,
    CK_T,
    CK_INT,        /* ival */
    CK_QUOTE_ATOM, /* a = sym index */
    CK_VAR,        /* a = binding index, in the owning fn's own table */
    CK_CAR,
    CK_CDR,
    CK_ATOM,       /* unary; a = operand */
    CK_EQ,
    CK_CONS,
    CK_ADD,
    CK_SUB,
    CK_MUL,
    CK_DIV,
    CK_MOD,
    CK_LT,
    CK_NUMEQ, /* binary; a, b = operands */
    CK_PRINT, /* unary; a = operand; the emitted call evaluates to its
               * own argument, so `print` composes in expression
               * position (matches Common Lisp's PRINT) */
    CK_IF,    /* a = cond, b = then, c = else */
    CK_COND,  /* children[2*i]/[2*i+1] = clause i's pred/val, i<child_count/2 */
    CK_LET,   /* let_start/let_count = binding slots (parallel to
               * children[0..let_count) init exprs); a = body */
    CK_PROGN, /* children[0..child_count); last is the value */
    CK_LAMBDA,      /* a = fn index (the lambda's own CccclPlanFn) */
    CK_CALL_DIRECT, /* a = fn index; children[0..child_count) = args */
    CK_APPLY,       /* a = callee expr index; children[0..child_count) = args */
    CK_FN_VALUE     /* a = fn index; a bare reference to a *toplevel*
                     * function's name in value (non-call-head) position --
                     * emits a closure over that function's thunk, see
                     * CccclPlanFn::needs_thunk */
} CccclExprKind;

typedef struct CccclExpr {
    CccclExprKind kind;
    long long     ival; /* CK_INT */
    int           a, b; /* operand/fn/binding indices, or
                         * CK_IF's cond/then (c below) */
    int c;              /* CK_IF's else */
    int children[CL_MAX_CHILDREN];
    int child_count;
    /* CK_LET: let_bindings[i] is the fn-table binding index that
     * children[i]'s init expression initializes -- NOT necessarily
     * contiguous (an earlier binding's own init expression can itself
     * append a BIND_CAPTURE binding to the same function, e.g. if it's a
     * LAMBDA capturing an outer variable, interleaving with this LET's own
     * binding indices), so each is recorded explicitly rather than
     * assumed to start at a fixed offset. */
    int let_bindings[CL_MAX_CHILDREN];
    int let_count;
    int is_self_tail; /* CK_CALL_DIRECT: a tail call to
                       * the function currently being
                       * lowered -- the emitter turns
                       * this into loop-and-reassign
                       * instead of a recursive call */
} CccclExpr;

typedef enum CccclBindKind {
    BIND_PARAM,
    BIND_LOCAL,
    BIND_CAPTURE
} CccclBindKind;

typedef struct {
    char          lisp_name[CL_NAME_LEN];
    char          c_name[CL_NAME_LEN + 16];
    CccclBindKind kind;
    /* BIND_CAPTURE only: the binding index in the *parent* (lexically
     * enclosing) function that this capture slot reads from at the
     * closure's creation site — see ccccl_lower.h's resolve_symbol(). */
    int capture_source;
} CccclBinding;

typedef struct {
    char lisp_name[CL_NAME_LEN];
    char c_name[CL_NAME_LEN + 32];
    int  is_lambda; /* anonymous, gensym'd C name */

    /* The one synthesized function holding a "program"-mode .lisp file's
     * toplevel executable forms (see ccccl_lower.h's
     * ccccl_lower_toplevel_exprs). Named `ccccl_toplevel`, emitted `static`,
     * called from the generated `main()`. Never a lambda, never has a Lisp
     * name that could resolve as a call target (ccccl_find_fn skips it). */
    int          is_toplevel_body;

    CccclBinding bindings[CL_MAX_BINDINGS];
    int          binding_count;
    int          param_count; /* bindings[0, param_count) */

    /* This function's own env-binding: LABEL functions bind their own name
     * to their own closure inside their own body. -1 if none. */
    int self_label_binding; /* index into bindings[], kind BIND_CAPTURE, or -1
                             */

    int body; /* expr index: this function's single body expression */

    /* Set (once, on first use in value position -- see ccccl_lower.h) when
     * a *toplevel* (non-lambda) function is ever passed as a value rather
     * than called directly; the emitter then also generates a small
     * `NAME__thunk(LObj *captures, LObj *args)` wrapper unpacking `args`
     * positionally and calling the real `NAME(a, b, ...)`, since a
     * toplevel define's real C signature has no (captures, args) closure
     * entry point of its own. */
    int needs_thunk;
} CccclPlanFn;

typedef struct {
    char text[CL_NAME_LEN];
} CccclSym;

typedef struct {
    CccclExpr   exprs[CL_MAX_EXPRS];
    int         expr_count;

    CccclPlanFn fns[CL_MAX_FNS];
    int         fn_count;

    CccclSym    syms[CL_MAX_SYMS];
    int         sym_count;

    /* Set on the first unsupported form; naming the form. Once set, lowering
     * stops adding IR (a partial plan is never silently compiled). */
    char error[CL_ERROR_LEN];
    int  has_error;
} CccclPlan;

static void ccccl_plan_init(CccclPlan *p) {
    p->expr_count = 0;
    p->fn_count   = 0;
    p->sym_count  = 0;
    p->error[0]   = '\0';
    p->has_error  = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* CCCCL_IR_H */
