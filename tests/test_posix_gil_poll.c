// Expected return: 42
// Verify that poll() releases the VM GIL while blocked.
// Thread A blocks in poll() on the read end of a pipe (1-second timeout).
// Thread B must acquire the GIL to write to the write end.
// Without GIL release: A holds the GIL during poll() -> B can never run ->
//   poll() times out -> poller returns 1 -> main returns 6.
// With GIL release: A releases GIL -> B runs and writes -> poll() returns
//   immediately with data -> poller returns 0 -> main returns 42.
#include <pthread.h>
#include <poll.h>
#include <unistd.h>

static int fds[2];

static void *poller(void *arg) {
    (void)arg;
    struct pollfd pfd;
    pfd.fd      = fds[0];
    pfd.events  = POLLIN;
    pfd.revents = 0;
    int r       = poll(&pfd, 1, 1000);
    return (void *)(long long)(r > 0 ? 0 : 1);
}

static void *writer(void *arg) {
    (void)arg;
    char c = 'x';
    write(fds[1], &c, 1);
    return 0;
}

int main(void) {
    if (pipe(fds) != 0)
        return 1;
    pthread_t t1, t2;
    if (pthread_create(&t1, 0, poller, 0) != 0)
        return 2;
    if (pthread_create(&t2, 0, writer, 0) != 0)
        return 3;
    void *r1 = 0;
    if (pthread_join(t1, &r1) != 0)
        return 4;
    if (pthread_join(t2, 0) != 0)
        return 5;
    close(fds[0]);
    close(fds[1]);
    return r1 == 0 ? 42 : 6;
}
