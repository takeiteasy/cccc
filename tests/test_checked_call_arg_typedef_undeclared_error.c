// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'n'
// A function type spelled by a typedef gets the same declaration-time check
// as a prototype.

typedef void SinkFn(int *[[cccc::array, cccc::count(n)]] p, int);

int main(void) {
    return 42;
}
