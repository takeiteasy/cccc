// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: return type of function 'f' has no bounds
// Checked regions: a return type of array kind with no bounds form.

[[cccc::checked]] int *[[cccc::array]] f(void) {
    return 0;
}

int main(void) {
    return 42;
}
