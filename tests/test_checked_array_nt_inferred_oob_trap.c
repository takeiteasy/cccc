// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// `_Nt_checked[]` takes its extent from the string initializer, so a
// non-null write into the terminator slot traps.

int main(void) {
    char s       _Nt_checked[] = "hi";
    volatile int i             = 2;
    s[i]                       = 'x';
    return 42;
}
