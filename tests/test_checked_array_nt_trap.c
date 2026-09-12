// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #487: `_Nt_checked[N]` widens the checked range by one element for a
// terminator slot, same as a checked pointer's [[cccc::ntarray]] -- CHKNT
// traps a non-null write into that slot. The declared length (11) already
// accounts for the terminator, so index 10 is the widened slot, not an
// out-of-bounds access (CHKR alone would admit it).

int main(void) {
    char s       _Nt_checked[11];
    volatile int i = 10;
    s[i]           = 'x';
    return 42;
}
