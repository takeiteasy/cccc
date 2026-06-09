/* stddef.h - standard definitions for CCCC C compiler */

#ifndef __STDDEF_H
#define __STDDEF_H

typedef long ptrdiff_t;
typedef long size_t;
typedef int wchar_t;

#define NULL ((void*)0)

/* offsetof macro - works with standard layout structs */
#define offsetof(type, member) ((size_t) &(((type *)0)->member))

#endif /* __STDDEF_H */
