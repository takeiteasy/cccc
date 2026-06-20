// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: empty backtick interpolation is not allowed

[[cccc::comptime(inline)]]
Node *bad_quote(void) {
    return `return ${ };`;
}

int main(void) { bad_quote(); }
