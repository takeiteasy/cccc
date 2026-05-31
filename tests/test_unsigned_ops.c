// Test USHR3, UDIV3, UMOD3 unsigned arithmetic opcodes.
// Uses unsigned long long with the high bit set so results genuinely differ
// from their signed counterparts — the test fails on the old SHR3/DIV3/MOD3
// opcodes and passes only when the correct unsigned ops are emitted.
#include <stdint.h>

int main() {
    // ---- Direct expressions ----

    // Logical right shift: 0xFFFFFFFFFFFFFFFF >> 4 == 0x0FFFFFFFFFFFFFFF
    // Signed arithmetic shift of -1 >> 4 would stay 0xFFFFFFFFFFFFFFFF.
    unsigned long long shr_result = 0xFFFFFFFFFFFFFFFFULL >> 4;
    if (shr_result != 0x0FFFFFFFFFFFFFFFULL)
        return 1;

    // Unsigned division: 0xFFFFFFFFFFFFFFFF / 2 == 9223372036854775807
    // Signed: (-1) / 2 == 0.
    unsigned long long div_result = 0xFFFFFFFFFFFFFFFFULL / 2ULL;
    if (div_result != 9223372036854775807ULL)
        return 2;

    // Unsigned modulo: 0xFFFFFFFFFFFFFFFF % 7 == 1
    // Signed: (-1) % 7 == -1 (wraps to 0xFFFFFFFFFFFFFFFF in register).
    unsigned long long mod_result = 0xFFFFFFFFFFFFFFFFULL % 7ULL;
    if (mod_result != 1ULL)
        return 3;

    // ---- Compound assignment forms ----

    unsigned long long a = 0xFFFFFFFFFFFFFFFFULL;
    a >>= 4;
    if (a != 0x0FFFFFFFFFFFFFFFULL)
        return 4;

    unsigned long long b = 0xFFFFFFFFFFFFFFFFULL;
    b /= 2ULL;
    if (b != 9223372036854775807ULL)
        return 5;

    unsigned long long c = 0xFFFFFFFFFFFFFFFFULL;
    c %= 7ULL;
    if (c != 1ULL)
        return 6;

    // ---- Additional boundary values ----

    // Shift by 0 (identity)
    unsigned long long d = 0x8000000000000000ULL;
    if ((d >> 0) != 0x8000000000000000ULL)
        return 7;

    // Shift high bit out
    if ((d >> 63) != 1ULL)
        return 8;

    // Unsigned div where result > LLONG_MAX
    // 0xFFFFFFFFFFFFFFFF / 1 == 0xFFFFFFFFFFFFFFFF
    if ((0xFFFFFFFFFFFFFFFFULL / 1ULL) != 0xFFFFFFFFFFFFFFFFULL)
        return 9;

    // Unsigned mod 1 == 0 always
    if ((0xFFFFFFFFFFFFFFFFULL % 1ULL) != 0ULL)
        return 10;

    return 42;
}
