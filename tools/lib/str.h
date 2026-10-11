#ifndef TOOLS_LIB_STR_H
#define TOOLS_LIB_STR_H

#include <stddef.h>
#include "tool.h"

typedef struct {
    char  *data; // always NUL-terminated once anything is appended
    size_t len, cap;
} StrBuf;

typedef struct {
    char **v;
    size_t n;
    char  *storage;
} Lines;

void sb_reserve(StrBuf *sb, size_t extra);
void sb_append_n(StrBuf *sb, const char *s, size_t n);
void sb_append(StrBuf *sb, const char *s);
void sb_appendf(StrBuf *sb, const char *fmt, ...);
void sb_free(StrBuf *sb);

int str_is_space(unsigned char c); // ASCII whitespace, like Python str.isspace
int str_starts_with(const char *s, const char *prefix);
int str_is_blank(const char *s); // only ASCII whitespace (Python strip() == "")
// Python str.strip() for ASCII whitespace; returns a malloc'd copy.
char *str_strip_dup(const char *s);
// Python text.split("\n"): "a\nb\n" -> {"a", "b", ""}. Lines are NUL-terminated
// slices of one private buffer; release with lines_free.
Lines str_split_lines(const char *text);
void lines_free(Lines *l);

#endif
