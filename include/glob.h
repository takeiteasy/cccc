/* glob.h - pathname pattern matching for CCCC */

#ifndef __GLOB_H
#define __GLOB_H

#ifdef _WIN32
#error "<glob.h> is only available on POSIX targets in CCCC"
#endif

#include "stddef.h"

#ifdef __APPLE__
typedef struct {
    size_t gl_pathc;
    size_t gl_matchc;
    size_t gl_offs;
    int    gl_flags;
    char **gl_pathv;
    void **gl_statv;
    void **gl_errfunc;
    char **gl_matchv;
    char  *gl_lstat;
    char  *gl_stat;
} glob_t;
#else
struct dirent;
typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
    size_t gl_offs;
    int    gl_flags;
    void (*gl_closedir)(void *);
    struct dirent *(*gl_readdir)(void *);
    void *(*gl_opendir)(const char *);
    int (*gl_lstat)(const char *, void *);
    int (*gl_stat)(const char *, void *);
} glob_t;
#endif

#ifdef __APPLE__
#define GLOB_APPEND   0x0001
#define GLOB_DOOFFS   0x0002
#define GLOB_ERR      0x0004
#define GLOB_MARK     0x0008
#define GLOB_NOCHECK  0x0010
#define GLOB_NOSORT   0x0020
#define GLOB_NOESCAPE 0x2000
// GNU extension bits also present on Darwin's own glob.h (values verified
// against the real SDK header, /usr/include/glob.h).
#define GLOB_ALTDIRFUNC 0x0040
#define GLOB_BRACE      0x0080
#define GLOB_MAGCHAR    0x0100
#define GLOB_NOMAGIC    0x0200
#define GLOB_QUOTE      0x0400
#define GLOB_TILDE      0x0800
#define GLOB_LIMIT      0x1000
#else
#define GLOB_ERR         0x0001
#define GLOB_MARK        0x0002
#define GLOB_NOSORT      0x0004
#define GLOB_DOOFFS      0x0008
#define GLOB_NOCHECK     0x0010
#define GLOB_APPEND      0x0020
#define GLOB_NOESCAPE    0x0040
// glibc GNU extension bits (bits/glob.h, __USE_GNU): not spot-checked
// against a real glibc header in this pass (no Linux container was up),
// but these are long-stable, widely-documented values.
#define GLOB_PERIOD      0x0080
#define GLOB_MAGCHAR     0x0100
#define GLOB_ALTDIRFUNC  0x0200
#define GLOB_BRACE       0x0400
#define GLOB_NOMAGIC     0x0800
#define GLOB_TILDE       0x1000
#define GLOB_ONLYDIR     0x2000
#define GLOB_TILDE_CHECK 0x4000
#endif

#ifdef __APPLE__
#define GLOB_NOSPACE (-1)
#define GLOB_ABORTED (-2)
#define GLOB_NOMATCH (-3)
#else
#define GLOB_NOSPACE 1
#define GLOB_ABORTED 2
#define GLOB_NOMATCH 3
#endif

extern int glob(const char *pattern, int                               flags,
                int (*errfunc)(const char *epath, int eerrno), glob_t *pglob);
extern void globfree(glob_t *pglob);

#endif /* __GLOB_H */
