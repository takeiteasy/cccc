// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked array local variable
// An array typedef from a command-line input is not exempt.

typedef int Arr[4];

[[cccc::checked]]
int f(void) {
    Arr a;
    return a[0];
}

int main(void) {
    return 42;
}
