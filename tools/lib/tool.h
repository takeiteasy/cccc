// Shared by every tools/lib module. Tools are single-TU and run interpreted
// (`<cccc> tools/x.c -- args`), so each tool #includes the lib .c files it
// uses.
#ifndef TOOLS_LIB_TOOL_H
#define TOOLS_LIB_TOOL_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static inline void tool_die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("tool: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static inline void *tool_xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p)
        tool_die("out of memory");
    return p;
}

static inline void *tool_xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p)
        tool_die("out of memory");
    return p;
}

#endif
