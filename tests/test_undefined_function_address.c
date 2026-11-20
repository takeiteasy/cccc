// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: undefined function: undefined_func
// Taking the address of a function that is declared but never defined is an
// error, like calling it directly.
int undefined_func(long);

int main(void) {
    int (*p)(long) = undefined_func;
    return p(0) == 1;
}
