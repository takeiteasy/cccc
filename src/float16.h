// IEEE 754 binary16 conversions, independent of any host `_Float16`.
// A `_Float16` value travels as its 16-bit pattern; the VM widens it to a
// double for arithmetic and rounds back on a store (see H2FR/FR2H).
#ifndef CCCC_FLOAT16_H
#define CCCC_FLOAT16_H

#include <stdint.h>
#include <string.h>

// Round to nearest even in one step (no double rounding through float).
static inline uint16_t cccc_f16_from_double(double d) {
    uint64_t bits;
    memcpy(&bits, &d, sizeof bits);
    uint16_t sign = (uint16_t)((bits >> 63) << 15);
    int      exp  = (int)((bits >> 52) & 0x7ff);
    uint64_t mant = bits & 0xfffffffffffffULL;

    if (exp == 0x7ff)
        return (uint16_t)(sign | 0x7c00 | (mant ? 0x200 | (mant >> 42) : 0));
    if (exp == 0)
        return sign;

    int e = exp - 1023 + 15;
    if (e >= 31)
        return (uint16_t)(sign | 0x7c00);

    uint64_t q;
    if (e <= 0) {
        int shift = 1051 - exp;
        if (shift >= 64)
            return sign;
        uint64_t full = (1ULL << 52) | mant;
        uint64_t rem  = full & ((1ULL << shift) - 1);
        uint64_t half = 1ULL << (shift - 1);
        q             = full >> shift;
        if (rem > half || (rem == half && (q & 1)))
            q++;
        return (uint16_t)(sign | q);
    }

    q             = mant >> 42;
    uint64_t rem  = mant & ((1ULL << 42) - 1);
    uint64_t half = 1ULL << 41;
    if (rem > half || (rem == half && (q & 1)))
        q++;
    uint64_t r = ((uint64_t)e << 10) + q;
    if (r >= 0x7c00)
        r = 0x7c00;
    return (uint16_t)(sign | r);
}

static inline double cccc_f16_to_double(uint16_t h) {
    uint64_t sign = (uint64_t)(h >> 15) << 63;
    int      exp  = (h >> 10) & 0x1f;
    uint64_t mant = h & 0x3ff;
    uint64_t bits;
    if (exp == 0x1f) {
        bits =
            sign | (0x7ffULL << 52) | (mant ? (1ULL << 51) | (mant << 42) : 0);
    } else if (exp == 0) {
        double v = (double)mant * 5.9604644775390625e-8; // 2^-24
        memcpy(&bits, &v, sizeof bits);
        bits |= sign;
    } else {
        bits = sign | ((uint64_t)(exp - 15 + 1023) << 52) | (mant << 42);
    }
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

#endif
