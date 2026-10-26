// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// An annotated prototype followed by a plain definition keeps its bounds.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

void sink(int *p, int n) {
    (void)p;
    (void)n;
}

int main(void) {
    int *[[cccc::array, cccc::count(2)]] s = (int[2]){1, 2};
    sink(s, 8);
    return 42;
}
