// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'zz'
// A function pointer nested in a prototype's parameter list is checked too.

void register_cb(void (*cb)(int *[[cccc::array, cccc::count(zz)]] p, int));

int main(void) {
    return 42;
}
