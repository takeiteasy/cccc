/* assert.h - assertion macro for CCCC C compiler */

#ifndef __ASSERT_H
#define __ASSERT_H

#include <stdio.h> // #1070: angle-bracket for a correct #include_next hand-off under real GCC
#include "stdlib.h"

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr)                                                           \
    ((expr) ? (void)0 : (puts("Assertion failed: " #expr), abort()))
#endif

#endif /* __ASSERT_H */
