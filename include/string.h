/* string.h - string manipulation functions for CCCC C compiler */

#ifndef __STRING_H
#define __STRING_H

extern void *memcpy(void *dest, const void *src, long n);
extern void *memmove(void *dest, const void *src, long n);
extern void *memset(void *s, int c, long n);
extern int memcmp(const void *s1, const void *s2, long n);
extern void *memchr(const void *s, int c, long n);

extern long strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, long n);
extern char *strcat(char *dest, const char *src);
extern char *strncat(char *dest, const char *src, long n);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, long n);

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

extern long strspn(const char *s, const char *accept);
extern long strcspn(const char *s, const char *reject);

extern long strxfrm(char *dest, const char *src, long n);
extern int strcoll(const char *s1, const char *s2);
extern char *strerror(int errnum);

/* extern char* strcpy_s(char *dest, long destsz, const char *src); */
/* extern char* strncpy_s(char *dest, long destsz, const char *src, long n); */
/* extern char* strcat_s(char *dest, long destsz, const char *src); */
/* extern char* strncat_s(char *dest, long destsz, const char *src, long n); */
extern char *strdup(const char *s);
extern char *strndup(const char *s, long n);
/* extern long strnlen_s(const char *s, long maxsize); */
/* extern char* strtok_s(char *str, const char *delim, char **context); */
extern void *memset_explicit(void *s, int c, long n);
/* extern int memset_s(void *s, long smax, int c, long n); */
/* extern void* memcpy_s(void *dest, long destsz, const void *src, long n); */
/* extern void* memmove_s(void *dest, long destsz, const void *src, long n); */
extern void *memccpy(void *dest, const void *src, int c, long n);
/* extern int strerror_s(char *buf, long bufsz, int errnum); */
/* extern long strerrorlen_s(int errnum); */

#endif /* __STRING_H */
