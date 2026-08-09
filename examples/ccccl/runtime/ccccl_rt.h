/* ccccl_rt.h — the runtime universe, public surface.
 *
 * Ordinary C. No cccc dependency, no comptime attributes, nothing here ever
 * runs inside the cccc VM. This is the header both the hand-written host TU
 * (examples/append_main.c and friends) and cccc's comptime pass (via
 * GetType("LObj")) see.
 *
 * The representation follows SectorLISP: an LObj is either an interned ATOM
 * (which doubles as a symbol and, for closures, carries a function pointer)
 * or a PAIR (cons cell). NIL and T are themselves atoms, not special
 * pointer values — `ccccl_nil` is a real LObj*, never NULL.
 *
 * LObj is deliberately opaque here (forward-declared, no member visible).
 * Nothing outside ccccl_rt.c ever touches an LObj's fields directly — every
 * operation goes through an accessor function (ccccl_car, ccccl_cons, ...).
 * This sidesteps a cccc `-c=generated` limitation: its type serializer
 * mis-prints struct types it re-derives from GetType() when they contain a
 * union of named struct members (confirmed minimal repro, reported
 * upstream) -- and even once printed correctly, a second, textually
 * separate copy of the same struct reaching the same translation unit
 * (once auto-derived, once via a captured #include) is a hard
 * "redefinition" error in portable C regardless of whether cccc's printer
 * gets it right. An opaque forward declaration has no body to duplicate or
 * mis-print. The real definition lives in ccccl_rt_internal.h, included
 * only by ccccl_rt.c.
 */
#ifndef CCCCL_RT_H
#define CCCCL_RT_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LObj LObj;

/* A lambda compiled to C has this shape: it receives the argument list
 * (already evaluated, as a cons list) and the environment active at the
 * closure's creation site, and returns a value. This is the environment-
 * passing lowering: SectorLISP's dynamic scoping, made explicit. */
typedef LObj *(*CccclFn)(LObj *args, LObj *env);

extern LObj *ccccl_nil; /* the NIL atom */
extern LObj *ccccl_t;   /* the T atom */

/* Function-call accessors for the two globals above, used by generated code
 * (src/ccccl_comptime.c): cccc's comptime `Quote()` identifier resolver
 * accepts a bare function-call identifier without requiring it to be
 * separately comptime-visible, but rejects a bare *variable* reference to
 * an extern declared only via a plain (non-comptime-routed) `#include`
 * with "undefined variable" (confirmed minimal repro). Hand-written code
 * may keep using the globals directly; only generated code needs these. */
LObj *ccccl_get_nil(void);
LObj *ccccl_get_t(void);

void ccccl_rt_init(void);

LObj *ccccl_intern(const char *name);

LObj *ccccl_cons(LObj *a, LObj *d);
LObj *ccccl_car(LObj *x);
LObj *ccccl_cdr(LObj *x);
LObj *ccccl_atom(LObj *x); /* -> ccccl_t if x is an atom, else ccccl_nil */
LObj *ccccl_eq(LObj *a, LObj *b);

/* env is an assoc list: ((sym . val) (sym . val) ...) built with ccccl_cons
 * and ccccl_bind. Lookup is linear, matching SectorLISP's own ASSOC. */
LObj *ccccl_bind(LObj *sym, LObj *val, LObj *env);
LObj *ccccl_bind_list(LObj *syms, LObj *vals, LObj *env);
LObj *ccccl_assoc(LObj *sym, LObj *env);

LObj *ccccl_closure(CccclFn fn, LObj *env);
LObj *ccccl_apply(LObj *f, LObj *args);

void ccccl_print(LObj *x, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* CCCCL_RT_H */
