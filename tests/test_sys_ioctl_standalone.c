// Regression test for #792: ioctl() is declared in include/sys/ioctl.h but
// was never registered via cc_register_cfunc anywhere, so a guest program
// including ONLY <sys/ioctl.h> (no unistd.h/sys/socket.h to trigger
// register_posix_functions some other way) got "undefined function: ioctl".
// Fixed by registering ioctl (variadic) in register_posix_functions and
// adding the sys/ioctl.h row to tools/stdlib.tsv.
//
// Also covers #795's request-code allowlist (wrap_ioctl in
// src/stdlib/posix.c): deliberately runs *without* --posix-emulation (no
// CCCC_FLAGS line below), unlike tests/suites/test_suite_posix.c which
// always sets that flag and so can only exercise the raw-passthrough
// escape hatch, not the default rejection path this test checks.
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void) {
    // TIOCGWINSZ: allowlisted. stdout is not guaranteed to be a tty under
    // the test runner, so this asserts only that the call resolves and
    // returns without crashing -- success is not the thing under test here
    // (that would fail in CI for the wrong reason). The guest/host
    // `struct winsize` layout is verified to match on both macOS and Linux
    // (4x unsigned short, 8 bytes).
    struct winsize ws;
    ws.ws_row = 0;
    ws.ws_col = 0;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    int r = ioctl(1, TIOCGWINSZ, &ws);
    (void)r; // resolution + no crash is what's under test, not the result

    // FIONREAD: also allowlisted (int* arg, no layout risk). On a regular
    // file this always succeeds and reports the remaining byte count.
    int fd = open("/tmp/cccc-ioctl-allowlist-test", O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) return 1;
    if (write(fd, "abc", 3) != 3) return 2;
    if (lseek(fd, 0, SEEK_SET) != 0) return 3;
    int avail = -1;
    if (ioctl(fd, FIONREAD, &avail) != 0) return 4;
    if (avail != 3) return 5;

    // A raw, unverified request code must be rejected -- not forwarded to
    // the host -- since --posix-emulation was not passed. errno == EINVAL
    // and no crash/corruption, regardless of what the host's real ioctl()
    // would have done with this code.
    errno = 0;
    int dummy = 0;
    int rc = ioctl(fd, 0x12345678, &dummy);
    if (rc != -1) return 6;
    if (errno != EINVAL) return 7;

    close(fd);
    unlink("/tmp/cccc-ioctl-allowlist-test");

    return 42;
}
