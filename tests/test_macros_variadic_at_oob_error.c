// EXPECT_COMPILE_ERROR
// Ticket #284: AST vararg access is bounds checked.

[[cccc::comptime(inline)]]
$node_t *bad_at(...) {
    return $vararg_at($vararg_count());
}

int main(void) {
    return bad_at(1);
}
