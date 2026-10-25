// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked pointer return type of function 'f'
// Checked regions: a function definition returning an unchecked pointer.

[[cccc::checked]]
int *f(void) {
    return 0;
}

int main(void) {
    return 42;
}
