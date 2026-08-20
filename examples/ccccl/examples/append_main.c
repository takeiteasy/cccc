/* append_main.c — the host translation unit.
 *
 * Ordinary C, never passed to cccc (see the file comment in
 * src/ccccl_comptime.c for why). Declares the lowered function it wants and
 * links against whatever ccccl_comptime.c generated from examples/append.lisp.
 */
#include <stdio.h>
#include "ccccl_rt.h"

extern LObj *append(LObj *args, LObj *env);

static LObj *list2(LObj *a, LObj *b) {
    return ccccl_cons(a, ccccl_cons(b, ccccl_nil));
}

int main(void) {
    LObj *xs, *ys, *result;

    ccccl_rt_init();

    xs     = list2(ccccl_intern("A"), ccccl_intern("B"));
    ys     = list2(ccccl_intern("C"), ccccl_intern("D"));

    result = append(ccccl_cons(xs, ccccl_cons(ys, ccccl_nil)), ccccl_nil);

    ccccl_print(result, stdout);
    fputc('\n', stdout);
    return 0;
}
