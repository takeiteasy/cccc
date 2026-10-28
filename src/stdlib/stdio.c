// stdio.h stdlib function registration
#include "../cccc.h"
#include "../internal.h" // cccc_current_ffi_vm, cccc_vm_heap_realloc
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#ifdef __GLIBC__
#include <obstack.h>
#endif

// C23 adds the %b/%B (binary integer) conversion specifier to the
// printf/scanf families. It's natively supported by glibc >= 2.35, but
// absent on macOS and older glibc, where printf("%b", ...) prints a
// literal 'b' and sscanf(..., "%b", ...) fails to match. On those
// platforms, register the custom engines from format_printf.c/
// format_scanf.c instead of the host libc functions (#394).
#if defined(__GLIBC__)
#include <features.h>
#if __GLIBC_PREREQ(2, 35)
#define CCCC_HAVE_NATIVE_PCT_B 1
#endif
#endif

// Always pull in the custom engines: format_printf.c is only used when
// CCCC_HAVE_NATIVE_PCT_B is unset, but format_scanf.c is used unconditionally
// (see the scanf-family registration below -- glibc's scanf only accepts
// lowercase %b, not the C23 %B, even on glibc >= 2.35, so the custom
// engine stays in use for the scan family regardless of this macro; #728).
#include "format.h"

// Standard stream getters (since we can't easily register global pointers).
// Named wrap_* rather than __cccc_stdin/stdout/stderr: those names are the
// *guest*-facing macro targets (include/stdio.h expands stdin/stdout/stderr
// to `(*__cccc_stdin())` etc.), and under self-hosting a host-side
// definition spelled the same way collides with its own extern declaration
// at parse time, then recurses into itself once that's reconciled (#1280).
static FILE *wrap_stdin(void) {
    return stdin;
}
static FILE *wrap_stdout(void) {
    return stdout;
}
static FILE *wrap_stderr(void) {
    return stderr;
}

// Wrappers for functions returning int that can be negative (sign-extend into
// long long)
static long long wrap_fgetc(long long stream) {
    return (long long)fgetc((FILE *)stream);
}
static long long wrap_getc(long long stream) {
    return (long long)getc((FILE *)stream);
}
static long long wrap_getchar(void) {
    return (long long)getchar();
}
static long long wrap_fputc(long long c, long long stream) {
    return (long long)fputc((int)c, (FILE *)stream);
}
static long long wrap_putc(long long c, long long stream) {
    return (long long)putc((int)c, (FILE *)stream);
}
static long long wrap_putchar(long long c) {
    return (long long)putchar((int)c);
}
static long long wrap_ungetc(long long c, long long stream) {
    return (long long)ungetc((int)c, (FILE *)stream);
}
static long long wrap_fclose(long long stream) {
    return (long long)fclose((FILE *)stream);
}
static long long wrap_fflush(long long stream) {
    return (long long)fflush((FILE *)stream);
}
static long long wrap_fputs(long long s, long long stream) {
    return (long long)fputs((const char *)s, (FILE *)stream);
}
static long long wrap_puts(long long s) {
    return (long long)puts((const char *)s);
}
static long long wrap_remove(long long path) {
    return (long long)remove((const char *)path);
}
static long long wrap_rename(long long old, long long new) {
    return (long long)rename((const char *)old, (const char *)new);
}
static long long wrap_fseek(long long stream, long long off, long long whence) {
    return (long long)fseek((FILE *)stream, (long)off, (int)whence);
}
static long long wrap_fgetpos(long long stream, long long pos) {
    return (long long)fgetpos((FILE *)stream, (fpos_t *)pos);
}
static long long wrap_fsetpos(long long stream, long long pos) {
    return (long long)fsetpos((FILE *)stream, (const fpos_t *)pos);
}
static long long wrap_setvbuf(long long stream, long long buf, long long mode,
                              long long size) {
    return (long long)setvbuf((FILE *)stream, (char *)buf, (int)mode,
                              (size_t)size);
}

// V* variants (format functions that take va_list).
// Extract args from cccc's va_list and re-dispatch via ffi_prep_cif_var to
// the matching non-v* variadic function so all arguments arrive correctly,
// not just the first. (#407)
#ifdef CCCC_HAVE_NATIVE_PCT_B
#include "va_ffi_helper.h"

static long long wrap_vprintf(const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)fmt};
    return cccc_ffi_call_variadic((void *)printf, 1, fixed, n, types, vals);
}

static long long wrap_vsprintf(char *str, const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)str, (int64_t)fmt};
    return cccc_ffi_call_variadic((void *)sprintf, 2, fixed, n, types, vals);
}

static long long wrap_vsnprintf(char *str, long long size, const char *fmt,
                                long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)str, (int64_t)size, (int64_t)fmt};
    return cccc_ffi_call_variadic((void *)snprintf, 3, fixed, n, types, vals);
}

static long long wrap_vfprintf(FILE *stream, const char *fmt,
                               long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)stream, (int64_t)fmt};
    return cccc_ffi_call_variadic((void *)fprintf, 2, fixed, n, types, vals);
}

// asprintf/vasprintf: BSD/GNU extension, present on both Darwin and glibc
// (#1132 self-hosting spike: src/build.c uses vasprintf). Not %b/%B-affected
// itself, but kept alongside the other v*-family wrappers here for the same
// va_list-pointer-conversion reason.
static long long wrap_vasprintf(char **strp, const char *fmt,
                                long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int     types[CCCC_VA_MAX_ARGS];
    int     n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = {(int64_t)strp, (int64_t)fmt};
    return cccc_ffi_call_variadic((void *)asprintf, 2, fixed, n, types, vals);
}

#endif // CCCC_HAVE_NATIVE_PCT_B

// getline/getdelim: libc would realloc() *lineptr, but a guest buffer is VM
// heap memory, so the line is read into a host scratch buffer and copied into
// a guest buffer grown through the VM allocator.
// TODO: the extra copy per line could be avoided by reading straight into the
// guest buffer with getc_unlocked.
static _Thread_local char  *getdelim_scratch;
static _Thread_local size_t getdelim_scratch_cap;

static long long wrap_getdelim(char **lineptr, size_t *n, int delim,
                               FILE *stream) {
    if (!lineptr || !n || !stream) {
        errno = EINVAL;
        return -1;
    }
    ssize_t len =
        getdelim(&getdelim_scratch, &getdelim_scratch_cap, delim, stream);
    if (len < 0)
        return -1;
    size_t need = (size_t)len + 1;
    if (!*lineptr || *n < need) {
        VirtualMachine *vm = cccc_current_ffi_vm();
        char *grown = vm ? cccc_vm_heap_realloc(vm, *lineptr, need) : NULL;
        if (!grown) {
            errno = ENOMEM;
            return -1;
        }
        *lineptr = grown;
        *n       = need;
    }
    memcpy(*lineptr, getdelim_scratch, need);
    return len;
}

static long long wrap_getline(char **lineptr, size_t *n, FILE *stream) {
    return wrap_getdelim(lineptr, n, '\n', stream);
}

// `__*_chk` entry points behind __builtin___*_chk and fortified headers. The
// printf family takes a `flag` (ignored) and the destination's object size;
// the output goes through the same engine as the plain function.
#if defined(CCCC_HAVE_NATIVE_PCT_B) && !defined(CCCC_HAS_DECIMAL)
#define CHK_VSNPRINTF      vsnprintf
#define CHK_VSPRINTF       vsprintf
#define CHK_VFPRINTF       vfprintf
#define CHK_VASPRINTF      vasprintf
#define CHK_WRAP_VSNPRINTF wrap_vsnprintf
#define CHK_WRAP_VSPRINTF  wrap_vsprintf
#define CHK_WRAP_VPRINTF   wrap_vprintf
#define CHK_WRAP_VFPRINTF  wrap_vfprintf
#define CHK_WRAP_VASPRINTF wrap_vasprintf
#else
#define CHK_VSNPRINTF      cccc_vsnprintf
#define CHK_VSPRINTF       cccc_vsprintf
#define CHK_VFPRINTF       cccc_vfprintf
#define CHK_VASPRINTF      cccc_vasprintf
#define CHK_WRAP_VSNPRINTF wrap_cccc_vsnprintf
#define CHK_WRAP_VSPRINTF  wrap_cccc_vsprintf
#define CHK_WRAP_VPRINTF   wrap_cccc_vprintf
#define CHK_WRAP_VFPRINTF  wrap_cccc_vfprintf
#define CHK_WRAP_VASPRINTF wrap_cccc_vasprintf
#endif

static int chk_snprintf(char *s, size_t n, int flag, size_t os, const char *fmt,
                        ...) {
    (void)flag;
    CCCC_FORTIFY_CHECK(n, os, "snprintf");
    va_list ap;
    va_start(ap, fmt);
    int r = CHK_VSNPRINTF(s, n, fmt, ap);
    va_end(ap);
    return r;
}

static int chk_sprintf(char *s, int flag, size_t os, const char *fmt, ...) {
    (void)flag;
    va_list ap, need_ap;
    va_start(ap, fmt);
    va_copy(need_ap, ap);
    int need = CHK_VSNPRINTF(NULL, 0, fmt, need_ap);
    va_end(need_ap);
    if (need >= 0)
        CCCC_FORTIFY_CHECK((size_t)need + 1, os, "sprintf");
    int r = CHK_VSPRINTF(s, fmt, ap);
    va_end(ap);
    return r;
}

static int chk_printf(int flag, const char *fmt, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    int r = CHK_VFPRINTF(stdout, fmt, ap);
    va_end(ap);
    return r;
}

static int chk_fprintf(FILE *stream, int flag, const char *fmt, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    int r = CHK_VFPRINTF(stream, fmt, ap);
    va_end(ap);
    return r;
}

static long long chk_vsnprintf(char *s, long long n, long long flag,
                               long long os, const char *fmt,
                               long long va_ptr) {
    (void)flag;
    CCCC_FORTIFY_CHECK(n, os, "vsnprintf");
    return CHK_WRAP_VSNPRINTF(s, n, fmt, va_ptr);
}

// The size of a vsprintf result is not known before it is formatted, so this
// formats into a bounded scratch copy first when the object size is known.
static long long chk_vsprintf(char *s, long long flag, long long os,
                              const char *fmt, long long va_ptr) {
    (void)flag;
    if ((size_t)os != (size_t)-1) {
        char probe[1];
        int  need = (int)CHK_WRAP_VSNPRINTF(probe, 0, fmt, va_ptr);
        if (need >= 0)
            CCCC_FORTIFY_CHECK((size_t)need + 1, os, "vsprintf");
    }
    return CHK_WRAP_VSPRINTF(s, fmt, va_ptr);
}

static long long chk_vprintf(long long flag, const char *fmt,
                             long long va_ptr) {
    (void)flag;
    return CHK_WRAP_VPRINTF(fmt, va_ptr);
}

static long long chk_vfprintf(FILE *stream, long long flag, const char *fmt,
                              long long va_ptr) {
    (void)flag;
    return CHK_WRAP_VFPRINTF(stream, fmt, va_ptr);
}

static char *chk_fgets(char *s, size_t size, int n, FILE *f) {
    CCCC_FORTIFY_CHECK(n < 0 ? 0 : (size_t)n, size, "fgets");
    return fgets(s, n, f);
}

static size_t chk_fread(void *p, size_t plen, size_t size, size_t n, FILE *f) {
    CCCC_FORTIFY_CHECK(size * n, plen, "fread");
    return fread(p, size, n, f);
}

static int chk_asprintf(char **strp, int flag, const char *fmt, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    int r = CHK_VASPRINTF(strp, fmt, ap);
    va_end(ap);
    return r;
}

static long long chk_vasprintf(char **strp, long long flag, const char *fmt,
                               long long va_ptr) {
    (void)flag;
    return CHK_WRAP_VASPRINTF(strp, fmt, va_ptr);
}

// dprintf/vdprintf: format into a scratch buffer, then write it to the fd.
static long long write_formatted(int fd, char *buf, int len) {
    for (int off = 0; off < len;) {
        ssize_t w = write(fd, buf + off, (size_t)(len - off));
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += (int)w;
    }
    return len;
}

static long long dprintf_va(int fd, const char *fmt, va_list ap) {
    va_list need_ap;
    va_copy(need_ap, ap);
    int len = CHK_VSNPRINTF(NULL, 0, fmt, need_ap);
    va_end(need_ap);
    if (len < 0)
        return -1;
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }
    CHK_VSNPRINTF(buf, (size_t)len + 1, fmt, ap);
    long long r = write_formatted(fd, buf, len);
    free(buf);
    return r;
}

static long long wrap_dprintf(int fd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    long long r = dprintf_va(fd, fmt, ap);
    va_end(ap);
    return r;
}

static long long chk_dprintf(int fd, int flag, const char *fmt, ...) {
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    long long r = dprintf_va(fd, fmt, ap);
    va_end(ap);
    return r;
}

static long long wrap_vdprintf(long long fd, const char *fmt,
                               long long va_ptr) {
    char probe[1];
    int  len = (int)CHK_WRAP_VSNPRINTF(probe, 0, fmt, va_ptr);
    if (len < 0)
        return -1;
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }
    CHK_WRAP_VSNPRINTF(buf, len + 1, fmt, va_ptr);
    long long r = write_formatted((int)fd, buf, len);
    free(buf);
    return r;
}

static long long chk_vdprintf(long long fd, long long flag, const char *fmt,
                              long long va_ptr) {
    (void)flag;
    return wrap_vdprintf(fd, fmt, va_ptr);
}

#ifdef __GLIBC__
// obstack_printf and obstack_vprintf format through the same engine as
// asprintf, then append the text to the obstack.
static long long obstack_append(long long h, char *buf, int len) {
    if (len < 0)
        return len;
    obstack_grow((struct obstack *)h, buf, (size_t)len);
    free(buf);
    return len;
}

static long long wrap_obstack_printf(long long h, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *buf;
    int   len = CHK_VASPRINTF(&buf, fmt, ap);
    va_end(ap);
    return obstack_append(h, buf, len);
}

static long long wrap_obstack_vprintf(long long h, const char *fmt,
                                      long long va_ptr) {
    char *buf;
    int   len = (int)CHK_WRAP_VASPRINTF(&buf, fmt, va_ptr);
    return obstack_append(h, buf, len);
}

static long long chk_obstack_printf(long long h, int flag, const char *fmt,
                                    ...) {
    (void)flag;
    va_list ap;
    va_start(ap, fmt);
    char *buf;
    int   len = CHK_VASPRINTF(&buf, fmt, ap);
    va_end(ap);
    return obstack_append(h, buf, len);
}

static long long chk_obstack_vprintf(long long h, long long flag,
                                     const char *fmt, long long va_ptr) {
    (void)flag;
    return wrap_obstack_vprintf(h, fmt, va_ptr);
}
#endif

#ifdef __linux__
// glibc exports these regardless of the feature macros; declared here rather
// than enabling _GNU_SOURCE for the whole file.
extern char *fgets_unlocked(char *s, int n, FILE *f);
static char *chk_fgets_unlocked(char *s, size_t size, int n, FILE *f) {
    CCCC_FORTIFY_CHECK(n < 0 ? 0 : (size_t)n, size, "fgets_unlocked");
    return fgets_unlocked(s, n, f);
}

static size_t chk_fread_unlocked(void *p, size_t plen, size_t size, size_t n,
                                 FILE *f) {
    CCCC_FORTIFY_CHECK(size * n, plen, "fread_unlocked");
    return fread_unlocked(p, size, n, f);
}

// The buffer-refill and flush hooks behind glibc's extern-inline
// fread_unlocked/getc_unlocked/putc_unlocked (live once __OPTIMIZE__ is set).
extern int __uflow(FILE *f);
extern int __overflow(FILE *f, int c);
#endif

// gets: removed from C11 and unlinkable on current glibc, so the VM reads the
// line itself. Returns the line without its newline, NULL on EOF with no input.
static char *read_gets_line(char *s, size_t limit, const char *fn) {
    size_t i = 0;
    int    c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (limit != (size_t)-1 && i + 1 >= limit)
            cccc_fortify_fail(fn);
        s[i++] = (char)c;
    }
    if (c == EOF && i == 0)
        return NULL;
    s[i] = '\0';
    return s;
}

static char *wrap_gets(char *s) {
    return read_gets_line(s, (size_t)-1, "gets");
}

static char *chk_gets(char *s, size_t size) {
    return read_gets_line(s, size, "gets");
}

// Register all stdio.h functions
void register_stdio_functions(VirtualMachine *vm) {
    // Standard streams
    cc_register_cfunc(vm, "__cccc_stdin", (void *)wrap_stdin, 0, 0);
    cc_register_cfunc(vm, "__cccc_stdout", (void *)wrap_stdout, 0, 0);
    cc_register_cfunc(vm, "__cccc_stderr", (void *)wrap_stderr, 0, 0);

    // The same streams as plain externs, for programs using the SDK's own
    // <stdio.h>. Self-hosting turns `stdout` into an accessor call instead.
#ifndef __CCCC__
    cc_register_cdata(vm, "stdin", &stdin, sizeof stdin);
    cc_register_cdata(vm, "stdout", &stdout, sizeof stdout);
    cc_register_cdata(vm, "stderr", &stderr, sizeof stderr);
#ifdef __APPLE__
    cc_register_cdata(vm, "__stdinp", &stdin, sizeof stdin);
    cc_register_cdata(vm, "__stdoutp", &stdout, sizeof stdout);
    cc_register_cdata(vm, "__stderrp", &stderr, sizeof stderr);
#endif
#endif

    // Variadic printf family
    //
    // #829: even on glibc >= 2.35, host printf has no IEEE 754-2008 decimal
    // floating-point support -- verified against a real glibc 2.39 build:
    // printf("%Df"/"%Hf"/"%DDf", ...) prints the length modifier and
    // conversion character literally rather than formatting the argument.
    // No libc on any of this project's target platforms implements DFP
    // printf (that lives in the separate libdfp in the GNU world, via
    // register_printf_specifier, and isn't something CCCC links against).
    // So a CCCC_HAS_DECIMAL build always routes through the custom engine,
    // the same way the scanf family already does unconditionally for %B
    // just below (#728) -- CCCC_HAVE_NATIVE_PCT_B's fast host path is only
    // safe to use when the guest program can never pass a decimal argument.
#if defined(CCCC_HAVE_NATIVE_PCT_B) && !defined(CCCC_HAS_DECIMAL)
    cc_register_variadic_cfunc(vm, "printf", (void *)printf, 1, 0);
    cc_register_variadic_cfunc(vm, "fprintf", (void *)fprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "sprintf", (void *)sprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "snprintf", (void *)snprintf, 3, 0);

    // V* variants still need wrappers to handle va_list pointer conversion
    cc_register_cfunc(vm, "vprintf", (void *)wrap_vprintf, 2, 0);
    cc_register_cfunc(vm, "vsprintf", (void *)wrap_vsprintf, 3, 0);
    cc_register_cfunc(vm, "vsnprintf", (void *)wrap_vsnprintf, 4, 0);
    cc_register_cfunc(vm, "vfprintf", (void *)wrap_vfprintf, 3, 0);
    cc_register_variadic_cfunc(vm, "asprintf", (void *)asprintf, 2, 0);
    cc_register_cfunc(vm, "vasprintf", (void *)wrap_vasprintf, 3, 0);
#else
    // Custom %b/%B-capable engine (format_printf.c)
    cc_register_variadic_cfunc(vm, "printf", (void *)cccc_printf, 1, 0);
    cc_register_variadic_cfunc(vm, "fprintf", (void *)cccc_fprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "sprintf", (void *)cccc_sprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "snprintf", (void *)cccc_snprintf, 3, 0);

    // V* variants still need wrappers to handle va_list pointer conversion
    cc_register_cfunc(vm, "vprintf", (void *)wrap_cccc_vprintf, 2, 0);
    cc_register_cfunc(vm, "vsprintf", (void *)wrap_cccc_vsprintf, 3, 0);
    cc_register_cfunc(vm, "vsnprintf", (void *)wrap_cccc_vsnprintf, 4, 0);
    cc_register_cfunc(vm, "vfprintf", (void *)wrap_cccc_vfprintf, 3, 0);
    cc_register_variadic_cfunc(vm, "asprintf", (void *)cccc_asprintf, 2, 0);
    cc_register_cfunc(vm, "vasprintf", (void *)wrap_cccc_vasprintf, 3, 0);
#endif

    // __builtin___*_chk
    cc_register_variadic_cfunc(vm, "__snprintf_chk", (void *)chk_snprintf, 5,
                               0);
    cc_register_variadic_cfunc(vm, "__sprintf_chk", (void *)chk_sprintf, 4, 0);
    cc_register_variadic_cfunc(vm, "__printf_chk", (void *)chk_printf, 2, 0);
    cc_register_variadic_cfunc(vm, "__fprintf_chk", (void *)chk_fprintf, 3, 0);
    cc_register_cfunc(vm, "__vsnprintf_chk", (void *)chk_vsnprintf, 6, 0);
    cc_register_cfunc(vm, "__vsprintf_chk", (void *)chk_vsprintf, 5, 0);
    cc_register_cfunc(vm, "__vprintf_chk", (void *)chk_vprintf, 3, 0);
    cc_register_cfunc(vm, "__vfprintf_chk", (void *)chk_vfprintf, 4, 0);
    cc_register_cfunc(vm, "__fgets_chk", (void *)chk_fgets, 4, 0);
    cc_register_cfunc(vm, "__fread_chk", (void *)chk_fread, 5, 0);
    cc_register_variadic_cfunc(vm, "__asprintf_chk", (void *)chk_asprintf, 3,
                               0);
    cc_register_cfunc(vm, "__vasprintf_chk", (void *)chk_vasprintf, 4, 0);
#ifdef __GLIBC__
    cc_register_variadic_cfunc(vm, "obstack_printf",
                               (void *)wrap_obstack_printf, 2, 0);
    cc_register_cfunc(vm, "obstack_vprintf", (void *)wrap_obstack_vprintf, 3,
                      0);
    cc_register_variadic_cfunc(vm, "__obstack_printf_chk",
                               (void *)chk_obstack_printf, 3, 0);
    cc_register_cfunc(vm, "__obstack_vprintf_chk", (void *)chk_obstack_vprintf,
                      4, 0);
#endif
    cc_register_variadic_cfunc(vm, "dprintf", (void *)wrap_dprintf, 2, 0);
    cc_register_cfunc(vm, "vdprintf", (void *)wrap_vdprintf, 3, 0);
    cc_register_variadic_cfunc(vm, "__dprintf_chk", (void *)chk_dprintf, 3, 0);
    cc_register_cfunc(vm, "__vdprintf_chk", (void *)chk_vdprintf, 4, 0);
    cc_register_cfunc(vm, "gets", (void *)wrap_gets, 1, 0);
    cc_register_cfunc(vm, "__gets_chk", (void *)chk_gets, 2, 0);
#ifdef __linux__
    cc_register_cfunc(vm, "fgets_unlocked", (void *)fgets_unlocked, 3, 0);
    cc_register_cfunc(vm, "fread_unlocked", (void *)fread_unlocked, 4, 0);
    cc_register_cfunc(vm, "__uflow", (void *)__uflow, 1, 0);
    cc_register_cfunc(vm, "__overflow", (void *)__overflow, 2, 0);
    cc_register_cfunc(vm, "__fgets_unlocked_chk", (void *)chk_fgets_unlocked, 4,
                      0);
    cc_register_cfunc(vm, "__fread_unlocked_chk", (void *)chk_fread_unlocked, 5,
                      0);
#endif

    // Variadic scanf family: always routed to the custom format_scanf.c
    // engine, even when CCCC_HAVE_NATIVE_PCT_B is set. glibc's scanf family
    // (>= 2.35) accepts the C23 %b conversion but rejects uppercase %B
    // outright, so the native path can't be used for scanf the way it can
    // for printf. See format_scanf.c and #728.
    cc_register_variadic_cfunc(vm, "scanf", (void *)cccc_scanf, 1, 0);
    cc_register_variadic_cfunc(vm, "sscanf", (void *)cccc_sscanf, 2, 0);
    cc_register_variadic_cfunc(vm, "fscanf", (void *)cccc_fscanf, 2, 0);

    cc_register_cfunc(vm, "vscanf", (void *)wrap_cccc_vscanf, 2, 0);
    cc_register_cfunc(vm, "vsscanf", (void *)wrap_cccc_vsscanf, 3, 0);
    cc_register_cfunc(vm, "vfscanf", (void *)wrap_cccc_vfscanf, 3, 0);

    // File operations
    cc_register_cfunc(vm, "remove", (void *)wrap_remove, 1, 0);
    cc_register_cfunc(vm, "rename", (void *)wrap_rename, 2, 0);
    cc_register_cfunc(vm, "tmpfile", (void *)tmpfile, 0, 0);
    cc_register_cfunc(vm, "tmpnam", (void *)tmpnam, 1, 0);
    cc_register_cfunc(vm, "fclose", (void *)wrap_fclose, 1, 0);
    cc_register_cfunc(vm, "fflush", (void *)wrap_fflush, 1, 0);
    cc_register_cfunc(vm, "fopen", (void *)fopen, 2, 0);
    cc_register_cfunc(vm, "fdopen", (void *)fdopen, 2, 0);
    cc_register_cfunc(vm, "freopen", (void *)freopen, 3, 0);
    // open_memstream: not printf-family, so it sits outside the
    // CCCC_HAVE_NATIVE_PCT_B split above. Plain 2-arg passthrough returning a
    // real host FILE*; the guest's char**/size_t* out-params are host
    // pointers written by the host libc on fflush/fclose.
    cc_register_cfunc(vm, "open_memstream", (void *)open_memstream, 2, 0);
    cc_register_cfunc(vm, "setbuf", (void *)setbuf, 2, 0);
    cc_register_cfunc(vm, "setvbuf", (void *)wrap_setvbuf, 4, 0);

    // Character I/O
    cc_register_cfunc(vm, "fgetc", (void *)wrap_fgetc, 1, 0);
    cc_register_cfunc(vm, "fputc", (void *)wrap_fputc, 2, 0);
    cc_register_cfunc(vm, "getc", (void *)wrap_getc, 1, 0);
    cc_register_cfunc(vm, "putc", (void *)wrap_putc, 2, 0);
    cc_register_cfunc(vm, "getchar", (void *)wrap_getchar, 0, 0);
    cc_register_cfunc(vm, "putchar", (void *)wrap_putchar, 1, 0);
    cc_register_cfunc(vm, "ungetc", (void *)wrap_ungetc, 2, 0);

    // String I/O
    cc_register_cfunc(vm, "fgets", (void *)fgets, 3, 0);
    cc_register_cfunc(vm, "getline", (void *)wrap_getline, 3, 0);
    cc_register_cfunc(vm, "getdelim", (void *)wrap_getdelim, 4, 0);
    cc_register_cfunc(vm, "fputs", (void *)wrap_fputs, 2, 0);
    cc_register_cfunc(vm, "puts", (void *)wrap_puts, 1, 0);

    // Binary I/O
    cc_register_cfunc(vm, "fread", (void *)fread, 4, 0);
    cc_register_cfunc(vm, "fwrite", (void *)fwrite, 4, 0);

    // Positioning
    cc_register_cfunc(vm, "fgetpos", (void *)wrap_fgetpos, 2, 0);
    cc_register_cfunc(vm, "fsetpos", (void *)wrap_fsetpos, 2, 0);
    cc_register_cfunc(vm, "fseek", (void *)wrap_fseek, 3, 0);
    cc_register_cfunc(vm, "fseeko", (void *)wrap_fseek, 3, 0);
    cc_register_cfunc(vm, "ftell", (void *)ftell, 1, 0);
    cc_register_cfunc(vm, "ftello", (void *)ftell, 1, 0);
    cc_register_cfunc(vm, "rewind", (void *)rewind, 1, 0);

    // Process I/O (POSIX)
    cc_register_cfunc(vm, "popen", (void *)popen, 2, 0);
    cc_register_cfunc(vm, "pclose", (void *)pclose, 1, 0);
    cc_register_cfunc(vm, "fileno", (void *)fileno, 1, 0);

    // Thread-safety file locking (POSIX)
    cc_register_cfunc(vm, "flockfile", (void *)flockfile, 1, 0);
    cc_register_cfunc(vm, "funlockfile", (void *)funlockfile, 1, 0);
    cc_register_cfunc(vm, "ftrylockfile", (void *)ftrylockfile, 1, 0);
    // Unlocked I/O (POSIX thread-unsafe fast paths)
    cc_register_cfunc(vm, "getc_unlocked", (void *)getc_unlocked, 1, 0);
    cc_register_cfunc(vm, "getchar_unlocked", (void *)getchar_unlocked, 0, 0);
    cc_register_cfunc(vm, "putc_unlocked", (void *)putc_unlocked, 2, 0);
    cc_register_cfunc(vm, "putchar_unlocked", (void *)putchar_unlocked, 1, 0);

    // Error handling
    cc_register_cfunc(vm, "clearerr", (void *)clearerr, 1, 0);
    cc_register_cfunc(vm, "feof", (void *)feof, 1, 0);
    cc_register_cfunc(vm, "ferror", (void *)ferror, 1, 0);
    cc_register_cfunc(vm, "perror", (void *)perror, 1, 0);
}
