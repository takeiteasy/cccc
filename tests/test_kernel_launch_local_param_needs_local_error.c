// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: needs a CCCC_LOCAL\(bytes\) argument
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p,
                               [[cccc::local]] int  *tmp) {
    tmp[0] = 1;
    p[0]   = tmp[0];
}
int main(void) {
    int x[1], y[1];
    cccc_launch(k, CCCC_RANGE(1), CCCC_RANGE(1), x, y);
    return 42;
}
