/* stddef.h - standard definitions for CCCC C compiler */

#ifndef __STDDEF_H
#define __STDDEF_H

typedef long          ptrdiff_t;
typedef unsigned long size_t;

/* wchar_t's signedness is an ABI choice, not a portable one: Linux aarch64
 * (AAPCS64) defines it unsigned, matching clang/gcc's own __WCHAR_TYPE__
 * there; every other platform CCCC supports (macOS arm64/x86_64, Linux
 * x86_64) keeps it signed int. A hardcoded `int` here compiled fine on
 * three of four platforms and only failed loudly under -c=native/-m on
 * Linux aarch64, where the replayed real host <stddef.h> redefines wchar_t
 * as unsigned int -- 'typedef redefinition with different types'. */
#if defined(__linux__) && defined(__aarch64__)
typedef unsigned int wchar_t;
#else
typedef int wchar_t;
#endif

#if __STDC_VERSION__ >= 202311L
typedef typeof(nullptr) nullptr_t;
#define unreachable() __builtin_unreachable()
#endif

#define NULL ((void *)0)

/* offsetof macro - works with standard layout structs */
#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif /* __STDDEF_H */
