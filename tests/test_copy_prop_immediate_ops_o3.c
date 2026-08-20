// CCCC_FLAGS: --optimize=3
// CCCC_MATRIX_SKIP: depends on --optimize=3 (copy-prop specifically)
#include <stdatomic.h>
// Ticket #759: copy-prop's op_operand_word_is_immediate() classifier was
// missing IOVFL, AXCHG, and ACAS. All three carry a packed immediate (not a
// register triple) in their pc+1 operand word, and all three implicitly
// redefine REG_A0 (IOVFL: overflow bool; AXCHG/ACAS: old value / CAS bool)
// in a way invisible to sub-pass A's generic operand-word scan.
//
// Root cause reproduced live (pre-fix): a `MOV3 A0, S2` feeding IOVFL's `a`
// argument left `copy_of[A0] == S2` in sub-pass A's copy map. IOVFL was not
// in the clear-all list, so that stale fact survived across the instruction
// that redefines A0 with the overflow bool. A later `JZ3` testing the
// overflow result got its condition register silently rewritten from A0 to
// S2 (copy propagation "helpfully" substituting the stale fact), so the
// branch tested the wrong value. Under `--optimize=3` this loop returned
// -1 (exit code 255) instead of 40.
static int sum_with_overflow_check(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        int t = i * 2;
        int r;
        if (__builtin_add_overflow(sum, t, &r))
            return -1;
        sum = r + t;
    }
    return sum;
}

// Same shape at `long` width (different size_enc byte in IOVFL's packed
// immediate) and with subtraction/multiplication (different op_type byte).
static long sub_overflow_loop(long n) {
    long acc = 1000;
    for (long i = 0; i < n; i++) {
        long r;
        if (__builtin_sub_overflow(acc, i, &r))
            return -1;
        acc = r;
    }
    return acc;
}

static int mul_overflow_loop(int n) {
    int acc = 1;
    for (int i = 1; i <= n; i++) {
        int r;
        if (__builtin_mul_overflow(acc, i, &r))
            return -1;
        acc = r;
    }
    return acc;
}

// AXCHG: implicit A0 read (new value) + write (old value).
static int axchg_check(void) {
    _Atomic int x    = 5;
    int         live = 7;
    int         old  = atomic_exchange(&x, live + 1);
    if (old != 5)
        return -1;
    if (x != 8)
        return -1;
    return live;
}

// ACAS: implicit A0/A1 read + A0 write (bool result).
static int acas_check(void) {
    _Atomic int x        = 10;
    int         live     = 3;
    int         expected = 10;
    int         ok = atomic_compare_exchange_strong(&x, &expected, live + 20);
    if (!ok)
        return -1;
    if (x != 23)
        return -1;
    ok = atomic_compare_exchange_strong(&x, &expected, 999);
    if (ok)
        return -1; // expected was stale (10), should fail and refresh
    if (expected != 23)
        return -1;
    return live;
}

int main(void) {
    if (sum_with_overflow_check(5) != 40)
        return 1;
    if (sub_overflow_loop(5) != 990)
        return 2;
    if (mul_overflow_loop(5) != 120)
        return 3;
    if (axchg_check() != 7)
        return 4;
    if (acas_check() != 3)
        return 5;
    return 42;
}
