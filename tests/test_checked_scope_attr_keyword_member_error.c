// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: applies only to a function definition
// _Checked specifier on a struct member opens no region, so it is an error
// rather than silently ignored.

struct S {
    _Checked int m;
};

int main(void) {
    return 42;
}
