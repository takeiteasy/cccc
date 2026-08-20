/* stdbit.h - C23 bit manipulation utilities (N3220 clause 7.18)
 *
 * Full implementation covering:
 *   - All 5 width suffixes: _uc (uint8), _us (uint16), _ui (uint32),
 *     _ul (uint64), _ull (uint64)
 *   - All 14 operations: leading/trailing zeros/ones, count ones/zeros,
 *     bit_width, has_single_bit, bit_floor, bit_ceil,
 *     first_leading/trailing one/zero
 *   - _Generic dispatch macros for unsuffixed (type-generic) calls
 *   - Endian macros (__STDC_ENDIAN_LITTLE__, __STDC_ENDIAN_BIG__,
 *     __STDC_ENDIAN_NATIVE__)
 */

#ifndef __STDBIT_H
#define __STDBIT_H

#define __STDC_VERSION_STDBIT_H__ 202311L

/* ---- Endian macros (C23 clause 7.18.4) ---- */

#define __STDC_ENDIAN_LITTLE__ 1234
#define __STDC_ENDIAN_BIG__    4321
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define __STDC_ENDIAN_NATIVE__ __STDC_ENDIAN_BIG__
#else
#define __STDC_ENDIAN_NATIVE__ __STDC_ENDIAN_LITTLE__
#endif

/* ===========================================================================
 * stdc_leading_zeros
 * =========================================================================*/

static inline unsigned int stdc_leading_zeros_uc(unsigned char x) {
    return x == 0 ? 8 : (unsigned int)(__builtin_clz((unsigned int)x) - 24);
}

static inline unsigned int stdc_leading_zeros_us(unsigned short x) {
    return x == 0 ? 16 : (unsigned int)(__builtin_clz((unsigned int)x) - 16);
}

static inline unsigned int stdc_leading_zeros_ui(unsigned int x) {
    return x == 0 ? 32 : (unsigned int)__builtin_clz(x);
}

static inline unsigned int stdc_leading_zeros_ul(unsigned long x) {
    return x == 0 ? 64 : (unsigned int)__builtin_clzll((unsigned long long)x);
}

static inline unsigned int stdc_leading_zeros_ull(unsigned long long x) {
    return x == 0 ? 64 : (unsigned int)__builtin_clzll(x);
}

/* ===========================================================================
 * stdc_trailing_zeros
 * =========================================================================*/

static inline unsigned int stdc_trailing_zeros_uc(unsigned char x) {
    return x == 0 ? 8 : (unsigned int)__builtin_ctz((unsigned int)x);
}

static inline unsigned int stdc_trailing_zeros_us(unsigned short x) {
    return x == 0 ? 16 : (unsigned int)__builtin_ctz((unsigned int)x);
}

static inline unsigned int stdc_trailing_zeros_ui(unsigned int x) {
    return x == 0 ? 32 : (unsigned int)__builtin_ctz(x);
}

static inline unsigned int stdc_trailing_zeros_ul(unsigned long x) {
    return x == 0 ? 64 : (unsigned int)__builtin_ctzll((unsigned long long)x);
}

static inline unsigned int stdc_trailing_zeros_ull(unsigned long long x) {
    return x == 0 ? 64 : (unsigned int)__builtin_ctzll(x);
}

/* ===========================================================================
 * stdc_leading_ones
 * =========================================================================*/

static inline unsigned int stdc_leading_ones_uc(unsigned char x) {
    return stdc_leading_zeros_uc((unsigned char)(~x));
}

static inline unsigned int stdc_leading_ones_us(unsigned short x) {
    return stdc_leading_zeros_us((unsigned short)(~x));
}

static inline unsigned int stdc_leading_ones_ui(unsigned int x) {
    return stdc_leading_zeros_ui(~x);
}

static inline unsigned int stdc_leading_ones_ul(unsigned long x) {
    return stdc_leading_zeros_ul(~x);
}

static inline unsigned int stdc_leading_ones_ull(unsigned long long x) {
    return stdc_leading_zeros_ull(~x);
}

/* ===========================================================================
 * stdc_trailing_ones
 * =========================================================================*/

static inline unsigned int stdc_trailing_ones_uc(unsigned char x) {
    return stdc_trailing_zeros_uc((unsigned char)(~x));
}

static inline unsigned int stdc_trailing_ones_us(unsigned short x) {
    return stdc_trailing_zeros_us((unsigned short)(~x));
}

static inline unsigned int stdc_trailing_ones_ui(unsigned int x) {
    return stdc_trailing_zeros_ui(~x);
}

static inline unsigned int stdc_trailing_ones_ul(unsigned long x) {
    return stdc_trailing_zeros_ul(~x);
}

static inline unsigned int stdc_trailing_ones_ull(unsigned long long x) {
    return stdc_trailing_zeros_ull(~x);
}

/* ===========================================================================
 * stdc_count_ones
 * =========================================================================*/

static inline unsigned int stdc_count_ones_uc(unsigned char x) {
    return (unsigned int)__builtin_popcount((unsigned int)x);
}

static inline unsigned int stdc_count_ones_us(unsigned short x) {
    return (unsigned int)__builtin_popcount((unsigned int)x);
}

static inline unsigned int stdc_count_ones_ui(unsigned int x) {
    return (unsigned int)__builtin_popcount(x);
}

static inline unsigned int stdc_count_ones_ul(unsigned long x) {
    return (unsigned int)__builtin_popcountll((unsigned long long)x);
}

static inline unsigned int stdc_count_ones_ull(unsigned long long x) {
    return (unsigned int)__builtin_popcountll(x);
}

/* ===========================================================================
 * stdc_count_zeros
 * =========================================================================*/

static inline unsigned int stdc_count_zeros_uc(unsigned char x) {
    return 8u - stdc_count_ones_uc(x);
}

static inline unsigned int stdc_count_zeros_us(unsigned short x) {
    return 16u - stdc_count_ones_us(x);
}

static inline unsigned int stdc_count_zeros_ui(unsigned int x) {
    return 32u - stdc_count_ones_ui(x);
}

static inline unsigned int stdc_count_zeros_ul(unsigned long x) {
    return 64u - stdc_count_ones_ul(x);
}

static inline unsigned int stdc_count_zeros_ull(unsigned long long x) {
    return 64u - stdc_count_ones_ull(x);
}

/* ===========================================================================
 * stdc_bit_width
 * =========================================================================*/

static inline unsigned int stdc_bit_width_uc(unsigned char x) {
    return x == 0 ? 0u : 32u - (unsigned int)__builtin_clz((unsigned int)x);
}

static inline unsigned int stdc_bit_width_us(unsigned short x) {
    return x == 0 ? 0u : 32u - (unsigned int)__builtin_clz((unsigned int)x);
}

static inline unsigned int stdc_bit_width_ui(unsigned int x) {
    return x == 0 ? 0u : 32u - (unsigned int)__builtin_clz(x);
}

static inline unsigned int stdc_bit_width_ul(unsigned long x) {
    return x == 0 ? 0u
                  : 64u - (unsigned int)__builtin_clzll((unsigned long long)x);
}

static inline unsigned int stdc_bit_width_ull(unsigned long long x) {
    return x == 0 ? 0u : 64u - (unsigned int)__builtin_clzll(x);
}

/* ===========================================================================
 * stdc_has_single_bit
 * =========================================================================*/

static inline int stdc_has_single_bit_uc(unsigned char x) {
    return x != 0 && (x & (unsigned char)(x - 1)) == 0;
}

static inline int stdc_has_single_bit_us(unsigned short x) {
    return x != 0 && (x & (unsigned short)(x - 1)) == 0;
}

static inline int stdc_has_single_bit_ui(unsigned int x) {
    return x != 0 && (x & (x - 1)) == 0;
}

static inline int stdc_has_single_bit_ul(unsigned long x) {
    return x != 0 && (x & (x - 1)) == 0;
}

static inline int stdc_has_single_bit_ull(unsigned long long x) {
    return x != 0 && (x & (x - 1)) == 0;
}

/* ===========================================================================
 * stdc_bit_floor
 * =========================================================================*/

static inline unsigned char stdc_bit_floor_uc(unsigned char x) {
    if (x == 0)
        return 0;
    return (unsigned char)(1u << (stdc_bit_width_uc(x) - 1));
}

static inline unsigned short stdc_bit_floor_us(unsigned short x) {
    if (x == 0)
        return 0;
    return (unsigned short)(1u << (stdc_bit_width_us(x) - 1));
}

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

/* ===========================================================================
 * stdc_bit_ceil
 *
 * Per C23, if the result is not representable the behaviour is
 * implementation-defined; this implementation returns 0.
 * =========================================================================*/

static inline unsigned char stdc_bit_ceil_uc(unsigned char x) {
    if (x <= 1)
        return 1;
    unsigned int w = stdc_bit_width_uc((unsigned char)(x - 1));
    if (w >= 8)
        return 0;
    return (unsigned char)(1u << w);
}

static inline unsigned short stdc_bit_ceil_us(unsigned short x) {
    if (x <= 1)
        return 1;
    unsigned int w = stdc_bit_width_us((unsigned short)(x - 1));
    if (w >= 16)
        return 0;
    return (unsigned short)(1u << w);
}

static inline unsigned int stdc_bit_ceil_ui(unsigned int x) {
    if (x <= 1)
        return 1;
    unsigned int w = stdc_bit_width_ui(x - 1);
    if (w >= 32)
        return 0;
    return 1u << w;
}

static inline unsigned long stdc_bit_ceil_ul(unsigned long x) {
    if (x <= 1)
        return 1;
    unsigned int w = stdc_bit_width_ul(x - 1);
    if (w >= 64)
        return 0;
    return 1ul << w;
}

static inline unsigned long long stdc_bit_ceil_ull(unsigned long long x) {
    if (x <= 1)
        return 1;
    unsigned int w = stdc_bit_width_ull(x - 1);
    if (w >= 64)
        return 0;
    return 1ull << w;
}

/* ===========================================================================
 * stdc_first_leading_one
 * Position (1-indexed from MSB) of the first 1-bit; 0 if none.
 * =========================================================================*/

static inline unsigned int stdc_first_leading_one_uc(unsigned char x) {
    return x == 0 ? 0u : stdc_leading_zeros_uc(x) + 1u;
}

static inline unsigned int stdc_first_leading_one_us(unsigned short x) {
    return x == 0 ? 0u : stdc_leading_zeros_us(x) + 1u;
}

static inline unsigned int stdc_first_leading_one_ui(unsigned int x) {
    return x == 0 ? 0u : stdc_leading_zeros_ui(x) + 1u;
}

static inline unsigned int stdc_first_leading_one_ul(unsigned long x) {
    return x == 0 ? 0u : stdc_leading_zeros_ul(x) + 1u;
}

static inline unsigned int stdc_first_leading_one_ull(unsigned long long x) {
    return x == 0 ? 0u : stdc_leading_zeros_ull(x) + 1u;
}

/* ===========================================================================
 * stdc_first_leading_zero
 * Position (1-indexed from MSB) of the first 0-bit; 0 if none.
 * =========================================================================*/

static inline unsigned int stdc_first_leading_zero_uc(unsigned char x) {
    return x == (unsigned char)-1 ? 0u : stdc_leading_ones_uc(x) + 1u;
}

static inline unsigned int stdc_first_leading_zero_us(unsigned short x) {
    return x == (unsigned short)-1 ? 0u : stdc_leading_ones_us(x) + 1u;
}

static inline unsigned int stdc_first_leading_zero_ui(unsigned int x) {
    return x == (unsigned int)-1 ? 0u : stdc_leading_ones_ui(x) + 1u;
}

static inline unsigned int stdc_first_leading_zero_ul(unsigned long x) {
    return x == (unsigned long)-1 ? 0u : stdc_leading_ones_ul(x) + 1u;
}

static inline unsigned int stdc_first_leading_zero_ull(unsigned long long x) {
    return x == (unsigned long long)-1 ? 0u : stdc_leading_ones_ull(x) + 1u;
}

/* ===========================================================================
 * stdc_first_trailing_one
 * Position (1-indexed from LSB) of the first 1-bit; 0 if none.
 * =========================================================================*/

static inline unsigned int stdc_first_trailing_one_uc(unsigned char x) {
    return x == 0 ? 0u : stdc_trailing_zeros_uc(x) + 1u;
}

static inline unsigned int stdc_first_trailing_one_us(unsigned short x) {
    return x == 0 ? 0u : stdc_trailing_zeros_us(x) + 1u;
}

static inline unsigned int stdc_first_trailing_one_ui(unsigned int x) {
    return x == 0 ? 0u : stdc_trailing_zeros_ui(x) + 1u;
}

static inline unsigned int stdc_first_trailing_one_ul(unsigned long x) {
    return x == 0 ? 0u : stdc_trailing_zeros_ul(x) + 1u;
}

static inline unsigned int stdc_first_trailing_one_ull(unsigned long long x) {
    return x == 0 ? 0u : stdc_trailing_zeros_ull(x) + 1u;
}

/* ===========================================================================
 * stdc_first_trailing_zero
 * Position (1-indexed from LSB) of the first 0-bit; 0 if none.
 * =========================================================================*/

static inline unsigned int stdc_first_trailing_zero_uc(unsigned char x) {
    return x == (unsigned char)-1 ? 0u : stdc_trailing_ones_uc(x) + 1u;
}

static inline unsigned int stdc_first_trailing_zero_us(unsigned short x) {
    return x == (unsigned short)-1 ? 0u : stdc_trailing_ones_us(x) + 1u;
}

static inline unsigned int stdc_first_trailing_zero_ui(unsigned int x) {
    return x == (unsigned int)-1 ? 0u : stdc_trailing_ones_ui(x) + 1u;
}

static inline unsigned int stdc_first_trailing_zero_ul(unsigned long x) {
    return x == (unsigned long)-1 ? 0u : stdc_trailing_ones_ul(x) + 1u;
}

static inline unsigned int stdc_first_trailing_zero_ull(unsigned long long x) {
    return x == (unsigned long long)-1 ? 0u : stdc_trailing_ones_ull(x) + 1u;
}

/* ===========================================================================
 * _Generic dispatch macros - unsuffixed type-generic interface
 * =========================================================================*/

#define stdc_leading_zeros(x)                                                  \
    _Generic((x),                                                              \
        unsigned char: stdc_leading_zeros_uc,                                  \
        unsigned short: stdc_leading_zeros_us,                                 \
        unsigned int: stdc_leading_zeros_ui,                                   \
        unsigned long: stdc_leading_zeros_ul,                                  \
        unsigned long long: stdc_leading_zeros_ull)(x)

#define stdc_trailing_zeros(x)                                                 \
    _Generic((x),                                                              \
        unsigned char: stdc_trailing_zeros_uc,                                 \
        unsigned short: stdc_trailing_zeros_us,                                \
        unsigned int: stdc_trailing_zeros_ui,                                  \
        unsigned long: stdc_trailing_zeros_ul,                                 \
        unsigned long long: stdc_trailing_zeros_ull)(x)

#define stdc_leading_ones(x)                                                   \
    _Generic((x),                                                              \
        unsigned char: stdc_leading_ones_uc,                                   \
        unsigned short: stdc_leading_ones_us,                                  \
        unsigned int: stdc_leading_ones_ui,                                    \
        unsigned long: stdc_leading_ones_ul,                                   \
        unsigned long long: stdc_leading_ones_ull)(x)

#define stdc_trailing_ones(x)                                                  \
    _Generic((x),                                                              \
        unsigned char: stdc_trailing_ones_uc,                                  \
        unsigned short: stdc_trailing_ones_us,                                 \
        unsigned int: stdc_trailing_ones_ui,                                   \
        unsigned long: stdc_trailing_ones_ul,                                  \
        unsigned long long: stdc_trailing_ones_ull)(x)

#define stdc_count_ones(x)                                                     \
    _Generic((x),                                                              \
        unsigned char: stdc_count_ones_uc,                                     \
        unsigned short: stdc_count_ones_us,                                    \
        unsigned int: stdc_count_ones_ui,                                      \
        unsigned long: stdc_count_ones_ul,                                     \
        unsigned long long: stdc_count_ones_ull)(x)

#define stdc_count_zeros(x)                                                    \
    _Generic((x),                                                              \
        unsigned char: stdc_count_zeros_uc,                                    \
        unsigned short: stdc_count_zeros_us,                                   \
        unsigned int: stdc_count_zeros_ui,                                     \
        unsigned long: stdc_count_zeros_ul,                                    \
        unsigned long long: stdc_count_zeros_ull)(x)

#define stdc_bit_width(x)                                                      \
    _Generic((x),                                                              \
        unsigned char: stdc_bit_width_uc,                                      \
        unsigned short: stdc_bit_width_us,                                     \
        unsigned int: stdc_bit_width_ui,                                       \
        unsigned long: stdc_bit_width_ul,                                      \
        unsigned long long: stdc_bit_width_ull)(x)

#define stdc_has_single_bit(x)                                                 \
    _Generic((x),                                                              \
        unsigned char: stdc_has_single_bit_uc,                                 \
        unsigned short: stdc_has_single_bit_us,                                \
        unsigned int: stdc_has_single_bit_ui,                                  \
        unsigned long: stdc_has_single_bit_ul,                                 \
        unsigned long long: stdc_has_single_bit_ull)(x)

#define stdc_bit_floor(x)                                                      \
    _Generic((x),                                                              \
        unsigned char: stdc_bit_floor_uc,                                      \
        unsigned short: stdc_bit_floor_us,                                     \
        unsigned int: stdc_bit_floor_ui,                                       \
        unsigned long: stdc_bit_floor_ul,                                      \
        unsigned long long: stdc_bit_floor_ull)(x)

#define stdc_bit_ceil(x)                                                       \
    _Generic((x),                                                              \
        unsigned char: stdc_bit_ceil_uc,                                       \
        unsigned short: stdc_bit_ceil_us,                                      \
        unsigned int: stdc_bit_ceil_ui,                                        \
        unsigned long: stdc_bit_ceil_ul,                                       \
        unsigned long long: stdc_bit_ceil_ull)(x)

#define stdc_first_leading_one(x)                                              \
    _Generic((x),                                                              \
        unsigned char: stdc_first_leading_one_uc,                              \
        unsigned short: stdc_first_leading_one_us,                             \
        unsigned int: stdc_first_leading_one_ui,                               \
        unsigned long: stdc_first_leading_one_ul,                              \
        unsigned long long: stdc_first_leading_one_ull)(x)

#define stdc_first_leading_zero(x)                                             \
    _Generic((x),                                                              \
        unsigned char: stdc_first_leading_zero_uc,                             \
        unsigned short: stdc_first_leading_zero_us,                            \
        unsigned int: stdc_first_leading_zero_ui,                              \
        unsigned long: stdc_first_leading_zero_ul,                             \
        unsigned long long: stdc_first_leading_zero_ull)(x)

#define stdc_first_trailing_one(x)                                             \
    _Generic((x),                                                              \
        unsigned char: stdc_first_trailing_one_uc,                             \
        unsigned short: stdc_first_trailing_one_us,                            \
        unsigned int: stdc_first_trailing_one_ui,                              \
        unsigned long: stdc_first_trailing_one_ul,                             \
        unsigned long long: stdc_first_trailing_one_ull)(x)

#define stdc_first_trailing_zero(x)                                            \
    _Generic((x),                                                              \
        unsigned char: stdc_first_trailing_zero_uc,                            \
        unsigned short: stdc_first_trailing_zero_us,                           \
        unsigned int: stdc_first_trailing_zero_ui,                             \
        unsigned long: stdc_first_trailing_zero_ul,                            \
        unsigned long long: stdc_first_trailing_zero_ull)(x)

#endif /* __STDBIT_H */
