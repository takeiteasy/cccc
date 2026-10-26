// EXPECT_COMPILE_ERROR
// The suffix form covers the parameters as well as the body, like
// [[cccc::checked]]: an unchecked pointer parameter is rejected.

void f(int *p) _Checked {
    (void)p;
}

int main(void) {
    return 42;
}
