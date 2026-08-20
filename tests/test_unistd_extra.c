// Expected return: 42
// #409: previously-unregistered unistd.h functions -- process groups,
// alarm, dup, and scatter/gather I/O (readv/writev).
#include <string.h>
#include <unistd.h>

int main(void) {
    pid_t pg = getpgrp();
    if (getpgid(0) != pg)
        return 1;

    // alarm(0) cancels any pending alarm and returns the previous
    // remaining time, which is 0 since none was set.
    unsigned int prev = alarm(0);
    if (prev != 0)
        return 2;

    int fd = dup(STDOUT_FILENO);
    if (fd < 0)
        return 3;
    close(fd);

    int fds[2];
    if (pipe(fds) != 0)
        return 4;

    const char  *msg1 = "hello ";
    const char  *msg2 = "world";
    struct iovec wiov[2];
    wiov[0].iov_base = (void *)msg1;
    wiov[0].iov_len  = strlen(msg1);
    wiov[1].iov_base = (void *)msg2;
    wiov[1].iov_len  = strlen(msg2);

    ssize_t wrote    = writev(fds[1], wiov, 2);
    if (wrote != (ssize_t)(strlen(msg1) + strlen(msg2)))
        return 5;

    char         buf1[6];
    char         buf2[5];
    struct iovec riov[2];
    riov[0].iov_base = buf1;
    riov[0].iov_len  = sizeof(buf1);
    riov[1].iov_base = buf2;
    riov[1].iov_len  = sizeof(buf2);

    ssize_t got      = readv(fds[0], riov, 2);
    if (got != 11)
        return 6;
    if (strncmp(buf1, "hello ", 6) != 0)
        return 7;
    if (strncmp(buf2, "world", 5) != 0)
        return 8;

    close(fds[0]);
    close(fds[1]);

    return 42;
}
