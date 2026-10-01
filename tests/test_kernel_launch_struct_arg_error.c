// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: does not support this kernel argument type
#include <cccc/kernel.h>
typedef struct {
    int a, b;
} Pair;
[[cccc::kernel]] static void k([[cccc::global]] int *p, Pair v) {
    p[0] = v.a;
}
int main(void) {
    int  x[1];
    Pair v = {1, 2};
    cccc_launch(k, CCCC_RANGE(1), CCCC_RANGE(1), x, v);
    return 42;
}
