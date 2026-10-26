// CCCC_FLAGS: --checked-pointers
// A bounds-cast argument whose claim covers the parameter passes.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);

int main(void) {
    int buf[5] = {0};
    int *raw   = buf;
    sink((int *[[cccc::array, cccc::count(5), cccc::assume]])raw, 5);
    return 42;
}

void sink(int *p, int n) {
    (void)p;
    (void)n;
}
