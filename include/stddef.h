/* stddef.h - standard definitions for CCCC C compiler */

#ifndef __STDDEF_H
#define __STDDEF_H

typedef long ptrdiff_t;
typedef unsigned long size_t;
typedef int wchar_t;

#if __STDC_VERSION__ >= 202311L
typedef typeof(nullptr) nullptr_t;
#define unreachable() __builtin_unreachable()
#endif

#define NULL ((void*)0)

/* offsetof macro - works with standard layout structs */
#define offsetof(type, member) ((size_t) &(((type *)0)->member))

#endif /* __STDDEF_H */
