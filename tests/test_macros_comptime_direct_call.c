// EXPECT_COMPILE_ERROR
// [[cccc::comptime]] helpers are not callable from runtime program code.

[[cccc::comptime]]
int comptime_helper(void) {
    return 42;
}

int main(void) {
    return comptime_helper();
}
