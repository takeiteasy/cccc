// stdio.h stdlib function registration
#include "../cccc.h"
#include <stdarg.h>

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

// Standard stream getters (since we can't easily register global pointers)
static FILE* __cccc_stdin(void) { return stdin; }
static FILE* __cccc_stdout(void) { return stdout; }
static FILE* __cccc_stderr(void) { return stderr; }

// Wrappers for functions returning int that can be negative (sign-extend into long long)
static long long wrap_fgetc(long long stream)                        { return (long long)fgetc((FILE *)stream); }
static long long wrap_getc(long long stream)                         { return (long long)getc((FILE *)stream); }
static long long wrap_getchar(void)                                  { return (long long)getchar(); }
static long long wrap_fputc(long long c, long long stream)           { return (long long)fputc((int)c, (FILE *)stream); }
static long long wrap_putc(long long c, long long stream)            { return (long long)putc((int)c, (FILE *)stream); }
static long long wrap_putchar(long long c)                           { return (long long)putchar((int)c); }
static long long wrap_ungetc(long long c, long long stream)          { return (long long)ungetc((int)c, (FILE *)stream); }
static long long wrap_fclose(long long stream)                       { return (long long)fclose((FILE *)stream); }
static long long wrap_fflush(long long stream)                       { return (long long)fflush((FILE *)stream); }
static long long wrap_fputs(long long s, long long stream)           { return (long long)fputs((const char *)s, (FILE *)stream); }
static long long wrap_puts(long long s)                              { return (long long)puts((const char *)s); }
static long long wrap_remove(long long path)                         { return (long long)remove((const char *)path); }
static long long wrap_rename(long long old, long long new)           { return (long long)rename((const char *)old, (const char *)new); }
static long long wrap_fseek(long long stream, long long off, long long whence) { return (long long)fseek((FILE *)stream, (long)off, (int)whence); }
static long long wrap_fgetpos(long long stream, long long pos)       { return (long long)fgetpos((FILE *)stream, (fpos_t *)pos); }
static long long wrap_fsetpos(long long stream, long long pos)       { return (long long)fsetpos((FILE *)stream, (const fpos_t *)pos); }
static long long wrap_setvbuf(long long stream, long long buf, long long mode, long long size) { return (long long)setvbuf((FILE *)stream, (char *)buf, (int)mode, (size_t)size); }

// V* variants (format functions that take va_list).
// Extract args from cccc's va_list and re-dispatch via ffi_prep_cif_var to
// the matching non-v* variadic function so all arguments arrive correctly,
// not just the first. (#407)
#ifdef CCCC_HAVE_NATIVE_PCT_B
#include "va_ffi_helper.h"

static long long wrap_vprintf(const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int types[CCCC_VA_MAX_ARGS];
    int n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = { (int64_t)fmt };
    return cccc_ffi_call_variadic((void *)printf, 1, fixed, n, types, vals);
}

static long long wrap_vsprintf(char *str, const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int types[CCCC_VA_MAX_ARGS];
    int n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = { (int64_t)str, (int64_t)fmt };
    return cccc_ffi_call_variadic((void *)sprintf, 2, fixed, n, types, vals);
}

static long long wrap_vsnprintf(char *str, long long size, const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int types[CCCC_VA_MAX_ARGS];
    int n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = { (int64_t)str, (int64_t)size, (int64_t)fmt };
    return cccc_ffi_call_variadic((void *)snprintf, 3, fixed, n, types, vals);
}

static long long wrap_vfprintf(FILE *stream, const char *fmt, long long va_ptr) {
    CCCC_VA_LOCAL(va, va_ptr);
    int types[CCCC_VA_MAX_ARGS];
    int n = cccc_parse_printf_fmt(fmt, types, CCCC_VA_MAX_ARGS);
    int64_t vals[CCCC_VA_MAX_ARGS];
    cccc_va_extract(va, types, n, vals);
    int64_t fixed[] = { (int64_t)stream, (int64_t)fmt };
    return cccc_ffi_call_variadic((void *)fprintf, 2, fixed, n, types, vals);
}

#endif // CCCC_HAVE_NATIVE_PCT_B

// Register all stdio.h functions
void register_stdio_functions(VirtualMachine *vm) {
    // Standard streams
    cc_register_cfunc(vm, "__cccc_stdin", (void*)__cccc_stdin, 0, 0);
    cc_register_cfunc(vm, "__cccc_stdout", (void*)__cccc_stdout, 0, 0);
    cc_register_cfunc(vm, "__cccc_stderr", (void*)__cccc_stderr, 0, 0);

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
    cc_register_variadic_cfunc(vm, "printf", (void*)printf, 1, 0);
    cc_register_variadic_cfunc(vm, "fprintf", (void*)fprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "sprintf", (void*)sprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "snprintf", (void*)snprintf, 3, 0);

    // V* variants still need wrappers to handle va_list pointer conversion
    cc_register_cfunc(vm, "vprintf", (void*)wrap_vprintf, 2, 0);
    cc_register_cfunc(vm, "vsprintf", (void*)wrap_vsprintf, 3, 0);
    cc_register_cfunc(vm, "vsnprintf", (void*)wrap_vsnprintf, 4, 0);
    cc_register_cfunc(vm, "vfprintf", (void*)wrap_vfprintf, 3, 0);
#else
    // Custom %b/%B-capable engine (format_printf.c)
    cc_register_variadic_cfunc(vm, "printf", (void*)cccc_printf, 1, 0);
    cc_register_variadic_cfunc(vm, "fprintf", (void*)cccc_fprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "sprintf", (void*)cccc_sprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "snprintf", (void*)cccc_snprintf, 3, 0);

    // V* variants still need wrappers to handle va_list pointer conversion
    cc_register_cfunc(vm, "vprintf", (void*)wrap_cccc_vprintf, 2, 0);
    cc_register_cfunc(vm, "vsprintf", (void*)wrap_cccc_vsprintf, 3, 0);
    cc_register_cfunc(vm, "vsnprintf", (void*)wrap_cccc_vsnprintf, 4, 0);
    cc_register_cfunc(vm, "vfprintf", (void*)wrap_cccc_vfprintf, 3, 0);
#endif

    // Variadic scanf family: always routed to the custom format_scanf.c
    // engine, even when CCCC_HAVE_NATIVE_PCT_B is set. glibc's scanf family
    // (>= 2.35) accepts the C23 %b conversion but rejects uppercase %B
    // outright, so the native path can't be used for scanf the way it can
    // for printf. See format_scanf.c and #728.
    cc_register_variadic_cfunc(vm, "scanf", (void*)cccc_scanf, 1, 0);
    cc_register_variadic_cfunc(vm, "sscanf", (void*)cccc_sscanf, 2, 0);
    cc_register_variadic_cfunc(vm, "fscanf", (void*)cccc_fscanf, 2, 0);

    cc_register_cfunc(vm, "vscanf", (void*)wrap_cccc_vscanf, 2, 0);
    cc_register_cfunc(vm, "vsscanf", (void*)wrap_cccc_vsscanf, 3, 0);
    cc_register_cfunc(vm, "vfscanf", (void*)wrap_cccc_vfscanf, 3, 0);


    // File operations
    cc_register_cfunc(vm, "remove", (void*)wrap_remove, 1, 0);
    cc_register_cfunc(vm, "rename", (void*)wrap_rename, 2, 0);
    cc_register_cfunc(vm, "tmpfile", (void*)tmpfile, 0, 0);
    cc_register_cfunc(vm, "tmpnam", (void*)tmpnam, 1, 0);
    cc_register_cfunc(vm, "fclose", (void*)wrap_fclose, 1, 0);
    cc_register_cfunc(vm, "fflush", (void*)wrap_fflush, 1, 0);
    cc_register_cfunc(vm, "fopen", (void*)fopen, 2, 0);
    cc_register_cfunc(vm, "freopen", (void*)freopen, 3, 0);
    cc_register_cfunc(vm, "setbuf", (void*)setbuf, 2, 0);
    cc_register_cfunc(vm, "setvbuf", (void*)wrap_setvbuf, 4, 0);

    // Character I/O
    cc_register_cfunc(vm, "fgetc", (void*)wrap_fgetc, 1, 0);
    cc_register_cfunc(vm, "fputc", (void*)wrap_fputc, 2, 0);
    cc_register_cfunc(vm, "getc", (void*)wrap_getc, 1, 0);
    cc_register_cfunc(vm, "putc", (void*)wrap_putc, 2, 0);
    cc_register_cfunc(vm, "getchar", (void*)wrap_getchar, 0, 0);
    cc_register_cfunc(vm, "putchar", (void*)wrap_putchar, 1, 0);
    cc_register_cfunc(vm, "ungetc", (void*)wrap_ungetc, 2, 0);

    // String I/O
    cc_register_cfunc(vm, "fgets", (void*)fgets, 3, 0);
    cc_register_cfunc(vm, "fputs", (void*)wrap_fputs, 2, 0);
    cc_register_cfunc(vm, "puts", (void*)wrap_puts, 1, 0);

    // Binary I/O
    cc_register_cfunc(vm, "fread", (void*)fread, 4, 0);
    cc_register_cfunc(vm, "fwrite", (void*)fwrite, 4, 0);

    // Positioning
    cc_register_cfunc(vm, "fgetpos", (void*)wrap_fgetpos, 2, 0);
    cc_register_cfunc(vm, "fsetpos", (void*)wrap_fsetpos, 2, 0);
    cc_register_cfunc(vm, "fseek",  (void*)wrap_fseek, 3, 0);
    cc_register_cfunc(vm, "fseeko", (void*)wrap_fseek, 3, 0);
    cc_register_cfunc(vm, "ftell",  (void*)ftell, 1, 0);
    cc_register_cfunc(vm, "ftello", (void*)ftell, 1, 0);
    cc_register_cfunc(vm, "rewind", (void*)rewind, 1, 0);

    // Process I/O (POSIX)
    cc_register_cfunc(vm, "popen",  (void*)popen,  2, 0);
    cc_register_cfunc(vm, "pclose", (void*)pclose, 1, 0);

    // Thread-safety file locking (POSIX)
    cc_register_cfunc(vm, "flockfile",      (void*)flockfile,      1, 0);
    cc_register_cfunc(vm, "funlockfile",    (void*)funlockfile,    1, 0);
    cc_register_cfunc(vm, "ftrylockfile",   (void*)ftrylockfile,   1, 0);
    // Unlocked I/O (POSIX thread-unsafe fast paths)
    cc_register_cfunc(vm, "getc_unlocked",    (void*)getc_unlocked,    1, 0);
    cc_register_cfunc(vm, "getchar_unlocked", (void*)getchar_unlocked, 0, 0);
    cc_register_cfunc(vm, "putc_unlocked",    (void*)putc_unlocked,    2, 0);
    cc_register_cfunc(vm, "putchar_unlocked", (void*)putchar_unlocked, 1, 0);

    // Error handling
    cc_register_cfunc(vm, "clearerr", (void*)clearerr, 1, 0);
    cc_register_cfunc(vm, "feof", (void*)feof, 1, 0);
    cc_register_cfunc(vm, "ferror", (void*)ferror, 1, 0);
    cc_register_cfunc(vm, "perror", (void*)perror, 1, 0);
}
