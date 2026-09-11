// EXPECT_COMPILE_ERROR
// Checked regions (#485): an unchecked pointer parameter on a
// [[cccc::checked]] function is a compile error, the same as an unchecked
// local -- create_param_lvars() (src/parse_decl.c) is one of the four
// declaration-ban call sites.

[[cccc::checked]]
void f(int *p) {
    (void)p;
}

int main(void) {
    int x = 0;
    f(&x);
    return 42;
}
