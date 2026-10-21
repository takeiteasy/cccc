// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __extension__ \(\{ va_list __cccc_va_fwd
// CCCC_REJECT_STDOUT: reg_ptr
// CCCC_REJECT_STDOUT: reg_count
// CCCC_REJECT_STDOUT: stack_ptr
// -m output forwards a copy of the va_list to the libc call.
#include <stdio.h>
#include <stdarg.h>
static int test_vsnprintf_basic(int count, ...) {
    va_list ap;
    va_start(ap, count);
    int  first = va_arg(ap, int);
    char buf[32];
    vsnprintf(buf, sizeof buf, "%d", ap);
    int second = va_arg(ap, int);
    va_end(ap);
    if (first != 10)
        return 1;
    if (buf[0] != '2' || buf[1] != '0' || buf[2] != 0)
        return 2;
    if (second != 20)
        return 3;
    return 0;
}
int main(void) {
    return test_vsnprintf_basic(2, 10, 20) == 0 ? 42 : 1;
}
