// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #486: __builtin_cccc_dynamic_check(cond) traps (CHKDC) when cond is
// false, under --checked-pointers.

int main(void) {
    int i = 10, n = 5;
    __builtin_cccc_dynamic_check(i < n);
    return 0;
}
