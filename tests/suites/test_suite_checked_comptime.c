// CCCC_FLAGS: --testing --checked-pointers
// A comptime function compiles under --checked-pointers, including the
// built-in libc bounds (which name their size parameter) and a user prototype
// that does the same.

#include <string.h>

int cc_sum(int *[[cccc::array, cccc::count(n)]] a, int n);

int cc_sum(int *[[cccc::array, cccc::count(n)]] a, int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += a[i];
    return s;
}

int cc_total;

[[cccc::comptime]]
Node *cc_probe(void) {
    return Quote("cc_total = 42;");
}

[[cccc::test]]
void test_comptime_expands_under_checked_pointers(void) {
    cc_probe();
    AssertEq(cc_total, 42);
}

[[cccc::test]]
void test_libc_bounds_still_apply_next_to_comptime(void) {
    char dst[4];
    memset(dst, 7, sizeof dst);
    AssertEq(dst[3], 7);
    int v[3] = {20, 20, 2};
    AssertEq(cc_sum(v, 3), 42);
}
