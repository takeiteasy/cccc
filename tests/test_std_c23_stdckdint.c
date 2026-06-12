// CCCC_FLAGS: --std=c23
// <stdckdint.h> - C23 checked integer arithmetic (ticket #390)
#include <stdckdint.h>
#include <limits.h>

int main(void) {
    int r;

    // No overflow
    if (ckd_add(&r, 1, 2)) return 1;
    if (r != 3) return 2;

    if (ckd_sub(&r, 5, 3)) return 3;
    if (r != 2) return 4;

    if (ckd_mul(&r, 3, 4)) return 5;
    if (r != 12) return 6;

    // Overflow detected
    if (!ckd_add(&r, INT_MAX, 1)) return 7;
    if (!ckd_sub(&r, INT_MIN, 1)) return 8;
    if (!ckd_mul(&r, INT_MAX, 2)) return 9;

    // long long variant
    long long rll;
    if (ckd_add(&rll, 1ll, 2ll)) return 10;
    if (rll != 3ll) return 11;
    if (!ckd_mul(&rll, (long long)LLONG_MAX, 2ll)) return 12;

    return 42;
}
