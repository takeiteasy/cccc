// CCCC_FLAGS: -Wchecked-calls
// CCCC_REJECT_STDERR: checked-calls
// Checked regions: [[cccc::trusted]] on a prototype exempts calls to it, even
// when only an earlier declaration carries the attribute.

[[cccc::trusted]] int read_first(int *p);

int read_first(int *p) {
    return p[0];
}

[[cccc::checked]]
int f(void) {
    int *[[cccc::array, cccc::count(1)]] a = (int[1]){42};
    return read_first(a);
}

int main(void) {
    return f();
}
