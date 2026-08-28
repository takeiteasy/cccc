/* reverse_main.c — host TU for examples/reverse.lisp.
 *
 * Exercises the tail-call path: `reverse-acc`'s recursive call sits in tail
 * position, so the emitter lowers it to loop-and-reassign (a `while (again)`
 * wrapper, see src/ccccl_comptime.c) instead of a recursive C call. See
 * `make show-reverse` and the README's "Tail calls" section.
 */
#include <stdio.h>
#include "ccccl_rt.h"

extern LObj *reverse_acc(LObj *xs, LObj *acc);

int main(void) {
    LObj *xs;

    ccccl_rt_init();
    xs = ccccl_cons(
        ccccl_intern("A"),
        ccccl_cons(ccccl_intern("B"),
                   ccccl_cons(ccccl_intern("C"),
                              ccccl_cons(ccccl_intern("D"), ccccl_nil))));

    ccccl_print(reverse_acc(xs, ccccl_nil), stdout);
    fputc('\n', stdout);
    return 0;
}
