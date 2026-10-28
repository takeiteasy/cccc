// wchar.h, wctype.h, and uchar.h stdlib function registration
#include "../cccc.h"
#include "../internal.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

typedef uint16_t      cccc_char16_t;
typedef uint32_t      cccc_char32_t;
typedef unsigned char cccc_char8_t;

// mbrtoc16/c16rtomb/mbrtoc32/c32rtomb native availability: glibc 2.16+
// provides <uchar.h>; macOS has none, so the mbrtowc/wcrtomb-based shims
// below remain in use there.
#if defined(__GLIBC__)
#include <features.h>
#if __GLIBC_PREREQ(2, 16)
#define CCCC_HAVE_NATIVE_UCHAR_CONV 1
#include <uchar.h>
#endif
#endif

// mbrtoc8/c8rtomb native availability: glibc 2.36+
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 36)
#define CCCC_HAVE_NATIVE_MBRTOC8 1
#endif
#endif

// wctob/btowc/mbrlen are glibc extern-inline functions (see <wchar.h>);
// taking their address directly (as cc_register_cfunc below used to for
// btowc/mbrlen) makes clang emit an out-of-line copy in every TU that
// registers them -- at -O0 these come out weak/discardable, but at any
// -O1+ clang emits them as strong symbols, causing "multiple definition"
// link errors the moment two TUs both pull the same header in (#883,
// reproduced on Linux aarch64/clang 18.1.3). A local wrapper function is
// never itself an extern-inline candidate, so its address is always safe
// to take.
static long long wrap_wctob(long long c) {
    return (long long)wctob((wint_t)c);
}
static long long wrap_btowc(long long c) {
    return (long long)btowc((int)c);
}
static long long wrap_mbrlen(long long s, long long n, long long ps) {
    return (long long)mbrlen((const char *)s, (size_t)n, (mbstate_t *)ps);
}
static long long wrap_mbsinit(long long ps) {
    return (long long)mbsinit((const mbstate_t *)ps);
}
static long long wrap_wcscmp(long long a, long long b) {
    return (long long)wcscmp((const wchar_t *)a, (const wchar_t *)b);
}
static long long wrap_wcsncmp(long long a, long long b, long long n) {
    return (long long)wcsncmp((const wchar_t *)a, (const wchar_t *)b,
                              (size_t)n);
}
// #1229: narrow the host wcstold result to `double` in C -- the VM has no
// FFI return slot for a real host `long double`, so registering bare wcstold
// with returns_double=1 would have libffi misread the return register on
// Linux (st(0) on x86-64, a wider fp reg on aarch64). Same double-precision
// shim convention as strtold / the math.h `...l` family (#491). No-op on
// macOS arm64 where `long double == double`.
static double cccc_wcstold(const wchar_t *nptr, wchar_t **endptr) {
    return (double)wcstold(nptr, endptr);
}
static long long wrap_iswalnum(long long c) {
    return (long long)iswalnum((wint_t)c);
}
static long long wrap_iswalpha(long long c) {
    return (long long)iswalpha((wint_t)c);
}
static long long wrap_iswblank(long long c) {
    return (long long)iswblank((wint_t)c);
}
static long long wrap_iswcntrl(long long c) {
    return (long long)iswcntrl((wint_t)c);
}
static long long wrap_iswdigit(long long c) {
    return (long long)iswdigit((wint_t)c);
}
static long long wrap_iswgraph(long long c) {
    return (long long)iswgraph((wint_t)c);
}
static long long wrap_iswlower(long long c) {
    return (long long)iswlower((wint_t)c);
}
static long long wrap_iswprint(long long c) {
    return (long long)iswprint((wint_t)c);
}
static long long wrap_iswpunct(long long c) {
    return (long long)iswpunct((wint_t)c);
}
static long long wrap_iswspace(long long c) {
    return (long long)iswspace((wint_t)c);
}
static long long wrap_iswupper(long long c) {
    return (long long)iswupper((wint_t)c);
}
static long long wrap_iswxdigit(long long c) {
    return (long long)iswxdigit((wint_t)c);
}
static long long wrap_iswctype(long long c, long long desc) {
    return (long long)iswctype((wint_t)c, (wctype_t)desc);
}

// #1141: src/serialize.c's serialize_uchar_shims() carries a hand-ported
// copy of every fallback in this #ifndef/#endif pair (and the
// CCCC_HAVE_NATIVE_MBRTOC8 pair below it) for -c=native, guarded by the
// identical __GLIBC_PREREQ feature test. The two copies have no shared
// source -- this one is compiled into CCCC itself, the other is text
// emitted into the generated guest program's own .c file -- and must be
// kept in sync by hand whenever either changes.
#ifndef CCCC_HAVE_NATIVE_UCHAR_CONV
static size_t cccc_mbrtoc16(cccc_char16_t *pc16, const char *s, size_t n,
                            mbstate_t *ps) {
    wchar_t wc;
    size_t  rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == 0)
        return rc;
    if (pc16)
        *pc16 = (cccc_char16_t)wc;
    return rc;
}

static size_t cccc_c16rtomb(char *s, cccc_char16_t c16, mbstate_t *ps) {
    return wcrtomb(s, (wchar_t)c16, ps);
}

static size_t cccc_mbrtoc32(cccc_char32_t *pc32, const char *s, size_t n,
                            mbstate_t *ps) {
    wchar_t wc;
    size_t  rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == 0)
        return rc;
    if (pc32)
        *pc32 = (cccc_char32_t)wc;
    return rc;
}

static size_t cccc_c32rtomb(char *s, cccc_char32_t c32, mbstate_t *ps) {
    return wcrtomb(s, (wchar_t)c32, ps);
}
#endif /* !CCCC_HAVE_NATIVE_UCHAR_CONV */

#ifndef CCCC_HAVE_NATIVE_MBRTOC8
// Queued-byte state for mbrtoc8: bytes 2-4 of a multi-byte UTF-8 sequence
// already produced, waiting to be drained one per call via (size_t)-3.
//
// *ps may also hold mbrtowc's own (opaque, implementation-defined) state
// for an incomplete multibyte sequence - e.g. after mbrtoc8 itself returns
// (size_t)-2. `magic` distinguishes "this is our queued-byte state" from
// "this is mbrtowc's pending-conversion state" so the two never get
// reinterpreted as each other.
typedef struct {
    unsigned char magic;
    unsigned char buf[4];
    unsigned char len;
    unsigned char pos;
} cccc_c8state;

#define CCCC_C8STATE_MAGIC 0xC8

_Static_assert(sizeof(mbstate_t) >= sizeof(cccc_c8state),
               "mbstate_t too small to hold cccc_c8state");

// Accumulation state for c8rtomb: UTF-8 bytes received so far for the
// current code point, and how many bytes that sequence needs in total.
typedef struct {
    unsigned char buf[4];
    unsigned char len;
    unsigned char need;
} cccc_c8out_state;

_Static_assert(sizeof(mbstate_t) >= sizeof(cccc_c8out_state),
               "mbstate_t too small to hold cccc_c8out_state");

// Encode a Unicode code point as 1-4 UTF-8 bytes. Returns the byte count,
// or 0 if cp is a surrogate or out of range.
static unsigned cccc_utf8_encode(unsigned char out[4], uint32_t cp) {
    if (cp <= 0x7F) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp <= 0x7FF) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF)
        return 0;
    if (cp <= 0xFFFF) {
        out[0] = (unsigned char)(0xE0 | (cp >> 12));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (unsigned char)(0xF0 | (cp >> 18));
        out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (unsigned char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

// Decode a complete len-byte UTF-8 sequence into a code point, validating
// continuation bytes, overlong encodings, surrogates and range. Returns 0
// on success, -1 if the sequence is invalid.
static int cccc_utf8_decode(const unsigned char *buf, unsigned len,
                            uint32_t *out) {
    uint32_t cp;
    switch (len) {
        case 1:
            if (buf[0] & 0x80)
                return -1;
            cp = buf[0];
            break;
        case 2:
            if ((buf[1] & 0xC0) != 0x80)
                return -1;
            cp = (uint32_t)(buf[0] & 0x1F) << 6 | (buf[1] & 0x3F);
            if (cp < 0x80)
                return -1;
            break;
        case 3:
            if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) != 0x80)
                return -1;
            cp = (uint32_t)(buf[0] & 0x0F) << 12 |
                 (uint32_t)(buf[1] & 0x3F) << 6 | (buf[2] & 0x3F);
            if (cp < 0x800)
                return -1;
            if (cp >= 0xD800 && cp <= 0xDFFF)
                return -1;
            break;
        case 4:
            if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) != 0x80 ||
                (buf[3] & 0xC0) != 0x80)
                return -1;
            cp = (uint32_t)(buf[0] & 0x07) << 18 |
                 (uint32_t)(buf[1] & 0x3F) << 12 |
                 (uint32_t)(buf[2] & 0x3F) << 6 | (buf[3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF)
                return -1;
            break;
        default:
            return -1;
    }
    *out = cp;
    return 0;
}

// C23 mbrtoc8: converts the next multibyte character to UTF-8, emitting one
// char8_t per call. If the character encodes to more than one UTF-8 byte,
// the first byte is returned now and the rest are queued in *ps, drained on
// subsequent calls via the (size_t)-3 "no input consumed" convention.
static size_t cccc_mbrtoc8(cccc_char8_t *pc8, const char *s, size_t n,
                           mbstate_t *ps) {
    static mbstate_t internal_state;
    if (!ps)
        ps = &internal_state;

    cccc_c8state st;
    memcpy(&st, ps, sizeof(st));

    if (st.magic == CCCC_C8STATE_MAGIC && st.len > 0) {
        if (pc8)
            *pc8 = st.buf[st.pos];
        st.pos++;
        st.len--;
        if (st.len == 0)
            memset(ps, 0, sizeof(*ps));
        else
            memcpy(ps, &st, sizeof(st));
        return (size_t)-3;
    }

    wchar_t wc;
    size_t  rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2)
        return rc;
    if (rc == 0) {
        if (pc8)
            *pc8 = 0;
        return 0;
    }

    unsigned char enc[4];
    unsigned      elen = cccc_utf8_encode(enc, (uint32_t)wc);
    if (elen == 0) {
        errno = EILSEQ;
        return (size_t)-1;
    }
    if (pc8)
        *pc8 = enc[0];
    if (elen > 1) {
        // mbrtowc consumed a complete character, so *ps is back in its
        // initial state and safe to overlay with the tagged queued-byte
        // state.
        cccc_c8state newst = {0};
        newst.magic        = CCCC_C8STATE_MAGIC;
        memcpy(newst.buf, enc + 1, elen - 1);
        newst.len = (unsigned char)(elen - 1);
        newst.pos = 0;
        memcpy(ps, &newst, sizeof(newst));
    }
    return rc;
}

// C23 c8rtomb: accumulates UTF-8 bytes for the current code point in *ps,
// returning 0 while incomplete and the encoded byte count (via wcrtomb)
// once the sequence is complete.
static size_t cccc_c8rtomb(char *s, cccc_char8_t c8, mbstate_t *ps) {
    static mbstate_t internal_state;
    if (!ps)
        ps = &internal_state;

    if (!s) {
        memset(ps, 0, sizeof(*ps));
        return 0;
    }

    cccc_c8out_state st;
    memcpy(&st, ps, sizeof(st));

    if (c8 == 0) {
        if (st.len != 0) {
            errno = EILSEQ;
            return (size_t)-1;
        }
        mbstate_t wcs;
        memset(&wcs, 0, sizeof(wcs));
        return wcrtomb(s, L'\0', &wcs);
    }

    if (st.len == 0) {
        unsigned need;
        if ((c8 & 0x80) == 0x00)
            need = 1;
        else if ((c8 & 0xE0) == 0xC0)
            need = 2;
        else if ((c8 & 0xF0) == 0xE0)
            need = 3;
        else if ((c8 & 0xF8) == 0xF0)
            need = 4;
        else {
            errno = EILSEQ;
            return (size_t)-1;
        }
        st.need = (unsigned char)need;
    } else if ((c8 & 0xC0) != 0x80) {
        memset(ps, 0, sizeof(*ps));
        errno = EILSEQ;
        return (size_t)-1;
    }

    st.buf[st.len++] = (unsigned char)c8;

    if (st.len < st.need) {
        memcpy(ps, &st, sizeof(st));
        return 0;
    }

    uint32_t cp;
    int      ok = cccc_utf8_decode(st.buf, st.len, &cp) == 0;
    memset(ps, 0, sizeof(*ps));
    if (!ok) {
        errno = EILSEQ;
        return (size_t)-1;
    }
    mbstate_t wcs;
    memset(&wcs, 0, sizeof(wcs));
    return wcrtomb(s, (wchar_t)cp, &wcs);
}
#endif /* !CCCC_HAVE_NATIVE_MBRTOC8 */

// wcslcpy/wcslcat: Darwin and glibc >= 2.38 have them; older glibc gets the
// BSD semantics from these two functions.
#if defined(__APPLE__)
#define CCCC_HOST_HAS_WCSLCPY 1
#elif defined(__GLIBC__)
#include <features.h>
#if __GLIBC_PREREQ(2, 38)
#define CCCC_HOST_HAS_WCSLCPY 1
#endif
#endif
#ifdef CCCC_HOST_HAS_WCSLCPY
#define cccc_wcslcpy wcslcpy
#define cccc_wcslcat wcslcat
#else
static size_t cccc_wcslcpy(wchar_t *d, const wchar_t *s, size_t n) {
    size_t len = wcslen(s);
    if (n) {
        size_t c = len < n - 1 ? len : n - 1;
        wmemcpy(d, s, c);
        d[c] = L'\0';
    }
    return len;
}
static size_t cccc_wcslcat(wchar_t *d, const wchar_t *s, size_t n) {
    size_t dl = 0;
    while (dl < n && d[dl])
        dl++;
    if (dl == n)
        return n + wcslen(s);
    return dl + cccc_wcslcpy(d + dl, s, n - dl);
}
#endif

// ---------------------------------------------------------------------------
// Wide printf family. The variadic forms go straight to the host; the v*
// forms replay the captured guest va_list through libffi. A wide format is
// narrowed to ASCII first, since only the % directives decide the argument
// types and those are the same as in the narrow printf.
// ---------------------------------------------------------------------------
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "va_ffi_helper.h"
#pragma GCC diagnostic pop

static long long wide_vprintf_call(void *host_fn, int nfixed, int64_t *fixed,
                                   const wchar_t *wfmt, long long va_ptr) {
    size_t flen = wcslen(wfmt);
    char  *nfmt = malloc(flen + 1);
    if (!nfmt) {
        errno = ENOMEM;
        return -1;
    }
    for (size_t i = 0; i <= flen; i++)
        nfmt[i] = wfmt[i] > 0 && wfmt[i] < 128 ? (char)wfmt[i] : '?';
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(nfmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    free(nfmt);
    return cccc_ffi_call_variadic(host_fn, nfixed, fixed, n, types, vals);
}

static long long wrap_vswprintf(wchar_t *s, long long n, const wchar_t *fmt,
                                long long va_ptr) {
    int64_t fixed[] = {(int64_t)s, (int64_t)n, (int64_t)fmt};
    return wide_vprintf_call((void *)swprintf, 3, fixed, fmt, va_ptr);
}

static long long wrap_vwprintf(const wchar_t *fmt, long long va_ptr) {
    int64_t fixed[] = {(int64_t)fmt};
    return wide_vprintf_call((void *)wprintf, 1, fixed, fmt, va_ptr);
}

static long long wrap_vfwprintf(FILE *stream, const wchar_t *fmt,
                                long long va_ptr) {
    int64_t fixed[] = {(int64_t)stream, (int64_t)fmt};
    return wide_vprintf_call((void *)fwprintf, 2, fixed, fmt, va_ptr);
}

// ---------------------------------------------------------------------------
// `__*_chk` entry points glibc's fortified <wchar.h> calls. The object sizes
// glibc passes are already in wchar_t units, except for the byte buffers of
// wcrtomb/wcsrtombs/wcsnrtombs.
// ---------------------------------------------------------------------------
static wchar_t *chk_wmemcpy(wchar_t *d, const wchar_t *s, size_t n, size_t ns) {
    CCCC_FORTIFY_CHECK(n, ns, "wmemcpy");
    return wmemcpy(d, s, n);
}
static wchar_t *chk_wmemmove(wchar_t *d, const wchar_t *s, size_t n,
                             size_t ns) {
    CCCC_FORTIFY_CHECK(n, ns, "wmemmove");
    return wmemmove(d, s, n);
}
static wchar_t *chk_wmemset(wchar_t *d, long long c, size_t n, size_t ns) {
    CCCC_FORTIFY_CHECK(n, ns, "wmemset");
    return wmemset(d, (wchar_t)c, n);
}
static wchar_t *chk_wcscpy(wchar_t *d, const wchar_t *s, size_t n) {
    CCCC_FORTIFY_CHECK(wcslen(s) + 1, n, "wcscpy");
    return wcscpy(d, s);
}
static wchar_t *chk_wcpcpy(wchar_t *d, const wchar_t *s, size_t n) {
    CCCC_FORTIFY_CHECK(wcslen(s) + 1, n, "wcpcpy");
    return wcpcpy(d, s);
}
static wchar_t *chk_wcsncpy(wchar_t *d, const wchar_t *s, size_t n, size_t dl) {
    CCCC_FORTIFY_CHECK(n, dl, "wcsncpy");
    return wcsncpy(d, s, n);
}
static wchar_t *chk_wcpncpy(wchar_t *d, const wchar_t *s, size_t n, size_t dl) {
    CCCC_FORTIFY_CHECK(n, dl, "wcpncpy");
    return wcpncpy(d, s, n);
}
static wchar_t *chk_wcscat(wchar_t *d, const wchar_t *s, size_t dl) {
    CCCC_FORTIFY_CHECK(wcslen(d) + wcslen(s) + 1, dl, "wcscat");
    return wcscat(d, s);
}
static wchar_t *chk_wcsncat(wchar_t *d, const wchar_t *s, size_t n, size_t dl) {
    size_t sl = wcslen(s);
    CCCC_FORTIFY_CHECK(wcslen(d) + (sl < n ? sl : n) + 1, dl, "wcsncat");
    return wcsncat(d, s, n);
}
static size_t chk_wcslcpy(wchar_t *d, const wchar_t *s, size_t n, size_t dl) {
    CCCC_FORTIFY_CHECK(n, dl, "wcslcpy");
    return cccc_wcslcpy(d, s, n);
}
static size_t chk_wcslcat(wchar_t *d, const wchar_t *s, size_t n, size_t dl) {
    CCCC_FORTIFY_CHECK(n, dl, "wcslcat");
    return cccc_wcslcat(d, s, n);
}
static size_t chk_wcrtomb(char *s, long long wc, mbstate_t *ps, size_t buflen) {
    if (s && buflen < (size_t)MB_CUR_MAX)
        cccc_fortify_fail("wcrtomb");
    return wcrtomb(s, (wchar_t)wc, ps);
}
static size_t chk_mbsrtowcs(wchar_t *d, const char **src, size_t len,
                            mbstate_t *ps, size_t dl) {
    CCCC_FORTIFY_CHECK(len, dl, "mbsrtowcs");
    return mbsrtowcs(d, src, len, ps);
}
static size_t chk_mbsnrtowcs(wchar_t *d, const char **src, size_t nmc,
                             size_t len, mbstate_t *ps, size_t dl) {
    CCCC_FORTIFY_CHECK(len, dl, "mbsnrtowcs");
    return mbsnrtowcs(d, src, nmc, len, ps);
}
static size_t chk_wcsrtombs(char *d, const wchar_t **src, size_t len,
                            mbstate_t *ps, size_t dl) {
    CCCC_FORTIFY_CHECK(len, dl, "wcsrtombs");
    return wcsrtombs(d, src, len, ps);
}
static size_t chk_wcsnrtombs(char *d, const wchar_t **src, size_t nwc,
                             size_t len, mbstate_t *ps, size_t dl) {
    CCCC_FORTIFY_CHECK(len, dl, "wcsnrtombs");
    return wcsnrtombs(d, src, nwc, len, ps);
}
static wchar_t *chk_fgetws(wchar_t *s, size_t size, int n, FILE *f) {
    CCCC_FORTIFY_CHECK(n < 0 ? 0 : (size_t)n, size, "fgetws");
    return fgetws(s, n, f);
}
#ifdef __linux__
// glibc exports these regardless of the feature macros; declared here rather
// than enabling _GNU_SOURCE for the whole file.
extern wchar_t *wmempcpy(wchar_t *d, const wchar_t *s, size_t n);
extern wchar_t *fgetws_unlocked(wchar_t *s, int n, FILE *f);
static wchar_t *chk_wmempcpy(wchar_t *d, const wchar_t *s, size_t n,
                             size_t ns) {
    CCCC_FORTIFY_CHECK(n, ns, "wmempcpy");
    return wmempcpy(d, s, n);
}
static wchar_t *chk_fgetws_unlocked(wchar_t *s, size_t size, int n, FILE *f) {
    CCCC_FORTIFY_CHECK(n < 0 ? 0 : (size_t)n, size, "fgetws_unlocked");
    return fgetws_unlocked(s, n, f);
}
#endif

static int chk_swprintf(wchar_t *s, size_t n, int flag, size_t slen,
                        const wchar_t *fmt, ...) {
    (void)flag;
    CCCC_FORTIFY_CHECK(n, slen, "swprintf");
    va_list ap;
    va_start(ap, fmt);
    int r = vswprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}
static int chk_fwprintf(FILE *f, int flag, const wchar_t *fmt, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    int r = vfwprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
static int chk_wprintf(int flag, const wchar_t *fmt, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    int r = vwprintf(fmt, ap);
    va_end(ap);
    return r;
}
static long long chk_vswprintf(wchar_t *s, long long n, long long flag,
                               long long slen, const wchar_t *fmt,
                               long long va_ptr) {
    (void)flag;
    CCCC_FORTIFY_CHECK(n, slen, "vswprintf");
    return wrap_vswprintf(s, n, fmt, va_ptr);
}
static long long chk_vfwprintf(FILE *f, long long flag, const wchar_t *fmt,
                               long long va_ptr) {
    (void)flag;
    return wrap_vfwprintf(f, fmt, va_ptr);
}
static long long chk_vwprintf(long long flag, const wchar_t *fmt,
                              long long va_ptr) {
    (void)flag;
    return wrap_vwprintf(fmt, va_ptr);
}

void register_wide_functions(VirtualMachine *vm) {
    cc_register_cfunc(vm, "mbsinit", (void *)wrap_mbsinit, 1, 0);
    cc_register_cfunc(vm, "mbrlen", (void *)wrap_mbrlen, 3, 0);
    cc_register_cfunc(vm, "mbrtowc", (void *)mbrtowc, 4, 0);
    cc_register_cfunc(vm, "wcrtomb", (void *)wcrtomb, 3, 0);
    cc_register_cfunc(vm, "mbsrtowcs", (void *)mbsrtowcs, 4, 0);
    cc_register_cfunc(vm, "wcsrtombs", (void *)wcsrtombs, 4, 0);
    cc_register_cfunc(vm, "wcscpy", (void *)wcscpy, 2, 0);
    cc_register_cfunc(vm, "wcsncpy", (void *)wcsncpy, 3, 0);
    cc_register_cfunc(vm, "wcscat", (void *)wcscat, 2, 0);
    cc_register_cfunc(vm, "wcsncat", (void *)wcsncat, 3, 0);
    cc_register_cfunc(vm, "wcscmp", (void *)wrap_wcscmp, 2, 0);
    cc_register_cfunc(vm, "wcsncmp", (void *)wrap_wcsncmp, 3, 0);
    cc_register_cfunc(vm, "wcslen", (void *)wcslen, 1, 0);
    cc_register_cfunc(vm, "wcschr", (void *)wcschr, 2, 0);
    cc_register_cfunc(vm, "wcsrchr", (void *)wcsrchr, 2, 0);
    cc_register_cfunc(vm, "wcsstr", (void *)wcsstr, 2, 0);
    cc_register_cfunc(vm, "wcsxfrm", (void *)wcsxfrm, 3, 0);
    cc_register_cfunc_ex(vm, "wcstod", (void *)wcstod, 2, 1, 0);
    cc_register_cfunc_ex(vm, "wcstof", (void *)wcstof, 2, 2,
                         0); // returns float (#777: was incorrectly 1/double)
    cc_register_cfunc_ex(vm, "wcstold", (void *)cccc_wcstold, 2, 1,
                         0); // #1229: double-narrowing shim, see cccc_wcstold
    cc_register_cfunc(vm, "wcstol", (void *)wcstol, 3, 0);
    cc_register_cfunc(vm, "wcstoll", (void *)wcstoll, 3, 0);
    cc_register_cfunc(vm, "wcstoul", (void *)wcstoul, 3, 0);
    cc_register_cfunc(vm, "wcstoull", (void *)wcstoull, 3, 0);
    cc_register_cfunc(vm, "wctob", (void *)wrap_wctob, 1, 0);
    cc_register_cfunc(vm, "btowc", (void *)wrap_btowc, 1, 0);

    cc_register_cfunc(vm, "iswalnum", (void *)wrap_iswalnum, 1, 0);
    cc_register_cfunc(vm, "iswalpha", (void *)wrap_iswalpha, 1, 0);
    cc_register_cfunc(vm, "iswblank", (void *)wrap_iswblank, 1, 0);
    cc_register_cfunc(vm, "iswcntrl", (void *)wrap_iswcntrl, 1, 0);
    cc_register_cfunc(vm, "iswdigit", (void *)wrap_iswdigit, 1, 0);
    cc_register_cfunc(vm, "iswgraph", (void *)wrap_iswgraph, 1, 0);
    cc_register_cfunc(vm, "iswlower", (void *)wrap_iswlower, 1, 0);
    cc_register_cfunc(vm, "iswprint", (void *)wrap_iswprint, 1, 0);
    cc_register_cfunc(vm, "iswpunct", (void *)wrap_iswpunct, 1, 0);
    cc_register_cfunc(vm, "iswspace", (void *)wrap_iswspace, 1, 0);
    cc_register_cfunc(vm, "iswupper", (void *)wrap_iswupper, 1, 0);
    cc_register_cfunc(vm, "iswxdigit", (void *)wrap_iswxdigit, 1, 0);
    cc_register_cfunc(vm, "iswctype", (void *)wrap_iswctype, 2, 0);
    cc_register_cfunc(vm, "towlower", (void *)towlower, 1, 0);
    cc_register_cfunc(vm, "towupper", (void *)towupper, 1, 0);
    cc_register_cfunc(vm, "towctrans", (void *)towctrans, 2, 0);
    cc_register_cfunc(vm, "wctype", (void *)wctype, 1, 0);
    cc_register_cfunc(vm, "wctrans", (void *)wctrans, 1, 0);

#ifdef CCCC_HAVE_NATIVE_UCHAR_CONV
    cc_register_cfunc(vm, "mbrtoc16", (void *)mbrtoc16, 4, 0);
    cc_register_cfunc(vm, "c16rtomb", (void *)c16rtomb, 3, 0);
    cc_register_cfunc(vm, "mbrtoc32", (void *)mbrtoc32, 4, 0);
    cc_register_cfunc(vm, "c32rtomb", (void *)c32rtomb, 3, 0);
#else
    cc_register_cfunc(vm, "mbrtoc16", (void *)cccc_mbrtoc16, 4, 0);
    cc_register_cfunc(vm, "c16rtomb", (void *)cccc_c16rtomb, 3, 0);
    cc_register_cfunc(vm, "mbrtoc32", (void *)cccc_mbrtoc32, 4, 0);
    cc_register_cfunc(vm, "c32rtomb", (void *)cccc_c32rtomb, 3, 0);
#endif

#ifdef CCCC_HAVE_NATIVE_MBRTOC8
    cc_register_cfunc(vm, "mbrtoc8", (void *)mbrtoc8, 4, 0);
    cc_register_cfunc(vm, "c8rtomb", (void *)c8rtomb, 3, 0);
#else
    cc_register_cfunc(vm, "mbrtoc8", (void *)cccc_mbrtoc8, 4, 0);
    cc_register_cfunc(vm, "c8rtomb", (void *)cccc_c8rtomb, 3, 0);
#endif

    cc_register_cfunc(vm, "wmemcpy", (void *)wmemcpy, 3, 0);
    cc_register_cfunc(vm, "wmemmove", (void *)wmemmove, 3, 0);
    cc_register_cfunc(vm, "wmemset", (void *)wmemset, 3, 0);
    cc_register_cfunc(vm, "wcpcpy", (void *)wcpcpy, 2, 0);
    cc_register_cfunc(vm, "wcpncpy", (void *)wcpncpy, 3, 0);
    cc_register_cfunc(vm, "wcslcpy", (void *)cccc_wcslcpy, 3, 0);
    cc_register_cfunc(vm, "wcslcat", (void *)cccc_wcslcat, 3, 0);
    cc_register_cfunc(vm, "fgetws", (void *)fgetws, 3, 0);
    cc_register_cfunc(vm, "mbsnrtowcs", (void *)mbsnrtowcs, 5, 0);
    cc_register_cfunc(vm, "wcsnrtombs", (void *)wcsnrtombs, 5, 0);
    cc_register_variadic_cfunc(vm, "swprintf", (void *)swprintf, 3, 0);
    cc_register_variadic_cfunc(vm, "wprintf", (void *)wprintf, 1, 0);
    cc_register_variadic_cfunc(vm, "fwprintf", (void *)fwprintf, 2, 0);
    cc_register_cfunc(vm, "vswprintf", (void *)wrap_vswprintf, 4, 0);
    cc_register_cfunc(vm, "vwprintf", (void *)wrap_vwprintf, 2, 0);
    cc_register_cfunc(vm, "vfwprintf", (void *)wrap_vfwprintf, 3, 0);
#ifdef __linux__
    cc_register_cfunc(vm, "wmempcpy", (void *)wmempcpy, 3, 0);
    cc_register_cfunc(vm, "fgetws_unlocked", (void *)fgetws_unlocked, 3, 0);
    cc_register_cfunc(vm, "__wmempcpy_chk", (void *)chk_wmempcpy, 4, 0);
    cc_register_cfunc(vm, "__fgetws_unlocked_chk", (void *)chk_fgetws_unlocked,
                      4, 0);
#endif

    // __*_chk
    cc_register_cfunc(vm, "__wmemcpy_chk", (void *)chk_wmemcpy, 4, 0);
    cc_register_cfunc(vm, "__wmemmove_chk", (void *)chk_wmemmove, 4, 0);
    cc_register_cfunc(vm, "__wmemset_chk", (void *)chk_wmemset, 4, 0);
    cc_register_cfunc(vm, "__wcscpy_chk", (void *)chk_wcscpy, 3, 0);
    cc_register_cfunc(vm, "__wcpcpy_chk", (void *)chk_wcpcpy, 3, 0);
    cc_register_cfunc(vm, "__wcsncpy_chk", (void *)chk_wcsncpy, 4, 0);
    cc_register_cfunc(vm, "__wcpncpy_chk", (void *)chk_wcpncpy, 4, 0);
    cc_register_cfunc(vm, "__wcscat_chk", (void *)chk_wcscat, 3, 0);
    cc_register_cfunc(vm, "__wcsncat_chk", (void *)chk_wcsncat, 4, 0);
    cc_register_cfunc(vm, "__wcslcpy_chk", (void *)chk_wcslcpy, 4, 0);
    cc_register_cfunc(vm, "__wcslcat_chk", (void *)chk_wcslcat, 4, 0);
    cc_register_cfunc(vm, "__wcrtomb_chk", (void *)chk_wcrtomb, 4, 0);
    cc_register_cfunc(vm, "__mbsrtowcs_chk", (void *)chk_mbsrtowcs, 5, 0);
    cc_register_cfunc(vm, "__mbsnrtowcs_chk", (void *)chk_mbsnrtowcs, 6, 0);
    cc_register_cfunc(vm, "__wcsrtombs_chk", (void *)chk_wcsrtombs, 5, 0);
    cc_register_cfunc(vm, "__wcsnrtombs_chk", (void *)chk_wcsnrtombs, 6, 0);
    cc_register_cfunc(vm, "__fgetws_chk", (void *)chk_fgetws, 4, 0);
    cc_register_variadic_cfunc(vm, "__swprintf_chk", (void *)chk_swprintf, 5,
                               0);
    cc_register_variadic_cfunc(vm, "__fwprintf_chk", (void *)chk_fwprintf, 3,
                               0);
    cc_register_variadic_cfunc(vm, "__wprintf_chk", (void *)chk_wprintf, 2, 0);
    cc_register_cfunc(vm, "__vswprintf_chk", (void *)chk_vswprintf, 6, 0);
    cc_register_cfunc(vm, "__vfwprintf_chk", (void *)chk_vfwprintf, 4, 0);
    cc_register_cfunc(vm, "__vwprintf_chk", (void *)chk_vwprintf, 3, 0);
}
