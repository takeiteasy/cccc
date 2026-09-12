// EXPECT_COMPILE_ERROR
// #1331 -- Checked regions, v1 ban list item 1: an unchecked pointer local
// declared inside a `_Checked { ... }` block is a compile error, same as the
// [[cccc::checked]] attribute form -- always on, deliberately not passed
// --checked-pointers here. See tests/test_checked_scope_decl_error.c for the
// attribute-form equivalent of this test.

int main(void) {
    _Checked {
        int *p = 0;
        (void)p;
    }
    return 42;
}
