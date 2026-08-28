/* letsum_main.c — host TU for examples/letsum.lisp.
 *
 * Exercises LET (a real C local, `s`), PROGN (a statement sequence -- the
 * `(print s)` side effect, then `s` as the tail value), and PRINT itself,
 * which evaluates to its own argument (matching Common Lisp's PRINT) so it
 * composes in expression position: `(progn (print s) s)` both prints `s`
 * *and* returns it. The Lisp-side `(print s)` writes the sum once with no
 * trailing newline; this file prints the C-side return value a second time
 * to prove it really did come back out of `letsum` -- not just that PRINT
 * ran -- hence the doubled digits in examples/letsum.expected.
 */
#include <stdio.h>
#include "ccccl_rt.h"

extern LObj *letsum(LObj *a, LObj *b);

int main(void) {
    LObj *result;

    ccccl_rt_init();
    result = letsum(ccccl_int(3), ccccl_int(4));
    ccccl_print(result, stdout);
    fputc('\n', stdout);
    return 0;
}
