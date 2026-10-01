// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -c=native
// CCCC_EXPECT_STDERR: not supported with -c=native
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    p[0] = 1;
}
int main(void) {
    int x[1];
    cccc_launch(k, CCCC_RANGE(1), CCCC_RANGE(1), x);
    return 42;
}
