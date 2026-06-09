// CCCC_FLAGS: -Wsign-compare
// CCCC_EXPECT_STDERR: \[-Wsign-compare\]

int main(void) {
    int x = -1;
    unsigned int y = 1u;
    // Signed vs unsigned comparison: warns because -1 < 1u is false in unsigned arithmetic.
    int result = (x < y) ? 1 : 0;
    return result == 0 ? 42 : 0;
}
