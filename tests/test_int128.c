// GNU __int128 / __int128_t / __uint128_t support, implemented on top of the
// _BitInt(128) machinery. Exercises the spellings, signed/unsigned arithmetic,
// unary negation/complement, 128-bit-wide overflow, and feature detection.

#ifndef __SIZEOF_INT128__
#error "__SIZEOF_INT128__ should be defined when __int128 is supported"
#endif

_Static_assert(sizeof(__int128) == 16, "__int128 must be 16 bytes");
_Static_assert(sizeof(__uint128_t) == 16, "__uint128_t must be 16 bytes");
_Static_assert(sizeof(unsigned __int128) == 16, "unsigned __int128 must be 16 bytes");

int main(void) {
    // 128-bit overflow: 1e12 * 1e9 = 1e21, well beyond 64 bits.
    __int128 a = 1000000000000LL;
    __int128 c = a * 1000000000LL;            // 1e21
    unsigned long long hi = (unsigned long long)(c / 1000000000000000000LL);
    if (hi != 1000) return 1;

    // Unary negation of a wide value (the path that previously crashed).
    __int128 n = -(a * 1000000LL);            // -1e18
    if ((long long)(n / 1000000000000LL) != -1000000) return 2;

    // Bitwise complement.
    __int128 m = ~(__int128)5;
    if ((long long)m != -6) return 3;

    // unsigned __int128 / __uint128_t agreement and high-word arithmetic.
    __uint128_t u = (__uint128_t)1 << 100;
    if ((unsigned long long)(u >> 100) != 1ULL) return 4;
    unsigned __int128 v = u;
    if (v != u) return 5;

    // __int128_t alias is signed.
    __int128_t s = -1;
    if (s >= 0) return 6;

    // Truthiness must reflect the whole value, not just the low 64 bits.
    __int128 zero = 0;
    __int128 high = (__int128)1 << 100;  // nonzero, low word is 0
    if (zero) return 7;                  // if() on address would wrongly take this
    if (!high) return 8;                 // ! must see the high bits
    if (!(_Bool)high) return 9;          // (_Bool) cast must see the high bits
    if (zero || !high) return 10;        // || short-circuit truthiness
    if (!(high && !zero)) return 11;     // && truthiness
    int iters = 0;
    for (__int128 i = high; i; i = 0) iters++;
    if (iters != 1) return 12;           // for-condition truthiness

    return 42;
}
