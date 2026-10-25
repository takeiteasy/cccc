// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked pointer struct member 'a'
// Checked regions: an array of unchecked pointers as a member.

[[cccc::checked]]
void f(void) {
    struct S {
        int *a[2];
    } s;
    (void)s;
}

int main(void) {
    return 42;
}
