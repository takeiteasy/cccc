// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int read_first\(int \*
// CCCC_REJECT_STDOUT: trusted
// Checked regions: [[cccc::trusted]] is stripped from -m/-c=native/-c=generated
// output.

[[cccc::trusted]] int read_first(int *p);

int read_first(int *p) {
    return p[0];
}

int main(void) {
    int x = 42;
    return read_first(&x);
}
