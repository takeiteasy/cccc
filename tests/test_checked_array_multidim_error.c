// EXPECT_COMPILE_ERROR
// A multidimensional `_Nt_checked` array is rejected: only the innermost
// dimension could hold a terminator, and that is not modelled.

int main(void) {
    char s _Nt_checked[3][10];
    return s[0][0];
}
