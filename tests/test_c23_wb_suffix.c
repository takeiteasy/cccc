// Test C23 wb/uwb integer literal suffixes (ticket #395)
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

    return 42;
}
