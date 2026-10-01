// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_RUNTIME_ERROR
// CCCC_EXPECT_STDERR: exceeds the limit of 256
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    p[0] = 1;
}
int main(void) {
    int x[1];
    cccc_launch(k, CCCC_RANGE(512), CCCC_RANGE(512), x);
    return 42;
}
