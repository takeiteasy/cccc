// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A default that names an earlier parameter does not skip the callee's bounds
// check on the call.

#include <cccc/derive.h>

int g(int *[[cccc::array, cccc::count(n)]] p, int n, int q @ default(n)) {
    return p[0] + q;
}

int main(void) {
    int          a[3] = {1, 2, 7};
    volatile int m    = 4;
    return g(a, m);
}
