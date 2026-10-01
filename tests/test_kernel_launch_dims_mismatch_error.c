// CCCC_NATIVE_SKIP: kernel launch runs in the VM only
// EXPECT_RUNTIME_ERROR
// CCCC_EXPECT_STDERR: global range has 2 dimension\(s\), local range has 1
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    p[0] = 1;
}
int main(void) {
    int x[4];
    cccc_launch(k, CCCC_RANGE(2, 2), CCCC_RANGE(2), x);
    return 42;
}
