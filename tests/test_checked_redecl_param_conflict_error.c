// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: conflicting checked-pointer bounds for parameter 1 of 'sink'
// A definition whose parameter bounds differ from the prototype's is an
// error, with or without --checked-pointers.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    return 42;
}

void sink(int *[[cccc::array, cccc::count(n + 100)]] p, int n) {
    (void)p;
    (void)n;
}
