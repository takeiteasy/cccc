// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: global range must be a cccc_range
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    p[0] = 1;
}
int main(void) {
    int x[1];
    cccc_launch(k, 4, CCCC_RANGE(1), x);
    return 42;
}
