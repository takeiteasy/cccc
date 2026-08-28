/* fib_main.c — host TU for examples/fib.lisp.
 *
 * Exercises fixnums, `+`/`-`/`<`, and `if` -- none of which examples/append,
 * lambda_head, or mutual touch (they never leave the ATOM/PAIR world).
 */
#include <stdio.h>
#include "ccccl_rt.h"

extern LObj *fib(LObj *n);

int main(void) {
    ccccl_rt_init();
    ccccl_print(fib(ccccl_int(10)), stdout);
    fputc('\n', stdout);
    return 0;
}
