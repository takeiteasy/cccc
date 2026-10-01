// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: CCCC_LOCAL is only valid for a \[\[cccc::local\]\] pointer parameter
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    p[0] = 1;
}
int main(void) {
    cccc_launch(k, CCCC_RANGE(1), CCCC_RANGE(1), CCCC_LOCAL(16));
    return 42;
}
