#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/cccc-stat-test-XXXXXX";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) return 1;

    if (write(fd, "abc", 3) != 3) return 2;
    close(fd);

    struct stat st;
    if (stat(path, &st) != 0) return 3;
    if (st.st_size != 3) return 4;
    if (!S_ISREG(st.st_mode)) return 5;

    if (chmod(path, S_IRUSR) != 0) return 6;
    if (stat(path, &st) != 0) return 7;
    if ((st.st_mode & S_IRWXU) != S_IRUSR) return 8;
    if (st.st_mode & S_IWUSR) return 9;

    unlink(path);

    if (mkdir("/tmp/cccc-stat-dir-test", S_IRWXU) != 0) return 10;
    if (stat("/tmp/cccc-stat-dir-test", &st) != 0) return 11;
    if (!S_ISDIR(st.st_mode)) return 12;
    rmdir("/tmp/cccc-stat-dir-test");

    return 42;
}
