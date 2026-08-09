// EXPECT_COMPILE_ERROR
// _BitInt(0) should be rejected
int main(void) {
    unsigned _BitInt(0) x = 0;  // error: width must be at least 1
    return 0;
}
