// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: requires a \[\[cccc::kernel\]\] function
#include <cccc/kernel.h>
static void plain(int *p) {
    p[0] = 1;
}
int main(void) {
    int x[1];
    cccc_launch(plain, CCCC_RANGE(1), CCCC_RANGE(1), x);
    return 42;
}
