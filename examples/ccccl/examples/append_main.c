/* append_main.c — the host translation unit.
 *
 * Ordinary C, never passed to cccc (see the file comment in
 * src/ccccl_comptime.c for why). Declares the lowered function it wants and
 * links against whatever ccccl_comptime.c generated from examples/append.lisp.
 */
#include <stdio.h>
#include "ccccl_rt.h"

extern LObj *append(LObj *x, LObj *y);
extern LObj *letters(void);

static LObj *list2(LObj *a, LObj *b) {
    return ccccl_cons(a, ccccl_cons(b, ccccl_nil));
}

int main(void) {
    LObj *xs, *ys, *result;

    ccccl_rt_init();

    xs = list2(ccccl_intern("A"), ccccl_intern("B"));
    ys = list2(ccccl_intern("C"), ccccl_intern("D"));

    /* letters() returns (E F G), a quoted list literal built once at
     * comptime -- appending it on proves both append() and QUOTE in the
     * same run. */
    result = append(append(xs, ys), letters());

    ccccl_print(result, stdout);
    fputc('\n', stdout);
    return 0;
}
