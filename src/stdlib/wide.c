// wchar.h, wctype.h, and uchar.h stdlib function registration
#include "../cccc.h"
#include <wchar.h>
#include <wctype.h>

typedef uint16_t cccc_char16_t;
typedef uint32_t cccc_char32_t;

static long long wrap_wctob(long long c) { return (long long)wctob((wint_t)c); }
static long long wrap_mbsinit(long long ps) { return (long long)mbsinit((const mbstate_t *)ps); }
static long long wrap_wcscmp(long long a, long long b) { return (long long)wcscmp((const wchar_t *)a, (const wchar_t *)b); }
static long long wrap_wcsncmp(long long a, long long b, long long n) { return (long long)wcsncmp((const wchar_t *)a, (const wchar_t *)b, (size_t)n); }
static long long wrap_iswalnum(long long c) { return (long long)iswalnum((wint_t)c); }
static long long wrap_iswalpha(long long c) { return (long long)iswalpha((wint_t)c); }
static long long wrap_iswblank(long long c) { return (long long)iswblank((wint_t)c); }
static long long wrap_iswcntrl(long long c) { return (long long)iswcntrl((wint_t)c); }
static long long wrap_iswdigit(long long c) { return (long long)iswdigit((wint_t)c); }
static long long wrap_iswgraph(long long c) { return (long long)iswgraph((wint_t)c); }
static long long wrap_iswlower(long long c) { return (long long)iswlower((wint_t)c); }
static long long wrap_iswprint(long long c) { return (long long)iswprint((wint_t)c); }
static long long wrap_iswpunct(long long c) { return (long long)iswpunct((wint_t)c); }
static long long wrap_iswspace(long long c) { return (long long)iswspace((wint_t)c); }
static long long wrap_iswupper(long long c) { return (long long)iswupper((wint_t)c); }
static long long wrap_iswxdigit(long long c) { return (long long)iswxdigit((wint_t)c); }
static long long wrap_iswctype(long long c, long long desc) { return (long long)iswctype((wint_t)c, (wctype_t)desc); }

static size_t cccc_mbrtoc16(cccc_char16_t *pc16, const char *s, size_t n, mbstate_t *ps) {
    wchar_t wc;
    size_t rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == 0)
        return rc;
    if (pc16)
        *pc16 = (cccc_char16_t)wc;
    return rc;
}

static size_t cccc_c16rtomb(char *s, cccc_char16_t c16, mbstate_t *ps) {
    return wcrtomb(s, (wchar_t)c16, ps);
}

static size_t cccc_mbrtoc32(cccc_char32_t *pc32, const char *s, size_t n, mbstate_t *ps) {
    wchar_t wc;
    size_t rc = mbrtowc(&wc, s, n, ps);
    if (rc == (size_t)-1 || rc == (size_t)-2 || rc == 0)
        return rc;
    if (pc32)
        *pc32 = (cccc_char32_t)wc;
    return rc;
}

static size_t cccc_c32rtomb(char *s, cccc_char32_t c32, mbstate_t *ps) {
    return wcrtomb(s, (wchar_t)c32, ps);
}

void register_wide_functions(CCCC *vm) {
    cc_register_cfunc(vm, "mbsinit", (void*)wrap_mbsinit, 1, 0);
    cc_register_cfunc(vm, "mbrlen", (void*)mbrlen, 3, 0);
    cc_register_cfunc(vm, "mbrtowc", (void*)mbrtowc, 4, 0);
    cc_register_cfunc(vm, "wcrtomb", (void*)wcrtomb, 3, 0);
    cc_register_cfunc(vm, "mbsrtowcs", (void*)mbsrtowcs, 4, 0);
    cc_register_cfunc(vm, "wcsrtombs", (void*)wcsrtombs, 4, 0);
    cc_register_cfunc(vm, "wcscpy", (void*)wcscpy, 2, 0);
    cc_register_cfunc(vm, "wcsncpy", (void*)wcsncpy, 3, 0);
    cc_register_cfunc(vm, "wcscat", (void*)wcscat, 2, 0);
    cc_register_cfunc(vm, "wcsncat", (void*)wcsncat, 3, 0);
    cc_register_cfunc(vm, "wcscmp", (void*)wrap_wcscmp, 2, 0);
    cc_register_cfunc(vm, "wcsncmp", (void*)wrap_wcsncmp, 3, 0);
    cc_register_cfunc(vm, "wcslen", (void*)wcslen, 1, 0);
    cc_register_cfunc(vm, "wcschr", (void*)wcschr, 2, 0);
    cc_register_cfunc(vm, "wcsrchr", (void*)wcsrchr, 2, 0);
    cc_register_cfunc(vm, "wcsstr", (void*)wcsstr, 2, 0);
    cc_register_cfunc(vm, "wcsxfrm", (void*)wcsxfrm, 3, 0);
    cc_register_cfunc_ex(vm, "wcstod", (void*)wcstod, 2, 1, 0);
    cc_register_cfunc_ex(vm, "wcstof", (void*)wcstof, 2, 1, 0);
    cc_register_cfunc_ex(vm, "wcstold", (void*)wcstold, 2, 1, 0);
    cc_register_cfunc(vm, "wcstol", (void*)wcstol, 3, 0);
    cc_register_cfunc(vm, "wcstoll", (void*)wcstoll, 3, 0);
    cc_register_cfunc(vm, "wcstoul", (void*)wcstoul, 3, 0);
    cc_register_cfunc(vm, "wcstoull", (void*)wcstoull, 3, 0);
    cc_register_cfunc(vm, "wctob", (void*)wrap_wctob, 1, 0);
    cc_register_cfunc(vm, "btowc", (void*)btowc, 1, 0);

    cc_register_cfunc(vm, "iswalnum", (void*)wrap_iswalnum, 1, 0);
    cc_register_cfunc(vm, "iswalpha", (void*)wrap_iswalpha, 1, 0);
    cc_register_cfunc(vm, "iswblank", (void*)wrap_iswblank, 1, 0);
    cc_register_cfunc(vm, "iswcntrl", (void*)wrap_iswcntrl, 1, 0);
    cc_register_cfunc(vm, "iswdigit", (void*)wrap_iswdigit, 1, 0);
    cc_register_cfunc(vm, "iswgraph", (void*)wrap_iswgraph, 1, 0);
    cc_register_cfunc(vm, "iswlower", (void*)wrap_iswlower, 1, 0);
    cc_register_cfunc(vm, "iswprint", (void*)wrap_iswprint, 1, 0);
    cc_register_cfunc(vm, "iswpunct", (void*)wrap_iswpunct, 1, 0);
    cc_register_cfunc(vm, "iswspace", (void*)wrap_iswspace, 1, 0);
    cc_register_cfunc(vm, "iswupper", (void*)wrap_iswupper, 1, 0);
    cc_register_cfunc(vm, "iswxdigit", (void*)wrap_iswxdigit, 1, 0);
    cc_register_cfunc(vm, "iswctype", (void*)wrap_iswctype, 2, 0);
    cc_register_cfunc(vm, "towlower", (void*)towlower, 1, 0);
    cc_register_cfunc(vm, "towupper", (void*)towupper, 1, 0);
    cc_register_cfunc(vm, "towctrans", (void*)towctrans, 2, 0);
    cc_register_cfunc(vm, "wctype", (void*)wctype, 1, 0);
    cc_register_cfunc(vm, "wctrans", (void*)wctrans, 1, 0);

    cc_register_cfunc(vm, "mbrtoc16", (void*)cccc_mbrtoc16, 4, 0);
    cc_register_cfunc(vm, "c16rtomb", (void*)cccc_c16rtomb, 3, 0);
    cc_register_cfunc(vm, "mbrtoc32", (void*)cccc_mbrtoc32, 4, 0);
    cc_register_cfunc(vm, "c32rtomb", (void*)cccc_c32rtomb, 3, 0);
}
