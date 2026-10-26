// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked array local variable
// A string-initialised plain array is not exempt.

[[cccc::checked]]
int f(void) {
    char s[] = "hi";
    return s[0];
}

int main(void) {
    return 42;
}
