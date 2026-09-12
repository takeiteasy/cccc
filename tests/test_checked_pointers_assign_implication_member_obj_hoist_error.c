// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #947 extends #945's per-access object-expression hoist to #944's CHKAB
// assignment-time bounds implication: `arr[k].p = s.p;` hoists `arr[k]` into
// a single temp for the post-store dst_lo/dst_hi read, rather than a trivial
// (bare-variable) target. This proves the target's own-bounds-not-implied
// trap through a runtime-indexed array-of-structs target fires.

struct S {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S     arr[2] = {{4, 0}, {10, 0}};
    struct S     s      = {4, (int[4]){1, 2, 3, 4}};
    volatile int k      = 1;
    arr[k].p = s.p; // arr[1].n == 10, wider than s's own count(4) -- traps
    return arr[k].p[0];
}
