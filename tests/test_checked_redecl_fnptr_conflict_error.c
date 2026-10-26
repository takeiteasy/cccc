// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: conflicting checked-pointer bounds for parameter 1 of 'apply'
// A function-pointer parameter's own parameter bounds are compared too.

void apply(void (*cb)(int *[[cccc::array, cccc::count(n)]] p, int n));
void apply(void (*cb)(int *[[cccc::array, cccc::count(n + 1)]] p, int n));

int main(void) {
    return 42;
}
