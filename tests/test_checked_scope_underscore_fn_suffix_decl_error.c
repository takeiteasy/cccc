// EXPECT_COMPILE_ERROR
// An unchecked pointer local in a function written `void f(void) _Checked
// { ... }` is a compile error, same as the [[cccc::checked]] attribute form.

void f(void) _Checked {
    int *p = 0;
    (void)p;
}

int main(void) {
    f();
    return 42;
}
