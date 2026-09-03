// printf-family core engine for platforms whose host libc lacks the C23
// %b/%B (binary integer) conversion specifier (macOS, glibc < 2.35).
// Built on a vendored fork of stb_sprintf (stb_sprintf.h), which already
// implements %b/%B with full flag/width/precision/# support. See #394.
#include "format.h"

#include <stdlib.h> // malloc/free (cccc_vasprintf)

#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_DECORATE(name) cccc_stbsp_##name
// Local patch (#577): force aligned accesses. The vendored fast paths store a
// 32-bit word straight into the output buffer (`*(stbsp__uint32*)bf = v`),
// which is misaligned-UB when bf is not 4-byte aligned (UBSan flagged
// stb_sprintf.h:434). This vendor-provided knob routes those through byte
// copies; the perf cost is negligible for our diagnostic/printf use.
#define STB_SPRINTF_NOUNALIGNED
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
    char  buf[STB_SPRINTF_MIN];
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

// asprintf/vasprintf (#1132 self-hosting spike: src/build.c uses vasprintf,
// found undeclared/unregistered -- neither is standard C, but both are
// present on Darwin and glibc). No host asprintf/vasprintf to fall back to
// here (this is the %b/%B custom-engine branch, CCCC_HAS_DECIMAL or a host
// lacking native %b/%B) so implement the usual two-pass measure-then-render
// shape directly on top of cccc_vsnprintf.
int cccc_vasprintf(char **strp, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int need = cccc_vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (need < 0) {
        *strp = NULL;
        return need;
    }
    char *buf = malloc((size_t)need + 1);
    if (!buf) {
        *strp = NULL;
        return -1;
    }
    int r = cccc_vsnprintf(buf, (size_t)need + 1, fmt, ap);
    if (r < 0) {
        free(buf);
        *strp = NULL;
        return r;
    }
    *strp = buf;
    return r;
}

int cccc_asprintf(char **strp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = cccc_vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;
}

// V* variant wrappers — extract args from cccc's va_list and re-dispatch
// via ffi_prep_cif_var to the matching non-v* variadic function. This builds
// a genuine host variadic frame so the callee's own va_start/va_arg work
// correctly for all arguments, not just the first. (#407)
#include "va_ffi_helper.h"

long long wrap_cccc_vprintf(const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)fmt};
    return cccc_ffi_call_variadic((void *)cccc_printf, 1, fixed, n, types,
                                  vals);
}

long long wrap_cccc_vsprintf(char *str, const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)str, (int64_t)fmt};
    return cccc_ffi_call_variadic((void *)cccc_sprintf, 2, fixed, n, types,
                                  vals);
}

long long wrap_cccc_vsnprintf(char *str, long long size, const char *fmt,
                              long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)str, (int64_t)size, (int64_t)fmt};
    return cccc_ffi_call_variadic((void *)cccc_snprintf, 3, fixed, n, types,
                                  vals);
}

long long wrap_cccc_vfprintf(FILE *stream, const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)stream, (int64_t)fmt};
    return cccc_ffi_call_variadic((void *)cccc_fprintf, 2, fixed, n, types,
                                  vals);
}

long long wrap_cccc_vasprintf(char **strp, const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)strp, (int64_t)fmt};
    return cccc_ffi_call_variadic((void *)cccc_asprintf, 2, fixed, n, types,
                                  vals);
}
