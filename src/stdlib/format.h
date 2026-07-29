// Internal declarations for the custom printf/scanf %b/%B engines (#394).
// Used on platforms where the host libc lacks %b/%B support (macOS,
// glibc < 2.35). See format_printf.c and format_scanf.c.
#ifndef CCCC_STDLIB_FORMAT_H
#define CCCC_STDLIB_FORMAT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// _Decimal32/64/128 printf/scanf integration (#829). Declared in
// src/internal.h too; mirrored here (rather than including internal.h)
// because format_printf.c/format_scanf.c/stb_sprintf.h only ever need this
// one small slice of the VM-wide internal API. Keep in sync with
// src/internal.h's CCCC_DECFMT_* / cccc_dec_format_ex / cccc_dec_from_string
// / CCCC_DEC_ENV_*.
#define CCCC_DECFMT_MINUS 1u
#define CCCC_DECFMT_PLUS  2u
#define CCCC_DECFMT_SPACE 4u
#define CCCC_DECFMT_ALT   8u
#define CCCC_DECFMT_ZERO  16u
#define CCCC_DEC_ENV_STATIC  0
#define CCCC_DEC_ENV_DYNAMIC 1
int  cccc_dec_format_ex(char *buf, size_t n, const void *val, int w, int conv,
                        unsigned flags, int field_width, int prec);
bool cccc_dec_from_string(int w, void *dst, const char *s, int env);

// ---- printf family (format_printf.c) ----
int cccc_vsnprintf(char *buf, size_t count, const char *fmt, va_list ap);
int cccc_vsprintf(char *buf, const char *fmt, va_list ap);
int cccc_vfprintf(FILE *stream, const char *fmt, va_list ap);

int cccc_printf(const char *fmt, ...);
int cccc_fprintf(FILE *stream, const char *fmt, ...);
int cccc_sprintf(char *buf, const char *fmt, ...);
int cccc_snprintf(char *buf, size_t count, const char *fmt, ...);

long long wrap_cccc_vprintf(const char *fmt, long long va_ptr);
long long wrap_cccc_vsprintf(char *str, const char *fmt, long long va_ptr);
long long wrap_cccc_vsnprintf(char *str, long long size, const char *fmt, long long va_ptr);
long long wrap_cccc_vfprintf(FILE *stream, const char *fmt, long long va_ptr);

// ---- scanf family (format_scanf.c) ----
int cccc_vscanf(const char *fmt, va_list ap);
int cccc_vsscanf(const char *str, const char *fmt, va_list ap);
int cccc_vfscanf(FILE *stream, const char *fmt, va_list ap);

int cccc_scanf(const char *fmt, ...);
int cccc_sscanf(const char *str, const char *fmt, ...);
int cccc_fscanf(FILE *stream, const char *fmt, ...);

long long wrap_cccc_vscanf(const char *fmt, long long va_ptr);
long long wrap_cccc_vsscanf(const char *str, const char *fmt, long long va_ptr);
long long wrap_cccc_vfscanf(FILE *stream, const char *fmt, long long va_ptr);

#endif // CCCC_STDLIB_FORMAT_H
