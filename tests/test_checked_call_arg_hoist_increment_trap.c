// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int *[[cccc::array, cccc::count(2)]] small = (int[2]){1, 2};
    int i                                      = 8;
    sink(small, i++);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
