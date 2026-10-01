// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_RUNTIME_ERROR
// CCCC_EXPECT_STDERR: zero size in dimension 0
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    p[0] = 1;
}
int main(void) {
    int x[1];
    cccc_launch(k, CCCC_RANGE(0), CCCC_RANGE(1), x);
    return 42;
}
