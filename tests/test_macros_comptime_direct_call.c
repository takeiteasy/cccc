// EXPECT_COMPILE_ERROR
// [[jcc::comptime]] helpers are not callable from runtime program code.

[[jcc::comptime]]
int comptime_helper(void) {
    return 42;
}

int main(void) {
    return comptime_helper();
}
