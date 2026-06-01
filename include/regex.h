/* regex.h - regular expression declarations for JCC */

#ifndef __REGEX_H
#define __REGEX_H

#ifdef _WIN32
#error "<regex.h> is only available on POSIX targets in JCC"
#endif

#include "stddef.h"

typedef long regoff_t;

#ifdef __APPLE__
typedef struct {
    int re_magic;
    size_t re_nsub;
    const char *re_endp;
    void *re_g;
} regex_t;
#else
typedef struct {
    void *__buffer;
    unsigned long __allocated;
    unsigned long __used;
    unsigned long __syntax;
    char *__fastmap;
    char *__translate;
    size_t re_nsub;
    unsigned int __flags;
} regex_t;
#endif

typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

#define REG_EXTENDED 1
#define REG_ICASE    2
#define REG_NOSUB    4
#define REG_NEWLINE  8

#define REG_NOTBOL 1
#define REG_NOTEOL 2

#define REG_NOMATCH 1

extern int regcomp(regex_t *preg, const char *regex, int cflags);
extern int regexec(const regex_t *preg, const char *string, size_t nmatch,
                   regmatch_t pmatch[], int eflags);
extern size_t regerror(int errcode, const regex_t *preg, char *errbuf,
                       size_t errbuf_size);
extern void regfree(regex_t *preg);

#endif /* __REGEX_H */
