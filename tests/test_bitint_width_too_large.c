// EXPECT_COMPILE_ERROR
// _BitInt width > BITINT_MAXWIDTH (65535) should be rejected
int main(void) {
    _BitInt(70000) x = 0;  // error: exceeds maximum width of 65535
    return 0;
}
