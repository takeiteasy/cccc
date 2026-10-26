// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// `_Nt_checked[n]` reserves the last slot for the terminator, so a non-zero
// store there traps.

int main(void) {
    volatile int n = 5;
    char s _Nt_checked[n];
    s[n - 1] = 0;
    s[n - 1] = 'x';
    return 42;
}
