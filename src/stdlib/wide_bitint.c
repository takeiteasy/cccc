// Wide _BitInt runtime helpers for N in (64, 65535].
// All operations use little-endian word arrays: w[0] is the least-significant
// 64-bit word.  The caller allocates dst; all pointer args are uint64_t *.
// "words" is the number of 64-bit words (ceil(N/64), max 1025).
// After arithmetic ops, __cccc_bitint_trunc must be called on the result to
// enforce the exact bit width (mask the top word).
#include "../cccc.h"
#include <stdint.h>
#include <string.h>

// ---------- helpers ----------

// Mask for the top word given an exact bit width.
// e.g. width=65 → words=2 → top word mask = (1<<1)-1 = 1
static uint64_t top_word_mask(int width) {
    int rem = width & 63; // bits used in top word (0 means full 64 bits)
    return rem ? ((uint64_t)1 << rem) - 1 : ~(uint64_t)0;
}

// Sign-extend the top word so that signed comparisons and arithmetic work.
static void sign_extend_top(uint64_t *a, int words, int width) {
    int rem = width & 63;
    if (rem == 0)
        return;                                 // already full 64-bit word
    uint64_t sign_bit = (uint64_t)1 << (rem - 1);
    if (a[words - 1] & sign_bit) {
        a[words - 1] |= ~((sign_bit << 1) - 1); // fill high bits with 1
    } else {
        a[words - 1] &= (sign_bit << 1) - 1;
    }
}

// ---------- truncation ----------

// Mask dst to exactly width bits (zero-fill bits above bit_width-1).
void __cccc_bitint_trunc(uint64_t *dst, int words, int width) {
    dst[words - 1] &= top_word_mask(width);
}

// ---------- arithmetic ----------

void __cccc_bitint_add(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                       int words, int width) {
    uint64_t carry = 0;
    for (int i = 0; i < words; i++) {
        __uint128_t s = (__uint128_t)a[i] + b[i] + carry;
        dst[i]        = (uint64_t)s;
        carry         = (uint64_t)(s >> 64);
    }
    __cccc_bitint_trunc(dst, words, width);
}

void __cccc_bitint_sub(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                       int words, int width) {
    uint64_t borrow = 0;
    for (int i = 0; i < words; i++) {
        __uint128_t s = (__uint128_t)a[i] - b[i] - borrow;
        dst[i]        = (uint64_t)s;
        borrow        = (s >> 127) & 1; // underflow => borrow
    }
    __cccc_bitint_trunc(dst, words, width);
}

void __cccc_bitint_mul(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                       int words, int width) {
    uint64_t tmp[words];
    memset(tmp, 0, (size_t)words * 8);
    for (int i = 0; i < words; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < words - i; j++) {
            __uint128_t p = (__uint128_t)a[i] * b[j] + tmp[i + j] + carry;
            tmp[i + j]    = (uint64_t)p;
            carry         = (uint64_t)(p >> 64);
        }
    }
    memcpy(dst, tmp, (size_t)words * 8);
    __cccc_bitint_trunc(dst, words, width);
}

// ---------- unsigned division (Knuth Algorithm D, simplified) ----------

// Divide two-word (128-bit) numerator [hi:lo] by a 64-bit denominator d.
// Returns quotient; *rem receives the remainder.
static uint64_t div128_by_64(uint64_t lo, uint64_t hi, uint64_t d,
                             uint64_t *rem) {
    __uint128_t n = ((__uint128_t)hi << 64) | lo;
    *rem          = (uint64_t)(n % d);
    return (uint64_t)(n / d);
}

// Unsigned multi-word division: dst_q = a / b, dst_r = a % b.
// Both results may alias neither a nor b.
static void udivmod(uint64_t *dst_q, uint64_t *dst_r, const uint64_t *a,
                    const uint64_t *b, int words) {
    // Find actual length of b (skip leading zero words).
    int blen = words;
    while (blen > 1 && b[blen - 1] == 0)
        blen--;

    // Zero the outputs.
    memset(dst_q, 0, (size_t)words * 8);
    memset(dst_r, 0, (size_t)words * 8);

    if (blen == 1) {
        // Single-word divisor: simple long division.
        uint64_t rem = 0;
        for (int i = words - 1; i >= 0; i--) {
            __uint128_t cur = ((__uint128_t)rem << 64) | a[i];
            dst_q[i]        = (uint64_t)(cur / b[0]);
            rem             = (uint64_t)(cur % b[0]);
        }
        dst_r[0] = rem;
        return;
    }

    // Multi-word: shift b so its top bit is set (normalize), then use
    // trial subtraction.  We work at the granularity of 64-bit words.
    int alen = words;
    while (alen > 1 && a[alen - 1] == 0)
        alen--;

    if (alen < blen) {
        // a < b: quotient = 0, remainder = a.
        memcpy(dst_r, a, (size_t)words * 8);
        return;
    }

    // Normalize: find shift so b[blen-1] has its top bit set.
    int      shift = 0;
    uint64_t top   = b[blen - 1];
    while (!(top & ((uint64_t)1 << 63))) {
        top <<= 1;
        shift++;
    }

    // Shifted copies (need one extra word for the numerator).
    uint64_t u[words + 1]; // numerator (alen+1 words)
    uint64_t v[words];     // denominator (blen words)
    memset(u, 0, (size_t)(words + 1) * 8);
    memset(v, 0, (size_t)words * 8);

    // Shift a left by `shift` bits into u.
    for (int i = 0; i < alen; i++) {
        u[i] |= a[i] << shift;
        if (shift && i + 1 <= alen)
            u[i + 1] = a[i] >> (64 - shift);
    }
    // Shift b left by `shift` bits into v.
    for (int i = 0; i < blen; i++) {
        v[i] |= b[i] << shift;
        if (shift && i + 1 < blen)
            v[i + 1] = b[i] >> (64 - shift);
    }

    int m = alen - blen; // number of quotient digits
    for (int j = m; j >= 0; j--) {
        // Estimate q̂ = (u[j+blen]*B + u[j+blen-1]) / v[blen-1].
        uint64_t u_hi = (j + blen < words + 1) ? u[j + blen] : 0;
        uint64_t u_lo = u[j + blen - 1];
        uint64_t rem_unused;
        uint64_t qhat =
            (u_hi >= v[blen - 1])
                ? ~(uint64_t)0
                : div128_by_64(u_lo, u_hi, v[blen - 1], &rem_unused);

        // Refine qhat: subtract qhat * v from u[j..j+blen].
        // Use signed 128-bit arithmetic to detect overdraft.
        while (1) {
            __uint128_t borrow = 0;
            uint64_t    tmp[words + 1];
            for (int i = 0; i <= blen; i++) {
                uint64_t    vi = (i < blen) ? v[i] : 0;
                __uint128_t p  = (__uint128_t)qhat * vi + borrow;
                uint64_t    uj = (j + i < words + 1) ? u[j + i] : 0;
                if (uj >= (uint64_t)p) {
                    tmp[i] = uj - (uint64_t)p;
                    borrow = p >> 64;
                } else {
                    tmp[i] = uj - (uint64_t)p; // wraps
                    borrow = (p >> 64) + 1;
                }
            }
            if (borrow == 0) {
                // Apply the subtraction.
                for (int i = 0; i <= blen; i++) {
                    if (j + i < words + 1)
                        u[j + i] = tmp[i];
                }
                break;
            }
            qhat--;
        }
        if (j < words)
            dst_q[j] = qhat;
    }

    // Un-shift the remainder.
    if (shift == 0) {
        for (int i = 0; i < blen; i++)
            dst_r[i] = u[i];
    } else {
        for (int i = 0; i < blen; i++) {
            dst_r[i] = (u[i] >> shift) |
                       (i + 1 < words + 1 ? u[i + 1] << (64 - shift) : 0);
        }
    }
}

void __cccc_bitint_udiv(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                        int words, int width) {
    uint64_t r[words];
    udivmod(dst, r, a, b, words);
    __cccc_bitint_trunc(dst, words, width);
}

void __cccc_bitint_umod(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                        int words, int width) {
    uint64_t q[words];
    udivmod(q, dst, a, b, words);
    __cccc_bitint_trunc(dst, words, width);
}

// ---------- signed division/modulo ----------

// Two's-complement negate a multi-word integer in place.
static void negate(uint64_t *a, int words) {
    uint64_t carry = 1;
    for (int i = 0; i < words; i++) {
        __uint128_t s = (__uint128_t)(~a[i]) + carry;
        a[i]          = (uint64_t)s;
        carry         = (uint64_t)(s >> 64);
    }
}

// Is the value negative (top bit of top word set)?
static int is_negative(const uint64_t *a, int words, int width) {
    int top_bit = (width & 63) ? (width & 63) - 1 : 63;
    return (a[words - 1] >> top_bit) & 1;
}

void __cccc_bitint_sdiv(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                        int words, int width) {
    uint64_t ua[words], ub[words];
    memcpy(ua, a, (size_t)words * 8);
    memcpy(ub, b, (size_t)words * 8);
    // Sign-extend to know the sign.
    sign_extend_top(ua, words, width);
    sign_extend_top(ub, words, width);
    int neg_a = is_negative(ua, words, width);
    int neg_b = is_negative(ub, words, width);
    if (neg_a)
        negate(ua, words);
    if (neg_b)
        negate(ub, words);
    uint64_t r[words];
    udivmod(dst, r, ua, ub, words);
    if (neg_a ^ neg_b)
        negate(dst, words);
    __cccc_bitint_trunc(dst, words, width);
}

void __cccc_bitint_smod(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                        int words, int width) {
    uint64_t ua[words], ub[words];
    memcpy(ua, a, (size_t)words * 8);
    memcpy(ub, b, (size_t)words * 8);
    sign_extend_top(ua, words, width);
    sign_extend_top(ub, words, width);
    int neg_a = is_negative(ua, words, width);
    int neg_b = is_negative(ub, words, width);
    if (neg_a)
        negate(ua, words);
    if (neg_b)
        negate(ub, words);
    uint64_t q[words];
    udivmod(q, dst, ua, ub, words);
    if (neg_a)
        negate(dst, words); // remainder has sign of dividend
    __cccc_bitint_trunc(dst, words, width);
}

// ---------- shifts ----------

void __cccc_bitint_shl(uint64_t *dst, const uint64_t *a, long long shift,
                       int words, int width) {
    memset(dst, 0, (size_t)words * 8);
    if (shift < 0 || shift >= width) {
        return; // undefined shift → zero
    }
    int word_sh = (int)(shift / 64);
    int bit_sh  = (int)(shift % 64);
    for (int i = words - 1; i >= word_sh; i--) {
        dst[i] = a[i - word_sh] << bit_sh;
        if (bit_sh && i - word_sh - 1 >= 0)
            dst[i] |= a[i - word_sh - 1] >> (64 - bit_sh);
    }
    __cccc_bitint_trunc(dst, words, width);
}

// Logical (unsigned) right shift.
void __cccc_bitint_ushr(uint64_t *dst, const uint64_t *a, long long shift,
                        int words, int width) {
    memset(dst, 0, (size_t)words * 8);
    if (shift < 0 || shift >= width) {
        return;
    }
    int word_sh = (int)(shift / 64);
    int bit_sh  = (int)(shift % 64);
    for (int i = 0; i < words - word_sh; i++) {
        dst[i] = a[i + word_sh] >> bit_sh;
        if (bit_sh && i + word_sh + 1 < words)
            dst[i] |= a[i + word_sh + 1] << (64 - bit_sh);
    }
}

// Arithmetic (signed) right shift.
void __cccc_bitint_sshr(uint64_t *dst, const uint64_t *a, long long shift,
                        int words, int width) {
    // Start with logical shift.
    __cccc_bitint_ushr(dst, a, shift, words, width);
    // Sign-extend if the original value was negative.
    uint64_t tmp[words];
    memcpy(tmp, a, (size_t)words * 8);
    sign_extend_top(tmp, words, width);
    if (is_negative(tmp, words, width)) {
        // Fill high bits with 1s.
        int effective_shift = (shift >= width) ? width - 1 : (int)shift;
        // Number of high bits that should be 1.
        int ones_bits = effective_shift; // top `ones_bits` bits become 1
        if (ones_bits >= width)
            ones_bits = width;
        for (int bit = width - ones_bits; bit < width; bit++) {
            int w = bit / 64, b = bit % 64;
            dst[w] |= (uint64_t)1 << b;
        }
    }
    __cccc_bitint_trunc(dst, words, width);
}

// ---------- comparison ----------

// Returns -1, 0, or 1.  is_signed controls whether to treat values as signed.
long long __cccc_bitint_cmp(const uint64_t *a, const uint64_t *b, int words,
                            int width, int is_signed) {
    if (is_signed) {
        uint64_t sa[words], sb[words];
        memcpy(sa, a, (size_t)words * 8);
        memcpy(sb, b, (size_t)words * 8);
        sign_extend_top(sa, words, width);
        sign_extend_top(sb, words, width);
        int neg_a = is_negative(sa, words, width);
        int neg_b = is_negative(sb, words, width);
        if (neg_a != neg_b)
            return neg_a ? -1 : 1;
        // Same sign: compare unsigned (sign-extended) from MSW down.
        for (int i = words - 1; i >= 0; i--) {
            if (sa[i] < sb[i])
                return neg_a ? 1 : -1;
            if (sa[i] > sb[i])
                return neg_a ? -1 : 1;
        }
        return 0;
    }
    // Unsigned.
    uint64_t ma = top_word_mask(width), mb = ma;
    uint64_t aw = a[words - 1] & ma, bw = b[words - 1] & mb;
    if (aw != bw)
        return aw < bw ? -1 : 1;
    for (int i = words - 2; i >= 0; i--) {
        if (a[i] < b[i])
            return -1;
        if (a[i] > b[i])
            return 1;
    }
    return 0;
}

// ---------- conversions ----------

// Convert int64 → wide _BitInt (sign-extend or zero-extend).
void __cccc_bitint_from_i64(uint64_t *dst, long long val, int words,
                            int width) {
    dst[0]        = (uint64_t)val;
    uint64_t fill = (val < 0) ? ~(uint64_t)0 : 0;
    for (int i = 1; i < words; i++)
        dst[i] = fill;
    __cccc_bitint_trunc(dst, words, width);
}

// Convert uint64 → wide _BitInt (zero-extend).
void __cccc_bitint_from_u64(uint64_t *dst, unsigned long long val, int words,
                            int width) {
    dst[0] = val;
    for (int i = 1; i < words; i++)
        dst[i] = 0;
    __cccc_bitint_trunc(dst, words, width);
}

// Convert wide _BitInt(width_src) → wide _BitInt(width_dst), sign- or
// zero-extending (per is_signed_src) when growing, truncating when shrinking.
void __cccc_bitint_extend(uint64_t *dst, const uint64_t *src, int words_src,
                          int width_src, int words_dst, int width_dst,
                          int is_signed_src) {
    int copy_words = words_src < words_dst ? words_src : words_dst;
    memcpy(dst, src, (size_t)copy_words * 8);
    if (words_dst > words_src) {
        uint64_t fill = 0;
        if (is_signed_src) {
            sign_extend_top(dst, copy_words, width_src);
            if (is_negative(dst, copy_words, width_src))
                fill = ~(uint64_t)0;
        }
        for (int i = copy_words; i < words_dst; i++)
            dst[i] = fill;
    }
    __cccc_bitint_trunc(dst, words_dst, width_dst);
}

// Convert wide _BitInt → int64 (truncate).
long long __cccc_bitint_to_i64(const uint64_t *a, int words, int width,
                               int is_signed) {
    (void)words;
    (void)width;
    (void)is_signed;
    return (long long)a[0];
}

// Convert wide _BitInt → double, returned as raw IEEE-754 bit-pattern (long
// long).
long long __cccc_bitint_to_double(const uint64_t *a, int words, int width,
                                  int is_signed) {
    double d;
    if (is_signed) {
        uint64_t tmp[words];
        memcpy(tmp, a, (size_t)words * 8);
        sign_extend_top(tmp, words, width);
        if (is_negative(tmp, words, width)) {
            negate(tmp, words);
            d           = 0;
            double base = 1.0;
            for (int i = 0; i < words; i++) {
                // Skip zero words once `base` overflows to +inf (words > ~16,
                // i.e. width > ~1024) — 0 * inf is NaN, not 0.
                if (tmp[i] != 0)
                    d += (double)tmp[i] * base;
                base *= 18446744073709551616.0;
            }
            d = -d;
        } else {
            d           = 0;
            double base = 1.0;
            for (int i = 0; i < words; i++) {
                if (tmp[i] != 0)
                    d += (double)tmp[i] * base;
                base *= 18446744073709551616.0;
            }
        }
    } else {
        d           = 0;
        double base = 1.0;
        for (int i = 0; i < words; i++) {
            if (a[i] != 0)
                d += (double)a[i] * base;
            base *= 18446744073709551616.0;
        }
    }
    long long bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

// Convert double → wide _BitInt.  val_bits is the raw IEEE-754 bit pattern
// passed as int64 (avoids float calling-convention complexity in the VM).
void __cccc_bitint_from_double(uint64_t *dst, long long val_bits, int words,
                               int width, int is_signed) {
    double val;
    memcpy(&val, &val_bits, sizeof(val));
    int neg = (is_signed && val < 0);
    if (neg)
        val = -val;
    double base = 18446744073709551616.0; // 2^64
    memset(dst, 0, (size_t)words * 8);
    double rem = val;
    // word_bases[i] = base^i; this overflows to +inf once i*64 exceeds
    // double's ~1024-bit exponent range (relevant once words > ~16, i.e.
    // width > ~1024 bits) — guard against using a non-finite base below,
    // since the corresponding word is always 0 for any finite double value.
    double word_bases[words];
    word_bases[0] = 1.0;
    for (int i = 1; i < words; i++)
        word_bases[i] = word_bases[i - 1] * base;
    for (int i = words - 1; i >= 0; i--) {
        double   wb = word_bases[i];
        uint64_t qi;
        if (!(wb <=
              rem)) { // also catches wb being non-finite (NaN compares false)
            qi = 0;
        } else {
            double q = rem / wb;
            qi = (q >= base && i < words - 1) ? ~(uint64_t)0 : (uint64_t)q;
        }
        dst[i] = qi;
        if (qi != 0)
            rem -= (double)qi * wb;
    }
    if (neg)
        negate(dst, words);
    __cccc_bitint_trunc(dst, words, width);
}

// ---------- bitwise ops ----------

void __cccc_bitint_and(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                       int words, int width) {
    for (int i = 0; i < words; i++)
        dst[i] = a[i] & b[i];
    __cccc_bitint_trunc(dst, words, width);
}

void __cccc_bitint_or(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                      int words, int width) {
    for (int i = 0; i < words; i++)
        dst[i] = a[i] | b[i];
    __cccc_bitint_trunc(dst, words, width);
}

void __cccc_bitint_xor(uint64_t *dst, const uint64_t *a, const uint64_t *b,
                       int words, int width) {
    for (int i = 0; i < words; i++)
        dst[i] = a[i] ^ b[i];
    __cccc_bitint_trunc(dst, words, width);
}

void __cccc_bitint_not(uint64_t *dst, const uint64_t *a, int words, int width) {
    for (int i = 0; i < words; i++)
        dst[i] = ~a[i];
    __cccc_bitint_trunc(dst, words, width);
}

void __cccc_bitint_neg(uint64_t *dst, const uint64_t *a, int words, int width) {
    memcpy(dst, a, (size_t)words * 8);
    negate(dst, words);
    __cccc_bitint_trunc(dst, words, width);
}

// Truth test: 1 if any word is non-zero, else 0. Used for boolean contexts
// (if/while/for conditions, !, &&, ||, casts to _Bool), where the value is
// kept in canonical (truncated) form so an OR over the words is sufficient.
int __cccc_bitint_nonzero(const uint64_t *a, int words) {
    uint64_t acc = 0;
    for (int i = 0; i < words; i++)
        acc |= a[i];
    return acc != 0;
}

// Convert base-10/16/2/8 string → wide _BitInt (for wb literals > 64 bits).
// str points to the digit string (no prefix), base is 2/8/10/16.
void __cccc_bitint_from_str(uint64_t *dst, const char *str, int base, int words,
                            int width) {
    memset(dst, 0, (size_t)words * 8);
    for (; *str && *str != '\''; str++) {
        if (*str == '\'')
            continue; // digit separator
        int  digit;
        char c = *str;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            break;
        if (digit >= base)
            break;
        // dst = dst * base + digit
        uint64_t carry = (uint64_t)digit;
        for (int i = 0; i < words; i++) {
            __uint128_t p = (__uint128_t)dst[i] * (uint64_t)base + carry;
            dst[i]        = (uint64_t)p;
            carry         = (uint64_t)(p >> 64);
        }
    }
    __cccc_bitint_trunc(dst, words, width);
}

// ---------- registration ----------

void register_wide_bitint_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "__cccc_bitint_add", (void *)__cccc_bitint_add, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_sub", (void *)__cccc_bitint_sub, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_mul", (void *)__cccc_bitint_mul, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_udiv", (void *)__cccc_bitint_udiv, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_umod", (void *)__cccc_bitint_umod, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_sdiv", (void *)__cccc_bitint_sdiv, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_smod", (void *)__cccc_bitint_smod, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_shl", (void *)__cccc_bitint_shl, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_ushr", (void *)__cccc_bitint_ushr, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_sshr", (void *)__cccc_bitint_sshr, 5,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_cmp", (void *)__cccc_bitint_cmp, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_trunc", (void *)__cccc_bitint_trunc, 3,
                      0);
    cc_register_cfunc(vm, "__cccc_bitint_from_i64",
                      (void *)__cccc_bitint_from_i64, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_from_u64",
                      (void *)__cccc_bitint_from_u64, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_to_i64", (void *)__cccc_bitint_to_i64,
                      4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_to_double",
                      (void *)__cccc_bitint_to_double, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_from_double",
                      (void *)__cccc_bitint_from_double, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_extend", (void *)__cccc_bitint_extend,
                      7, 0);
    cc_register_cfunc(vm, "__cccc_bitint_and", (void *)__cccc_bitint_and, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_or", (void *)__cccc_bitint_or, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_xor", (void *)__cccc_bitint_xor, 5, 0);
    cc_register_cfunc(vm, "__cccc_bitint_not", (void *)__cccc_bitint_not, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_neg", (void *)__cccc_bitint_neg, 4, 0);
    cc_register_cfunc(vm, "__cccc_bitint_nonzero",
                      (void *)__cccc_bitint_nonzero, 2, 0);
    cc_register_cfunc(vm, "__cccc_bitint_from_str",
                      (void *)__cccc_bitint_from_str, 5, 0);
}
