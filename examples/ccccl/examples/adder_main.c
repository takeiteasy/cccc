/* adder_main.c — host TU for examples/adder.lisp.
 *
 * Exercises a real lexical closure: `make-adder`'s LAMBDA captures its
 * parameter `n` from the enclosing function, positionally, at the closure's
 * creation site (see ccccl_closure/ccccl_capture_set in runtime/ccccl_rt.h
 * and the README's "Closures" section) -- something none of the other
 * examples touch (lambda_head's lambda captures nothing).
 */
#include <stdio.h>
#include "ccccl_rt.h"

extern LObj *make_adder(LObj *n);
extern LObj *get_square(void);

int main(void) {
    LObj *add5, *sq, *result;

    ccccl_rt_init();

    add5   = make_adder(ccccl_int(5));
    result = ccccl_apply(add5, ccccl_cons(ccccl_int(37), ccccl_nil));
    ccccl_print(result, stdout);
    fputc('\n', stdout);

    /* get_square() returns `square` as a value -- a closure over
     * square__thunk, not square() itself, since square has no (captures,
     * args) entry point. ccccl_apply calls through it the same as any
     * other closure. */
    sq     = get_square();
    result = ccccl_apply(sq, ccccl_cons(ccccl_int(6), ccccl_nil));
    ccccl_print(result, stdout);
    fputc('\n', stdout);

    return 0;
}
