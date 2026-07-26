// Regression test for #792: <sys/uio.h> did not exist prior to this fix --
// struct iovec/readv/writev lived only in include/unistd.h, so a guest
// program could not include the POSIX-mandated <sys/uio.h> at all. It is
// now its own header (include/sys/uio.h), with unistd.h re-including it for
// backward compatibility.
//
// Includes ONLY <sys/uio.h> (declaring pipe()/close() itself, rather than
// pulling in unistd.h/fcntl.h, which are already-registered headers that
// would mask a broken sys/uio.h row) to prove reg_fn_for_header("sys/uio.h")
// on its own is enough for readv/writev to resolve and work.
#include <sys/uio.h>

extern int pipe(int fds[2]);
extern int close(int fd);

int main(void) {
    int fds[2];
    if (pipe(fds) != 0) return 1;

    char msg1[] = "hi";
    char msg2[] = "!!";
    struct iovec wv[2] = {
        {msg1, 2},
        {msg2, 2},
    };
    if (writev(fds[1], wv, 2) != 4) return 2;

    char buf[4] = {0};
    struct iovec rv[1] = {{buf, 4}};
    if (readv(fds[0], rv, 1) != 4) return 3;
    if (buf[0] != 'h' || buf[1] != 'i' || buf[2] != '!' || buf[3] != '!') return 4;

    close(fds[0]);
    close(fds[1]);
    return 42;
}
