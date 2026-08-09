/* mutual_main.c — host TU for examples/mutual.lisp.
 *
 * Exercises true mutual recursion between two independently-defined
 * toplevel `define`s (evenp/oddp), which neither examples/append.lisp
 * (self-recursion) nor examples/lambda_head.lisp (a LAMBDA nested inside
 * its enclosing function) touch at all.
 */
#include <stdio.h>
#include "ccccl_rt.h"

extern LObj *evenp(LObj *args, LObj *env);

int main(void) {
    LObj *xs;

    ccccl_rt_init();
    xs = ccccl_cons(ccccl_intern("A"),
            ccccl_cons(ccccl_intern("B"),
                ccccl_cons(ccccl_intern("C"),
                    ccccl_cons(ccccl_intern("D"), ccccl_nil))));

    ccccl_print(evenp(ccccl_cons(xs, ccccl_nil), ccccl_nil), stdout);
    fputc('\n', stdout);
    return 0;
}
