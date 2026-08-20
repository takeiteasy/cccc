// EXPECT_COMPILE_ERROR
// Signed _BitInt(1) should be rejected (needs >= 2 bits for sign + value)
int main(void) {
    _BitInt(1) x = 0; // error: signed _BitInt needs at least 2 bits
    return 0;
}
