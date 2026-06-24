// CCCC_FLAGS: --testing
// Consolidated suite: miscellaneous: volatile, builtins
// Source tests: test_builtins, test_volatile

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

// [from test_builtins]
// Test GNU-style builtins implemented in the parser (ticket #220, #212, #213, #513)

// [from test_volatile]
// Test volatile keyword parsing and semantics.
// Verifies: correct values observed through volatile accesses, no caching,
// correct behaviour through pointer aliases and signal-like patterns.

volatile int global_volatile = 42;

static volatile sig_atomic_t flag = 0;

#pragma cccc suite begin "misc"

// test_builtins
[[cccc::test(return = 42)]]
int test_builtins(void) {
    // Math constants
    double h = HUGE_VAL;
    float hf = HUGE_VALF;
    double inf = INFINITY;
    float inff = __builtin_inff();
    double nan = NAN;
    float nanf = __builtin_nanf("");

    // Verify HUGE_VAL is positive infinity
    if (!(h > 0)) return 1;
    if (!(inf > 0)) return 2;
    if (!(hf > 0)) return 3;
    if (!(inff > 0)) return 4;

    // Verify NaN
    if (nan == nan) return 5;     // NaN != NaN
    if (nanf == nanf) return 6;

    // Math predicates
    if (!__builtin_isnan(nan)) return 10;
    if (!__builtin_isnan(nanf)) return 11;
    if (__builtin_isnan(1.0)) return 12;

    if (!__builtin_isinf(h)) return 13;
    if (!__builtin_isinf(inf)) return 14;
    if (__builtin_isinf(1.0)) return 15;

    if (!__builtin_isfinite(1.0)) return 16;
    if (__builtin_isfinite(h)) return 17;
    if (__builtin_isfinite(nan)) return 18;

    if (!__builtin_signbit(-1.0)) return 19;
    if (__builtin_signbit(1.0)) return 20;

    // __builtin_expect (ignored hint)
    if (__builtin_expect(42, 0) != 42) return 21;
    if (__builtin_expect(0, 1) != 0) return 22;

    // __builtin_constant_p
    if (!__builtin_constant_p(1 + 2)) return 23;
    int x = 5;
    if (__builtin_constant_p(x)) return 24;

    // __builtin_alloca
    void *p = __builtin_alloca(64);
    if (!p) return 25;

    // ==== Bit manipulation (#212) ====

    // clz / clzll
    if (__builtin_clz(1u) != 31) return 30;
    if (__builtin_clz(0x80000000u) != 0) return 31;
    if (__builtin_clzll(1ull) != 63) return 32;
    if (__builtin_clzll(0x8000000000000000ull) != 0) return 33;

    // ctz / ctzll
    if (__builtin_ctz(8u) != 3) return 34;
    if (__builtin_ctz(1u) != 0) return 35;
    if (__builtin_ctzll(8ull) != 3) return 36;
    if (__builtin_ctzll(1ull) != 0) return 37;

    // popcount / popcountll
    if (__builtin_popcount(0u) != 0) return 38;
    if (__builtin_popcount(0xFFu) != 8) return 39;
    if (__builtin_popcount(0xFFFFFFFFu) != 32) return 40;
    if (__builtin_popcountll(0xFFFFFFFFFFFFFFFFull) != 64) return 41;

    // parity / parityll  (0xB = 0b1011, 0x6 = 0b0110)
    if (__builtin_parity(0xBu) != 1) return 42;
    if (__builtin_parity(0x6u) != 0) return 43;
    if (__builtin_parityll(0xBull) != 1) return 44;

    // ffs / ffsll: 1-based index of lowest set bit, 0 for 0
    if (__builtin_ffs(0) != 0) return 45;
    if (__builtin_ffs(8) != 4) return 46;
    if (__builtin_ffs(1) != 1) return 47;
    if (__builtin_ffsll(0ll) != 0) return 48;
    if (__builtin_ffsll(8ll) != 4) return 49;

    // bswap16 / bswap32 / bswap64
    if (__builtin_bswap16(0x0102u) != 0x0201u) return 50;
    if (__builtin_bswap32(0x01020304u) != 0x04030201u) return 51;
    if (__builtin_bswap64(0x0102030405060708ull) != 0x0807060504030201ull) return 52;

    // ==== Overflow arithmetic (#213) ====

    // add_overflow
    int r_add;
    if (__builtin_add_overflow(1, 2, &r_add)) return 60;
    if (r_add != 3) return 61;
    if (!__builtin_add_overflow(INT_MAX, 1, &r_add)) return 62;

    // sub_overflow
    int r_sub;
    if (__builtin_sub_overflow(5, 3, &r_sub)) return 63;
    if (r_sub != 2) return 64;
    if (!__builtin_sub_overflow(INT_MIN, 1, &r_sub)) return 65;

    // mul_overflow (int variant — matches type.c usage)
    int r_mul;
    if (__builtin_mul_overflow(3, 4, &r_mul)) return 66;
    if (r_mul != 12) return 67;
    if (!__builtin_mul_overflow(INT_MAX, 2, &r_mul)) return 68;

    // mul_overflow (long long variant — matches optimize.c usage)
    long long r_mull;
    if (__builtin_mul_overflow(3ll, 4ll, &r_mull)) return 69;
    if (r_mull != 12ll) return 70;
    if (!__builtin_mul_overflow((long long)INT64_MAX, 2ll, &r_mull)) return 71;

    // add_overflow no-overflow with long long result
    long long r_addl;
    if (__builtin_add_overflow(1ll, 2ll, &r_addl)) return 72;
    if (r_addl != 3ll) return 73;

    // ==== Ticket #513: long-double constant builtins ====

    // __builtin_huge_vall() -> long double positive infinity
    long double hvl = __builtin_huge_vall();
    if (!(hvl > 0)) return 80;
    if (sizeof(hvl) != sizeof(long double)) return 81;

    // __builtin_infl() -> long double infinity
    long double il = __builtin_infl();
    if (!(il > 0)) return 82;

    // __builtin_nanl("") -> long double NaN
    long double nl = __builtin_nanl("");
    if (nl == nl) return 83;  // NaN != NaN

    // ==== __builtin_alloca_with_align ====
    // align arg is in bits; 128 = 16 bytes (VM minimum)
    char *awa = (char *)__builtin_alloca_with_align(16, 128);
    if (!awa) return 84;
    awa[0] = 'X';
    if (awa[0] != 'X') return 85;

    // ==== __builtin_strlen / __builtin_strcmp (forwarded to libc) ====
    const char *s = "hello";
    if (__builtin_strlen(s) != strlen(s)) return 86;
    if (__builtin_strlen("") != 0) return 87;
    if (__builtin_strcmp("abc", "abc") != 0) return 88;
    if (__builtin_strcmp("abc", "abd") >= 0) return 89;
    if (__builtin_strcmp("abd", "abc") <= 0) return 90;

    // Verify __builtin_strlen agrees with <string.h> strlen on same symbol
    if (__builtin_strlen("world") != 5) return 91;

    return 42;
}

// test_volatile
[[cccc::test(return = 42)]]
int test_volatile(void) {
    // Basic volatile local
    volatile int x = 10;

    // Volatile pointer
    volatile int *vp = &x;

    // Pointer to volatile (unused but must parse)
    int * volatile pv;
    (void)pv;

    // Const volatile
    const volatile int cv = 100;

    // Read volatile variable
    int a = x;

    // Write volatile variable
    x = 20;

    // Read through volatile pointer (must re-read, not cache)
    int b = *vp;

    if (a != 10) return 1;
    if (b != 20) return 2;
    if (cv != 100) return 3;
    if (global_volatile != 42) return 4;

    // Alias test: write through non-volatile alias, read back via volatile
    // pointer — the volatile read must see the updated value.
    volatile int aliased = 0;
    int *alias = (int *)&aliased;
    *alias = 99;
    if (aliased != 99) return 5;

    // Loop test: volatile counter modified through alias inside loop.
    // If the volatile read were hoisted out of the loop this would fail.
    volatile int counter = 0;
    int *cp = (int *)&counter;
    for (int i = 0; i < 5; i++) {
        *cp = i;
        if (counter != i) return 6;
    }

    // Signal-flag pattern: write to volatile sig_atomic_t, read back.
    flag = 1;
    if (flag != 1) return 7;
    flag = 0;
    if (flag != 0) return 8;

    // Volatile struct member access
    struct { volatile int v; int n; } s;
    s.v = 77;
    s.n = 3;
    if (s.v != 77) return 9;

    // Volatile global write and re-read
    global_volatile = 55;
    if (global_volatile != 55) return 10;

    return 42;
}

#pragma cccc suite end
