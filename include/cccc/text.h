/*
 CCCC: Comprehensiev C Compensation Compiler - text reader runtime

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// Runtime behind a struct's @from_string attribute (cccc/derive.h): it reads
// the text @dump and @to_string write. Include it next to derive.h; the
// generated functions call the __cccc_text_* helpers below, and nothing here is
// meant to be called directly. Every reader returns 0 on success.

#ifndef CCCC_TEXT_H
#define CCCC_TEXT_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
} __cccc_text_r;

static void __cccc_text_r_init(__cccc_text_r *r, const char *s) {
    r->p = s;
}

static void __cccc_text_r_ws(__cccc_text_r *r) {
    while (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')
        r->p++;
}

// Consumes `c` (after whitespace).
static int __cccc_text_r_open(__cccc_text_r *r, char c) {
    __cccc_text_r_ws(r);
    if (*r->p != c)
        return 1;
    r->p++;
    return 0;
}

// True when only whitespace is left.
static int __cccc_text_r_at_end(__cccc_text_r *r) {
    __cccc_text_r_ws(r);
    return *r->p == 0;
}

static int __cccc_text_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int __cccc_text_ident_char(char c) {
    return __cccc_text_ident_start(c) || (c >= '0' && c <= '9');
}

// Reads an identifier into name; fails when it does not fit.
static int __cccc_text_r_ident(__cccc_text_r *r, char *name, size_t cap) {
    __cccc_text_r_ws(r);
    if (!__cccc_text_ident_start(*r->p))
        return 1;
    size_t n = 0;
    while (__cccc_text_ident_char(r->p[n])) {
        if (n + 1 >= cap)
            return 1;
        name[n] = r->p[n];
        n++;
    }
    name[n]  = 0;
    r->p    += n;
    return 0;
}

// Skips the type name in front of a struct's '{', if there is one.
static void __cccc_text_r_label(__cccc_text_r *r) {
    __cccc_text_r_ws(r);
    while (__cccc_text_ident_char(*r->p))
        r->p++;
}

// Consumes `word` when it is next and not the start of a longer identifier.
static int __cccc_text_r_lit(__cccc_text_r *r, const char *word) {
    __cccc_text_r_ws(r);
    size_t n = strlen(word);
    if (strncmp(r->p, word, n) || __cccc_text_ident_char(r->p[n]))
        return 1;
    r->p += n;
    return 0;
}

static int __cccc_text_r_bool(__cccc_text_r *r, int *out) {
    if (!__cccc_text_r_lit(r, "true")) {
        *out = 1;
        return 0;
    }
    if (!__cccc_text_r_lit(r, "false")) {
        *out = 0;
        return 0;
    }
    return 1;
}

// Digits of an unsigned number up to `limit`.
static int __cccc_text_digits(__cccc_text_r *r, unsigned long long limit,
                              unsigned long long *out) {
    const char *p = r->p;
    if (*p < '0' || *p > '9')
        return 1;
    unsigned long long v = 0;
    for (; *p >= '0' && *p <= '9'; p++) {
        unsigned d = (unsigned)(*p - '0');
        if (v > (limit - d) / 10)
            return 1;
        v = v * 10 + d;
    }
    r->p = p;
    *out = v;
    return 0;
}

// Integer of `size` bytes.
static int __cccc_text_r_int(__cccc_text_r *r, long long *out, int size) {
    __cccc_text_r_ws(r);
    int neg = *r->p == '-';
    if (neg)
        r->p++;
    unsigned long long limit =
        size >= 8 ? 0x7FFFFFFFFFFFFFFFULL : (1ULL << (size * 8 - 1)) - 1;
    unsigned long long v;
    if (__cccc_text_digits(r, limit + (unsigned)neg, &v))
        return 1;
    *out = neg ? (long long)(0 - v) : (long long)v;
    return 0;
}

static int __cccc_text_r_uint(__cccc_text_r *r, unsigned long long *out,
                              int size) {
    __cccc_text_r_ws(r);
    unsigned long long limit = size >= 8 ? ~0ULL : (1ULL << (size * 8)) - 1;
    return __cccc_text_digits(r, limit, out);
}

// The extent of a floating-point token: digits, sign, '.', exponent, and the
// words inf and nan.
static size_t __cccc_text_float_token(__cccc_text_r *r, char *tmp, size_t cap) {
    __cccc_text_r_ws(r);
    const char *p = r->p;
    size_t      n = 0;
    while ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.' ||
           (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
        if (n + 1 >= cap)
            return 0;
        tmp[n++] = *p++;
    }
    tmp[n] = 0;
    return n;
}

static int __cccc_text_r_f64(__cccc_text_r *r, double *out) {
    char   tmp[64];
    size_t n = __cccc_text_float_token(r, tmp, sizeof tmp);
    if (!n)
        return 1;
    char *end;
    *out = strtod(tmp, &end);
    if (*end)
        return 1;
    r->p += n;
    return 0;
}

static int __cccc_text_r_ldouble(__cccc_text_r *r, long double *out) {
    char   tmp[64];
    size_t n = __cccc_text_float_token(r, tmp, sizeof tmp);
    if (!n)
        return 1;
    char *end;
    *out = strtold(tmp, &end);
    if (*end)
        return 1;
    r->p += n;
    return 0;
}

// Reads one byte of a quoted string, undoing the escapes @dump writes: \"
// \\ \n \t \r and \ooo. A raw NUL, an unterminated string and \000 fail.
static int __cccc_text_str_byte(__cccc_text_r *r, int *c) {
    unsigned char b = (unsigned char)*r->p;
    if (b == 0)
        return 1;
    r->p++;
    if (b != '\\') {
        *c = b;
        return 0;
    }
    char e = *r->p++;
    if (e == '"' || e == '\\')
        *c = (unsigned char)e;
    else if (e == 'n')
        *c = '\n';
    else if (e == 't')
        *c = '\t';
    else if (e == 'r')
        *c = '\r';
    else if (e >= '0' && e <= '7') {
        int v = e - '0';
        for (int i = 0; i < 2 && r->p[0] >= '0' && r->p[0] <= '7'; i++)
            v = v * 8 + (*r->p++ - '0');
        if (v == 0 || v > 255)
            return 1;
        *c = v;
    } else
        return 1;
    return 0;
}

// Reads a quoted string into a char array of `cap` bytes. A string that fills
// the array exactly has no NUL, as @dump prints it; a shorter one is padded
// with zeros.
static int __cccc_text_r_str(__cccc_text_r *r, char *dst, size_t cap) {
    if (__cccc_text_r_open(r, '"'))
        return 1;
    size_t n = 0;
    while (*r->p != '"') {
        int c;
        if (__cccc_text_str_byte(r, &c) || n >= cap)
            return 1;
        dst[n++] = (char)c;
    }
    r->p++;
    memset(dst + n, 0, cap - n);
    return 0;
}

// Reads a quoted string into a new malloc'd buffer (*out), which the caller
// frees.
static int __cccc_text_r_new_str(__cccc_text_r *r, char **out) {
    __cccc_text_r_ws(r);
    if (*r->p != '"')
        return 1;
    const char *q = r->p + 1;
    while (*q != '"') {
        if (*q == 0)
            return 1;
        q += (*q == '\\' && q[1]) ? 2 : 1;
    }
    // Decoding never makes the text longer, so the raw span bounds it.
    char *s = malloc((size_t)(q - r->p));
    if (!s)
        return 1;
    r->p++;
    size_t n = 0;
    while (*r->p != '"') {
        int c;
        if (__cccc_text_str_byte(r, &c)) {
            free(s);
            return 1;
        }
        s[n++] = (char)c;
    }
    r->p++;
    s[n] = 0;
    *out = s;
    return 0;
}

// ---- Helpers for the generated struct reader
// ---------------------------------

// Next member of a struct whose '{' is already consumed. Returns 1 with the
// name in `name` and the '=' consumed, 0 at the closing '}' (consumed), -1 on
// error. *first is 1 before the first call.
static int __cccc_text_r_key(__cccc_text_r *r, char *name, size_t cap,
                             int *first) {
    __cccc_text_r_ws(r);
    if (*r->p == '}') {
        r->p++;
        return 0;
    }
    if (!*first && __cccc_text_r_open(r, ','))
        return -1;
    *first = 0;
    if (__cccc_text_r_ident(r, name, cap) || __cccc_text_r_open(r, '='))
        return -1;
    return 1;
}

static void __cccc_text_zero(unsigned char *p, int n) {
    for (int i = 0; i < n; i++)
        p[i] = 0;
}

static int __cccc_text_all_set(const unsigned char *p, int n) {
    for (int i = 0; i < n; i++)
        if (!p[i])
            return 0;
    return 1;
}

static int __cccc_text_key_is(const char *key, const char *name) {
    return strcmp(key, name) == 0;
}

#endif // CCCC_TEXT_H
