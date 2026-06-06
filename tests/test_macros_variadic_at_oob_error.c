// EXPECT_COMPILE_ERROR
// Ticket #284: AST vararg access is bounds checked.

[[jcc::comptime(inline)]]
$node_t *bad_at(...) {
    return _AST_VARARG_AT(_AST_VARARG_COUNT());
}

int main(void) {
    return bad_at(1);
}
