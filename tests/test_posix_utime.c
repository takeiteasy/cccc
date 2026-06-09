#include <utime.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/cccc-utime-test-XXXXXX";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) return 1;
    close(fd);

    struct utimbuf times;
    times.actime = 1234567890;
    times.modtime = 1234567890;
    if (utime(path, &times) != 0) return 2;

    struct stat st;
    if (stat(path, &st) != 0) return 3;
    if (st.st_atimespec.tv_sec != 1234567890) return 4;
    if (st.st_mtimespec.tv_sec != 1234567890) return 5;

    unlink(path);
    return 42;
}
