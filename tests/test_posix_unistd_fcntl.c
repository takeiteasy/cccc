#include <fcntl.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/jcc-posix-test-XXXXXX";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) return 1;

    char msg[] = "abc";
    if (write(fd, msg, 3) != 3) return 2;
    if (lseek(fd, 0, SEEK_SET) != 0) return 3;

    char buf[4] = {0};
    if (read(fd, buf, 3) != 3) return 4;
    if (buf[0] != 'a' || buf[1] != 'b' || buf[2] != 'c') return 5;

    if (close(fd) != 0) return 6;
    if (unlink(path) != 0) return 7;

    return 42;
}
