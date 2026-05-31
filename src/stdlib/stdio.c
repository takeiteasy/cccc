// stdio.h stdlib function registration
#include "../jcc.h"
#include <stdarg.h>

// Standard stream getters (since we can't easily register global pointers)
static FILE* __jcc_stdin(void) { return stdin; }
static FILE* __jcc_stdout(void) { return stdout; }
static FILE* __jcc_stderr(void) { return stderr; }

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

// V* variants (format functions that take va_list) - wrappers needed for va_list pointer dereference
static long long wrap_vprintf(const char *fmt, long long va_ptr) {
    return (long long)vprintf(fmt, *(va_list*)va_ptr);
}

static long long wrap_vsprintf(char *str, const char *fmt, long long va_ptr) {
    return (long long)vsprintf(str, fmt, *(va_list*)va_ptr);
}

static long long wrap_vsnprintf(char *str, long long size, const char *fmt, long long va_ptr) {
    return (long long)vsnprintf(str, (size_t)size, fmt, *(va_list*)va_ptr);
}

static long long wrap_vfprintf(FILE *stream, const char *fmt, long long va_ptr) {
    return (long long)vfprintf(stream, fmt, *(va_list*)va_ptr);
}

static long long wrap_vscanf(const char *fmt, long long va_ptr) {
    return (long long)vscanf(fmt, *(va_list*)va_ptr);
}

static long long wrap_vsscanf(const char *str, const char *fmt, long long va_ptr) {
    return (long long)vsscanf(str, fmt, *(va_list*)va_ptr);
}

static long long wrap_vfscanf(FILE *stream, const char *fmt, long long va_ptr) {
    return (long long)vfscanf(stream, fmt, *(va_list*)va_ptr);
}

// Register all stdio.h functions
void register_stdio_functions(JCC *vm) {
    // Standard streams
    cc_register_cfunc(vm, "__jcc_stdin", (void*)__jcc_stdin, 0, 0);
    cc_register_cfunc(vm, "__jcc_stdout", (void*)__jcc_stdout, 0, 0);
    cc_register_cfunc(vm, "__jcc_stderr", (void*)__jcc_stderr, 0, 0);

    // Variadic printf/scanf family
    cc_register_variadic_cfunc(vm, "printf", (void*)printf, 1, 0);
    cc_register_variadic_cfunc(vm, "fprintf", (void*)fprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "sprintf", (void*)sprintf, 2, 0);
    cc_register_variadic_cfunc(vm, "snprintf", (void*)snprintf, 3, 0);
    cc_register_variadic_cfunc(vm, "scanf", (void*)scanf, 1, 0);
    cc_register_variadic_cfunc(vm, "sscanf", (void*)sscanf, 2, 0);
    cc_register_variadic_cfunc(vm, "fscanf", (void*)fscanf, 2, 0);
    
    // V* variants still need wrappers to handle va_list pointer conversion
    cc_register_cfunc(vm, "vprintf", (void*)wrap_vprintf, 2, 0);
    cc_register_cfunc(vm, "vsprintf", (void*)wrap_vsprintf, 3, 0);
    cc_register_cfunc(vm, "vsnprintf", (void*)wrap_vsnprintf, 4, 0);
    cc_register_cfunc(vm, "vfprintf", (void*)wrap_vfprintf, 3, 0);
    cc_register_cfunc(vm, "vscanf", (void*)wrap_vscanf, 2, 0);
    cc_register_cfunc(vm, "vsscanf", (void*)wrap_vsscanf, 3, 0);
    cc_register_cfunc(vm, "vfscanf", (void*)wrap_vfscanf, 3, 0);


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
    cc_register_cfunc(vm, "fseek", (void*)wrap_fseek, 3, 0);
    cc_register_cfunc(vm, "ftell", (void*)ftell, 1, 0);
    cc_register_cfunc(vm, "rewind", (void*)rewind, 1, 0);

    // Error handling
    cc_register_cfunc(vm, "clearerr", (void*)clearerr, 1, 0);
    cc_register_cfunc(vm, "feof", (void*)feof, 1, 0);
    cc_register_cfunc(vm, "ferror", (void*)ferror, 1, 0);
    cc_register_cfunc(vm, "perror", (void*)perror, 1, 0);
}
