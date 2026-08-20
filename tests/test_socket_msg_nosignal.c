// #814 audit: sys/socket.h's MSG_NOSIGNAL was previously __linux__-only,
// with a comment claiming macOS has no equivalent -- that's not true of
// modern macOS SDKs (verified against MacOSX14/14.5/15/15.5), which do
// define it. This exercises the actual behavior it promises (no SIGPIPE
// on send() to a socket whose peer closed its read end) on both platforms,
// not just that the macro compiles.
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    signal(SIGPIPE, SIG_DFL);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return 1;

    close(sv[1]); /* peer's read end is gone */

    char    byte = 'x';
    ssize_t n    = send(sv[0], &byte, 1, MSG_NOSIGNAL);
    if (n != -1)
        return 2; /* should fail (EPIPE), just not via a signal */

    close(sv[0]);
    return 42;
}
