// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: too many arguments
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    p[0] = 1;
}
int main(void) {
    int x[1];
    cccc_launch(k, CCCC_RANGE(1), CCCC_RANGE(1), x, 3);
    return 42;
}
