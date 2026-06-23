// <stdint.h> must provide the C99 7.18.4 integer-constant macros
// (INTn_C / UINTn_C / INTMAX_C / UINTMAX_C).  They were missing, so e.g.
// UINT64_C(x) parsed as a call to an undeclared function and was rejected as
// "not a compile-time constant" inside a static initializer (the libsqlite
// "static const u64 aBase[] = { UINT64_C(0x80...) }" idiom).

#include <stdint.h>

// Must be usable in a static, constant initializer.
static const uint64_t TAB[] = {
    UINT64_C(0x8000000000000000),
    UINT64_C(0xa000000000000000),
};

int main(void) {
    if (TAB[0] != 0x8000000000000000ULL) return 1;
    if (TAB[1] != 0xa000000000000000ULL) return 2;

    // The 64-bit / max-width forms carry (unsigned) long long width, so the
    // high bit survives without truncation or sign issues.
    if (UINT64_C(0xFFFFFFFFFFFFFFFF) != 18446744073709551615ULL) return 3;
    if (UINTMAX_C(1) << 63 != 0x8000000000000000ULL) return 4;
    if (INT32_C(-5) + UINT32_C(5) != 0) return 5;
    if (INT64_C(1) << 40 != 1099511627776LL) return 6;

    return 42;
}
