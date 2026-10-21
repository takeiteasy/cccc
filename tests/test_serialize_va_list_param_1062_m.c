// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: va_list __cccc_va_param_ap
// CCCC_EXPECT_STDOUT: va_copy\(ap, __cccc_va_param_ap\)
// CCCC_REJECT_STDOUT: reg_ptr
// CCCC_REJECT_STDOUT: reg_count
// CCCC_REJECT_STDOUT: stack_ptr
// -m output renames the va_list parameter and copies it in a prologue.
#include <stdarg.h>
static int consume_one(int n, va_list ap) {
    (void)n;
    return va_arg(ap, int);
}
static int forward(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int first    = va_arg(ap, int);
    int consumed = consume_one(1, ap);
    int next     = va_arg(ap, int);
    va_end(ap);
    if (first != 10)
        return 1;
    if (consumed != 20)
        return 2;
    if (next != 20)
        return 3;
    return 0;
}
int main(void) {
    return forward(2, 10, 20) == 0 ? 42 : 1;
}
