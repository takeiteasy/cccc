// EXPECT_COMPILE_ERROR
// #1331 -- Checked regions, v1 ban list item 2: a cast to an unchecked
// pointer type inside a `_Checked { ... }` block is a compile error, same as
// the [[cccc::checked]] attribute form. See
// tests/test_checked_scope_cast_error.c for the attribute-form equivalent.

extern int puts(const char *);

void f(void) {
    _Checked {
        int *[[cccc::array, cccc::count(1)]] a = 0;
        puts((char *)a); // explicit cast to an unchecked pointer type
    }
}

int main(void) {
    f();
    return 42;
}
