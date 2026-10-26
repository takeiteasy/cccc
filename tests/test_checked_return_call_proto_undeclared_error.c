// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined variable
// A prototype's return bound naming an undeclared identifier is diagnosed at
// the declaration, without --checked-pointers.

int *[[cccc::array, cccc::count(n)]] head(int);

int main(void) {
    return 42;
}
