// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not after a declarator
// __attribute__((checked)) after the parameter list of a definition opens no region, so it is an error
// rather than silently ignored.

void f(void) __attribute__((checked)) {
}

int main(void) {
    return 42;
}
