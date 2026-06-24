// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unterminated backtick quasi-quote

[[cccc::comptime]]
Node *bad_quote(void) {
    return `return 42;
}

int main(void) { bad_quote(); }
