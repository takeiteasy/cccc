/* string.h - string manipulation functions for CCCC C compiler */

#ifndef __STRING_H
#define __STRING_H

extern void *memcpy(void *dest, const void *src, long n);
extern void *memmove(void *dest, const void *src, long n);
extern void *memset(void *s, int c, long n);
extern int memcmp(const void *s1, const void *s2, long n);

extern long strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, long n);
extern char *strcat(char *dest, const char *src);
extern char *strncat(char *dest, const char *src, long n);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, long n);
extern char *strchr(const char *s, int c);
extern char *strrchr(const char *s, int c);
extern char *strstr(const char *haystack, const char *needle);
extern long strxfrm(char *dest, const char *src, long n);
extern char* strerror(int errnum);

// extern char* strcpy_s(char *dest, long destsz, const char *src);
// extern char* strncpy_s(char *dest, long destsz, const char *src, long n);
// extern char* strcat_s(char *dest, long destsz, const char *src);
// extern char* strncat_s(char *dest, long destsz, const char *src, long n);
extern char* strdup(const char *s);
extern char* strndup(const char *s, long n);
// extern long strnlen_s(const char *s, long maxsize);
// extern char* strtok_s(char *str, const char *delim, char **context);
// extern void* memset_explicit(void *s, int c, long n);
// extern int memset_s(void *s, long smax, int c, long n);
// extern void* memcpy_s(void *dest, long destsz, const void *src, long n);
// extern void* memmove_s(void *dest, long destsz, const void *src, long n);
extern void* memccpy(void *dest, const void *src, int c, long n);
// extern int strerror_s(char *buf, long bufsz, int errnum);
// extern long strerrorlen_s(int errnum);

#endif /* __STRING_H */
