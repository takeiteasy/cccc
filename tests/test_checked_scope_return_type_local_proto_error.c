// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked pointer return type of function 'g'
// Checked regions: a block-scope prototype returning an unchecked pointer.

[[cccc::checked]]
void f(void) {
    int *g(void);
}

int main(void) {
    return 42;
}
