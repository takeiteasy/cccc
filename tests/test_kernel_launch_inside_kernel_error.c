// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cccc_launch is not allowed in kernel code
#include <cccc/kernel.h>
[[cccc::kernel]] static void inner([[cccc::global]] int *p) {
    p[0] = 1;
}
[[cccc::kernel]] static void outer([[cccc::global]] int *p) {
    cccc_launch(inner, CCCC_RANGE(1), CCCC_RANGE(1), p);
}
int main(void) {
    return 42;
}
