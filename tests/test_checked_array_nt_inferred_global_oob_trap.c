// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A global `_Nt_checked[]` takes its extent from the string initializer.

char s _Nt_checked[] = "hi";

int main(void) {
    volatile int i = 2;
    s[i]           = 'x';
    return 42;
}
