// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// __builtin_bzero past the end of the array is caught by the bounds table.
int main(void) {
    char a[8];
    volatile unsigned long n = 16;
    __builtin_bzero(a, n);
    return 42;
}
