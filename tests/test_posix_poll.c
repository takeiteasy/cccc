#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    int fd[2];
    if (pipe(fd) != 0) return 1;

    char msg[] = "x";
    write(fd[1], msg, 1);

    struct pollfd pfd = { fd[0], POLLIN, 0 };
    int r = poll(&pfd, 1, 1000);
    if (r != 1) return 2;
    if (!(pfd.revents & POLLIN)) return 3;

    close(fd[0]);
    close(fd[1]);
    return 42;
}
