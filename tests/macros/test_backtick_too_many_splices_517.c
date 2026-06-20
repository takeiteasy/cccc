// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: backtick quasi-quotes support at most 6 interpolations

[[cccc::comptime(inline)]]
Node *bad_quote(Node *x) {
    return `${x}+${x}+${x}+${x}+${x}+${x}+${x}`;
}

int main(void) { return bad_quote(6); }
