/* lambda_head_main.c — host TU for examples/lambda_head.lisp.
 *
 * Exercises LAMBDA and closure application (ccccl_closure/ccccl_apply),
 * which examples/append.lisp does not touch at all.
 */
#include <stdio.h>
#include "ccccl_rt.h"

extern LObj *head(LObj *args, LObj *env);

int main(void) {
    LObj *xs;

    ccccl_rt_init();
    xs = ccccl_cons(ccccl_intern("A"), ccccl_cons(ccccl_intern("B"), ccccl_nil));

    ccccl_print(head(ccccl_cons(xs, ccccl_nil), ccccl_nil), stdout);
    fputc('\n', stdout);
    return 0;
}
