// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A constant store into a _Nt_checked array's terminator slot keeps its
// null-terminator guard.

int main(void) {
    char s _Nt_checked[4] = "abc";
    s[3]                  = 'x';
    return 42;
}
