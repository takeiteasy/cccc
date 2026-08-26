/* uchar.h - Unicode character conversion declarations for CCCC */

#ifndef __UCHAR_H
#define __UCHAR_H

#include "stddef.h"
#include "wchar.h"

typedef unsigned char char8_t; /* C23 §7.28 */
typedef unsigned short char16_t;
typedef unsigned int char32_t;

extern size_t mbrtoc8(char8_t *pc8, const char *s, size_t n, mbstate_t *ps);
extern size_t c8rtomb(char *s, char8_t c8, mbstate_t *ps);
extern size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps);
extern size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps);
extern size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);
extern size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps);

#endif /* __UCHAR_H */
