// CCCC_FLAGS: -O3
// Path-exercise and regression guard for the copy-prop sub-pass B fix (#497):
// AXCHG/ACAS carry a width_enc i64 immediate in their operand words, not a
// register encoding.  At -O3 with high register pressure the allocator may
// assign a live MOV3 destination to the same register number as the low byte
// of width_enc (r8/r9 for 4-byte atomics, r16/r17 for 8-byte).  The precise
// sub-pass B arm (else-if before the generic else) prevents the false
// KILL_INT_DEF from NOPing that MOV3.
//
// Note: the exact register-number collision is allocator-dependent and cannot
// be pinned from C source, so this is a path-exercise + regression guard, not
// a deterministic reproducer of the latent miscompile.
#include <stdatomic.h>
#include <stdio.h>

// High register pressure: many live locals crossing the atomic ops to force
// the register allocator to use higher-numbered registers (r8+) around the
// atomic sites.  Values are chosen so any miscompile produces a wrong result.
int main(void) {
    // --- int (4-byte) atomic: width_enc = 8 (signed) or 9 (unsigned) ---
    atomic_int xi = 0;

    // Many live locals — kept live across the exchange via sums.
    int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8;
    int i = 9, j = 10, k = 11, l = 12;

    atomic_store(&xi, 100);

    int old_int = atomic_exchange(&xi, 200);
    if (old_int != 100) {
        printf("FAIL: atomic_exchange int: old=%d want 100\n", old_int);
        return 1;
    }
    if (atomic_load(&xi) != 200) {
        printf("FAIL: atomic_exchange int: new=%d want 200\n", atomic_load(&xi));
        return 2;
    }

    // Use the live locals so they stay live across the exchange.
    int sum = a + b + c + d + e + f + g + h + i + j + k + l;
    if (sum != 78) {
        printf("FAIL: locals corrupted: sum=%d want 78\n", sum);
        return 3;
    }

    // CAS success case.
    int expected_i = 200;
    int r = atomic_compare_exchange_strong(&xi, &expected_i, 300);
    if (!r || atomic_load(&xi) != 300) {
        printf("FAIL: CAS int success: r=%d val=%d\n", r, atomic_load(&xi));
        return 4;
    }

    // CAS failure case.
    expected_i = 999;  // wrong expected
    r = atomic_compare_exchange_strong(&xi, &expected_i, 400);
    if (r || atomic_load(&xi) != 300 || expected_i != 300) {
        printf("FAIL: CAS int fail: r=%d val=%d exp=%d\n", r, atomic_load(&xi), expected_i);
        return 5;
    }

    // --- long (8-byte) atomic: width_enc = 16 (signed) or 17 (unsigned) ---
    atomic_long xl = 0;

    long p = 100, q = 200, s2 = 300, t = 400, u = 500, v = 600;
    long w2 = 700, x2 = 800, y = 900, z = 1000;

    atomic_store(&xl, 10000LL);

    long old_long = atomic_exchange(&xl, 20000LL);
    if (old_long != 10000LL) {
        printf("FAIL: atomic_exchange long: old=%ld want 10000\n", old_long);
        return 6;
    }
    if (atomic_load(&xl) != 20000LL) {
        printf("FAIL: atomic_exchange long: new=%ld want 20000\n", atomic_load(&xl));
        return 7;
    }

    // Keep long locals live.
    long lsum = p + q + s2 + t + u + v + w2 + x2 + y + z;
    if (lsum != 5500LL) {
        printf("FAIL: long locals corrupted: lsum=%ld want 5500\n", lsum);
        return 8;
    }

    // CAS success.
    long expected_l = 20000LL;
    int rl = atomic_compare_exchange_strong(&xl, &expected_l, 30000LL);
    if (!rl || atomic_load(&xl) != 30000LL) {
        printf("FAIL: CAS long success: r=%d val=%ld\n", rl, atomic_load(&xl));
        return 9;
    }

    // CAS failure.
    expected_l = 99999LL;
    rl = atomic_compare_exchange_strong(&xl, &expected_l, 40000LL);
    if (rl || atomic_load(&xl) != 30000LL || expected_l != 30000LL) {
        printf("FAIL: CAS long fail: r=%d val=%ld exp=%ld\n", rl, atomic_load(&xl), expected_l);
        return 10;
    }

    return 42;
}
