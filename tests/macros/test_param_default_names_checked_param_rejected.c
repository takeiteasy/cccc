// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a @default cannot name the checked pointer parameter 'p'

#include <cccc/derive.h>

int g(int *[[cccc::array, cccc::count(n)]] p, int n, int q @ default(p[0])) {
    return q;
}

int main(void) {
    int a[3] = {1, 2, 7};
    return g(a, 3);
}
