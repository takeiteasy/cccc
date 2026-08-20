// Expected return: 42
// #779 regression guard: guest EAGAIN must match the value the host kernel
// actually returns. A non-blocking read on an empty pipe is a real syscall
// round-trip through the host, so this only catches a host/guest mismatch
// (it passes vacuously on platforms where the two constants already agree).
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    int fds[2];
    if (pipe(fds) != 0)
        return 1;
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0)
        return 2;

    char buf[1];
    errno     = 0;
    ssize_t n = read(fds[0], buf, sizeof(buf));
    if (n != -1)
        return 3;
    if (errno != EAGAIN)
        return 4;

    close(fds[0]);
    close(fds[1]);
    return 42;
}
