// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: nested backtick quasi-quotes are not supported

[[cccc::comptime(inline)]]
Node *bad_quote(void) {
    return `return ${ `42` };`;
}

int main(void) { bad_quote(); }
