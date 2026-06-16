// Test C23 wb/uwb integer literal suffixes (ticket #395, wide literals #452)
// These produce _BitInt(N) / unsigned _BitInt(N) values where N is the
// minimum bit width needed to hold the constant.

int main(void) {
    // 0wb -> signed _BitInt(2) (minimum for signed _BitInt)
    if (sizeof(0wb) != 1) return 1;

    // 1wb -> _BitInt(2): 1 value bit + 1 sign = 2 bits, stored in 1 byte
    if (sizeof(1wb) != 1) return 2;

    // 127wb -> _BitInt(8): 7 value bits + 1 sign = 8 bits
    if (sizeof(127wb) != 1) return 3;

    // 128wb -> _BitInt(9): 8 value bits + 1 sign = 9 bits, stored in 2 bytes
    if (sizeof(128wb) != 2) return 4;

    // 0uwb -> unsigned _BitInt(1) (minimum)
    if (sizeof(0uwb) != 1) return 5;

    // 1uwb -> unsigned _BitInt(1): 1 bit holds 0..1
    if (sizeof(1uwb) != 1) return 6;

    // 255uwb -> unsigned _BitInt(8)
    if (sizeof(255uwb) != 1) return 7;

    // 256uwb -> unsigned _BitInt(9), stored in 2 bytes
    if (sizeof(256uwb) != 2) return 8;

    // Arithmetic: wb values behave as _BitInt
    _BitInt(8) a = 127wb;
    a = a + 1wb;
    if (a != -128) return 9;    // signed overflow wraps

    unsigned _BitInt(5) b = 31uwb;
    b = b + 1uwb;
    if (b != 0) return 10;      // unsigned overflow wraps

    // Assignment from wb literal to _BitInt variable
    _BitInt(4) s4 = 7wb;        // 7 fits in _BitInt(4) (max 7)
    if (s4 != 7) return 11;

    // Mixed arithmetic with wb and regular integer
    unsigned _BitInt(8) c = 100uwb;
    c = c + 55;
    if (c != 155) return 12;

    // --- #452: literals needing > 64 bits must infer a wide width and
    // materialize the full-precision value, not just the truncated low 64
    // bits (the bug: tokenize.c parsed the digit text with strtoul into a
    // 64-bit value *before* computing the width).

    // Decimal literal needing 97/98 bits (bit_length(123456789012345678901234567890) == 97).
    if (sizeof(123456789012345678901234567890wb) != 16) return 13;  // width 98 (97 value bits + sign)
    if (sizeof(123456789012345678901234567890uwb) != 16) return 14; // width 97

    _BitInt(98) big_signed = 123456789012345678901234567890wb;
    unsigned _BitInt(97) big_unsigned = 123456789012345678901234567890uwb;
    // Verify the full-precision value round-tripped correctly by checking
    // the low and high 64-bit halves independently.
    if ((unsigned long long)big_signed != 14083847773837265618ULL) return 15;
    if ((unsigned long long)(big_signed >> 64) != 6692605942ULL) return 16;
    if ((unsigned long long)big_unsigned != 14083847773837265618ULL) return 17;
    if ((unsigned long long)(big_unsigned >> 64) != 6692605942ULL) return 18;

    // Hex literal needing 65 bits (0x1FFFFFFFFFFFFFFFF == 2^65 - 1) —
    // exercises the base-16 fast width-inference path.
    if (sizeof(0x1FFFFFFFFFFFFFFFFuwb) != 16) return 19; // width 65
    if (sizeof(0x1FFFFFFFFFFFFFFFFwb) != 16) return 20;  // width 66 (sign bit)
    unsigned _BitInt(65) hex_unsigned = 0x1FFFFFFFFFFFFFFFFuwb;
    _BitInt(66) hex_signed = 0x1FFFFFFFFFFFFFFFFwb;
    if ((unsigned long long)hex_unsigned != 0xFFFFFFFFFFFFFFFFULL) return 21;
    if ((unsigned long long)(hex_unsigned >> 64) != 1ULL) return 22;
    if ((unsigned long long)hex_signed != 0xFFFFFFFFFFFFFFFFULL) return 23;
    if ((unsigned long long)(hex_signed >> 64) != 1ULL) return 24;

    // Binary literal needing exactly 65 bits (2^64) — exercises the
    // base-2 fast width-inference path.
    unsigned _BitInt(65) bin_unsigned =
        0b10000000000000000000000000000000000000000000000000000000000000000uwb;
    if (sizeof(bin_unsigned) != 16) return 25;
    if ((unsigned long long)bin_unsigned != 0ULL) return 26;
    if ((unsigned long long)(bin_unsigned >> 64) != 1ULL) return 27;

    // Sign-bit boundary: 2^63 fits unsigned in 64 bits (narrow, scalar
    // storage) but needs 65 bits signed (wide, address-based storage) —
    // the same magnitude crosses from narrow to wide purely because of u.
    if (sizeof(9223372036854775808uwb) != 8) return 28;
    if (sizeof(9223372036854775808wb) != 16) return 29;

    return 42;
}
