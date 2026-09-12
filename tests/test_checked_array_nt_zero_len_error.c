// EXPECT_COMPILE_ERROR
// #487: `_Nt_checked[N]` reserves its last element for the terminator slot,
// so N must be at least 1 -- `_Nt_checked[0]` has no real elements left once
// the terminator slot is reserved and is rejected rather than silently
// treated as a zero-length checked array.

int main(void) {
    char s _Nt_checked[0];
    return s[0];
}
