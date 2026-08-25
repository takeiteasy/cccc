/* Standard library function declarations for FFI */
/* This header provides declarations for C standard library functions */
/* that are registered in the VM's FFI and can be called directly */

#ifndef __STDLIB_H
#define __STDLIB_H

#include "stddef.h"

/* EXIT macros */
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

/* typedef unsigned int once_flag; */
/* extern void call_once(once_flag* flag, void (*func)(void)); */

extern double atof(const char *nptr);
extern int atoi(const char *nptr);
extern long int atol(const char *nptr);
extern long long int atoll(const char *nptr);
/* extern int strfromd(char* s, size_t n, const char* format, double fp); */
/* extern int strfromf(char* s, size_t n, const char* format, float fp); */
/* extern int strfroml(char* s, size_t n, const char* format, long double fp);
 */
extern double strtod(const char *nptr, char **endptr);
extern float strtof(const char *nptr, char **endptr);
extern long double strtold(const char *nptr, char **endptr);
extern long int strtol(const char *nptr, char **endptr, int base);
extern long long int strtoll(const char *nptr, char **endptr, int base);
extern unsigned long int strtoul(const char *nptr, char **endptr, int base);
extern unsigned long long int strtoull(const char *nptr, char **endptr,
                                       int base);

/* strtod32/64/128 (C23/TS 18661-2, tracker #832): parse a decimal out of a */
/* string, exact per IEEE 754-2008 (no round-trip through a binary double). */
/* Thin wrappers over __cccc_dec_strtod (src/stdlib/decimal.c's */
/* cccc_dec_strtod via src/stdlib/stdlib.c's FFI trampoline) -- no new */
/* opcode, same pattern <decimal_math.h>'s functions use. Decimal return by */
/* value from a guest static inline is already supported (struct-ABI reuse, */
/* see man/VM.md); no decimal value crosses the FFI boundary itself. */
#ifdef __STDC_IEC_60559_DFP__
extern long long __cccc_dec_strtod(long long w, long long dst, long long s,
                                   long long endp);
static inline _Decimal32 strtod32(const char *nptr, char **endptr) {
    _Decimal32 r;
    __cccc_dec_strtod(0, (long long)&r, (long long)nptr, (long long)endptr);
    return r;
}
static inline _Decimal64 strtod64(const char *nptr, char **endptr) {
    _Decimal64 r;
    __cccc_dec_strtod(1, (long long)&r, (long long)nptr, (long long)endptr);
    return r;
}
static inline _Decimal128 strtod128(const char *nptr, char **endptr) {
    _Decimal128 r;
    __cccc_dec_strtod(2, (long long)&r, (long long)nptr, (long long)endptr);
    return r;
}
#endif

extern int rand(void);
extern void srand(unsigned int seed);
/* alloc_size/malloc (#649): self-describing sizes for __builtin_object_size, */
/* generalizing #642's hardcoded name-based malloc-family detection. realloc */
/* and reallocarray are not annotated `malloc` since they may return the same */
/* block as their input pointer (not a fresh, non-aliasing allocation). */
extern void *aligned_alloc(size_t alignment, size_t size)
    __attribute__((alloc_size(2), malloc));
extern void *calloc(size_t nmemb, size_t size)
    __attribute__((alloc_size(1, 2), malloc));
extern void free(void *ptr);
extern void free_sized(void *ptr, size_t size);
extern void free_aligned_sized(void *ptr, size_t alignment, size_t size);
extern void *malloc(size_t size) __attribute__((alloc_size(1), malloc));
extern void *realloc(void *ptr, size_t size) __attribute__((alloc_size(2)));
/* (#699) Routed through the VM heap's overflow-checked REALCA opcode by */
/* default, or the cccc_reallocarray polyfill on the -V/--no-vm-heap host */
/* allocator path -- not a native host FFI call, since not every libc */
/* provides reallocarray (e.g. this SDK's macOS does not). */
extern void *reallocarray(void *ptr, size_t nmemb, size_t size)
    __attribute__((alloc_size(2, 3)));
extern void abort(void);
extern int atexit(void (*func)(void));
extern int at_quick_exit(void (*func)(void));
extern void exit(int status);
extern void _Exit(int status);
extern char *getenv(const char *name);
extern int setenv(const char *name, const char *value, int overwrite);
extern int unsetenv(const char *name);
extern int putenv(char *string);
extern void quick_exit(int status);
extern int system(const char *string);
extern int posix_memalign(void **memptr, size_t alignment, size_t size);
extern void *bsearch(const void *key, void *base, size_t nmemb, size_t size,
                     int (*compar)(const void *, const void *));
extern void qsort(void *base, size_t nmemb, size_t size,
                  int (*compar)(const void *, const void *));
extern int abs(int j);
extern long int labs(long int j);
extern long long int llabs(long long int j);

typedef struct {
    int quot;
    int rem;
} div_t;
typedef struct {
    long quot;
    long rem;
} ldiv_t;
typedef struct {
    long long quot;
    long long rem;
} lldiv_t;

extern div_t div(int numer, int denom);
extern ldiv_t ldiv(long int numer, long int denom);
extern lldiv_t lldiv(long long int numer, long long int denom);

extern int mblen(const char *s, size_t n);
extern int mbtowc(wchar_t *pwc, const char *s, size_t n);
extern int wctomb(char *s, wchar_t wc);

extern size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n);
extern size_t wcstombs(char *s, const wchar_t *pwcs, size_t n);
extern size_t memalignment(const void *p);

/* MB_CUR_MAX (C17 7.22.1, #1069): the max multibyte-char length for the */
/* *current* locale -- unlike MB_LEN_MAX (limits.h, #1067, a compile-time */
/* upper bound), this is genuinely runtime/locale-dependent, so it can't be */
/* a plain macro constant. mblen/mbtowc/wctomb/mbstowcs/wcstombs above are */
/* already real host FFI passthroughs (src/stdlib/stdlib.c), and setlocale */
/* (locale.h) is too (src/stdlib/locale.c) -- CCCC itself never calls */
/* setlocale, so the host process's locale already is the guest's, and */
/* reading the host's own MB_CUR_MAX through an accessor shim (same */
/* __cccc_stdout/__cccc_errno_ptr pattern as stdio.h/errno.h) is correct */
/* with no separate locale model needed. */
/* */
/* Unlike stdout/errno/FLT_ROUNDS, the shim's own definition */
/* (native_accessor_shims, src/serialize.c) does NOT reach the real */
/* MB_CUR_MAX by `#include`-ing this header a second time: an earlier */
/* attempt gave this header its own #ifdef __CCCC__ / #include_next */
/* hand-off (the #1018/stdio.h/errno.h/fenv.h/math.h pattern) so the */
/* shim's body could see the real host's own MB_CUR_MAX -- but that */
/* #include_next chain, followed all the way down through the real host's */
/* own <stdlib.h>, reaches other system headers (sys/types.h, and via it */
/* sys/time.h) that CCCC also bundles its own unconditional (non-hand-off) */
/* copies of, still shadowed by this same -I./include forwarding (#1054's */
/* exact hazard, just one level deeper) -- e.g. real macOS's own */
/* <_stdlib.h> pulls in <sys/time.h>, CCCC's own copy of which */
/* unconditionally #includes CCCC's own top-level time.h and defines its */
/* own `clock_t`, later colliding with the real host's `clock_t` once */
/* sys/types.h's own hand-off reaches it too ("typedef redefinition"). */
/* Widening the hand-off to every header transitively reachable this way */
/* has no clear stopping point, so the shim instead spells the host's */
/* internal accessor directly (glibc: __ctype_get_mb_cur_max(), a function */
/* call; macOS: __mb_cur_max, a plain global) rather than pulling in a */
/* second copy of <stdlib.h> at all -- see native_accessor_shims for both */
/* declarations, verified against the real headers on both hosts. */
/* */
/* #1063: this exact file is also what a native re-emission's replayed */
/* `#include <stdlib.h>` resolves to (-I./include is searched ahead of the */
/* system dirs) -- an unconditional `extern` here would conflict with the */
/* `static` definition native_accessor_shims emits once the shim is */
/* actually used ("static declaration follows non-static declaration"), */
/* the same trap __cccc_errno_ptr/issignaling_f/etc already document. */
/* Guarded on __CCCC__ (always defined while CCCC's own preprocessor */
/* parses guest source; its absence means a genuine host compiler is */
/* reprocessing this file during serializer replay) -- no #include_next */
/* fallback needed, since the shim's own static body is a complete */
/* definition that also serves as its own prototype. */
#ifdef __CCCC__
extern size_t __cccc_mb_cur_max(void);
#define MB_CUR_MAX (__cccc_mb_cur_max())
#endif

#endif
