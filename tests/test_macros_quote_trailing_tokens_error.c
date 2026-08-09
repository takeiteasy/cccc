// EXPECT_COMPILE_ERROR
// Ticket #955: leftover tokens after Quote()'s single expression parse must
// be a compile error, not silently discarded. "1 + 2 extra_junk" has no
// trailing ';' (so quote_is_stmt() takes the expression branch, parsing only
// "1 + 2") and no top-level ';' to trigger multi-statement wrapping either --
// "extra_junk" must be reported, not dropped.

[[cccc::comptime]]
Node *bad_expr(void) {
    return Quote("1 + 2 extra_junk");
}

int main(void) {
    int x = bad_expr();
    return 42;
}
