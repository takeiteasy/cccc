// O_PATH (<fcntl.h>) and syscall() (<unistd.h>) are Linux-only and are
// declared and registered only under __linux__, like mremap/splice.
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef __linux__
#if defined(__aarch64__)
#define NR_GETPID 172
#elif defined(__x86_64__)
#define NR_GETPID 39
#endif

int main(void) {
    // An O_PATH descriptor exists only to name the file: read() on it fails.
    int fd = open("/", O_PATH | O_DIRECTORY);
    if (fd < 0)
        return 1;
    char c;
    if (read(fd, &c, 1) != -1 || errno != EBADF)
        return 2;
    close(fd);

    if (syscall(NR_GETPID) != (long)getpid())
        return 3;
    return 42;
}
#else
int main(void) {
    return 42;
}
#endif
