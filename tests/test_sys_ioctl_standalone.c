// Regression test for #792: ioctl() is declared in include/sys/ioctl.h but
// was never registered via cc_register_cfunc anywhere, so a guest program
// including ONLY <sys/ioctl.h> (no unistd.h/sys/socket.h to trigger
// register_posix_functions some other way) got "undefined function: ioctl".
// Fixed by registering ioctl (variadic) in register_posix_functions and
// adding the sys/ioctl.h row to tools/stdlib.tsv.
//
// stdout is not guaranteed to be a tty under the test runner, so this
// asserts only that the call resolves and returns without crashing --
// TIOCGWINSZ succeeding is not the thing under test here (that would fail
// in CI for the wrong reason). The guest/host `struct winsize` layout is
// verified to match on macOS (4x unsigned short, 8 bytes); ioctl itself is
// a general passthrough, so requests other than TIOCGWINSZ/TIOCSWINSZ are
// not layout-guaranteed (see the registration comment in src/stdlib/posix.c).
#include <sys/ioctl.h>

int main(void) {
    struct winsize ws;
    ws.ws_row = 0;
    ws.ws_col = 0;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    int r = ioctl(1, TIOCGWINSZ, &ws);
    (void)r; // resolution + no crash is what's under test, not the result

    return 42;
}
