// Test C23 _Decimal32/64/128 types (placeholder: binary float aliases)
// Note: these use binary floating-point, not real IEEE-754-2008 decimal encoding.

int main(void) {
    // sizeof checks per C23 spec
    if (sizeof(_Decimal32) != 4) return 1;
    if (sizeof(_Decimal64) != 8) return 2;
    if (sizeof(_Decimal128) != 16) return 3;

    // Basic variable declarations and arithmetic
    _Decimal32 d32 = 1.5f;
    _Decimal64 d64 = 2.5;
    _Decimal128 d128 = 3.5L;

    if (d32 + d32 != 3.0f) return 4;
    if (d64 + d64 != 5.0) return 5;
    if (d128 + d128 != 7.0L) return 6;

    // Assignment and comparison
    _Decimal64 x = 10.0;
    x = x * 2.0;
    if (x != 20.0) return 7;

    return 42;
}
