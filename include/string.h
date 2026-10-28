/* string.h - string manipulation functions for CCCC C compiler */

#ifndef __STRING_H
#define __STRING_H

#include "stddef.h" /* C requires <string.h> to define NULL (and size_t) */

extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memmove(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern void *memchr(const void *s, int c, size_t n);

extern size_t strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, size_t n);
extern char *strcat(char *dest, const char *src);
extern char *strncat(char *dest, const char *src, size_t n);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);

/* C23 const-correct search functions: return type matches const-ness of input.
 * Macros use the parenthesized (funcname) form to call the actual function and
 * avoid recursive macro expansion. Declarations use the same form so the macro
 * is never applied to the declaration itself. */

#define strchr(s, c)                                                           \
    _Generic((s),                                                              \
        const char *: (const char *)(strchr)((s), (c)),                        \
        char *: (strchr)((s), (c)))
extern char *(strchr)(const char *s, int c);

#define strrchr(s, c)                                                          \
    _Generic((s),                                                              \
        const char *: (const char *)(strrchr)((s), (c)),                       \
        char *: (strrchr)((s), (c)))
extern char *(strrchr)(const char *s, int c);

#define strstr(haystack, needle)                                               \
    _Generic((haystack),                                                       \
        const char *: (const char *)(strstr)((haystack), (needle)),            \
        char *: (strstr)((haystack), (needle)))
extern char *(strstr)(const char *haystack, const char *needle);

#define strpbrk(s, accept)                                                     \
    _Generic((s),                                                              \
        const char *: (const char *)(strpbrk)((s), (accept)),                  \
        char *: (strpbrk)((s), (accept)))
extern char *(strpbrk)(const char *s, const char *accept);

extern size_t strspn(const char *s, const char *accept);
extern size_t strcspn(const char *s, const char *reject);

extern size_t strxfrm(char *dest, const char *src, size_t n);
extern int strcoll(const char *s1, const char *s2);
extern char *strerror(int errnum);
/* strsignal: printable description of a signal number, pointer into static
   storage (like strerror). POSIX.1-2008; present on both Darwin and glibc. */
extern char *strsignal(int sig);

/* extern char* strcpy_s(char *dest, size_t destsz, const char *src); */
/* extern char* strncpy_s(char *dest, size_t destsz, const char *src, size_t n);
 */
/* extern char* strcat_s(char *dest, size_t destsz, const char *src); */
/* extern char* strncat_s(char *dest, size_t destsz, const char *src, size_t n);
 */
extern char *strdup(const char *s);
extern char *strndup(const char *s, size_t n);
/* extern size_t strnlen_s(const char *s, size_t maxsize); */
extern char *strtok(char *restrict str, const char *restrict delim);
/* strtok_r: POSIX re-entrant strtok, caller-owned saveptr. Present on both
   Darwin and glibc. */
extern char *strtok_r(char *restrict str, const char *restrict delim,
                      char **restrict saveptr);
/* extern char* strtok_s(char *str, const char *delim, char **context); */
extern void *memset_explicit(void *s, int c, size_t n);
/* extern int memset_s(void *s, size_t smax, int c, size_t n); */
/* extern void* memcpy_s(void *dest, size_t destsz, const void *src, size_t n);
 */
/* extern void* memmove_s(void *dest, size_t destsz, const void *src, size_t n);
 */
extern void *memccpy(void *dest, const void *src, int c, size_t n);
/* BSD strlcpy/strlcat: Darwin and glibc >= 2.38 natively, older glibc via a
   VM fallback with the same semantics. */
extern size_t strlcpy(char *restrict dst, const char *restrict src, size_t n);
extern size_t strlcat(char *restrict dst, const char *restrict src, size_t n);
#ifdef __linux__
/* glibc-only; Darwin has memset_explicit (C23). */
extern void explicit_bzero(void *s, size_t n);
#endif
/* extern int strerror_s(char *buf, size_t bufsz, int errnum); */
/* extern size_t strerrorlen_s(int errnum); */

#endif /* __STRING_H */
