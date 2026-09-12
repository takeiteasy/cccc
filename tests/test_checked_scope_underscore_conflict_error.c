// EXPECT_COMPILE_ERROR
// #1331 -- an attribute region and a keyword region disagreeing on the same
// compound statement ([[cccc::unchecked]] on a `_Checked { ... }` block) is a
// compile error, reusing apply_checked_scope_attr()'s own conflict wording
// (src/parse_types.c) rather than inventing a second phrasing. See
// tests/test_checked_scope_both_attrs_error.c for the pure-attribute-form
// equivalent ([[cccc::checked, cccc::unchecked]]).

int main(void) {
    [[cccc::unchecked]] _Checked {
        return 42;
    }
}
