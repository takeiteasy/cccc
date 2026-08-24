/*
 * stdio.h - Standard I/O functions for CCCC VM
 */

#ifndef __STDIO_H
#define __STDIO_H

// #1040: this exact file is also what a native/generated re-emission's
// replayed `#include <stdio.h>` resolves to (run_native_backend forwards
// -I./include straight through to the host cc, and -I paths are searched
// ahead of system directories). Guarding only the __cccc_stdin/stdout/
// stderr `extern`s below (the fix direction the ticket originally
// proposed) is not enough: the #define stdout __cccc_stdout() macros stay
// live either way, so a real host compiler re-lexing this same physical
// text would still expand `stdout` inside the shim body
// serialize.c's native_accessor_shims later emits
// (`static FILE *__cccc_stdout(void) { return stdout; }`) right back into
// a call to itself -- the identical infinite-recursion trap
// include/float.h's FLT_ROUNDS shim comment documents, and confirmed with
// a minimal `fprintf(stdout, ...)` repro. Guarding the whole CCCC-flavored
// body instead (same shape as include/fenv.h/include/errno.h/
// include/math.h/include/float.h, #1021) and handing off to the host's own
// <stdio.h> via #include_next avoids the self-reference entirely, since
// the host's real `stdout` is a plain object/macro with no such loop.
// __CCCC__ is defined unconditionally by CCCC's own preprocessor before any
// header is read, so its absence here means a genuine host compiler is
// reprocessing this file -- only possible during -c=native/-c=generated
// serializer replay.
#ifdef __CCCC__

#include <stdarg.h> // #1070: angle-bracket for a correct #include_next hand-off under real GCC
#include "stddef.h"
#include "sys/types.h" // #1144: off_t for fseeko/ftello below

// FILE type (opaque pointer to host FILE)
typedef void FILE;

// Standard streams (accessed via getter functions)
extern FILE *__cccc_stdin(void);
extern FILE *__cccc_stdout(void);
extern FILE *__cccc_stderr(void);
#define stdin  __cccc_stdin()
#define stdout __cccc_stdout()
#define stderr __cccc_stderr()

/* Types and macros from the C standard */
typedef long fpos_t;

/* Buffering and limits */
#ifndef BUFSIZ
#define BUFSIZ 8192
#endif

#ifndef EOF
#define EOF (-1)
#endif

#ifndef FILENAME_MAX
#define FILENAME_MAX 4096
#endif

#ifndef FOPEN_MAX
#define FOPEN_MAX 20
#endif

#ifndef L_tmpnam
#define L_tmpnam 20
#endif

#ifndef TMP_MAX
#define TMP_MAX 238328
#endif

/* Seek constants */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* setvbuf modes */
#ifndef _IOFBF
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#endif

// ==================================================
// Variadic function declarations
// ==================================================
// Supported via platform inline assembly

// Printf family
extern int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern int fprintf(FILE *stream, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
extern int sprintf(char *str, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
extern int snprintf(char *str, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

// Scanf family
extern int scanf(const char *fmt, ...) __attribute__((format(scanf, 1, 2)));
extern int sscanf(const char *str, const char *fmt, ...)
    __attribute__((format(scanf, 2, 3)));
extern int fscanf(FILE *stream, const char *fmt, ...)
    __attribute__((format(scanf, 2, 3)));

// V* variants (take va_list) - Note: va_list manipulation not fully supported
// in VM These are provided for completeness but may not work as expected
extern int vprintf(char *fmt, va_list ap);
extern int vsprintf(char *str, char *fmt, va_list ap);
extern int vsnprintf(char *str, long size, char *fmt, va_list ap);
extern int vfprintf(FILE *stream, char *fmt, va_list ap);
extern int vscanf(char *fmt, va_list ap);
extern int vsscanf(char *str, char *fmt, va_list ap);
extern int vfscanf(FILE *stream, char *fmt, va_list ap);

// ==================================================
// Common non-variadic stdio functions (both modes)
// ==================================================

extern int remove(const char *filename);
extern int rename(const char *old, const char *new);
extern FILE *tmpfile(void);
extern char *tmpnam(char *s);
extern int fclose(FILE *stream);
extern int fflush(FILE *stream);
extern FILE *fopen(const char *filename, const char *mode);
extern FILE *freopen(const char *filename, const char *mode, FILE *stream);
extern void setbuf(FILE *stream, char *buf);
extern int setvbuf(FILE *stream, char *buf, int mode, size_t size);
extern int fgetc(FILE *stream);
extern char *fgets(char *s, int n, FILE *stream);
extern int fputc(int c, FILE *stream);
extern int fputs(const char *s, FILE *stream);
extern int getc(FILE *stream);
extern int getchar(void);
extern int putc(int c, FILE *stream);
extern int putchar(int c);
extern int puts(const char *s);
extern int ungetc(int c, FILE *stream);
extern size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
extern size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
extern int fgetpos(FILE *stream, fpos_t *pos);
extern int fseek(FILE *stream, long int offset, int whence);
extern int fsetpos(FILE *stream, const fpos_t *pos);
extern long int ftell(FILE *stream);
extern void rewind(FILE *stream);
extern void clearerr(FILE *stream);
extern int feof(FILE *stream);
extern int ferror(FILE *stream);
extern void perror(const char *s);

// #1144: registered VM cfuncs (src/stdlib/stdio.c) with no bundled-header
// declaration anywhere -- found auditing every cc_register_cfunc name
// against every bundled header's own declarations (the "broader audit" the
// ticket asks for) once implicit function declaration became a hard error
// at C99+/-c=native. All eleven are ordinary POSIX <stdio.h> extensions
// present on both macOS and glibc.
extern FILE *popen(const char *command, const char *mode);
extern int pclose(FILE *stream);
extern int fseeko(FILE *stream, off_t offset, int whence);
extern off_t ftello(FILE *stream);
extern void flockfile(FILE *stream);
extern int ftrylockfile(FILE *stream);
extern void funlockfile(FILE *stream);
extern int getc_unlocked(FILE *stream);
extern int getchar_unlocked(void);
extern int putc_unlocked(int c, FILE *stream);
extern int putchar_unlocked(int c);

#else
#include_next <stdio.h>
#endif /* __CCCC__ */

#endif // __STDIO_H
