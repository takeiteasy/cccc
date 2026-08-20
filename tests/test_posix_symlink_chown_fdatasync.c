// Regression test for #783: chown, fdatasync, and symlink were declared
// with real prototypes in include/unistd.h but never registered via
// cc_register_cfunc anywhere, so calling them from guest code failed to
// resolve. Found while auditing src/stdlib/*.c FFI registrations for
// #777 (tools/audit_ffi.py flagged them as declared-but-unregistered).
//
// fdatasync is POSIX but genuinely absent from Darwin's libc (no
// syscall/library symbol at all -- unlike fsync), so both its guest-side
// declaration (include/unistd.h) and its registration (src/stdlib/
// posix.c) are #ifdef __linux__-guarded, matching the existing
// mremap/splice pattern for Linux-only functions (#729, #731). Skipped
// entirely on non-Linux here rather than tested.
//
// chown's success path needs elevated privileges to change ownership to
// an arbitrary uid/gid, but chowning a file to the *calling* process's
// own euid/egid is specifically permitted for any owner (POSIX), so that
// is what's exercised here rather than requiring root or tolerating
// EPERM.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    const char *target = "/tmp/cccc_test_symlink_chown_target";
    const char *link   = "/tmp/cccc_test_symlink_chown_link";

    unlink(target);
    unlink(link);

    int fd = open(target, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return 1;
    if (write(fd, "hi", 2) != 2)
        return 2;
    close(fd);

    // symlink + readlink round-trip.
    if (symlink(target, link) != 0)
        return 3;

    char buf[256];
    memset(buf, 0, sizeof(buf));
    ssize_t n = readlink(link, buf, sizeof(buf) - 1);
    if (n < 0)
        return 4;
    if (strcmp(buf, target) != 0)
        return 5;

    struct stat st;
    if (lstat(link, &st) != 0)
        return 6;
    if (!S_ISLNK(st.st_mode))
        return 7;

    // chown: self-chown (own euid/egid) is always permitted.
    if (chown(target, geteuid(), getegid()) != 0)
        return 8;

    fd = open(target, O_WRONLY);
    if (fd < 0)
        return 9;
    if (write(fd, "!!", 2) != 2)
        return 10;

#ifdef __linux__
    if (fdatasync(fd) != 0)
        return 11;
#endif
    close(fd);

    unlink(link);
    unlink(target);

    return 42;
}
