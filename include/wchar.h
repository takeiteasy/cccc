/* wchar.h - wide-character declarations for CCCC */

#ifndef __WCHAR_H
#define __WCHAR_H

#include "stddef.h"
#include <stdarg.h> // #1070: angle-bracket for a correct #include_next hand-off under real GCC
#include "time.h"

#ifndef WEOF
#define WEOF ((wint_t)-1)
#endif

typedef unsigned int wint_t;
typedef struct {
    unsigned long long __opaque[16];
} mbstate_t;

extern int mbsinit(const mbstate_t *ps);
extern size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
extern size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
extern size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
extern size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
extern size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);

extern wchar_t *wcscpy(wchar_t *s1, const wchar_t *s2);
extern wchar_t *wcsncpy(wchar_t *s1, const wchar_t *s2, size_t n);
extern wchar_t *wcscat(wchar_t *s1, const wchar_t *s2);
extern wchar_t *wcsncat(wchar_t *s1, const wchar_t *s2, size_t n);
extern int wcscmp(const wchar_t *s1, const wchar_t *s2);
extern int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
extern size_t wcslen(const wchar_t *s);
extern wchar_t *wcschr(const wchar_t *s, wchar_t c);
extern wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
extern wchar_t *wcsstr(const wchar_t *s1, const wchar_t *s2);
extern size_t wcsxfrm(wchar_t *s1, const wchar_t *s2, size_t n);

extern double wcstod(const wchar_t *nptr, wchar_t **endptr);
extern float wcstof(const wchar_t *nptr, wchar_t **endptr);
extern long double wcstold(const wchar_t *nptr, wchar_t **endptr);
extern long wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
extern long long wcstoll(const wchar_t *nptr, wchar_t **endptr, int base);
extern unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base);
extern unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr, int base);

extern int wctob(wint_t c);
extern wint_t btowc(int c);

#endif /* __WCHAR_H */
