// Test C23 _BitInt(N) for N in [1, 64]

int main(void) {
    // sizeof checks (unsigned _BitInt(1) is valid; signed needs >= 2 bits)
    unsigned _BitInt(1) b1 = 0;
    if (sizeof(b1) != 1) return 1;

    _BitInt(8) b8 = 0;
    if (sizeof(b8) != 1) return 2;

    _BitInt(9) b9 = 0;
    if (sizeof(b9) != 2) return 3;

    _BitInt(17) b17 = 0;
    if (sizeof(b17) != 4) return 4;

    _BitInt(64) b64 = 0;
    if (sizeof(b64) != 8) return 5;

    // unsigned _BitInt wraparound: 5-bit unsigned max is 31, +1 wraps to 0
    unsigned _BitInt(5) u5 = 31;
    u5 = u5 + 1;
    if (u5 != 0) return 6;

    // Unsigned arithmetic: 16+17 = 33 but 5-bit max is 31, so 33-32 = 1
    unsigned _BitInt(5) ua = 16, ub = 17;
    unsigned _BitInt(5) uc = ua + ub;
    if (uc != 1) return 7;

    // Signed _BitInt(8): max is 127, +1 wraps to -128
    _BitInt(8) s8 = 127;
    s8 = s8 + 1;
    if (s8 != -128) return 8;

    // Sign extension: signed _BitInt(4) with bit pattern 0b1000 = -8
    _BitInt(4) s4 = -8;
    if (s4 != -8) return 9;

    // _BitInt(64) full range
    _BitInt(64) big = 1000000000LL;
    big = big * 1000000000LL;
    if (big != 1000000000000000000LL) return 10;

    // unsigned _BitInt(5) from constant
    unsigned _BitInt(5) u5b = 7;
    u5b = u5b * 5;  // 35 & 31 = 3
    if (u5b != 3) return 11;

    return 42;
}
