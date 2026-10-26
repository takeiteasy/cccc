// CCCC_FLAGS: --checked-pointers
// EXPECT_RUNTIME_ERROR
// A bounds-cast argument is verified against the parameter's bounds.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int buf[5] = {0};
    int *raw   = buf;
    sink((int *[[cccc::array, cccc::count(5), cccc::assume]])raw, 100);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
