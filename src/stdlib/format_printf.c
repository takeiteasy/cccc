// printf-family core engine for platforms whose host libc lacks the C23
// %b/%B (binary integer) conversion specifier (macOS, glibc < 2.35).
// Built on a vendored fork of stb_sprintf (stb_sprintf.h), which already
// implements %b/%B with full flag/width/precision/# support. See #394.
#include "format.h"

#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_DECORATE(name) cccc_stbsp_##name
#include "stb_sprintf.h"

int cccc_vsnprintf(char *buf, size_t count, const char *fmt, va_list ap) {
    return cccc_stbsp_vsnprintf(buf, (int)count, fmt, ap);
}

int cccc_vsprintf(char *buf, const char *fmt, va_list ap) {
    return cccc_stbsp_vsprintf(buf, fmt, ap);
}

// Streams formatted output to a FILE* in STB_SPRINTF_MIN-sized chunks.
typedef struct {
    FILE *stream;
    char buf[STB_SPRINTF_MIN];
} cccc_vfprintf_ctx;

static char *cccc_vfprintf_cb(const char *buf, void *user, int len) {
    cccc_vfprintf_ctx *ctx = (cccc_vfprintf_ctx *)user;
    fwrite(buf, 1, (size_t)len, ctx->stream);
    return ctx->buf;
}

int cccc_vfprintf(FILE *stream, const char *fmt, va_list ap) {
    cccc_vfprintf_ctx ctx;
    ctx.stream = stream;
    return cccc_stbsp_vsprintfcb(cccc_vfprintf_cb, &ctx, ctx.buf, fmt, ap);
}

int cccc_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cccc_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

int cccc_fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cccc_vfprintf(stream, fmt, ap);
    va_end(ap);
    return r;
}

int cccc_sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cccc_vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int cccc_snprintf(char *buf, size_t count, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cccc_vsnprintf(buf, count, fmt, ap);
    va_end(ap);
    return r;
}

// V* variant wrappers - the VM passes a pointer to a va_list (see wrap_v*
// in stdio.c for the host-libc equivalents).
long long wrap_cccc_vprintf(const char *fmt, long long va_ptr) {
    return (long long)cccc_vfprintf(stdout, fmt, *(va_list *)va_ptr);
}

long long wrap_cccc_vsprintf(char *str, const char *fmt, long long va_ptr) {
    return (long long)cccc_vsprintf(str, fmt, *(va_list *)va_ptr);
}

long long wrap_cccc_vsnprintf(char *str, long long size, const char *fmt, long long va_ptr) {
    return (long long)cccc_vsnprintf(str, (size_t)size, fmt, *(va_list *)va_ptr);
}

long long wrap_cccc_vfprintf(FILE *stream, const char *fmt, long long va_ptr) {
    return (long long)cccc_vfprintf(stream, fmt, *(va_list *)va_ptr);
}
