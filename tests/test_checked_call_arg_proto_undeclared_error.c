// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable 'n'
// A prototype's bound naming an undeclared identifier is diagnosed at the
// declaration, with no call site and without --checked-pointers.

void sink(int *[[cccc::array, cccc::count(n)]] p, int);

int main(void) {
    return 42;
}
