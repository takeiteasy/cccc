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
#include <ctype.h>
#include <string.h>

#ifdef CCCC_HAS_DECIMAL
#include "bid_conf.h" // must precede bid_functions.h (Intel's own contract)
#include "bid_functions.h"
#include <fenv.h> // host fenv.h -- #832 rounding-mode/exception-flag plumbing
#include <stdlib.h> // malloc/free (cccc_dec_strtod's unbounded prefix copy)
#include <errno.h> // ERANGE (cccc_dec_strtod, matching strtod/strtof/strtold)

typedef struct { uint8_t bytes[4]; }  Dec32Raw;
typedef struct { uint8_t bytes[8]; }  Dec64Raw;
typedef struct { uint8_t bytes[16]; } Dec128Raw;

static BID_UINT32  load32(const void *p)  { BID_UINT32 v;  memcpy(&v, p, 4);  return v; }
static BID_UINT64  load64(const void *p)  { BID_UINT64 v;  memcpy(&v, p, 8);  return v; }
static BID_UINT128 load128(const void *p) { BID_UINT128 v; memcpy(&v, p, 16); return v; }
static void store32(void *dst, BID_UINT32 v)   { memcpy(dst, &v, 4); }
static void store64(void *dst, BID_UINT64 v)   { memcpy(dst, &v, 8); }
static void store128(void *dst, BID_UINT128 v) { memcpy(dst, &v, 16); }

// #832: fesetround() now has real effect on decimal arithmetic, and BID's
// exception flags now feed fetestexcept(). Every entry point that can round
// or raise takes a trailing `env` parameter (CCCC_DEC_ENV_DYNAMIC or
// _STATIC, src/internal.h): DYNAMIC translates the *host's current*
// fegetround() into a BID_ROUNDING_* mode and raises the resulting BID
// exception flags via feraiseexcept() -- used by every runtime (VM opcode /
// strtod / scanf) call site. STATIC always rounds to-nearest and discards
// flags -- used only by the compile-time constant folder (src/parse.c's
// eval_decimal), which runs inside the *compiler* process and must never
// observe or perturb the host FP environment (see eval_decimal's fenv
// barrier). cccc_dec_neg/cccc_dec_cmp take no such parameter: negation is
// exact and the quiet comparisons cannot raise (see their own comments).
//
// Perf note: cccc_dec_host_rounding()/cccc_dec_raise_flags() now run on
// every dynamic-env decimal op, and FE_INEXACT is set by most divisions, so
// the feraiseexcept() call fires on the common path. Placeholder
// performance, same policy as the rest of decimal FP (#831/#833's
// follow-ups); no fast path here yet.
static int cccc_dec_host_rounding(void) {
    switch (fegetround()) {
    case FE_DOWNWARD:   return BID_ROUNDING_DOWN;
    case FE_UPWARD:     return BID_ROUNDING_UP;
    case FE_TOWARDZERO: return BID_ROUNDING_TO_ZERO;
    case FE_TONEAREST:
    default:            return BID_ROUNDING_TO_NEAREST;
    }
}

// BID's exception bits (DEC_FE_INVALID/DIVBYZERO/OVERFLOW/UNDERFLOW/INEXACT)
// share their bit positions and meaning with the real host FE_* macros on
// every platform CCCC targets (verified: bid_functions.h's DEC_FE_* comment
// block and each platform's <fenv.h> agree on INVALID=1, DIVBYZERO=4,
// OVERFLOW=8, UNDERFLOW=0x10, INEXACT=0x20) -- only DEC_FE_UNNORMAL (0x02,
// denormal) has no portable FE_* equivalent, so it's masked out rather than
// raised as some unrelated host flag.
static void cccc_dec_raise_flags(unsigned f) {
    if (!f) return; // feraiseexcept() is not free -- skip the common case
    int host = 0;
    if (f & DEC_FE_INVALID)   host |= FE_INVALID;
    if (f & DEC_FE_DIVBYZERO) host |= FE_DIVBYZERO;
    if (f & DEC_FE_OVERFLOW)  host |= FE_OVERFLOW;
    if (f & DEC_FE_UNDERFLOW) host |= FE_UNDERFLOW;
    if (f & DEC_FE_INEXACT)   host |= FE_INEXACT;
    if (host) feraiseexcept(host);
}

static int env_round(int env) {
    return env == CCCC_DEC_ENV_DYNAMIC ? cccc_dec_host_rounding()
                                        : BID_ROUNDING_TO_NEAREST;
}
static void env_raise(int env, unsigned f) {
    if (env == CCCC_DEC_ENV_DYNAMIC) cccc_dec_raise_flags(f);
}

#define RND env_round(env)

bool cccc_dec_binop(int op, int w, void *dst, const void *a, const void *b, int env) {
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
        env_raise(env, f);
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
        env_raise(env, f);
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
        env_raise(env, f);
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

bool cccc_dec_from_int(int w, void *dst, long long v, bool is_unsigned, int env) {
    unsigned f = 0;
    switch (w) {
    // bid32/64 from int64 can lose precision (7/16 significant digits vs.
    // up to 19), so they take a rounding mode; bid128's 34 digits always
    // fit a 64-bit integer exactly, so it takes none.
    case 0:
        store32(dst, is_unsigned ? __bid32_from_uint64((uint64_t)v, RND, &f)
                                  : __bid32_from_int64(v, RND, &f));
        env_raise(env, f);
        return true;
    case 1:
        store64(dst, is_unsigned ? __bid64_from_uint64((uint64_t)v, RND, &f)
                                  : __bid64_from_int64(v, RND, &f));
        env_raise(env, f);
        return true;
    case 2:
        store128(dst, is_unsigned ? __bid128_from_uint64((uint64_t)v)
                                   : __bid128_from_int64(v));
        return true;
    default: return false;
    }
}

bool cccc_dec_to_int(int w, const void *src, long long *out, bool is_unsigned, int env) {
    unsigned f = 0;
    switch (w) {
    case 0: {
        BID_UINT32 x = load32(src);
        *out = is_unsigned ? (long long)__bid32_to_uint64_int(x, &f)
                            : __bid32_to_int64_int(x, &f);
        env_raise(env, f);
        return true;
    }
    case 1: {
        BID_UINT64 x = load64(src);
        *out = is_unsigned ? (long long)__bid64_to_uint64_int(x, &f)
                            : __bid64_to_int64_int(x, &f);
        env_raise(env, f);
        return true;
    }
    case 2: {
        BID_UINT128 x = load128(src);
        *out = is_unsigned ? (long long)__bid128_to_uint64_int(x, &f)
                            : __bid128_to_int64_int(x, &f);
        env_raise(env, f);
        return true;
    }
    default: return false;
    }
}

bool cccc_dec_from_bin(int w, void *dst, uint64_t bits, bool src_is_f32, int env) {
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
    case 0: store32(dst, __binary64_to_bid32(d, RND, &f)); env_raise(env, f); return true;
    case 1: store64(dst, __binary64_to_bid64(d, RND, &f)); env_raise(env, f); return true;
    case 2: store128(dst, __binary64_to_bid128(d, RND, &f)); env_raise(env, f); return true;
    default: return false;
    }
}

bool cccc_dec_to_bin(int w, const void *src, bool dst_is_f32, uint64_t *out_bits, int env) {
    unsigned f = 0;
    double d;
    switch (w) {
    case 0: d = __bid32_to_binary64(load32(src), RND, &f); break;
    case 1: d = __bid64_to_binary64(load64(src), RND, &f); break;
    case 2: d = __bid128_to_binary64(load128(src), RND, &f); break;
    default: return false;
    }
    env_raise(env, f);
    if (dst_is_f32) {
        float fv = (float)d;
        uint32_t b32; memcpy(&b32, &fv, 4);
        *out_bits = b32;
    } else {
        memcpy(out_bits, &d, 8);
    }
    return true;
}

bool cccc_dec_convert(int dst_w, int src_w, void *dst, const void *src, int env) {
    unsigned f = 0;
    if (dst_w == src_w) {
        memcpy(dst, src, dst_w == 0 ? 4 : dst_w == 1 ? 8 : 16);
        return true;
    }
    // src -> bid128 -> dst is not exact for narrowing conversions, so go
    // through the direct pairwise entry points instead.
    switch (src_w * 3 + dst_w) {
    case 0*3+1: store64(dst, __bid32_to_bid64(load32(src), &f)); env_raise(env, f); return true;
    case 0*3+2: store128(dst, __bid32_to_bid128(load32(src), &f)); env_raise(env, f); return true;
    case 1*3+0: store32(dst, __bid64_to_bid32(load64(src), RND, &f)); env_raise(env, f); return true;
    case 1*3+2: store128(dst, __bid64_to_bid128(load64(src), &f)); env_raise(env, f); return true;
    case 2*3+0: store32(dst, __bid128_to_bid32(load128(src), RND, &f)); env_raise(env, f); return true;
    case 2*3+1: store64(dst, __bid128_to_bid64(load128(src), RND, &f)); env_raise(env, f); return true;
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

// Compile-time literal encoding only (called from src/codegen.c and
// src/parse.c's write_gvar_data): always round-to-nearest, flags discarded --
// there is no `env` parameter to thread, and a translation-time literal has
// no dynamic rounding mode to honour in the first place.
bool cccc_dec_encode_literal(const char *digits, int w, void *out) {
    unsigned f = 0;
    switch (w) {
    case 0: store32(out, __bid32_from_string((char *)digits, BID_ROUNDING_TO_NEAREST, &f)); return true;
    case 1: store64(out, __bid64_from_string((char *)digits, BID_ROUNDING_TO_NEAREST, &f)); return true;
    case 2: store128(out, __bid128_from_string((char *)digits, BID_ROUNDING_TO_NEAREST, &f)); return true;
    default: return false;
    }
}

// #829: parse a decimal literal out of a NUL-terminated string (scanf's
// %Hf/%Df/%DDf). Same entry points __bidNN_from_string uses for compile-time
// literal encoding (cccc_dec_encode_literal above) -- BID does the rounding,
// so this is exact per IEEE 754-2008 rather than round-tripping through a
// binary double the way a naive strtod-based implementation would. #832:
// gains the `env` parameter like every other runtime-reachable entry point
// (scanf always passes CCCC_DEC_ENV_DYNAMIC).
bool cccc_dec_from_string(int w, void *dst, const char *s, int env) {
    unsigned f = 0;
    switch (w) {
    case 0: store32(dst, __bid32_from_string((char *)s, RND, &f)); env_raise(env, f); return true;
    case 1: store64(dst, __bid64_from_string((char *)s, RND, &f)); env_raise(env, f); return true;
    case 2: store128(dst, __bid128_from_string((char *)s, RND, &f)); env_raise(env, f); return true;
    default: return false;
    }
}

// #832: strtod32/64/128's runtime entry point. BID's __bidNN_from_string
// gives no endptr, so the longest valid numeric prefix must be scanned by
// hand first -- mirrors src/stdlib/format_scanf.c's scan_float_value_raw
// grammar (sign, digits, '.', exponent, inf/infinity/nan(...)), but does NOT
// inherit that function's fixed 256-byte buffer: a legal >255-digit input
// would truncate there, and truncating coefficient digits without adjusting
// the exponent silently changes the parsed value. This scans the prefix
// length first and allocates exactly enough (falling back to a small stack
// buffer for the common short-token case).
static size_t dec_strtod_scan_prefix(const char *s) {
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
           s[i] == '\v' || s[i] == '\f' || s[i] == '\r')
        i++;
    size_t start = i;
    if (s[i] == '+' || s[i] == '-') i++;

    // inf/infinity
    if ((s[i] == 'i' || s[i] == 'I')) {
        size_t save = i;
        static const char *const words[] = {"infinity", "inf"};
        for (int w = 0; w < 2; w++) {
            size_t len = strlen(words[w]);
            size_t j;
            for (j = 0; j < len; j++)
                if (tolower((unsigned char)s[i + j]) != words[w][j]) break;
            if (j == len) return i + len - start;
        }
        i = save;
    }
    // nan or nan(...)
    if ((s[i] == 'n' || s[i] == 'N') &&
        tolower((unsigned char)s[i + 1]) == 'a' &&
        tolower((unsigned char)s[i + 2]) == 'n') {
        i += 3;
        if (s[i] == '(') {
            size_t j = i + 1;
            while (s[j] && s[j] != ')' &&
                   (isalnum((unsigned char)s[j]) || s[j] == '_'))
                j++;
            if (s[j] == ')') i = j + 1;
        }
        return i - start;
    }

    size_t digits_start = i;
    while (isdigit((unsigned char)s[i])) i++;
    bool has_int_digits = i > digits_start;
    bool has_frac_digits = false;
    if (s[i] == '.') {
        i++;
        size_t frac_start = i;
        while (isdigit((unsigned char)s[i])) i++;
        has_frac_digits = i > frac_start;
    }
    if (!has_int_digits && !has_frac_digits)
        return 0; // no valid numeric prefix at all
    if (s[i] == 'e' || s[i] == 'E') {
        size_t save = i;
        i++;
        if (s[i] == '+' || s[i] == '-') i++;
        size_t exp_start = i;
        while (isdigit((unsigned char)s[i])) i++;
        if (i == exp_start) i = save; // no exponent digits: back off
    }
    return i - start;
}

bool cccc_dec_strtod(int w, void *dst, const char *s, char **endptr, int env) {
    if (!s) {
        if (endptr) *endptr = NULL;
        return false;
    }
    size_t lead_ws = 0;
    while (s[lead_ws] == ' ' || s[lead_ws] == '\t' || s[lead_ws] == '\n' ||
           s[lead_ws] == '\v' || s[lead_ws] == '\f' || s[lead_ws] == '\r')
        lead_ws++;
    size_t toklen = dec_strtod_scan_prefix(s);
    if (toklen == 0) {
        // No valid conversion: store +0, endptr unchanged from `s` (C's
        // strtod() contract), no exception raised.
        unsigned zf = 0;
        switch (w) {
        case 0: store32(dst, __bid32_from_string((char *)"0", BID_ROUNDING_TO_NEAREST, &zf)); break;
        case 1: store64(dst, __bid64_from_string((char *)"0", BID_ROUNDING_TO_NEAREST, &zf)); break;
        case 2: store128(dst, __bid128_from_string((char *)"0", BID_ROUNDING_TO_NEAREST, &zf)); break;
        default: if (endptr) *endptr = (char *)s; return false;
        }
        if (endptr) *endptr = (char *)s;
        return true;
    }
    size_t copy_len = lead_ws + toklen;
    char stackbuf[128];
    char *buf = stackbuf;
    bool heap = false;
    if (copy_len + 1 > sizeof stackbuf) {
        buf = malloc(copy_len + 1);
        if (!buf) { if (endptr) *endptr = (char *)s; return false; }
        heap = true;
    }
    memcpy(buf, s, copy_len);
    buf[copy_len] = '\0';

    unsigned f = 0;
    bool ok = true;
    switch (w) {
    case 0: store32(dst, __bid32_from_string(buf, RND, &f)); break;
    case 1: store64(dst, __bid64_from_string(buf, RND, &f)); break;
    case 2: store128(dst, __bid128_from_string(buf, RND, &f)); break;
    default: ok = false; break;
    }
    if (heap) free(buf);
    if (!ok) { if (endptr) *endptr = (char *)s; return false; }
    env_raise(env, f);
    if (f & (DEC_FE_OVERFLOW | DEC_FE_UNDERFLOW))
        errno = ERANGE; // matches strtod()/strtof()/strtold()'s C contract
    if (endptr) *endptr = (char *)s + copy_len;
    return true;
}

// ---------------------------------------------------------------------------
// #829: printf-style formatting (%Hf/%Df/%DDf -- f/F/e/E/g/G, with flags,
// field width, and precision). cccc_dec_format() above only ever produces
// BID's canonical shortest-form string (the __builtin_decimal_to_chars
// contract); this is a separate, fuller renderer built on the same
// [sign]digits*10^exp decomposition.

// Decomposed (sign, coefficient digits, decimal exponent) view of a decimal
// value: value == (is_neg ? -1 : 1) * digits * 10^exp, unless nan/inf.
typedef struct {
    bool is_neg;
    bool is_nan;
    bool is_inf;
    char digits[48];
    int ndigits;
    long long exp;
} DecParts;

static bool dec_decompose(const void *val, int w, DecParts *out) {
    unsigned f = 0;
    char canon[64];
    switch (w) {
    case 0: __bid32_to_string(canon, load32(val), &f); break;
    case 1: __bid64_to_string(canon, load64(val), &f); break;
    case 2: __bid128_to_string(canon, load128(val), &f); break;
    default: return false;
    }
    out->is_neg = (canon[0] == '-');
    const char *p = canon + 1;
    out->is_nan = (strncasecmp(p, "nan", 3) == 0 || strncasecmp(p, "snan", 4) == 0);
    out->is_inf = (strncasecmp(p, "inf", 3) == 0);
    if (out->is_nan || out->is_inf) {
        out->ndigits = 0;
        out->exp = 0;
        out->digits[0] = '\0';
        return true;
    }
    int i = 0;
    while (p[i] && p[i] != 'E' && i < (int)sizeof(out->digits) - 1) {
        out->digits[i] = p[i];
        i++;
    }
    out->digits[i] = '\0';
    out->ndigits = i;
    out->exp = 0;
    const char *e = strchr(p, 'E');
    if (e) out->exp = strtoll(e + 1, NULL, 10);
    return true;
}

// Rounds the `len`-digit coefficient string `digits` (ASCII '0'-'9', no
// sign) to `keep` significant digits using round-half-even -- the IEEE
// 754-2008 default and BID's own default rounding mode, so this stays
// consistent with the arithmetic operators. `*exp` is adjusted so that
// out * 10^(*exp) == the correctly-rounded value. `keep` must be in
// [0, len]; a carry that overflows all kept digits (e.g. "99" -> "100" at
// 1 sig fig) collapses to a single leading '1' with `*exp` bumped an extra
// place, rather than growing the digit count -- callers must not assume the
// returned length is always exactly `keep`. `out` must have room for
// keep+1 bytes (worst case: keep==0 collapsing to "1\0", or a keep-digit
// carry collapsing to "1\0").
static int round_sig(const char *digits, int len, long long *exp, int keep,
                     char *out) {
    if (keep >= len) {
        memcpy(out, digits, (size_t)len);
        out[len] = '\0';
        return len;
    }
    // keep in [0, len-1]: digits[keep] is the first dropped digit.
    char rd = digits[keep];
    bool round_up;
    if (rd > '5') {
        round_up = true;
    } else if (rd < '5') {
        round_up = false;
    } else {
        bool rest_nonzero = false;
        for (int i = keep + 1; i < len; i++) {
            if (digits[i] != '0') { rest_nonzero = true; break; }
        }
        if (rest_nonzero) round_up = true;
        else if (keep == 0) round_up = false; // tie, no prior digit: "0" is even
        else round_up = ((digits[keep - 1] - '0') % 2) != 0;
    }
    *exp += (len - keep);
    if (keep == 0) {
        if (round_up) { out[0] = '1'; out[1] = '\0'; return 1; }
        out[0] = '0'; out[1] = '\0'; *exp = 0; return 1;
    }
    memcpy(out, digits, (size_t)keep);
    out[keep] = '\0';
    if (round_up) {
        int i = keep - 1;
        while (i >= 0 && out[i] == '9') { out[i] = '0'; i--; }
        if (i >= 0) {
            out[i]++;
        } else {
            // All `keep` kept digits were '9' and carried out: "999..9"+1 =
            // "1000..0" (keep+1 digits). Collapse the keep trailing zeros
            // into the exponent instead of storing them, so out becomes the
            // single digit "1" and *exp absorbs all `keep` of the places
            // that would have held those zeros (not just one -- e.g.
            // rounding "9999" (keep=4) up must land on 10000 = "1" at
            // exp+4, not "1" at exp+1).
            out[0] = '1';
            out[1] = '\0';
            *exp += keep;
            return 1;
        }
    }
    return keep;
}

// Renders a rounded (digits,len,exp) coefficient with exactly `frac` digits
// after the decimal point (zero-padded on the right as needed), no sign, no
// field-width padding. `frac` may be 0 (integer-only, point omitted unless
// `force_point`).
static void render_fixed_body(char *out, size_t n, const char *digits, int len,
                              long long exp, int frac, bool force_point) {
    long long pointpos = (long long)len + exp; // digits before the point
    size_t pos = 0;
    if (pointpos <= 0) {
        if (pos + 1 < n) out[pos++] = '0';
    } else {
        long long take = pointpos < len ? pointpos : len;
        for (long long i = 0; i < take && pos + 1 < n; i++) out[pos++] = digits[i];
        for (long long i = take; i < pointpos && pos + 1 < n; i++) out[pos++] = '0';
    }
    if (frac > 0 || force_point) {
        if (pos + 1 < n) out[pos++] = '.';
    }
    for (int i = 0; i < frac && pos + 1 < n; i++) {
        long long src_idx = pointpos + i; // index into `digits` (may be <0 or >=len)
        char c = (src_idx >= 0 && src_idx < len) ? digits[src_idx] : '0';
        out[pos++] = c;
    }
    out[pos] = '\0';
}

// Renders a rounded (digits,len,exp) coefficient in scientific form
// "d[.ddd]e±dd" (>=2 exponent digits, C's minimum), no sign, no field-width
// padding.
static void render_sci_body(char *out, size_t n, const char *digits, int len,
                            long long exp, int prec, bool force_point,
                            bool upper) {
    long long e10 = (long long)len + exp - 1; // decimal exponent of digits[0]
    size_t pos = 0;
    if (pos + 1 < n) out[pos++] = len > 0 ? digits[0] : '0';
    if (prec > 0 || force_point) {
        if (pos + 1 < n) out[pos++] = '.';
        for (int i = 0; i < prec && pos + 1 < n; i++) {
            int src_idx = i + 1;
            char c = (src_idx < len) ? digits[src_idx] : '0';
            out[pos++] = c;
        }
    }
    if (pos + 1 < n) out[pos++] = upper ? 'E' : 'e';
    if (pos + 1 < n) out[pos++] = (e10 < 0) ? '-' : '+';
    long long ae = e10 < 0 ? -e10 : e10;
    char ebuf[24];
    int ei = 0;
    if (ae == 0) ebuf[ei++] = '0';
    while (ae > 0 && ei < (int)sizeof(ebuf)) { ebuf[ei++] = (char)('0' + ae % 10); ae /= 10; }
    while (ei < 2) ebuf[ei++] = '0'; // C requires >=2 exponent digits
    for (int i = ei - 1; i >= 0 && pos + 1 < n; i--) out[pos++] = ebuf[i];
    out[pos] = '\0';
}

// Strips trailing fractional zeros (and a bare trailing '.') from a rendered
// numeric body in place -- %g's "no trailing zeros unless #" rule.
static void strip_trailing_frac_zeros(char *body) {
    char *dot = strchr(body, '.');
    if (!dot) return;
    char *end = body + strlen(body);
    // Don't eat into an exponent suffix if present.
    char *e = strpbrk(dot, "eE");
    char *stop = e ? e : end;
    char *p = stop;
    while (p > dot + 1 && p[-1] == '0') p--;
    if (p == dot + 1) p = dot; // no fractional digits left: drop the point too
    if (p < stop) memmove(p, stop, (size_t)(end - stop) + 1);
}

int cccc_dec_format_ex(char *buf, size_t n, const void *val, int w, int conv,
                       unsigned flags, int field_width, int prec) {
    DecParts parts;
    if (!dec_decompose(val, w, &parts)) return -1;

    bool upper = (conv == 'F' || conv == 'E' || conv == 'G');
    char body[8320]; // matches format_from_string's DEC128_TRUE_MIN margin
    bool force_point = (flags & CCCC_DECFMT_ALT) != 0;

    if (parts.is_nan || parts.is_inf) {
        const char *word = parts.is_nan ? "nan" : "inf";
        char nb[8];
        snprintf(nb, sizeof nb, "%s", word);
        if (upper) for (char *c = nb; *c; c++) *c = (char)toupper((unsigned char)*c);
        snprintf(body, sizeof body, "%s", nb);
    } else {
        char conv_lower = (char)tolower(conv);
        if (conv_lower == 'f') {
            int p = prec < 0 ? 6 : prec;
            long long pointpos = (long long)parts.ndigits + parts.exp;
            long long keep = pointpos + p;
            char rdigits[64];
            int rlen;
            long long rexp;
            if (keep < 0) {
                rdigits[0] = '0'; rdigits[1] = '\0'; rlen = 1; rexp = 0;
            } else {
                int keep_i = keep > (long long)parts.ndigits ? parts.ndigits : (int)keep;
                rexp = parts.exp;
                rlen = round_sig(parts.digits, parts.ndigits, &rexp, keep_i, rdigits);
            }
            render_fixed_body(body, sizeof body, rdigits, rlen, rexp, p, force_point);
        } else if (conv_lower == 'e') {
            int p = prec < 0 ? 6 : prec;
            char rdigits[64];
            long long rexp = parts.exp;
            int rlen = round_sig(parts.digits, parts.ndigits, &rexp, p + 1, rdigits);
            render_sci_body(body, sizeof body, rdigits, rlen, rexp, p, force_point, upper);
        } else { // 'g'
            int P = prec < 0 ? 6 : (prec == 0 ? 1 : prec);
            char rdigits[64];
            long long rexp = parts.exp;
            int rlen = round_sig(parts.digits, parts.ndigits, &rexp, P, rdigits);
            long long X = (long long)rlen + rexp - 1; // decimal exponent, post-rounding
            if (X >= -4 && X < P) {
                int frac = (int)(P - 1 - X);
                if (frac < 0) frac = 0;
                render_fixed_body(body, sizeof body, rdigits, rlen, rexp, frac, force_point);
            } else {
                render_sci_body(body, sizeof body, rdigits, rlen, rexp, P - 1, force_point, upper);
            }
            if (!force_point) strip_trailing_frac_zeros(body);
        }
        if (upper) for (char *c = body; *c; c++) *c = (char)toupper((unsigned char)*c);
    }

    // Sign.
    char sign = 0;
    if (parts.is_neg) sign = '-';
    else if (flags & CCCC_DECFMT_PLUS) sign = '+';
    else if (flags & CCCC_DECFMT_SPACE) sign = ' ';

    char numeric[8330];
    size_t npos = 0;
    if (sign) numeric[npos++] = sign;
    snprintf(numeric + npos, sizeof numeric - npos, "%s", body);

    int len = (int)strlen(numeric);
    int pad = field_width > len ? field_width - len : 0;
    bool left = (flags & CCCC_DECFMT_MINUS) != 0;
    // Zero-padding never applies to nan/inf, and never combines with '-'.
    bool zero_pad = (flags & CCCC_DECFMT_ZERO) != 0 && !left &&
                    !parts.is_nan && !parts.is_inf;

    size_t pos = 0;
    if (!left && !zero_pad) {
        for (int i = 0; i < pad && pos + 1 < n; i++) { if (buf) buf[pos] = ' '; pos++; }
    }
    if (zero_pad && sign) { if (pos + 1 < n && buf) buf[pos] = sign; pos++; }
    if (zero_pad) {
        for (int i = 0; i < pad && pos + 1 < n; i++) { if (buf) buf[pos] = '0'; pos++; }
    }
    const char *rest = (zero_pad && sign) ? numeric + 1 : numeric;
    for (const char *s = rest; *s && pos + 1 < n; s++) { if (buf) buf[pos] = *s; pos++; }
    if (left) {
        for (int i = 0; i < pad && pos + 1 < n; i++) { if (buf) buf[pos] = ' '; pos++; }
    }
    if (buf && n) buf[pos < n ? pos : n - 1] = '\0';

    // Return the length that *would* have been written, snprintf-style,
    // matching cccc_dec_format()'s contract.
    int total = len + pad;
    return total;
}

#else // !CCCC_HAS_DECIMAL

static bool dec_unsupported(void) { return false; }

bool cccc_dec_binop(int op, int w, void *dst, const void *a, const void *b, int env) {
    (void)op; (void)w; (void)dst; (void)a; (void)b; (void)env;
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
bool cccc_dec_from_int(int w, void *dst, long long v, bool is_unsigned, int env) {
    (void)w; (void)dst; (void)v; (void)is_unsigned; (void)env;
    return dec_unsupported();
}
bool cccc_dec_to_int(int w, const void *src, long long *out, bool is_unsigned, int env) {
    (void)w; (void)src; (void)is_unsigned; (void)env;
    if (out) *out = 0;
    return dec_unsupported();
}
bool cccc_dec_from_bin(int w, void *dst, uint64_t bits, bool src_is_f32, int env) {
    (void)w; (void)dst; (void)bits; (void)src_is_f32; (void)env;
    return dec_unsupported();
}
bool cccc_dec_to_bin(int w, const void *src, bool dst_is_f32, uint64_t *out_bits, int env) {
    (void)w; (void)src; (void)dst_is_f32; (void)env;
    if (out_bits) *out_bits = 0;
    return dec_unsupported();
}
bool cccc_dec_convert(int dst_w, int src_w, void *dst, const void *src, int env) {
    (void)dst_w; (void)src_w; (void)dst; (void)src; (void)env;
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
bool cccc_dec_from_string(int w, void *dst, const char *s, int env) {
    (void)w; (void)dst; (void)s; (void)env;
    return dec_unsupported();
}
bool cccc_dec_strtod(int w, void *dst, const char *s, char **endptr, int env) {
    (void)w; (void)dst; (void)s; (void)env;
    if (endptr) *endptr = (char *)s;
    return dec_unsupported();
}
int cccc_dec_format_ex(char *buf, size_t n, const void *val, int w, int conv,
                       unsigned flags, int field_width, int prec) {
    (void)val; (void)w; (void)conv; (void)flags; (void)field_width; (void)prec;
    if (buf && n) buf[0] = '\0';
    return dec_unsupported() ? 0 : -1;
}

#endif // CCCC_HAS_DECIMAL
