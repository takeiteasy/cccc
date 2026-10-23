// EXPECT_COMPILE_ERROR
// `_Atomic (` after '*' is the type-specifier form, not a qualifier.
// CCCC_EXPECT_STDERR: variable name omitted
int main(void) {
    int *_Atomic(int) p;
    return 0;
}
