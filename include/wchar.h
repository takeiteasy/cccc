/* wchar.h - wide-character declarations for CCCC */

#ifndef __WCHAR_H
#define __WCHAR_H

#include "stddef.h"
#include <stdarg.h> /* #1070: angle-bracket for a correct #include_next hand-off under real GCC */
#include "time.h"

#ifndef WEOF
#define WEOF ((wint_t) - 1)
#endif

typedef unsigned int wint_t;

/* #1142: a full `#ifdef __CCCC__` / `#include_next <wchar.h>` hand-off */
/* (the fenv.h/#1021, pthread.h/#1022, sys/mount.h/#1031 shape) was tried */
/* here first and reverted -- confirmed by inspecting the real headers */
/* directly (macOS SDK's own sys/_wchar.h; glibc's wchar.h), NOT */
/* reasoned from first principles: both platforms' real <wchar.h> */
/* unconditionally #include <time.h> internally (for wcsftime's `struct */
/* tm`), and include/time.h has no hand-off of its own -- deliberately, */
/* per its own comment, after #1022's identical attempt at a full */
/* <time.h> hand-off dragged in far more than struct timespec and hit a */
/* clockid_t collision with no narrow fix. A real host compiler */
/* re-processing that #include_next chain hits CCCC's own bundled, */
/* non-hand-off time.h again via the same -I./include forwarding that */
/* found this file in the first place -- `struct tm`/`clock_t`/`time_t` */
/* redefinition, the identical cascade #1069's own __cccc_mb_cur_max */
/* shim comment (src/serialize.c) documents for the same reason a */
/* <stdlib.h> hand-off was rejected. Landing the full split would only */
/* trade #1103's already-fixed mbstate_t-layout mismatch for a strictly */
/* worse, unconditional compile failure on every host. */
/* */
/* Falling back instead to a narrow fix matching #1022's own */
/* `_STRUCT_TIMESPEC` precedent: guard just the `mbstate_t` typedef under */
/* the exact macro name each real host's own mbstate_t definition uses as */
/* its include guard (glibc: `__mbstate_t_defined`, */
/* bits/types/mbstate_t.h; Darwin: `_MBSTATE_T`, */
/* sys/_types/_mbstate_t.h -- verified directly against both). Nothing */
/* else in this file's own replay chain currently reaches either */
/* platform's real mbstate_t independently, so this doesn't yet prevent */
/* an active redefinition the way `_STRUCT_TIMESPEC` does for */
/* pthread.h+time.h today -- it preempts one, should some future header */
/* gain a hand-off that also reaches this TU's real mbstate_t, without */
/* this file's own 128-byte `__opaque` projection colliding with it. */
#ifdef __APPLE__
#define _MBSTATE_T
#else
#define __mbstate_t_defined 1
#endif
typedef struct {
    unsigned long long __opaque[16];
} mbstate_t;

extern int mbsinit(const mbstate_t *ps);
extern size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
extern size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
extern size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
extern size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len,
                        mbstate_t *ps);
extern size_t wcsrtombs(char *dst, const wchar_t **src, size_t len,
                        mbstate_t *ps);

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
extern unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr,
                                   int base);

extern int wctob(wint_t c);
extern wint_t btowc(int c);

#endif /* __WCHAR_H */
