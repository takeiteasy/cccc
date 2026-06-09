/* strings.h - POSIX string functions supplement for CCCC */

#ifndef __STRINGS_H
#define __STRINGS_H

#ifdef _WIN32
#error "<strings.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"

extern int strcasecmp(const char *s1, const char *s2);
extern int strncasecmp(const char *s1, const char *s2, size_t n);
extern void bzero(void *s, size_t n);
extern void bcopy(const void *src, void *dst, size_t n);
extern int bcmp(const void *s1, const void *s2, size_t n);
extern char *index(const char *s, int c);
extern char *rindex(const char *s, int c);

#endif /* __STRINGS_H */
