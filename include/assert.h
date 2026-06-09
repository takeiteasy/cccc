/* assert.h - assertion macro for CCCC C compiler */

#ifndef __ASSERT_H
#define __ASSERT_H

#include "stdio.h"
#include "stdlib.h"

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) ((expr) ? (void)0 : (puts("Assertion failed: " #expr), abort()))
#endif

#endif /* __ASSERT_H */
