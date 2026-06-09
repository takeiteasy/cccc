// EXPECT_COMPILE_ERROR
// Ticket #284: string vararg access is bounds checked.

[[cccc::comptime]]
void bad_str(...) {
    _AST_VARARG_STR_AT(_AST_VARARG_COUNT());
}

bad_str(foo);

int main(void) {
    return 42;
}
