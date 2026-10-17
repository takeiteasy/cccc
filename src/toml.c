/*
 CCCC: Comprehensiev C Compensation Compiler

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

#include "toml.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail_at(const char *file, int line, const char *fmt, va_list ap) {
    fprintf(stderr, "error: %s", file);
    if (line)
        fprintf(stderr, ":%d", line);
    fputs(": ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    exit(1);
}

void cccc_toml_fail(toml_datum_t at, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fail_at(at.source ? at.source : "(toml)", at.lineno, fmt, ap);
    va_end(ap);
}

toml_result_t cccc_toml_parse_text(const char *name, const char *src,
                                   size_t len) {
    toml_result_t r = toml_parse_named(src, (int)len, name);
    if (!r.ok) {
        // The parser words its errors "(line N) message".
        int         line  = 0;
        const char *msg   = r.errmsg;
        const char *close = strchr(msg, ')');
        if (close && sscanf(msg, "(line %d)", &line) == 1)
            msg = close + 1 + (close[1] == ' ');
        toml_datum_t at = {.source = name, .lineno = line};
        cccc_toml_fail(at, "%s", msg);
    }
    return r;
}

char *cccc_toml_read(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot read %s\n", path);
        exit(1);
    }
    size_t cap = 4096, n = 0;
    char  *buf = malloc(cap);
    size_t got;
    while (buf && (got = fread(buf + n, 1, cap - n, f)) > 0) {
        n += got;
        if (n == cap) {
            cap *= 2;
            buf  = realloc(buf, cap);
        }
    }
    bool bad = !buf || ferror(f);
    fclose(f);
    if (bad) {
        fprintf(stderr, "error: cannot read %s\n", path);
        exit(1);
    }
    *len = n;
    return buf;
}

toml_result_t cccc_toml_load(const char *path) {
    size_t        len;
    char         *src = cccc_toml_read(path, &len);
    toml_result_t r   = cccc_toml_parse_text(path, src, len);
    free(src);
    return r;
}

// C0 controls other than tab, DEL, and the C1 controls (U+0080..U+009F, which
// some terminals read as escape introducers).
static bool text_safe(const char *s, int len) {
    if ((int)strlen(s) != len)
        return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p < 0x20 && *p != '\t') || *p == 0x7f)
            return false;
        if (p[0] == 0xc2 && p[1] >= 0x80 && p[1] <= 0x9f)
            return false;
    }
    return true;
}

void cccc_toml_check_text(toml_datum_t d) {
    switch (d.type) {
        case TOML_STRING:
            if (!text_safe(d.u.str.ptr, d.u.str.len))
                cccc_toml_fail(d, "control characters are not allowed");
            break;
        case TOML_ARRAY:
            for (int i = 0; i < d.u.arr.size; i++)
                cccc_toml_check_text(d.u.arr.elem[i]);
            break;
        case TOML_TABLE:
            for (int i = 0; i < d.u.tab.size; i++) {
                if (!text_safe(d.u.tab.key[i], d.u.tab.len[i]))
                    cccc_toml_fail(d.u.tab.value[i],
                                   "control characters are not allowed");
                cccc_toml_check_text(d.u.tab.value[i]);
            }
            break;
        default:
            break;
    }
}

const char *cccc_toml_type_name(toml_type_t type) {
    switch (type) {
        case TOML_STRING:
            return "string";
        case TOML_INT64:
            return "integer";
        case TOML_FP64:
            return "float";
        case TOML_BOOLEAN:
            return "boolean";
        case TOML_DATE:
        case TOML_TIME:
        case TOML_DATETIME:
        case TOML_DATETIMETZ:
            return "date or time";
        case TOML_ARRAY:
            return "array";
        case TOML_TABLE:
            return "table";
        default:
            return "unknown";
    }
}
