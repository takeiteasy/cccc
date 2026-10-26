// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: conflicting checked-pointer bounds for parameter 1 of 'sink'
// Two prototypes naming different bounds forms conflict.

void sink(int *[[cccc::array, cccc::count(n)]] p, int n);
void sink(int *[[cccc::array, cccc::byte_count(n)]] p, int n);

int main(void) {
    return 42;
}
