// _Decimal32/64/128 runtime shim (tracker #402): real IEEE-754-2008 decimal
// arithmetic via the Intel BID library, when built with CCCC_HAS_DECIMAL=1.
// Built without that flag, every entry point here raises a clean runtime
// error rather than silently falling back to binary-float aliasing --
// declarations, sizeof, struct layout etc. still work in that build; only
// literals and arithmetic require the library.
//
// All entry points take/return raw byte pointers so no BID type needs to
// appear in a VM header; `w` is the width code 0=_Decimal32, 1=_Decimal64,
// 2=_Decimal128. Called directly from src/ops.c's DADD/DSUB/... handlers
// (not FFI-registered -- this is an internal shim, not a stdlib module).
//
// Performance placeholder: every operation below is an out-of-line call
// into libbid. No inline fast paths, no BID-value caching in registers, no
// fusion (e.g. no DMADD). Acceptable for phase 1; see the #402 follow-up
// ticket for measurement-driven optimization.

#include "../internal.h"
#include <string.h>

#ifdef CCCC_HAS_DECIMAL
#include "bid_conf.h" // must precede bid_functions.h (Intel's own contract)
#include "bid_functions.h"

typedef struct { uint8_t bytes[4]; }  Dec32Raw;
typedef struct { uint8_t bytes[8]; }  Dec64Raw;
typedef struct { uint8_t bytes[16]; } Dec128Raw;

static BID_UINT32  load32(const void *p)  { BID_UINT32 v;  memcpy(&v, p, 4);  return v; }
static BID_UINT64  load64(const void *p)  { BID_UINT64 v;  memcpy(&v, p, 8);  return v; }
static BID_UINT128 load128(const void *p) { BID_UINT128 v; memcpy(&v, p, 16); return v; }
static void store32(void *dst, BID_UINT32 v)   { memcpy(dst, &v, 4); }
static void store64(void *dst, BID_UINT64 v)   { memcpy(dst, &v, 8); }
static void store128(void *dst, BID_UINT128 v) { memcpy(dst, &v, 16); }

#define RND BID_ROUNDING_TO_NEAREST

bool cccc_dec_binop(int op, int w, void *dst, const void *a, const void *b) {
    unsigned f = 0;
    switch (w) {
    case 0: {
        BID_UINT32 x = load32(a), y = load32(b), r;
        switch (op) {
        case '+': r = __bid32_add(x, y, RND, &f); break;
        case '-': r = __bid32_sub(x, y, RND, &f); break;
        case '*': r = __bid32_mul(x, y, RND, &f); break;
        case '/': r = __bid32_div(x, y, RND, &f); break;
        default: return false;
        }
        store32(dst, r);
        return true;
    }
    case 1: {
        BID_UINT64 x = load64(a), y = load64(b), r;
        switch (op) {
        case '+': r = __bid64_add(x, y, RND, &f); break;
        case '-': r = __bid64_sub(x, y, RND, &f); break;
        case '*': r = __bid64_mul(x, y, RND, &f); break;
        case '/': r = __bid64_div(x, y, RND, &f); break;
        default: return false;
        }
        store64(dst, r);
        return true;
    }
    case 2: {
        BID_UINT128 x = load128(a), y = load128(b), r;
        switch (op) {
        case '+': r = __bid128_add(x, y, RND, &f); break;
        case '-': r = __bid128_sub(x, y, RND, &f); break;
        case '*': r = __bid128_mul(x, y, RND, &f); break;
        case '/': r = __bid128_div(x, y, RND, &f); break;
        default: return false;
        }
        store128(dst, r);
        return true;
    }
    default: return false;
    }
}

bool cccc_dec_neg(int w, void *dst, const void *a) {
    // bid{32,64,128}_negate take no rounding-mode/exception-flags params --
    // negation is exact and cannot raise (only DECIMAL_CALL_BY_REFERENCE and
    // the unrelated _EXC_MASKS_PARAM/_EXC_INFO_PARAM group affect the
    // signature, and neither applies with the flags this shim builds with).
    switch (w) {
    case 0: store32(dst, __bid32_negate(load32(a))); return true;
    case 1: store64(dst, __bid64_negate(load64(a))); return true;
    case 2: store128(dst, __bid128_negate(load128(a))); return true;
    default: return false;
    }
}

// 0=EQ, 1=LT, 2=GT, 3=UNORDERED (matches DCMP's opcode contract).
int cccc_dec_cmp(int w, const void *a, const void *b) {
    unsigned f = 0;
    switch (w) {
    case 0: {
        BID_UINT32 x = load32(a), y = load32(b);
        if (__bid32_quiet_equal(x, y, &f)) return 0;
        if (__bid32_quiet_less(x, y, &f)) return 1;
        if (__bid32_quiet_greater(x, y, &f)) return 2;
        return 3;
    }
    case 1: {
        BID_UINT64 x = load64(a), y = load64(b);
        if (__bid64_quiet_equal(x, y, &f)) return 0;
        if (__bid64_quiet_less(x, y, &f)) return 1;
        if (__bid64_quiet_greater(x, y, &f)) return 2;
        return 3;
    }
    case 2: {
        BID_UINT128 x = load128(a), y = load128(b);
        if (__bid128_quiet_equal(x, y, &f)) return 0;
        if (__bid128_quiet_less(x, y, &f)) return 1;
        if (__bid128_quiet_greater(x, y, &f)) return 2;
        return 3;
    }
    default: return 3;
    }
}

bool cccc_dec_from_int(int w, void *dst, long long v, bool is_unsigned) {
    unsigned f = 0;
    switch (w) {
    // bid32/64 from int64 can lose precision (7/16 significant digits vs.
    // up to 19), so they take a rounding mode; bid128's 34 digits always
    // fit a 64-bit integer exactly, so it takes none.
    case 0:
        store32(dst, is_unsigned ? __bid32_from_uint64((uint64_t)v, RND, &f)
                                  : __bid32_from_int64(v, RND, &f));
        return true;
    case 1:
        store64(dst, is_unsigned ? __bid64_from_uint64((uint64_t)v, RND, &f)
                                  : __bid64_from_int64(v, RND, &f));
        return true;
    case 2:
        store128(dst, is_unsigned ? __bid128_from_uint64((uint64_t)v)
                                   : __bid128_from_int64(v));
        return true;
    default: return false;
    }
}

bool cccc_dec_to_int(int w, const void *src, long long *out, bool is_unsigned) {
    unsigned f = 0;
    switch (w) {
    case 0: {
        BID_UINT32 x = load32(src);
        *out = is_unsigned ? (long long)__bid32_to_uint64_int(x, &f)
                            : __bid32_to_int64_int(x, &f);
        return true;
    }
    case 1: {
        BID_UINT64 x = load64(src);
        *out = is_unsigned ? (long long)__bid64_to_uint64_int(x, &f)
                            : __bid64_to_int64_int(x, &f);
        return true;
    }
    case 2: {
        BID_UINT128 x = load128(src);
        *out = is_unsigned ? (long long)__bid128_to_uint64_int(x, &f)
                            : __bid128_to_int64_int(x, &f);
        return true;
    }
    default: return false;
    }
}

bool cccc_dec_from_bin(int w, void *dst, uint64_t bits, bool src_is_f32) {
    unsigned f = 0;
    double d;
    if (src_is_f32) {
        uint32_t b32 = (uint32_t)bits;
        float fv; memcpy(&fv, &b32, 4);
        d = (double)fv;
    } else {
        memcpy(&d, &bits, 8);
    }
    switch (w) {
    case 0: store32(dst, __binary64_to_bid32(d, RND, &f)); return true;
    case 1: store64(dst, __binary64_to_bid64(d, RND, &f)); return true;
    case 2: store128(dst, __binary64_to_bid128(d, RND, &f)); return true;
    default: return false;
    }
}

bool cccc_dec_to_bin(int w, const void *src, bool dst_is_f32, uint64_t *out_bits) {
    unsigned f = 0;
    double d;
    switch (w) {
    case 0: d = __bid32_to_binary64(load32(src), RND, &f); break;
    case 1: d = __bid64_to_binary64(load64(src), RND, &f); break;
    case 2: d = __bid128_to_binary64(load128(src), RND, &f); break;
    default: return false;
    }
    if (dst_is_f32) {
        float fv = (float)d;
        uint32_t b32; memcpy(&b32, &fv, 4);
        *out_bits = b32;
    } else {
        memcpy(out_bits, &d, 8);
    }
    return true;
}

bool cccc_dec_convert(int dst_w, int src_w, void *dst, const void *src) {
    unsigned f = 0;
    if (dst_w == src_w) {
        memcpy(dst, src, dst_w == 0 ? 4 : dst_w == 1 ? 8 : 16);
        return true;
    }
    // src -> bid128 -> dst is not exact for narrowing conversions, so go
    // through the direct pairwise entry points instead.
    switch (src_w * 3 + dst_w) {
    case 0*3+1: store64(dst, __bid32_to_bid64(load32(src), &f)); return true;
    case 0*3+2: store128(dst, __bid32_to_bid128(load32(src), &f)); return true;
    case 1*3+0: store32(dst, __bid64_to_bid32(load64(src), RND, &f)); return true;
    case 1*3+2: store128(dst, __bid64_to_bid128(load64(src), &f)); return true;
    case 2*3+0: store32(dst, __bid128_to_bid32(load128(src), RND, &f)); return true;
    case 2*3+1: store64(dst, __bid128_to_bid64(load128(src), RND, &f)); return true;
    default: return false;
    }
}

// Intel's __bidNN_to_string emits canonical form ("+33E-1"); C wants "3.3".
// Repositioning the decimal point is exact string manipulation -- no
// arithmetic -- so this can't introduce rounding error of its own.
static void reposition_point(char *out, size_t n, const char *sign,
                             const char *digits, long long exp) {
    size_t ndigits = strlen(digits);
    if (exp >= 0) {
        // digits followed by `exp` zeros, no fractional part.
        size_t pos = 0;
        for (const char *s = sign; *s && pos + 1 < n; s++) out[pos++] = *s;
        for (const char *s = digits; *s && pos + 1 < n; s++) out[pos++] = *s;
        for (long long i = 0; i < exp && pos + 1 < n; i++) out[pos++] = '0';
        out[pos] = '\0';
        return;
    }
    long long point = (long long)ndigits + exp; // digits before the point
    size_t pos = 0;
    for (const char *s = sign; *s && pos + 1 < n; s++) out[pos++] = *s;
    if (point <= 0) {
        // 0.000ddd form: exactly -point zeros between "0." and the digits.
        if (pos + 1 < n) out[pos++] = '0';
        if (pos + 1 < n) out[pos++] = '.';
        for (long long i = 0; i < -point && pos + 1 < n; i++) out[pos++] = '0';
        for (const char *s = digits; *s && pos + 1 < n; s++) out[pos++] = *s;
    } else if ((size_t)point >= ndigits) {
        for (const char *s = digits; *s && pos + 1 < n; s++) out[pos++] = *s;
    } else {
        for (long long i = 0; i < point && pos + 1 < n; i++) out[pos++] = digits[i];
        if (pos + 1 < n) out[pos++] = '.';
        for (const char *s = digits + point; *s && pos + 1 < n; s++) out[pos++] = *s;
    }
    out[pos] = '\0';
}

static int format_from_string(char *buf, size_t n, const char *canon) {
    // canon is Intel's own [sign]digits E exp form, e.g. "+33E-1", "-0E+0",
    // "+NaN", "+Inf".
    const char *sign = (canon[0] == '-') ? "-" : "";
    const char *p = canon + 1;
    if (strncasecmp(p, "nan", 3) == 0 || strncasecmp(p, "snan", 4) == 0)
        return snprintf(buf, n, "%snan", sign);
    if (strncasecmp(p, "inf", 3) == 0)
        return snprintf(buf, n, "%sinf", sign);
    char digits[48];
    int i = 0;
    while (p[i] && p[i] != 'E' && i < (int)sizeof(digits) - 1) {
        digits[i] = p[i];
        i++;
    }
    digits[i] = '\0';
    long long exp = 0;
    const char *e = strchr(p, 'E');
    if (e) exp = strtoll(e + 1, NULL, 10);
    // _Decimal128's exponent range (roughly -6176..6144, biased by up to 34
    // coefficient digits) means the fully-expanded non-scientific form can
    // run to several thousand characters (e.g. DEC128_TRUE_MIN) -- a small
    // fixed buffer here would silently truncate long before snprintf's own
    // `n` bound ever comes into play. 8192 covers the worst case with margin.
    char out[8192];
    reposition_point(out, sizeof out, sign, digits, exp);
    return snprintf(buf, n, "%s", out);
}

int cccc_dec_format(char *buf, size_t n, const void *val, int w) {
    unsigned f = 0;
    char canon[64];
    switch (w) {
    case 0: __bid32_to_string(canon, load32(val), &f); break;
    case 1: __bid64_to_string(canon, load64(val), &f); break;
    case 2: __bid128_to_string(canon, load128(val), &f); break;
    default: return -1;
    }
    return format_from_string(buf, n, canon);
}

bool cccc_dec_encode_literal(const char *digits, int w, void *out) {
    unsigned f = 0;
    switch (w) {
    case 0: store32(out, __bid32_from_string((char *)digits, RND, &f)); return true;
    case 1: store64(out, __bid64_from_string((char *)digits, RND, &f)); return true;
    case 2: store128(out, __bid128_from_string((char *)digits, RND, &f)); return true;
    default: return false;
    }
}

#else // !CCCC_HAS_DECIMAL

static bool dec_unsupported(void) { return false; }

bool cccc_dec_binop(int op, int w, void *dst, const void *a, const void *b) {
    (void)op; (void)w; (void)dst; (void)a; (void)b;
    return dec_unsupported();
}
bool cccc_dec_neg(int w, void *dst, const void *a) {
    (void)w; (void)dst; (void)a;
    return dec_unsupported();
}
int cccc_dec_cmp(int w, const void *a, const void *b) {
    (void)w; (void)a; (void)b;
    return 3; // UNORDERED
}
bool cccc_dec_from_int(int w, void *dst, long long v, bool is_unsigned) {
    (void)w; (void)dst; (void)v; (void)is_unsigned;
    return dec_unsupported();
}
bool cccc_dec_to_int(int w, const void *src, long long *out, bool is_unsigned) {
    (void)w; (void)src; (void)is_unsigned;
    if (out) *out = 0;
    return dec_unsupported();
}
bool cccc_dec_from_bin(int w, void *dst, uint64_t bits, bool src_is_f32) {
    (void)w; (void)dst; (void)bits; (void)src_is_f32;
    return dec_unsupported();
}
bool cccc_dec_to_bin(int w, const void *src, bool dst_is_f32, uint64_t *out_bits) {
    (void)w; (void)src; (void)dst_is_f32;
    if (out_bits) *out_bits = 0;
    return dec_unsupported();
}
bool cccc_dec_convert(int dst_w, int src_w, void *dst, const void *src) {
    (void)dst_w; (void)src_w; (void)dst; (void)src;
    return dec_unsupported();
}
int cccc_dec_format(char *buf, size_t n, const void *val, int w) {
    (void)val; (void)w;
    if (buf && n) buf[0] = '\0';
    return dec_unsupported() ? 0 : -1;
}
bool cccc_dec_encode_literal(const char *digits, int w, void *out) {
    (void)digits; (void)w; (void)out;
    return dec_unsupported();
}

#endif // CCCC_HAS_DECIMAL
