// EXPECT_COMPILE_ERROR
// An unchecked pointer local in a function written `_Checked int f(void)
// { ... }` is a compile error.

_Checked int f(void) {
    int *p = 0;
    (void)p;
    return 42;
}

int main(void) {
    return f();
}
