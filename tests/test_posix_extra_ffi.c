// Regression test for #590: additional POSIX FFI functions registered for the
// SQLite unix VFS (fchmod, geteuid, pread, pwrite, getpagesize, nanosleep, ...).
//
// Exercises the newly-registered functions in a portable way and returns 42.

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(void) {
    // getpagesize: must be a positive power-of-two-ish value
    if (getpagesize() <= 0)
        return 1;

    // geteuid: effective uid is non-negative (uid_t); just confirm it is callable
    (void)geteuid();

    // pread / pwrite round-trip through a temp file
    char tmpl[] = "/tmp/cccc_posix_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0)
        return 2;

    const char *msg = "hello";
    if (pwrite(fd, msg, 5, 0) != 5)
        return 3;

    // fchmod the temp fd
    if (fchmod(fd, 0600) != 0)
        return 4;

    char buf[8] = {0};
    if (pread(fd, buf, 5, 0) != 5)
        return 5;
    if (memcmp(buf, "hello", 5) != 0)
        return 6;

    close(fd);
    unlink(tmpl);

    // nanosleep: a tiny, harmless sleep
    struct timespec req = { 0, 1000000 }; // 1 ms
    if (nanosleep(&req, 0) != 0)
        return 7;

    return 42;
}
