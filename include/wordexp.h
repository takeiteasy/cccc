/* wordexp.h - shell-like word expansion for CCCC
 *
 * wordexp_t is byte-identical on macOS and glibc ({ size_t we_wordc;
 * char **we_wordv; size_t we_offs; }, 24 bytes, verified against real
 * headers), so it's a plain pass-through -- no marshalling needed. The
 * WRDE_* flag and error constants diverge (WRDE_APPEND/WRDE_DOOFFS are
 * even swapped between the two), so those stay #ifdef __APPLE__-split,
 * same pattern as glob.h's GLOB_* constants.
 */

#ifndef __WORDEXP_H
#define __WORDEXP_H

#ifdef _WIN32
#error "<wordexp.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"

typedef struct {
    size_t we_wordc;
    char **we_wordv;
    size_t we_offs;
} wordexp_t;

#ifdef __APPLE__
#define WRDE_APPEND  0x01
#define WRDE_DOOFFS  0x02
#define WRDE_NOCMD   0x04
#define WRDE_REUSE   0x08
#define WRDE_SHOWERR 0x10
#define WRDE_UNDEF   0x20

#define WRDE_BADCHAR 1
#define WRDE_BADVAL  2
#define WRDE_CMDSUB  3
#define WRDE_NOSPACE 4
#define WRDE_NOSYS   5
#define WRDE_SYNTAX  6
#else
#define WRDE_DOOFFS  0x01
#define WRDE_APPEND  0x02
#define WRDE_NOCMD   0x04
#define WRDE_REUSE   0x08
#define WRDE_SHOWERR 0x10
#define WRDE_UNDEF   0x20

#define WRDE_NOSPACE 1
#define WRDE_BADCHAR 2
#define WRDE_BADVAL  3
#define WRDE_CMDSUB  4
#define WRDE_SYNTAX  5
#define WRDE_NOSYS   (-1)
#endif

extern int wordexp(const char *words, wordexp_t *we, int flags);
extern void wordfree(wordexp_t *we);

#endif /* __WORDEXP_H */
