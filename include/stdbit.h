/* stdbit.h - C23 bit manipulation utilities for CCCC C compiler
 *
 * NOTE: This is a partial implementation covering the "core" operations
 * for the 32-bit (unsigned int) and 64-bit (unsigned long / unsigned long
 * long) widths only. uint8_t/uint16_t typed variants, the type-generic
 * (no-suffix) _Generic dispatch macros, and the remaining C23 operations
 * (stdc_count_zeros, stdc_leading_ones, stdc_trailing_ones,
 * stdc_first_leading/trailing_one/zero, byteswap/endian helpers) are not
 * yet implemented - see ticket tracker for the follow-up.
 */

#ifndef __STDBIT_H
#define __STDBIT_H

#define __STDC_VERSION_STDBIT_H__ 202311L

/* ---- stdc_leading_zeros ---- */

static inline int stdc_leading_zeros_ui(unsigned int x) {
    return __builtin_clz(x);
}

static inline int stdc_leading_zeros_ul(unsigned long x) {
    return __builtin_clzll(x);
}

static inline int stdc_leading_zeros_ull(unsigned long long x) {
    return __builtin_clzll(x);
}

/* ---- stdc_trailing_zeros ---- */

static inline int stdc_trailing_zeros_ui(unsigned int x) {
    return __builtin_ctz(x);
}

static inline int stdc_trailing_zeros_ul(unsigned long x) {
    return __builtin_ctzll(x);
}

static inline int stdc_trailing_zeros_ull(unsigned long long x) {
    return __builtin_ctzll(x);
}

/* ---- stdc_count_ones ---- */

static inline int stdc_count_ones_ui(unsigned int x) {
    return __builtin_popcount(x);
}

static inline int stdc_count_ones_ul(unsigned long x) {
    return __builtin_popcountll(x);
}

static inline int stdc_count_ones_ull(unsigned long long x) {
    return __builtin_popcountll(x);
}

/* ---- stdc_bit_width ---- */

static inline int stdc_bit_width_ui(unsigned int x) {
    return x == 0 ? 0 : 32 - __builtin_clz(x);
}

static inline int stdc_bit_width_ul(unsigned long x) {
    return x == 0 ? 0 : 64 - __builtin_clzll(x);
}

static inline int stdc_bit_width_ull(unsigned long long x) {
    return x == 0 ? 0 : 64 - __builtin_clzll(x);
}

/* ---- stdc_has_single_bit ---- */

static inline int stdc_has_single_bit_ui(unsigned int x) {
    return x != 0 && (x & (x - 1)) == 0;
}

static inline int stdc_has_single_bit_ul(unsigned long x) {
    return x != 0 && (x & (x - 1)) == 0;
}

static inline int stdc_has_single_bit_ull(unsigned long long x) {
    return x != 0 && (x & (x - 1)) == 0;
}

/* ---- stdc_bit_floor ---- */

static inline unsigned int stdc_bit_floor_ui(unsigned int x) {
    if (x == 0)
        return 0;
    return 1u << (stdc_bit_width_ui(x) - 1);
}

static inline unsigned long stdc_bit_floor_ul(unsigned long x) {
    if (x == 0)
        return 0;
    return 1ul << (stdc_bit_width_ul(x) - 1);
}

static inline unsigned long long stdc_bit_floor_ull(unsigned long long x) {
    if (x == 0)
        return 0;
    return 1ull << (stdc_bit_width_ull(x) - 1);
}

/* ---- stdc_bit_ceil ----
 *
 * Per C23, if the result is not representable in the return type the
 * behaviour is implementation-defined; this implementation returns 0.
 */

static inline unsigned int stdc_bit_ceil_ui(unsigned int x) {
    if (x <= 1)
        return 1;
    int w = stdc_bit_width_ui(x - 1);
    if (w >= 32)
        return 0;
    return 1u << w;
}

static inline unsigned long stdc_bit_ceil_ul(unsigned long x) {
    if (x <= 1)
        return 1;
    int w = stdc_bit_width_ul(x - 1);
    if (w >= 64)
        return 0;
    return 1ul << w;
}

static inline unsigned long long stdc_bit_ceil_ull(unsigned long long x) {
    if (x <= 1)
        return 1;
    int w = stdc_bit_width_ull(x - 1);
    if (w >= 64)
        return 0;
    return 1ull << w;
}

#endif /* __STDBIT_H */
