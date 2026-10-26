// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: compound literal of an unchecked array type
// A plain array compound literal is not allowed in a checked region.

[[cccc::checked]]
int f(void) {
    int *[[cccc::array, cccc::count(2)]] p = (int[2]){1, 2};
    return p[0];
}

int main(void) {
    return 42;
}
