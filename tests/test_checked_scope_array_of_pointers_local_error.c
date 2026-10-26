// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked pointer local variable 'a'
// Checked regions: an array of unchecked pointers as a local.

[[cccc::checked]]
void f(void) {
    int *a _Checked[2];
    (void)a;
}

int main(void) {
    return 42;
}
