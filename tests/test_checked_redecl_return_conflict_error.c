// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: conflicting checked-pointer bounds for the return value of 'mk'

int *[[cccc::array, cccc::count(n)]] mk(int n);
int *[[cccc::array, cccc::count(n + 1)]] mk(int n);

int main(void) {
    return 42;
}
