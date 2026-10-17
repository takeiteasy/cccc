/*
 CCCC: Comprehensiev C Compensation Compiler - JSON runtime

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

// Runtime behind the @to_json / @from_json attributes (cccc/derive.h). Include
// it next to derive.h; the generated functions call the __cccc_json_* helpers
// below, and nothing here is meant to be called directly.

#ifndef CCCC_JSON_H
#define CCCC_JSON_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- Writer ----------------------------------------------------------------
// snprintf semantics: output past `cap` is counted in `len` but not stored.

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} __cccc_json_w;

static void __cccc_json_w_init(__cccc_json_w *w, char *buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
}

static void __cccc_json_w_ch(__cccc_json_w *w, char c) {
    if (w->len + 1 < w->cap)
        w->buf[w->len] = c;
    w->len++;
}

static void __cccc_json_w_raw(__cccc_json_w *w, const char *s) {
    while (*s)
        __cccc_json_w_ch(w, *s++);
}

// A string of at most `max` bytes (shorter if it holds a NUL), escaped.
static void __cccc_json_w_str(__cccc_json_w *w, const char *s, size_t max) {
    __cccc_json_w_ch(w, '"');
    for (size_t i = 0; i < max && s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            __cccc_json_w_ch(w, '\\');
            __cccc_json_w_ch(w, (char)c);
        } else if (c == '\n')
            __cccc_json_w_raw(w, "\\n");
        else if (c == '\r')
            __cccc_json_w_raw(w, "\\r");
        else if (c == '\t')
            __cccc_json_w_raw(w, "\\t");
        else if (c == '\b')
            __cccc_json_w_raw(w, "\\b");
        else if (c == '\f')
            __cccc_json_w_raw(w, "\\f");
        else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof esc, "\\u%04x", c);
            __cccc_json_w_raw(w, esc);
        } else
            __cccc_json_w_ch(w, (char)c);
    }
    __cccc_json_w_ch(w, '"');
}

// `"name":`
static void __cccc_json_w_key(__cccc_json_w *w, const char *name) {
    __cccc_json_w_str(w, name, (size_t)-1);
    __cccc_json_w_ch(w, ':');
}

static void __cccc_json_w_i64(__cccc_json_w *w, long long v) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%lld", v);
    __cccc_json_w_raw(w, tmp);
}

static void __cccc_json_w_u64(__cccc_json_w *w, unsigned long long v) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%llu", v);
    __cccc_json_w_raw(w, tmp);
}

// NaN and infinities have no JSON spelling and are written as null.
static void __cccc_json_w_f64(__cccc_json_w *w, double v) {
    if (v != v || v - v != 0) {
        __cccc_json_w_raw(w, "null");
        return;
    }
    char tmp[40];
    snprintf(tmp, sizeof tmp, "%.17g", v);
    __cccc_json_w_raw(w, tmp);
}

static void __cccc_json_w_bool(__cccc_json_w *w, int v) {
    __cccc_json_w_raw(w, v ? "true" : "false");
}

// NUL-terminates and returns the length the whole output needs.
static size_t __cccc_json_w_end(__cccc_json_w *w) {
    if (w->cap)
        w->buf[w->len < w->cap ? w->len : w->cap - 1] = 0;
    return w->len;
}

// ---- Reader ----------------------------------------------------------------
// Every reader returns 0 on success.

typedef struct {
    const char *p;
} __cccc_json_r;

static void __cccc_json_r_init(__cccc_json_r *r, const char *json) {
    r->p = json;
}

static void __cccc_json_r_ws(__cccc_json_r *r) {
    while (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')
        r->p++;
}

// Consumes `c` (after whitespace).
static int __cccc_json_r_open(__cccc_json_r *r, char c) {
    __cccc_json_r_ws(r);
    if (*r->p != c)
        return 1;
    r->p++;
    return 0;
}

// True when only whitespace is left.
static int __cccc_json_r_at_end(__cccc_json_r *r) {
    __cccc_json_r_ws(r);
    return *r->p == 0;
}

static int __cccc_json_hex4(const char *s, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (unsigned)(c - 'A' + 10);
        else
            return 1;
    }
    *out = v;
    return 0;
}

// Appends the UTF-8 encoding of `cp` at *n, counting past `cap`.
static void __cccc_json_put_utf8(char *dst, size_t cap, size_t *n,
                                 unsigned cp) {
    char   tmp[4];
    size_t len;
    if (cp < 0x80) {
        tmp[0] = (char)cp;
        len    = 1;
    } else if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        len    = 2;
    } else if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        len    = 3;
    } else {
        tmp[0] = (char)(0xF0 | (cp >> 18));
        tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[3] = (char)(0x80 | (cp & 0x3F));
        len    = 4;
    }
    for (size_t i = 0; i < len; i++) {
        if (*n + 1 < cap)
            dst[*n] = tmp[i];
        (*n)++;
    }
}

// Reads a string into dst (NUL-terminated). With `truncate` an over-long
// string is cut to fit; otherwise it is an error. dst may be NULL with cap 0
// to skip a string.
static int __cccc_json_r_parse_str(__cccc_json_r *r, char *dst, size_t cap,
                                   int truncate) {
    if (__cccc_json_r_open(r, '"'))
        return 1;
    size_t n = 0;
    for (;;) {
        unsigned char c = (unsigned char)*r->p;
        if (c == 0 || c < 0x20)
            return 1;
        r->p++;
        if (c == '"')
            break;
        if (c != '\\') {
            if (dst && n + 1 < cap)
                dst[n] = (char)c;
            n++;
            continue;
        }
        char e = *r->p++;
        unsigned cp;
        if (e == '"' || e == '\\' || e == '/')
            cp = (unsigned char)e;
        else if (e == 'n')
            cp = '\n';
        else if (e == 'r')
            cp = '\r';
        else if (e == 't')
            cp = '\t';
        else if (e == 'b')
            cp = '\b';
        else if (e == 'f')
            cp = '\f';
        else if (e == 'u') {
            if (__cccc_json_hex4(r->p, &cp))
                return 1;
            r->p += 4;
            if (cp >= 0xD800 && cp < 0xDC00) {
                unsigned lo;
                if (r->p[0] != '\\' || r->p[1] != 'u' ||
                    __cccc_json_hex4(r->p + 2, &lo) || lo < 0xDC00 ||
                    lo > 0xDFFF)
                    return 1;
                r->p += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF)
                return 1;
        } else
            return 1;
        if (dst)
            __cccc_json_put_utf8(dst, cap, &n, cp);
        else
            n++;
    }
    if (dst && cap) {
        if (n + 1 > cap && !truncate)
            return 1;
        dst[n < cap ? n : cap - 1] = 0;
    }
    return 0;
}

static int __cccc_json_r_str(__cccc_json_r *r, char *dst, size_t cap) {
    return __cccc_json_r_parse_str(r, dst, cap, 0);
}

// Reads a string into a new malloc'd buffer (*out), which the caller frees.
static int __cccc_json_r_new_str(__cccc_json_r *r, char **out) {
    __cccc_json_r_ws(r);
    if (*r->p != '"')
        return 1;
    const char *q = r->p + 1;
    while (*q != '"') {
        if ((unsigned char)*q < 0x20)
            return 1;
        q += (*q == '\\' && q[1]) ? 2 : 1;
    }
    // Decoding never makes the text longer, so the raw span bounds it.
    size_t cap = (size_t)(q - r->p);
    char  *s   = malloc(cap);
    if (!s)
        return 1;
    if (__cccc_json_r_parse_str(r, s, cap, 0)) {
        free(s);
        return 1;
    }
    *out = s;
    return 0;
}

// Next member of an object whose '{' is already consumed. Returns 1 with the
// key in `key` and the ':' consumed, 0 at the closing '}' (consumed), -1 on
// error. *first is 1 before the first call.
static int __cccc_json_r_key(__cccc_json_r *r, char *key, size_t cap,
                             int *first) {
    __cccc_json_r_ws(r);
    if (*r->p == '}') {
        r->p++;
        return 0;
    }
    if (!*first && __cccc_json_r_open(r, ','))
        return -1;
    *first = 0;
    if (__cccc_json_r_parse_str(r, key, cap, 1) || __cccc_json_r_open(r, ':'))
        return -1;
    return 1;
}

// The extent of a JSON number token copied into tmp; returns its length or 0.
static size_t __cccc_json_number_token(__cccc_json_r *r, char *tmp,
                                       size_t cap) {
    __cccc_json_r_ws(r);
    const char *p = r->p;
    size_t      n = 0;
    while ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.' ||
           *p == 'e' || *p == 'E') {
        if (n + 1 >= cap)
            return 0;
        tmp[n++] = *p++;
    }
    tmp[n] = 0;
    return n;
}

// Integer of `size` bytes, no fraction or exponent.
static int __cccc_json_r_int(__cccc_json_r *r, long long *out, int size) {
    char   tmp[32];
    size_t n = __cccc_json_number_token(r, tmp, sizeof tmp);
    if (!n)
        return 1;
    size_t i   = 0;
    int    neg = 0;
    if (tmp[0] == '-') {
        neg = 1;
        i++;
    }
    if (i == n || (tmp[i] == '0' && i + 1 < n))
        return 1;
    unsigned long long limit =
        size >= 8 ? 0x7FFFFFFFFFFFFFFFULL : (1ULL << (size * 8 - 1)) - 1;
    unsigned long long v = 0;
    for (; i < n; i++) {
        if (tmp[i] < '0' || tmp[i] > '9')
            return 1;
        unsigned d = (unsigned)(tmp[i] - '0');
        if (v > (limit + (unsigned)neg - d) / 10)
            return 1;
        v = v * 10 + d;
    }
    r->p += n;
    *out = neg ? (long long)(0 - v) : (long long)v;
    return 0;
}

static int __cccc_json_r_uint(__cccc_json_r *r, unsigned long long *out,
                              int size) {
    char   tmp[32];
    size_t n = __cccc_json_number_token(r, tmp, sizeof tmp);
    if (!n || (tmp[0] == '0' && n > 1))
        return 1;
    unsigned long long limit = size >= 8 ? ~0ULL : (1ULL << (size * 8)) - 1;
    unsigned long long v     = 0;
    for (size_t i = 0; i < n; i++) {
        if (tmp[i] < '0' || tmp[i] > '9')
            return 1;
        unsigned d = (unsigned)(tmp[i] - '0');
        if (v > (limit - d) / 10)
            return 1;
        v = v * 10 + d;
    }
    r->p += n;
    *out = v;
    return 0;
}

static int __cccc_json_r_f64(__cccc_json_r *r, double *out) {
    char   tmp[64];
    size_t n = __cccc_json_number_token(r, tmp, sizeof tmp);
    if (!n)
        return 1;
    char *end;
    *out = strtod(tmp, &end);
    if (*end)
        return 1;
    r->p += n;
    return 0;
}

static int __cccc_json_r_lit(__cccc_json_r *r, const char *word) {
    __cccc_json_r_ws(r);
    size_t n = strlen(word);
    if (strncmp(r->p, word, n))
        return 1;
    r->p += n;
    return 0;
}

static int __cccc_json_r_bool(__cccc_json_r *r, int *out) {
    if (!__cccc_json_r_lit(r, "true")) {
        *out = 1;
        return 0;
    }
    if (!__cccc_json_r_lit(r, "false")) {
        *out = 0;
        return 0;
    }
    return 1;
}

// Skips any value (used for object members the struct has no field for).
static int __cccc_json_r_skip_depth(__cccc_json_r *r, int depth) {
    if (depth > 64)
        return 1;
    __cccc_json_r_ws(r);
    char c = *r->p;
    if (c == '"')
        return __cccc_json_r_parse_str(r, NULL, 0, 0);
    if (c == '{') {
        r->p++;
        int first = 1;
        for (;;) {
            char key[1];
            __cccc_json_r_ws(r);
            if (*r->p == '}') {
                r->p++;
                return 0;
            }
            if (!first && __cccc_json_r_open(r, ','))
                return 1;
            first = 0;
            if (__cccc_json_r_parse_str(r, key, 0, 1) ||
                __cccc_json_r_open(r, ':') ||
                __cccc_json_r_skip_depth(r, depth + 1))
                return 1;
        }
    }
    if (c == '[') {
        r->p++;
        int first = 1;
        for (;;) {
            __cccc_json_r_ws(r);
            if (*r->p == ']') {
                r->p++;
                return 0;
            }
            if (!first && __cccc_json_r_open(r, ','))
                return 1;
            first = 0;
            if (__cccc_json_r_skip_depth(r, depth + 1))
                return 1;
        }
    }
    if (!__cccc_json_r_lit(r, "true") || !__cccc_json_r_lit(r, "false") ||
        !__cccc_json_r_lit(r, "null"))
        return 0;
    double d;
    return __cccc_json_r_f64(r, &d);
}

static int __cccc_json_r_skip(__cccc_json_r *r) {
    return __cccc_json_r_skip_depth(r, 0);
}

// ---- Helpers for the generated object reader --------------------------------

static void __cccc_json_zero(unsigned char *p, int n) {
    for (int i = 0; i < n; i++)
        p[i] = 0;
}

static int __cccc_json_all_set(const unsigned char *p, int n) {
    for (int i = 0; i < n; i++)
        if (!p[i])
            return 0;
    return 1;
}

static int __cccc_json_key_is(const char *key, const char *name) {
    return strcmp(key, name) == 0;
}

#endif // CCCC_JSON_H
