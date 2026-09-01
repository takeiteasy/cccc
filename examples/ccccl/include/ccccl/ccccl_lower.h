/* ccccl_lower.h — form tree -> tree IR (ccccl_ir.h): declarations.
 *
 * Pure C. The definitions live in src/ccccl_lower.c, compiled twice: once by
 * plain `cc`, once inside the cccc comptime VM via src/ccccl_comptime.c's
 * `#include @comptime "ccccl_lower.h"` + on-demand body forwarding.
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
 * statically; free variables are resolved at lowering time and captured
 * positionally, read back via `ccccl_captured` at the generated function's
 * entry (see src/ccccl_comptime.c).
 *
 * A symbol unresolved anywhere in its enclosing scope chain — including a
 * toplevel bare reference — is a lowering error, not a silent NIL: this is
 * the "real names" design's other half. A bare reference to a *known
 * toplevel function's* name in value position (not as a call head) is not
 * an error — it lowers to CK_FN_VALUE and marks that function `needs_thunk`
 * (see ccccl_ir.h), since a toplevel define's real C signature has no
 * closure entry point of its own to hand to `ccccl_apply`.
 */
#ifndef CCCCL_LOWER_H
#define CCCCL_LOWER_H

#include "ccccl_ir.h"
#include "ccccl_form.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A toplevel `(define ...)` form -- as opposed to a toplevel *executable*
 * form (any other list, or an atom), which "program"-mode .lisp files carry
 * alongside their defines. Only checks the head; ccccl_declare_toplevel
 * validates the rest of the shape. */
int ccccl_form_is_define(CccclForm *form);

/* Declares one toplevel `(define (f params...) body)` form: validates its
 * shape and creates its CccclPlanFn (so name resolution finds it), but does
 * not lower its params/body yet. Split from lowering so ccccl_compile can
 * run this over every toplevel form first, making forward references (and
 * true mutual recursion) resolve as direct calls. Returns NULL without an
 * error for a toplevel form that is not a `(define ...)`. */
CccclPlanFn *ccccl_declare_toplevel(CccclPlan *p, CccclForm *form);

/* The one synthesized function holding a "program"-mode file's toplevel
 * executable forms, emitted `static LObj *ccccl_toplevel(void)`. */
CccclPlanFn *ccccl_new_toplevel_fn(CccclPlan *p);

/* Lowers a toplevel define's params+body into the CccclPlanFn
 * ccccl_declare_toplevel already created for it. */
void ccccl_lower_toplevel_body(CccclPlan *p, CccclForm *form, CccclPlanFn *fn);

/* "Program"-mode lowering: every toplevel form that is NOT a `(define ...)`
 * is lowered into `fn` (from ccccl_new_toplevel_fn) as one CK_PROGN body,
 * in file order. */
void ccccl_lower_toplevel_exprs(CccclPlan *p, CccclPlanFn *fn,
                                CccclForm **forms, int n);

#ifdef __cplusplus
}
#endif

#endif /* CCCCL_LOWER_H */
