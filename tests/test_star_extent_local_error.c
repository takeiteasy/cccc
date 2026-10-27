// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expected an expression
// `[*]` is not valid outside a parameter list.

int main(void) {
    int (*p)[*];
    return 42;
}
