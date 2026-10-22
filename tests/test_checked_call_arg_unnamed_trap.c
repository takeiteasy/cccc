// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// An unnamed checked pointer parameter's bound is enforced like a named one:
// `small` declares count(2) but the call claims 8.

void sink(int *[[cccc::array, cccc::count(n)]], int n);

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    sink(small, 8);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
