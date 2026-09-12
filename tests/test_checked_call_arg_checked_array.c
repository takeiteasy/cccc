// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #488: a #487 `_Checked[N]` array argument decays into an ordinary
// declared-checked pointer (its checked_array_extent copied onto the
// adjusted pointer at the array-to-pointer parameter adjustment,
// src/parse_types.c) before this rewrite ever sees it -- find_checked_
// base()/checked_base_is_declared() treat it exactly like a checked
// pointer local, no special-casing needed. `small` decays with count(2);
// the call claims count(8).

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int small _Checked[2] = {1, 2};
    sink(small, 8);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
