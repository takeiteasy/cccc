// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: too few arguments
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p, int n) {
    p[0] = n;
}
int main(void) {
    int x[1];
    cccc_launch(k, CCCC_RANGE(1), CCCC_RANGE(1), x);
    return 42;
}
