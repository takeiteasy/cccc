// -c=native C11/C23 <uchar.h> shims (#1141): mbrtoc16/c16rtomb/mbrtoc32/
// c32rtomb/mbrtoc8/c8rtomb, ported from src/stdlib/wide.c. Each block is
// wrapped in the same __GLIBC_PREREQ feature test wide.c uses.
//
// Source of truth for the text tools/gen_shims.py embeds into
// src/shims.inc. NOT COMPILED. Gating and rationale live in the
// matching serialize_*_shims() in src/serialize_shims.c.

// >>> shim: includes
#if defined(__GLIBC__)
#include <features.h>
#endif
#include <errno.h>
// <<< shim

// >>> shim: guard_16_32_open
#if !defined(__GLIBC__)
#define __CCCC_NEED_UCHAR16_32_SHIM 1
#elif !__GLIBC_PREREQ(2, 16)
#define __CCCC_NEED_UCHAR16_32_SHIM 1
#endif
#ifdef __CCCC_NEED_UCHAR16_32_SHIM
// <<< shim

// >>> shim: mbrtoc16
size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps) {
    wchar_t wc;
    size_t rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == 0)
        return rc;
    if (pc16) *pc16 = (char16_t)wc;
    return rc;
}
// <<< shim

// >>> shim: c16rtomb
size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps) {
    return wcrtomb(s, (wchar_t)c16, ps);
}
// <<< shim

// >>> shim: mbrtoc32
size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps) {
    wchar_t wc;
    size_t rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == 0)
        return rc;
    if (pc32) *pc32 = (char32_t)wc;
    return rc;
}
// <<< shim

// >>> shim: c32rtomb
size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps) {
    return wcrtomb(s, (wchar_t)c32, ps);
}
// <<< shim

// >>> shim: guard_8_open
#if !defined(__GLIBC__)
#define __CCCC_NEED_UCHAR8_SHIM 1
#elif !__GLIBC_PREREQ(2, 36)
#define __CCCC_NEED_UCHAR8_SHIM 1
#endif
#ifdef __CCCC_NEED_UCHAR8_SHIM
// <<< shim

// >>> shim: utf8_helpers
typedef struct { unsigned char magic; unsigned char buf[4];
                 unsigned char len; unsigned char pos; } __cccc_c8state;
#define __CCCC_C8STATE_MAGIC 0xC8
_Static_assert(sizeof(mbstate_t) >= sizeof(__cccc_c8state),
               "cccc: host mbstate_t too small to hold __cccc_c8state");
typedef struct { unsigned char buf[4]; unsigned char len;
                 unsigned char need; } __cccc_c8out_state;
_Static_assert(sizeof(mbstate_t) >= sizeof(__cccc_c8out_state),
               "cccc: host mbstate_t too small to hold __cccc_c8out_state");
static unsigned __cccc_utf8_encode(unsigned char out[4], unsigned cp) {
    if (cp <= 0x7F) { out[0] = (unsigned char)cp; return 1; }
    if (cp <= 0x7FF) {
        out[0] = (unsigned char)(0xC0 | (cp >> 6));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
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
static int __cccc_utf8_decode(const unsigned char *buf, unsigned len, unsigned *out) {
    unsigned cp;
    switch (len) {
        case 1:
            if (buf[0] & 0x80) return -1;
            cp = buf[0];
            break;
        case 2:
            if ((buf[1] & 0xC0) != 0x80) return -1;
            cp = (unsigned)(buf[0] & 0x1F) << 6 | (buf[1] & 0x3F);
            if (cp < 0x80) return -1;
            break;
        case 3:
            if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) != 0x80) return -1;
            cp = (unsigned)(buf[0] & 0x0F) << 12 | (unsigned)(buf[1] & 0x3F) << 6 | (buf[2] & 0x3F);
            if (cp < 0x800) return -1;
            if (cp >= 0xD800 && cp <= 0xDFFF) return -1;
            break;
        case 4:
            if ((buf[1] & 0xC0) != 0x80 || (buf[2] & 0xC0) != 0x80 ||
                (buf[3] & 0xC0) != 0x80) return -1;
            cp = (unsigned)(buf[0] & 0x07) << 18 | (unsigned)(buf[1] & 0x3F) << 12 |
                 (unsigned)(buf[2] & 0x3F) << 6 | (buf[3] & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) return -1;
            break;
        default:
            return -1;
    }
    *out = cp;
    return 0;
}
// <<< shim

// >>> shim: mbrtoc8
size_t mbrtoc8(char8_t *pc8, const char *s, size_t n, mbstate_t *ps) {
    static mbstate_t internal_state;
    if (!ps) ps = &internal_state;
    __cccc_c8state st;
    __builtin_memcpy(&st, ps, sizeof(st));
    if (st.magic == __CCCC_C8STATE_MAGIC && st.len > 0) {
        if (pc8) *pc8 = st.buf[st.pos];
        st.pos++;
        st.len--;
        if (st.len == 0) __builtin_memset(ps, 0, sizeof(*ps));
        else __builtin_memcpy(ps, &st, sizeof(st));
        return (size_t)-3;
    }
    wchar_t wc;
    size_t rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2) return rc;
    if (rc == 0) { if (pc8) *pc8 = 0; return 0; }
    unsigned char enc[4];
    unsigned elen = __cccc_utf8_encode(enc, (unsigned)wc);
    if (elen == 0) { errno = EILSEQ; return (size_t)-1; }
    if (pc8) *pc8 = enc[0];
    if (elen > 1) {
        __cccc_c8state newst;
        __builtin_memset(&newst, 0, sizeof(newst));
        newst.magic = __CCCC_C8STATE_MAGIC;
        __builtin_memcpy(newst.buf, enc + 1, elen - 1);
        newst.len = (unsigned char)(elen - 1);
        newst.pos = 0;
        __builtin_memcpy(ps, &newst, sizeof(newst));
    }
    return rc;
}
// <<< shim

// >>> shim: c8rtomb
size_t c8rtomb(char *s, char8_t c8, mbstate_t *ps) {
    static mbstate_t internal_state;
    if (!ps) ps = &internal_state;
    if (!s) { __builtin_memset(ps, 0, sizeof(*ps)); return 0; }
    __cccc_c8out_state st;
    __builtin_memcpy(&st, ps, sizeof(st));
    if (c8 == 0) {
        if (st.len != 0) { errno = EILSEQ; return (size_t)-1; }
        mbstate_t wcs;
        __builtin_memset(&wcs, 0, sizeof(wcs));
        return wcrtomb(s, L'\0', &wcs);
    }
    if (st.len == 0) {
        unsigned need;
        if ((c8 & 0x80) == 0x00) need = 1;
        else if ((c8 & 0xE0) == 0xC0) need = 2;
        else if ((c8 & 0xF0) == 0xE0) need = 3;
        else if ((c8 & 0xF8) == 0xF0) need = 4;
        else { errno = EILSEQ; return (size_t)-1; }
        st.need = (unsigned char)need;
    } else if ((c8 & 0xC0) != 0x80) {
        __builtin_memset(ps, 0, sizeof(*ps));
        errno = EILSEQ;
        return (size_t)-1;
    }
    st.buf[st.len++] = (unsigned char)c8;
    if (st.len < st.need) {
        __builtin_memcpy(ps, &st, sizeof(st));
        return 0;
    }
    unsigned cp;
    int ok = __cccc_utf8_decode(st.buf, st.len, &cp) == 0;
    __builtin_memset(ps, 0, sizeof(*ps));
    if (!ok) { errno = EILSEQ; return (size_t)-1; }
    mbstate_t wcs;
    __builtin_memset(&wcs, 0, sizeof(wcs));
    return wcrtomb(s, (wchar_t)cp, &wcs);
}
// <<< shim
