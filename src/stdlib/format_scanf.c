// scanf-family core engine for platforms whose host libc lacks the C23
// %b/%B (binary integer) conversion specifier (macOS, glibc < 2.35).
// stb_sprintf has no scanf equivalent, so this is a from-scratch
// implementation of the conversions specified by C11/C23
// (d i u o x X b B f e g a F E G A s c [ p n %), used by all scanf-family
// wrappers so behavior is identical across fscanf/sscanf/scanf and their
// v-variants. See #394.
//
// Known limitations (acceptable for this ticket's scope):
//  - %ls/%lc/%l[ (wide-character variants) are treated like their narrow
//    counterparts; wchar_t destinations are not supported.
//  - Field-width accounting does not apply to an optional "0x"/"0b" prefix
//    on %i/%x/%X/%b/%B (the prefix is consumed in addition to `width`
//    digit characters). This matches common practice and only differs from
//    a strict reading of the standard for very small explicit widths.
#include "format.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------
// Input source abstraction: unifies FILE* streams and string buffers with
// a small internal pushback buffer (so we can push back more than the one
// character ungetc() guarantees).
// ---------------------------------------------------------------------

#define CCCC_SCAN_PUSHBACK_MAX 32

typedef struct {
    int (*read)(void *ctx);
    void *ctx;
    int pushback[CCCC_SCAN_PUSHBACK_MAX];
    int pushback_n;
    long consumed;
} ScanSource;

static int sgetc(ScanSource *src) {
    int c;
    if (src->pushback_n > 0)
        c = src->pushback[--src->pushback_n];
    else
        c = src->read(src->ctx);
    if (c != EOF)
        src->consumed++;
    return c;
}

static void sungetc(ScanSource *src, int c) {
    if (c == EOF)
        return;
    src->pushback[src->pushback_n++] = c;
    src->consumed--;
}

static void skip_ws_input(ScanSource *src) {
    for (;;) {
        int c = sgetc(src);
        if (c == EOF)
            return;
        if (!isspace((unsigned char)c)) {
            sungetc(src, c);
            return;
        }
    }
}

// ---------------------------------------------------------------------
// Length modifiers
// ---------------------------------------------------------------------

enum {
    LEN_NONE,
    LEN_hh,
    LEN_h,
    LEN_l,
    LEN_ll,
    LEN_j,
    LEN_z,
    LEN_t,
    LEN_L,
    // #829: _Decimal32/64/128 length modifiers (%Hf/%Df/%DDf), destination
    // is a _Decimal32/64/128* rather than a float/double/long double*, so
    // these are handled by their own branch in the 'f'/'e'/'g'/'a' case
    // below (cccc_dec_from_string), not by store_float.
    LEN_H,
    LEN_D,
    LEN_DD,
};

static void store_int(void *ptr, int lenmod, unsigned long long val) {
    switch (lenmod) {
    case LEN_hh: *(unsigned char *)ptr = (unsigned char)val; break;
    case LEN_h:  *(unsigned short *)ptr = (unsigned short)val; break;
    case LEN_l:  *(unsigned long *)ptr = (unsigned long)val; break;
    case LEN_ll: *(unsigned long long *)ptr = (unsigned long long)val; break;
    case LEN_j:  *(uintmax_t *)ptr = (uintmax_t)val; break;
    case LEN_z:  *(size_t *)ptr = (size_t)val; break;
    case LEN_t:  *(ptrdiff_t *)ptr = (ptrdiff_t)val; break;
    default:     *(unsigned int *)ptr = (unsigned int)val; break;
    }
}

static void store_float(void *ptr, int lenmod, long double val) {
    if (lenmod == LEN_L)
        *(long double *)ptr = val;
    else if (lenmod == LEN_l)
        *(double *)ptr = (double)val;
    else
        *(float *)ptr = (float)val;
}

// ---------------------------------------------------------------------
// Integer conversion (d i u o x X b B)
// ---------------------------------------------------------------------

enum { SCAN_OK = 0, SCAN_NOMATCH = 1, SCAN_EOF = 2 };

static int digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int is_digit_for_base(int c, int base) {
    int v = digit_value(c);
    return v >= 0 && v < base;
}

// Reads an integer token (optional sign, optional base prefix, digits) and
// returns its value via *out (already negated if a '-' sign was present).
// `base` is 0 (auto-detect, for %i), 2, 8, 10 or 16.
static int scan_uint_value(ScanSource *src, int base, int width,
                            unsigned long long *out) {
    skip_ws_input(src);

    int max = width > 0 ? width : INT_MAX;
    int n = 0;
    int sign_char = 0;

    int c = sgetc(src);
    if (c == EOF)
        return SCAN_EOF;
    n++;

    if (c == '+' || c == '-') {
        sign_char = c;
        if (n >= max) {
            sungetc(src, c);
            return SCAN_NOMATCH;
        }
        c = sgetc(src);
        if (c == EOF) {
            sungetc(src, sign_char);
            return SCAN_NOMATCH;
        }
        n++;
    }

    // Optional 0x/0b prefix (consumed in addition to `width`, see header note).
    if (c == '0') {
        int c2 = sgetc(src);
        if (c2 != EOF && (c2 == 'x' || c2 == 'X') && (base == 0 || base == 16)) {
            int c3 = sgetc(src);
            if (c3 != EOF && is_digit_for_base(c3, 16)) {
                base = 16;
                c = c3;
            } else {
                if (c3 != EOF) sungetc(src, c3);
                sungetc(src, c2);
                if (base == 0) base = 8;
                // c stays '0', handled by the digit loop below
            }
        } else if (c2 != EOF && (c2 == 'b' || c2 == 'B') && (base == 0 || base == 2)) {
            int c3 = sgetc(src);
            if (c3 != EOF && (c3 == '0' || c3 == '1')) {
                base = 2;
                c = c3;
            } else {
                if (c3 != EOF) sungetc(src, c3);
                sungetc(src, c2);
                if (base == 0) base = 8;
            }
        } else {
            if (c2 != EOF) sungetc(src, c2);
            if (base == 0) base = 8;
        }
    } else if (base == 0) {
        base = 10;
    }

    char digits[256];
    int dn = 0;
    int remaining = max - (sign_char ? 1 : 0);
    while (c != EOF && dn < remaining && dn < (int)sizeof(digits) - 1 &&
           is_digit_for_base(c, base)) {
        digits[dn++] = (char)c;
        c = sgetc(src);
    }
    if (c != EOF)
        sungetc(src, c);

    if (dn == 0) {
        if (sign_char)
            sungetc(src, sign_char);
        return SCAN_NOMATCH;
    }

    digits[dn] = 0;
    unsigned long long val = strtoull(digits, NULL, base);
    if (sign_char == '-')
        val = 0ULL - val;
    *out = val;
    return SCAN_OK;
}

// ---------------------------------------------------------------------
// Floating-point conversion (f e g a F E G A)
// ---------------------------------------------------------------------

// `raw_out`/`raw_cap`, if raw_out is non-NULL, receive a copy of the exact
// numeric token text (sign, digits, '.', exponent, or an inf/nan spelling)
// alongside the strtold'd *out -- used by the %Hf/%Df/%DDf decimal path
// (#829) so BID can parse the token directly via cccc_dec_from_string
// instead of round-tripping through long double, which would round a value
// like 0.1 to its nearest *binary* approximation before it ever reaches the
// correctly-rounded decimal parser.
static int scan_float_value_raw(ScanSource *src, int width, long double *out,
                                char *raw_out, size_t raw_cap) {
    skip_ws_input(src);

    int max = width > 0 ? width : INT_MAX;
    char buf[256];
    int n = 0;

    int c = sgetc(src);
    if (c == EOF)
        return SCAN_EOF;

    if ((c == '+' || c == '-') && n < max) {
        buf[n++] = (char)c;
        c = sgetc(src);
    }

    // inf/infinity/nan(...) - `c` holds the first letter (not yet committed
    // to buf or pushed back).
    if (c != EOF && (tolower(c) == 'i' || tolower(c) == 'n')) {
        static const char *const words[] = {"infinity", "inf", "nan"};
        for (int w = 0; w < 3; w++) {
            const char *word = words[w];
            int len = (int)strlen(word);
            if (tolower(c) != word[0] || n + len > max)
                continue;

            int peek[16];
            int pn = 0;
            int ok = 1;
            for (int i = 1; i < len; i++) {
                int pc = sgetc(src);
                peek[pn++] = pc;
                if (pc == EOF || tolower(pc) != word[i]) {
                    ok = 0;
                    break;
                }
            }
            if (ok) {
                buf[n++] = (char)c;
                for (int i = 0; i < pn; i++)
                    buf[n++] = (char)peek[i];
                if (w == 2) { // nan(...)
                    int pc = sgetc(src);
                    if (pc == '(') {
                        buf[n++] = (char)pc;
                        int depth = 1;
                        while (depth > 0) {
                            pc = sgetc(src);
                            if (pc == EOF) break;
                            if (pc == '(') depth++;
                            else if (pc == ')') depth--;
                            if (n < (int)sizeof(buf) - 1) buf[n++] = (char)pc;
                        }
                    } else if (pc != EOF) {
                        sungetc(src, pc);
                    }
                }
                buf[n] = 0;
                *out = strtold(buf, NULL);
                if (raw_out) snprintf(raw_out, raw_cap, "%s", buf);
                return SCAN_OK;
            }
            // not a match: push back everything peeked, in reverse order
            for (int i = pn - 1; i >= 0; i--)
                if (peek[i] != EOF) sungetc(src, peek[i]);
        }
    }

    int is_hex = 0;
    if (c == '0' && n < max) {
        buf[n++] = '0';
        int c2 = sgetc(src);
        if (c2 != EOF && (c2 == 'x' || c2 == 'X') && n < max) {
            buf[n++] = (char)c2;
            is_hex = 1;
            c = sgetc(src);
        } else {
            c = c2;
        }
    }

    int any_digit = 0;
    while (c != EOF && n < max && n < (int)sizeof(buf) - 1 &&
           (isdigit((unsigned char)c) || (is_hex && isxdigit((unsigned char)c)))) {
        buf[n++] = (char)c;
        any_digit = 1;
        c = sgetc(src);
    }
    if (c == '.' && n < max && n < (int)sizeof(buf) - 1) {
        buf[n++] = '.';
        c = sgetc(src);
        while (c != EOF && n < max && n < (int)sizeof(buf) - 1 &&
               (isdigit((unsigned char)c) || (is_hex && isxdigit((unsigned char)c)))) {
            buf[n++] = (char)c;
            any_digit = 1;
            c = sgetc(src);
        }
    }

    if (any_digit) {
        int exp_lo = is_hex ? 'p' : 'e';
        int exp_hi = is_hex ? 'P' : 'E';
        if ((c == exp_lo || c == exp_hi) && n < (int)sizeof(buf) - 1) {
            int echar = c;
            int c2 = sgetc(src);
            int sign_c = 0;
            if (c2 == '+' || c2 == '-') {
                sign_c = c2;
                c2 = sgetc(src);
            }
            if (c2 != EOF && isdigit((unsigned char)c2)) {
                buf[n++] = (char)echar;
                if (sign_c) buf[n++] = (char)sign_c;
                while (c2 != EOF && n < max && n < (int)sizeof(buf) - 1 &&
                       isdigit((unsigned char)c2)) {
                    buf[n++] = (char)c2;
                    c2 = sgetc(src);
                }
                c = c2;
            } else {
                if (c2 != EOF) sungetc(src, c2);
                if (sign_c) sungetc(src, sign_c);
                // leave c (the exponent char) to be pushed back below
            }
        }
    }

    if (c != EOF)
        sungetc(src, c);

    if (!any_digit) {
        // No valid number: push back everything we consumed (in buf), in
        // reverse order, so the caller sees the original input again.
        for (int i = n - 1; i >= 0; i--)
            sungetc(src, (unsigned char)buf[i]);
        return SCAN_NOMATCH;
    }

    buf[n] = 0;
    *out = strtold(buf, NULL);
    if (raw_out) snprintf(raw_out, raw_cap, "%s", buf);
    return SCAN_OK;
}

static int scan_float_value(ScanSource *src, int width, long double *out) {
    return scan_float_value_raw(src, width, out, NULL, 0);
}

// ---------------------------------------------------------------------
// %[ scanset]
// ---------------------------------------------------------------------

static int in_scanset(unsigned char c, const char *start, const char *end, int negate) {
    int found = 0;
    const char *p = start;
    while (p < end) {
        if (p + 2 < end && p[1] == '-') {
            if (c >= (unsigned char)p[0] && c <= (unsigned char)p[2]) {
                found = 1;
                break;
            }
            p += 3;
        } else {
            if (c == (unsigned char)*p) {
                found = 1;
                break;
            }
            p++;
        }
    }
    return negate ? !found : found;
}

// ---------------------------------------------------------------------
// Core scan loop
// ---------------------------------------------------------------------

static int cccc_vscan(ScanSource *src, const char *fmt, va_list ap) {
    int result = 0;
    const char *f = fmt;

    while (*f) {
        if (isspace((unsigned char)*f)) {
            while (*f && isspace((unsigned char)*f)) f++;
            for (;;) {
                int c = sgetc(src);
                if (c == EOF) break;
                if (!isspace((unsigned char)c)) {
                    sungetc(src, c);
                    break;
                }
            }
            continue;
        }

        if (*f != '%') {
            int c = sgetc(src);
            if (c == EOF) goto eof_before;
            if (c != (unsigned char)*f) {
                sungetc(src, c);
                goto nomatch;
            }
            f++;
            continue;
        }

        f++; // skip '%'

        if (*f == '%') {
            int c = sgetc(src);
            if (c == EOF) goto eof_before;
            if (c != '%') {
                sungetc(src, c);
                goto nomatch;
            }
            f++;
            continue;
        }

        int suppress = 0;
        if (*f == '*') {
            suppress = 1;
            f++;
        }

        int width = 0;
        while (isdigit((unsigned char)*f)) {
            width = width * 10 + (*f - '0');
            f++;
        }

        int lenmod = LEN_NONE;
        switch (*f) {
        case 'h':
            f++;
            if (*f == 'h') { lenmod = LEN_hh; f++; } else lenmod = LEN_h;
            break;
        case 'l':
            f++;
            if (*f == 'l') { lenmod = LEN_ll; f++; } else lenmod = LEN_l;
            break;
        case 'j': lenmod = LEN_j; f++; break;
        case 'z': lenmod = LEN_z; f++; break;
        case 't': lenmod = LEN_t; f++; break;
        case 'L': lenmod = LEN_L; f++; break;
        case 'H': lenmod = LEN_H; f++; break;
        case 'D':
            f++;
            if (*f == 'D') { lenmod = LEN_DD; f++; } else lenmod = LEN_D;
            break;
        default: break;
        }

        char conv = *f;
        if (!conv)
            break;
        f++;

        switch (conv) {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X':
        case 'b': case 'B': {
            int base;
            switch (conv) {
            case 'o': base = 8; break;
            case 'x': case 'X': base = 16; break;
            case 'b': case 'B': base = 2; break;
            case 'u': case 'd': base = 10; break;
            default: base = 0; break; // 'i'
            }
            unsigned long long val;
            int r = scan_uint_value(src, base, width, &val);
            if (r == SCAN_EOF) goto eof_before;
            if (r == SCAN_NOMATCH) goto nomatch;
            if (!suppress) {
                store_int(va_arg(ap, void *), lenmod, val);
                result++;
            }
            break;
        }

        case 'f': case 'e': case 'g': case 'a':
        case 'F': case 'E': case 'G': case 'A': {
            if (lenmod == LEN_H || lenmod == LEN_D || lenmod == LEN_DD) {
                // #829: decimal destination -- scan the raw token text and
                // hand it to BID directly (cccc_dec_from_string) rather than
                // going through long double, so the result is correctly
                // rounded per IEEE 754-2008 rather than double-rounded
                // through a binary intermediate.
                long double val;
                char raw[256];
                int r = scan_float_value_raw(src, width, &val, raw, sizeof raw);
                if (r == SCAN_EOF) goto eof_before;
                if (r == SCAN_NOMATCH) goto nomatch;
                if (!suppress) {
                    int w = (lenmod == LEN_H) ? 0 : (lenmod == LEN_D) ? 1 : 2;
                    if (cccc_dec_from_string(w, va_arg(ap, void *), raw, CCCC_DEC_ENV_DYNAMIC))
                        result++;
                    else
                        goto nomatch; // CCCC_HAS_DECIMAL not built in
                }
                break;
            }
            long double val;
            int r = scan_float_value(src, width, &val);
            if (r == SCAN_EOF) goto eof_before;
            if (r == SCAN_NOMATCH) goto nomatch;
            if (!suppress) {
                store_float(va_arg(ap, void *), lenmod, val);
                result++;
            }
            break;
        }

        case 'p': {
            unsigned long long val;
            int r = scan_uint_value(src, 16, width, &val);
            if (r == SCAN_EOF) goto eof_before;
            if (r == SCAN_NOMATCH) goto nomatch;
            if (!suppress) {
                *(void **)va_arg(ap, void *) = (void *)(uintptr_t)val;
                result++;
            }
            break;
        }

        case 's': {
            skip_ws_input(src);
            int c = sgetc(src);
            if (c == EOF) goto eof_before;
            int max = width > 0 ? width : INT_MAX;
            char *out = suppress ? NULL : (char *)va_arg(ap, void *);
            int n = 0;
            while (c != EOF && !isspace((unsigned char)c) && n < max) {
                if (out) out[n] = (char)c;
                n++;
                c = sgetc(src);
            }
            if (c != EOF) sungetc(src, c);
            if (out) out[n] = 0;
            if (!suppress) result++;
            break;
        }

        case 'c': {
            int max = width > 0 ? width : 1;
            char *out = suppress ? NULL : (char *)va_arg(ap, void *);
            int n = 0;
            for (; n < max; n++) {
                int c = sgetc(src);
                if (c == EOF) {
                    if (n == 0) goto eof_before;
                    goto nomatch;
                }
                if (out) out[n] = (char)c;
            }
            if (!suppress) result++;
            break;
        }

        case '[': {
            int negate = 0;
            if (*f == '^') { negate = 1; f++; }
            const char *set_start = f;
            if (*f == ']') f++; // literal ']' as first member
            while (*f && *f != ']') f++;
            const char *set_end = f;
            if (*f == ']') f++;

            int max = width > 0 ? width : INT_MAX;
            char *out = suppress ? NULL : (char *)va_arg(ap, void *);
            int n = 0;
            int c = sgetc(src);
            while (c != EOF && n < max && in_scanset((unsigned char)c, set_start, set_end, negate)) {
                if (out) out[n] = (char)c;
                n++;
                c = sgetc(src);
            }
            if (c != EOF) sungetc(src, c);
            if (n == 0) {
                if (c == EOF) goto eof_before;
                goto nomatch;
            }
            if (out) out[n] = 0;
            if (!suppress) result++;
            break;
        }

        case 'n':
            if (!suppress)
                store_int(va_arg(ap, void *), lenmod, (unsigned long long)src->consumed);
            break;

        default:
            goto nomatch;
        }
    }

    return result;

eof_before:
    return (result == 0) ? EOF : result;

nomatch:
    return result;
}

// ---------------------------------------------------------------------
// Source constructors and public entry points
// ---------------------------------------------------------------------

static int read_from_file(void *ctx) {
    return fgetc((FILE *)ctx);
}

typedef struct {
    const char *s;
    size_t pos;
} StrReadCtx;

static int read_from_str(void *ctx) {
    StrReadCtx *sc = (StrReadCtx *)ctx;
    unsigned char c = (unsigned char)sc->s[sc->pos];
    if (c == '\0')
        return EOF;
    sc->pos++;
    return (int)c;
}

int cccc_vfscanf(FILE *stream, const char *fmt, va_list ap) {
    ScanSource src = {0};
    src.read = read_from_file;
    src.ctx = stream;
    int r = cccc_vscan(&src, fmt, ap);
    // Restore any pushed-back characters to the real stream so subsequent
    // reads on it see them again.
    while (src.pushback_n > 0)
        ungetc(src.pushback[--src.pushback_n], stream);
    return r;
}

int cccc_vsscanf(const char *str, const char *fmt, va_list ap) {
    StrReadCtx ctx = { .s = str, .pos = 0 };
    ScanSource src = {0};
    src.read = read_from_str;
    src.ctx = &ctx;
    return cccc_vscan(&src, fmt, ap);
}

int cccc_vscanf(const char *fmt, va_list ap) {
    return cccc_vfscanf(stdin, fmt, ap);
}

int cccc_scanf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cccc_vscanf(fmt, ap);
    va_end(ap);
    return r;
}

int cccc_fscanf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cccc_vfscanf(stream, fmt, ap);
    va_end(ap);
    return r;
}

int cccc_sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cccc_vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

// V* variant wrappers — extract pointer args from cccc's va_list and
// re-dispatch via ffi_prep_cif_var to the matching non-v* variadic function.
// All scanf variadic args are output pointers (CCCC_VAARG_INT). (#407)
#include "va_ffi_helper.h"

long long wrap_cccc_vscanf(const char *fmt, long long va_ptr) {
    cccc_va_list_t *va = (cccc_va_list_t *)va_ptr;
    int types[CCCC_VA_MAX_ARGS];
    int n = cccc_parse_scanf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = { (int64_t)fmt };
    return cccc_ffi_call_variadic((void *)cccc_scanf, 1, fixed, n, types, vals);
}

long long wrap_cccc_vsscanf(const char *str, const char *fmt, long long va_ptr) {
    cccc_va_list_t *va = (cccc_va_list_t *)va_ptr;
    int types[CCCC_VA_MAX_ARGS];
    int n = cccc_parse_scanf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = { (int64_t)str, (int64_t)fmt };
    return cccc_ffi_call_variadic((void *)cccc_sscanf, 2, fixed, n, types, vals);
}

long long wrap_cccc_vfscanf(FILE *stream, const char *fmt, long long va_ptr) {
    cccc_va_list_t *va = (cccc_va_list_t *)va_ptr;
    int types[CCCC_VA_MAX_ARGS];
    int n = cccc_parse_scanf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = { (int64_t)stream, (int64_t)fmt };
    return cccc_ffi_call_variadic((void *)cccc_fscanf, 2, fixed, n, types, vals);
}
