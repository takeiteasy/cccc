// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// The extent is fixed when the declaration runs: growing `n` afterwards does
// not grow the array, so the bound must not grow either.

int main(void) {
    int n = 5;
    int a _Checked[n];
    int old = n;
    n += 10;
    a[old] = 1;
    return 42;
}
