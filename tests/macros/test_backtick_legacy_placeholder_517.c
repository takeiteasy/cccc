// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: legacy Quote placeholders are not allowed

[[cccc::comptime]]
Node *bad_quote(Node *x) {
    return `return $1;
    `;
}

int main(void) {
    bad_quote(42);
}
