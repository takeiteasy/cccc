// CCCC_FLAGS: --testing --checked-pointers
// Built-in bounds for libc/POSIX prototypes: a call whose buffer argument is a
// plain array or checked pointer is checked against the size argument, with no
// annotation in the header or the program.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// ---------------------------------------------------------------------
// In-bounds calls pass.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_libc_string_in_bounds(void) {
    char a[8], b[16];
    memset(b, 'x', sizeof b);
    memcpy(a, b, sizeof a);
    memmove(b, a, sizeof a);
    AssertEq(a[7], 'x');
}

[[cccc::test]]
void test_libc_snprintf_in_bounds(void) {
    char a[8];
    AssertEq(snprintf(a, sizeof a, "%d", 12345), 5);
    AssertEq(snprintf(NULL, 0, "%d", 12345), 5);
    AssertEq(snprintf(a, 0, "%d", 12345), 5);
}

[[cccc::test]]
void test_libc_stdio_in_bounds(void) {
    char  a[8];
    FILE *f = tmpfile();
    AssertEq(fwrite("abcdefgh", 1, 8, f), 8);
    rewind(f);
    AssertEq(fread(a, 2, 4, f), 4);
    rewind(f);
    AssertEq(fgets(a, sizeof a, f) != NULL, 1);
    fclose(f);
}

[[cccc::test]]
void test_libc_fd_in_bounds(void) {
    char a[8];
    int  fds[2];
    AssertEq(pipe(fds), 0);
    AssertEq(write(fds[1], "abcd", 4), 4);
    AssertEq(read(fds[0], a, sizeof a), 4);
    AssertEq(getcwd(a, 0) == NULL, 1);
    close(fds[0]);
    close(fds[1]);
}

[[cccc::test]]
void test_libc_socket_in_bounds(void) {
    char a[8];
    int  sv[2];
    AssertEq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    AssertEq(send(sv[0], "abcd", 4, 0), 4);
    AssertEq(recv(sv[1], a, sizeof a, 0), 4);
    close(sv[0]);
    close(sv[1]);
}

static int vformat(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

[[cccc::test]]
void test_libc_remaining_in_bounds(void) {
    char a[16];
    int  sv[2];
    AssertEq(vformat(a, sizeof a, "%d", 7), 1);
    AssertEq(gethostname(a, sizeof a), 0);
    readlink("/tmp", a, sizeof a);
    FILE *f = tmpfile();
    AssertEq(pwrite(fileno(f), "abcd", 4, 0), 4);
    AssertEq(pread(fileno(f), a, sizeof a, 0), 4);
    fclose(f);
    AssertEq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    AssertEq(sendto(sv[0], "abcd", 4, 0, NULL, 0), 4);
    AssertEq(recvfrom(sv[1], a, sizeof a, 0, NULL, NULL), 4);
    close(sv[0]);
    close(sv[1]);
}

[[cccc::test]]
void test_libc_unchecked_pointers_untouched(void) {
    char  big[32];
    char *raw = big;
    memset(raw, 0, 32);
    AssertEq(raw[31], 0);
}

// ---------------------------------------------------------------------
// Out-of-bounds calls trap.
// ---------------------------------------------------------------------

[[cccc::test(exit_code = 255)]]
void test_libc_memcpy_dest_oob(void) {
    char            a[8], b[16] = {0};
    volatile size_t n = 16;
    memcpy(a, b, n);
}

[[cccc::test(exit_code = 255)]]
void test_libc_memcpy_src_oob(void) {
    char            a[16], b[8] = {0};
    volatile size_t n = 16;
    memcpy(a, b, n);
}

[[cccc::test(exit_code = 255)]]
void test_libc_memset_oob(void) {
    char            a[8];
    volatile size_t n = 9;
    memset(a, 0, n);
}

[[cccc::test(exit_code = 255)]]
void test_libc_snprintf_oob(void) {
    char            a[8];
    volatile size_t n = 16;
    snprintf(a, n, "x");
}

[[cccc::test(exit_code = 255)]]
void test_libc_fread_oob(void) {
    char            a[8];
    FILE           *f    = tmpfile();
    volatile size_t size = 2;
    fread(a, size, 8, f);
}

[[cccc::test(exit_code = 255)]]
void test_libc_fgets_oob(void) {
    char         a[8];
    FILE        *f = tmpfile();
    volatile int n = 16;
    fgets(a, n, f);
}

[[cccc::test(exit_code = 255)]]
void test_libc_read_oob(void) {
    char a[8];
    int  fds[2];
    pipe(fds);
    volatile size_t n = 16;
    read(fds[0], a, n);
}

[[cccc::test(exit_code = 255)]]
void test_libc_write_oob(void) {
    char a[8] = {0};
    int  fds[2];
    pipe(fds);
    volatile size_t n = 16;
    write(fds[1], a, n);
}

[[cccc::test(exit_code = 255)]]
void test_libc_getcwd_oob(void) {
    char            a[8];
    volatile size_t n = 4096;
    getcwd(a, n);
}

[[cccc::test(exit_code = 255)]]
void test_libc_recv_oob(void) {
    char a[8];
    int  sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    volatile size_t n = 16;
    recv(sv[1], a, n, 0);
}

[[cccc::test(exit_code = 255)]]
void test_libc_checked_pointer_arg_oob(void) {
    char *[[cccc::array, cccc::count(4)]] p = (char[4]){0};
    char            b[16]                   = {0};
    volatile size_t n                       = 8;
    memcpy(p, b, n);
}

// ---------------------------------------------------------------------
// Fortified spellings and builtins keep their bounds.
// ---------------------------------------------------------------------

[[cccc::test]]
void test_libc_builtin_chk_in_bounds(void) {
    char a[8], b[16] = "abc";
    __builtin___memcpy_chk(a, b, sizeof a, __builtin_object_size(a, 0));
    __builtin___memset_chk(a, 'x', sizeof a, __builtin_object_size(a, 0));
    AssertEq(a[7], 'x');
    __builtin___snprintf_chk(a, sizeof a, 0, __builtin_object_size(a, 0), "%d",
                             123);
    AssertEq(strcmp(a, "123"), 0);
    __builtin___strncpy_chk(a, b, sizeof a, __builtin_object_size(a, 0));
    AssertEq(strcmp(a, "abc"), 0);
}

[[cccc::test(exit_code = 255)]]
void test_libc_builtin_chk_memcpy_dest_oob(void) {
    char            a[8], b[16] = {0};
    volatile size_t n = 16;
    __builtin___memcpy_chk(a, b, n, (size_t)-1);
}

[[cccc::test(exit_code = 255)]]
void test_libc_builtin_chk_memset_oob(void) {
    char            a[8];
    volatile size_t n = 16;
    __builtin___memset_chk(a, 0, n, (size_t)-1);
}

[[cccc::test(exit_code = 255)]]
void test_libc_builtin_chk_snprintf_oob(void) {
    char            a[8];
    volatile size_t n = 16;
    __builtin___snprintf_chk(a, n, 0, (size_t)-1, "%d", 1);
}

[[cccc::test(exit_code = 255)]]
void test_libc_builtin_chk_strncpy_dest_oob(void) {
    char            a[8], b[16] = "abc";
    volatile size_t n = 16;
    __builtin___strncpy_chk(a, b, n, (size_t)-1);
}

[[cccc::test(exit_code = 255)]]
void test_libc_builtin_memcpy_oob(void) {
    char            a[8], b[16] = {0};
    volatile size_t n = 16;
    __builtin_memcpy(a, b, n);
}

[[cccc::test(exit_code = 255)]]
void test_libc_builtin_memset_oob(void) {
    char            a[8];
    volatile size_t n = 16;
    __builtin_memset(a, 0, n);
}

[[cccc::test(exit_code = 255)]]
void test_libc_strncpy_dest_oob(void) {
    char            a[8], b[16] = "abc";
    volatile size_t n = 16;
    strncpy(a, b, n);
}

[[cccc::test]]
void test_libc_strncpy_in_bounds(void) {
    char a[8], b[16] = "abc";
    strncpy(a, b, sizeof a);
    AssertEq(strcmp(a, "abc"), 0);
}
