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
#else
#define GLOB_ERR      0x0001
#define GLOB_MARK     0x0002
#define GLOB_NOSORT   0x0004
#define GLOB_DOOFFS   0x0008
#define GLOB_NOCHECK  0x0010
#define GLOB_APPEND   0x0020
#define GLOB_NOESCAPE 0x0040
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
