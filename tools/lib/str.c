#include "str.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void sb_reserve(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap)
        return;
    size_t cap = sb->cap ? sb->cap : 64;
    while (cap < sb->len + extra + 1)
        cap *= 2;
    sb->data = tool_xrealloc(sb->data, cap);
    sb->cap  = cap;
}

void sb_append_n(StrBuf *sb, const char *s, size_t n) {
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len           += n;
    sb->data[sb->len]  = '\0';
}

void sb_append(StrBuf *sb, const char *s) {
    sb_append_n(sb, s, strlen(s));
}

void sb_appendf(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        tool_die("vsnprintf failed");
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)n;
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

int str_starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int str_is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f';
}

int str_is_blank(const char *s) {
    for (; *s; s++)
        if (!str_is_space((unsigned char)*s))
            return 0;
    return 1;
}

char *str_strip_dup(const char *s) {
    while (*s && str_is_space((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && str_is_space((unsigned char)s[n - 1]))
        n--;
    char *out = tool_xmalloc(n + 1);
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

Lines str_split_lines(const char *text) {
    Lines  l;
    size_t len = strlen(text);
    l.storage  = tool_xmalloc(len + 1);
    memcpy(l.storage, text, len + 1);
    size_t n = 1;
    for (size_t i = 0; i < len; i++)
        if (text[i] == '\n')
            n++;
    l.v        = tool_xmalloc(n * sizeof(char *));
    l.n        = 0;
    l.v[l.n++] = l.storage;
    for (size_t i = 0; i < len; i++)
        if (l.storage[i] == '\n') {
            l.storage[i] = '\0';
            l.v[l.n++]   = l.storage + i + 1;
        }
    return l;
}

void lines_free(Lines *l) {
    free(l->v);
    free(l->storage);
    l->v       = NULL;
    l->storage = NULL;
    l->n       = 0;
}
