// Internal declarations for the custom printf/scanf %b/%B engines (#394).
// Used on platforms where the host libc lacks %b/%B support (macOS,
// glibc < 2.35). See format_printf.c and format_scanf.c.
#ifndef CCCC_STDLIB_FORMAT_H
#define CCCC_STDLIB_FORMAT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

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
