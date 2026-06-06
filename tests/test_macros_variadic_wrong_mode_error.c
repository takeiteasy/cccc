// EXPECT_COMPILE_ERROR
// Ticket #284: string vararg access is only valid for global-generation macros.

[[jcc::comptime(inline)]]
$node_t *bad_mode(...) {
    return $string_literal(_AST_VARARG_STR_AT(0));
}

int main(void) {
    return bad_mode(1) ? 42 : 1;
}
