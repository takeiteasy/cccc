// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3
// CCCC_EXPECT_STDERR: UNINITIALIZED VARIABLE READ
// Positive control for #1008's fix: a genuinely uninitialized local whose
// address is never taken must still be caught. There was previously no
// dedicated uninitialized-detection regression test in the suite at all --
// without this, an over-broad addr_taken/is_captured/is_block_var guard
// could silently disable the whole detector and the suite would stay green
// (the #979 lesson). Must be verified to fail if the CHKI guard added for
// #1008 is made unconditional.
int main(void) {
    int x;
    int y = 10;
    int z = x + y;
    return z;
}
