// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'comptime' is a cccc attribute; write \[\[cccc::comptime\]\]

[[clang::comptime]] int answer(void) {
    return 42;
}

int main(void) {
    return 0;
}
