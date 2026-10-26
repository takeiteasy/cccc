// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: applies only to a function definition
// _Checked specifier on a parameter opens no region, so it is an error
// rather than silently ignored.

void f(_Checked int a) {
    (void)a;
}

int main(void) {
    return 42;
}
