/* ccccl_rt.h — the runtime universe, public surface.
 *
 * Ordinary C. No cccc dependency, no comptime attributes, nothing here ever
 * runs inside the cccc VM. This is the header both the hand-written host TU
 * (examples/NAME_main.c) and cccc's comptime pass (via GetType("LObj")) see.
 *
 * An LObj is one of four shapes (SectorLISP+, see README.md): an interned
 * ATOM (symbol; NIL and T are ordinary atoms, not special pointer values),
 * a PAIR (cons cell), a fixnum INT, or a CLOSURE (a generated C function
 * paired with its capture list).
 *
 * LObj is deliberately opaque here (forward-declared, no member visible).
 * Nothing outside ccccl_rt.c ever touches an LObj's fields directly — every
 * operation goes through an accessor function (ccccl_car, ccccl_cons, ...).
 * The real definition lives in ccccl_rt_internal.h, included only by
 * ccccl_rt.c.
 */
#ifndef CCCCL_RT_H
#define CCCCL_RT_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LObj LObj;

/* A lambda/toplevel-value closure compiled to C has this shape: it
 * receives its capture list (an ordinary cons list of the free variables
 * captured at the closure's creation site, walked positionally by
 * ccccl_captured — see the README's "Closures" section) and the argument
 * list (already evaluated, as a cons list), and returns a value. */
typedef LObj *(*CccclFn)(LObj *captures, LObj *args);

extern LObj *ccccl_nil; /* the NIL atom */
extern LObj *ccccl_t;   /* the T atom */

void ccccl_rt_init(void);

LObj *ccccl_intern(const char *name);
LObj *ccccl_int(long v);

LObj *ccccl_cons(LObj *a, LObj *d);
LObj *ccccl_car(LObj *x);
LObj *ccccl_cdr(LObj *x);
LObj *ccccl_atom(LObj *x); /* -> ccccl_t if x is an atom, else ccccl_nil */
LObj *ccccl_eq(LObj *a, LObj *b);

/* Fixnum arithmetic/comparison. A non-INT operand is a runtime error
 * (ccccl_rt_fatal), not UB — this is a teaching demo, not a hardened VM. */
LObj *ccccl_add(LObj *a, LObj *b);
LObj *ccccl_sub(LObj *a, LObj *b);
LObj *ccccl_mul(LObj *a, LObj *b);
LObj *ccccl_div(LObj *a, LObj *b);
LObj *ccccl_mod(LObj *a, LObj *b);
LObj *ccccl_num_lt(LObj *a, LObj *b); /* -> t/nil */
LObj *ccccl_num_eq(LObj *a, LObj *b); /* -> t/nil */

/* nth(list, k): the runtime half of a closure's capture-by-index read.
 * Also used directly by generated code for positional argument unpacking
 * (`ccccl_nth(args, 0)`, ...). */
LObj *ccccl_nth(LObj *list, int k);

/* A closure over `fn`, capturing `captures` (an ordinary cons list built
 * at the closure's creation site — see ccccl_capture_set below for the
 * self-referential LABEL case). ccccl_apply builds a positional argument
 * cons list and calls fn(captures, args). */
LObj *ccccl_closure(CccclFn fn, LObj *captures);
LObj *ccccl_apply(LObj *f, LObj *args);

/* LABEL's own name is bound to the closure *inside its own body*, but the
 * capture list is built before the closure exists (see the README's
 * "LABEL" section). ccccl_capture_set patches slot `k` of an
 * already-built capture list in place after the closure is created.
 * ccccl_closure_self is the composed, self-contained form: `captures`
 * carries a placeholder (typically ccccl_nil) at index `self_slot`, and
 * the returned closure has already been patched into it — a single pure
 * expression at the call site, no separate statement to sequence after
 * ccccl_closure. */
void ccccl_capture_set(LObj *captures, int k, LObj *v);
LObj *ccccl_closure_self(CccclFn fn, LObj *captures, int self_slot);

void ccccl_print(LObj *x, FILE *out);

/* `ccccl_print(x, stdout)`, for generated code: `stdout` is a libc macro,
 * not a plain extern, and is not reliably nameable from a `Quote()`
 * template even via `#include @shared` -- PRINT's codegen calls this
 * instead of spelling `stdout` itself. */
LObj *ccccl_print_stdout(LObj *x);

#ifdef __cplusplus
}
#endif

#endif /* CCCCL_RT_H */
