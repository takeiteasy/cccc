// EXPECT_RUNTIME_ERROR
// CCCC_EXPECT_STDERR: does not divide global size
#include <cccc/kernel.h>
[[cccc::kernel]] static void k([[cccc::global]] int *p) {
    p[cccc_global_id(0)] = 1;
}
int main(void) {
    int x[10];
    cccc_launch(k, CCCC_RANGE(10), CCCC_RANGE(4), x);
    return 42;
}
