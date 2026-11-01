// CCCC_NATIVE_SKIP: a shift count of 64 or more is undefined in C; the VM masks it to 6 bits
#include <limits.h>
// NEG3 of LLONG_MIN and the register shifts by an out-of-range count must not
// be undefined on the host (run under test_ubsan). Values arrive through
// noinline parameters so nothing is folded.
[[gnu::noinline]] long long neg(long long x) {
    return -x;
}
[[gnu::noinline]] unsigned long long shl(unsigned long long x, long long n) {
    return x << n;
}
[[gnu::noinline]] long long shr(long long x, long long n) {
    return x >> n;
}
[[gnu::noinline]] unsigned long long ushr(unsigned long long x, long long n) {
    return x >> n;
}

int main(void) {
    if (neg(LLONG_MIN) != LLONG_MIN || neg(5) != -5 || neg(-5) != 5 ||
        neg(0) != 0)
        return 1;
    if (shl(1, 0) != 1 || shl(1, 63) != 1ull << 63 || shl(1, 64) != 1 ||
        shl(1, -1) != 1ull << 63)
        return 2;
    if (shr(-8, 1) != -4 || shr(-8, 64) != -8 || shr(-8, -1) != -1)
        return 3;
    if (ushr(1ull << 63, 63) != 1 || ushr(8, 64) != 8 ||
        ushr(1ull << 63, -1) != 1)
        return 4;
    return 42;
}
