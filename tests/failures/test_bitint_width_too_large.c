// _BitInt width > 64 should be rejected
int main(void) {
    _BitInt(65) x = 0;  // error: exceeds maximum width of 64
    return 0;
}
